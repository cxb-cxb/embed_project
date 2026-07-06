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
#include <linux/fb.h>
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
#define FB_DEV          "/dev/fb0"
#define DEFAULT_WIDTH   640
#define DEFAULT_HEIGHT  480
#define MAX_BUFFERS     4
#define LVDS_CONNECTOR_ID  154
#define MAX_CART_ITEMS 16

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
static const struct retail_product *g_last_product = NULL;
static int g_last_price_cents = 0;
static int g_last_total_cents = 0;
static int g_last_item_count = 0;
static char g_order_id[48] = "ORDER: pending";
static char g_payment_url[160] = "PAY: scan checkout";
static char g_status_text[80] = "READY";
static int g_checkout_done = 0;
static unsigned int g_order_seq = 0;

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

static const char *trim_payload(const char *payload)
{
    while (*payload == ' ' || *payload == '\t' || *payload == '\r' || *payload == '\n') payload++;
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
    if (strncmp(value, prefix, prefix_len) == 0) value += prefix_len;

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
    for (int i = 0; i < g_cart_count; i++) total += g_cart[i].product->price_cents * g_cart[i].quantity;
    return total;
}

static int retail_item_count(void)
{
    int count = 0;
    for (int i = 0; i < g_cart_count; i++) count += g_cart[i].quantity;
    return count;
}

static void retail_money(char *out, size_t out_size, int cents)
{
    snprintf(out, out_size, "CNY %d.%02d", cents / 100, cents % 100);
}

static void retail_reset_payment(void)
{
    snprintf(g_order_id, sizeof(g_order_id), "ORDER: pending");
    snprintf(g_payment_url, sizeof(g_payment_url), "PAY: scan checkout");
}

static void retail_create_payment_order(void)
{
    g_order_seq++;
    snprintf(g_order_id, sizeof(g_order_id), "ORDER:QSM%04u", g_order_seq);
    snprintf(g_payment_url, sizeof(g_payment_url),
             "PAY:https://pay.example.local/qsm368?order=QSM%04u&amount=%d",
             g_order_seq, g_last_total_cents);
}

static void retail_clear_cart(void)
{
    memset(g_cart, 0, sizeof(g_cart));
    g_cart_count = 0;
    g_last_product = NULL;
    g_last_price_cents = 0;
    g_last_total_cents = 0;
    g_last_item_count = 0;
    g_checkout_done = 0;
    retail_reset_payment();
    snprintf(g_status_text, sizeof(g_status_text), "CART CLEARED");
}

static int retail_handle_control_payload(const char *payload)
{
    const char *value = trim_payload(payload);
    if (equals_ignore_case(value, "clear") || equals_ignore_case(value, "cart:clear")) {
        retail_clear_cart();
        return 1;
    }
    if (equals_ignore_case(value, "checkout") || equals_ignore_case(value, "pay") ||
        equals_ignore_case(value, "cart:checkout")) {
        if (g_last_total_cents <= 0) {
            g_checkout_done = 0;
            retail_reset_payment();
            snprintf(g_status_text, sizeof(g_status_text), "CART EMPTY");
        } else {
            g_checkout_done = 1;
            retail_create_payment_order();
            snprintf(g_status_text, sizeof(g_status_text), "CHECKOUT READY");
        }
        return 1;
    }
    return 0;
}

static const struct retail_product *retail_add_payload(const char *payload)
{
    const struct retail_product *product = retail_find_product(payload);
    if (!product) return NULL;

    for (int i = 0; i < g_cart_count; i++) {
        if (g_cart[i].product == product) {
            g_cart[i].quantity++;
            goto done;
        }
    }
    if (g_cart_count >= MAX_CART_ITEMS) return NULL;
    g_cart[g_cart_count].product = product;
    g_cart[g_cart_count].quantity = 1;
    g_cart_count++;

done:
    g_last_product = product;
    g_last_price_cents = product->price_cents;
    g_last_total_cents = retail_total_cents();
    g_last_item_count = retail_item_count();
    g_checkout_done = 0;
    retail_reset_payment();
    snprintf(g_status_text, sizeof(g_status_text), "ADDED %s", product->name);
    return product;
}

static void draw_fill_rect(uint32_t *fb, int fw, int fh, int x, int y, int w, int h, uint32_t color)
{
    int x0 = clamp_int(x, 0, fw - 1);
    int y0 = clamp_int(y, 0, fh - 1);
    int x1 = clamp_int(x + w, 0, fw);
    int y1 = clamp_int(y + h, 0, fh);
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) fb[yy * fw + xx] = color;
    }
}

