/*
 * qr_scanner_display.c — 摄像采集 + LVDS实时显示 + 二维码识别
 *
 * 纯内核 ioctl，不依赖 libdrm.so。
 *
 * 交叉编译：
 *   aarch64-linux-gnu-gcc -std=c11 -O2 -Wall -D_GNU_SOURCE \
 *     -I lib/quirc \
 *     src/qr_scanner_display.c lib/quirc/*.c \
 *     -lm -o build_arm/qr_scanner_display
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>

#include "quirc.h"

/* libdrm constants not in kernel uapi headers */
#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif
#ifndef DRM_MODE_CONNECTOR_LVDS
#define DRM_MODE_CONNECTOR_LVDS 15
#endif

/* ── 配置 ────────────────────────────────────────────────────────── */

#define CAMERA_DEV      "/dev/video-camera0"
#define DRM_DEV         "/dev/dri/card0"
#define DEFAULT_WIDTH   640
#define DEFAULT_HEIGHT  480
#define MAX_BUFFERS     4
#define LVDS_CONNECTOR_ID  154

/* ── 全局变量 ────────────────────────────────────────────────────── */

static volatile int g_running = 1;

/* V4L2 */
static int            g_fd = -1;
static void          *g_buf_ptr[MAX_BUFFERS];
static unsigned int   g_buf_len[MAX_BUFFERS];
static unsigned int   g_nbufs    = 0;
static unsigned int   g_y_stride = 0;
static unsigned int   g_uv_stride = 0;

/* DRM */
static int            g_drm_fd      = -1;
static uint32_t       g_drm_fb_id   = 0;
static uint32_t       g_drm_handle  = 0;
static uint32_t       g_drm_pitch   = 0;
static uint64_t       g_drm_size    = 0;
static void          *g_drm_map     = MAP_FAILED;
static uint32_t       g_drm_crtc_id = 0;
static uint32_t       g_drm_conn_id = 0;
static int            g_drm_w = 0, g_drm_h = 0;

static void sig_handler(int sig) { (void)sig; g_running = 0; }

/* ── 工具函数 ────────────────────────────────────────────────────── */

static int xioctl(int fd, unsigned long r, void *a)
{
    int v;
    do { v = ioctl(fd, r, a); } while (v == -1 && errno == EINTR);
    return v;
}
static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static inline int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ── NV12 → XRGB8888 软转 ────────────────────────────────────────── */

static void nv12_to_xrgb(uint8_t *y, uint8_t *uv,
                          int w, int h, int ys, int uvs,
                          uint32_t *dst, int ds)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int Y = y[j * ys + i];
            int U = uv[(j / 2) * uvs + (i & ~1)];
            int V = uv[(j / 2) * uvs + (i & ~1) + 1];
            int C = Y - 16, D = U - 128, E = V - 128;
            int R = clamp_int((298 * C + 409 * E + 128) >> 8, 0, 255);
            int G = clamp_int((298 * C - 100 * D - 208 * E + 128) >> 8, 0, 255);
            int B = clamp_int((298 * C + 516 * D + 128) >> 8, 0, 255);
            dst[j * ds + i] = 0xFF000000U | (R << 16) | (G << 8) | B;
        }
    }
}

/* ── 画空心矩形 ──────────────────────────────────────────────────── */

static void draw_rect_rgb(uint32_t *fb, int fw, int fh,
                          int rx, int ry, int rw, int rh,
                          uint32_t color, int t)
{
    int x0 = clamp_int(rx, 0, fw - 1);
    int y0 = clamp_int(ry, 0, fh - 1);
    int x1 = clamp_int(rx + rw, 0, fw - 1);
    int y1 = clamp_int(ry + rh, 0, fh - 1);
    for (int k = 0; k < t; k++) {
        for (int x = x0; x <= x1; x++) {
            if (y0 + k < fh) fb[(y0 + k) * fw + x] = color;
            if (y1 - k >= 0)  fb[(y1 - k) * fw + x] = color;
        }
        for (int y = y0; y <= y1; y++) {
            if (x0 + k < fw) fb[y * fw + (x0 + k)] = color;
            if (x1 - k >= 0)  fb[y * fw + (x1 - k)] = color;
        }
    }
}

/* ── DRM 显示初始化 (纯 ioctl, 无需 libdrm) ───────────────────────── */

