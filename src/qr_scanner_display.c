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
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <linux/fb.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>

#include "quirc.h"
#include "retail_ui_assets.h"

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
#define FB_DEV          "/dev/fb0"
#define DEFAULT_WIDTH   640
#define DEFAULT_HEIGHT  480
#define MAX_BUFFERS     4
#define LVDS_CONNECTOR_ID  154
#define CAMERA_X        24
#define CAMERA_Y        86
#define MAX_CART_ITEMS 16
#define VOICE_STATE_FILE "/tmp/qsm_retail_voice_state"
#define PAYMENT_WAIT_FILE "/tmp/qsm_payment_waiting_method"
#define PAYMENT_DONE_FILE "/tmp/qsm_payment_done"
#define PAYMENT_FINISHED_VOICE_FILE "/tmp/qsm_payment_finished_voice"
#define VOICE_HISTORY_LINES 4
#define VOICE_HISTORY_TEXT_MAX 192
#define QR_PRODUCT_ADD_COOLDOWN_MS 1800
#define QR_SCAN_PAUSE_AFTER_ADD_MS 1800
#define QR_SHIFTED_CROP_COUNT 8
#define PAYMENT_QR_W 360
#define PAYMENT_QR_H 480
#define PAYMENT_POPUP_MS 30000
#define PAYMENT_WECHAT_BGRA "/userdata/Embed_project/assets/payment_wechat.bgra"
#define PAYMENT_ALIPAY_BGRA "/userdata/Embed_project/assets/payment_alipay.bgra"

/* ── 全局变量 ────────────────────────────────────────────────────── */

static volatile int g_running = 1;

/* V4L2 */
static int            g_fd = -1;
static void          *g_y_ptr[MAX_BUFFERS];
static void          *g_uv_ptr[MAX_BUFFERS];
static unsigned int   g_y_len[MAX_BUFFERS];
static unsigned int   g_uv_len[MAX_BUFFERS];
static unsigned int   g_nbufs    = 0;
static unsigned int   g_y_stride = 0;
static unsigned int   g_uv_stride = 0;
static unsigned int   g_cam_h = 0;

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
static void          *g_ui_map      = NULL;
static int            g_ui_w = 0, g_ui_h = 0;
static int            g_ui_pitch = 0;
static int            g_ui_rotate_landscape = 0;
static int            g_last_total_cents = 0;
static char           g_order_id[48] = "";
static char           g_payment_url[160] = "PAY: scan checkout";
static int            g_checkout_requested = 0;
static char           g_payment_method[16] = "";
static int64_t        g_payment_popup_until_ms = 0;
static int            g_stdin_closed = 0;
static uint32_t      *g_payment_wechat_bgra = NULL;
static uint32_t      *g_payment_alipay_bgra = NULL;
static time_t         g_voice_state_mtime = 0;
static char           g_voice_question[160] = "等待唤醒词";
static char           g_voice_answer[160] = "请说小智小智后提问";
static char           g_voice_history[VOICE_HISTORY_LINES][VOICE_HISTORY_TEXT_MAX] = {
    "AI：可说：小智小智，结账",
    "AI：可说：微信支付、支付宝支付",
    "AI：可说：把牛奶加入购物车",
    "",
};
struct retail_product;

struct retail_cart_line {
    const struct retail_product *product;
    int quantity;
};

static struct retail_cart_line g_cart[MAX_CART_ITEMS];
static int g_cart_count = 0;

static void sig_handler(int sig) { (void)sig; g_running = 0; }

struct qr_crop_region {
    int crop_percent;
    int offset_x_percent;
    int offset_y_percent;
};

static const struct qr_crop_region g_qr_shifted_crops[QR_SHIFTED_CROP_COUNT] = {
    {76, -10,   0},
    {76,  10,   0},
    {76,   0, -10},
    {76,   0,  10},
    {68, -14, -10},
    {68,  14, -10},
    {68, -14,  10},
    {68,  14,  10},
};

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

static void copy_luma_for_qr(uint8_t *dst, const uint8_t *src,
                             int width, int height, int stride)
{
    for (int y = 0; y < height; y++) {
        memcpy(dst + y * width, src + y * stride, width);
    }
}

static void enhance_luma_for_qr(uint8_t *dst, const uint8_t *src,
                                int width, int height, int stride)
{
    unsigned int hist[256];
    int total = width * height;
    int low_target = total / 50;
    int high_target = total - low_target;
    int cumulative = 0;
    int lo = 0;
    int hi = 255;
    memset(hist, 0, sizeof(hist));

    for (int y = 0; y < height; y++) {
        const uint8_t *row = src + y * stride;
        for (int x = 0; x < width; x++) {
            hist[row[x]]++;
        }
    }
    for (int i = 0; i < 256; i++) {
        cumulative += (int)hist[i];
        if (cumulative >= low_target) {
            lo = i;
            break;
        }
    }
    cumulative = 0;
    for (int i = 0; i < 256; i++) {
        cumulative += (int)hist[i];
        if (cumulative >= high_target) {
            hi = i;
            break;
        }
    }
    if (hi - lo < 32) {
        lo = clamp_int(lo - 24, 0, 255);
        hi = clamp_int(hi + 24, 0, 255);
    }
    if (hi <= lo) {
        copy_luma_for_qr(dst, src, width, height, stride);
        return;
    }

    for (int y = 0; y < height; y++) {
        const uint8_t *row = src + y * stride;
        uint8_t *out = dst + y * width;
        for (int x = 0; x < width; x++) {
            int v = (row[x] - lo) * 255 / (hi - lo);
            v = clamp_int(v, 0, 255);
            if (v < 42) v = 0;
            else if (v > 213) v = 255;
            out[x] = (uint8_t)v;
        }
    }
}

static void sharpen_luma_for_qr(uint8_t *dst, uint8_t *tmp, int width, int height)
{
    memcpy(tmp, dst, (size_t)width * (size_t)height);
    for (int y = 1; y < height - 1; y++) {
        uint8_t *out = dst + y * width;
        const uint8_t *mid = tmp + y * width;
        const uint8_t *up = tmp + (y - 1) * width;
        const uint8_t *down = tmp + (y + 1) * width;
        for (int x = 1; x < width - 1; x++) {
            int v = 5 * mid[x] - mid[x - 1] - mid[x + 1] - up[x] - down[x];
            out[x] = (uint8_t)clamp_int(v, 0, 255);
        }
    }
}

static void crop_enhance_luma_for_qr(uint8_t *dst, const uint8_t *src,
                                     int width, int height, int stride,
                                     int crop_percent)
{
    int crop_w = width * crop_percent / 100;
    int crop_h = height * crop_percent / 100;
    int crop_x = (width - crop_w) / 2;
    int crop_y = (height - crop_h) / 2;
    uint8_t *tmp = malloc((size_t)width * (size_t)height);
    if (crop_w <= 0 || crop_h <= 0) {
        copy_luma_for_qr(dst, src, width, height, stride);
        return;
    }
    for (int y = 0; y < height; y++) {
        int sy = crop_y + (int)((int64_t)y * crop_h / height);
        uint8_t *out = dst + y * width;
        const uint8_t *row = src + sy * stride;
        for (int x = 0; x < width; x++) {
            int sx = crop_x + (int)((int64_t)x * crop_w / width);
            out[x] = row[sx];
        }
    }
    enhance_luma_for_qr(dst, dst, width, height, width);
    if (tmp) {
        sharpen_luma_for_qr(dst, tmp, width, height);
        free(tmp);
    }
}

/* ── NV12 → XRGB8888 软转 ────────────────────────────────────────── */

static void crop_region_enhance_luma_for_qr(uint8_t *dst, const uint8_t *src,
                                            int width, int height, int stride,
                                            const struct qr_crop_region *region)
{
    int crop_percent = region ? region->crop_percent : 88;
    int offset_x_percent = region ? region->offset_x_percent : 0;
    int offset_y_percent = region ? region->offset_y_percent : 0;
    int crop_w = width * crop_percent / 100;
    int crop_h = height * crop_percent / 100;
    int crop_x = (width - crop_w) / 2 + width * offset_x_percent / 100;
    int crop_y = (height - crop_h) / 2 + height * offset_y_percent / 100;
    uint8_t *tmp = malloc((size_t)width * (size_t)height);

    if (crop_w <= 0 || crop_h <= 0) {
        copy_luma_for_qr(dst, src, width, height, stride);
        return;
    }
    crop_x = clamp_int(crop_x, 0, width - crop_w);
    crop_y = clamp_int(crop_y, 0, height - crop_h);

    for (int y = 0; y < height; y++) {
        int sy = crop_y + (int)((int64_t)y * crop_h / height);
        uint8_t *out = dst + y * width;
        const uint8_t *row = src + sy * stride;
        for (int x = 0; x < width; x++) {
            int sx = crop_x + (int)((int64_t)x * crop_w / width);
            out[x] = row[sx];
        }
    }
    enhance_luma_for_qr(dst, dst, width, height, width);
    if (tmp) {
        sharpen_luma_for_qr(dst, tmp, width, height);
        free(tmp);
    }
}

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

static void y_to_xrgb_scaled(uint8_t *y, int src_w, int src_h, int ys,
                             uint32_t *dst, int dst_w, int dst_h, int ds)
{
    for (int j = 0; j < dst_h; j++) {
        int sy = (int)((int64_t)j * src_h / dst_h);
        uint8_t *src_row = y + sy * ys;
        uint32_t *dst_row = dst + j * ds;
        for (int i = 0; i < dst_w; i++) {
            int sx = (int)((int64_t)i * src_w / dst_w);
            uint8_t v = src_row[sx];
            dst_row[i] = 0xFF000000U | ((uint32_t)v << 16) | ((uint32_t)v << 8) | v;
        }
    }
}

static void nv12_to_xrgb_scaled(uint8_t *y, uint8_t *uv,
                                int src_w, int src_h, int ys, int uvs,
                                uint32_t *dst, int dst_w, int dst_h, int ds)
{
    for (int j = 0; j < dst_h; j++) {
        int sy = (int)((int64_t)j * src_h / dst_h);
        uint8_t *y_row = y + sy * ys;
        uint8_t *uv_row = uv + (sy / 2) * uvs;
        uint32_t *dst_row = dst + j * ds;
        for (int i = 0; i < dst_w; i++) {
            int sx = (int)((int64_t)i * src_w / dst_w);
            int uvx = sx & ~1;
            int Y = y_row[sx];
            int U = uv_row[uvx];
            int V = uv_row[uvx + 1];
            int C = Y - 16;
            int D = U - 128;
            int E = V - 128;
            int R = clamp_int((298 * C + 409 * E + 128) >> 8, 0, 255);
            int G = clamp_int((298 * C - 100 * D - 208 * E + 128) >> 8, 0, 255);
            int B = clamp_int((298 * C + 516 * D + 128) >> 8, 0, 255);
            dst_row[i] = 0xFF000000U | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
        }
    }
}

static void nv12_to_xrgb_scaled_rect(uint8_t *y, uint8_t *uv,
                                     int src_w, int src_h, int ys, int uvs,
                                     uint32_t *dst, int dst_w, int dst_h, int ds,
                                     int dx, int dy, int dw, int dh)
{
    int x0 = clamp_int(dx, 0, dst_w - 1);
    int y0 = clamp_int(dy, 0, dst_h - 1);
    int x1 = clamp_int(dx + dw, 0, dst_w);
    int y1 = clamp_int(dy + dh, 0, dst_h);
    int out_w = x1 - x0;
    int out_h = y1 - y0;
    if (out_w <= 0 || out_h <= 0) return;

    for (int j = 0; j < out_h; j++) {
        int sy = (int)((int64_t)j * src_h / out_h);
        uint8_t *y_row = y + sy * ys;
        uint8_t *uv_row = uv + (sy / 2) * uvs;
        uint32_t *dst_row = dst + (y0 + j) * ds + x0;
        for (int i = 0; i < out_w; i++) {
            int sx = (int)((int64_t)i * src_w / out_w);
            int uvx = sx & ~1;
            int Y = y_row[sx];
            int U = uv_row[uvx];
            int V = uv_row[uvx + 1];
            int C = Y - 16;
            int D = U - 128;
            int E = V - 128;
            int R = clamp_int((298 * C + 409 * E + 128) >> 8, 0, 255);
            int G = clamp_int((298 * C - 100 * D - 208 * E + 128) >> 8, 0, 255);
            int B = clamp_int((298 * C + 516 * D + 128) >> 8, 0, 255);
            dst_row[i] = 0xFF000000U | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
        }
    }
}

