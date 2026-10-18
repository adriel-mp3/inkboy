#include "frame.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_pixels(unsigned width, unsigned height, uint32_t screen_width,
                         uint32_t screen_height)
{
    struct inkboy_layout layout;
    struct inkboy_rect rect;
    const uint32_t margin = INKBOY_LAYOUT_MARGIN_PIXELS;
    size_t pitch = ((size_t)width + 3U) * 4U;
    uint8_t *pixels = malloc(pitch * height);
    uint8_t *gray = malloc((size_t)width * height * 2U);
    size_t output_size;
    uint8_t *actual;
    uint8_t *expected;

    assert(pixels && gray);
    assert(inkboy_layout_build(&layout, screen_width, screen_height) == 0);
    assert(inkboy_buffer_size(screen_width, screen_height, &output_size) == 0);
    actual = malloc(output_size);
    expected = malloc(output_size);
    assert(actual && expected);
    memset(actual, 255, output_size);
    memset(expected, 255, output_size);
    for (unsigned y = 0U; y < height; y++) {
        for (unsigned x = 0U; x < width; x++) {
            uint32_t value = ((x * 31U + y * 7U) * 0x010307U) ^ 0xa548f032U;
            memcpy(pixels + (size_t)y * pitch + (size_t)x * 4U, &value, 4U);
        }
    }
    struct inkboy_video_frame frame = {(const uint32_t *)pixels, width, height, pitch};
    assert(inkboy_frame_rect(&layout, &frame, &rect) == 0);
    assert(rect.x >= margin && rect.y >= margin);
    assert(rect.x + rect.width <= screen_width - margin);
    assert(rect.y + rect.height <= screen_height - margin);
    assert(inkboy_frame_convert(&frame, gray, (size_t)width * height * 2U) == 0);
    assert(inkboy_frame_scale(gray, width, height, actual, screen_width, screen_height, &rect) == 0);
    /* TASK-008's original fused nearest sampling + grayscale reference. */
    for (uint32_t y = 0U; y < rect.height; y++) {
        unsigned sy = (unsigned)((uint64_t)y * height / rect.height);
        for (uint32_t x = 0U; x < rect.width; x++) {
            unsigned sx = (unsigned)((uint64_t)x * width / rect.width);
            uint32_t pixel;
            memcpy(&pixel, pixels + (size_t)sy * pitch + (size_t)sx * 4U, 4U);
            uint8_t gray_value = (uint8_t)((77U * (uint8_t)(pixel >> 16U) +
                150U * (uint8_t)(pixel >> 8U) + 29U * (uint8_t)pixel + 128U) >> 8U);
            expected[((size_t)(rect.y + y) * screen_width + rect.x + x) * 2U] = gray_value;
        }
    }
    assert(memcmp(actual, expected, output_size) == 0);
    assert(inkboy_frame_convert(&frame, gray, 0U) == -1);
    rect.x = screen_width;
    assert(inkboy_frame_scale(gray, width, height, actual, screen_width, screen_height, &rect) == -1);
    frame.pitch = 0U;
    assert(inkboy_frame_rect(&layout, &frame, &rect) == -1);
    free(pixels); free(gray); free(actual); free(expected);
}

int main(void)
{
    test_pixels(160U, 144U, 1236U, 1648U);
    test_pixels(161U, 147U, 900U, 1200U);
    test_pixels(144U, 160U, 1236U, 1648U);
    test_pixels(1U, 1U, 900U, 1200U);
    puts("frame: byte-identical to TASK-008, padded pitch, portrait and bounds pass");
    return 0;
}
