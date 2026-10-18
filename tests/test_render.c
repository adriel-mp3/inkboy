#include "render.h"
#include "profile.h"
#include "fbink.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fake_now = UINT64_C(1000000000);
static uint64_t render_test_now(void) { return fake_now; }
#define inkboy_profile_now render_test_now
#include "../src/render.c"
#undef inkboy_profile_now

static int draw_error;
static int submit_error;
static int wait_error;
static bool wait_interrupted;
static int restore_error;
static int close_error;
static bool empty_rect;
static unsigned draws;
static unsigned full_refreshes;
static unsigned region_refreshes;
static unsigned restores;
static unsigned frees;
static unsigned closes;
static FBInkRect last_rect;
static uint8_t *full_image;
static char order[32];
static unsigned order_count;

const char *fbink_version(void) { return "test"; }
int fbink_open(void) { return 7; }
int fbink_init(int fd, const FBInkConfig *config)
{
    assert(fd == 7 && config->is_quiet);
    return 0;
}
void fbink_get_state(const FBInkConfig *config, FBInkState *state)
{
    (void)config;
    *state = (FBInkState){.view_width = 1236U, .view_height = 1648U,
        .screen_width = 1236U, .screen_height = 1664U, .view_vert_origin = 8U};
}
int fbink_dump(int fd, FBInkDump *dump)
{
    assert(fd == 7);
    dump->data = malloc(1U);
    assert(dump->data != NULL);
    return 0;
}
int fbink_print_raw_data(int fd, const unsigned char *data, const int width,
                         const int height, const size_t length,
                         short x, short y, const FBInkConfig *config)
{
    assert(fd == 7 && config->no_refresh);
    assert(length == (size_t)width * (size_t)height * 2U);
    draws++;
    last_rect = (FBInkRect){.left = (unsigned short)x,
        .top = (unsigned short)(y + 8), .width = (unsigned short)width,
        .height = (unsigned short)height};
    if (draws == 1U) {
        assert(x == 0 && y == 0 && width == 1236 && height == 1648);
        full_image = malloc(length);
        assert(full_image != NULL);
        memcpy(full_image, data, length);
    } else if (width != 1236 || height != 1648) {
        for (int row = 0; row < height; row++) {
            assert(memcmp(data + (size_t)row * width * 2U,
                full_image + ((size_t)(row + y) * 1236U + x) * 2U,
                (size_t)width * 2U) == 0);
        }
    }
    return draw_error;
}
FBInkRect fbink_get_last_rect(bool rotated)
{
    assert(!rotated);
    return empty_rect ? (FBInkRect){0} : last_rect;
}
int fbink_refresh(int fd, uint32_t top, uint32_t left, uint32_t width,
                   uint32_t height, const FBInkConfig *config)
{
    assert(fd == 7 && top == 0U && left == 0U && width == 0U && height == 0U);
    assert(config->wfm_mode == WFM_AUTO && config->is_flashing);
    full_refreshes++;
    return submit_error;
}
int fbink_refresh_rect(int fd, const FBInkRect *rect, const FBInkConfig *config)
{
    assert(fd == 7);
    assert((config->wfm_mode == WFM_GC16 && !config->is_flashing) ||
           (config->wfm_mode == WFM_AUTO && config->is_flashing));
    assert(memcmp(rect, &last_rect, sizeof(*rect)) == 0);
    assert(rect->left > 0U && rect->top > 8U);
    region_refreshes++;
    return submit_error;
}
int fbink_wait_for_complete(int fd, uint32_t marker)
{
    assert(fd == 7 && marker == LAST_MARKER);
    order[order_count++] = 'W';
    if (wait_interrupted) {
        wait_interrupted = false;
        errno = EINTR;
        return -1;
    }
    return wait_error;
}
int fbink_restore(int fd, const FBInkConfig *config, const FBInkDump *dump)
{
    assert(fd == 7 && config->is_flashing && dump->data != NULL);
    restores++;
    order[order_count++] = 'R';
    return restore_error;
}
int fbink_free_dump_data(FBInkDump *dump)
{
    free(dump->data);
    dump->data = NULL;
    frees++;
    order[order_count++] = 'F';
    return 0;
}
int fbink_close(int fd)
{
    assert(fd == 7);
    closes++;
    order[order_count++] = 'C';
    return close_error;
}