static void rotate_landscape_to_portrait(const uint32_t *src,
                                         int src_w, int src_h,
                                         uint32_t *dst,
                                         int dst_w, int dst_h, int dst_pitch)
{
    if (src_w != dst_h || src_h != dst_w) return;
    for (int y = 0; y < src_h; y++) {
        const uint32_t *src_row = src + y * src_w;
        for (int x = 0; x < src_w; x++) {
            int px = dst_w - 1 - y;
            int py = x;
            dst[py * dst_pitch + px] = src_row[x];
        }
    }
}

static int display_prepare_landscape_canvas(void)
{
    if (!g_drm_map || g_drm_map == MAP_FAILED || g_drm_w <= 0 || g_drm_h <= 0) {
        return -1;
    }

    if (g_drm_h > g_drm_w) {
        g_ui_w = g_drm_h;
        g_ui_h = g_drm_w;
        g_ui_pitch = g_ui_w;
        g_ui_rotate_landscape = 1;
        g_ui_map = calloc((size_t)g_ui_w * (size_t)g_ui_h, sizeof(uint32_t));
        if (!g_ui_map) {
            perror("calloc landscape canvas");
            return -1;
        }
    } else {
        g_ui_w = g_drm_w;
        g_ui_h = g_drm_h;
        g_ui_pitch = g_drm_pitch / 4;
        g_ui_rotate_landscape = 0;
        g_ui_map = g_drm_map;
    }
    return 0;
}

static void display_flush_landscape_canvas(void)
{
    if (!g_ui_map || !g_drm_map || g_drm_map == MAP_FAILED) return;
    if (g_ui_rotate_landscape) {
        rotate_landscape_to_portrait((const uint32_t *)g_ui_map,
                                     g_ui_w, g_ui_h,
                                     (uint32_t *)g_drm_map,
                                     g_drm_w, g_drm_h,
                                     g_drm_pitch / 4);
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

static void fill_rect_rgb(uint32_t *fb, int fw, int fh,
                          int rx, int ry, int rw, int rh,
                          uint32_t color)
{
    int x0 = clamp_int(rx, 0, fw - 1);
    int y0 = clamp_int(ry, 0, fh - 1);
    int x1 = clamp_int(rx + rw, 0, fw);
    int y1 = clamp_int(ry + rh, 0, fh);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            fb[y * fw + x] = color;
        }
    }
}

static const uint8_t *font5x7_rows(char c)
{
    static const uint8_t sp[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t qm[7] = {14, 17, 1, 2, 4, 0, 4};
    static const uint8_t colon[7] = {0, 4, 4, 0, 4, 4, 0};
    static const uint8_t dot[7] = {0, 0, 0, 0, 0, 6, 6};
    static const uint8_t slash[7] = {1, 2, 2, 4, 8, 8, 16};
    static const uint8_t dash[7] = {0, 0, 0, 31, 0, 0, 0};
    static const uint8_t zero[7] = {14, 17, 19, 21, 25, 17, 14};
    static const uint8_t one[7] = {4, 12, 4, 4, 4, 4, 14};
    static const uint8_t two[7] = {14, 17, 1, 2, 4, 8, 31};
    static const uint8_t three[7] = {30, 1, 1, 14, 1, 1, 30};
    static const uint8_t four[7] = {2, 6, 10, 18, 31, 2, 2};
    static const uint8_t five[7] = {31, 16, 16, 30, 1, 1, 30};
    static const uint8_t six[7] = {14, 16, 16, 30, 17, 17, 14};
    static const uint8_t seven[7] = {31, 1, 2, 4, 8, 8, 8};
    static const uint8_t eight[7] = {14, 17, 17, 14, 17, 17, 14};
    static const uint8_t nine[7] = {14, 17, 17, 15, 1, 1, 14};
    static const uint8_t A[7] = {14, 17, 17, 31, 17, 17, 17};
    static const uint8_t B[7] = {30, 17, 17, 30, 17, 17, 30};
    static const uint8_t C[7] = {14, 17, 16, 16, 16, 17, 14};
    static const uint8_t D[7] = {30, 17, 17, 17, 17, 17, 30};
    static const uint8_t E[7] = {31, 16, 16, 30, 16, 16, 31};
    static const uint8_t F[7] = {31, 16, 16, 30, 16, 16, 16};
    static const uint8_t G[7] = {14, 17, 16, 23, 17, 17, 14};
    static const uint8_t H[7] = {17, 17, 17, 31, 17, 17, 17};
    static const uint8_t I[7] = {14, 4, 4, 4, 4, 4, 14};
    static const uint8_t J[7] = {7, 2, 2, 2, 18, 18, 12};
    static const uint8_t K[7] = {17, 18, 20, 24, 20, 18, 17};
    static const uint8_t L[7] = {16, 16, 16, 16, 16, 16, 31};
    static const uint8_t M[7] = {17, 27, 21, 21, 17, 17, 17};
    static const uint8_t N[7] = {17, 25, 21, 19, 17, 17, 17};
    static const uint8_t O[7] = {14, 17, 17, 17, 17, 17, 14};
    static const uint8_t P[7] = {30, 17, 17, 30, 16, 16, 16};
    static const uint8_t Q[7] = {14, 17, 17, 17, 21, 18, 13};
    static const uint8_t R[7] = {30, 17, 17, 30, 20, 18, 17};
    static const uint8_t S[7] = {15, 16, 16, 14, 1, 1, 30};
    static const uint8_t T[7] = {31, 4, 4, 4, 4, 4, 4};
    static const uint8_t U[7] = {17, 17, 17, 17, 17, 17, 14};
    static const uint8_t V[7] = {17, 17, 17, 17, 17, 10, 4};
    static const uint8_t W[7] = {17, 17, 17, 21, 21, 21, 10};
    static const uint8_t X[7] = {17, 17, 10, 4, 10, 17, 17};
    static const uint8_t Y[7] = {17, 17, 10, 4, 4, 4, 4};
    static const uint8_t Z[7] = {31, 1, 2, 4, 8, 16, 31};

    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    switch (c) {
    case ' ': return sp; case '?': return qm; case ':': return colon;
    case '.': return dot; case '/': return slash; case '-': return dash;
    case '0': return zero; case '1': return one; case '2': return two;
    case '3': return three; case '4': return four; case '5': return five;
    case '6': return six; case '7': return seven; case '8': return eight;
    case '9': return nine; case 'A': return A; case 'B': return B;
    case 'C': return C; case 'D': return D; case 'E': return E;
    case 'F': return F; case 'G': return G; case 'H': return H;
    case 'I': return I; case 'J': return J; case 'K': return K;
    case 'L': return L; case 'M': return M; case 'N': return N;
    case 'O': return O; case 'P': return P; case 'Q': return Q;
    case 'R': return R; case 'S': return S; case 'T': return T;
    case 'U': return U; case 'V': return V; case 'W': return W;
    case 'X': return X; case 'Y': return Y; case 'Z': return Z;
    default: return qm;
    }
}

static void draw_char_rgb(uint32_t *fb, int fw, int fh,
                          int x, int y, char c, int scale, uint32_t color)
{
    const uint8_t *rows = font5x7_rows(c);
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (rows[row] & (1U << (4 - col))) {
                fill_rect_rgb(fb, fw, fh,
                              x + col * scale, y + row * scale,
                              scale, scale, color);
            }
        }
    }
}

static void draw_text_rgb(uint32_t *fb, int fw, int fh,
                          int x, int y, const char *text,
                          int scale, uint32_t color)
{
    int cursor = x;
    while (*text) {
        draw_char_rgb(fb, fw, fh, cursor, y, *text, scale, color);
        cursor += 6 * scale;
        text++;
    }
}

static void draw_text_shadow_rgb(uint32_t *fb, int fw, int fh,
                                 int x, int y, const char *text,
                                 int scale, uint32_t color)
{
    draw_text_rgb(fb, fw, fh, x + scale, y + scale, text, scale, 0xFF000000);
    draw_text_rgb(fb, fw, fh, x, y, text, scale, color);
}

static uint32_t utf8_next_codepoint(const char **text)
{
    const unsigned char *s = (const unsigned char *)(*text);
    uint32_t cp;
    if (s[0] < 0x80) {
        *text += 1;
        return s[0];
    }
    if ((s[0] & 0xE0) == 0xC0 && s[1]) {
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
        *text += 2;
        return cp;
    }
    if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
        cp = ((uint32_t)(s[0] & 0x0F) << 12) |
             ((uint32_t)(s[1] & 0x3F) << 6) |
             (uint32_t)(s[2] & 0x3F);
        *text += 3;
        return cp;
    }
    *text += 1;
    return '?';
}

static const struct ui_glyph24 *find_ui_glyph(uint32_t codepoint)
{
    for (size_t i = 0; i < UI_GLYPH_COUNT; i++) {
        if (UI_GLYPHS[i].codepoint == codepoint) return &UI_GLYPHS[i];
    }
    return NULL;
}

static int draw_codepoint_rgb(uint32_t *fb, int fw, int fh,
                              int x, int y, uint32_t cp,
                              int scale, uint32_t color)
{
    if (cp < 0x80) {
        draw_char_rgb(fb, fw, fh, x, y + scale * 2, (char)cp, scale, color);
        return 6 * scale;
    }
    const struct ui_glyph24 *glyph = find_ui_glyph(cp);
    if (!glyph) {
        return 10 * scale;
    }
    int pixel = scale;
    for (int row = 0; row < UI_GLYPH_H; row++) {
        uint32_t bits = glyph->rows[row];
        for (int col = 0; col < UI_GLYPH_W; col++) {
            if (bits & (1U << (UI_GLYPH_W - 1 - col))) {
                fill_rect_rgb(fb, fw, fh,
                              x + col * pixel,
                              y + row * pixel,
                              pixel, pixel, color);
            }
        }
    }
    return (UI_GLYPH_W + 2) * pixel;
}

static void draw_text_utf8_rgb(uint32_t *fb, int fw, int fh,
                               int x, int y, const char *text,
                               int scale, uint32_t color)
{
    int cursor = x;
    while (*text) {
        uint32_t cp = utf8_next_codepoint(&text);
        cursor += draw_codepoint_rgb(fb, fw, fh, cursor, y, cp, scale, color);
    }
}

static void draw_text_utf8_shadow_rgb(uint32_t *fb, int fw, int fh,
                                      int x, int y, const char *text,
                                      int scale, uint32_t color)
{
    draw_text_utf8_rgb(fb, fw, fh, x + scale, y + scale, text, scale, 0xFF000000);
    draw_text_utf8_rgb(fb, fw, fh, x, y, text, scale, color);
}

static int utf8_codepoint_width(uint32_t cp, int scale)
{
    if (cp < 0x80) return 6 * scale;
    if (!find_ui_glyph(cp)) return 6 * scale;
    return (UI_GLYPH_W + 2) * scale;
}

static int draw_utf8_segment_rgb(uint32_t *fb, int fw, int fh,
                                 int x, int y,
                                 const char *start, const char *end,
                                 int scale, uint32_t color)
{
    char buf[160];
    size_t len = (size_t)(end - start);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';
    draw_text_utf8_rgb(fb, fw, fh, x, y, buf, scale, color);
    return 1;
}

static int draw_text_utf8_wrapped_rgb(uint32_t *fb, int fw, int fh,
                                      int x, int y, int max_w, int max_lines,
                                      const char *text, int scale,
                                      uint32_t color)
{
    const char *p = text;
    const char *line_start = text;
    int line_w = 0;
    int lines = 0;
    int line_h = 26 * scale;

    while (*p && lines < max_lines) {
        const char *char_start = p;
        uint32_t cp = utf8_next_codepoint(&p);
        int cw = utf8_codepoint_width(cp, scale);
        if ((cp == '\n') || (line_w > 0 && line_w + cw > max_w)) {
            draw_utf8_segment_rgb(fb, fw, fh, x, y + lines * line_h,
                                  line_start, char_start, scale, color);
            lines++;
            if (cp == '\n') {
                line_start = p;
                line_w = 0;
            } else {
                line_start = char_start;
                line_w = cw;
            }
            continue;
        }
        line_w += cw;
    }

    if (lines < max_lines && line_start < p) {
        draw_utf8_segment_rgb(fb, fw, fh, x, y + lines * line_h,
                              line_start, p, scale, color);
        lines++;
    }
    return lines;
}

static int voice_codepoint_width(uint32_t cp)
{
    if (cp < 0x80) {
        if (cp == ' ') return 12;
        if ((cp >= '0' && cp <= '9') ||
            (cp >= 'A' && cp <= 'Z') ||
            (cp >= 'a' && cp <= 'z') ||
            cp == '.') {
            return 24;
        }
        return 18;
    }
    return utf8_codepoint_width(cp, 1);
}

static uint32_t voice_fullwidth_codepoint(uint32_t cp)
{
    if (cp >= '0' && cp <= '9') return 0xFF10 + (cp - '0');
    if (cp >= 'A' && cp <= 'Z') return 0xFF21 + (cp - 'A');
    if (cp >= 'a' && cp <= 'z') return 0xFF21 + (cp - 'a');
    if (cp == '.') return 0xFF0E;
    return cp;
}

