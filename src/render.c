#include "render.h"
#include "profile.h"

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
    uint8_t *gray_buffer;
    size_t gray_buffer_size;
    uint8_t *region_buffer;
    size_t region_buffer_size;
    uint8_t *last_image;
    size_t last_image_size;
    struct inkboy_rect last_game_rect;
    unsigned last_width, last_height;
    bool frame_presented;
    uint64_t last_cleanup;
    struct inkboy_presentation_policy policy;
};

/* A signal may interrupt the ioctl while the update is still in flight.
 * Finish waiting for the same marker before restoring or reusing buffers. */
static int
wait_for_complete(int fbfd)
{
    int result;

    do {
        errno = 0;
        result = fbink_wait_for_complete(fbfd, LAST_MARKER);
    } while (result != 0 && (result == -EINTR || errno == EINTR));
    return result;
}

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

static int
draw_game_frame(struct inkboy_renderer *renderer, uint8_t *buffer,
                const struct inkboy_video_frame *frame)
{
    struct inkboy_rect rect;
    size_t size;
    struct inkboy_profile_timer timer;
    int result;

    if (inkboy_frame_rect(&renderer->layout, frame, &rect) != 0 ||
        inkboy_buffer_size(frame->width, frame->height, &size) != 0) {
        return -1;
    }
    timer = inkboy_profile_timer_start(INKBOY_PROFILE_CONVERT);
    if (renderer->gray_buffer_size < size) {
        uint8_t *storage = realloc(renderer->gray_buffer, size);

        if (storage == NULL) {
            inkboy_profile_timer_cancel(timer);
            return -1;
        }
        renderer->gray_buffer = storage;
        renderer->gray_buffer_size = size;
    }
    result = inkboy_frame_convert(frame, renderer->gray_buffer, size);
    inkboy_profile_timer_stop(timer);
    if (result != 0) {
        return result;
    }
    timer = inkboy_profile_timer_start(INKBOY_PROFILE_SCALE);
    result = inkboy_frame_scale(renderer->gray_buffer, frame->width, frame->height,
                                buffer, renderer->layout.width,
                                renderer->layout.height, &rect);
    inkboy_profile_timer_stop(timer);
    return result;
}

static int
build_screen(struct inkboy_renderer *renderer,
             const struct inkboy_video_frame *frame,
             uint8_t **buffer_out)
{
    const struct inkboy_layout *layout = &renderer->layout;
    uint8_t **storage = &renderer->screen_buffer;
    size_t *storage_size = &renderer->screen_buffer_size;
    struct inkboy_profile_timer composition =
        inkboy_profile_timer_start(INKBOY_PROFILE_COMPOSE);
    size_t buffer_size;
    uint8_t *buffer;
    size_t offset;

    if (layout == NULL || buffer_out == NULL ||
        inkboy_buffer_size(layout->width, layout->height, &buffer_size) != 0) {
        inkboy_profile_timer_cancel(composition);
        return -1;
    }

    if (*storage == NULL || *storage_size < buffer_size) {
        buffer = realloc(*storage, buffer_size);
        if (buffer == NULL) {
            inkboy_profile_timer_cancel(composition);
            fprintf(stderr, "inkboy: unable to allocate %zu-byte screen buffer: %s\n",
                    buffer_size, strerror(errno));
            return -1;
        }
        *storage = buffer;
        *storage_size = buffer_size;
    }
    buffer = *storage;
    if (buffer == NULL) {
        inkboy_profile_timer_cancel(composition);
        fprintf(stderr, "inkboy: unable to allocate %zu-byte screen buffer: %s\n",
                buffer_size, strerror(errno));
        return -1;
    }

    for (offset = 0U; offset < buffer_size; offset += 2U) {
        buffer[offset] = UINT8_MAX;
        buffer[offset + 1U] = UINT8_MAX;
    }

    draw_title(buffer, layout->width, layout->title_x, layout->title_y);
    inkboy_profile_timer_stop(composition);
    if (frame == NULL) {
        draw_duck(buffer, layout->width, layout->duck_x, layout->duck_y,
                  layout->duck_size);
    } else if (renderer->policy.mode != INKBOY_REFRESH_FULL_FLASH) {
        struct inkboy_rect rect;
        if (inkboy_frame_rect(layout, frame, &rect) != 0) {
            inkboy_profile_timer_cancel(composition);
            return -1;
        }
        for (uint32_t y = 0U; y < rect.height; y++) {
            memcpy(buffer + ((size_t)(rect.y + y) * layout->width + rect.x) * 2U,
                   renderer->region_buffer + (size_t)y * rect.width * 2U,
                   (size_t)rect.width * 2U);
        }
    } else {
        if (draw_game_frame(renderer, buffer, frame) != 0) {
            inkboy_profile_timer_cancel(composition);
            return -1;
        }
    }
    draw_controls(buffer, layout);
    draw_centered_label(buffer, layout->width, &layout->exit, "EXIT", CONTROL_SCALE);
    *buffer_out = buffer;
    return 0;
}

