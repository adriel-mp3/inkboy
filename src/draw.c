#include "draw.h"

#include <stddef.h>
#include <stdint.h>

#define LABEL_GLYPH_WIDTH 5U
#define LABEL_GLYPH_HEIGHT 7U
#define CONTROL_SCALE 4U
#define CONTROL_LINE_WIDTH 4U

static const uint8_t *
label_glyph(char character)
{
    static const uint8_t i[] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f};
    static const uint8_t e[] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    static const uint8_t x[] = {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
    static const uint8_t t[] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t a[] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    static const uint8_t r[] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    static const uint8_t s[] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    static const uint8_t l[] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
    static const uint8_t c[] = {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f};
    static const uint8_t b[] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};

    switch (character) {
    case 'I': return i;
    case 'E': return e;
    case 'X': return x;
    case 'T': return t;
    case 'A': return a;
    case 'R': return r;
    case 'S': return s;
    case 'L': return l;
    case 'C': return c;
    case 'B': return b;
    default: return NULL;
    }
}

static void
set_pixel(uint8_t *buffer, uint32_t width, uint32_t x, uint32_t y, uint8_t gray)
{
    size_t offset = ((size_t)y * width + x) * 2U;

    /* FBInk's YA input is luminance followed by alpha. */
    buffer[offset] = gray;
    buffer[offset + 1U] = UINT8_MAX;
}

static void
draw_label(uint8_t *buffer, uint32_t screen_width, uint32_t origin_x, uint32_t origin_y,
           const char *text, uint32_t scale)
{
    uint32_t glyph_index;

    for (glyph_index = 0U; text[glyph_index] != '\0'; glyph_index++) {
        const uint8_t *glyph = label_glyph(text[glyph_index]);
        uint32_t row;

        if (glyph == NULL) {
            continue;
        }
        for (row = 0U; row < LABEL_GLYPH_HEIGHT; row++) {
            uint32_t column;
            for (column = 0U; column < LABEL_GLYPH_WIDTH; column++) {
                if ((glyph[row] & (uint8_t)(1U << (LABEL_GLYPH_WIDTH - 1U - column))) != 0U) {
                    uint32_t y;
                    for (y = 0U; y < scale; y++) {
                        uint32_t x;
                        for (x = 0U; x < scale; x++) {
                            set_pixel(buffer, screen_width,
                                      origin_x + glyph_index * (LABEL_GLYPH_WIDTH + 1U) * scale + column * scale + x,
                                      origin_y + row * scale + y, 0U);
                        }
                    }
                }
            }
        }
    }
}

static void
draw_rect(uint8_t *buffer, uint32_t width, uint32_t x, uint32_t y,
          uint32_t rect_width, uint32_t rect_height)
{
    uint32_t offset;
    uint32_t thickness;

    for (offset = 0U; offset < rect_width; offset++) {
        for (thickness = 0U; thickness < CONTROL_LINE_WIDTH && thickness < rect_height; thickness++) {
            set_pixel(buffer, width, x + offset, y + thickness, 0U);
            set_pixel(buffer, width, x + offset, y + rect_height - 1U - thickness, 0U);
        }
    }
    for (offset = 1U; offset + 1U < rect_height; offset++) {
        for (thickness = 0U; thickness < CONTROL_LINE_WIDTH && thickness < rect_width; thickness++) {
            set_pixel(buffer, width, x + thickness, y + offset, 0U);
            set_pixel(buffer, width, x + rect_width - 1U - thickness, y + offset, 0U);
        }
    }
}

static void
draw_circle(uint8_t *buffer, uint32_t width, uint32_t center_x,
            uint32_t center_y, uint32_t radius)
{
    uint32_t y;
    uint32_t inner_radius = radius > CONTROL_LINE_WIDTH ? radius - CONTROL_LINE_WIDTH : 0U;
    uint32_t inner_radius_squared = inner_radius * inner_radius;
    uint32_t radius_squared = radius * radius;

    for (y = 0U; y <= radius * 2U; y++) {
        uint32_t x;
        for (x = 0U; x <= radius * 2U; x++) {
            int32_t dx = (int32_t)x - (int32_t)radius;
            int32_t dy = (int32_t)y - (int32_t)radius;
            uint32_t distance = (uint32_t)(dx * dx + dy * dy);
            if (distance <= radius_squared && distance >= inner_radius_squared) {
                set_pixel(buffer, width, center_x + x - radius,
                          center_y + y - radius, 0U);
            }
        }
    }
}

static uint32_t
label_width(const char *text, uint32_t scale)
{
    uint32_t length = 0U;

    while (text[length] != '\0') {
        length++;
    }
    return length * (LABEL_GLYPH_WIDTH + 1U) * scale;
}

static void
draw_centered_label(uint8_t *buffer, uint32_t width,
                    const struct inkboy_rect *rect,
                    const char *text, uint32_t scale)
{
    uint32_t text_width = label_width(text, scale);
    uint32_t text_height = LABEL_GLYPH_HEIGHT * scale;

    draw_label(buffer, width,
               rect->x + (rect->width - text_width) / 2U,
               rect->y + (rect->height - text_height) / 2U,
               text, scale);
}

static void
draw_centered_circle_label(uint8_t *buffer, uint32_t width,
                           const struct inkboy_circle *circle,
                           const char *text, uint32_t scale)
{
    uint32_t text_width = label_width(text, scale);
    uint32_t text_height = LABEL_GLYPH_HEIGHT * scale;

    draw_label(buffer, width,
               circle->center_x - text_width / 2U,
               circle->center_y - text_height / 2U,
               text, scale);
}

void
inkboy_draw_controls(uint8_t *buffer, const struct inkboy_layout *layout)
{
    uint32_t dpad_x = layout->dpad.x + (layout->dpad.width - layout->dpad_arm) / 2U;
    uint32_t dpad_y = layout->dpad.y + (layout->dpad.height - layout->dpad_arm) / 2U;

    draw_rect(buffer, layout->width, dpad_x, layout->dpad.y,
              layout->dpad_arm, layout->dpad.height);
    draw_rect(buffer, layout->width, layout->dpad.x, dpad_y,
              layout->dpad.width, layout->dpad_arm);
    draw_circle(buffer, layout->width, layout->a.center_x,
                layout->a.center_y, layout->a.radius);
    draw_circle(buffer, layout->width, layout->b.center_x,
                layout->b.center_y, layout->b.radius);
    draw_centered_circle_label(buffer, layout->width, &layout->a, "A", CONTROL_SCALE);
    draw_centered_circle_label(buffer, layout->width, &layout->b, "B", CONTROL_SCALE);
    draw_rect(buffer, layout->width, layout->l.x, layout->l.y,
              layout->l.width, layout->l.height);
    draw_rect(buffer, layout->width, layout->r.x, layout->r.y,
              layout->r.width, layout->r.height);
    draw_centered_label(buffer, layout->width, &layout->l, "L", CONTROL_SCALE);
    draw_centered_label(buffer, layout->width, &layout->r, "R", CONTROL_SCALE);
    draw_rect(buffer, layout->width, layout->select.x, layout->select.y,
              layout->select.width, layout->select.height);
    draw_rect(buffer, layout->width, layout->start.x, layout->start.y,
              layout->start.width, layout->start.height);
    draw_centered_label(buffer, layout->width, &layout->select, "SELECT", 3U);
    draw_centered_label(buffer, layout->width, &layout->start, "START", 3U);
    draw_centered_label(buffer, layout->width, &layout->exit, "EXIT", CONTROL_SCALE);
}