static void draw_voice_codepoint_rgb(uint32_t *fb, int fw, int fh,
                                     int x, int y, uint32_t cp,
                                     uint32_t color)
{
    if (cp < 0x80) {
        uint32_t full = voice_fullwidth_codepoint(cp);
        if ((cp >= '0' && cp <= '9') ||
            (cp >= 'A' && cp <= 'Z') ||
            (cp >= 'a' && cp <= 'z') ||
            cp == '.') {
            draw_char_rgb(fb, fw, fh, x, y - 1, (char)cp, 4, color);
            return;
        }
        if (cp == '?') return;
        if (full != cp && find_ui_glyph(full)) {
            draw_codepoint_rgb(fb, fw, fh, x, y, full, 1, color);
            return;
        }
        draw_char_rgb(fb, fw, fh, x, y + 2, (char)cp, 3, color);
        return;
    }
    draw_codepoint_rgb(fb, fw, fh, x, y, cp, 1, color);
}

static int draw_voice_utf8_segment_rgb(uint32_t *fb, int fw, int fh,
                                       int x, int y,
                                       const char *start, const char *end,
                                       uint32_t color)
{
    const char *p = start;
    int cursor = x;
    while (p < end && *p) {
        uint32_t cp = utf8_next_codepoint(&p);
        draw_voice_codepoint_rgb(fb, fw, fh, cursor, y, cp, color);
        cursor += voice_codepoint_width(cp);
    }
    return 1;
}

static int draw_voice_text_wrapped_rgb(uint32_t *fb, int fw, int fh,
                                       int x, int y, int max_w, int max_lines,
                                       const char *text, uint32_t color)
{
    const char *p = text;
    const char *line_start = text;
    int line_w = 0;
    int lines = 0;
    const int line_h = 30;

    while (*p && lines < max_lines) {
        const char *char_start = p;
        uint32_t cp = utf8_next_codepoint(&p);
        int cw = voice_codepoint_width(cp);
        if ((cp == '\n') || (line_w > 0 && line_w + cw > max_w)) {
            draw_voice_utf8_segment_rgb(fb, fw, fh, x, y + lines * line_h,
                                        line_start, char_start, color);
            lines++;
            if (cp == '\n') {
                line_start = p;
                line_w = 0;
            } else {
                line_start = char_start;
                line_w = cw;
            }
            continue;
        }
        line_w += cw;
    }

    if (lines < max_lines && line_start < p) {
        draw_voice_utf8_segment_rgb(fb, fw, fh, x, y + lines * line_h,
                                    line_start, p, color);
        lines++;
    }
    return lines;
}

static void blit_icon_rgb(uint32_t *fb, int fw, int fh,
                          int x, int y, const uint32_t *icon)
{
    if (!icon) return;
    for (int row = 0; row < UI_ICON_H; row++) {
        int yy = y + row;
        if (yy < 0 || yy >= fh) continue;
        for (int col = 0; col < UI_ICON_W; col++) {
            int xx = x + col;
            if (xx < 0 || xx >= fw) continue;
            fb[yy * fw + xx] = icon[row * UI_ICON_W + col];
        }
    }
}

static int load_bgra_asset(const char *path, uint32_t **out, int w, int h)
{
    FILE *fp;
    size_t pixels = (size_t)w * (size_t)h;
    size_t got;
    if (!path || !out) return -1;
    *out = (uint32_t *)malloc(pixels * sizeof(uint32_t));
    if (!*out) return -1;
    fp = fopen(path, "rb");
    if (!fp) {
        free(*out);
        *out = NULL;
        return -1;
    }
    got = fread(*out, sizeof(uint32_t), pixels, fp);
    fclose(fp);
    if (got != pixels) {
        free(*out);
        *out = NULL;
        return -1;
    }
    return 0;
}

static void load_payment_qr_assets(void)
{
    if (!g_payment_wechat_bgra) {
        load_bgra_asset(PAYMENT_WECHAT_BGRA, &g_payment_wechat_bgra,
                        PAYMENT_QR_W, PAYMENT_QR_H);
    }
    if (!g_payment_alipay_bgra) {
        load_bgra_asset(PAYMENT_ALIPAY_BGRA, &g_payment_alipay_bgra,
                        PAYMENT_QR_W, PAYMENT_QR_H);
    }
}

static void blit_bgra_rgb(uint32_t *fb, int fw, int fh,
                          int x, int y, int w, int h,
                          const uint32_t *pixels)
{
    if (!pixels) return;
    for (int row = 0; row < h; row++) {
        int yy = y + row;
        if (yy < 0 || yy >= fh) continue;
        for (int col = 0; col < w; col++) {
            int xx = x + col;
            if (xx < 0 || xx >= fw) continue;
            fb[yy * fw + xx] = pixels[row * w + col];
        }
    }
}

static void draw_circle_rgb(uint32_t *fb, int fw, int fh,
                            int cx, int cy, int r, uint32_t color)
{
    int rr = r * r;
    for (int y = cy - r; y <= cy + r; y++) {
        if (y < 0 || y >= fh) continue;
        for (int x = cx - r; x <= cx + r; x++) {
            if (x < 0 || x >= fw) continue;
            int dx = x - cx;
            int dy = y - cy;
            if (dx * dx + dy * dy <= rr) fb[y * fw + x] = color;
        }
    }
}

static void draw_panel_header(uint32_t *fb, int fw, int fh,
                              int x, int y, int w, const char *title,
                              uint32_t color)
{
    fill_rect_rgb(fb, fw, fh, x, y, w, 26, 0xFF102532);
    draw_rect_rgb(fb, fw, fh, x, y, w, 26, color, 1);
    draw_text_rgb(fb, fw, fh, x + 10, y + 7, title, 1, 0xFFFFFFFF);
}

struct retail_product {
    const char *id;
    const char *name;
    const char *barcode;
    int price_cents;
};

static const struct retail_product g_products[] = {
    {"water", "Water", "690100000001", 200},
    {"cola", "Cola", "690100000002", 350},
    {"milk", "Milk", "690100000003", 450},
    {"bread", "Bread", "690100000004", 500},
    {"noodle", "Instant Noodle", "690100000005", 400},
    {"chips", "Chips", "690100000006", 600},
    {"biscuit", "Biscuit", "690100000007", 550},
    {"toothpaste", "Toothpaste", "690100000008", 800},
    {"tissue", "Tissue", "690100000009", 400},
    {"soap", "Soap", "690100000010", 300},
};

static int64_t g_product_last_add_ms[sizeof(g_products) / sizeof(g_products[0])] = {0};
static int64_t g_qr_scan_resume_ms = 0;