static void
free_dump(struct inkboy_renderer *renderer)
{
    if (renderer->dump.data != NULL) {
        int result = fbink_free_dump_data(&renderer->dump);

        fprintf(stdout, "cleanup stage=dump_free result=%d\n", result);
        if (result != 0) {
            fprintf(stderr, "inkboy: warning: unable to free framebuffer dump\n");
        }
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
    renderer->policy = inkboy_presentation_policy();
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
    fprintf(stdout, "display event=open screen=%ux%u viewport=%ux%u origin=%u,%u"
            " fbink=%s policy=%d display_cap_fps=%u cleanup_s=%llu synchronous=1\n",
            renderer->state.screen_width, renderer->state.screen_height,
            renderer->state.view_width, renderer->state.view_height,
            renderer->state.view_hori_origin, renderer->state.view_vert_origin,
            fbink_version(), renderer->policy.mode, renderer->policy.fps,
            (unsigned long long)(renderer->policy.cleanup_ns / UINT64_C(1000000000)));
    renderer->refresh_config.wfm_mode = WFM_AUTO;
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
        build_screen(renderer, NULL, &buffer) != 0) {
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
    return wait_for_complete(renderer->fbfd) == 0 ? 0 : -1;
}

/* Produce only the game image; the complete composition remains cached. */
static int prepare_region(struct inkboy_renderer *renderer,
                          const struct inkboy_video_frame *frame,
                          const struct inkboy_rect *rect)
{
    size_t gray_size, region_size;
    uint8_t *storage;
    struct inkboy_profile_timer timer =
        inkboy_profile_timer_start(INKBOY_PROFILE_CONVERT);
    struct inkboy_rect local = {0U, 0U, rect->width, rect->height};

    if (inkboy_buffer_size(frame->width, frame->height, &gray_size) != 0 ||
        inkboy_buffer_size(rect->width, rect->height, &region_size) != 0) {
        inkboy_profile_timer_cancel(timer);
        return -1;
    }
    if (renderer->gray_buffer_size < gray_size) {
        storage = realloc(renderer->gray_buffer, gray_size);
        if (storage == NULL) {
            inkboy_profile_timer_cancel(timer);
            return -1;
        }
        renderer->gray_buffer = storage;
        renderer->gray_buffer_size = gray_size;
    }
    if (inkboy_frame_convert(frame, renderer->gray_buffer, gray_size) != 0) {
        inkboy_profile_timer_cancel(timer);
        return -1;
    }
    inkboy_profile_timer_stop(timer);
    timer = inkboy_profile_timer_start(INKBOY_PROFILE_SCALE);
    if (renderer->region_buffer_size < region_size) {
        storage = realloc(renderer->region_buffer, region_size);
        if (storage == NULL) {
            inkboy_profile_timer_cancel(timer);
            return -1;
        }
        renderer->region_buffer = storage;
        renderer->region_buffer_size = region_size;
    }
    if (inkboy_frame_scale(renderer->gray_buffer, frame->width, frame->height,
                           renderer->region_buffer, rect->width, rect->height,
                           &local) != 0) {
        inkboy_profile_timer_cancel(timer);
        return -1;
    }
    inkboy_profile_timer_stop(timer);
    return 0;
}

enum inkboy_present_result
inkboy_renderer_present_frame(struct inkboy_renderer *renderer,
                              const struct inkboy_video_frame *frame)
{
    uint8_t *buffer = NULL;
    size_t buffer_size;
    int result;
    struct inkboy_profile_timer timer;
    bool full_screen;
    bool experimental;
    bool geometry_changed;
    FBInkRect draw_rect;
    struct inkboy_rect game_rect;
    uint32_t draw_width, draw_height;
    short draw_x = 0, draw_y = 0;
    enum inkboy_present_result kind;
    enum inkboy_profile_stage submit_stage, wait_stage;

    if (renderer == NULL || frame == NULL ||
        inkboy_frame_rect(&renderer->layout, frame, &game_rect) != 0) {
        return INKBOY_PRESENT_ERROR;
    }
    experimental = renderer->policy.mode != INKBOY_REFRESH_FULL_FLASH;
    geometry_changed = !renderer->frame_presented ||
        frame->width != renderer->last_width || frame->height != renderer->last_height ||
        game_rect.x != renderer->last_game_rect.x ||
        game_rect.y != renderer->last_game_rect.y ||
        game_rect.width != renderer->last_game_rect.width ||
        game_rect.height != renderer->last_game_rect.height;
    kind = geometry_changed ? INKBOY_PRESENT_INITIAL : INKBOY_PRESENT_PARTIAL;
    full_screen = true;
    draw_width = renderer->state.view_width;
    draw_height = renderer->state.view_height;
    if (experimental) {
        if (prepare_region(renderer, frame, &game_rect) != 0) return INKBOY_PRESENT_ERROR;
        buffer_size = (size_t)game_rect.width * game_rect.height * 2U;
        if (!geometry_changed) {
            uint64_t now = inkboy_profile_now();
            bool cleanup_due = now - renderer->last_cleanup >= renderer->policy.cleanup_ns;
            if (!cleanup_due && renderer->last_image_size == buffer_size &&
                memcmp(renderer->region_buffer, renderer->last_image, buffer_size) == 0) {
                inkboy_profile_image_unchanged();
                return INKBOY_PRESENT_UNCHANGED;
            }
            kind = cleanup_due ? INKBOY_PRESENT_CLEANUP : INKBOY_PRESENT_PARTIAL;
            full_screen = false;
            buffer = renderer->region_buffer;
            draw_width = game_rect.width;
            draw_height = game_rect.height;
            draw_x = (short)game_rect.x;
            draw_y = (short)game_rect.y;
        }
        /* Allocate the comparison cache before submitting any hardware work. */
        if (renderer->last_image_size != buffer_size) {
            uint8_t *storage = realloc(renderer->last_image, buffer_size);
            if (storage == NULL) return INKBOY_PRESENT_ERROR;
            renderer->last_image = storage;
            renderer->last_image_size = buffer_size;
        }
    }
    if (full_screen) {
        if (inkboy_buffer_size(draw_width, draw_height, &buffer_size) != 0 ||
            build_screen(renderer, frame, &buffer) != 0) return INKBOY_PRESENT_ERROR;
    }
#ifdef INKBOY_PROFILE_GAME_REGION
    /* Keep TASK-009's flashing region diagnostic available independently. */
    if (!experimental && !geometry_changed) {
        timer = inkboy_profile_timer_start(INKBOY_PROFILE_REGION_PACK);
        buffer_size = (size_t)game_rect.width * game_rect.height * 2U;
        if (renderer->region_buffer_size < buffer_size) {
            uint8_t *storage = realloc(renderer->region_buffer, buffer_size);
            if (storage == NULL) return INKBOY_PRESENT_ERROR;
            renderer->region_buffer = storage;
            renderer->region_buffer_size = buffer_size;
        }
        for (uint32_t y = 0U; y < game_rect.height; y++) {
            memcpy(renderer->region_buffer + (size_t)y * game_rect.width * 2U,
                   buffer + ((size_t)(game_rect.y + y) * renderer->layout.width + game_rect.x) * 2U,
                   (size_t)game_rect.width * 2U);
        }
        buffer = renderer->region_buffer;
        draw_width = game_rect.width;
        draw_height = game_rect.height;
        draw_x = (short)game_rect.x;
        draw_y = (short)game_rect.y;
        full_screen = false;
        inkboy_profile_timer_stop(timer);
    }
#endif
    if (!renderer->dump_saved) {
        result = fbink_dump(renderer->fbfd, &renderer->dump);
        if (result != 0) {
            fprintf(stderr, "inkboy: unable to save the previous screen: %d\n", result);
            return INKBOY_PRESENT_ERROR;
        }
        renderer->dump_saved = true;
    }
    renderer->config.no_refresh = true;
    timer = inkboy_profile_timer_start(INKBOY_PROFILE_DRAW);
    result = fbink_print_raw_data(renderer->fbfd, buffer,
                                  (int)draw_width, (int)draw_height,
                                  buffer_size, draw_x, draw_y, &renderer->config);
    inkboy_profile_timer_stop(timer);
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to draw the game frame: %d\n", result);
        return INKBOY_PRESENT_ERROR;
    }
    renderer->refresh_config.is_flashing = !experimental || kind != INKBOY_PRESENT_PARTIAL;
    renderer->refresh_config.wfm_mode = experimental && kind == INKBOY_PRESENT_PARTIAL &&
        renderer->policy.mode == INKBOY_REFRESH_REGION_GC16 ? WFM_GC16 : WFM_AUTO;
    draw_rect = fbink_get_last_rect(false);
    if (!full_screen && (draw_rect.width == 0U || draw_rect.height == 0U)) {
        fprintf(stderr, "inkboy: empty refresh rectangle\n");
        return INKBOY_PRESENT_ERROR;
    }
    fprintf(stdout, "display event=refresh kind=%d draw_rect=%u,%u,%u,%u"
            " refresh_rect=%u,%u,%u,%u waveform_requested=%s"
            " waveform_actual=unknown flashing=%u\n", kind,
            draw_rect.left, draw_rect.top, draw_rect.width, draw_rect.height,
            full_screen ? 0U : draw_rect.left, full_screen ? 0U : draw_rect.top,
            full_screen ? renderer->state.screen_width : draw_rect.width,
            full_screen ? renderer->state.screen_height : draw_rect.height,
            renderer->refresh_config.wfm_mode == WFM_GC16 ? "GC16" : "AUTO",
            renderer->refresh_config.is_flashing);
    submit_stage = kind == INKBOY_PRESENT_INITIAL ? INKBOY_PROFILE_INITIAL_SUBMIT :
        kind == INKBOY_PRESENT_CLEANUP ? INKBOY_PROFILE_CLEAN_SUBMIT : INKBOY_PROFILE_REFRESH_SUBMIT;
    wait_stage = kind == INKBOY_PRESENT_INITIAL ? INKBOY_PROFILE_INITIAL_WAIT :
        kind == INKBOY_PRESENT_CLEANUP ? INKBOY_PROFILE_CLEAN_WAIT : INKBOY_PROFILE_REFRESH_WAIT;
    inkboy_profile_refresh_attempted();
    timer = inkboy_profile_timer_start(submit_stage);
    result = full_screen
        ? fbink_refresh(renderer->fbfd, 0U, 0U, 0U, 0U, &renderer->refresh_config)
        : fbink_refresh_rect(renderer->fbfd, &draw_rect, &renderer->refresh_config);
    inkboy_profile_timer_stop(timer);
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to refresh the game frame: %d\n", result);
        inkboy_profile_refresh_failed(kind == INKBOY_PRESENT_INITIAL ? INKBOY_PROFILE_PRESENT_INITIAL :
            kind == INKBOY_PRESENT_CLEANUP ? INKBOY_PROFILE_PRESENT_CLEANUP : INKBOY_PROFILE_PRESENT_NORMAL);
        return INKBOY_PRESENT_ERROR;
    }
    timer = inkboy_profile_timer_start(wait_stage);
    result = wait_for_complete(renderer->fbfd);
    inkboy_profile_timer_stop(timer);
    if (result != 0) {
        fprintf(stderr, "inkboy: refresh completion failed: %d\n", result);
        inkboy_profile_refresh_failed(kind == INKBOY_PRESENT_INITIAL ? INKBOY_PROFILE_PRESENT_INITIAL :
            kind == INKBOY_PRESENT_CLEANUP ? INKBOY_PROFILE_PRESENT_CLEANUP : INKBOY_PROFILE_PRESENT_NORMAL);
        return INKBOY_PRESENT_ERROR;
    }
    if (experimental) {
        memcpy(renderer->last_image, renderer->region_buffer, renderer->last_image_size);
    }
    renderer->frame_presented = true;
    renderer->last_game_rect = game_rect;
    renderer->last_width = frame->width;
    renderer->last_height = frame->height;
    if (kind != INKBOY_PRESENT_PARTIAL) renderer->last_cleanup = inkboy_profile_now();
    inkboy_profile_frame_presented(kind == INKBOY_PRESENT_INITIAL ? INKBOY_PROFILE_PRESENT_INITIAL :
        kind == INKBOY_PRESENT_CLEANUP ? INKBOY_PROFILE_PRESENT_CLEANUP : INKBOY_PROFILE_PRESENT_NORMAL);
    return kind;
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
        struct inkboy_profile_timer restore_timer =
            inkboy_profile_timer_start(INKBOY_PROFILE_RESTORE);

        renderer->refresh_config.is_flashing = true;
        renderer->refresh_config.wfm_mode = WFM_AUTO;
        result = fbink_restore(renderer->fbfd, &renderer->refresh_config,
                                &renderer->dump);
        fprintf(stdout, "cleanup stage=restore result=%d duration_ms=%.3f\n",
                result, inkboy_profile_timer_elapsed_ns(restore_timer) / 1000000.0);
        inkboy_profile_timer_stop(restore_timer);
        if (result != 0) {
            fprintf(stderr, "inkboy: warning: screen restoration failed: %d\n", result);
        } else {
            restore_timer = inkboy_profile_timer_start(INKBOY_PROFILE_RESTORE_WAIT);
            result = wait_for_complete(renderer->fbfd);
            fprintf(stdout, "cleanup stage=restore_wait result=%d duration_ms=%.3f\n",
                    result, inkboy_profile_timer_elapsed_ns(restore_timer) / 1000000.0);
            inkboy_profile_timer_stop(restore_timer);
            if (result != 0) {
                fprintf(stderr, "inkboy: screen restoration wait failed: %d\n", result);
            }
        }
    } else {
        fprintf(stdout, "cleanup stage=restore result=not-needed\n");
    }
    free_dump(renderer);
    free(renderer->screen_buffer);
    free(renderer->gray_buffer);
    free(renderer->region_buffer);
    free(renderer->last_image);
    if (renderer->fbink_opened) {
        int close_result = fbink_close(renderer->fbfd);

        fprintf(stdout, "cleanup stage=framebuffer_close result=%d\n", close_result);
        if (close_result != 0) {
            fprintf(stderr, "inkboy: warning: unable to close framebuffer\n");
            if (result == 0) {
                result = -1;
            }
        }
    }
    free(renderer);
    return result == 0 ? 0 : -1;
}
