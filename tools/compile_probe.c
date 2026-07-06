#include <linux/videodev2.h>
#include <linux/fb.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
#include <stdio.h>

int main(void) {
    printf("%lu\n", (unsigned long)sizeof(struct v4l2_format));
    return 0;
}