static int ascii_lower(int c)
{
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int equals_ignore_case(const char *a, const char *b)
{
    while (*a && *b) {
        if (ascii_lower((unsigned char)*a) != ascii_lower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int retail_payload_alias_matches(const char *value, const char *product_id)
{
    if (equals_ignore_case(product_id, "water")) {
        return equals_ignore_case(value, "mineral_water") || equals_ignore_case(value, "mineral water");
    }
    if (equals_ignore_case(product_id, "noodle")) {
        return equals_ignore_case(value, "instant_noodles") || equals_ignore_case(value, "instant noodles");
    }
    if (equals_ignore_case(product_id, "biscuit")) {
        return equals_ignore_case(value, "cookies") || equals_ignore_case(value, "cookie");
    }
    return 0;
}

static int retail_product_is_enabled(const struct retail_product *product)
{
    if (!product) return 0;
    if (equals_ignore_case(product->id, "cola")) return 0;
    return 1;
}

static void retail_payload_extract_value(const char *payload, char *out, size_t out_sz)
{
    const char *value = payload;
    const char *keys[] = {
        "product=", "id=", "sku=", "barcode=",
        "\"product\"", "\"id\"", "\"sku\"", "\"barcode\""
    };

    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!payload || !payload[0]) return;

    if (strncmp(payload, "product:", 8) == 0) value = payload + 8;
    else if (strncmp(payload, "qr:", 3) == 0) value = payload + 3;
    else {
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            const char *hit = strstr(payload, keys[i]);
            if (!hit) continue;
            hit += strlen(keys[i]);
            while (*hit == ' ' || *hit == ':' || *hit == '=' || *hit == '"' || *hit == '\'') hit++;
            value = hit;
            break;
        }
    }

    size_t j = 0;
    while (value[j] && j + 1 < out_sz) {
        char c = value[j];
        if (c == '&' || c == '?' || c == '#' || c == ',' || c == ';' ||
            c == '}' || c == ']' || c == '"' || c == '\'' ||
            c == '\r' || c == '\n' || c == ' ') {
            break;
        }
        out[j] = c;
        j++;
    }
    out[j] = '\0';
}

static const struct retail_product *retail_product_from_payload(const char *payload)
{
    char extracted[96];
    const char *value = extracted;
    if (!payload || !payload[0]) return NULL;
    retail_payload_extract_value(payload, extracted, sizeof(extracted));
    if (!extracted[0]) return NULL;

    for (size_t i = 0; i < sizeof(g_products) / sizeof(g_products[0]); i++) {
        if (!retail_product_is_enabled(&g_products[i])) continue;
        if (equals_ignore_case(value, g_products[i].id) ||
            equals_ignore_case(value, g_products[i].barcode) ||
            retail_payload_alias_matches(value, g_products[i].id)) {
            return &g_products[i];
        }
    }
    return NULL;
}

static int retail_product_index(const struct retail_product *product)
{
    if (!product) return -1;
    for (size_t i = 0; i < sizeof(g_products) / sizeof(g_products[0]); i++) {
        if (product == &g_products[i]) return (int)i;
    }
    return -1;
}

static int retail_product_can_add_from_qr(const struct retail_product *product,
                                          int64_t qr_now_ms)
{
    int index = retail_product_index(product);
    if (index < 0) return 0;
    if (g_product_last_add_ms[index] > 0 &&
        qr_now_ms - g_product_last_add_ms[index] < QR_PRODUCT_ADD_COOLDOWN_MS) {
        return 0;
    }
    g_product_last_add_ms[index] = qr_now_ms;
    return 1;
}

static void retail_ensure_order_id(void);

static const uint32_t *retail_product_icon(const struct retail_product *product)
{
    if (!product) return UI_ICON_WATER;
    if (equals_ignore_case(product->id, "water")) return UI_ICON_WATER;
    if (equals_ignore_case(product->id, "cola")) return UI_ICON_COLA;
    if (equals_ignore_case(product->id, "milk")) return UI_ICON_MILK;
    if (equals_ignore_case(product->id, "bread")) return UI_ICON_BREAD;
    if (equals_ignore_case(product->id, "noodle")) return UI_ICON_NOODLE;
    if (equals_ignore_case(product->id, "chips")) return UI_ICON_CHIPS;
    if (equals_ignore_case(product->id, "biscuit")) return UI_ICON_BISCUIT;
    if (equals_ignore_case(product->id, "toothpaste")) return UI_ICON_TOOTHPASTE;
    if (equals_ignore_case(product->id, "tissue")) return UI_ICON_TISSUE;
    if (equals_ignore_case(product->id, "soap")) return UI_ICON_SOAP;
    return UI_ICON_WATER;
}

static const char *retail_product_name_cn(const struct retail_product *product)
{
    if (!product) return "商品";
    if (equals_ignore_case(product->id, "water")) return "矿泉水";
    if (equals_ignore_case(product->id, "cola")) return "可乐";
    if (equals_ignore_case(product->id, "milk")) return "牛奶";
    if (equals_ignore_case(product->id, "bread")) return "面包";
    if (equals_ignore_case(product->id, "noodle")) return "泡面";
    if (equals_ignore_case(product->id, "chips")) return "薯片";
    if (equals_ignore_case(product->id, "biscuit")) return "饼干";
    if (equals_ignore_case(product->id, "toothpaste")) return "牙膏";
    if (equals_ignore_case(product->id, "tissue")) return "纸巾";
    if (equals_ignore_case(product->id, "soap")) return "香皂";
    return product->name;
}

static void retail_cart_add_product(const struct retail_product *product)
{
    if (!product) return;
    g_checkout_requested = 0;
    retail_ensure_order_id();
    for (int i = 0; i < g_cart_count; i++) {
        if (g_cart[i].product == product) {
            g_cart[i].quantity++;
            return;
        }
    }
    if (g_cart_count < MAX_CART_ITEMS) {
        g_cart[g_cart_count].product = product;
        g_cart[g_cart_count].quantity = 1;
        g_cart_count++;
    }
}

static void retail_cart_clear(void)
{
    memset(g_cart, 0, sizeof(g_cart));
    g_cart_count = 0;
    g_order_id[0] = '\0';
}

static int retail_cart_item_count(void)
{
    int count = 0;
    for (int i = 0; i < g_cart_count; i++) count += g_cart[i].quantity;
    return count;
}

static int retail_cart_total_cents(void)
{
    int total = 0;
    for (int i = 0; i < g_cart_count; i++) {
        total += g_cart[i].product->price_cents * g_cart[i].quantity;
    }
    return total;
}

static void retail_ensure_order_id(void)
{
    if (g_order_id[0]) return;
    unsigned int seed = (unsigned int)(now_ms() ^ (int64_t)time(NULL) ^
                                       (int64_t)(uintptr_t)&seed);
    srand(seed);
    snprintf(g_order_id, sizeof(g_order_id), "%06d", 100000 + rand() % 900000);
}

static void money_text(int cents, char *out, size_t out_sz)
{
    snprintf(out, out_sz, "%d.%02d", cents / 100, cents % 100);
}

static void retail_checkout_summary(char *out, size_t out_sz)
{
    char total[24];
    money_text(retail_cart_total_cents(), total, sizeof(total));
    snprintf(out, out_sz, "PAYMENT READY: %d items total %s",
             retail_cart_item_count(), total);
}

static void replace_all_inplace(char *text, size_t text_sz,
                                const char *needle,
                                const char *replacement)
{
    char tmp[256];
    char *hit;
    size_t before_len;
    size_t needle_len;
    size_t repl_len;
    size_t tail_len;

    if (!text || !needle || !replacement || !needle[0]) return;
    needle_len = strlen(needle);
    repl_len = strlen(replacement);

    while ((hit = strstr(text, needle)) != NULL) {
        before_len = (size_t)(hit - text);
        tail_len = strlen(hit + needle_len);
        if (before_len + repl_len + tail_len >= sizeof(tmp)) {
            tail_len = sizeof(tmp) - before_len - repl_len - 1;
        }
        memcpy(tmp, text, before_len);
        memcpy(tmp + before_len, replacement, repl_len);
        memcpy(tmp + before_len + repl_len, hit + needle_len, tail_len);
        tmp[before_len + repl_len + tail_len] = '\0';
        if (text_sz > 0) {
            strncpy(text, tmp, text_sz - 1);
            text[text_sz - 1] = '\0';
        }
    }
}

static void strip_voice_role_prefix(char *text)
{
    const char *prefixes[] = {
        "问：", "答：", "客户：", "客户:", "助手：", "AI：", "AI:"
    };
    int changed = 1;
    if (!text) return;
    while (*text == ' ' || *text == '\t') {
        memmove(text, text + 1, strlen(text));
    }
    while (changed) {
        changed = 0;
        for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
            size_t len = strlen(prefixes[i]);
            if (strncmp(text, prefixes[i], len) == 0) {
                memmove(text, text + len, strlen(text + len) + 1);
                changed = 1;
                break;
            }
        }
    }
    while (*text == ' ' || *text == '\t') {
        memmove(text, text + 1, strlen(text));
    }
}

static void normalize_voice_common_sentence(char *text, size_t text_sz)
{
    if (!text || text_sz == 0 || !text[0]) return;

    if (strstr(text, "结账") || strstr(text, "结算") || strstr(text, "总共") ||
        strstr(text, "合计") || strstr(text, "应付")) {
        char total[24];
        money_text(retail_cart_total_cents(), total, sizeof(total));
        snprintf(text, text_sz, "合计%d件商品，应付%s元。请选择微信支付或支付宝支付。",
                 retail_cart_item_count(), total);
        return;
    }
    if (strstr(text, "微信") && (strstr(text, "收") || strstr(text, "码") ||
                                strstr(text, "扫码"))) {
        snprintf(text, text_sz, "已为您打开微信收款码，请扫码支付。");
        return;
    }
    if ((strstr(text, "支付宝") || strstr(text, "支付寶")) &&
        (strstr(text, "收") || strstr(text, "码") || strstr(text, "扫码"))) {
        snprintf(text, text_sz, "已为您打开支付宝收款码，请扫码支付。");
        return;
    }
    if (strstr(text, "银联") || strstr(text, "云闪付")) {
        snprintf(text, text_sz, "银联云闪付暂不可用，请选择微信支付或支付宝支付。");
        return;
    }
    if (strstr(text, "加入购物车") || strstr(text, "加入购物車") ||
        strstr(text, "added to cart")) {
        for (size_t i = 0; i < sizeof(g_products) / sizeof(g_products[0]); i++) {
            if (strstr(text, g_products[i].name) ||
                strstr(text, retail_product_name_cn(&g_products[i]))) {
                snprintf(text, text_sz, "%s已加入购物车。",
                         retail_product_name_cn(&g_products[i]));
                return;
            }
        }
    }
}

static void sanitize_voice_text(char *text, size_t text_sz)
{
    char clean[256];
    size_t j = 0;
    if (!text || text_sz == 0) return;

    strip_voice_role_prefix(text);

    replace_all_inplace(text, text_sz, "濂界殑", "好的");
    replace_all_inplace(text, text_sz, "锛屽凡涓烘偍鎵撳紑", "，已为您打开");
    replace_all_inplace(text, text_sz, "鏀浠樺疂", "支付宝");
    replace_all_inplace(text, text_sz, "鏀舵剧爜", "收款码");
    replace_all_inplace(text, text_sz, "锛岃锋壂鐮佹敮浠樸", "，请扫码支付。");
    replace_all_inplace(text, text_sz, "宸蹭负鎮ㄦ墦寮", "已为您打开");
    replace_all_inplace(text, text_sz, "宸蹭负", "已为");
    replace_all_inplace(text, text_sz, "璇锋壂鐮佹敮浠", "请扫码支付");
    replace_all_inplace(text, text_sz, "璇锋壂鐮", "请扫码");

    if (strstr(text, "支付宝") &&
        (strstr(text, "收码") || strstr(text, "收款码") || strstr(text, "扫码支付"))) {
        snprintf(text, text_sz, "已为您打开支付宝收款码，请扫码支付。");
    } else if (strstr(text, "微信") &&
               (strstr(text, "收码") || strstr(text, "收款码") || strstr(text, "扫码支付"))) {
        snprintf(text, text_sz, "已为您打开微信收款码，请扫码支付。");
    }
    normalize_voice_common_sentence(text, text_sz);

    for (size_t i = 0; text[i] && j + 1 < sizeof(clean); i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 && c != '\t') continue;
        if (c == '?') continue;
        clean[j++] = text[i];
    }
    clean[j] = '\0';
    strncpy(text, clean, text_sz - 1);
    text[text_sz - 1] = '\0';
    strip_voice_role_prefix(text);
    normalize_voice_common_sentence(text, text_sz);
}

static void utf8_safe_copy(char *dst, size_t dst_sz, const char *src)
{
    size_t i = 0;
    size_t j = 0;
    if (!dst || dst_sz == 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (src[i] && j + 1 < dst_sz) {
        unsigned char c = (unsigned char)src[i];
        size_t need = 1;
        if (c < 0x20 && c != '\t') {
            i++;
            continue;
        }
        if (c == '?') {
            i++;
            continue;
        }
        if (c < 0x80) {
            need = 1;
        } else if ((c & 0xE0) == 0xC0) {
            need = 2;
        } else if ((c & 0xF0) == 0xE0) {
            need = 3;
        } else if ((c & 0xF8) == 0xF0) {
            need = 4;
        } else {
            i++;
            continue;
        }
        if (j + need >= dst_sz) break;
        int valid = 1;
        for (size_t k = 1; k < need; k++) {
            if ((src[i + k] & 0xC0) != 0x80) {
                valid = 0;
                break;
            }
        }
        if (!valid) {
            i++;
            continue;
        }
        if (need == 3 &&
            (unsigned char)src[i] == 0xEF &&
            (unsigned char)src[i + 1] == 0xBF &&
            (unsigned char)src[i + 2] == 0xBD) {
            i += need;
            continue;
        }
        for (size_t k = 0; k < need; k++) {
            dst[j++] = src[i + k];
        }
        i += need;
    }
    dst[j] = '\0';
}

static void sanitize_voice_history_line(char *text, size_t text_sz)
{
    char clean[256];
    size_t j = 0;
    if (!text || text_sz == 0) return;
    replace_all_inplace(text, text_sz, "AI.", "AI：");
    replace_all_inplace(text, text_sz, "AI:", "AI：");
    replace_all_inplace(text, text_sz, "客户:", "客户：");
    for (size_t i = 0; text[i] && j + 1 < sizeof(clean); i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 && c != '\t') continue;
        if (c == '?') continue;
        clean[j++] = text[i];
    }
    clean[j] = '\0';
    utf8_safe_copy(text, text_sz, clean);
}

static const char *voice_line_content(const char *line,
                                      const char **label,
                                      uint32_t *color)
{
    if (!line) {
        *label = "";
        *color = 0xFFFFFFFF;
        return "";
    }
    if (strncmp(line, "AI：", strlen("AI：")) == 0 ||
        strncmp(line, "AI:", strlen("AI:")) == 0 ||
        strncmp(line, "AI.", strlen("AI.")) == 0) {
        *label = "AI：";
        *color = 0xFF52E27E;
        if (strncmp(line, "AI：", strlen("AI：")) == 0) return line + strlen("AI：");
        return line + 3;
    }
    if (strncmp(line, "客户：", strlen("客户：")) == 0 ||
        strncmp(line, "客户:", strlen("客户:")) == 0) {
        *label = "客户：";
        *color = 0xFFFFFFFF;
        return line + strlen("客户：");
    }
    *label = "";
    *color = 0xFFFFFFFF;
    return line;
}

static int draw_voice_label_rgb(uint32_t *fb, int fw, int fh,
                                int x, int y, const char *label,
    uint32_t color)
{
    if (!label || !label[0]) return 0;
    if (strcmp(label, "AI：") == 0) {
        draw_text_utf8_rgb(fb, fw, fh, x, y, "ＡＩ：", 1, color);
        return 58;
    }
    draw_text_utf8_rgb(fb, fw, fh, x, y, label, 1, color);
    return 82;
}

static void format_prefixed_text(char *out, size_t out_sz,
                                 const char *prefix,
                                 const char *text)
{
    size_t used;
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (prefix) {
        strncpy(out, prefix, out_sz - 1);
        out[out_sz - 1] = '\0';
    }
    used = strlen(out);
    if (text && used + 1 < out_sz) {
        strncpy(out + used, text, out_sz - used - 1);
        out[out_sz - 1] = '\0';
    }
}

static void text_from_line(char *out, size_t out_sz, const char *line, const char *prefix)
{
    size_t len = strlen(prefix);
    if (strncmp(line, prefix, len) != 0) return;
    snprintf(out, out_sz, "%s", line + len);
    out[strcspn(out, "\r\n")] = '\0';
}

static void retail_create_payment_order(void);
static void retail_push_voice_history(const char *line);
static void retail_hide_payment_popup(void);
static void retail_finish_payment_and_reset(void);
static void retail_show_payment_popup(const char *method);
static void retail_consume_voice_state_command(void);
static int retail_stdin_enter_pressed(void);
static int retail_payment_done_requested(void);

