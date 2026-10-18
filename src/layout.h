#ifndef INKBOY_LAYOUT_H
#define INKBOY_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef INKBOY_LAYOUT_MARGIN_PIXELS
#define INKBOY_LAYOUT_MARGIN_PIXELS 40U
#endif

enum inkboy_control {
    INKBOY_CONTROL_NONE,
    INKBOY_CONTROL_EXIT,
    INKBOY_CONTROL_UP,
    INKBOY_CONTROL_DOWN,
    INKBOY_CONTROL_LEFT,
    INKBOY_CONTROL_RIGHT,
    INKBOY_CONTROL_A,
    INKBOY_CONTROL_B,
    INKBOY_CONTROL_L,
    INKBOY_CONTROL_R,
    INKBOY_CONTROL_START,
    INKBOY_CONTROL_SELECT
};

struct inkboy_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct inkboy_circle {
    uint32_t center_x;
    uint32_t center_y;
    uint32_t radius;
};

struct inkboy_layout {
    uint32_t width;
    uint32_t height;
    uint32_t header_bottom;
    uint32_t main_bottom;
    uint32_t primary_bottom;
    uint32_t viewport_x;
    uint32_t viewport_y;
    uint32_t viewport_size;
    struct inkboy_rect exit;
    struct inkboy_rect dpad;
    uint32_t dpad_arm;
    struct inkboy_circle a;
    struct inkboy_circle b;
    struct inkboy_rect l;
    struct inkboy_rect r;
    struct inkboy_rect start;
    struct inkboy_rect select;
};

int inkboy_layout_build(struct inkboy_layout *layout,
                        uint32_t width,
                        uint32_t height);
int inkboy_buffer_size(uint32_t width, uint32_t height, size_t *size);
uint32_t inkboy_normalize_axis(int raw,
                               int minimum,
                               int maximum,
                               uint32_t output_size);
bool inkboy_point_in_rect(uint32_t x,
                          uint32_t y,
                          const struct inkboy_rect *rect);
bool inkboy_point_in_circle(uint32_t x,
                            uint32_t y,
                            const struct inkboy_circle *circle);
enum inkboy_control inkboy_control_at_point(const struct inkboy_layout *layout,
                                            uint32_t x,
                                            uint32_t y);
bool inkboy_control_contains_point(const struct inkboy_layout *layout,
                                   enum inkboy_control control,
                                   uint32_t x,
                                   uint32_t y);
bool inkboy_is_interactive_control(enum inkboy_control control);
const char *inkboy_control_name(enum inkboy_control control);

#endif
