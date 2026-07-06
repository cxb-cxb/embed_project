/*
 * V4L2 NV12 camera to stdout PGM stream.
 *
 * This is a PC preview helper for boards without a working LVDS screen.
 * Run through adb exec-out and decode the P5 frames on the host.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <linux/videodev2.h>

#define CAMERA_DEV "/dev/video5"
#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600
#define MAX_BUFFERS 4

static int g_fd = -1;
static void *g_y_ptr[MAX_BUFFERS];
static unsigned int g_y_len[MAX_BUFFERS];
static unsigned int g_nbufs = 0;
static unsigned int g_y_stride = 0;

static int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do {
        r = ioctl(fd, req, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static int camera_open(const char *dev, unsigned int *w, unsigned int *h)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;

    g_fd = open(dev, O_RDWR);
    if (g_fd < 0) {
        perror("open camera");
        return -1;
    }

    memset(&cap, 0, sizeof(cap));
    if (xioctl(g_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("QUERYCAP");
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = *w;
    fmt.fmt.pix_mp.height = *h;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    if (xioctl(g_fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("S_FMT");
        return -1;
    }
    *w = fmt.fmt.pix_mp.width;
    *h = fmt.fmt.pix_mp.height;
    g_y_stride = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    if (g_y_stride == 0) g_y_stride = *w;

    memset(&req, 0, sizeof(req));
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = MAX_BUFFERS;
    if (xioctl(g_fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("REQBUFS");
        return -1;
    }
    g_nbufs = req.count;

    for (unsigned int i = 0; i < g_nbufs; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[2];
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 2;
        if (xioctl(g_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("QUERYBUF");
            return -1;
        }
        g_y_len[i] = planes[0].length;
        g_y_ptr[i] = mmap(NULL, g_y_len[i], PROT_READ | PROT_WRITE,
                          MAP_SHARED, g_fd, planes[0].m.mem_offset);
        if (g_y_ptr[i] == MAP_FAILED) {
            perror("mmap y");
            return -1;
        }
    }
    return 0;
}

static int camera_stream_on(void)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    for (unsigned int i = 0; i < g_nbufs; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[2];
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 2;
        if (xioctl(g_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("QBUF");
            return -1;
        }
    }
    return xioctl(g_fd, VIDIOC_STREAMON, &type);
}

static int camera_grab(unsigned int *idx, uint8_t **y)
{
    struct v4l2_buffer buf;
    struct v4l2_plane planes[2];
    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length = 2;
    if (xioctl(g_fd, VIDIOC_DQBUF, &buf) < 0) return -1;
    *idx = buf.index;
    *y = (uint8_t *)g_y_ptr[buf.index];
    return 0;
}

static void camera_release(unsigned int idx)
{
    struct v4l2_buffer buf;
    struct v4l2_plane planes[2];
    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = idx;
    buf.m.planes = planes;
    buf.length = 2;
    xioctl(g_fd, VIDIOC_QBUF, &buf);
}

int main(int argc, char **argv)
{
    const char *dev = CAMERA_DEV;
    unsigned int w = DEFAULT_WIDTH;
    unsigned int h = DEFAULT_HEIGHT;
    unsigned int frame_limit = 0;
    int opt;

    while ((opt = getopt(argc, argv, "d:W:H:n:h")) != -1) {
        switch (opt) {
        case 'd': dev = optarg; break;
        case 'W': w = (unsigned int)atoi(optarg); break;
        case 'H': h = (unsigned int)atoi(optarg); break;
        case 'n': frame_limit = (unsigned int)atoi(optarg); break;
        default:
            fprintf(stderr, "Usage: %s [-d dev] [-W width] [-H height] [-n frames]\n", argv[0]);
            return 1;
        }
    }

    if (camera_open(dev, &w, &h) < 0) return 1;
    if (camera_stream_on() < 0) return 1;

    unsigned int frame = 0;
    while (frame_limit == 0 || frame < frame_limit) {
        unsigned int idx;
        uint8_t *y;
        if (camera_grab(&idx, &y) < 0) continue;

        printf("P5\n%u %u\n255\n", w, h);
        for (unsigned int row = 0; row < h; row++) {
            fwrite(y + row * g_y_stride, 1, w, stdout);
        }
        fflush(stdout);

        camera_release(idx);
        frame++;
    }
    return 0;
}
