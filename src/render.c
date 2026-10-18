#include "render.h"
#include "profile.h"
#include "draw.h"
#include "fbink.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    bool refresh_pending;
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

static int
ensure_gray_buffer(struct inkboy_renderer *renderer, size_t gray_size)
{
    if (renderer->gray_buffer_size < gray_size) {
        uint8_t *storage = realloc(renderer->gray_buffer, gray_size);

        if (storage == NULL) {
            return -1;
        }
        renderer->gray_buffer = storage;
        renderer->gray_buffer_size = gray_size;
    }
    return 0;
}

static int
build_screen(struct inkboy_renderer *renderer,
             const struct inkboy_video_frame *frame,
             uint8_t **buffer_out)
{
    const struct inkboy_layout *layout = &renderer->layout;
    struct inkboy_profile_timer composition =
        inkboy_profile_timer_start(INKBOY_PROFILE_COMPOSE);
    size_t buffer_size;
    uint8_t *buffer;
    size_t offset;

    if (buffer_out == NULL ||
        inkboy_buffer_size(layout->width, layout->height, &buffer_size) != 0) {
        inkboy_profile_timer_cancel(composition);
        return -1;
    }

    if (renderer->screen_buffer == NULL ||
        renderer->screen_buffer_size < buffer_size) {
        buffer = realloc(renderer->screen_buffer, buffer_size);
        if (buffer == NULL) {
            inkboy_profile_timer_cancel(composition);
            fprintf(stderr, "inkboy: unable to allocate %zu-byte screen buffer: %s\n",
                    buffer_size, strerror(errno));
            return -1;
        }
        renderer->screen_buffer = buffer;
        renderer->screen_buffer_size = buffer_size;
    }
    buffer = renderer->screen_buffer;

    for (offset = 0U; offset < buffer_size; offset += 2U) {
        buffer[offset] = UINT8_MAX;
        buffer[offset + 1U] = UINT8_MAX;
    }

    inkboy_profile_timer_stop(composition);
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
    inkboy_draw_controls(buffer, layout);
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
            " fbink=%s display_cap_fps=%u cleanup_s=%llu async_partial=1\n",
            renderer->state.screen_width, renderer->state.screen_height,
            renderer->state.view_width, renderer->state.view_height,
            renderer->state.view_hori_origin, renderer->state.view_vert_origin,
            fbink_version(), renderer->policy.fps,
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
    if (ensure_gray_buffer(renderer, gray_size) != 0) {
        inkboy_profile_timer_cancel(timer);
        return -1;
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

struct presentation_details {
    const char *name;
    enum inkboy_profile_stage submit_stage;
    enum inkboy_profile_stage wait_stage;
    enum inkboy_profile_presentation profile_kind;
};

struct presentation_plan {
    enum inkboy_present_result kind;
    struct presentation_details details;
    bool full_screen;
    uint8_t *buffer;
    size_t game_image_size;
    size_t draw_buffer_size;
    uint32_t draw_width;
    uint32_t draw_height;
    short draw_x;
    short draw_y;
};

static struct presentation_details
presentation_details_for(enum inkboy_present_result kind)
{
    switch (kind) {
    case INKBOY_PRESENT_INITIAL:
        return (struct presentation_details){"initial",
            INKBOY_PROFILE_INITIAL_SUBMIT, INKBOY_PROFILE_INITIAL_WAIT,
            INKBOY_PROFILE_PRESENT_INITIAL};
    case INKBOY_PRESENT_CLEANUP:
        return (struct presentation_details){"cleanup",
            INKBOY_PROFILE_CLEAN_SUBMIT, INKBOY_PROFILE_CLEAN_WAIT,
            INKBOY_PROFILE_PRESENT_CLEANUP};
    default:
        return (struct presentation_details){"normal",
            INKBOY_PROFILE_REFRESH_SUBMIT, INKBOY_PROFILE_REFRESH_WAIT,
            INKBOY_PROFILE_PRESENT_NORMAL};
    }
}

static int
wait_for_pending_refresh(struct inkboy_renderer *renderer)
{
    struct inkboy_profile_timer timer;
    int result;

    if (!renderer->refresh_pending) {
        return 0;
    }
    timer = inkboy_profile_timer_start(INKBOY_PROFILE_REFRESH_WAIT);
    result = wait_for_complete(renderer->fbfd);
    inkboy_profile_timer_stop(timer);
    if (result != 0) {
        fprintf(stderr, "inkboy: pending refresh completion failed: %d\n", result);
        return -1;
    }
    renderer->refresh_pending = false;
    return 0;
}

static int
draw_frame(struct inkboy_renderer *renderer, uint8_t *buffer,
           uint32_t width, uint32_t height, size_t size,
           short x, short y, uint64_t *draw_end_ns)
{
    struct inkboy_profile_timer timer;
    int result;

    renderer->config.no_refresh = true;
    timer = inkboy_profile_timer_start(INKBOY_PROFILE_DRAW);
    result = fbink_print_raw_data(renderer->fbfd, buffer,
                                  (int)width, (int)height, size,
                                  x, y, &renderer->config);
    inkboy_profile_timer_stop(timer);
    *draw_end_ns = inkboy_profile_now();
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to draw the game frame: %d\n", result);
        return -1;
    }
    return 0;
}

static int
submit_refresh(struct inkboy_renderer *renderer,
               enum inkboy_present_result kind,
               struct presentation_details details,
               bool full_screen, const FBInkRect *draw_rect,
               uint64_t *submit_end_ns)
{
    struct inkboy_profile_timer timer;
    int result;

    renderer->refresh_config.is_flashing = kind != INKBOY_PRESENT_PARTIAL;
    renderer->refresh_config.wfm_mode = kind == INKBOY_PRESENT_PARTIAL
        ? WFM_GC16 : WFM_AUTO;
    if (!full_screen && (draw_rect->width == 0U || draw_rect->height == 0U)) {
        fprintf(stderr, "inkboy: empty refresh rectangle\n");
        return -1;
    }
    fprintf(stdout, "display event=refresh kind=%d draw_rect=%u,%u,%u,%u"
            " refresh_rect=%u,%u,%u,%u waveform_requested=%s"
            " waveform_actual=unknown flashing=%u\n", kind,
            draw_rect->left, draw_rect->top, draw_rect->width, draw_rect->height,
            full_screen ? 0U : draw_rect->left, full_screen ? 0U : draw_rect->top,
            full_screen ? renderer->state.screen_width : draw_rect->width,
            full_screen ? renderer->state.screen_height : draw_rect->height,
            renderer->refresh_config.wfm_mode == WFM_GC16 ? "GC16" : "AUTO",
            renderer->refresh_config.is_flashing);
    inkboy_profile_refresh_attempted();
    timer = inkboy_profile_timer_start(details.submit_stage);
    result = full_screen
        ? fbink_refresh(renderer->fbfd, 0U, 0U, 0U, 0U, &renderer->refresh_config)
        : fbink_refresh_rect(renderer->fbfd, draw_rect, &renderer->refresh_config);
    inkboy_profile_timer_stop(timer);
    *submit_end_ns = inkboy_profile_now();
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to refresh the game frame: %d\n", result);
        inkboy_profile_refresh_failed(details.profile_kind);
        return -1;
    }
    return 0;
}

