#define _POSIX_C_SOURCE 200809L

#include "fbink.h"
#include "libretro.h"

#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

typedef void (*fn_void)(void);
typedef void (*fn_environment)(retro_environment_t);
typedef void (*fn_video_refresh)(retro_video_refresh_t);
typedef void (*fn_audio_sample)(retro_audio_sample_t);
typedef void (*fn_audio_batch)(retro_audio_sample_batch_t);
typedef void (*fn_input_poll)(retro_input_poll_t);
typedef void (*fn_input_state)(retro_input_state_t);
typedef bool (*fn_load_game)(const struct retro_game_info *);
typedef void (*fn_unload_game)(void);
typedef void (*fn_system_av_info)(struct retro_system_av_info *);

struct core_api {
    fn_environment set_environment;
    fn_video_refresh set_video_refresh;
    fn_audio_sample set_audio_sample;
    fn_audio_batch set_audio_batch;
    fn_input_poll set_input_poll;
    fn_input_state set_input_state;
    fn_void init;
    fn_void deinit;
    fn_void run;
    fn_load_game load_game;
    fn_unload_game unload_game;
    fn_system_av_info get_system_av_info;
};

struct frame_buffer {
    uint32_t *pixels;
    unsigned width;
    unsigned height;
    size_t row_bytes;
    size_t pitch;
    bool ready;
};

struct metrics {
    unsigned long runs;
    unsigned long callbacks;
    unsigned long valid_frames;
    unsigned long scheduled_display_attempts;
    unsigned long display_updates;
    unsigned long skipped_display_frames;
    unsigned long refresh_failures;
    unsigned long refresh_attempts;
    unsigned long draw_attempts;
    unsigned long draw_failures;
    unsigned long callback_copy_count;
    unsigned long audio_batch_callbacks;
    unsigned long audio_frames;
    unsigned long convert_count;
    unsigned long nearest_scale_count;
    unsigned long sleep_count;
    unsigned long scheduling_count;
    double emu_run_seconds;
    double callback_copy_seconds;
    double convert_seconds;
    double nearest_scale_seconds;
    double draw_seconds;
    double refresh_submit_seconds;
    double refresh_wait_seconds;
    double sleep_seconds;
    double scheduling_seconds;
};

struct display_context {
    int fd;
    bool initialized;
    bool dumped;
    bool first_update;
    bool restoration_failed;
    FBInkDump dump;
    FBInkConfig draw_config;
    FBInkConfig refresh_config;
    FBInkState state;
    unsigned char *ya;
    size_t ya_size;
    unsigned ya_width;
    unsigned ya_height;
    unsigned char *scaled_ya;
    size_t scaled_ya_size;
    unsigned scaled_width;
    unsigned scaled_height;
    uint32_t *nearest_x;
    size_t nearest_x_size;
    uint32_t *nearest_y;
    size_t nearest_y_size;
    unsigned nearest_x_width;
    unsigned nearest_y_height;
    unsigned nearest_x_source_width;
    unsigned nearest_y_source_height;
};

enum scale_mode {
    SCALE_SMOOTH,
    SCALE_NATIVE,
    SCALE_NEAREST
};

#define DEFAULT_WARMUP_FRAMES 180UL
#define DEFAULT_DURATION_SECONDS 30.0
#define DEFAULT_DISPLAY_FPS 1.0
#define MAX_DURATION_SECONDS 3600.0
#define MAX_DISPLAY_FPS 30.0
#define MAX_FRAME_WIDTH 4096U
#define MAX_FRAME_HEIGHT 4096U

static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t signal_received;
static unsigned long warmup_frames = DEFAULT_WARMUP_FRAMES;
static unsigned long total_runs;
static struct frame_buffer producer;
static struct frame_buffer display;
static struct metrics measured;
static bool pixel_format_seen;
static bool pixel_format_ok;
static bool capture_error;
static enum scale_mode selected_scale = SCALE_SMOOTH;
static unsigned target_sample_rate;
static unsigned long target_sample_rate_queries;
static unsigned long target_sample_rate_responses;

static struct timespec monotonic_now(void)
{
    struct timespec result;

    clock_gettime(CLOCK_MONOTONIC, &result);
    return result;
}

