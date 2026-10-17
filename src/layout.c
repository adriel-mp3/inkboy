#include "layout.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEWPORT_MAX_SIZE 420U
#define VIEWPORT_MIN_SIZE 192U
#define HEADER_SAFE_PERCENT 10U
#define HEADER_SIDE_MARGIN_PERCENT 3U
#define EXIT_TOUCH_WIDTH_PERCENT 14U
#define EXIT_TOUCH_HEIGHT_PERCENT 6U

static uint32_t
percent_of(uint32_t value, uint32_t percent)
{
    return (uint32_t)(((uint64_t)value * percent) / 100U);
}

int
inkboy_buffer_size(uint32_t width, uint32_t height, size_t *size)
{
    uint64_t pixels;

    if (size == NULL || width == 0U || height == 0U) {
        return -1;
    }

    pixels = (uint64_t)width * (uint64_t)height;
    if (pixels > (uint64_t)(SIZE_MAX / 2U)) {
        return -1;
    }

    *size = (size_t)pixels * 2U;
    return 0;
}

int
inkboy_layout_build(struct inkboy_layout *layout, uint32_t width, uint32_t height)
{
    if (layout == NULL) {
        return -1;
    }

    memset(layout, 0, sizeof(*layout));
    layout->width = width;
    layout->height = height;
    layout->header_bottom = percent_of(height, HEADER_SAFE_PERCENT);
    layout->main_bottom = percent_of(height, 55U);
    layout->primary_bottom = percent_of(height, 90U);
    layout->exit.width = percent_of(width, EXIT_TOUCH_WIDTH_PERCENT);
    layout->exit.height = percent_of(height, EXIT_TOUCH_HEIGHT_PERCENT);
    layout->exit.x = width - percent_of(width, HEADER_SIDE_MARGIN_PERCENT) -
                     layout->exit.width;
    layout->exit.y = layout->header_bottom;

    layout->viewport_size = percent_of(width, 38U);
    if (layout->viewport_size > VIEWPORT_MAX_SIZE) {
        layout->viewport_size = VIEWPORT_MAX_SIZE;
    }
    if (layout->viewport_size < VIEWPORT_MIN_SIZE) {
        layout->viewport_size = VIEWPORT_MIN_SIZE;
    }
    if (layout->viewport_size > percent_of(layout->main_bottom - layout->header_bottom, 70U)) {
        layout->viewport_size = percent_of(layout->main_bottom - layout->header_bottom, 70U);
    }
    layout->viewport_x = (width - layout->viewport_size) / 2U;
    layout->viewport_y = layout->header_bottom +
                         (layout->main_bottom - layout->header_bottom - layout->viewport_size) / 2U;

    layout->dpad.width = percent_of(width, 36U);
    layout->dpad.height = layout->dpad.width;
    layout->dpad.x = percent_of(width, 7U);
    layout->dpad.y = layout->main_bottom +
                     percent_of(layout->primary_bottom - layout->main_bottom, 22U);
    layout->dpad_arm = percent_of(layout->dpad.width, 34U);

    layout->a.radius = percent_of(width, 15U) / 2U;
    layout->a.center_x = percent_of(width, 79U);
    layout->a.center_y = layout->main_bottom +
                         percent_of(layout->primary_bottom - layout->main_bottom, 36U);
    layout->b.radius = layout->a.radius;
    layout->b.center_x = percent_of(width, 71U);
    layout->b.center_y = layout->main_bottom +
                         percent_of(layout->primary_bottom - layout->main_bottom, 70U);

    layout->l.width = percent_of(width, 27U);
    layout->l.height = percent_of(height, 6U);
    layout->l.x = percent_of(width, 5U);
    layout->l.y = layout->main_bottom + percent_of(height, 1U);
    layout->r = layout->l;
    layout->r.x = width - percent_of(width, 5U) - layout->r.width;

    layout->select.width = percent_of(width, 20U);
    layout->select.height = percent_of(height, 6U);
    layout->select.x = percent_of(width, 29U);
    layout->select.y = layout->primary_bottom + percent_of(height, 2U);
    layout->start = layout->select;
    layout->start.x = percent_of(width, 51U);

    if (width == 0U || height == 0U ||
        layout->viewport_size < VIEWPORT_MIN_SIZE ||
        layout->main_bottom <= layout->header_bottom ||
        layout->primary_bottom <= layout->main_bottom ||
        layout->primary_bottom + percent_of(height, 10U) > height ||
        layout->exit.x + layout->exit.width > width ||
        layout->dpad.y + layout->dpad.height > layout->primary_bottom ||
        layout->r.x + layout->r.width > width ||
        layout->start.x + layout->start.width > width) {
        fprintf(stderr, "inkboy: screen is too small for the composition\n");
        return -1;
    }

    return 0;
}

uint32_t
inkboy_normalize_axis(int raw, int minimum, int maximum, uint32_t output_size)
{
    int64_t min_value = minimum;
    int64_t max_value = maximum;

    if (output_size == 0U || maximum <= minimum) {
        return 0U;
    }
    if (raw <= minimum) {
        return 0U;
    }
    if (raw >= maximum) {
        return output_size - 1U;
    }
    return (uint32_t)(((int64_t)raw - min_value) * (output_size - 1U) /
                      (max_value - min_value));
}

bool
inkboy_point_in_rect(uint32_t x, uint32_t y, const struct inkboy_rect *rect)
{
    return rect != NULL && x >= rect->x && x < rect->x + rect->width &&
           y >= rect->y && y < rect->y + rect->height;
}

