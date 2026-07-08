#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quirc.h"

static int read_token(FILE *fp, char *out, size_t out_sz)
{
    int c;
    size_t n = 0;
    do {
        c = fgetc(fp);
        if (c == '#') {
            while (c != '\n' && c != EOF) c = fgetc(fp);
        }
    } while (c == ' ' || c == '\n' || c == '\r' || c == '\t');
    if (c == EOF) return -1;
    while (c != EOF && c != ' ' && c != '\n' && c != '\r' && c != '\t') {
        if (n + 1 < out_sz) out[n++] = (char)c;
        c = fgetc(fp);
    }
    out[n] = '\0';
    return n > 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s image.pgm\n", argv[0]);
        return 2;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("open pgm");
        return 2;
    }

    char tok[64];
    if (read_token(fp, tok, sizeof(tok)) < 0 || strcmp(tok, "P5") != 0) {
        fprintf(stderr, "not a P5 pgm\n");
        fclose(fp);
        return 2;
    }
    if (read_token(fp, tok, sizeof(tok)) < 0) return 2;
    int w = atoi(tok);
    if (read_token(fp, tok, sizeof(tok)) < 0) return 2;
    int h = atoi(tok);
    if (read_token(fp, tok, sizeof(tok)) < 0) return 2;
    int maxv = atoi(tok);
    if (w <= 0 || h <= 0 || maxv != 255) {
        fprintf(stderr, "unsupported pgm %dx%d max=%d\n", w, h, maxv);
        fclose(fp);
        return 2;
    }

    uint8_t *img = malloc((size_t)w * (size_t)h);
    if (!img) {
        fclose(fp);
        return 2;
    }
    if (fread(img, 1, (size_t)w * (size_t)h, fp) != (size_t)w * (size_t)h) {
        fprintf(stderr, "short pgm\n");
        free(img);
        fclose(fp);
        return 2;
    }
    fclose(fp);

    struct quirc *qr = quirc_new();
    if (!qr || quirc_resize(qr, w, h) < 0) {
        fprintf(stderr, "quirc init failed\n");
        free(img);
        return 2;
    }

    int qw, qh;
    uint8_t *dst = quirc_begin(qr, &qw, &qh);
    if (!dst || qw != w || qh != h) {
        fprintf(stderr, "quirc buffer mismatch\n");
        quirc_destroy(qr);
        free(img);
        return 2;
    }
    memcpy(dst, img, (size_t)w * (size_t)h);
    quirc_end(qr);

    int decoded = 0;
    for (int i = 0; i < quirc_count(qr); i++) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(qr, i, &code);
        if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
            printf("%s\n", data.payload);
            decoded++;
        }
    }

    quirc_destroy(qr);
    free(img);
    return decoded > 0 ? 0 : 1;
}