static double seconds_between(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

static struct timespec add_seconds(struct timespec value, double seconds)
{
    time_t whole = (time_t)seconds;
    long nanoseconds = (long)((seconds - (double)whole) * 1000000000.0);

    value.tv_sec += whole;
    value.tv_nsec += nanoseconds;
    while (value.tv_nsec >= 1000000000L) {
        value.tv_sec++;
        value.tv_nsec -= 1000000000L;
    }
    return value;
}

static bool before(struct timespec left, struct timespec right)
{
    return left.tv_sec < right.tv_sec ||
           (left.tv_sec == right.tv_sec && left.tv_nsec < right.tv_nsec);
}

static void request_stop(int signal_number)
{
    (void)signal_number;
    signal_received = 1;
    stop_requested = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction action = {0};

    action.sa_handler = request_stop;
    if (sigemptyset(&action.sa_mask) != 0 ||
        sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGHUP, &action, NULL) != 0) {
        return -1;
    }
    return 0;
}

static void audio_sample(int16_t left, int16_t right)
{
    (void)left;
    (void)right;
}

static size_t audio_batch(const int16_t *data, size_t frames)
{
    (void)data;
    measured.audio_batch_callbacks++;
    measured.audio_frames += frames;
    return frames;
}

static void input_poll(void)
{
}

static int16_t input_state(unsigned port, unsigned device, unsigned index,
                           unsigned id)
{
    (void)port;
    (void)device;
    (void)index;
    (void)id;
    return 0;
}

static void core_log(enum retro_log_level level, const char *format, ...)
{
    (void)level;
    (void)format;
}

static bool environment(unsigned command, void *data)
{
    if (command == RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE) {
        target_sample_rate_queries++;
        if (data != NULL && target_sample_rate != 0U) {
            *(unsigned *)data = target_sample_rate;
            target_sample_rate_responses++;
            return true;
        }
        return false;
    }
    if (command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) {
        enum retro_pixel_format requested = *(enum retro_pixel_format *)data;

        pixel_format_seen = true;
        pixel_format_ok = requested == RETRO_PIXEL_FORMAT_XRGB8888;
        fprintf(stderr, "environment: XRGB8888 %s\n",
                pixel_format_ok ? "accepted" : "rejected");
        return pixel_format_ok;
    }
    if (command == RETRO_ENVIRONMENT_GET_CAN_DUPE) {
        *(bool *)data = true;
        return true;
    }
    if (command == RETRO_ENVIRONMENT_GET_LOG_INTERFACE) {
        struct retro_log_callback *callback = data;

        callback->log = core_log;
        return true;
    }
    return false;
}

static bool size_mul(size_t left, size_t right, size_t *result)
{
    if (right != 0U && left > SIZE_MAX / right) {
        return false;
    }
    *result = left * right;
    return true;
}

static bool ensure_frame_storage(struct frame_buffer *frame,
                                 unsigned width,
                                 unsigned height,
                                 size_t row_bytes,
                                 size_t frame_bytes)
{
    uint32_t *pixels;

    if (frame->pixels != NULL && frame->width == width &&
        frame->height == height && frame->row_bytes == row_bytes) {
        return true;
    }
    pixels = malloc(frame_bytes);
    if (pixels == NULL) {
        return false;
    }
    free(frame->pixels);
    frame->pixels = pixels;
    frame->width = width;
    frame->height = height;
    frame->row_bytes = row_bytes;
    frame->pitch = row_bytes;
    frame->ready = false;
    return true;
}

static void video_refresh(const void *data, unsigned width, unsigned height,
                          size_t pitch)
{
    size_t row_bytes;
    size_t frame_bytes;
    struct timespec copy_start;
    struct timespec copy_finish;

    measured.callbacks++;
    if (data == NULL || total_runs < warmup_frames) {
        return;
    }
    if (width == 0U || height == 0U || width > MAX_FRAME_WIDTH ||
        height > MAX_FRAME_HEIGHT || !size_mul((size_t)width, 4U, &row_bytes) ||
        pitch < row_bytes || !size_mul(row_bytes, (size_t)height, &frame_bytes)) {
        fprintf(stderr, "capture: invalid XRGB8888 frame %ux%u pitch=%zu\n",
                width, height, pitch);
        capture_error = true;
        return;
    }
    copy_start = monotonic_now();
    if (!ensure_frame_storage(&producer, width, height, row_bytes, frame_bytes)) {
        fprintf(stderr, "capture: unable to allocate %zu bytes\n", frame_bytes);
        capture_error = true;
        return;
    }
    for (unsigned row = 0U; row < height; row++) {
        memcpy((unsigned char *)producer.pixels + (size_t)row * row_bytes,
               (const unsigned char *)data + (size_t)row * pitch, row_bytes);
    }
    copy_finish = monotonic_now();
    measured.callback_copy_seconds += seconds_between(copy_start, copy_finish);
    measured.callback_copy_count++;
    if (producer.ready) {
        measured.skipped_display_frames++;
    }
    producer.pitch = pitch;
    producer.ready = true;
    measured.valid_frames++;
}