static const uint8_t *font_rows(char c)
{
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    static const uint8_t unknown[7] = {14,17,1,2,4,0,4};
#define FONT_CASE(ch, a,b,c,d,e,f,g) case ch: { static const uint8_t r[7] = {a,b,c,d,e,f,g}; return r; }
    switch (c) {
    FONT_CASE('A',14,17,17,31,17,17,17)
    FONT_CASE('B',30,17,17,30,17,17,30)
    FONT_CASE('C',14,17,16,16,16,17,14)
    FONT_CASE('D',30,17,17,17,17,17,30)
    FONT_CASE('E',31,16,16,30,16,16,31)
    FONT_CASE('F',31,16,16,30,16,16,16)
    FONT_CASE('G',14,17,16,23,17,17,15)
    FONT_CASE('H',17,17,17,31,17,17,17)
    FONT_CASE('I',14,4,4,4,4,4,14)
    FONT_CASE('J',7,2,2,2,18,18,12)
    FONT_CASE('K',17,18,20,24,20,18,17)
    FONT_CASE('L',16,16,16,16,16,16,31)
    FONT_CASE('M',17,27,21,21,17,17,17)
    FONT_CASE('N',17,25,21,19,17,17,17)
    FONT_CASE('O',14,17,17,17,17,17,14)
    FONT_CASE('P',30,17,17,30,16,16,16)
    FONT_CASE('Q',14,17,17,17,21,18,13)
    FONT_CASE('R',30,17,17,30,20,18,17)
    FONT_CASE('S',15,16,16,14,1,1,30)
    FONT_CASE('T',31,4,4,4,4,4,4)
    FONT_CASE('U',17,17,17,17,17,17,14)
    FONT_CASE('V',17,17,17,17,17,10,4)
    FONT_CASE('W',17,17,17,21,21,21,10)
    FONT_CASE('X',17,17,10,4,10,17,17)
    FONT_CASE('Y',17,17,10,4,4,4,4)
    FONT_CASE('Z',31,1,2,4,8,16,31)
    FONT_CASE('0',14,17,19,21,25,17,14)
    FONT_CASE('1',4,12,4,4,4,4,14)
    FONT_CASE('2',14,17,1,2,4,8,31)
    FONT_CASE('3',30,1,1,14,1,1,30)
    FONT_CASE('4',2,6,10,18,31,2,2)
    FONT_CASE('5',31,16,30,1,1,17,14)
    FONT_CASE('6',6,8,16,30,17,17,14)
    FONT_CASE('7',31,1,2,4,8,8,8)
    FONT_CASE('8',14,17,17,14,17,17,14)
    FONT_CASE('9',14,17,17,15,1,2,12)
    FONT_CASE(':',0,4,4,0,4,4,0)
    FONT_CASE('.',0,0,0,0,0,12,12)
    FONT_CASE('/',1,1,2,4,8,16,16)
    FONT_CASE('-',0,0,0,31,0,0,0)
    FONT_CASE('_',0,0,0,0,0,0,31)
    FONT_CASE('?',14,17,1,2,4,0,4)
    case ' ': return blank;
    default: return unknown;
    }
#undef FONT_CASE
}

static char display_char(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    if (c == '&' || c == '=') return ':';
    return c;
}

static void draw_char(uint32_t *fb, int fw, int fh, int x, int y, char c, int scale, uint32_t color)
{
    const uint8_t *rows = font_rows(display_char(c));
    for (int yy = 0; yy < 7; yy++) {
        for (int xx = 0; xx < 5; xx++) {
            if (rows[yy] & (1 << (4 - xx))) {
                draw_fill_rect(fb, fw, fh, x + xx * scale, y + yy * scale, scale, scale, color);
            }
        }
    }
}

static void draw_text(uint32_t *fb, int fw, int fh, int x, int y, const char *text, int scale, uint32_t color)
{
    int cursor = x;
    int max_chars = (fw - x - 8) / (6 * scale);
    for (int i = 0; text[i] && i < max_chars; i++) {
        draw_char(fb, fw, fh, cursor, y, text[i], scale, color);
        cursor += 6 * scale;
    }
}

