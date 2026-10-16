#ifndef INKBOY_FRAME_H
#define INKBOY_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "layout.h"

struct inkboy_video_frame {
    const uint32_t *pixels;
    unsigned width;
    unsigned height;
    size_t pitch;
};

int inkboy_frame_rect(const struct inkboy_layout *layout,
                      const struct inkboy_video_frame *frame,
                      struct inkboy_rect *rect);
int inkboy_frame_convert(const struct inkboy_video_frame *frame,
                         uint8_t *ya, size_t size);
int inkboy_frame_scale(const uint8_t *ya, unsigned width, unsigned height,
                       uint8_t *output, uint32_t output_width,
                       uint32_t output_height, const struct inkboy_rect *rect);

#endif