static void *load_symbol(void *handle, const char *name)
{
    void *result = dlsym(handle, name);

    if (result == NULL) {
        fprintf(stderr, "core: missing symbol %s\n", name);
    }
    return result;
}

static bool load_core_api(void *handle, struct core_api *api)
{
#define LOAD(field, type, symbol_name) \
    api->field = (type)load_symbol(handle, symbol_name); \
    if (api->field == NULL) return false
    LOAD(set_environment, fn_environment, "retro_set_environment");
    LOAD(set_video_refresh, fn_video_refresh, "retro_set_video_refresh");
    LOAD(set_audio_sample, fn_audio_sample, "retro_set_audio_sample");
    LOAD(set_audio_batch, fn_audio_batch, "retro_set_audio_sample_batch");
    LOAD(set_input_poll, fn_input_poll, "retro_set_input_poll");
    LOAD(set_input_state, fn_input_state, "retro_set_input_state");
    LOAD(init, fn_void, "retro_init");
    LOAD(deinit, fn_void, "retro_deinit");
    LOAD(run, fn_void, "retro_run");
    LOAD(load_game, fn_load_game, "retro_load_game");
    LOAD(unload_game, fn_unload_game, "retro_unload_game");
    LOAD(get_system_av_info, fn_system_av_info, "retro_get_system_av_info");
#undef LOAD
    return true;
}

static int parse_unsigned(const char *text, unsigned long *result)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0U) {
        return -1;
    }
    *result = value;
    return 0;
}

static int parse_positive_double(const char *text, double *result)
{
    char *end = NULL;
    double value;

    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || value <= 0.0 ||
        value > MAX_DURATION_SECONDS) {
        return -1;
    }
    *result = value;
    return 0;
}

static uint64_t usage_cpu_microseconds(void)
{
    struct rusage usage;

    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0U;
    }
    return (uint64_t)usage.ru_utime.tv_sec * 1000000U +
           (uint64_t)usage.ru_utime.tv_usec +
           (uint64_t)usage.ru_stime.tv_sec * 1000000U +
           (uint64_t)usage.ru_stime.tv_usec;
}

static uint64_t rss_kib(void)
{
    struct rusage usage;

    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0U;
    }
    return (uint64_t)usage.ru_maxrss;
}

static int convert_to_ya(struct display_context *screen,
                         const struct frame_buffer *frame)
{
    size_t pixels;
    size_t output_size;
    unsigned char *ya;
    struct timespec convert_start;
    struct timespec convert_finish;

    if (!size_mul((size_t)frame->width, (size_t)frame->height, &pixels) ||
        !size_mul(pixels, 2U, &output_size)) {
        return -1;
    }
    convert_start = monotonic_now();
    if (screen->ya == NULL || screen->ya_size != output_size ||
        screen->ya_width != frame->width || screen->ya_height != frame->height) {
        ya = malloc(output_size);
        if (ya == NULL) {
            return -1;
        }
        free(screen->ya);
        screen->ya = ya;
        screen->ya_size = output_size;
        screen->ya_width = frame->width;
        screen->ya_height = frame->height;
    }
    for (size_t index = 0U; index < pixels; index++) {
        uint32_t pixel = frame->pixels[index];
        uint8_t red = (uint8_t)(pixel >> 16U);
        uint8_t green = (uint8_t)(pixel >> 8U);
        uint8_t blue = (uint8_t)pixel;

        screen->ya[index * 2U] = (uint8_t)((77U * red + 150U * green +
                                            29U * blue + 128U) >> 8U);
        screen->ya[index * 2U + 1U] = UINT8_MAX;
    }
    convert_finish = monotonic_now();
    measured.convert_seconds += seconds_between(convert_start, convert_finish);
    measured.convert_count++;
    return 0;
}

