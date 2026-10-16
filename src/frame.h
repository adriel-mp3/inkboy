#ifndef INKBOY_FRAME_H
#define INKBOY_FRAME_H

#include <stddef.h>
#include <stdint.h>

struct inkboy_video_frame {
    const uint32_t *pixels;
    unsigned width;
    unsigned height;
    size_t pitch;
};

#endif