static void retail_apply_voice_cart_command(const char *cmd)
{
    if (!cmd || !cmd[0]) return;
    if (equals_ignore_case(cmd, "clear")) {
        retail_cart_clear();
        retail_hide_payment_popup();
        return;
    }
    if (equals_ignore_case(cmd, "checkout")) {
        char summary[120];
        g_checkout_requested = 1;
        g_last_total_cents = retail_cart_total_cents();
        retail_create_payment_order();
        retail_checkout_summary(summary, sizeof(summary));
        snprintf(g_voice_question, sizeof(g_voice_question), "CHECKOUT");
        snprintf(g_voice_answer, sizeof(g_voice_answer), "%s", summary);
        retail_push_voice_history("客户：CHECKOUT");
        format_prefixed_text(summary, sizeof(summary), "AI：", g_voice_answer);
        retail_push_voice_history(summary);
        return;
    }
    if (equals_ignore_case(cmd, "checkout_pending")) {
        g_checkout_requested = 1;
        g_last_total_cents = retail_cart_total_cents();
        retail_create_payment_order();
        g_payment_method[0] = '\0';
        g_payment_popup_until_ms = 0;
        snprintf(g_voice_question, sizeof(g_voice_question), "CHECKOUT");
        snprintf(g_voice_answer, sizeof(g_voice_answer), "请选择微信支付、支付宝支付或银联云闪付");
        retail_push_voice_history("客户：CHECKOUT");
        retail_push_voice_history("AI：请选择微信支付、支付宝支付或银联云闪付");
        return;
    }
    if (equals_ignore_case(cmd, "pay:wechat")) {
        snprintf(g_voice_question, sizeof(g_voice_question), "微信支付");
        snprintf(g_voice_answer, sizeof(g_voice_answer), "已为您打开微信收款码，请扫码支付。");
        retail_push_voice_history("客户：微信支付");
        retail_push_voice_history("AI：已为您打开微信收款码，请扫码支付。");
        retail_show_payment_popup("wechat");
        return;
    }
    if (equals_ignore_case(cmd, "pay:alipay")) {
        snprintf(g_voice_question, sizeof(g_voice_question), "支付宝支付");
        snprintf(g_voice_answer, sizeof(g_voice_answer), "已为您打开支付宝收款码，请扫码支付。");
        retail_push_voice_history("客户：支付宝支付");
        retail_push_voice_history("AI：已为您打开支付宝收款码，请扫码支付。");
        retail_show_payment_popup("alipay");
        return;
    }
    if (equals_ignore_case(cmd, "pay:unionpay")) {
        g_payment_method[0] = '\0';
        g_payment_popup_until_ms = 0;
        g_checkout_requested = 1;
        snprintf(g_voice_question, sizeof(g_voice_question), "银联云闪付");
        snprintf(g_voice_answer, sizeof(g_voice_answer), "该支付方式暂不可用");
        retail_push_voice_history("客户：银联云闪付");
        retail_push_voice_history("AI：该支付方式暂不可用");
        return;
    }
    if (strncmp(cmd, "add:", 4) == 0) {
        const struct retail_product *product = retail_product_from_payload(cmd + 4);
        retail_cart_add_product(product);
        g_checkout_requested = 0;
        retail_hide_payment_popup();
    }
}

static void retail_push_voice_history(const char *line)
{
    char clean_line[160];
    if (!line || !line[0]) return;
    snprintf(clean_line, sizeof(clean_line), "%s", line);
    sanitize_voice_history_line(clean_line, sizeof(clean_line));
    if (!clean_line[0]) return;
    for (int i = 0; i < VOICE_HISTORY_LINES - 1; i++) {
        utf8_safe_copy(g_voice_history[i], sizeof(g_voice_history[i]), g_voice_history[i + 1]);
    }
    utf8_safe_copy(g_voice_history[VOICE_HISTORY_LINES - 1],
                   sizeof(g_voice_history[VOICE_HISTORY_LINES - 1]), clean_line);
}

static void retail_consume_voice_state_command(void)
{
    unlink(VOICE_STATE_FILE);
    g_voice_state_mtime = 0;
}

static int retail_stdin_enter_pressed(void)
{
    fd_set rfds;
    struct timeval tv;
    char line[32];

    if (g_stdin_closed) return 0;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) <= 0) return 0;
    if (!FD_ISSET(STDIN_FILENO, &rfds)) return 0;
    if (!fgets(line, sizeof(line), stdin)) {
        g_stdin_closed = 1;
        return 0;
    }
    return line[0] == '\n' || line[0] == '\r' || line[0] == '\0';
}

static int retail_payment_done_requested(void)
{
    if (retail_stdin_enter_pressed()) return 1;
    if (access(PAYMENT_DONE_FILE, F_OK) == 0) {
        unlink(PAYMENT_DONE_FILE);
        return 1;
    }
    return 0;
}

static int retail_apply_voice_state(void)
{
    struct stat st;
    FILE *fp;
    char line[256];
    char question[160] = "";
    char answer[160] = "";
    char cart_cmd[80] = "";

    if (stat(VOICE_STATE_FILE, &st) < 0) return 0;
    if (st.st_mtime == g_voice_state_mtime) return 0;
    fp = fopen(VOICE_STATE_FILE, "r");
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        text_from_line(question, sizeof(question), line, "QUESTION=");
        text_from_line(answer, sizeof(answer), line, "ANSWER=");
        text_from_line(cart_cmd, sizeof(cart_cmd), line, "CART_CMD=");
    }
    fclose(fp);
    g_voice_state_mtime = st.st_mtime;
    if (question[0]) {
        char line_q[VOICE_HISTORY_TEXT_MAX];
        sanitize_voice_text(question, sizeof(question));
        snprintf(g_voice_question, sizeof(g_voice_question), "%s", question);
        format_prefixed_text(line_q, sizeof(line_q), "客户：", question);
        retail_push_voice_history(line_q);
    }
    if (answer[0]) {
        char line_a[VOICE_HISTORY_TEXT_MAX];
        sanitize_voice_text(answer, sizeof(answer));
        snprintf(g_voice_answer, sizeof(g_voice_answer), "%s", answer);
        format_prefixed_text(line_a, sizeof(line_a), "AI：", answer);
        retail_push_voice_history(line_a);
    }
    retail_apply_voice_cart_command(cart_cmd);
    if (cart_cmd[0]) {
        retail_consume_voice_state_command();
    }
    return 1;
}

static void retail_speak_text_async(const char *text)
{
    char cmd[512];
    char safe[220];
    size_t j = 0;
    if (!text || !text[0]) return;
    for (size_t i = 0; text[i] && j + 1 < sizeof(safe); i++) {
        if (text[i] == '\'' || text[i] == '\n' || text[i] == '\r') continue;
        safe[j++] = text[i];
    }
    safe[j] = '\0';
    snprintf(cmd, sizeof(cmd),
             "sh /userdata/Embed_project/scripts/run_voiceask_speaker.sh ask '%s' >/tmp/qsm_qr_speaker.log 2>&1 &",
             safe);
    system(cmd);
}

static void retail_speak_product_added(const struct retail_product *product)
{
    char text[160];
    const char *name_cn;
    if (!product) return;
    name_cn = retail_product_name_cn(product);
    snprintf(text, sizeof(text), "%s已加入购物车。", name_cn);
    retail_speak_text_async(text);
    snprintf(g_voice_question, sizeof(g_voice_question), "扫码识别：%s", name_cn);
    snprintf(g_voice_answer, sizeof(g_voice_answer), "%s已加入购物车。", name_cn);
    retail_push_voice_history("客户：扫码识别");
    format_prefixed_text(text, sizeof(text), "AI：", g_voice_answer);
    retail_push_voice_history(text);
}

static void retail_create_payment_order(void)
{
    retail_ensure_order_id();
    snprintf(g_payment_url, sizeof(g_payment_url), "order=%s&amount=%d",
             g_order_id, g_last_total_cents);
}

static void retail_hide_payment_popup(void)
{
    g_payment_method[0] = '\0';
    g_payment_popup_until_ms = 0;
    g_checkout_requested = 0;
}

static void retail_finish_payment_and_reset(void)
{
    FILE *finish_voice;
    retail_cart_clear();
    retail_hide_payment_popup();
    unlink(PAYMENT_WAIT_FILE);
    unlink(VOICE_STATE_FILE);
    g_voice_state_mtime = 0;
    g_checkout_requested = 0;
    g_last_total_cents = 0;
    g_order_id[0] = '\0';
    snprintf(g_payment_url, sizeof(g_payment_url), "PAY: scan checkout");
    snprintf(g_voice_question, sizeof(g_voice_question), "PAYMENT COMPLETE");
    snprintf(g_voice_answer, sizeof(g_voice_answer), "Cart cleared, ready for next customer.");
    retail_push_voice_history("AI：购物车已清空，欢迎继续选购。");
    finish_voice = fopen(PAYMENT_FINISHED_VOICE_FILE, "w");
    if (finish_voice) {
        fputs("payment_finished\n", finish_voice);
        fclose(finish_voice);
    }
    printf("[payment] complete; cart cleared and main UI reset\n");
    fflush(stdout);
}

static void retail_show_payment_popup(const char *method)
{
    if (!method || !method[0]) return;
    if (g_last_total_cents <= 0) {
        g_last_total_cents = retail_cart_total_cents();
    }
    if (!g_order_id[0] || !g_checkout_requested) {
        retail_create_payment_order();
    }
    g_checkout_requested = 1;
    snprintf(g_payment_method, sizeof(g_payment_method), "%s", method);
    g_payment_popup_until_ms = now_ms() + PAYMENT_POPUP_MS;
}

static void dashboard_layout(int *cam_x, int *cam_y, int *cam_w, int *cam_h,
                             int *side_x, int *side_y, int *side_w, int *side_h)
{
    int canvas_w = g_ui_w > 0 ? g_ui_w : g_drm_w;
    int canvas_h = g_ui_h > 0 ? g_ui_h : g_drm_h;
    int margin = 24;
    int top = 86;
    int right_w = 560;
    int gap = 18;

    if (canvas_w < 1180) {
        right_w = 420;
    }
    *cam_x = margin;
    *cam_y = top;
    *side_x = canvas_w - right_w - margin;
    *side_y = top;
    *side_w = right_w;
    *side_h = canvas_h - top - 42;
    *cam_w = *side_x - gap - *cam_x;
    *cam_h = canvas_h - top - 210;
    if (*cam_h > *cam_w * 3 / 4) *cam_h = *cam_w * 3 / 4;
    if (*cam_h < 340) *cam_h = 340;
}

static void draw_top_status_bar(void)
{
    uint32_t *fb = (uint32_t *)g_ui_map;
    int fw = g_ui_w;
    int fh = g_ui_h;

    fill_rect_rgb(fb, fw, fh, 0, 0, fw, 64, 0xFF081018);
    draw_rect_rgb(fb, fw, fh, 0, 0, fw - 1, 63, 0xFF123A4A, 1);
    draw_rect_rgb(fb, fw, fh, 20, 14, 32, 32, 0xFFFFFFFF, 3);
    fill_rect_rgb(fb, fw, fh, 17, 11, 8, 4, 0xFFFFFFFF);
    draw_circle_rgb(fb, fw, fh, 28, 52, 4, 0xFFFFFFFF);
    draw_circle_rgb(fb, fw, fh, 48, 52, 4, 0xFFFFFFFF);
    draw_text_utf8_shadow_rgb(fb, fw, fh, 72, 18, "智慧零售终端", 1, 0xFFFFFFFF);

    int bx = fw - 520;
    draw_text_utf8_rgb(fb, fw, fh, bx, 20, "摄像头", 1, 0xFF64D2FF);
    draw_text_rgb(fb, fw, fh, bx + 84, 22, "21fps", 2, 0xFF64D2FF);
    draw_text_utf8_rgb(fb, fw, fh, bx + 178, 20, "语音", 1, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, bx + 236, 22, "0.8s", 2, 0xFF52E27E);
    draw_circle_rgb(fb, fw, fh, bx + 330, 34, 8, 0xFF52E27E);
    draw_text_utf8_rgb(fb, fw, fh, bx + 346, 20, "结算就绪", 1, 0xFFFFFFFF);
}

static void draw_product_categories(int x, int y, int w, int h)
{
    uint32_t *fb = (uint32_t *)g_ui_map;
    int fw = g_ui_w;
    int fh = g_ui_h;
    const char *items[10] = {"水", "可乐", "牛奶", "面包", "泡面",
                             "薯片", "饼干", "牙膏", "纸巾", "香皂"};
    int cols = 5;
    int gap = 8;
    int tile_w = (w - 24 - gap * (cols - 1)) / cols;
    int tile_h = 76;
    int start_y = y + 38;

    fill_rect_rgb(fb, fw, fh, x, y, w, h, 0xFF0B202B);
    draw_rect_rgb(fb, fw, fh, x, y, w, h, 0xFF145169, 1);
    draw_text_utf8_rgb(fb, fw, fh, x + 14, y + 10, "可识别商品分类", 1, 0xFFFFFFFF);
    for (int i = 0; i < 10; i++) {
        int col = i % cols;
        int row = i / cols;
        int tx = x + 12 + col * (tile_w + gap);
        int ty = start_y + row * (tile_h + gap);
        if (ty + tile_h > y + h - 8) break;
        fill_rect_rgb(fb, fw, fh, tx, ty, tile_w, tile_h, 0xFF102D3A);
        draw_rect_rgb(fb, fw, fh, tx, ty, tile_w, tile_h, 0xFF25536A, 1);
        blit_icon_rgb(fb, fw, fh, tx + (tile_w - UI_ICON_W) / 2, ty + 6, ui_icon_by_index((size_t)i));
        draw_text_utf8_rgb(fb, fw, fh, tx + tile_w / 2 - 18, ty + tile_h - 24, items[i], 1, 0xFFFFFFFF);
    }
}