static struct inkboy_renderer *open_renderer(void)
{
    struct inkboy_renderer *renderer = NULL;

    fake_now = UINT64_C(1000000000);
    draw_error = submit_error = wait_error = restore_error = close_error = 0;
    draws = full_refreshes = region_refreshes = restores = frees = closes = 0U;
    order_count = 0U;
    empty_rect = false;
    wait_interrupted = false;
    memset(order, 0, sizeof(order));
    free(full_image);
    full_image = NULL;
    inkboy_profile_begin("test");
    assert(inkboy_renderer_open(&renderer) == 0);
    return renderer;
}

static void close_renderer(struct inkboy_renderer *renderer, int expected,
                            const char *expected_order)
{
    order_count = 0U;
    memset(order, 0, sizeof(order));
    assert(inkboy_renderer_close(renderer) == expected);
    assert(strcmp(order, expected_order) == 0);
    assert(restores == 1U && frees == 1U && closes == 1U);
}

int main(void)
{
    static uint32_t pixels[160U * 144U];
    struct inkboy_video_frame frame = {pixels, 160U, 144U, 640U};
    struct inkboy_renderer *renderer = open_renderer();
    const struct inkboy_presentation_policy policy = inkboy_presentation_policy();
    assert(policy.fps == 2U && policy.cleanup_ns == UINT64_C(60000000000));
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_INITIAL);
    assert(full_refreshes == 1U && renderer->refresh_config.is_flashing);
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_UNCHANGED);
    assert(draws == 1U && inkboy_profile_counter_value(INKBOY_PROFILE_PRESENTATIONS) == 1U);
    /* A changed color maps to the same grayscale: compare presented YA bytes. */
    pixels[0] = 0x000001U;
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_UNCHANGED);
    pixels[0] = 0x808080U;
    struct inkboy_rect rect;
    uint8_t gray[160U * 144U * 2U];
    assert(inkboy_frame_rect(&renderer->layout, &frame, &rect) == 0);
    assert(inkboy_frame_convert(&frame, gray, sizeof(gray)) == 0);
    assert(inkboy_frame_scale(gray, frame.width, frame.height, full_image,
                             1236U, 1648U, &rect) == 0);
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_PARTIAL);
    assert(!renderer->refresh_config.is_flashing && region_refreshes == 1U);
    assert(inkboy_profile_stage_value(INKBOY_PROFILE_COMPOSE).count == 1U);
    assert(inkboy_profile_counter_value(INKBOY_PROFILE_NORMAL_PRESENTATIONS) == 1U);
    fake_now += policy.cleanup_ns - 1U;
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_UNCHANGED);
    fake_now++;
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_CLEANUP);
    assert(renderer->refresh_config.is_flashing && full_refreshes == 1U);
    assert(inkboy_profile_counter_value(INKBOY_PROFILE_CLEANUPS) == 1U && inkboy_profile_counter_value(INKBOY_PROFILE_PRESENTATIONS) == 3U);
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_UNCHANGED);
    struct inkboy_video_frame portrait = {pixels, 144U, 160U, 576U};
    assert(inkboy_renderer_present_frame(renderer, &portrait) == INKBOY_PRESENT_INITIAL);
    assert(full_refreshes == 2U);
    /* The old landscape border is cleared on the full portrait composition. */
    assert(renderer->screen_buffer[((size_t)rect.y * 1236U + rect.x) * 2U] == 255U);
    close_renderer(renderer, 0, "RWFC");

    /* Faults during normal updates and pending cleanups must restore fully. */
    for (unsigned cleanup = 0U; cleanup < 2U; cleanup++) {
        for (unsigned failure = 0U; failure < 5U; failure++) {
            renderer = open_renderer();
            assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_INITIAL);
            if (cleanup) {
                fake_now += policy.cleanup_ns;
            } else {
                pixels[1] ^= 0x606060U;
                assert(inkboy_frame_convert(&frame, gray, sizeof(gray)) == 0);
                assert(inkboy_frame_scale(gray, frame.width, frame.height, full_image,
                                         1236U, 1648U, &rect) == 0);
            }
            if (failure == 0U) draw_error = -EIO;
            if (failure == 1U) submit_error = -EIO;
            if (failure == 2U) wait_error = -EIO;
            if (failure == 3U) empty_rect = true;
            if (failure == 4U) wait_interrupted = true;
            enum inkboy_present_result result = inkboy_renderer_present_frame(renderer, &frame);
            bool deferred_normal = !cleanup && (failure == 2U || failure == 4U);
            assert(result == (deferred_normal ? INKBOY_PRESENT_PARTIAL :
                failure == 4U ? INKBOY_PRESENT_CLEANUP : INKBOY_PRESENT_ERROR));
            if (failure == 1U || (failure == 2U && cleanup)) {
                assert(inkboy_profile_counter_value(INKBOY_PROFILE_REFRESH_FAILURES) == 1U);
                assert(inkboy_profile_counter_value(INKBOY_PROFILE_CLEANUP_REFRESH_FAILURES) == cleanup);
                assert(inkboy_profile_counter_value(INKBOY_PROFILE_NORMAL_REFRESH_FAILURES) == !cleanup);
            }
            assert(inkboy_profile_counter_value(INKBOY_PROFILE_PRESENTATIONS) ==
                (failure == 4U || deferred_normal ? 2U : 1U));
            wait_error = 0;
            close_renderer(renderer, 0, deferred_normal && failure == 4U ?
                "WWRWFC" : failure == 2U ? "WRWFC" : "RWFC");
        }
    }

    renderer = open_renderer();
    wait_interrupted = true;
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_INITIAL);
    assert(inkboy_profile_counter_value(INKBOY_PROFILE_REFRESH_FAILURES) == 0U);
    wait_interrupted = true;
    close_renderer(renderer, 0, "RWWFC");

    renderer = open_renderer();
    draw_error = -EIO;
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_ERROR);
    assert(inkboy_profile_counter_value(INKBOY_PROFILE_REFRESH_ATTEMPTS) == 0U);
    close_renderer(renderer, 0, "RWFC");

    renderer = open_renderer();
    submit_error = -EIO;
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_ERROR);
    assert(inkboy_profile_counter_value(INKBOY_PROFILE_REFRESH_FAILURES) == 1U &&
           inkboy_profile_counter_value(INKBOY_PROFILE_PRESENTATIONS) == 0U);
    close_renderer(renderer, 0, "RWFC");

    renderer = open_renderer();
    wait_error = -EIO;
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_ERROR);
    assert(inkboy_profile_counter_value(INKBOY_PROFILE_REFRESH_FAILURES) == 1U &&
           inkboy_profile_counter_value(INKBOY_PROFILE_PRESENTATIONS) == 0U);
    wait_error = 0;
    close_renderer(renderer, 0, "WRWFC");

    renderer = open_renderer();
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_INITIAL);
    restore_error = -EIO;
    close_renderer(renderer, -1, "RFC");

    renderer = open_renderer();
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_INITIAL);
    wait_error = -EIO;
    close_renderer(renderer, -1, "RWFC");

    renderer = open_renderer();
    assert(inkboy_renderer_present_frame(renderer, &frame) == INKBOY_PRESENT_INITIAL);
    close_error = -EIO;
    close_renderer(renderer, -1, "RWFC");

    renderer = open_renderer();
    assert(inkboy_renderer_close(renderer) == 0);
    assert(restores == 0U && frees == 0U && closes == 1U);
    assert(inkboy_renderer_close(NULL) == 0);
    free(full_image);
    puts("presentation: waveform, grayscale cache, skip, cleanup, geometry and restoration pass");
    return 0;
}
