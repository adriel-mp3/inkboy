#include "frame.h"

#include <string.h>

static bool valid_frame(const struct inkboy_video_frame *frame)
{
    return frame != NULL && frame->pixels != NULL && frame->width > 0U &&
           frame->height > 0U && frame->width <= 1024U && frame->height <= 1024U &&
           frame->pitch >= (size_t)frame->width * sizeof(uint32_t) &&
           frame->pitch <= SIZE_MAX / frame->height;
}

int inkboy_frame_rect(const struct inkboy_layout *layout,
                      const struct inkboy_video_frame *frame,
                      struct inkboy_rect *rect)
{
    uint32_t width;
    uint32_t height;

    if (layout == NULL || rect == NULL || !valid_frame(frame)) {
        return -1;
    }
    width = height = layout->duck_size;
    if ((uint64_t)width * frame->height <= (uint64_t)height * frame->width) {
        height = (uint32_t)((uint64_t)width * frame->height / frame->width);
    } else {
        width = (uint32_t)((uint64_t)height * frame->width / frame->height);
    }
    if (width == 0U || height == 0U || layout->duck_x > layout->width ||
        layout->duck_y > layout->height ||
        layout->duck_size > layout->width - layout->duck_x ||
        layout->duck_size > layout->height - layout->duck_y) {
        return -1;
    }
    *rect = (struct inkboy_rect){
        layout->duck_x + (layout->duck_size - width) / 2U,
        layout->duck_y + (layout->duck_size - height) / 2U, width, height
    };
    return 0;
}

int inkboy_frame_convert(const struct inkboy_video_frame *frame,
                         uint8_t *ya, size_t size)
{
    if (!valid_frame(frame) || ya == NULL ||
        size < (size_t)frame->width * frame->height * 2U) {
        return -1;
    }
    for (unsigned y = 0U; y < frame->height; y++) {
        const unsigned char *row = (const unsigned char *)frame->pixels +
                                  (size_t)y * frame->pitch;
        for (unsigned x = 0U; x < frame->width; x++) {
            uint32_t pixel;
            size_t offset = ((size_t)y * frame->width + x) * 2U;

            memcpy(&pixel, row + (size_t)x * 4U, sizeof(pixel));
            ya[offset] = (uint8_t)((77U * ((pixel >> 16U) & 255U) +
                                   150U * ((pixel >> 8U) & 255U) +
                                   29U * (pixel & 255U) + 128U) >> 8U);
            ya[offset + 1U] = UINT8_MAX;
        }
    }
    return 0;
}

int inkboy_frame_scale(const uint8_t *ya, unsigned width, unsigned height,
                       uint8_t *output, uint32_t output_width,
                       uint32_t output_height, const struct inkboy_rect *rect)
{
    if (ya == NULL || output == NULL || rect == NULL || width == 0U ||
        height == 0U || width > 1024U || height > 1024U ||
        rect->width == 0U || rect->height == 0U ||
        rect->x > output_width || rect->y > output_height ||
        rect->width > output_width - rect->x ||
        rect->height > output_height - rect->y ||
        output_width == 0U || output_height > SIZE_MAX / 2U / output_width) {
        return -1;
    }
    for (uint32_t y = 0U; y < rect->height; y++) {
        size_t source_y = (size_t)((uint64_t)y * height / rect->height);
        uint8_t *row = output + ((size_t)(rect->y + y) * output_width + rect->x) * 2U;

        for (uint32_t x = 0U; x < rect->width; x++) {
            size_t source_x = (size_t)((uint64_t)x * width / rect->width);

            memcpy(row + (size_t)x * 2U, ya + (source_y * width + source_x) * 2U, 2U);
        }
    }
    return 0;
}
