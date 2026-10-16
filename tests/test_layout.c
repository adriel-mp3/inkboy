#include "layout.h"

#include <assert.h>
#include <stddef.h>

static void
test_buffer_size(void)
{
    size_t size = 0U;

    assert(inkboy_buffer_size(10U, 20U, &size) == 0);
    assert(size == 400U);
    assert(inkboy_buffer_size(0U, 20U, &size) != 0);
    assert(inkboy_buffer_size(10U, 0U, &size) != 0);
    assert(inkboy_buffer_size(10U, 20U, NULL) != 0);
}

static void
test_normalize_axis(void)
{
    assert(inkboy_normalize_axis(-10, 0, 100, 100U) == 0U);
    assert(inkboy_normalize_axis(0, 0, 100, 100U) == 0U);
    assert(inkboy_normalize_axis(50, 0, 100, 100U) == 49U);
    assert(inkboy_normalize_axis(100, 0, 100, 100U) == 99U);
    assert(inkboy_normalize_axis(110, 0, 100, 100U) == 99U);
    assert(inkboy_normalize_axis(50, 100, 0, 100U) == 0U);
    assert(inkboy_normalize_axis(50, 0, 100, 0U) == 0U);
}

static void
test_layout_and_controls(void)
{
    struct inkboy_layout layout;
    uint32_t dpad_center_x;
    uint32_t dpad_center_y;

    assert(inkboy_layout_build(&layout, 1236U, 1648U) == 0);
    assert(layout.width == 1236U);
    assert(layout.height == 1648U);
    assert(layout.duck_size >= 192U);

    dpad_center_x = layout.dpad.x + layout.dpad.width / 2U;
    dpad_center_y = layout.dpad.y + layout.dpad.height / 2U;
    assert(inkboy_control_at_point(&layout, dpad_center_x,
                                   layout.dpad.y + 1U) == INKBOY_CONTROL_UP);
    assert(inkboy_control_at_point(&layout, dpad_center_x,
                                   layout.dpad.y + layout.dpad.height - 1U) ==
           INKBOY_CONTROL_DOWN);
    assert(inkboy_control_at_point(&layout, layout.dpad.x + 1U,
                                   dpad_center_y) == INKBOY_CONTROL_LEFT);
    assert(inkboy_control_at_point(&layout,
                                   layout.dpad.x + layout.dpad.width - 1U,
                                   dpad_center_y) == INKBOY_CONTROL_RIGHT);
    assert(inkboy_control_at_point(&layout, dpad_center_x, dpad_center_y) ==
           INKBOY_CONTROL_NONE);
    assert(inkboy_control_at_point(&layout, layout.a.center_x,
                                   layout.a.center_y) == INKBOY_CONTROL_A);
    assert(inkboy_control_at_point(&layout, layout.b.center_x,
                                   layout.b.center_y) == INKBOY_CONTROL_B);
    assert(inkboy_control_at_point(&layout,
                                   layout.exit.x + layout.exit.width / 2U,
                                   layout.exit.y + layout.exit.height / 2U) ==
           INKBOY_CONTROL_EXIT);
    assert(inkboy_control_at_point(&layout, 0U, 0U) == INKBOY_CONTROL_NONE);
    assert(!inkboy_control_contains_point(&layout, INKBOY_CONTROL_A,
                                          layout.a.center_x + layout.a.radius + 1U,
                                          layout.a.center_y));
    assert(inkboy_is_interactive_control(INKBOY_CONTROL_A));
    assert(!inkboy_is_interactive_control(INKBOY_CONTROL_EXIT));
    assert(inkboy_control_name(INKBOY_CONTROL_START) != NULL);
    assert(inkboy_control_name(INKBOY_CONTROL_NONE) == NULL);
}

static void
test_invalid_layouts(void)
{
    struct inkboy_layout layout;

    assert(inkboy_layout_build(NULL, 1236U, 1648U) != 0);
    assert(inkboy_layout_build(&layout, 0U, 1648U) != 0);
    assert(inkboy_layout_build(&layout, 1236U, 0U) != 0);
    assert(inkboy_layout_build(&layout, 100U, 100U) != 0);
}

int
main(void)
{
    test_buffer_size();
    test_normalize_axis();
    test_layout_and_controls();
    test_invalid_layouts();
    return 0;
}
