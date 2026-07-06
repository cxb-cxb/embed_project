/*
 * qr_scanner.c — V4L2 camera capture + quirc QR code decoder
 *
 * Captures frames from /dev/video-camera0 (NV12 format),
 * extracts the Y (luminance) plane, and feeds it to quirc
 * for QR code detection. Results are printed to stdout.
 */

#define _GNU_SOURCE
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

#include "quirc.h"

/* ── configuration ───────────────────────────────────────────────── */

#define CAMERA_DEV      "/dev/video-camera0"
#define DEFAULT_WIDTH   640
#define DEFAULT_HEIGHT  480
#define MAX_BUFFERS     4
#define TERM_COLS       80
#define TERM_ROWS       30
#define MAX_QR_BOXES    16
#define MAX_CART_ITEMS   16

/* ── globals for cleanup ─────────────────────────────────────────── */

static volatile int g_running = 1;
static int            g_fd    = -1;
static void          *g_buf_ptr[MAX_BUFFERS];
static unsigned int   g_buf_len[MAX_BUFFERS];
static unsigned int   g_nbufs = 0;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ── helpers ─────────────────────────────────────────────────────── */

static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do { r = ioctl(fd, request, arg); } while (r == -1 && errno == EINTR);
    return r;
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

struct qr_box {
    int decoded;
    int x0;
    int y0;
    int x1;
    int y1;
    int corners[4][2];
};

struct retail_product {
    const char *id;
    const char *name;
    const char *barcode;
    int price_cents;
};

