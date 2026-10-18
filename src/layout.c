#include "layout.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEWPORT_MAX_SIZE 420U
#define VIEWPORT_MIN_SIZE 192U

/* Vertical bands are percentages of the margin-inset content height. */
#define HEADER_SAFE_PERCENT 10U
#define MAIN_SECTION_BOTTOM_PERCENT 55U
#define PRIMARY_SECTION_BOTTOM_PERCENT 90U
#define PRIMARY_SECTION_RESERVE_PERCENT 10U

/* Header controls and viewport use the margin-inset content box. */
#define HEADER_SIDE_MARGIN_PERCENT 3U
#define EXIT_TOUCH_WIDTH_PERCENT 14U
#define EXIT_TOUCH_HEIGHT_PERCENT 6U
#define VIEWPORT_WIDTH_PERCENT 38U
#define VIEWPORT_SECTION_HEIGHT_PERCENT 70U

/* Gameplay controls: x uses content width; y uses the primary section. */
#define DPAD_WIDTH_PERCENT 36U
#define DPAD_LEFT_PERCENT 7U
#define DPAD_TOP_IN_PRIMARY_PERCENT 22U
#define DPAD_ARM_PERCENT 34U
#define ACTION_BUTTON_DIAMETER_PERCENT 15U
#define ACTION_A_CENTER_X_PERCENT 79U
#define ACTION_A_CENTER_Y_IN_PRIMARY_PERCENT 36U
#define ACTION_B_CENTER_X_PERCENT 71U
#define ACTION_B_CENTER_Y_IN_PRIMARY_PERCENT 70U

/* Lower controls use content width for x and content height for y/height. */
#define SHOULDER_WIDTH_PERCENT 27U
#define SHOULDER_HEIGHT_PERCENT 6U
#define SHOULDER_SIDE_OFFSET_PERCENT 5U
#define SHOULDER_TOP_OFFSET_PERCENT 1U
#define MENU_BUTTON_WIDTH_PERCENT 20U
#define MENU_BUTTON_HEIGHT_PERCENT 6U
#define SELECT_LEFT_PERCENT 29U
#define MENU_BUTTON_TOP_OFFSET_PERCENT 2U
#define START_LEFT_PERCENT 51U

static uint32_t
percent_of(uint32_t value, uint32_t percent)
{
    return (uint32_t)(((uint64_t)value * percent) / 100U);
}

static bool
rect_inside_margin(const struct inkboy_rect *rect, uint32_t margin,
                   uint32_t right, uint32_t bottom)
{
    return rect->x >= margin && rect->y >= margin &&
           rect->x + rect->width <= right &&
           rect->y + rect->height <= bottom;
}

static bool
circle_inside_margin(const struct inkboy_circle *circle, uint32_t margin,
                     uint32_t right, uint32_t bottom)
{
    return circle->center_x >= margin + circle->radius &&
           circle->center_x + circle->radius <= right &&
           circle->center_y >= margin + circle->radius &&
           circle->center_y + circle->radius <= bottom;
}

static bool
layout_sections_fit(const struct inkboy_layout *layout,
                    uint32_t content_bottom, uint32_t content_height)
{
    return layout->main_bottom > layout->header_bottom &&
           layout->primary_bottom > layout->main_bottom &&
           layout->primary_bottom +
               percent_of(content_height, PRIMARY_SECTION_RESERVE_PERCENT) <=
               content_bottom;
}

static bool
viewport_inside_margin(const struct inkboy_layout *layout, uint32_t margin,
                       uint32_t right, uint32_t bottom)
{
    return layout->viewport_size >= VIEWPORT_MIN_SIZE &&
           layout->viewport_x >= margin && layout->viewport_y >= margin &&
           layout->viewport_x + layout->viewport_size <= right &&
           layout->viewport_y + layout->viewport_size <= bottom;
}

