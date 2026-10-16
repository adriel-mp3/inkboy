#include "render.h"
#include "profile.h"
#include "fbink.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    assert(fd == 7 && config->wfm_mode == WFM_AUTO && config->is_flashing);
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
    static const uint32_t pixels[160U * 144U] = {0};
    struct inkboy_video_frame frame = {pixels, 160U, 144U, 640U};
    struct inkboy_renderer *renderer = open_renderer();

    assert(inkboy_renderer_present_frame(renderer, &frame) == 0);
    assert(inkboy_renderer_present_frame(renderer, &frame) == 0);
    assert(inkboy_profile.presentations == 2U && inkboy_profile.refresh_failures == 0U);
#ifdef INKBOY_PROFILE_GAME_REGION
    assert(full_refreshes == 1U && region_refreshes == 1U);
#else
    assert(full_refreshes == 2U && region_refreshes == 0U);
#endif
    close_renderer(renderer, 0, "RWFC");

    renderer = open_renderer();
    wait_interrupted = true;
    assert(inkboy_renderer_present_frame(renderer, &frame) == 0);
    assert(inkboy_profile.refresh_failures == 0U);
    wait_interrupted = true;
    close_renderer(renderer, 0, "RWWFC");

    renderer = open_renderer();
    draw_error = -EIO;
    assert(inkboy_renderer_present_frame(renderer, &frame) != 0);
    assert(inkboy_profile.refresh_attempts == 0U);
    close_renderer(renderer, 0, "RWFC");

    renderer = open_renderer();
    submit_error = -EIO;
    assert(inkboy_renderer_present_frame(renderer, &frame) != 0);
    assert(inkboy_profile.refresh_failures == 1U && inkboy_profile.presentations == 0U);
    close_renderer(renderer, 0, "RWFC");

    renderer = open_renderer();
    wait_error = -EIO;
    assert(inkboy_renderer_present_frame(renderer, &frame) != 0);
    assert(inkboy_profile.refresh_failures == 1U && inkboy_profile.presentations == 0U);
    wait_error = 0;
    close_renderer(renderer, 0, "RWFC");

    renderer = open_renderer();
    assert(inkboy_renderer_present_frame(renderer, &frame) == 0);
    restore_error = -EIO;
    close_renderer(renderer, -1, "RFC");

    renderer = open_renderer();
    assert(inkboy_renderer_present_frame(renderer, &frame) == 0);
    wait_error = -EIO;
    close_renderer(renderer, -1, "RWFC");

    renderer = open_renderer();
    assert(inkboy_renderer_present_frame(renderer, &frame) == 0);
    close_error = -EIO;
    close_renderer(renderer, -1, "RWFC");

#ifdef INKBOY_PROFILE_GAME_REGION
    renderer = open_renderer();
    assert(inkboy_renderer_present_frame(renderer, &frame) == 0);
    struct inkboy_video_frame portrait = {pixels, 144U, 160U, 576U};
    assert(inkboy_renderer_present_frame(renderer, &portrait) == 0);
    assert(full_refreshes == 2U && region_refreshes == 0U);
    close_renderer(renderer, 0, "RWFC");

    renderer = open_renderer();
    assert(inkboy_renderer_present_frame(renderer, &frame) == 0);
    empty_rect = true;
    assert(inkboy_renderer_present_frame(renderer, &frame) != 0);
    close_renderer(renderer, 0, "RWFC");
#endif
    renderer = open_renderer();
    assert(inkboy_renderer_close(renderer) == 0);
    assert(restores == 0U && frees == 0U && closes == 1U);
    assert(inkboy_renderer_close(NULL) == 0);
    free(full_image);
    puts("renderer: presentation/refresh/restore failures, geometry and cleanup pass");
    return 0;
}