static int drm_display_init(int cam_w, int cam_h)
{
    int fd = open(DRM_DEV, O_RDWR);
    if (fd < 0) { perror("open drm"); return -1; }
    g_drm_fd = fd;

    /* 获取资源列表 — 先获取数量，再获取 ID */
    uint32_t fb_id_arr[64], crtc_arr[16], conn_arr[32], enc_arr[32];
    struct drm_mode_card_res res = {0};
    res.fb_id_ptr        = (uint64_t)(uintptr_t)fb_id_arr;
    res.crtc_id_ptr      = (uint64_t)(uintptr_t)crtc_arr;
    res.connector_id_ptr = (uint64_t)(uintptr_t)conn_arr;
    res.encoder_id_ptr   = (uint64_t)(uintptr_t)enc_arr;
    res.count_fbs        = 64;
    res.count_crtcs      = 16;
    res.count_connectors = 32;
    res.count_encoders   = 32;

    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        perror("MODE_GETRESOURCES"); return -1;
    }
    printf("[drm] resources: crtcs=%u connectors=%u encoders=%u\n",
           res.count_crtcs, res.count_connectors, res.count_encoders);

    /* 找 connector */
    uint32_t best_conn = 0, best_enc = 0, best_crtc = 0;
    int best_w = 0, best_h = 0, best_refresh = 0;
    int best_score = -1;
    struct drm_mode_modeinfo best_mode = {0};

    for (unsigned int ci = 0; ci < res.count_connectors; ci++) {
        struct drm_mode_get_connector conn = {0};
        conn.connector_id = conn_arr[ci];

        uint32_t mode_arr[256], prop_arr[128], enc_arr2[16];
        conn.modes_ptr       = (uint64_t)(uintptr_t)mode_arr;
        conn.props_ptr       = (uint64_t)(uintptr_t)prop_arr;
        conn.prop_values_ptr = (uint64_t)(uintptr_t)prop_arr;
        conn.encoders_ptr    = (uint64_t)(uintptr_t)enc_arr2;
        conn.count_modes     = 256;
        conn.count_props     = 128;
        conn.count_encoders  = 16;

        if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;
        if (conn.connection != DRM_MODE_CONNECTED || conn.count_modes == 0) continue;

        /* prioritize LVDS connector 154, then any LVDS, then any connected */
        int priority = 0;
        if (conn.connector_id == LVDS_CONNECTOR_ID) priority = 3;
        else if (conn.connector_type == DRM_MODE_CONNECTOR_LVDS) priority = 2;
        else priority = 1;

        /* get modes */
        struct drm_mode_modeinfo modes[128];
        conn.modes_ptr       = (uint64_t)(uintptr_t)modes;
        conn.count_modes     = conn.count_modes > 128 ? 128 : conn.count_modes;
        conn.props_ptr       = (uint64_t)(uintptr_t)prop_arr;
        conn.prop_values_ptr = (uint64_t)(uintptr_t)prop_arr;
        conn.encoders_ptr    = (uint64_t)(uintptr_t)enc_arr2;
        if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;

        for (unsigned int mi = 0; mi < conn.count_modes; mi++) {
            int score = 0;
            if ((unsigned int)modes[mi].hdisplay == (unsigned int)cam_w &&
                (unsigned int)modes[mi].vdisplay == (unsigned int)cam_h) score += 100;
            if (priority > 2)
                score += priority * 1000 + modes[mi].vrefresh;

            if (score > 0 || best_conn == 0) {
                /* Find first acceptable mode */
            }
            /* pick best based on priority and resolution match */
            int use = 0;
            if (!best_conn) use = 1;
            else if (priority > 0 && modes[mi].hdisplay == (unsigned int)cam_w &&
                     modes[mi].vdisplay == (unsigned int)cam_h) use = 1;
            else if (!best_w) use = 1;

            if (use) {
                /* Pick best encoder for this connector */
                uint32_t enc_for_conn[16];
                conn.encoders_ptr = (uint64_t)(uintptr_t)enc_for_conn;
                conn.count_encoders = conn.count_encoders > 16 ? 16 : conn.count_encoders;
                conn.modes_ptr    = 0;
                conn.props_ptr    = 0;
                conn.prop_values_ptr = 0;
                if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;

                for (unsigned int ei = 0; ei < conn.count_encoders; ei++) {
                    struct drm_mode_get_encoder enc = {0};
                    enc.encoder_id = enc_for_conn[ei];
                    if (ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) < 0) continue;

                    best_conn = conn.connector_id;
                    best_enc  = enc.encoder_id;
                    best_crtc = enc.crtc_id;
                    best_mode = modes[mi];
                    best_w = modes[mi].hdisplay;
                    best_h = modes[mi].vdisplay;
                    best_refresh = modes[mi].vrefresh;
                    goto found;
                }
            }
        }
    }