static bool
controls_inside_margin(const struct inkboy_layout *layout, uint32_t margin,
                       uint32_t right, uint32_t bottom)
{
    return rect_inside_margin(&layout->exit, margin, right, bottom) &&
           rect_inside_margin(&layout->dpad, margin, right, bottom) &&
           circle_inside_margin(&layout->a, margin, right, bottom) &&
           circle_inside_margin(&layout->b, margin, right, bottom) &&
           rect_inside_margin(&layout->l, margin, right, bottom) &&
           rect_inside_margin(&layout->r, margin, right, bottom) &&
           rect_inside_margin(&layout->select, margin, right, bottom) &&
           rect_inside_margin(&layout->start, margin, right, bottom) &&
           layout->dpad.y + layout->dpad.height <= layout->primary_bottom &&
           layout->start.x + layout->start.width <= layout->width;
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

static void
set_vertical_sections(struct inkboy_layout *layout, uint32_t margin,
                      uint32_t content_height)
{
    layout->header_bottom = margin + percent_of(content_height, HEADER_SAFE_PERCENT);
    layout->main_bottom = margin +
                          percent_of(content_height, MAIN_SECTION_BOTTOM_PERCENT);
    layout->primary_bottom = margin +
                             percent_of(content_height, PRIMARY_SECTION_BOTTOM_PERCENT);
}

static void
set_exit_button(struct inkboy_layout *layout, uint32_t content_width,
                uint32_t content_height, uint32_t content_right)
{
    layout->exit.width = percent_of(content_width, EXIT_TOUCH_WIDTH_PERCENT);
    layout->exit.height = percent_of(content_height, EXIT_TOUCH_HEIGHT_PERCENT);
    layout->exit.x = content_right -
                     percent_of(content_width, HEADER_SIDE_MARGIN_PERCENT) -
                     layout->exit.width;
    layout->exit.y = layout->header_bottom;
}

static void
set_viewport(struct inkboy_layout *layout, uint32_t margin,
             uint32_t content_width)
{
    uint32_t max_height = percent_of(
        layout->main_bottom - layout->header_bottom,
        VIEWPORT_SECTION_HEIGHT_PERCENT);

    layout->viewport_size = percent_of(content_width, VIEWPORT_WIDTH_PERCENT);
    if (layout->viewport_size > VIEWPORT_MAX_SIZE) {
        layout->viewport_size = VIEWPORT_MAX_SIZE;
    }
    if (layout->viewport_size < VIEWPORT_MIN_SIZE) {
        layout->viewport_size = VIEWPORT_MIN_SIZE;
    }
    if (layout->viewport_size > max_height) {
        layout->viewport_size = max_height;
    }
    layout->viewport_x = margin + (content_width - layout->viewport_size) / 2U;
    layout->viewport_y = layout->header_bottom +
        (layout->main_bottom - layout->header_bottom - layout->viewport_size) / 2U;
}

static void
set_directional_pad(struct inkboy_layout *layout, uint32_t margin,
                    uint32_t content_width)
{
    layout->dpad.width = percent_of(content_width, DPAD_WIDTH_PERCENT);
    layout->dpad.height = layout->dpad.width;
    layout->dpad.x = margin + percent_of(content_width, DPAD_LEFT_PERCENT);
    layout->dpad.y = layout->main_bottom +
        percent_of(layout->primary_bottom - layout->main_bottom,
                   DPAD_TOP_IN_PRIMARY_PERCENT);
    layout->dpad_arm = percent_of(layout->dpad.width, DPAD_ARM_PERCENT);
}

static void
set_action_buttons(struct inkboy_layout *layout, uint32_t margin,
                   uint32_t content_width)
{
    uint32_t action_band_height = layout->primary_bottom - layout->main_bottom;

    layout->a.radius = percent_of(content_width, ACTION_BUTTON_DIAMETER_PERCENT) / 2U;
    layout->a.center_x = margin + percent_of(content_width, ACTION_A_CENTER_X_PERCENT);
    layout->a.center_y = layout->main_bottom +
        percent_of(action_band_height, ACTION_A_CENTER_Y_IN_PRIMARY_PERCENT);
    layout->b.radius = layout->a.radius;
    layout->b.center_x = margin + percent_of(content_width, ACTION_B_CENTER_X_PERCENT);
    layout->b.center_y = layout->main_bottom +
        percent_of(action_band_height, ACTION_B_CENTER_Y_IN_PRIMARY_PERCENT);
}

static void
set_shoulder_buttons(struct inkboy_layout *layout, uint32_t margin,
                     uint32_t content_width, uint32_t content_height,
                     uint32_t content_right)
{
    layout->l.width = percent_of(content_width, SHOULDER_WIDTH_PERCENT);
    layout->l.height = percent_of(content_height, SHOULDER_HEIGHT_PERCENT);
    layout->l.x = margin + percent_of(content_width, SHOULDER_SIDE_OFFSET_PERCENT);
    layout->l.y = layout->main_bottom +
                  percent_of(content_height, SHOULDER_TOP_OFFSET_PERCENT);
    layout->r = layout->l;
    layout->r.x = content_right -
                  percent_of(content_width, SHOULDER_SIDE_OFFSET_PERCENT) -
                  layout->r.width;
}

static void
set_menu_buttons(struct inkboy_layout *layout, uint32_t margin,
                 uint32_t content_width, uint32_t content_height)
{
    layout->select.width = percent_of(content_width, MENU_BUTTON_WIDTH_PERCENT);
    layout->select.height = percent_of(content_height, MENU_BUTTON_HEIGHT_PERCENT);
    layout->select.x = margin + percent_of(content_width, SELECT_LEFT_PERCENT);
    layout->select.y = layout->primary_bottom +
                       percent_of(content_height, MENU_BUTTON_TOP_OFFSET_PERCENT);
    layout->start = layout->select;
    layout->start.x = margin + percent_of(content_width, START_LEFT_PERCENT);
}

int
inkboy_layout_build(struct inkboy_layout *layout, uint32_t width, uint32_t height)
{
    const uint32_t margin = INKBOY_LAYOUT_MARGIN_PIXELS;
    uint32_t content_width;
    uint32_t content_height;
    uint32_t content_right;
    uint32_t content_bottom;

    if (layout == NULL) {
        return -1;
    }

    memset(layout, 0, sizeof(*layout));
    layout->width = width;
    layout->height = height;
    if (width == 0U || height == 0U ||
        (uint64_t)margin * 2U >= width || (uint64_t)margin * 2U >= height) {
        fprintf(stderr, "inkboy: screen is too small for the composition\n");
        return -1;
    }

    content_width = width - margin * 2U;
    content_height = height - margin * 2U;
    content_right = width - margin;
    content_bottom = height - margin;

    set_vertical_sections(layout, margin, content_height);
    set_exit_button(layout, content_width, content_height, content_right);
    set_viewport(layout, margin, content_width);
    set_directional_pad(layout, margin, content_width);
    set_action_buttons(layout, margin, content_width);
    set_shoulder_buttons(layout, margin, content_width, content_height,
                         content_right);
    set_menu_buttons(layout, margin, content_width, content_height);

    if (!layout_sections_fit(layout, content_bottom, content_height) ||
        !viewport_inside_margin(layout, margin, content_right, content_bottom) ||
        !controls_inside_margin(layout, margin, content_right, content_bottom)) {
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

/* Return whether the point is on the D-pad cross; its center is a dead zone. */
static bool
dpad_control_at_point(const struct inkboy_layout *layout,
                      uint32_t x, uint32_t y,
                      enum inkboy_control *control_out)
{
    uint32_t center_x = layout->dpad.x + layout->dpad.width / 2U;
    uint32_t center_y = layout->dpad.y + layout->dpad.height / 2U;
    uint32_t arm_left = center_x - layout->dpad_arm / 2U;
    uint32_t arm_right = arm_left + layout->dpad_arm;
    uint32_t arm_top = center_y - layout->dpad_arm / 2U;
    uint32_t arm_bottom = arm_top + layout->dpad_arm;
    bool on_vertical_arm = x >= arm_left && x < arm_right &&
                           y >= layout->dpad.y &&
                           y < layout->dpad.y + layout->dpad.height;
    bool on_horizontal_arm = y >= arm_top && y < arm_bottom &&
                             x >= layout->dpad.x &&
                             x < layout->dpad.x + layout->dpad.width;

    if (!on_vertical_arm && !on_horizontal_arm) {
        return false;
    }
    if (x >= arm_left && x < arm_right && y >= arm_top && y < arm_bottom) {
        *control_out = INKBOY_CONTROL_NONE;
    } else {
        int64_t dx = (int64_t)x - center_x;
        int64_t dy = (int64_t)y - center_y;

        *control_out = llabs(dx) > llabs(dy)
            ? (dx < 0 ? INKBOY_CONTROL_LEFT : INKBOY_CONTROL_RIGHT)
            : (dy < 0 ? INKBOY_CONTROL_UP : INKBOY_CONTROL_DOWN);
    }
    return true;
}

enum inkboy_control
inkboy_control_at_point(const struct inkboy_layout *layout, uint32_t x, uint32_t y)
{
    enum inkboy_control dpad_control;

    if (layout == NULL) {
        return INKBOY_CONTROL_NONE;
    }

    if (inkboy_point_in_rect(x, y, &layout->exit)) {
        return INKBOY_CONTROL_EXIT;
    }
    if (dpad_control_at_point(layout, x, y, &dpad_control)) {
        return dpad_control;
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