static int display_init(struct display_context *screen)
{
    screen->fd = fbink_open();
    if (screen->fd < 0) {
        fprintf(stderr, "display: fbink_open failed: %s\n", strerror(errno));
        return -1;
    }
    screen->draw_config.is_quiet = true;
    if (fbink_init(screen->fd, &screen->draw_config) != 0) {
        fprintf(stderr, "display: fbink_init failed: %s\n", strerror(errno));
        return -1;
    }
    screen->initialized = true;
    fbink_get_state(&screen->draw_config, &screen->state);
    if (screen->state.view_width == 0U || screen->state.view_height == 0U) {
        fprintf(stderr, "display: invalid viewport %ux%u\n",
                screen->state.view_width, screen->state.view_height);
        return -1;
    }
    if (fbink_dump(screen->fd, &screen->dump) != 0) {
        fprintf(stderr, "display: fbink_dump failed: %s\n", strerror(errno));
        return -1;
    }
    screen->dumped = true;
    screen->draw_config.no_refresh = true;
    if (selected_scale == SCALE_SMOOTH) {
        screen->draw_config.scaled_width = -2;
        screen->draw_config.scaled_height = -2;
    } else {
        screen->draw_config.scaled_width = 0;
        screen->draw_config.scaled_height = 0;
    }
    screen->draw_config.halign = CENTER;
    screen->draw_config.valign = CENTER;
    screen->refresh_config.wfm_mode = WFM_AUTO;
    screen->first_update = true;
    return 0;
}

static int scale_nearest(struct display_context *screen)
{
    size_t x_bytes;
    size_t y_bytes;
    size_t pixels;
    size_t output_size;
    struct timespec scale_start = monotonic_now();

    if (screen->ya_width == 0U || screen->ya_height == 0U ||
        screen->state.view_width == 0U || screen->state.view_height == 0U) {
        return -1;
    }
    screen->scaled_width = screen->state.view_width;
    screen->scaled_height = screen->state.view_height;
    if ((uint64_t)screen->scaled_width * screen->ya_height <=
        (uint64_t)screen->scaled_height * screen->ya_width) {
        screen->scaled_height = (unsigned)(((uint64_t)screen->scaled_width *
                                            screen->ya_height) /
                                           screen->ya_width);
    } else {
        screen->scaled_width = (unsigned)(((uint64_t)screen->scaled_height *
                                           screen->ya_width) /
                                          screen->ya_height);
    }
    if (screen->scaled_width == 0U || screen->scaled_height == 0U ||
        !size_mul((size_t)screen->scaled_width, screen->scaled_height,
                  &pixels) || !size_mul(pixels, 2U, &output_size) ||
        !size_mul((size_t)screen->scaled_width, sizeof(*screen->nearest_x),
                  &x_bytes) ||
        !size_mul((size_t)screen->scaled_height, sizeof(*screen->nearest_y),
                  &y_bytes)) {
        return -1;
    }
    if (screen->scaled_ya == NULL || screen->scaled_ya_size != output_size) {
        unsigned char *scaled = malloc(output_size);

        if (scaled == NULL) {
            return -1;
        }
        free(screen->scaled_ya);
        screen->scaled_ya = scaled;
        screen->scaled_ya_size = output_size;
    }
    if (screen->nearest_x == NULL || screen->nearest_x_size != x_bytes) {
        uint32_t *nearest_x = malloc(x_bytes);

        if (nearest_x == NULL) {
            return -1;
        }
        free(screen->nearest_x);
        screen->nearest_x = nearest_x;
        screen->nearest_x_size = x_bytes;
    }
    if (screen->nearest_y == NULL || screen->nearest_y_size != y_bytes) {
        uint32_t *nearest_y = malloc(y_bytes);

        if (nearest_y == NULL) {
            return -1;
        }
        free(screen->nearest_y);
        screen->nearest_y = nearest_y;
        screen->nearest_y_size = y_bytes;
    }
    if (screen->nearest_x_width != screen->scaled_width ||
        screen->nearest_x_source_width != screen->ya_width) {
        for (unsigned x = 0U; x < screen->scaled_width; x++) {
            screen->nearest_x[x] = (uint32_t)(((uint64_t)x * screen->ya_width) /
                                             screen->scaled_width);
        }
        screen->nearest_x_width = screen->scaled_width;
        screen->nearest_x_source_width = screen->ya_width;
    }
    if (screen->nearest_y_height != screen->scaled_height ||
        screen->nearest_y_source_height != screen->ya_height) {
        for (unsigned y = 0U; y < screen->scaled_height; y++) {
            screen->nearest_y[y] = (uint32_t)(((uint64_t)y * screen->ya_height) /
                                             screen->scaled_height);
        }
        screen->nearest_y_height = screen->scaled_height;
        screen->nearest_y_source_height = screen->ya_height;
    }
    for (unsigned y = 0U; y < screen->scaled_height; y++) {
        const unsigned char *source_row =
            screen->ya + (size_t)screen->nearest_y[y] * screen->ya_width * 2U;
        unsigned char *destination =
            screen->scaled_ya + (size_t)y * screen->scaled_width * 2U;

        for (unsigned x = 0U; x < screen->scaled_width; x++) {
            const unsigned char *source =
                source_row + (size_t)screen->nearest_x[x] * 2U;

            *destination++ = source[0];
            *destination++ = source[1];
        }
    }
    measured.nearest_scale_seconds +=
        seconds_between(scale_start, monotonic_now());
    measured.nearest_scale_count++;
    return 0;
}