static void draw_retail_overlay(void)
{
    if (!g_drm_map || g_drm_map == MAP_FAILED || g_drm_w <= 0 || g_drm_h <= 0) return;

    uint32_t *fb = (uint32_t *)g_drm_map;
    int panel_w = g_drm_w < 760 ? g_drm_w - 20 : 740;
    int panel_h = 214;
    char line[192];
    char price[32];
    char total[32];

    draw_fill_rect(fb, g_drm_w, g_drm_h, 10, 10, panel_w, panel_h, 0xDD101010);
    draw_rect_rgb(fb, g_drm_w, g_drm_h, 10, 10, panel_w, panel_h, 0xFF00FF00, 3);

    draw_text(fb, g_drm_w, g_drm_h, 24, 24, "SMART RETAIL", 3, 0xFFFFFFFF);
    if (g_last_product) {
        retail_money(price, sizeof(price), g_last_price_cents);
        retail_money(total, sizeof(total), g_last_total_cents);
        snprintf(line, sizeof(line), "ITEM:%s", g_last_product->name);
        draw_text(fb, g_drm_w, g_drm_h, 24, 62, line, 2, 0xFFFFFFFF);
        snprintf(line, sizeof(line), "PRICE:%s", price);
        draw_text(fb, g_drm_w, g_drm_h, 24, 88, line, 2, 0xFFFFFF00);
        snprintf(line, sizeof(line), "COUNT:%d  TOTAL:%s", g_last_item_count, total);
        draw_text(fb, g_drm_w, g_drm_h, 24, 114, line, 2, 0xFF00FF00);
        snprintf(line, sizeof(line), "STATUS:%s", g_status_text);
        draw_text(fb, g_drm_w, g_drm_h, 24, 140, line, 2, g_checkout_done ? 0xFF00FFFF : 0xFFFFFFFF);
        draw_text(fb, g_drm_w, g_drm_h, 24, 166, g_order_id, 1, g_checkout_done ? 0xFF00FFFF : 0xFFFFFFFF);
        draw_text(fb, g_drm_w, g_drm_h, 24, 190, g_payment_url, 1, 0xFFFFFFFF);
    } else {
        draw_text(fb, g_drm_w, g_drm_h, 24, 72, "SCAN PRODUCT QR", 2, 0xFFFFFFFF);
        draw_text(fb, g_drm_w, g_drm_h, 24, 104, "QR:COLA MILK BREAD", 2, 0xFFFFFF00);
        draw_text(fb, g_drm_w, g_drm_h, 24, 136, "CONTROL:CHECKOUT CLEAR", 2, 0xFF00FFFF);
    }
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

    int rx0 = min_x * g_drm_w / (int)cam_w;
    int ry0 = min_y * g_drm_h / (int)cam_h;
    int rx1 = max_x * g_drm_w / (int)cam_w;
    int ry1 = max_y * g_drm_h / (int)cam_h;
    int rx = clamp_int(rx0, 0, g_drm_w - 1);
    int ry = clamp_int(ry0, 0, g_drm_h - 1);
    int rw = clamp_int(rx1 - rx0, 1, g_drm_w - rx);
    int rh = clamp_int(ry1 - ry0, 1, g_drm_h - ry);

    draw_rect_rgb((uint32_t *)g_drm_map, g_drm_w, g_drm_h,
                  rx, ry, rw, rh, 0xFF00FF00, 4);
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
    if (!qr || quirc_resize(qr, cam_w, cam_h) < 0) {
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
    if (camera_stream_on() < 0) return 1;

    printf("\n[scanner] %ux%u | display:%s | quirc QR\n"
           "  Put QR code in front of camera. Ctrl+C to stop.\n\n",
           cam_w, cam_h, display_name);
    fflush(stdout);

    unsigned int frame = 0, found = 0;
    int64_t t0 = now_ms();
    char last[256] = "";
    int qr_absent_frames = 0;
    int saw_qr_this_frame = 0;

    while (g_running) {
        unsigned int idx, yl, uvl;
        uint8_t *yp, *uvp;
        saw_qr_this_frame = 0;

        if (camera_grab(&idx, &yp, &yl, &uvp, &uvl) < 0) {
            if (!g_running) break;
            continue;
        }
        frame++;

        /* LVDS 显示 */
        if (have_display) {
            (void)uvl;
            nv12_to_xrgb_scaled(yp, uvp, cam_w, cam_h,
                                g_y_stride, g_uv_stride,
                                (uint32_t *)g_drm_map,
                                g_drm_w, g_drm_h,
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
                if (have_display) {
                    draw_qr_outline(&code, cam_w, cam_h);
                }
                if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
                    saw_qr_this_frame = 1;
                    int same_payload = strcmp((char *)data.payload, last) == 0;
                    if (!same_payload) {
                        int handled_control = retail_handle_control_payload((char *)data.payload);
                        const struct retail_product *product = NULL;
                        if (!handled_control) product = retail_add_payload((char *)data.payload);

                        printf("\n>>> QR CODE: %s <<<\n", data.payload);
                        printf("    ver:%d ecc:%c\n",
                               data.version, "MLHQ"[data.ecc_level]);
                        if (handled_control) {
                            printf("    control:%s\n", g_status_text);
                        } else if (product) {
                            printf("    added:%s total:%d.%02d\n", product->name,
                                   g_last_total_cents / 100, g_last_total_cents % 100);
                        } else {
                            snprintf(g_status_text, sizeof(g_status_text), "NO PRODUCT MAP");
                            printf("    no product mapping\n");
                        }
                        fflush(stdout);

                        strncpy(last, (char *)data.payload, sizeof(last) - 1);
                        last[sizeof(last) - 1] = '\0';
                        found++;
                        if (!continuous) g_running = 0;
                    }
                }
            }
        }

        if (saw_qr_this_frame) {
            qr_absent_frames = 0;
        } else if (++qr_absent_frames > 12) {
            last[0] = 0;
        }

        if (have_display) {
            draw_retail_overlay();
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