static int
complete_refresh(struct inkboy_renderer *renderer, bool defer_wait,
                 struct presentation_details details,
                 uint64_t *complete_ns)
{
    struct inkboy_profile_timer timer;
    int result = 0;

    renderer->refresh_pending = true;
    if (!defer_wait) {
        timer = inkboy_profile_timer_start(details.wait_stage);
        result = wait_for_complete(renderer->fbfd);
        inkboy_profile_timer_stop(timer);
        if (result == 0) {
            renderer->refresh_pending = false;
        }
    }
    *complete_ns = inkboy_profile_now();
    if (result != 0) {
        fprintf(stderr, "inkboy: refresh completion failed: %d\n", result);
        inkboy_profile_refresh_failed(details.profile_kind);
        return -1;
    }
    return 0;
}

static bool
frame_geometry_changed(const struct inkboy_renderer *renderer,
                       const struct inkboy_video_frame *frame,
                       const struct inkboy_rect *game_rect)
{
    return !renderer->frame_presented ||
        frame->width != renderer->last_width ||
        frame->height != renderer->last_height ||
        game_rect->x != renderer->last_game_rect.x ||
        game_rect->y != renderer->last_game_rect.y ||
        game_rect->width != renderer->last_game_rect.width ||
        game_rect->height != renderer->last_game_rect.height;
}

