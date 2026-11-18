#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <jpeglib.h>
#include "liveview.h"

int vcam_liveview_jpeg(unsigned int width, unsigned int height, int quality,
	unsigned char **data, uint32_t *size) {
	static const unsigned char colors[][3] = {
		{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
		{255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0},
	};
	struct timespec now;
	if (!width || !height || clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
	uint64_t frame = (uint64_t)now.tv_sec * 60 + (uint64_t)now.tv_nsec * 60 / 1000000000;
	unsigned char *row = malloc((size_t)width * 3);
	if (!row) return -1;
	struct jpeg_compress_struct jpeg;
	struct jpeg_error_mgr error;
	jpeg.err = jpeg_std_error(&error);
	jpeg_create_compress(&jpeg);
	unsigned long length = 0;
	unsigned char *output = NULL;
	jpeg_mem_dest(&jpeg, &output, &length);
	jpeg.image_width = width;
	jpeg.image_height = height;
	jpeg.input_components = 3;
	jpeg.in_color_space = JCS_RGB;
	jpeg_set_defaults(&jpeg);
	jpeg_set_quality(&jpeg, quality, TRUE);
	jpeg_start_compress(&jpeg, TRUE);
	for (unsigned int x = 0; x < width; x++) {
		unsigned int bar = ((x + frame * 2) % width) * 8 / width;
		for (int c = 0; c < 3; c++) row[x * 3 + c] = colors[bar][c];
	}
	while (jpeg.next_scanline < height) {
		JSAMPROW rows[] = { row };
		jpeg_write_scanlines(&jpeg, rows, 1);
	}
	jpeg_finish_compress(&jpeg);
	jpeg_destroy_compress(&jpeg);
	free(row);
	if (length > UINT32_MAX) { free(output); return -1; }
	*data = output;
	*size = (uint32_t)length;
	return 0;
}