static void draw_cart_table(int x, int y, int w, int h,
                            const char *product, unsigned int qr_count)
{
    uint32_t *fb = (uint32_t *)g_ui_map;
    int fw = g_ui_w;
    int fh = g_ui_h;
    char price[24];
    char qty[16];
    char total[24];
    int visible;
    if (product && qr_count == (unsigned int)-1) {
        draw_text_rgb(fb, fw, fh, x, y, product, 1, 0xFFFFFFFF);
    }

    fill_rect_rgb(fb, fw, fh, x, y, w, h, 0xFF0B202B);
    draw_rect_rgb(fb, fw, fh, x, y, w, h, 0xFF145169, 1);
    draw_text_utf8_rgb(fb, fw, fh, x + 14, y + 12, "购物车清单", 1, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, x + 26, y + 48, "序号", 1, 0xFFB8CDD5);
    draw_text_utf8_rgb(fb, fw, fh, x + 116, y + 48, "商品", 1, 0xFFB8CDD5);
    draw_text_utf8_rgb(fb, fw, fh, x + 320, y + 48, "单价", 1, 0xFFB8CDD5);
    draw_text_utf8_rgb(fb, fw, fh, x + 412, y + 48, "数量", 1, 0xFFB8CDD5);
    draw_text_utf8_rgb(fb, fw, fh, x + w - 92, y + 48, "小计", 1, 0xFFB8CDD5);

    visible = g_cart_count < 3 ? g_cart_count : 3;
    if (visible == 0) {
        draw_text_utf8_rgb(fb, fw, fh, x + 54, y + 96,
                           "等待扫码或语音添加商品", 1, 0xFFB8CDD5);
    }
    for (int i = 0; i < visible; i++) {
        const struct retail_product *p = g_cart[i].product;
        int row_y = y + 70 + i * 46;
        char row_no[8];
        snprintf(row_no, sizeof(row_no), "%d", i + 1);
        money_text(p->price_cents, price, sizeof(price));
        snprintf(qty, sizeof(qty), "%d", g_cart[i].quantity);
        money_text(p->price_cents * g_cart[i].quantity, total, sizeof(total));

        draw_rect_rgb(fb, fw, fh, x + 10, row_y, w - 20, 42, 0xFF244B5C, 1);
        draw_text_rgb(fb, fw, fh, x + 42, row_y + 15, row_no, 2, 0xFFFFFFFF);
        blit_icon_rgb(fb, fw, fh, x + 92, row_y + 2, retail_product_icon(p));
        draw_text_utf8_rgb(fb, fw, fh, x + 166, row_y + 13,
                           retail_product_name_cn(p), 1, 0xFFFFFFFF);
        draw_text_rgb(fb, fw, fh, x + 318, row_y + 15, price, 2, 0xFFFFFFFF);
        draw_text_rgb(fb, fw, fh, x + 426, row_y + 15, qty, 2, 0xFFFFFFFF);
        draw_text_rgb(fb, fw, fh, x + w - 106, row_y + 15, total, 2, 0xFFFFFFFF);
    }

    fill_rect_rgb(fb, fw, fh, x + 10, y + h - 50, w - 20, 38, 0xFF0F2C3B);
    snprintf(qty, sizeof(qty), "%d", retail_cart_item_count());
    draw_text_utf8_rgb(fb, fw, fh, x + 44, y + h - 39, "合计：", 1, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, x + 116, y + h - 42, qty, 3, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, x + 154, y + h - 39, "件商品", 1, 0xFFFFFFFF);
    money_text(retail_cart_total_cents(), total, sizeof(total));
    draw_text_utf8_rgb(fb, fw, fh, x + 282, y + h - 39, "应付：", 1, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, x + w - 146, y + h - 42, total, 3, 0xFF52E27E);
    draw_text_utf8_rgb(fb, fw, fh, x + w - 52, y + h - 39, "元", 1, 0xFF52E27E);
    return;

    fill_rect_rgb(fb, fw, fh, x, y, w, h, 0xFF0B202B);
    draw_rect_rgb(fb, fw, fh, x, y, w, h, 0xFF145169, 1);
    draw_text_utf8_rgb(fb, fw, fh, x + 14, y + 10, "购物车清单", 1, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, x + 26, y + 44, "序号", 1, 0xFFB8CDD5);
    draw_text_utf8_rgb(fb, fw, fh, x + 116, y + 44, "商品名称", 1, 0xFFB8CDD5);
    draw_text_utf8_rgb(fb, fw, fh, x + 320, y + 44, "单价", 1, 0xFFB8CDD5);
    draw_text_utf8_rgb(fb, fw, fh, x + 412, y + 44, "数量", 1, 0xFFB8CDD5);
    draw_text_utf8_rgb(fb, fw, fh, x + w - 84, y + 44, "小计", 1, 0xFFB8CDD5);

    draw_rect_rgb(fb, fw, fh, x + 10, y + 70, w - 20, 44, 0xFF244B5C, 1);
    draw_text_rgb(fb, fw, fh, x + 42, y + 86, "1", 2, 0xFFFFFFFF);
    blit_icon_rgb(fb, fw, fh, x + 92, y + 72, UI_ICON_WATER);
    draw_text_utf8_rgb(fb, fw, fh, x + 166, y + 82, "矿泉水", 1, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, x + 334, y + 86, "2.00", 2, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, x + 448, y + 86, "1", 2, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, x + w - 70, y + 86, "2.00", 2, 0xFFFFFFFF);

    draw_rect_rgb(fb, fw, fh, x + 10, y + 120, w - 20, 44, 0xFF244B5C, 1);
    draw_text_rgb(fb, fw, fh, x + 42, y + 136, "2", 2, 0xFFFFFFFF);
    blit_icon_rgb(fb, fw, fh, x + 92, y + 122, UI_ICON_SOAP);
    draw_text_utf8_rgb(fb, fw, fh, x + 166, y + 132, "香皂", 1, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, x + 334, y + 136, "1.80", 2, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, x + 448, y + 136, "1", 2, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, x + w - 70, y + 136, "1.80", 2, 0xFFFFFFFF);

    fill_rect_rgb(fb, fw, fh, x + 10, y + h - 50, w - 20, 38, 0xFF0F2C3B);
    draw_text_utf8_rgb(fb, fw, fh, x + 54, y + h - 39, "合计：", 1, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, x + 132, y + h - 42, "2", 3, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, x + 154, y + h - 39, "件商品", 1, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, x + 278, y + h - 39, "应付金额：", 1, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, x + w - 142, y + h - 42, "3.80", 3, 0xFF52E27E);
    draw_text_utf8_rgb(fb, fw, fh, x + w - 56, y + h - 40, "元", 1, 0xFF52E27E);
}

static void draw_payment_panel(int x, int y, int w, int h, unsigned int qr_count)
{
    uint32_t *fb = (uint32_t *)g_ui_map;
    int fw = g_ui_w;
    int fh = g_ui_h;
    char amount[24];
    (void)qr_count;

    g_last_total_cents = retail_cart_total_cents();

    fill_rect_rgb(fb, fw, fh, x, y, w, h, 0xFF0B202B);
    draw_rect_rgb(fb, fw, fh, x, y, w, h, 0xFF145169, 1);
    money_text(g_last_total_cents, amount, sizeof(amount));

    fill_rect_rgb(fb, fw, fh, x + 14, y + 16, w - 286, h - 32, 0xFF0F2C3B);
    draw_rect_rgb(fb, fw, fh, x + 14, y + 16, w - 286, h - 32, 0xFF244B5C, 1);
    draw_text_utf8_rgb(fb, fw, fh, x + 28, y + 28, "支付信息", 1, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, x + 28, y + 74, "订单号：", 1, 0xFFCED7DD);
    if (g_order_id[0]) draw_text_rgb(fb, fw, fh, x + 142, y + 72, g_order_id, 2, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, x + 28, y + 116, "金额：", 1, 0xFFCED7DD);
    draw_text_rgb(fb, fw, fh, x + 118, y + 117, amount, 2, 0xFF52E27E);
    draw_text_utf8_rgb(fb, fw, fh, x + 200, y + 116, "元", 1, 0xFF52E27E);

    int bx_new = x + w - 256;
    int bw_new = 236;
    fill_rect_rgb(fb, fw, fh, bx_new, y + 24, bw_new, 36, 0xFF16A34A);
    fill_rect_rgb(fb, fw, fh, bx_new, y + 70, bw_new, 36, 0xFF0A84FF);
    fill_rect_rgb(fb, fw, fh, bx_new, y + 116, bw_new, 36, 0xFFDC2626);
    draw_text_utf8_rgb(fb, fw, fh, bx_new + 70, y + 30, "微信支付", 1, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, bx_new + 82, y + 76, "支付宝", 1, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, bx_new + 58, y + 122, "银联云闪付", 1, 0xFFFFFFFF);
    return;

    fill_rect_rgb(fb, fw, fh, x + 112, y + 18, w - 132, 112, 0xFF0B202B);
    draw_text_utf8_rgb(fb, fw, fh, x + 122, y + 26, "扫码支付", 1, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, x + 122, y + 56, "订单", 1, 0xFFCED7DD);
    draw_text_rgb(fb, fw, fh, x + 178, y + 54, "000123", 2, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, x + 122, y + 82, "请在", 1, 0xFFCED7DD);
    draw_text_rgb(fb, fw, fh, x + 178, y + 80, "60", 2, 0xFF52E27E);
    draw_text_utf8_rgb(fb, fw, fh, x + 122, y + 110, "秒内完成支付", 1, 0xFFCED7DD);

    money_text(g_last_total_cents, amount, sizeof(amount));
    fill_rect_rgb(fb, fw, fh, x + 112, y + 18, w - 132, 112, 0xFF0B202B);
    draw_text_rgb(fb, fw, fh, x + 122, y + 26,
                  g_checkout_requested ? "PAYMENT READY" : "WAIT CHECKOUT",
                  2, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, x + 122, y + 56, g_order_id, 1, 0xFFCED7DD);
    draw_text_rgb(fb, fw, fh, x + 122, y + 82, "AMOUNT", 1, 0xFFCED7DD);
    draw_text_rgb(fb, fw, fh, x + 192, y + 78, amount, 3, 0xFF52E27E);
    draw_text_rgb(fb, fw, fh, x + 122, y + 116, g_payment_url, 1, 0xFFCED7DD);

    int bx = x + w - 256;
    int bw = 236;
    fill_rect_rgb(fb, fw, fh, bx, y + 24, bw, 36, 0xFF16A34A);
    fill_rect_rgb(fb, fw, fh, bx, y + 70, bw, 36, 0xFF0A84FF);
    fill_rect_rgb(fb, fw, fh, bx, y + 116, bw, 36, 0xFFDC2626);
    draw_text_utf8_rgb(fb, fw, fh, bx + 70, y + 30, "微信支付", 1, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, bx + 82, y + 76, "支付宝", 1, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, bx + 58, y + 122, "银联云闪付", 1, 0xFFFFFFFF);
}

static void draw_payment_popup(void)
{
    uint32_t *fb = (uint32_t *)g_ui_map;
    int fw = g_ui_w;
    int fh = g_ui_h;
    char amount[24];
    int panel_w = PAYMENT_QR_W + 88;
    int panel_h = PAYMENT_QR_H + 150;
    int panel_x = (fw - panel_w) / 2;
    int panel_y = (fh - panel_h) / 2;
    int qr_x = panel_x + 44;
    int qr_y = panel_y + 76;
    int remain = (int)((g_payment_popup_until_ms - now_ms() + 999) / 1000);
    uint32_t theme = equals_ignore_case(g_payment_method, "alipay") ? 0xFF1677FF : 0xFF07C160;
    const uint32_t *qr = equals_ignore_case(g_payment_method, "alipay") ?
                         g_payment_alipay_bgra : g_payment_wechat_bgra;
    const char *title = equals_ignore_case(g_payment_method, "alipay") ?
                        "支付宝收款码" : "微信收款码";

    if (remain < 0) remain = 0;
    money_text(g_last_total_cents, amount, sizeof(amount));
    fill_rect_rgb(fb, fw, fh, 0, 0, fw, fh, 0xEE071018);
    fill_rect_rgb(fb, fw, fh, panel_x, panel_y, panel_w, panel_h, 0xFFFFFFFF);
    draw_rect_rgb(fb, fw, fh, panel_x, panel_y, panel_w, panel_h, theme, 6);
    fill_rect_rgb(fb, fw, fh, panel_x, panel_y, panel_w, 58, theme);
    draw_text_utf8_rgb(fb, fw, fh, panel_x + 36, panel_y + 18, title, 1, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, panel_x + panel_w - 150, panel_y + 16, amount, 3, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, panel_x + panel_w - 58, panel_y + 22, "元", 1, 0xFFFFFFFF);

    if (qr) {
        blit_bgra_rgb(fb, fw, fh, qr_x, qr_y, PAYMENT_QR_W, PAYMENT_QR_H, qr);
    } else {
        fill_rect_rgb(fb, fw, fh, qr_x, qr_y, PAYMENT_QR_W, PAYMENT_QR_H, 0xFFF4F7F9);
        draw_rect_rgb(fb, fw, fh, qr_x, qr_y, PAYMENT_QR_W, PAYMENT_QR_H, 0xFF8899AA, 2);
        draw_text_rgb(fb, fw, fh, qr_x + 42, qr_y + 220, "PAYMENT QR MISSING", 2, 0xFF111111);
    }
    char remain_text[16];
    int hint_x = panel_x + 36;
    int hint_y = panel_y + panel_h + 28;
    snprintf(remain_text, sizeof(remain_text), "%d", remain);
    fill_rect_rgb(fb, fw, fh, panel_x, panel_y + panel_h + 12, panel_w, 54, 0xDD071018);
    draw_rect_rgb(fb, fw, fh, panel_x, panel_y + panel_h + 12, panel_w, 54, theme, 2);
    draw_text_utf8_rgb(fb, fw, fh, hint_x, hint_y,
                       "请扫码完成支付，", 1, 0xFFFFFFFF);
    draw_text_rgb(fb, fw, fh, hint_x + 204, hint_y + 2,
                  remain_text, 3, theme);
    draw_text_utf8_rgb(fb, fw, fh,
                       hint_x + 204 + (int)strlen(remain_text) * 18 + 10,
                       hint_y, "秒后自动返回", 1, 0xFFFFFFFF);
}

