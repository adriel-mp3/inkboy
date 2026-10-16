#include "render.h"

#include "duck_bitmap.h"
#include "fbink.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITLE_SCALE 4U
#define TITLE_GLYPH_WIDTH 5U
#define TITLE_GLYPH_HEIGHT 7U
#define TITLE_SPACING 2U
#define CONTROL_SCALE 4U
#define CONTROL_LINE_WIDTH 4U

struct inkboy_renderer {
    int fbfd;
    bool fbink_opened;
    bool dump_saved;
    FBInkConfig config;
    FBInkConfig refresh_config;
    FBInkDump dump;
    FBInkState state;
    struct inkboy_layout layout;
    uint8_t *screen_buffer;
    size_t screen_buffer_size;
};

static const uint8_t title_glyphs[6U][TITLE_GLYPH_HEIGHT] = {
    /* I */ {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f},
    /* n */ {0x00, 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11},
    /* k */ {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    /* B */ {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
    /* o */ {0x00, 0x0e, 0x11, 0x11, 0x11, 0x11, 0x0e},
    /* y */ {0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x18},
};

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
draw_duck(uint8_t *buffer,
          uint32_t screen_width,
          uint32_t origin_x,
          uint32_t origin_y,
          uint32_t size)
{
    uint32_t output_y;

    for (output_y = 0U; output_y < size; output_y++) {
        uint32_t source_y = (output_y * DUCK_BITMAP_HEIGHT) / size;
        uint32_t output_x;

        for (output_x = 0U; output_x < size; output_x++) {
            uint32_t source_x = (output_x * DUCK_BITMAP_WIDTH) / size;
            size_t source_offset =
                (size_t)source_y * DUCK_BITMAP_STRIDE + (source_x / 8U);
            uint8_t source_pixel = duck_bitmap[source_offset];

            if ((source_pixel & (uint8_t)(1U << (7U - (source_x % 8U)))) != 0U) {
                set_pixel(buffer, screen_width,
                          origin_x + output_x, origin_y + output_y, 0U);
            }
        }
    }
}

static void
draw_title(uint8_t *buffer, uint32_t screen_width, uint32_t origin_x, uint32_t origin_y)
{
    uint32_t glyph;

    for (glyph = 0U; glyph < 6U; glyph++) {
        uint32_t row;

        for (row = 0U; row < TITLE_GLYPH_HEIGHT; row++) {
            uint32_t column;

            for (column = 0U; column < TITLE_GLYPH_WIDTH; column++) {
                uint8_t mask = (uint8_t)(1U << (TITLE_GLYPH_WIDTH - 1U - column));

                if ((title_glyphs[glyph][row] & mask) != 0U) {
                    uint32_t scale_y;

                    for (scale_y = 0U; scale_y < TITLE_SCALE; scale_y++) {
                        uint32_t scale_x;

                        for (scale_x = 0U; scale_x < TITLE_SCALE; scale_x++) {
                            set_pixel(buffer, screen_width,
                                      origin_x + glyph * (TITLE_GLYPH_WIDTH + TITLE_SPACING) * TITLE_SCALE + column * TITLE_SCALE + scale_x,
                                      origin_y + row * TITLE_SCALE + scale_y, 0U);
                        }
                    }
                }
            }
        }
    }
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
        for (row = 0U; row < TITLE_GLYPH_HEIGHT; row++) {
            uint32_t column;
            for (column = 0U; column < TITLE_GLYPH_WIDTH; column++) {
                if ((glyph[row] & (uint8_t)(1U << (TITLE_GLYPH_WIDTH - 1U - column))) != 0U) {
                    uint32_t y;
                    for (y = 0U; y < scale; y++) {
                        uint32_t x;
                        for (x = 0U; x < scale; x++) {
                            set_pixel(buffer, screen_width,
                                      origin_x + glyph_index * (TITLE_GLYPH_WIDTH + 1U) * scale + column * scale + x,
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
    return length * (TITLE_GLYPH_WIDTH + 1U) * scale;
}

static void
draw_centered_label(uint8_t *buffer, uint32_t width,
                    const struct inkboy_rect *rect,
                    const char *text, uint32_t scale)
{
    uint32_t text_width = label_width(text, scale);
    uint32_t text_height = TITLE_GLYPH_HEIGHT * scale;

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
    uint32_t text_height = TITLE_GLYPH_HEIGHT * scale;

    draw_label(buffer, width,
               circle->center_x - text_width / 2U,
               circle->center_y - text_height / 2U,
               text, scale);
}

static void
draw_controls(uint8_t *buffer, const struct inkboy_layout *layout)
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
}

static void
draw_game_frame(uint8_t *buffer, const struct inkboy_layout *layout,
                const struct inkboy_video_frame *frame)
{
    uint32_t output_width = layout->duck_size;
    uint32_t output_height = layout->duck_size;
    uint32_t scaled_width = output_width;
    uint32_t scaled_height = output_height;
    uint32_t origin_x;
    uint32_t origin_y;

    if (frame == NULL || frame->pixels == NULL || frame->width == 0U ||
        frame->height == 0U || frame->pitch < (size_t)frame->width * 4U) {
        return;
    }
    if ((uint64_t)scaled_width * frame->height <=
        (uint64_t)scaled_height * frame->width) {
        scaled_height = (uint32_t)(((uint64_t)scaled_width * frame->height) /
                                   frame->width);
    } else {
        scaled_width = (uint32_t)(((uint64_t)scaled_height * frame->width) /
                                  frame->height);
    }
    origin_x = layout->duck_x + (output_width - scaled_width) / 2U;
    origin_y = layout->duck_y + (output_height - scaled_height) / 2U;
    for (uint32_t y = 0U; y < scaled_height; y++) {
        unsigned source_y = (unsigned)(((uint64_t)y * frame->height) /
                                       scaled_height);
        const unsigned char *source_row =
            (const unsigned char *)frame->pixels + (size_t)source_y * frame->pitch;
        for (uint32_t x = 0U; x < scaled_width; x++) {
            unsigned source_x = (unsigned)(((uint64_t)x * frame->width) /
                                           scaled_width);
            uint32_t pixel;
            uint8_t red;
            uint8_t green;
            uint8_t blue;
            uint8_t gray;

            memcpy(&pixel, source_row + (size_t)source_x * 4U, sizeof(pixel));
            red = (uint8_t)(pixel >> 16U);
            green = (uint8_t)(pixel >> 8U);
            blue = (uint8_t)pixel;
            gray = (uint8_t)((77U * red + 150U * green + 29U * blue + 128U) >> 8U);
            set_pixel(buffer, layout->width, origin_x + x, origin_y + y, gray);
        }
    }
}

static int
build_screen(const struct inkboy_layout *layout,
             const struct inkboy_video_frame *frame,
             uint8_t **buffer_out,
             uint8_t **storage,
             size_t *storage_size)
{
    size_t buffer_size;
    uint8_t *buffer;
    size_t offset;

    if (layout == NULL || buffer_out == NULL ||
        inkboy_buffer_size(layout->width, layout->height, &buffer_size) != 0) {
        return -1;
    }

    if (*storage == NULL || *storage_size < buffer_size) {
        buffer = realloc(*storage, buffer_size);
        if (buffer == NULL) {
            fprintf(stderr, "inkboy: unable to allocate %zu-byte screen buffer: %s\n",
                    buffer_size, strerror(errno));
            return -1;
        }
        *storage = buffer;
        *storage_size = buffer_size;
    }
    buffer = *storage;
    if (buffer == NULL) {
        fprintf(stderr, "inkboy: unable to allocate %zu-byte screen buffer: %s\n",
                buffer_size, strerror(errno));
        return -1;
    }

    for (offset = 0U; offset < buffer_size; offset += 2U) {
        buffer[offset] = UINT8_MAX;
        buffer[offset + 1U] = UINT8_MAX;
    }

    draw_title(buffer, layout->width, layout->title_x, layout->title_y);
    if (frame == NULL) {
        draw_duck(buffer, layout->width, layout->duck_x, layout->duck_y,
                  layout->duck_size);
    } else {
        draw_game_frame(buffer, layout, frame);
    }
    draw_controls(buffer, layout);
    draw_centered_label(buffer, layout->width, &layout->exit, "EXIT", CONTROL_SCALE);
    *buffer_out = buffer;
    return 0;
}

static void
free_dump(struct inkboy_renderer *renderer)
{
    if (renderer->dump.data != NULL && fbink_free_dump_data(&renderer->dump) != 0) {
        fprintf(stderr, "inkboy: warning: unable to free framebuffer dump\n");
    }
}

int
inkboy_renderer_open(struct inkboy_renderer **renderer_out)
{
    struct inkboy_renderer *renderer;
    int result;

    if (renderer_out == NULL) {
        return -1;
    }
    *renderer_out = NULL;
    renderer = calloc(1U, sizeof(*renderer));
    if (renderer == NULL) {
        fprintf(stderr, "inkboy: unable to allocate renderer: %s\n", strerror(errno));
        return -1;
    }
    renderer->fbfd = FBFD_AUTO;
    renderer->fbfd = fbink_open();
    if (renderer->fbfd < 0) {
        fprintf(stderr, "inkboy: fbink_open failed: %s\n", strerror(errno));
        free(renderer);
        return -1;
    }
    renderer->fbink_opened = true;
    renderer->config.is_quiet = true;
    result = fbink_init(renderer->fbfd, &renderer->config);
    if (result != 0) {
        fprintf(stderr, "inkboy: fbink_init failed: %d\n", result);
        (void)fbink_close(renderer->fbfd);
        free(renderer);
        return -1;
    }

    fbink_get_state(&renderer->config, &renderer->state);
    if (renderer->state.view_width == 0U || renderer->state.view_height == 0U ||
        inkboy_layout_build(&renderer->layout,
                            renderer->state.view_width,
                            renderer->state.view_height) != 0) {
        fprintf(stderr, "inkboy: FBInk returned an invalid screen geometry\n");
        (void)fbink_close(renderer->fbfd);
        free(renderer);
        return -1;
    }

    *renderer_out = renderer;
    return 0;
}

int
inkboy_renderer_present(struct inkboy_renderer *renderer)
{
    uint8_t *buffer = NULL;
    size_t buffer_size;
    int result;

    if (renderer == NULL || renderer->dump_saved ||
        inkboy_buffer_size(renderer->state.view_width,
                           renderer->state.view_height,
                           &buffer_size) != 0 ||
        build_screen(&renderer->layout, NULL, &buffer,
                     &renderer->screen_buffer, &renderer->screen_buffer_size) != 0) {
        return -1;
    }

    result = fbink_dump(renderer->fbfd, &renderer->dump);
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to save the previous screen: %d\n", result);
        return -1;
    }
    renderer->dump_saved = true;

    renderer->config.no_refresh = true;
    result = fbink_print_raw_data(renderer->fbfd,
                                  buffer,
                                  (int)renderer->state.view_width,
                                  (int)renderer->state.view_height,
                                  buffer_size,
                                  0,
                                  0,
                                  &renderer->config);
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to draw the composition: %d\n", result);
        return -1;
    }

    renderer->refresh_config.is_flashing = true;
    result = fbink_refresh(renderer->fbfd, 0U, 0U, 0U, 0U,
                           &renderer->refresh_config);
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to refresh the composition: %d\n", result);
        return -1;
    }
    (void)fbink_wait_for_complete(renderer->fbfd, LAST_MARKER);
    return 0;
}

int
inkboy_renderer_present_frame(struct inkboy_renderer *renderer,
                              const struct inkboy_video_frame *frame)
{
    uint8_t *buffer = NULL;
    size_t buffer_size;
    int result;

    if (renderer == NULL || frame == NULL ||
        inkboy_buffer_size(renderer->state.view_width,
                           renderer->state.view_height,
                           &buffer_size) != 0 ||
        build_screen(&renderer->layout, frame, &buffer,
                     &renderer->screen_buffer, &renderer->screen_buffer_size) != 0) {
        return -1;
    }
    if (!renderer->dump_saved) {
        result = fbink_dump(renderer->fbfd, &renderer->dump);
        if (result != 0) {
            fprintf(stderr, "inkboy: unable to save the previous screen: %d\n", result);
            return -1;
        }
        renderer->dump_saved = true;
    }
    renderer->config.no_refresh = true;
    result = fbink_print_raw_data(renderer->fbfd, buffer,
                                  (int)renderer->state.view_width,
                                  (int)renderer->state.view_height,
                                  buffer_size, 0, 0, &renderer->config);
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to draw the game frame: %d\n", result);
        return -1;
    }
    renderer->refresh_config.is_flashing = true;
    result = fbink_refresh(renderer->fbfd, 0U, 0U, 0U, 0U,
                           &renderer->refresh_config);
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to refresh the game frame: %d\n", result);
        return -1;
    }
    (void)fbink_wait_for_complete(renderer->fbfd, LAST_MARKER);
    return 0;
}

const struct inkboy_layout *
inkboy_renderer_layout(const struct inkboy_renderer *renderer)
{
    return renderer == NULL ? NULL : &renderer->layout;
}

const char *
inkboy_renderer_version(const struct inkboy_renderer *renderer)
{
    (void)renderer;
    return fbink_version();
}

int
inkboy_renderer_close(struct inkboy_renderer *renderer)
{
    int result = 0;

    if (renderer == NULL) {
        return 0;
    }
    if (renderer->dump_saved) {
        result = fbink_restore(renderer->fbfd, &renderer->refresh_config,
                                &renderer->dump);
        if (result != 0) {
            fprintf(stderr, "inkboy: warning: screen restoration failed: %d\n", result);
        } else {
            (void)fbink_wait_for_complete(renderer->fbfd, LAST_MARKER);
        }
    }
    free_dump(renderer);
    free(renderer->screen_buffer);
    if (renderer->fbink_opened && fbink_close(renderer->fbfd) != 0) {
        fprintf(stderr, "inkboy: warning: unable to close framebuffer\n");
        if (result == 0) {
            result = -1;
        }
    }
    free(renderer);
    return result == 0 ? 0 : -1;
}
