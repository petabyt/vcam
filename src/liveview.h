#ifndef VCAM_LIVEVIEW_H
#define VCAM_LIVEVIEW_H

#include <stdint.h>

/* Caller owns the JPEG. Animation is based on CLOCK_MONOTONIC at 60 Hz. */
int vcam_liveview_jpeg(unsigned int width, unsigned int height, int quality,
	unsigned char **data, uint32_t *size);

#define VCAM_LV_WIDTH 640
#define VCAM_LV_HEIGHT 480
#define VCAM_LV_QUALITY 80

#endif