bool
inkboy_point_in_circle(uint32_t x, uint32_t y, const struct inkboy_circle *circle)
{
    int64_t dx;
    int64_t dy;

    if (circle == NULL) {
        return false;
    }
    dx = (int64_t)x - circle->center_x;
    dy = (int64_t)y - circle->center_y;
    return dx * dx + dy * dy <= (int64_t)circle->radius * circle->radius;
}

bool
inkboy_is_interactive_control(enum inkboy_control control)
{
    return control >= INKBOY_CONTROL_UP && control <= INKBOY_CONTROL_SELECT;
}

enum inkboy_control
inkboy_control_at_point(const struct inkboy_layout *layout, uint32_t x, uint32_t y)
{
    uint32_t dpad_center_x;
    uint32_t dpad_center_y;
    uint32_t arm_left;
    uint32_t arm_right;
    uint32_t arm_top;
    uint32_t arm_bottom;

    if (layout == NULL) {
        return INKBOY_CONTROL_NONE;
    }

    dpad_center_x = layout->dpad.x + layout->dpad.width / 2U;
    dpad_center_y = layout->dpad.y + layout->dpad.height / 2U;
    arm_left = dpad_center_x - layout->dpad_arm / 2U;
    arm_right = arm_left + layout->dpad_arm;
    arm_top = dpad_center_y - layout->dpad_arm / 2U;
    arm_bottom = arm_top + layout->dpad_arm;

    if (inkboy_point_in_rect(x, y, &layout->exit)) {
        return INKBOY_CONTROL_EXIT;
    }
    if ((x >= arm_left && x < arm_right && y >= layout->dpad.y &&
         y < layout->dpad.y + layout->dpad.height) ||
        (y >= arm_top && y < arm_bottom && x >= layout->dpad.x &&
         x < layout->dpad.x + layout->dpad.width)) {
        if (x >= arm_left && x < arm_right && y >= arm_top && y < arm_bottom) {
            return INKBOY_CONTROL_NONE;
        }
        {
            int64_t dx = (int64_t)x - dpad_center_x;
            int64_t dy = (int64_t)y - dpad_center_y;

            if (llabs(dx) > llabs(dy)) {
                return dx < 0 ? INKBOY_CONTROL_LEFT : INKBOY_CONTROL_RIGHT;
            }
            return dy < 0 ? INKBOY_CONTROL_UP : INKBOY_CONTROL_DOWN;
        }
    }
    if (inkboy_point_in_circle(x, y, &layout->a)) {
        return INKBOY_CONTROL_A;
    }
    if (inkboy_point_in_circle(x, y, &layout->b)) {
        return INKBOY_CONTROL_B;
    }
    if (inkboy_point_in_rect(x, y, &layout->l)) {
        return INKBOY_CONTROL_L;
    }
    if (inkboy_point_in_rect(x, y, &layout->r)) {
        return INKBOY_CONTROL_R;
    }
    if (inkboy_point_in_rect(x, y, &layout->start)) {
        return INKBOY_CONTROL_START;
    }
    if (inkboy_point_in_rect(x, y, &layout->select)) {
        return INKBOY_CONTROL_SELECT;
    }
    return INKBOY_CONTROL_NONE;
}

bool
inkboy_control_contains_point(const struct inkboy_layout *layout,
                              enum inkboy_control control,
                              uint32_t x,
                              uint32_t y)
{
    switch (control) {
    case INKBOY_CONTROL_EXIT:
        return layout != NULL && inkboy_point_in_rect(x, y, &layout->exit);
    case INKBOY_CONTROL_A:
        return layout != NULL && inkboy_point_in_circle(x, y, &layout->a);
    case INKBOY_CONTROL_B:
        return layout != NULL && inkboy_point_in_circle(x, y, &layout->b);
    case INKBOY_CONTROL_L:
        return layout != NULL && inkboy_point_in_rect(x, y, &layout->l);
    case INKBOY_CONTROL_R:
        return layout != NULL && inkboy_point_in_rect(x, y, &layout->r);
    case INKBOY_CONTROL_START:
        return layout != NULL && inkboy_point_in_rect(x, y, &layout->start);
    case INKBOY_CONTROL_SELECT:
        return layout != NULL && inkboy_point_in_rect(x, y, &layout->select);
    case INKBOY_CONTROL_UP:
    case INKBOY_CONTROL_DOWN:
    case INKBOY_CONTROL_LEFT:
    case INKBOY_CONTROL_RIGHT:
        return inkboy_control_at_point(layout, x, y) == control;
    default:
        return false;
    }
}

const char *
inkboy_control_name(enum inkboy_control control)
{
    static const char *const names[] = {
        [INKBOY_CONTROL_UP] = "UP",
        [INKBOY_CONTROL_DOWN] = "DOWN",
        [INKBOY_CONTROL_LEFT] = "LEFT",
        [INKBOY_CONTROL_RIGHT] = "RIGHT",
        [INKBOY_CONTROL_A] = "A",
        [INKBOY_CONTROL_B] = "B",
        [INKBOY_CONTROL_L] = "L",
        [INKBOY_CONTROL_R] = "R",
        [INKBOY_CONTROL_START] = "START",
        [INKBOY_CONTROL_SELECT] = "SELECT"
    };

    if (control < 0 || (size_t)control >= sizeof(names) / sizeof(names[0])) {
        return NULL;
    }
    return names[control];
}