/* Choose full, regional, cleanup, or unchanged presentation for this frame. */
static void
select_presentation_plan(struct inkboy_renderer *renderer,
                         const struct inkboy_video_frame *frame,
                         const struct inkboy_rect *game_rect,
                         uint64_t trace_start_ns,
                         struct presentation_plan *plan)
{
    bool geometry_changed =
        frame_geometry_changed(renderer, frame, game_rect);

    plan->kind = geometry_changed
        ? INKBOY_PRESENT_INITIAL : INKBOY_PRESENT_PARTIAL;
    plan->full_screen = geometry_changed;
    plan->draw_width = geometry_changed
        ? renderer->state.view_width : game_rect->width;
    plan->draw_height = geometry_changed
        ? renderer->state.view_height : game_rect->height;
    plan->draw_x = geometry_changed ? 0 : (short)game_rect->x;
    plan->draw_y = geometry_changed ? 0 : (short)game_rect->y;
    plan->buffer = geometry_changed ? NULL : renderer->region_buffer;
    plan->game_image_size = (size_t)game_rect->width * game_rect->height * 2U;

    if (!geometry_changed) {
        uint64_t now = inkboy_profile_now();
        bool cleanup_due = now - renderer->last_cleanup >= renderer->policy.cleanup_ns;

        if (!cleanup_due && renderer->last_image_size == plan->game_image_size &&
            memcmp(renderer->region_buffer, renderer->last_image,
                   plan->game_image_size) == 0) {
            uint64_t trace_end_ns = inkboy_profile_now();

            plan->kind = INKBOY_PRESENT_UNCHANGED;
            inkboy_profile_image_unchanged();
            fprintf(stdout, "display event=trace kind=unchanged prepare_ms=%.3f"
                    " draw_ms=0.000 submit_ms=0.000 wait_ms=0.000 total_ms=%.3f\n",
                    (trace_end_ns - trace_start_ns) / 1000000.0,
                    (trace_end_ns - trace_start_ns) / 1000000.0);
            return;
        }
        plan->kind = cleanup_due ? INKBOY_PRESENT_CLEANUP : INKBOY_PRESENT_PARTIAL;
    }
    plan->details = presentation_details_for(plan->kind);
}

/* Finish queued work before drawing a full frame or cleanup region. */
static int
prepare_presentation(struct inkboy_renderer *renderer,
                     const struct inkboy_video_frame *frame,
                     struct presentation_plan *plan)
{
    if (renderer->last_image_size != plan->game_image_size) {
        uint8_t *storage = realloc(renderer->last_image, plan->game_image_size);

        if (storage == NULL) return -1;
        renderer->last_image = storage;
        renderer->last_image_size = plan->game_image_size;
    }
    if (plan->kind != INKBOY_PRESENT_PARTIAL &&
        wait_for_pending_refresh(renderer) != 0) {
        return -1;
    }
    if (plan->full_screen) {
        if (inkboy_buffer_size(plan->draw_width, plan->draw_height,
                               &plan->draw_buffer_size) != 0 ||
            build_screen(renderer, frame, &plan->buffer) != 0) {
            return -1;
        }
    } else {
        plan->draw_buffer_size = plan->game_image_size;
    }
    if (!renderer->dump_saved) {
        int result = fbink_dump(renderer->fbfd, &renderer->dump);

        if (result != 0) {
            fprintf(stderr, "inkboy: unable to save the previous screen: %d\n", result);
            return -1;
        }
        renderer->dump_saved = true;
    }
    return 0;
}