found:

    if (!best_conn) {
        fprintf(stderr, "No connected display found\n");
        return -1;
    }

    g_drm_conn_id = best_conn;
    g_drm_crtc_id = best_crtc;
    g_drm_w = best_w;
    g_drm_h = best_h;
    printf("[drm] Connector %u, CRTC %u, Mode %dx%d@%dHz\n",
           best_conn, best_crtc, best_w, best_h, best_refresh);

    /* 创建 dumb buffer */
    struct drm_mode_create_dumb cd = {0};
    cd.width  = (uint32_t)best_w;
    cd.height = (uint32_t)best_h;
    cd.bpp    = 32;
    if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) {
        perror("CREATE_DUMB"); return -1;
    }
    g_drm_handle = cd.handle;
    g_drm_pitch  = cd.pitch;
    g_drm_size   = cd.size;

    /* Add framebuffer */
    {
        struct drm_mode_fb_cmd fb = {0};
        fb.width  = (uint32_t)best_w;
        fb.height = (uint32_t)best_h;
        fb.pitch  = g_drm_pitch;
        fb.bpp    = 32;
        fb.depth  = 24;
        fb.handle = g_drm_handle;
        if (ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fb) < 0) {
            /* Try with DRM_FORMAT_XRGB8888 via ADDFB2 */
            uint32_t handles[4] = {g_drm_handle, 0, 0, 0};
            uint32_t pitches[4] = {g_drm_pitch, 0, 0, 0};
            uint32_t offsets[4] = {0, 0, 0, 0};
            struct drm_mode_fb_cmd2 fb2 = {0};
            fb2.width        = best_w;
            fb2.height       = best_h;
            fb2.pixel_format = DRM_FORMAT_XRGB8888;
            fb2.handles[0]   = handles[0];
            fb2.handles[1]   = handles[1];
            fb2.handles[2]   = handles[2];
            fb2.handles[3]   = handles[3];
            fb2.pitches[0]   = pitches[0];
            fb2.pitches[1]   = pitches[1];
            fb2.pitches[2]   = pitches[2];
            fb2.pitches[3]   = pitches[3];
            fb2.offsets[0]   = offsets[0];
            fb2.offsets[1]   = offsets[1];
            fb2.offsets[2]   = offsets[2];
            fb2.offsets[3]   = offsets[3];
            if (ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &fb2) < 0) {
                perror("ADDFB/ADDFB2"); return -1;
            }
            g_drm_fb_id = fb2.fb_id;
        } else {
            g_drm_fb_id = fb.fb_id;
        }
    }

    /* mmap dumb buffer */
    struct drm_mode_map_dumb md = {0};
    md.handle = g_drm_handle;
    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md) < 0) {
        perror("MAP_DUMB"); return -1;
    }
    g_drm_map = mmap(NULL, g_drm_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, md.offset);
    if (g_drm_map == MAP_FAILED) { perror("mmap fb"); return -1; }

    /* 设为 CRTC 输出 */
    if (ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &((struct drm_mode_crtc){
        .set_connectors_ptr = (uint64_t)(uintptr_t)&g_drm_conn_id,
        .count_connectors = 1,
        .crtc_id = g_drm_crtc_id,
        .fb_id = g_drm_fb_id,
        .x = 0, .y = 0,
        .mode_valid = 1,
        .mode = best_mode
    })) < 0) {
        perror("SETCRTC"); return -1;
    }

    printf("[drm] Display init OK. FB %dx%d, pitch=%u\n",
           best_w, best_h, g_drm_pitch);
    return 0;
}