static int present_frame(struct display_context *screen,
                         const struct frame_buffer *frame)
{
    FBInkRect rect;
    struct timespec draw_start;
    struct timespec draw_finish;
    struct timespec before_refresh;
    struct timespec after_refresh;
    int result;

    if (convert_to_ya(screen, frame) != 0) {
        fprintf(stderr, "display: unable to convert frame to YA\n");
        return -1;
    }
    {
        const unsigned char *data = screen->ya;
        unsigned width = frame->width;
        unsigned height = frame->height;

        if (selected_scale == SCALE_NEAREST) {
            if (scale_nearest(screen) != 0) {
                fprintf(stderr, "display: unable to nearest-scale frame\n");
                return -1;
            }
            data = screen->scaled_ya;
            width = screen->scaled_width;
            height = screen->scaled_height;
        }
    screen->draw_config.is_cleared = screen->first_update;
    draw_start = monotonic_now();
    result = fbink_print_raw_data(screen->fd, data, (int)width,
                                  (int)height,
                                  (size_t)width * height * 2U,
                                  0, 0, &screen->draw_config);
    draw_finish = monotonic_now();
    measured.draw_seconds += seconds_between(draw_start, draw_finish);
    measured.draw_attempts++;
    if (result != 0) {
        fprintf(stderr, "display: fbink_print_raw_data failed: %d (%s)\n",
                result, strerror(errno));
        measured.draw_failures++;
        measured.refresh_failures++;
        return -1;
    }
    }

    before_refresh = monotonic_now();
    if (screen->first_update) {
        fprintf(stderr, "display: refresh_rect x=0 y=0 w=%u h=%u (full)\n",
                screen->state.view_width, screen->state.view_height);
        screen->refresh_config.is_flashing = true;
        result = fbink_refresh(screen->fd, 0U, 0U, 0U, 0U,
                               &screen->refresh_config);
    } else {
        screen->refresh_config.is_flashing = false;
        rect = fbink_get_last_rect(false);
        if (rect.width == 0U || rect.height == 0U) {
            fprintf(stderr, "display: fbink_get_last_rect returned empty rect\n");
            measured.refresh_failures++;
            return -1;
        }
        fprintf(stderr, "display: refresh_rect x=%u y=%u w=%u h=%u\n",
                rect.left, rect.top, rect.width, rect.height);
        result = fbink_refresh_rect(screen->fd, &rect,
                                    &screen->refresh_config);
    }
    after_refresh = monotonic_now();
    measured.refresh_submit_seconds +=
        seconds_between(before_refresh, after_refresh);
    measured.refresh_attempts++;
    if (result != 0) {
        fprintf(stderr, "display: refresh submission failed: %d (%s)\n",
                result, strerror(errno));
        measured.refresh_failures++;
        return -1;
    }

    before_refresh = monotonic_now();
    result = fbink_wait_for_complete(screen->fd, LAST_MARKER);
    after_refresh = monotonic_now();
    measured.refresh_wait_seconds +=
        seconds_between(before_refresh, after_refresh);
    if (result != 0) {
        fprintf(stderr, "display: refresh completion failed: %d (%s)\n",
                result, strerror(errno));
        measured.refresh_failures++;
        return -1;
    }
    screen->first_update = false;
    measured.display_updates++;
    return 0;
}

