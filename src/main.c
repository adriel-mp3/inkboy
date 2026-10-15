#include "duck_bitmap.h"

#include "fbink.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TITLE_SCALE 8U
#define TITLE_GLYPH_WIDTH 5U
#define TITLE_GLYPH_HEIGHT 7U
#define TITLE_SPACING 2U
#define TITLE_LENGTH 6U
#define DUCK_MAX_SIZE 420U
#define DUCK_MIN_SIZE 192U
#define DUCK_GAP 32U

static volatile sig_atomic_t stop_requested;

static const uint8_t title_glyphs[TITLE_LENGTH][TITLE_GLYPH_HEIGHT] = {
    /* I */ {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f},
    /* n */ {0x00, 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11},
    /* k */ {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    /* B */ {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
    /* o */ {0x00, 0x0e, 0x11, 0x11, 0x11, 0x11, 0x0e},
    /* y */ {0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x18},
};

static void
request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int
install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    if (sigemptyset(&action.sa_mask) != 0) {
        return -1;
    }

    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGHUP, &action, NULL) != 0) {
        return -1;
    }

    return 0;
}

static int
checked_size(uint32_t width, uint32_t height, size_t *size)
{
    uint64_t pixels;

    if (width == 0U || height == 0U) {
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
                set_pixel(buffer,
                          screen_width,
                          origin_x + output_x,
                          origin_y + output_y,
                          0U);
            }
        }
    }
}

static void
draw_title(uint8_t *buffer, uint32_t screen_width, uint32_t origin_x, uint32_t origin_y)
{
    uint32_t glyph;

    for (glyph = 0U; glyph < TITLE_LENGTH; glyph++) {
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
                            set_pixel(buffer,
                                      screen_width,
                                      origin_x + glyph * (TITLE_GLYPH_WIDTH + TITLE_SPACING) * TITLE_SCALE + column * TITLE_SCALE + scale_x,
                                      origin_y + row * TITLE_SCALE + scale_y,
                                      0U);
                        }
                    }
                }
            }
        }
    }
}

static int
build_screen(uint8_t **buffer_out, uint32_t width, uint32_t height)
{
    size_t buffer_size;
    uint8_t *buffer;
    uint32_t duck_size = width * 2U / 5U;
    uint32_t title_width =
        (TITLE_LENGTH * TITLE_GLYPH_WIDTH + (TITLE_LENGTH - 1U) * TITLE_SPACING) * TITLE_SCALE;
    uint32_t title_height = TITLE_GLYPH_HEIGHT * TITLE_SCALE;
    uint32_t total_height;
    uint32_t origin_x;
    uint32_t origin_y;

    if (duck_size > DUCK_MAX_SIZE) {
        duck_size = DUCK_MAX_SIZE;
    }
    if (duck_size < DUCK_MIN_SIZE) {
        duck_size = DUCK_MIN_SIZE;
    }

    total_height = duck_size + DUCK_GAP + title_height;
    if (total_height > height || title_width > width) {
        fprintf(stderr, "inkboy: screen is too small for the composition\n");
        return -1;
    }

    if (checked_size(width, height, &buffer_size) != 0) {
        fprintf(stderr, "inkboy: framebuffer dimensions overflow memory size\n");
        return -1;
    }

    buffer = malloc(buffer_size);
    if (buffer == NULL) {
        fprintf(stderr, "inkboy: unable to allocate %zu-byte screen buffer: %s\n",
                buffer_size,
                strerror(errno));
        return -1;
    }

    for (size_t offset = 0U; offset < buffer_size; offset += 2U) {
        buffer[offset] = UINT8_MAX;
        buffer[offset + 1U] = UINT8_MAX;
    }

    origin_x = (width - duck_size) / 2U;
    origin_y = (height - total_height) / 2U;
    draw_duck(buffer, width, origin_x, origin_y, duck_size);
    draw_title(buffer,
               width,
               (width - title_width) / 2U,
               origin_y + duck_size + DUCK_GAP);

    *buffer_out = buffer;
    return 0;
}

static void
free_dump(FBInkDump *dump, int fbfd)
{
    if (dump->data != NULL && fbink_free_dump_data(dump) != 0) {
        fprintf(stderr, "inkboy: warning: unable to free framebuffer dump\n");
    }
    if (fbink_close(fbfd) != 0) {
        fprintf(stderr, "inkboy: warning: unable to close framebuffer\n");
    }
}

int
main(void)
{
    FBInkConfig config = {0};
    FBInkConfig refresh_config = {0};
    FBInkDump dump = {0};
    FBInkState state = {0};
    uint8_t *buffer = NULL;
    size_t buffer_size;
    int fbfd = FBFD_AUTO;
    int exit_code = EXIT_FAILURE;
    int result;

    if (install_signal_handlers() != 0) {
        fprintf(stderr, "inkboy: unable to install signal handlers: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    fbfd = fbink_open();
    if (fbfd < 0) {
        fprintf(stderr, "inkboy: fbink_open failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    config.is_quiet = true;
    result = fbink_init(fbfd, &config);
    if (result != 0) {
        fprintf(stderr, "inkboy: fbink_init failed: %d\n", result);
        free_dump(&dump, fbfd);
        return EXIT_FAILURE;
    }

    fbink_get_state(&config, &state);
    if (state.view_width == 0U || state.view_height == 0U) {
        fprintf(stderr, "inkboy: FBInk returned an invalid screen geometry\n");
        free_dump(&dump, fbfd);
        return EXIT_FAILURE;
    }

    if (checked_size(state.view_width, state.view_height, &buffer_size) != 0 ||
        build_screen(&buffer, state.view_width, state.view_height) != 0) {
        free_dump(&dump, fbfd);
        return EXIT_FAILURE;
    }

    result = fbink_dump(fbfd, &dump);
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to save the previous screen: %d\n", result);
        free(buffer);
        free_dump(&dump, fbfd);
        return EXIT_FAILURE;
    }

    config.no_refresh = true;
    result = fbink_print_raw_data(fbfd,
                                  buffer,
                                  (int)state.view_width,
                                  (int)state.view_height,
                                  buffer_size,
                                  0,
                                  0,
                                  &config);
    free(buffer);
    buffer = NULL;
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to draw the composition: %d\n", result);
        (void)fbink_restore(fbfd, &refresh_config, &dump);
        free_dump(&dump, fbfd);
        return EXIT_FAILURE;
    }

    refresh_config.is_flashing = true;
    result = fbink_refresh(fbfd, 0U, 0U, 0U, 0U, &refresh_config);
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to refresh the composition: %d\n", result);
        (void)fbink_restore(fbfd, &refresh_config, &dump);
        free_dump(&dump, fbfd);
        return EXIT_FAILURE;
    }
    (void)fbink_wait_for_complete(fbfd, LAST_MARKER);

    fprintf(stdout,
            "InkBoy rendered with FBInk %s on %ux%u; waiting for termination\n",
            fbink_version(),
            state.view_width,
            state.view_height);
    fflush(stdout);

    while (!stop_requested) {
        pause();
    }

    result = fbink_restore(fbfd, &refresh_config, &dump);
    if (result != 0) {
        fprintf(stderr, "inkboy: warning: screen restoration failed: %d\n", result);
    } else {
        (void)fbink_wait_for_complete(fbfd, LAST_MARKER);
        exit_code = EXIT_SUCCESS;
    }

    free_dump(&dump, fbfd);
    return exit_code;
}