static void draw_voice_history_panel(uint32_t *fb, int fw, int fh,
                                     int x, int y, int w, int h)
{
    fill_rect_rgb(fb, fw, fh, x + 134, y + 16, w - 154, h - 32, 0xFF0F2C3B);
    draw_rect_rgb(fb, fw, fh, x + 134, y + 16, w - 154, h - 32, 0xFF24536A, 1);
    int text_x = x + 154;
    int text_y = y + 24;
    int text_w = w - 190;
    int remaining_lines = (h - 44) / 30;
    if (remaining_lines > 5) remaining_lines = 5;
    for (int i = 0; i < VOICE_HISTORY_LINES; i++) {
        const char *label;
        const char *content;
        uint32_t color;
        int label_w;
        if (remaining_lines <= 0) break;
        content = voice_line_content(g_voice_history[i], &label, &color);
        label_w = draw_voice_label_rgb(fb, fw, fh, text_x, text_y, label, color);
        int used = draw_voice_text_wrapped_rgb(fb, fw, fh, text_x + label_w, text_y,
                                               text_w - label_w, remaining_lines,
                                               content, color);
        text_y += used * 30;
        remaining_lines -= used;
    }
}

static void draw_voice_dialog_panel(int x, int y, int w, int h)
{
    uint32_t *fb = (uint32_t *)g_ui_map;
    int fw = g_ui_w;
    int fh = g_ui_h;

    fill_rect_rgb(fb, fw, fh, x, y, w, h, 0xFF0B202B);
    draw_rect_rgb(fb, fw, fh, x, y, w, h, 0xFF145169, 1);
    int cx = x + 64;
    int cy = y + h / 2;
    draw_circle_rgb(fb, fw, fh, cx, cy, 50, 0xFF092F55);
    draw_circle_rgb(fb, fw, fh, cx, cy, 38, 0xFF0E5FA8);
    fill_rect_rgb(fb, fw, fh, cx - 26, cy - 16, 52, 34, 0xFFBCEBFF);
    draw_rect_rgb(fb, fw, fh, cx - 26, cy - 16, 52, 34, 0xFF34C7FF, 2);
    draw_circle_rgb(fb, fw, fh, cx - 12, cy, 4, 0xFF003A70);
    draw_circle_rgb(fb, fw, fh, cx + 12, cy, 4, 0xFF003A70);
    fill_rect_rgb(fb, fw, fh, cx - 2, cy - 30, 4, 14, 0xFF34C7FF);
    draw_circle_rgb(fb, fw, fh, cx, cy - 36, 5, 0xFF34C7FF);

    draw_voice_history_panel(fb, fw, fh, x, y, w, h);
    return;
    draw_text_utf8_rgb(fb, fw, fh, x + 154, y + 30, "客户：多少钱？", 1, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, x + 154, y + 64, "助手：矿泉水", 1, 0xFF7FFFE0);
    draw_text_rgb(fb, fw, fh, x + 316, y + 60, "2.00", 3, 0xFF52E27E);
    draw_text_utf8_rgb(fb, fw, fh, x + 392, y + 64, "元", 1, 0xFF7FFFE0);
}

static void draw_dashboard_background(void)
{
    uint32_t *fb = (uint32_t *)g_ui_map;
    int fw = g_ui_w;
    int fh = g_ui_h;
    int cam_x, cam_y, cam_w, cam_h, side_x, side_y, side_w, side_h;
    dashboard_layout(&cam_x, &cam_y, &cam_w, &cam_h,
                     &side_x, &side_y, &side_w, &side_h);

    fill_rect_rgb(fb, fw, fh, 0, 0, fw, fh, 0xFF071018);
    fill_rect_rgb(fb, fw, fh, cam_x - 12, cam_y - 34, cam_w + 24, cam_h + 46, 0xFF0B202B);
    draw_rect_rgb(fb, fw, fh, cam_x - 12, cam_y - 34, cam_w + 24, cam_h + 46, 0xFF145169, 1);
    draw_rect_rgb(fb, fw, fh, cam_x, cam_y, cam_w, cam_h, 0xFF00FF00, 3);
    draw_top_status_bar();

    fill_rect_rgb(fb, fw, fh, 0, fh - 34, fw, 34, 0xFF071D28);
    draw_rect_rgb(fb, fw, fh, 0, fh - 34, fw - 1, 33, 0xFF123A4A, 1);
    draw_text_utf8_rgb(fb, fw, fh, 48, fh - 28, "安全认证，请放心使用", 1, 0xFFCED7DD);
    draw_text_utf8_rgb(fb, fw, fh, fw / 2 - 90, fh - 28,
                       "遇到问题？点帮助或呼叫店员", 1, 0xFFCED7DD);
    draw_text_utf8_rgb(fb, fw, fh, fw - 150, fh - 28, "帮助", 1, 0xFFFFFFFF);
    draw_text_utf8_rgb(fb, fw, fh, fw - 82, fh - 28, "退出", 1, 0xFFFFFFFF);
}

static void retail_product_label(const char *payload, char *out, size_t out_sz)
{
    const char *label = "WAITING";
    if (payload && payload[0]) {
        if (strstr(payload, "milk") || strstr(payload, "MILK")) label = "MILK";
        else if (strstr(payload, "cola") || strstr(payload, "COLA")) label = "COLA";
        else if (strstr(payload, "water") || strstr(payload, "WATER")) label = "WATER";
        else if (strstr(payload, "bread") || strstr(payload, "BREAD")) label = "BREAD";
        else label = "QR DETECTED";
    }
    snprintf(out, out_sz, "%s", label);
}

static void draw_retail_ui_overlay(const char *last_payload,
                                   unsigned int qr_count,
                                   int64_t uptime_ms)
{
    int fh = g_ui_h;
    int cam_x, cam_y, cam_w, cam_h, side_x, side_y, side_w, side_h;
    char product[32];
    char line[64];
    int categories_h;
    int cart_h;
    int payment_h;
    int gap = 20;

    retail_product_label(last_payload, product, sizeof(product));
    dashboard_layout(&cam_x, &cam_y, &cam_w, &cam_h,
                     &side_x, &side_y, &side_w, &side_h);

    snprintf(line, sizeof(line), "SCAN READY  AI VOICE  RUN %lldS",
             (long long)(uptime_ms / 1000));
    (void)line;

    categories_h = side_w >= 360 ? 236 : side_h / 3;
    if (categories_h < 236) categories_h = 236;
    cart_h = side_w >= 360 ? 210 : side_h / 4;
    if (cart_h < 190) cart_h = 190;
    payment_h = side_h - categories_h - cart_h - gap * 2;
    if (payment_h < 180) payment_h = 180;

    draw_product_categories(side_x, side_y, side_w, categories_h);
    draw_cart_table(side_x, side_y + categories_h + gap,
                    side_w, cart_h, product, qr_count);
    draw_payment_panel(side_x, side_y + categories_h + cart_h + gap * 2,
                       side_w, payment_h, qr_count);
    draw_voice_dialog_panel(24, cam_y + cam_h + 24,
                            side_x - 42, fh - (cam_y + cam_h + 84));
}

static void draw_qr_outline(const struct quirc_code *code,
                            unsigned int cam_w, unsigned int cam_h)
{
    int min_x = code->corners[0].x;
    int max_x = code->corners[0].x;
    int min_y = code->corners[0].y;
    int max_y = code->corners[0].y;

    for (int i = 1; i < 4; i++) {
        min_x = min_x < code->corners[i].x ? min_x : code->corners[i].x;
        max_x = max_x > code->corners[i].x ? max_x : code->corners[i].x;
        min_y = min_y < code->corners[i].y ? min_y : code->corners[i].y;
        max_y = max_y > code->corners[i].y ? max_y : code->corners[i].y;
    }

    int view_x, view_y, view_w, view_h, side_x, side_y, side_w, side_h;
    dashboard_layout(&view_x, &view_y, &view_w, &view_h,
                     &side_x, &side_y, &side_w, &side_h);

    int rx0 = view_x + min_x * view_w / (int)cam_w;
    int ry0 = view_y + min_y * view_h / (int)cam_h;
    int rx1 = view_x + max_x * view_w / (int)cam_w;
    int ry1 = view_y + max_y * view_h / (int)cam_h;
    int rx = clamp_int(rx0, 0, g_ui_w - 1);
    int ry = clamp_int(ry0, 0, g_ui_h - 1);
    int rw = clamp_int(rx1 - rx0, 1, g_ui_w - rx);
    int rh = clamp_int(ry1 - ry0, 1, g_ui_h - ry);

    draw_rect_rgb((uint32_t *)g_ui_map, g_ui_w, g_ui_h,
                  rx, ry, rw, rh, 0xFF00FF00, 4);
}

/* ── DRM 显示初始化 (纯 ioctl, 无需 libdrm) ───────────────────────── */

static int decode_qr_candidates(struct quirc *qr,
                                unsigned int cam_w, unsigned int cam_h,
                                int have_display,
                                char *last, size_t last_sz,
                                unsigned int *found,
                                int *redraw_dashboard,
                                int continuous,
                                const char *pass_name)
{
    int decoded = 0;
    int count = quirc_count(qr);
    for (int i = 0; i < count; i++) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(qr, i, &code);
        if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
            const char *payload = (char *)data.payload;
            int64_t qr_now_ms = now_ms();
            const struct retail_product *product =
                retail_product_from_payload(payload);
            decoded = 1;
            if (!product) {
                printf("\n>>> QR CODE: %s <<<\n", payload);
                printf("    pass:%s ignored: unknown product\n", pass_name);
                fflush(stdout);
                continue;
            }
            if (have_display) {
                draw_qr_outline(&code, cam_w, cam_h);
            }
            if (!retail_product_can_add_from_qr(product, qr_now_ms)) {
                printf("\n>>> QR CODE: %s <<<\n", payload);
                printf("    pass:%s ignored: duplicate product within %dms\n",
                       pass_name, QR_PRODUCT_ADD_COOLDOWN_MS);
                fflush(stdout);
                continue;
            }
            if (strcmp(payload, last) != 0) {
                printf("\n>>> QR CODE: %s <<<\n", payload);
                printf("    ver:%d ecc:%c pass:%s\n",
                       data.version, "MLHQ"[data.ecc_level], pass_name);
                fflush(stdout);

                strncpy(last, payload, last_sz - 1);
                last[last_sz - 1] = '\0';
            }
            retail_cart_add_product(product);
            retail_speak_product_added(product);
            (*found)++;
            *redraw_dashboard = 1;
            g_qr_scan_resume_ms = now_ms() + QR_SCAN_PAUSE_AFTER_ADD_MS;
            if (!continuous) g_running = 0;
            return 1;
        }
    }
    return decoded;
}

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
        printf("[drm] checking connector index=%u id=%u\n", ci, conn.connector_id);

        if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
            perror("MODE_GETCONNECTOR");
            continue;
        }
        printf("[drm] connector %u type=%u state=%u modes=%u props=%u encoders=%u\n",
               conn.connector_id, conn.connector_type, conn.connection,
               conn.count_modes, conn.count_props, conn.count_encoders);
        if (conn.connection != DRM_MODE_CONNECTED || conn.count_modes == 0) continue;

        /* prioritize LVDS connector 154, then any LVDS, then any connected */
        int priority = 0;
        if (conn.connector_id == LVDS_CONNECTOR_ID) priority = 3;
        else if (conn.connector_type == DRM_MODE_CONNECTOR_LVDS) priority = 2;
        else priority = 1;

        /* get modes */
        struct drm_mode_modeinfo modes[128];
        uint32_t prop_arr[128], prop_val_arr[128], enc_arr2[16];
        conn.modes_ptr       = (uint64_t)(uintptr_t)modes;
        conn.count_modes     = conn.count_modes > 128 ? 128 : conn.count_modes;
        conn.props_ptr       = (uint64_t)(uintptr_t)prop_arr;
        conn.prop_values_ptr = (uint64_t)(uintptr_t)prop_val_arr;
        conn.count_props     = conn.count_props > 128 ? 128 : conn.count_props;
        conn.encoders_ptr    = (uint64_t)(uintptr_t)enc_arr2;
        conn.count_encoders  = conn.count_encoders > 16 ? 16 : conn.count_encoders;
        if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
            perror("MODE_GETCONNECTOR modes");
            continue;
        }
        printf("[drm] connector %u loaded modes=%u encoders=%u first_mode=%ux%u\n",
               conn.connector_id, conn.count_modes, conn.count_encoders,
               conn.count_modes ? modes[0].hdisplay : 0,
               conn.count_modes ? modes[0].vdisplay : 0);

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
                uint32_t chosen_enc = 0;
                uint32_t chosen_crtc = 0;
                for (unsigned int ei = 0; ei < conn.count_encoders; ei++) {
                    struct drm_mode_get_encoder enc = {0};
                    enc.encoder_id = enc_arr2[ei];
                    if (ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) < 0) continue;
                    chosen_enc = enc.encoder_id;
                    chosen_crtc = enc.crtc_id;
                    break;
                }
                if (!chosen_enc && conn.count_encoders > 0) chosen_enc = enc_arr2[0];
                if (!chosen_crtc && res.count_crtcs > 0) chosen_crtc = crtc_arr[0];
                printf("[drm] candidate conn=%u enc=%u crtc=%u\n",
                       conn.connector_id, chosen_enc, chosen_crtc);
                if (!chosen_crtc) continue;

                best_conn = conn.connector_id;
                best_enc  = chosen_enc;
                best_crtc = chosen_crtc;
                best_mode = modes[mi];
                best_w = modes[mi].hdisplay;
                best_h = modes[mi].vdisplay;
                best_refresh = modes[mi].vrefresh;
                goto found;
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

