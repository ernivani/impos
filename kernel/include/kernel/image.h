#ifndef _KERNEL_IMAGE_H
#define _KERNEL_IMAGE_H

#include <stdint.h>

typedef struct {
    uint32_t *pixels;   /* ARGB 0xAARRGGBB, heap-allocated */
    int       width;
    int       height;
} image_t;

/* Decode a BMP from raw data. Returns NULL on failure. */
image_t *image_load_bmp(const void *data, uint32_t size);

/* Decode a PNG from raw data. Returns NULL on failure. */
image_t *image_load_png(const void *data, uint32_t size);

/* Auto-detect format and decode. Returns NULL on failure. */
image_t *image_load(const void *data, uint32_t size);

/* Load an image from the filesystem by path. Returns NULL on failure. */
image_t *image_load_file(const char *path);

/* Free an image and its pixel buffer. */
void image_free(image_t *img);

/* Scale image using bilinear interpolation. Returns new image. */
image_t *image_scale(const image_t *src, int new_w, int new_h);

#endif