struct cart_line {
    const struct retail_product *product;
    int quantity;
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

static struct cart_line g_cart[MAX_CART_ITEMS];
static int g_cart_count = 0;

static int ascii_lower(int c)
{
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int equals_ignore_case(const char *a, const char *b)
{
    while (*a && *b) {
        if (ascii_lower((unsigned char)*a) != ascii_lower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static const char *trim_payload(const char *payload)
{
    while (*payload == ' ' || *payload == '\t' || *payload == '\r' || *payload == '\n') {
        payload++;
    }
    return payload;
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

static const struct retail_product *retail_find_product(const char *payload)
{
    const char *value = trim_payload(payload);
    const char *prefix = "product:";
    size_t prefix_len = strlen(prefix);

    if (strncmp(value, prefix, prefix_len) == 0) {
        value += prefix_len;
    }

    for (size_t i = 0; i < sizeof(g_products) / sizeof(g_products[0]); i++) {
        if (equals_ignore_case(value, g_products[i].id) ||
            equals_ignore_case(value, g_products[i].name) ||
            retail_payload_alias_matches(value, g_products[i].id) ||
            strcmp(value, g_products[i].barcode) == 0) {
            return &g_products[i];
        }
    }
    return NULL;
}

static int retail_total_cents(void)
{
    int total = 0;
    for (int i = 0; i < g_cart_count; i++) {
        total += g_cart[i].product->price_cents * g_cart[i].quantity;
    }
    return total;
}

static void print_money(int cents)
{
    printf("CNY %d.%02d", cents / 100, cents % 100);
}

static void retail_add_and_print(const char *payload)
{
    const struct retail_product *product = retail_find_product(payload);
    if (!product) {
        printf("[retail] QR payload not mapped to a product: %s\n", payload);
        fflush(stdout);
        return;
    }

    for (int i = 0; i < g_cart_count; i++) {
        if (g_cart[i].product == product) {
            g_cart[i].quantity++;
            goto printed;
        }
    }

    if (g_cart_count >= MAX_CART_ITEMS) {
        printf("[retail] Cart is full, cannot add %s\n", product->name);
        fflush(stdout);
        return;
    }
    g_cart[g_cart_count].product = product;
    g_cart[g_cart_count].quantity = 1;
    g_cart_count++;

printed:
    printf("\n[retail] Added: %s  unit=", product->name);
    print_money(product->price_cents);
    printf("\n[retail] Cart:\n");
    for (int i = 0; i < g_cart_count; i++) {
        printf("  - %s x%d = ", g_cart[i].product->name, g_cart[i].quantity);
        print_money(g_cart[i].product->price_cents * g_cart[i].quantity);
        printf("\n");
    }
    printf("[retail] Total: ");
    print_money(retail_total_cents());
    printf("\n");
    fflush(stdout);
}


static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void code_to_box(const struct quirc_code *code, int decoded, struct qr_box *box)
{
    int min_x = code->corners[0].x;
    int max_x = code->corners[0].x;
    int min_y = code->corners[0].y;
    int max_y = code->corners[0].y;

    for (int i = 0; i < 4; i++) {
        int x = code->corners[i].x;
        int y = code->corners[i].y;
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
        box->corners[i][0] = x;
        box->corners[i][1] = y;
    }

    box->decoded = decoded;
    box->x0 = min_x;
    box->y0 = min_y;
    box->x1 = max_x;
    box->y1 = max_y;
}

static void draw_terminal_preview(const uint8_t *y_plane,
                                  unsigned int width,
                                  unsigned int height,
                                  const struct qr_box *boxes,
                                  int box_count,
                                  unsigned int frame,
                                  int found)
{
    static const char shades[] = " .:-=+*#%@";
    char canvas[TERM_ROWS][TERM_COLS + 1];

    for (int row = 0; row < TERM_ROWS; row++) {
        unsigned int src_y = (unsigned int)((uint64_t)row * height / TERM_ROWS);
        if (src_y >= height) src_y = height - 1;

        for (int col = 0; col < TERM_COLS; col++) {
            unsigned int src_x = (unsigned int)((uint64_t)col * width / TERM_COLS);
            if (src_x >= width) src_x = width - 1;
            uint8_t lum = y_plane[src_y * width + src_x];
            int shade_idx = (int)((unsigned int)lum * 9 / 255);
            canvas[row][col] = shades[shade_idx];
        }
        canvas[row][TERM_COLS] = '\0';
    }

    for (int i = 0; i < box_count; i++) {
        int left = clamp_int(boxes[i].x0 * TERM_COLS / (int)width, 0, TERM_COLS - 1);
        int right = clamp_int(boxes[i].x1 * TERM_COLS / (int)width, 0, TERM_COLS - 1);
        int top = clamp_int(boxes[i].y0 * TERM_ROWS / (int)height, 0, TERM_ROWS - 1);
        int bottom = clamp_int(boxes[i].y1 * TERM_ROWS / (int)height, 0, TERM_ROWS - 1);

        for (int col = left; col <= right; col++) {
            canvas[top][col] = '#';
            canvas[bottom][col] = '#';
        }
        for (int row = top; row <= bottom; row++) {
            canvas[row][left] = '#';
            canvas[row][right] = '#';
        }
    }

    printf("\033[H\033[2J");
    printf("[preview] frame=%u  qr_found=%d  boxes=%d  size=%ux%u\n",
           frame, found, box_count, width, height);
    for (int row = 0; row < TERM_ROWS; row++) {
        puts(canvas[row]);
    }
    fflush(stdout);
}

/* ── camera open / setup ─────────────────────────────────────────── */

static int camera_open(const char *dev, unsigned int *p_width, unsigned int *p_height)
{
    struct v4l2_capability cap;
    struct v4l2_format      fmt;
    struct v4l2_requestbuffers req;
    unsigned int i;

    g_fd = open(dev, O_RDWR);
    if (g_fd < 0) {
        perror("open camera");
        return -1;
    }

    /* check capabilities */
    memset(&cap, 0, sizeof(cap));
    if (xioctl(g_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        return -1;
    }
    printf("[camera] driver  : %s\n", cap.driver);
    printf("[camera] card    : %s\n", cap.card);
    printf("[camera] caps    : 0x%08X\n", cap.capabilities);
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        fprintf(stderr, "error: device does not support multiplanar capture\n");
        return -1;
    }

    /* set format — NV12, requested resolution */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width    = *p_width;
    fmt.fmt.pix_mp.height   = *p_height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field    = V4L2_FIELD_NONE;
    if (xioctl(g_fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return -1;
    }
    *p_width  = fmt.fmt.pix_mp.width;
    *p_height = fmt.fmt.pix_mp.height;
    printf("[camera] format  : NV12 %ux%u  (stride Y:%u UV:%u)\n",
           fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
           fmt.fmt.pix_mp.plane_fmt[0].bytesperline,
           fmt.fmt.pix_mp.plane_fmt[1].bytesperline);

    /* request buffers */
    memset(&req, 0, sizeof(req));
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count  = MAX_BUFFERS;
    if (xioctl(g_fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }
    g_nbufs = req.count;
    printf("[camera] buffers : %u\n", g_nbufs);

    /* mmap each buffer */
    for (i = 0; i < g_nbufs; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane  planes[2];  /* NV12 = 2 planes */

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.index    = i;
        buf.m.planes = planes;
        buf.length   = 2;
        if (xioctl(g_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        /* For multiplanar NV12 the two planes are usually contiguous
         * in one mmap region.  We mmap from plane 0 offset 0 for the
         * total size. */
        unsigned int total = planes[0].length + planes[1].length;
        g_buf_len[i] = total;
        g_buf_ptr[i] = mmap(NULL, total,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED, g_fd,
                            planes[0].m.mem_offset);
        if (g_buf_ptr[i] == MAP_FAILED) {
            perror("mmap");
            return -1;
        }
    }

    return 0;
}

/* ── start / stop streaming ──────────────────────────────────────── */

static int camera_stream_on(void)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    unsigned int i;

    /* queue all buffers */
    for (i = 0; i < g_nbufs; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane  planes[2];

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.index    = i;
        buf.m.planes = planes;
        buf.length   = 2;
        if (xioctl(g_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    if (xioctl(g_fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return -1;
    }
    return 0;
}

static void camera_stream_off(void)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    xioctl(g_fd, VIDIOC_STREAMOFF, &type);
}

static void camera_close(void)
{
    unsigned int i;

    if (g_fd < 0) return;

    for (i = 0; i < g_nbufs; i++) {
        if (g_buf_ptr[i] && g_buf_ptr[i] != MAP_FAILED)
            munmap(g_buf_ptr[i], g_buf_len[i]);
    }
    close(g_fd);
    g_fd = -1;
}

/* ── capture one frame ───────────────────────────────────────────── */
/* Returns the buffer index.  *p_y  points to the Y-plane inside the
 * mmap'd buffer, *p_y_len is the Y-plane size.                        */

static int camera_grab(unsigned int *p_idx,
                       uint8_t **p_y, unsigned int *p_y_len)
{
    struct v4l2_buffer buf;
    struct v4l2_plane  planes[2];

    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));
    buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory   = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length   = 2;

    if (xioctl(g_fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EINTR) return -1;
        perror("VIDIOC_DQBUF");
        return -1;
    }

    *p_idx   = buf.index;
    /* Y plane is the first plane, data starts at offset 0 of the mmap */
    *p_y     = (uint8_t *)g_buf_ptr[buf.index];
    *p_y_len = planes[0].bytesused;

    return 0;
}

static int camera_release(unsigned int idx)
{
    struct v4l2_buffer buf;
    struct v4l2_plane  planes[2];

    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));
    buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory   = V4L2_MEMORY_MMAP;
    buf.index    = idx;
    buf.m.planes = planes;
    buf.length   = 2;

    if (xioctl(g_fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF");
        return -1;
    }
    return 0;
}

/* ── main ────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    const char *device = CAMERA_DEV;
    unsigned int width  = DEFAULT_WIDTH;
    unsigned int height = DEFAULT_HEIGHT;
    int continuous = 0;   /* 0 = exit on first QR, 1 = keep scanning */
    int retail_mode = 0;
    int terminal_preview = 0;
    unsigned int preview_interval = 5;
    int opt;

    while ((opt = getopt(argc, argv, "d:W:H:ctrp:h")) != -1) {
        switch (opt) {
        case 'd': device = optarg; break;
        case 'W': width  = (unsigned int)atoi(optarg); break;
        case 'H': height = (unsigned int)atoi(optarg); break;
        case 'c': continuous = 1; break;
        case 't': terminal_preview = 1; continuous = 1; break;
        case 'r': retail_mode = 1; continuous = 1; break;
        case 'p':
            preview_interval = (unsigned int)atoi(optarg);
            if (preview_interval == 0) preview_interval = 1;
            break;
        case 'h':
        default:
            printf("Usage: %s [-d device] [-W width] [-H height] [-c] [-t] [-r] [-p frames]\n", argv[0]);
            printf("  -d  video device    (default: %s)\n", CAMERA_DEV);
            printf("  -W  capture width   (default: %u)\n", DEFAULT_WIDTH);
            printf("  -H  capture height  (default: %u)\n", DEFAULT_HEIGHT);
            printf("  -c  continuous mode (keep scanning, don't exit on first QR)\n");
            printf("  -t  terminal preview with QR boxes\n");
            printf("  -r  retail mode: map QR payloads to products and print cart totals\n");
            printf("  -p  preview refresh interval in frames (default: %u)\n", preview_interval);
            return 1;
        }
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* ── open camera ──────────────────────────────────────────────── */
    if (camera_open(device, &width, &height) < 0)
        return 1;

    /* ── init quirc ───────────────────────────────────────────────── */
    struct quirc *qr = quirc_new();
    if (!qr) {
        fprintf(stderr, "quirc_new failed\n");
        camera_close();
        return 1;
    }
    if (quirc_resize(qr, width, height) < 0) {
        fprintf(stderr, "quirc_resize failed\n");
        quirc_destroy(qr);
        camera_close();
        return 1;
    }

    /* ── start streaming ──────────────────────────────────────────── */
    if (camera_stream_on() < 0) {
        quirc_destroy(qr);
        camera_close();
        return 1;
    }

    printf("\n[scanner] Scanning for QR codes (%ux%u NV12, quirc)...\n"
           "[scanner] Press Ctrl+C to stop.\n",
           width, height);
    if (terminal_preview) {
        printf("[scanner] Terminal preview enabled. Refresh every %u frame(s).\n", preview_interval);
    }
    printf("\n");

    /* ── main loop ────────────────────────────────────────────────── */
    unsigned int   frame   = 0;
    int64_t        t_start = now_ms();
    int            found   = 0;
    char           last_payload[256] = "";
    int            qr_absent_frames = 0;
    int            saw_qr_this_frame = 0;
    struct qr_box  boxes[MAX_QR_BOXES];

    while (g_running) {
        unsigned int idx, y_len;
        uint8_t     *y_plane;
        int box_count = 0;

        saw_qr_this_frame = 0;

        if (camera_grab(&idx, &y_plane, &y_len) < 0) {
            if (!g_running) break;
            continue;
        }

        frame++;

        /* feed Y plane to quirc (luminance → grayscale) */
        int qw, qh;
        uint8_t *qr_buf = quirc_begin(qr, &qw, &qh);
        if (qr_buf && qw == (int)width && qh == (int)height) {
            unsigned int copy_w = (unsigned int)qw;
            if (copy_w > width) copy_w = width;
            /* NV12 Y plane: tightly packed, straight memcpy works */
            memcpy(qr_buf, y_plane, width * height);
            quirc_end(qr);

            int count = quirc_count(qr);
            for (int i = 0; i < count; i++) {
                struct quirc_code code;
                struct quirc_data data;
                quirc_extract(qr, i, &code);
                quirc_decode_error_t err = quirc_decode(&code, &data);
                int decoded = (err == QUIRC_SUCCESS);

                if (box_count < MAX_QR_BOXES) {
                    code_to_box(&code, decoded, &boxes[box_count]);
                    box_count++;
                }

                if (err == QUIRC_SUCCESS) {
                    saw_qr_this_frame = 1;
                    int same_payload = strncmp((char *)data.payload, last_payload,
                                               sizeof(last_payload) - 1) == 0;
                    if (!same_payload) {
                        struct qr_box decoded_box;
                        code_to_box(&code, 1, &decoded_box);
                        printf("\n>>> QR CODE FOUND <<<\n");
                        printf("     Payload : %s\n", data.payload);
                        printf("     Version : %d\n", data.version);
                        printf("     ECC     : %c\n",
                               "MLHQ"[data.ecc_level]);
                        printf("     Box     : (%d,%d)-(%d,%d)\n",
                               decoded_box.x0, decoded_box.y0,
                               decoded_box.x1, decoded_box.y1);
                        printf("     Corners : (%d,%d) (%d,%d) (%d,%d) (%d,%d)\n",
                               code.corners[0].x, code.corners[0].y,
                               code.corners[1].x, code.corners[1].y,
                               code.corners[2].x, code.corners[2].y,
                               code.corners[3].x, code.corners[3].y);
                        printf("\n");
                        fflush(stdout);

                        strncpy(last_payload, (char *)data.payload,
                                sizeof(last_payload) - 1);
                        last_payload[sizeof(last_payload) - 1] = '\0';
                        found++;
                        if (retail_mode) {
                            retail_add_and_print((char *)data.payload);
                        }

                        if (!continuous) {
                            g_running = 0;
                            camera_release(idx);
                            break;
                        }
                    }
                }
            }
        }

        if (saw_qr_this_frame) {
            qr_absent_frames = 0;
        } else if (++qr_absent_frames > 12) {
            last_payload[0] = 0;
        }

        if (terminal_preview && (frame % preview_interval == 0)) {
            draw_terminal_preview(y_plane, width, height, boxes, box_count, frame, found);
        }

        camera_release(idx);

        /* progress indicator every 30 frames (~1 second) */
        if (frame % 30 == 0) {
            int64_t elapsed = now_ms() - t_start;
            printf("[scanner] %u frames  |  %.1f fps  |  %d QR found\r",
                   frame, (float)frame * 1000.0f / (float)elapsed, found);
            fflush(stdout);
        }
    }

    /* ── cleanup ──────────────────────────────────────────────────── */
    camera_stream_off();
    camera_close();
    quirc_destroy(qr);

    int64_t elapsed = now_ms() - t_start;
    printf("\n\n[scanner] Done.  %u frames in %.1f s  (%.1f fps)  |  %d QR found.\n",
           frame, elapsed / 1000.0, (float)frame * 1000.0f / (float)elapsed, found);

    return (found > 0) ? 0 : 2;
}