static int fb_display_init(void)
{
    int fd = open(FB_DEV, O_RDWR);
    if (fd < 0) { perror("open fb"); return -1; }

    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    memset(&var, 0, sizeof(var));
    memset(&fix, 0, sizeof(fix));

    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        perror("FBIOGET_VSCREENINFO");
        close(fd);
        return -1;
    }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        perror("FBIOGET_FSCREENINFO");
        close(fd);
        return -1;
    }
    if (var.bits_per_pixel != 32) {
        fprintf(stderr, "fb bpp %u is not supported, need 32\n", var.bits_per_pixel);
        close(fd);
        return -1;
    }

    g_drm_size = (uint64_t)fix.line_length * var.yres_virtual;
    if (g_drm_size == 0) g_drm_size = fix.smem_len;
    g_drm_map = mmap(NULL, g_drm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (g_drm_map == MAP_FAILED) {
        perror("mmap fb");
        close(fd);
        return -1;
    }

    g_drm_fd = fd;
    g_drm_w = (int)var.xres;
    g_drm_h = (int)var.yres;
    g_drm_pitch = fix.line_length;
    memset(g_drm_map, 0, g_drm_size);

    printf("[fb] Display init OK. FB %dx%d, pitch=%u, bpp=%u\n",
           g_drm_w, g_drm_h, g_drm_pitch, var.bits_per_pixel);
    return 0;
}

static void drm_display_cleanup(void)
{
    if (g_ui_rotate_landscape && g_ui_map) {
        free(g_ui_map);
    }
    g_ui_map = NULL;
    g_ui_w = 0;
    g_ui_h = 0;
    g_ui_pitch = 0;
    g_ui_rotate_landscape = 0;

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
    free(g_payment_wechat_bgra);
    free(g_payment_alipay_bgra);
    g_payment_wechat_bgra = NULL;
    g_payment_alipay_bgra = NULL;
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
    g_cam_h = *h;
    g_y_stride  = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    g_uv_stride = fmt.fmt.pix_mp.plane_fmt[1].bytesperline;
    if (g_uv_stride == 0) g_uv_stride = g_y_stride;
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

        g_y_len[i] = planes[0].length;
        g_uv_len[i] = planes[1].length;
        g_y_ptr[i] = mmap(NULL, g_y_len[i], PROT_READ | PROT_WRITE,
                          MAP_SHARED, g_fd, planes[0].m.mem_offset);
        if (g_y_ptr[i] == MAP_FAILED) { perror("mmap y"); return -1; }
        if (g_uv_len[i] > 0) {
            g_uv_ptr[i] = mmap(NULL, g_uv_len[i], PROT_READ | PROT_WRITE,
                               MAP_SHARED, g_fd, planes[1].m.mem_offset);
            if (g_uv_ptr[i] == MAP_FAILED) { perror("mmap uv"); return -1; }
        } else {
            g_uv_ptr[i] = NULL;
        }
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
    for (unsigned int i = 0; i < g_nbufs; i++) {
        if (g_y_ptr[i] && g_y_ptr[i] != MAP_FAILED)
            munmap(g_y_ptr[i], g_y_len[i]);
        if (g_uv_ptr[i] && g_uv_ptr[i] != MAP_FAILED)
            munmap(g_uv_ptr[i], g_uv_len[i]);
    }
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
    *py  = (uint8_t *)g_y_ptr[buf.index];
    *yl  = planes[0].bytesused;
    if (g_uv_ptr[buf.index]) {
        *puv = (uint8_t *)g_uv_ptr[buf.index];
    } else {
        *puv = (uint8_t *)g_y_ptr[buf.index] + g_y_stride * g_cam_h;
    }
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
    struct quirc *qr_enhanced = quirc_new();
    struct quirc *qr_crop = quirc_new();
    if (!qr || !qr_enhanced || !qr_crop ||
        quirc_resize(qr, cam_w, cam_h) < 0 ||
        quirc_resize(qr_enhanced, cam_w, cam_h) < 0 ||
        quirc_resize(qr_crop, cam_w, cam_h) < 0) {
        fprintf(stderr, "quirc init failed\n"); return 1;
    }

    int have_display = 0;
    const char *display_name = "OFF";
    if (drm_display_init(cam_w, cam_h) == 0) {
        have_display = 1;
        display_name = "DRM";
    } else if (fb_display_init() == 0) {
        have_display = 1;
        display_name = "FB0";
    }
    if (have_display && display_prepare_landscape_canvas() < 0) {
        have_display = 0;
        display_name = "OFF";
    }
    if (have_display) {
        load_payment_qr_assets();
    }
    if (camera_stream_on() < 0) return 1;

    printf("\n[scanner] %ux%u | display:%s | quirc QR\n"
           "  Put QR code in front of camera. Ctrl+C to stop.\n\n",
           cam_w, cam_h, display_name);
    fflush(stdout);

    unsigned int frame = 0, found = 0;
    int64_t t0 = now_ms();
    char last[256] = "";
    char rendered_last[256] = "";
    unsigned int rendered_found = (unsigned int)-1;
    int redraw_dashboard = 1;

    while (g_running) {
        unsigned int idx, yl, uvl;
        uint8_t *yp, *uvp;
        int saw_qr_this_frame = 0;
        if (camera_grab(&idx, &yp, &yl, &uvp, &uvl) < 0) {
            if (!g_running) break;
            continue;
        }
        frame++;
        if (retail_apply_voice_state()) {
            redraw_dashboard = 1;
        }
        if (g_payment_popup_until_ms > 0 && now_ms() >= g_payment_popup_until_ms) {
            retail_finish_payment_and_reset();
            redraw_dashboard = 1;
        }
        if (g_payment_popup_until_ms > 0 && retail_payment_done_requested()) {
            retail_finish_payment_and_reset();
            redraw_dashboard = 1;
        }

        /* LVDS 显示 */
        if (have_display) {
            int cam_view_x, cam_view_y, cam_view_w, cam_view_h;
            int side_x, side_y, side_w, side_h;
            (void)uvl;
            dashboard_layout(&cam_view_x, &cam_view_y, &cam_view_w, &cam_view_h,
                             &side_x, &side_y, &side_w, &side_h);
            if (redraw_dashboard ||
                rendered_found != found ||
                strcmp(rendered_last, last) != 0) {
                draw_dashboard_background();
                draw_retail_ui_overlay(last, found, now_ms() - t0);
                strncpy(rendered_last, last, sizeof(rendered_last) - 1);
                rendered_last[sizeof(rendered_last) - 1] = '\0';
                rendered_found = found;
                redraw_dashboard = 0;
            }
            nv12_to_xrgb_scaled_rect(yp, uvp, cam_w, cam_h,
                                     g_y_stride, g_uv_stride,
                                     (uint32_t *)g_ui_map,
                                     g_ui_w, g_ui_h,
                                     g_ui_pitch,
                                     cam_view_x, cam_view_y,
                                     cam_view_w, cam_view_h);
            if (g_payment_popup_until_ms > 0 && g_payment_method[0]) {
                draw_payment_popup();
            }
        }

        /* QR 检测 */
        int qw, qh;
        int decoded_this_frame = 0;
        int64_t qr_scan_now = now_ms();
        unsigned int found_before_decode = found;
        uint8_t *qb = NULL;
        if (qr_scan_now >= g_qr_scan_resume_ms) {
            qb = quirc_begin(qr, &qw, &qh);
        }
        if (qb && qw == (int)cam_w && qh == (int)cam_h) {
            copy_luma_for_qr(qb, yp, cam_w, cam_h, g_y_stride);
            quirc_end(qr);
            decoded_this_frame = decode_qr_candidates(qr, cam_w, cam_h,
                                                      have_display,
                                                      last, sizeof(last),
                                                      &found, &redraw_dashboard,
                                                      continuous, "raw");
            if (!decoded_this_frame) {
                int qw2, qh2;
                uint8_t *qb2 = quirc_begin(qr_enhanced, &qw2, &qh2);
                if (qb2 && qw2 == (int)cam_w && qh2 == (int)cam_h) {
                    enhance_luma_for_qr(qb2, yp, cam_w, cam_h, g_y_stride);
                    quirc_end(qr_enhanced);
                    decoded_this_frame = decode_qr_candidates(qr_enhanced, cam_w, cam_h,
                                                              have_display,
                                                              last, sizeof(last),
                                                              &found, &redraw_dashboard,
                                                              continuous, "enhanced");
                }
            }
            if (!decoded_this_frame) {
                int qw3, qh3;
                uint8_t *qb3 = quirc_begin(qr_crop, &qw3, &qh3);
                if (qb3 && qw3 == (int)cam_w && qh3 == (int)cam_h) {
                    crop_enhance_luma_for_qr(qb3, yp, cam_w, cam_h, g_y_stride, 88);
                    quirc_end(qr_crop);
                    decoded_this_frame = decode_qr_candidates(qr_crop, cam_w, cam_h,
                                                              have_display,
                                                              last, sizeof(last),
                                                              &found, &redraw_dashboard,
                                                              continuous, "center88");
                }
            }
            if (!decoded_this_frame) {
                int qw4, qh4;
                char pass_name[32];
                const struct qr_crop_region *region =
                    &g_qr_shifted_crops[frame % QR_SHIFTED_CROP_COUNT];
                uint8_t *qb4 = quirc_begin(qr_crop, &qw4, &qh4);
                if (qb4 && qw4 == (int)cam_w && qh4 == (int)cam_h) {
                    crop_region_enhance_luma_for_qr(qb4, yp, cam_w, cam_h,
                                                    g_y_stride, region);
                    quirc_end(qr_crop);
                    snprintf(pass_name, sizeof(pass_name), "shift%d",
                             (int)(frame % QR_SHIFTED_CROP_COUNT));
                    decoded_this_frame = decode_qr_candidates(qr_crop, cam_w, cam_h,
                                                              have_display,
                                                              last, sizeof(last),
                                                              &found, &redraw_dashboard,
                                                              continuous, pass_name);
                }
            }
        }
        if (found > found_before_decode) {
            g_qr_scan_resume_ms = now_ms() + QR_SCAN_PAUSE_AFTER_ADD_MS;
        }
        saw_qr_this_frame = decoded_this_frame;
        (void)saw_qr_this_frame;

        if (have_display) {
            display_flush_landscape_canvas();
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
    quirc_destroy(qr_enhanced);
    quirc_destroy(qr_crop);

    int64_t e = now_ms() - t0;
    printf("\nDone. %u frames %.1fs (%.1ffps) | %d QR found.\n",
           frame, e / 1000.0, frame * 1000.0f / (float)e, found);
    return (found > 0) ? 0 : 2;
}