static int display_restore(struct display_context *screen)
{
    int result = 0;

    if (screen->dumped) {
        screen->refresh_config.is_flashing = true;
        result = fbink_restore(screen->fd, &screen->refresh_config,
                               &screen->dump);
        if (result != 0) {
            fprintf(stderr, "display: framebuffer restore failed: %d (%s)\n",
                    result, strerror(errno));
        } else {
            int wait_result = fbink_wait_for_complete(screen->fd, LAST_MARKER);

            if (wait_result != 0) {
                fprintf(stderr, "display: restore completion failed: %d (%s)\n",
                        wait_result, strerror(errno));
                result = -1;
            }
        }
    }
    if (screen->dump.data != NULL && fbink_free_dump_data(&screen->dump) != 0) {
        fprintf(stderr, "display: unable to free framebuffer dump\n");
        result = -1;
    }
    if (screen->fd >= 0 && fbink_close(screen->fd) != 0) {
        fprintf(stderr, "display: fbink_close failed: %s\n", strerror(errno));
        result = -1;
    }
    screen->fd = -1;
    return result;
}

static void report_metrics(double elapsed, uint64_t cpu_start)
{
    double runs = (double)measured.runs;
    double cpu_percent = 0.0;
    uint64_t cpu_now = usage_cpu_microseconds();

    if (cpu_now >= cpu_start && elapsed > 0.0) {
        cpu_percent = (double)(cpu_now - cpu_start) / 10000.0 / elapsed;
    }
    printf("metrics elapsed=%.3f emu_fps=%.2f display_fps=%.2f "
           "avg_emu_frame_ms=%.3f avg_callback_copy_ms=%.3f "
           "target_sample_rate_hz=%u target_sample_rate_queries=%lu "
           "target_sample_rate_responses=%lu audio_batch_callbacks=%lu "
           "audio_frames=%lu "
           "avg_convert_ms=%.3f avg_fbink_draw_ms=%.3f "
           "avg_nearest_scale_ms=%.3f "
           "avg_refresh_submit_ms=%.3f avg_refresh_wait_ms=%.3f "
           "avg_schedule_ms=%.3f sleep_ms=%.3f cpu_pct=%.2f "
           "rss_kib=%" PRIu64 " scheduled_display_attempts=%lu "
           "skipped_display_frames=%lu refresh_failures=%lu\n",
           elapsed, runs / elapsed,
           (double)measured.display_updates / elapsed,
           runs > 0.0 ? measured.emu_run_seconds * 1000.0 / runs : 0.0,
           measured.callback_copy_count > 0U
               ? measured.callback_copy_seconds * 1000.0 /
                     (double)measured.callback_copy_count
               : 0.0,
           target_sample_rate, target_sample_rate_queries,
           target_sample_rate_responses, measured.audio_batch_callbacks,
           measured.audio_frames,
           measured.convert_count > 0U
               ? measured.convert_seconds * 1000.0 /
                     (double)measured.convert_count
               : 0.0,
           measured.draw_attempts > 0U
               ? measured.draw_seconds * 1000.0 /
                     (double)measured.draw_attempts
               : 0.0,
           measured.nearest_scale_count > 0U
               ? measured.nearest_scale_seconds * 1000.0 /
                     (double)measured.nearest_scale_count
               : 0.0,
           measured.refresh_attempts > 0U
               ? measured.refresh_submit_seconds * 1000.0 /
                     (double)measured.refresh_attempts
               : 0.0,
           measured.display_updates > 0U
               ? measured.refresh_wait_seconds * 1000.0 /
                     (double)measured.display_updates
               : 0.0,
           measured.scheduling_count > 0U
               ? measured.scheduling_seconds * 1000.0 /
                     (double)measured.scheduling_count
               : 0.0,
           measured.sleep_count > 0U
               ? measured.sleep_seconds * 1000.0 /
                     (double)measured.sleep_count
               : 0.0,
           cpu_percent, rss_kib(), measured.scheduled_display_attempts,
           measured.skipped_display_frames, measured.refresh_failures);
    fflush(stdout);
}

static int usage(const char *program)
{
    fprintf(stderr, "usage: %s CORE ROM [duration_seconds] [display_fps] "
                    "[--warmup FRAMES] [--capture-only] "
                    "[--scale smooth|native|nearest] "
                    "[--target-sample-rate HZ]\n", program);
    return 2;
}