static void drm_display_cleanup(void)
{
    if (g_drm_map && g_drm_map != MAP_FAILED) {
        munmap(g_drm_map, g_drm_size);
    }
    if (g_drm_fb_id && g_drm_fd >= 0) {
        ioctl(g_drm_fd, DRM_IOCTL_MODE_RMFB, &g_drm_fb_id);
    }
    if (g_drm_handle && g_drm_fd >= 0) {
        struct drm_mode_destroy_dumb dd = {.handle = g_drm_handle};
        ioctl(g_drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
    }
    if (g_drm_fd >= 0) close(g_drm_fd);
    g_drm_fd = -1;
}

/* ── 摄像头操作 ──────────────────────────────────────────────────── */

static int camera_open(const char *dev, unsigned int *w, unsigned int *h)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    unsigned int i;

    g_fd = open(dev, O_RDWR);
    if (g_fd < 0) { perror("open camera"); return -1; }

    memset(&cap, 0, sizeof(cap));
    if (xioctl(g_fd, VIDIOC_QUERYCAP, &cap) < 0) { perror("QUERYCAP"); return -1; }
    printf("[camera] %s\n", cap.card);

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width  = *w;
    fmt.fmt.pix_mp.height = *h;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field  = V4L2_FIELD_NONE;
    if (xioctl(g_fd, VIDIOC_S_FMT, &fmt) < 0) { perror("S_FMT"); return -1; }
    *w = fmt.fmt.pix_mp.width;
    *h = fmt.fmt.pix_mp.height;
    g_y_stride  = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    g_uv_stride = fmt.fmt.pix_mp.plane_fmt[1].bytesperline;
    printf("[camera] NV12 %ux%u stride Y:%u UV:%u\n", *w, *h, g_y_stride, g_uv_stride);

    memset(&req, 0, sizeof(req));
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count  = MAX_BUFFERS;
    if (xioctl(g_fd, VIDIOC_REQBUFS, &req) < 0) { perror("REQBUFS"); return -1; }
    g_nbufs = req.count;

    for (i = 0; i < g_nbufs; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane  planes[2];
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        buf.m.planes = planes;
        buf.length = 2;
        if (xioctl(g_fd, VIDIOC_QUERYBUF, &buf) < 0) { perror("QUERYBUF"); return -1; }

        unsigned int total = planes[0].length + planes[1].length;
        g_buf_len[i] = total;
        g_buf_ptr[i] = mmap(NULL, total, PROT_READ | PROT_WRITE,
                            MAP_SHARED, g_fd, planes[0].m.mem_offset);
        if (g_buf_ptr[i] == MAP_FAILED) { perror("mmap vb"); return -1; }
    }
    return 0;
}

static int camera_stream_on(void)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    for (unsigned int i = 0; i < g_nbufs; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane  planes[2];
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        buf.m.planes = planes;
        buf.length = 2;
        if (xioctl(g_fd, VIDIOC_QBUF, &buf) < 0) { perror("QBUF"); return -1; }
    }
    return xioctl(g_fd, VIDIOC_STREAMON, &type);
}

static void camera_stream_off(void)
{
    enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    xioctl(g_fd, VIDIOC_STREAMOFF, &t);
}

static void camera_close(void)
{
    if (g_fd < 0) return;
    for (unsigned int i = 0; i < g_nbufs; i++)
        if (g_buf_ptr[i] && g_buf_ptr[i] != MAP_FAILED)
            munmap(g_buf_ptr[i], g_buf_len[i]);
    close(g_fd);
    g_fd = -1;
}

static int camera_grab(unsigned int *idx, uint8_t **py, unsigned int *yl,
                        uint8_t **puv, unsigned int *uvl)
{
    struct v4l2_buffer buf;
    struct v4l2_plane  planes[2];
    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length = 2;
    if (xioctl(g_fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EINTR) return -1;
        perror("DQBUF"); return -1;
    }
    *idx = buf.index;
    *py  = (uint8_t *)g_buf_ptr[buf.index];
    *yl  = planes[0].bytesused;
    *puv = (uint8_t *)g_buf_ptr[buf.index] + planes[0].length;
    *uvl = planes[1].bytesused;
    return 0;
}

static int camera_release(unsigned int idx)
{
    struct v4l2_buffer buf;
    struct v4l2_plane  planes[2];
    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = idx;
    buf.m.planes = planes;
    buf.length = 2;
    return xioctl(g_fd, VIDIOC_QBUF, &buf);
}