enum inkboy_present_result
inkboy_renderer_present_frame(struct inkboy_renderer *renderer,
                              const struct inkboy_video_frame *frame)
{
    struct presentation_plan plan = {0};
    FBInkRect draw_rect;
    struct inkboy_rect game_rect;
    uint64_t trace_start_ns;
    uint64_t prepare_end_ns, draw_end_ns, submit_end_ns, complete_ns;

    if (renderer == NULL || frame == NULL ||
        inkboy_frame_rect(&renderer->layout, frame, &game_rect) != 0) {
        return INKBOY_PRESENT_ERROR;
    }
    trace_start_ns = inkboy_profile_now();
    if (prepare_region(renderer, frame, &game_rect) != 0) {
        return INKBOY_PRESENT_ERROR;
    }
    select_presentation_plan(renderer, frame, &game_rect, trace_start_ns, &plan);
    if (plan.kind == INKBOY_PRESENT_UNCHANGED) return plan.kind;
    if (prepare_presentation(renderer, frame, &plan) != 0) {
        return INKBOY_PRESENT_ERROR;
    }
    prepare_end_ns = inkboy_profile_now();
    if (draw_frame(renderer, plan.buffer, plan.draw_width, plan.draw_height,
                   plan.draw_buffer_size, plan.draw_x, plan.draw_y,
                   &draw_end_ns) != 0) {
        return INKBOY_PRESENT_ERROR;
    }
    draw_rect = fbink_get_last_rect(false);
    if (submit_refresh(renderer, plan.kind, plan.details, plan.full_screen, &draw_rect,
                       &submit_end_ns) != 0) {
        return INKBOY_PRESENT_ERROR;
    }
    bool defer_wait = plan.kind == INKBOY_PRESENT_PARTIAL;
    if (complete_refresh(renderer, defer_wait, plan.details, &complete_ns) != 0) {
        return INKBOY_PRESENT_ERROR;
    }
    memcpy(renderer->last_image, renderer->region_buffer, renderer->last_image_size);
    renderer->frame_presented = true;
    renderer->last_game_rect = game_rect;
    renderer->last_width = frame->width;
    renderer->last_height = frame->height;
    if (plan.kind != INKBOY_PRESENT_PARTIAL) renderer->last_cleanup = inkboy_profile_now();
    inkboy_profile_frame_presented(plan.details.profile_kind);
    fprintf(stdout, "display event=trace kind=%s prepare_ms=%.3f draw_ms=%.3f"
            " submit_ms=%.3f wait_ms=%.3f completion=%s total_ms=%.3f\n",
            plan.details.name,
            (prepare_end_ns - trace_start_ns) / 1000000.0,
            (draw_end_ns - prepare_end_ns) / 1000000.0,
            (submit_end_ns - draw_end_ns) / 1000000.0,
            (complete_ns - submit_end_ns) / 1000000.0,
            defer_wait ? "deferred" : "complete",
            (complete_ns - trace_start_ns) / 1000000.0);
    return plan.kind;
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
        if (renderer->refresh_pending) {
            struct inkboy_profile_timer pending_timer =
                inkboy_profile_timer_start(INKBOY_PROFILE_REFRESH_WAIT);
            result = wait_for_complete(renderer->fbfd);
            fprintf(stdout, "cleanup stage=pending_refresh_wait result=%d duration_ms=%.3f\n",
                    result, inkboy_profile_timer_elapsed_ns(pending_timer) / 1000000.0);
            inkboy_profile_timer_stop(pending_timer);
            if (result == 0) renderer->refresh_pending = false;
            else fprintf(stderr, "inkboy: pending refresh wait failed: %d\n", result);
        }
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