int main(int argc, char **argv)
{
    const char *core_path;
    const char *rom_path;
    double duration = DEFAULT_DURATION_SECONDS;
    double display_fps = DEFAULT_DISPLAY_FPS;
    bool duration_set = false;
    bool display_fps_set = false;
    bool capture_only = false;
    FILE *rom = NULL;
    void *rom_data = NULL;
    long rom_size;
    void *handle = NULL;
    struct core_api api = {0};
    bool core_initialized = false;
    bool game_loaded = false;
    struct display_context screen = {.fd = -1};
    struct timespec start;
    struct timespec next_display;
    struct timespec next_emulation;
    struct timespec next_report;
    struct timespec finish;
    uint64_t cpu_start = 0U;
    double emu_fps = 60.0;
    int exit_code = 1;

    if (argc < 3 || argc > 12) {
        return usage(argv[0]);
    }
    core_path = argv[1];
    rom_path = argv[2];
    for (int argument = 3; argument < argc; argument++) {
        if (strcmp(argv[argument], "--capture-only") == 0) {
            capture_only = true;
        } else if (strcmp(argv[argument], "--scale") == 0 &&
                   argument + 1 < argc) {
            const char *mode = argv[++argument];

            if (strcmp(mode, "smooth") == 0) {
                selected_scale = SCALE_SMOOTH;
            } else if (strcmp(mode, "native") == 0) {
                selected_scale = SCALE_NATIVE;
            } else if (strcmp(mode, "nearest") == 0) {
                selected_scale = SCALE_NEAREST;
            } else {
                return usage(argv[0]);
            }
        } else if (strcmp(argv[argument], "--warmup") == 0 &&
                   argument + 1 < argc &&
                   parse_unsigned(argv[++argument], &warmup_frames) == 0) {
        } else if (strncmp(argv[argument], "--warmup=", 9U) == 0 &&
                   parse_unsigned(argv[argument] + 9U, &warmup_frames) == 0) {
        } else if (strcmp(argv[argument], "--target-sample-rate") == 0 &&
                   argument + 1 < argc) {
            unsigned long parsed_sample_rate;

            if (parse_unsigned(argv[++argument], &parsed_sample_rate) != 0 ||
                parsed_sample_rate <= 8000UL ||
                parsed_sample_rate >= 1024UL * 1024UL) {
                return usage(argv[0]);
            }
            target_sample_rate = (unsigned)parsed_sample_rate;
        } else if (!duration_set &&
                   parse_positive_double(argv[argument], &duration) == 0) {
            duration_set = true;
        } else if (!display_fps_set &&
                   parse_positive_double(argv[argument], &display_fps) == 0) {
            display_fps_set = true;
        } else {
            return usage(argv[0]);
        }
    }
    if (display_fps > MAX_DISPLAY_FPS) {
        fprintf(stderr, "display_fps must be <= %.0f\n", MAX_DISPLAY_FPS);
        return 2;
    }
    if (install_signal_handlers() != 0) {
        fprintf(stderr, "signals: unable to install handlers: %s\n", strerror(errno));
        return 2;
    }

    rom = fopen(rom_path, "rb");
    if (rom == NULL || fseek(rom, 0, SEEK_END) != 0 ||
        (rom_size = ftell(rom)) <= 0 || fseek(rom, 0, SEEK_SET) != 0) {
        fprintf(stderr, "ROM: unable to open or size %s: %s\n",
                rom_path, strerror(errno));
        goto cleanup;
    }
    rom_data = malloc((size_t)rom_size);
    if (rom_data == NULL || fread(rom_data, 1U, (size_t)rom_size, rom) !=
                                (size_t)rom_size) {
        fprintf(stderr, "ROM: unable to read %s\n", rom_path);
        goto cleanup;
    }
    fclose(rom);
    rom = NULL;

    handle = dlopen(core_path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL || !load_core_api(handle, &api)) {
        fprintf(stderr, "core: unable to load %s\n", core_path);
        goto cleanup;
    }
    api.set_environment(environment);
    api.set_video_refresh(video_refresh);
    api.set_audio_sample(audio_sample);
    api.set_audio_batch(audio_batch);
    api.set_input_poll(input_poll);
    api.set_input_state(input_state);
    api.init();
    core_initialized = true;
    {
        struct retro_game_info game = {
            .path = rom_path, .data = rom_data, .size = (size_t)rom_size
        };
        if (!api.load_game(&game)) {
            fprintf(stderr, "retro_load_game failed\n");
            goto cleanup;
        }
    }
    game_loaded = true;
    {
        struct retro_system_av_info av_info = {0};

        api.get_system_av_info(&av_info);
        if (av_info.timing.fps > 1.0 && av_info.timing.fps < 1000.0) {
            emu_fps = av_info.timing.fps;
        }
        fprintf(stderr, "timing: nominal emu_fps=%.6f\n", emu_fps);
    }

    for (; total_runs < warmup_frames && !stop_requested; total_runs++) {
        api.run();
    }
    if (capture_error || !pixel_format_seen || !pixel_format_ok) {
        fprintf(stderr, "capture: warmup did not establish XRGB8888 output\n");
        goto cleanup;
    }
    if (!capture_only && display_init(&screen) != 0) {
        goto cleanup;
    }

    measured = (struct metrics){0};
    start = monotonic_now();
    next_display = start;
    next_emulation = start;
    next_report = add_seconds(start, 1.0);
    cpu_start = usage_cpu_microseconds();
    while (!stop_requested && seconds_between(start, monotonic_now()) < duration) {
        struct timespec before_run = monotonic_now();
        struct timespec after_run;

        api.run();
        after_run = monotonic_now();
        total_runs++;
        measured.runs++;
        measured.emu_run_seconds += seconds_between(before_run, after_run);
        next_emulation = add_seconds(next_emulation, 1.0 / emu_fps);
        if (before(next_emulation, after_run)) {
            next_emulation = after_run;
        }

        if (!capture_only && producer.ready &&
            !before(monotonic_now(), next_display)) {
            struct frame_buffer selected = producer;

            producer = display;
            producer.ready = false;
            display = selected;
            measured.scheduled_display_attempts++;
            if (present_frame(&screen, &display) != 0) {
                stop_requested = 1;
            } else {
                next_display = add_seconds(monotonic_now(), 1.0 / display_fps);
            }
        }
        {
            struct timespec current = monotonic_now();
            struct timespec scheduling_start;
            struct timespec scheduling_finish;
            struct timespec sleep_start;
            struct timespec sleep_finish;
            struct timespec sleep_until;
            struct timespec remaining = {0};
            bool should_sleep = false;

            if (!before(current, next_report)) {
                double elapsed = seconds_between(start, current);

                report_metrics(elapsed, cpu_start);
                do {
                    next_report = add_seconds(next_report, 1.0);
                } while (!before(current, next_report));
            }

            scheduling_start = monotonic_now();
            current = monotonic_now();
            if (before(current, next_emulation)) {
                sleep_until = next_emulation;

                if (!capture_only && before(next_display, sleep_until)) {
                    sleep_until = next_display;
                }
                remaining.tv_sec = sleep_until.tv_sec - current.tv_sec;
                remaining.tv_nsec = sleep_until.tv_nsec - current.tv_nsec;
                if (remaining.tv_nsec < 0) {
                    remaining.tv_sec--;
                    remaining.tv_nsec += 1000000000L;
                }
                should_sleep = remaining.tv_sec >= 0;
            }
            scheduling_finish = monotonic_now();
            measured.scheduling_seconds +=
                seconds_between(scheduling_start, scheduling_finish);
            measured.scheduling_count++;
            if (should_sleep) {
                sleep_start = monotonic_now();
                nanosleep(&remaining, NULL);
                sleep_finish = monotonic_now();
                measured.sleep_seconds += seconds_between(sleep_start,
                                                           sleep_finish);
                measured.sleep_count++;
            }
        }
    }
    finish = monotonic_now();
    if (measured.runs > 0U) {
        report_metrics(seconds_between(start, finish), cpu_start);
    }
    exit_code = capture_error ? 1 : 0;

cleanup:
    if (screen.fd >= 0 && display_restore(&screen) != 0) {
        screen.restoration_failed = true;
        exit_code = 1;
    }
    free(screen.ya);
    free(screen.scaled_ya);
    free(screen.nearest_x);
    free(screen.nearest_y);
    free(producer.pixels);
    free(display.pixels);
    if (game_loaded) {
        api.unload_game();
    }
    if (core_initialized) {
        api.deinit();
    }
    if (rom != NULL) {
        fclose(rom);
    }
    free(rom_data);
    if (handle != NULL) {
        dlclose(handle);
    }
    printf("final total_runs=%lu callbacks=%lu valid_frames=%lu "
           "scheduled_display_attempts=%lu display_updates=%lu "
           "skipped_frames=%lu refresh_attempts=%lu refresh_failures=%lu "
           "shutdown_reason=%s restoration_status=%s\n",
           total_runs, measured.callbacks, measured.valid_frames,
           measured.scheduled_display_attempts, measured.display_updates,
           measured.skipped_display_frames, measured.refresh_attempts,
           measured.refresh_failures,
           signal_received ? "signal" :
               (capture_error || measured.refresh_failures > 0U
                    ? "error" : "duration"),
           screen.restoration_failed ? "failed" :
               (screen.fd < 0 ? "complete-or-not-requested" : "not-attempted"));
    return exit_code;
}