/* ── main ────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    const char *device = CAMERA_DEV;
    unsigned int cam_w = DEFAULT_WIDTH, cam_h = DEFAULT_HEIGHT;
    int continuous = 0, opt;

    while ((opt = getopt(argc, argv, "d:W:H:ch")) != -1) {
        switch (opt) {
        case 'd': device = optarg; break;
        case 'W': cam_w = (unsigned int)atoi(optarg); break;
        case 'H': cam_h = (unsigned int)atoi(optarg); break;
        case 'c': continuous = 1; break;
        default:
            printf("Usage: %s [-d dev] [-W w] [-H h] [-c]\n", argv[0]);
            return 1;
        }
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (camera_open(device, &cam_w, &cam_h) < 0) return 1;

    struct quirc *qr = quirc_new();
    if (!qr || quirc_resize(qr, cam_w, cam_h) < 0) {
        fprintf(stderr, "quirc init failed\n"); return 1;
    }

    int have_display = (drm_display_init(cam_w, cam_h) == 0);
    if (camera_stream_on() < 0) return 1;

    printf("\n[scanner] %ux%u | display:%s | quirc QR\n"
           "  Put QR code in front of camera. Ctrl+C to stop.\n\n",
           cam_w, cam_h, have_display ? "LVDS" : "OFF");
    fflush(stdout);

    unsigned int frame = 0, found = 0;
    int64_t t0 = now_ms();
    char last[256] = "";

    while (g_running) {
        unsigned int idx, yl, uvl;
        uint8_t *yp, *uvp;
        if (camera_grab(&idx, &yp, &yl, &uvp, &uvl) < 0) {
            if (!g_running) break;
            continue;
        }
        frame++;

        /* LVDS 显示 */
        if (have_display) {
            nv12_to_xrgb(yp, uvp, cam_w, cam_h,
                         g_y_stride, g_uv_stride,
                         (uint32_t *)g_drm_map,
                         g_drm_pitch / 4);
        }

        /* QR 检测 */
        int qw, qh;
        uint8_t *qb = quirc_begin(qr, &qw, &qh);
        if (qb && qw == (int)cam_w && qh == (int)cam_h) {
            memcpy(qb, yp, cam_w * cam_h);
            quirc_end(qr);

            for (int i = 0; i < quirc_count(qr); i++) {
                struct quirc_code code;
                struct quirc_data data;
                quirc_extract(qr, i, &code);
                if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
                    if (strcmp((char *)data.payload, last) != 0) {
                        printf("\n>>> QR CODE: %s <<<\n", data.payload);
                        printf("    ver:%d ecc:%c\n",
                               data.version, "MLHQ"[data.ecc_level]);
                        fflush(stdout);

                        if (have_display) {
                            int cx0 = code.corners[0].x, cy0 = code.corners[0].y;
                            int cx2 = code.corners[2].x, cy2 = code.corners[2].y;
                            int rx = clamp_int(cx0 < cx2 ? cx0 : cx2, 0, g_drm_w);
                            int ry = clamp_int(cy0 < cy2 ? cy0 : cy2, 0, g_drm_h);
                            int rw = abs(cx2 - cx0), rh = abs(cy2 - cy0);
                            draw_rect_rgb((uint32_t *)g_drm_map, g_drm_w, g_drm_h,
                                          rx, ry, rw, rh, 0xFF00FF00, 3);
                        }

                        strncpy(last, (char *)data.payload, sizeof(last) - 1);
                        found++;
                        if (!continuous) g_running = 0;
                    }
                }
            }
        }

        camera_release(idx);

        if (frame % 30 == 0) {
            int64_t e = now_ms() - t0;
            printf("[scanner] %u fr | %.1ffps | %d QR    \r",
                   frame, frame * 1000.0f / (float)e, found);
            fflush(stdout);
        }
    }

    camera_stream_off();
    camera_close();
    drm_display_cleanup();
    quirc_destroy(qr);

    int64_t e = now_ms() - t0;
    printf("\nDone. %u frames %.1fs (%.1ffps) | %d QR found.\n",
           frame, e / 1000.0, frame * 1000.0f / (float)e, found);
    return (found > 0) ? 0 : 2;
}
