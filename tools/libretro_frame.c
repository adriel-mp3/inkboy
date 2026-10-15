#define _POSIX_C_SOURCE 200809L

#include "fbink.h"
#include "libretro.h"

#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

typedef void (*fn_init)(void);
typedef void (*fn_deinit)(void);
typedef unsigned (*fn_api)(void);
typedef void (*fn_set_environment)(retro_environment_t);
typedef void (*fn_set_video_refresh)(retro_video_refresh_t);
typedef void (*fn_set_audio_sample)(retro_audio_sample_t);
typedef void (*fn_set_audio_sample_batch)(retro_audio_sample_batch_t);
typedef void (*fn_set_input_poll)(retro_input_poll_t);
typedef void (*fn_set_input_state)(retro_input_state_t);
typedef void (*fn_run)(void);
typedef bool (*fn_load)(const struct retro_game_info *);
typedef void (*fn_unload)(void);
typedef void (*fn_get_system_info)(struct retro_system_info *);

#define DEFAULT_FRAME_LIMIT 240UL
#define DEFAULT_WARMUP_FRAMES 180UL
#define DEFAULT_HOLD_SECONDS 10.0
#define MAX_FRAME_WIDTH 4096U
#define MAX_FRAME_HEIGHT 4096U

static volatile sig_atomic_t stop_requested;
static enum retro_pixel_format negotiated = RETRO_PIXEL_FORMAT_UNKNOWN;
static bool pixel_format_seen;
static bool pixel_format_ok;
static unsigned long warmup_frames;
static unsigned long retro_runs;

struct captured_frame {
    uint32_t *pixels;
    unsigned width;
    unsigned height;
    size_t row_bytes;
    size_t pitch;
    bool captured;
    bool error;
};

static struct captured_frame captured;
static unsigned long video_callbacks;

static void
request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int
install_signal_handlers(void)
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

static bool
size_mul(size_t left, size_t right, size_t *result)
{
    if (right != 0U && left > SIZE_MAX / right) {
        return false;
    }
    *result = left * right;
    return true;
}

static void
log_cb(enum retro_log_level level, const char *format, ...)
{
    va_list arguments;

    fprintf(stderr, "core[%d]: ", (int)level);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
}

static bool
environment(unsigned command, void *data)
{
    if (command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) {
        enum retro_pixel_format requested = *(enum retro_pixel_format *)data;

        pixel_format_seen = true;
        pixel_format_ok = requested == RETRO_PIXEL_FORMAT_XRGB8888;
        if (pixel_format_ok) {
            negotiated = requested;
        }
        fprintf(stderr,
                "environment: SET_PIXEL_FORMAT requested=%d %s\n",
                requested,
                pixel_format_ok ? "accepted" : "rejected");
        return pixel_format_ok;
    }
    if (command == RETRO_ENVIRONMENT_GET_LOG_INTERFACE) {
        struct retro_log_callback *callback = data;

        callback->log = log_cb;
        return true;
    }
    if (command == RETRO_ENVIRONMENT_GET_CAN_DUPE) {
        *(bool *)data = true;
        return true;
    }
    return false;
}

static void
capture_video(const void *data, unsigned width, unsigned height, size_t pitch)
{
    size_t row_bytes;
    size_t frame_bytes;
    uint32_t *copy;
    unsigned row;
    bool first_capture;

    video_callbacks++;
    if (captured.error || data == NULL || retro_runs < warmup_frames) {
        return;
    }
    if (width == 0U || height == 0U || width > MAX_FRAME_WIDTH ||
        height > MAX_FRAME_HEIGHT ||
        !size_mul((size_t)width, sizeof(uint32_t), &row_bytes) ||
        pitch < row_bytes ||
        !size_mul(row_bytes, (size_t)height, &frame_bytes) ||
        (height > 1U &&
         (pitch > (SIZE_MAX - row_bytes) / (size_t)(height - 1U)))) {
        fprintf(stderr,
                "capture: invalid XRGB8888 frame %ux%u pitch=%zu\n",
                width,
                height,
                pitch);
        captured.error = true;
        return;
    }

    copy = malloc(frame_bytes);
    if (copy == NULL) {
        fprintf(stderr, "capture: unable to allocate %zu bytes\n", frame_bytes);
        captured.error = true;
        return;
    }
    first_capture = !captured.captured;
    for (row = 0U; row < height; row++) {
        memcpy((unsigned char *)copy + (size_t)row * row_bytes,
               (const unsigned char *)data + (size_t)row * pitch,
               row_bytes);
    }
    free(captured.pixels);
    captured.pixels = copy;
    captured.width = width;
    captured.height = height;
    captured.row_bytes = row_bytes;
    captured.pitch = pitch;
    captured.captured = true;
    if (first_capture) {
        fprintf(stderr,
                "capture: first post-warmup frame width=%u height=%u pitch=%zu bytes=%zu\n",
                width,
                height,
                pitch,
                frame_bytes);
    }
}

static void
audio_sample(int16_t left, int16_t right)
{
    (void)left;
    (void)right;
}

static size_t
audio_batch(const int16_t *data, size_t frames)
{
    (void)data;
    return frames;
}

static void
input_poll(void)
{
}

static int16_t
input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)port;
    (void)device;
    (void)index;
    (void)id;
    return 0;
}

static uint64_t
rss_kib(void)
{
    struct rusage usage;

    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0U;
    }
    return (uint64_t)usage.ru_maxrss;
}

static void *
symbol(void *handle, const char *name)
{
    void *address = dlsym(handle, name);

    if (address == NULL) {
        fprintf(stderr, "missing symbol: %s\n", name);
    }
    return address;
}

static int
parse_frames(const char *text, unsigned long *frames)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0U) {
        return -1;
    }
    *frames = value;
    return 0;
}

static int
parse_hold_seconds(const char *text, double *seconds)
{
    char *end = NULL;
    double value;

    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || value < 0.0 ||
        value > 3600.0) {
        return -1;
    }
    *seconds = value;
    return 0;
}

static int
usage(const char *program)
{
    fprintf(stderr,
            "usage: %s CORE ROM [frames] [hold_seconds] "
            "[--warmup FRAMES] [--capture-only]\n",
            program);
    return 2;
}

static void
free_capture(void)
{
    free(captured.pixels);
    captured.pixels = NULL;
    captured.captured = false;
}

static int
convert_to_ya(unsigned char **output, size_t *output_size)
{
    size_t pixel_count;
    size_t size;
    unsigned char *ya;
    unsigned row;
    unsigned column;

    if (!size_mul((size_t)captured.width, (size_t)captured.height, &pixel_count) ||
        !size_mul(pixel_count, 2U, &size)) {
        return -1;
    }
    ya = malloc(size);
    if (ya == NULL) {
        return -1;
    }
    for (row = 0U; row < captured.height; row++) {
        for (column = 0U; column < captured.width; column++) {
            uint32_t pixel;
            uint8_t red;
            uint8_t green;
            uint8_t blue;
            uint8_t gray;
            size_t source_offset = (size_t)row * captured.row_bytes +
                                   (size_t)column * sizeof(uint32_t);
            size_t output_offset = ((size_t)row * captured.width + column) * 2U;

            memcpy(&pixel, (const unsigned char *)captured.pixels + source_offset,
                   sizeof(pixel));
            red = (uint8_t)(pixel >> 16U);
            green = (uint8_t)(pixel >> 8U);
            blue = (uint8_t)pixel;
            gray = (uint8_t)((77U * red + 150U * green + 29U * blue + 128U) >> 8U);
            ya[output_offset] = gray;
            ya[output_offset + 1U] = UINT8_MAX;
        }
    }
    *output = ya;
    *output_size = size;
    return 0;
}

static int
print_gray_stats(const unsigned char *ya, size_t size, unsigned width, unsigned height)
{
    size_t pixels;
    size_t index;
    uint64_t sum = 0U;
    size_t below_250 = 0U;
    uint8_t minimum = UINT8_MAX;
    uint8_t maximum = 0U;
    uint64_t hash = UINT64_C(1469598103934665603);

    if (!size_mul((size_t)width, (size_t)height, &pixels) ||
        !size_mul(pixels, 2U, &index) || index != size || pixels == 0U) {
        return -1;
    }
    for (index = 0U; index < size; index++) {
        hash ^= ya[index];
        hash *= UINT64_C(1099511628211);
    }
    for (index = 0U; index < pixels; index++) {
        uint8_t luminance = ya[index * 2U];

        if (luminance < minimum) {
            minimum = luminance;
        }
        if (luminance > maximum) {
            maximum = luminance;
        }
        sum += luminance;
        if (luminance < 250U) {
            below_250++;
        }
    }
    printf("grayscale min=%u max=%u average=%.3f below_250=%zu "
           "hash=fnv1a64:%016" PRIx64 "\n",
           minimum,
           maximum,
           (double)sum / (double)pixels,
           below_250,
           hash);
    return 0;
}

static void
wait_for_display(double seconds)
{
    struct timespec remaining;
    time_t whole_seconds = (time_t)seconds;

    remaining.tv_sec = whole_seconds;
    remaining.tv_nsec = (long)((seconds - (double)whole_seconds) * 1000000000.0);
    while (!stop_requested &&
           nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

static int
restore_display(int fbfd,
                FBInkDump *dump,
                bool dumped,
                const FBInkConfig *refresh_config)
{
    int result = 0;

    if (dumped) {
        result = fbink_restore(fbfd, refresh_config, dump);
        if (result != 0) {
            fprintf(stderr, "display: unable to restore previous screen: %d\n", result);
        } else {
            (void)fbink_wait_for_complete(fbfd, LAST_MARKER);
        }
    }
    if (dump->data != NULL && fbink_free_dump_data(dump) != 0) {
        fprintf(stderr, "display: unable to free framebuffer dump\n");
        result = -1;
    }
    if (fbink_close(fbfd) != 0) {
        fprintf(stderr, "display: unable to close framebuffer\n");
        result = -1;
    }
    return result;
}

int
main(int argc, char **argv)
{
    const char *core_path;
    const char *rom_path;
    unsigned long frame_limit = DEFAULT_FRAME_LIMIT;
    unsigned long requested_warmup = DEFAULT_WARMUP_FRAMES;
    double hold_seconds = DEFAULT_HOLD_SECONDS;
    bool frame_limit_set = false;
    bool warmup_set = false;
    bool hold_seconds_set = false;
    bool capture_only = false;
    FILE *rom = NULL;
    void *rom_data = NULL;
    long rom_size;
    void *handle = NULL;
    bool core_initialized = false;
    bool game_loaded = false;
    int fbfd = FBFD_AUTO;
    bool dumped = false;
    FBInkConfig config = {0};
    FBInkConfig refresh_config = {0};
    FBInkState state = {0};
    FBInkDump dump = {0};
    unsigned char *gray_ya = NULL;
    size_t gray_ya_size = 0U;
    struct timespec start;
    struct timespec finish;
    double elapsed;
    int exit_code = 2;

    if (argc < 3 || argc > 8) {
        return usage(argv[0]);
    }
    core_path = argv[1];
    rom_path = argv[2];
    for (int argument = 3; argument < argc; argument++) {
        if (strcmp(argv[argument], "--capture-only") == 0) {
            capture_only = true;
        } else if (strcmp(argv[argument], "--warmup") == 0 &&
                   argument + 1 < argc &&
                   parse_frames(argv[++argument], &requested_warmup) == 0) {
            warmup_set = true;
        } else if (strncmp(argv[argument], "--warmup=", 9U) == 0 &&
                   parse_frames(argv[argument] + 9U, &requested_warmup) == 0) {
            warmup_set = true;
        } else if (!frame_limit_set && parse_frames(argv[argument], &frame_limit) == 0) {
            frame_limit_set = true;
        } else if (!hold_seconds_set &&
                   parse_hold_seconds(argv[argument], &hold_seconds) == 0) {
            hold_seconds_set = true;
        } else {
            return usage(argv[0]);
        }
    }
    if (warmup_set) {
        warmup_frames = requested_warmup;
    } else {
        warmup_frames = DEFAULT_WARMUP_FRAMES;
    }
    if (warmup_frames >= frame_limit) {
        fprintf(stderr,
                "frames: total limit %lu must exceed warmup %lu\n",
                frame_limit,
                warmup_frames);
        return 2;
    }
    if (install_signal_handlers() != 0) {
        fprintf(stderr, "signals: unable to install handlers: %s\n", strerror(errno));
        return 2;
    }

    rom = fopen(rom_path, "rb");
    if (rom == NULL) {
        fprintf(stderr, "ROM: %s: %s\n", rom_path, strerror(errno));
        return 2;
    }
    if (fseek(rom, 0, SEEK_END) != 0 || (rom_size = ftell(rom)) <= 0 ||
        fseek(rom, 0, SEEK_SET) != 0) {
        fprintf(stderr, "ROM: invalid size\n");
        goto cleanup;
    }
    rom_data = malloc((size_t)rom_size);
    if (rom_data == NULL ||
        fread(rom_data, 1U, (size_t)rom_size, rom) != (size_t)rom_size) {
        fprintf(stderr, "ROM: unable to read %s\n", rom_path);
        goto cleanup;
    }
    fclose(rom);
    rom = NULL;

    handle = dlopen(core_path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "core: %s\n", dlerror());
        goto cleanup;
    }

#define LOAD(name, type) type name = (type)symbol(handle, #name); if (name == NULL) goto cleanup
    LOAD(retro_api_version, fn_api);
    LOAD(retro_set_environment, fn_set_environment);
    LOAD(retro_set_video_refresh, fn_set_video_refresh);
    LOAD(retro_set_audio_sample, fn_set_audio_sample);
    LOAD(retro_set_audio_sample_batch, fn_set_audio_sample_batch);
    LOAD(retro_set_input_poll, fn_set_input_poll);
    LOAD(retro_set_input_state, fn_set_input_state);
    LOAD(retro_init, fn_init);
    LOAD(retro_deinit, fn_deinit);
    LOAD(retro_run, fn_run);
    LOAD(retro_load_game, fn_load);
    LOAD(retro_unload_game, fn_unload);
    LOAD(retro_get_system_info, fn_get_system_info);
#undef LOAD

    {
        struct retro_system_info info = {0};

        retro_get_system_info(&info);
        printf("api=%u name=%s version=%s\n",
               retro_api_version(),
               info.library_name != NULL ? info.library_name : "?",
               info.library_version != NULL ? info.library_version : "?");
    }
    retro_set_environment(environment);
    retro_set_video_refresh(capture_video);
    retro_set_audio_sample(audio_sample);
    retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    retro_init();
    core_initialized = true;

    {
        struct retro_game_info game = {
            .path = rom_path,
            .data = rom_data,
            .size = (size_t)rom_size,
            .meta = NULL
        };

        if (!retro_load_game(&game)) {
            fprintf(stderr, "retro_load_game failed\n");
            goto cleanup;
        }
    }
    game_loaded = true;
    retro_runs = 0U;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (; retro_runs < frame_limit && !captured.error &&
         !stop_requested;
         retro_runs++) {
        retro_run();
    }
    clock_gettime(CLOCK_MONOTONIC, &finish);
    elapsed = (finish.tv_sec - start.tv_sec) +
              (finish.tv_nsec - start.tv_nsec) / 1e9;
    if (captured.error || !captured.captured) {
        fprintf(stderr,
                "capture: no valid frame after %lu runs (%lu callbacks)\n",
                retro_runs,
                video_callbacks);
        goto cleanup;
    }
    if (!pixel_format_seen || !pixel_format_ok ||
        negotiated != RETRO_PIXEL_FORMAT_XRGB8888) {
        fprintf(stderr, "capture: XRGB8888 was not confirmed\n");
        goto cleanup;
    }
    if (convert_to_ya(&gray_ya, &gray_ya_size) != 0) {
        fprintf(stderr, "convert: unable to allocate or create YA frame\n");
        goto cleanup;
    }
    if (game_loaded) {
        retro_unload_game();
        game_loaded = false;
    }
    if (core_initialized) {
        retro_deinit();
        core_initialized = false;
    }
    dlclose(handle);
    handle = NULL;

    printf("captured format=XRGB8888 dimensions=%ux%u pitch=%zu bytes=%zu "
           "warmup=%lu runs=%lu callbacks=%lu seconds=%.6f rss_kib=%" PRIu64 "\n",
           captured.width,
           captured.height,
           captured.pitch,
           gray_ya_size,
           warmup_frames,
           retro_runs,
           video_callbacks,
           elapsed,
           rss_kib());
    if (print_gray_stats(gray_ya,
                         gray_ya_size,
                         captured.width,
                         captured.height) != 0) {
        fprintf(stderr, "diagnostic: unable to calculate grayscale statistics\n");
        goto cleanup;
    }
    if (capture_only) {
        exit_code = 0;
        goto cleanup;
    }

    fbfd = fbink_open();
    if (fbfd < 0) {
        fprintf(stderr, "display: fbink_open failed: %s\n", strerror(errno));
        goto cleanup;
    }
    config.is_quiet = true;
    if (fbink_init(fbfd, &config) != 0) {
        fprintf(stderr, "display: fbink_init failed\n");
        goto cleanup_display;
    }
    fbink_get_state(&config, &state);
    if (state.view_width == 0U || state.view_height == 0U) {
        fprintf(stderr, "display: invalid FBInk viewport %ux%u\n",
                state.view_width,
                state.view_height);
        goto cleanup_display;
    }
    if (fbink_dump(fbfd, &dump) != 0) {
        fprintf(stderr, "display: unable to save previous screen\n");
        goto cleanup_display;
    }
    dumped = true;
    config.is_cleared = true;
    config.no_refresh = true;
    config.scaled_width = -2;
    config.scaled_height = -2;
    config.halign = CENTER;
    config.valign = CENTER;
    if (fbink_print_raw_data(fbfd,
                             gray_ya,
                             (int)captured.width,
                             (int)captured.height,
                             gray_ya_size,
                             0,
                             0,
                             &config) != 0) {
        fprintf(stderr, "display: unable to draw captured frame\n");
        goto cleanup_display;
    }
    refresh_config.is_flashing = true;
    if (fbink_refresh(fbfd, 0U, 0U, 0U, 0U, &refresh_config) != 0) {
        fprintf(stderr, "display: explicit refresh failed\n");
        goto cleanup_display;
    }
    if (fbink_wait_for_complete(fbfd, LAST_MARKER) != 0) {
        fprintf(stderr, "display: warning: refresh completion wait failed\n");
    }
    printf("displayed viewport=%ux%u scale=fit refresh=full hold_seconds=%.3f\n",
           state.view_width,
           state.view_height,
           hold_seconds);
    if (hold_seconds == 0.0) {
        fprintf(stderr, "display: hold_seconds is zero; restoring immediately\n");
    } else {
        fprintf(stderr, "display: holding frame; send SIGINT to restore early\n");
    }
    wait_for_display(hold_seconds);
    exit_code = 0;

cleanup_display:
    if (fbfd >= 0) {
        if (restore_display(fbfd, &dump, dumped, &refresh_config) != 0) {
            exit_code = 1;
        }
        fbfd = FBFD_AUTO;
    }
cleanup:
    if (rom != NULL) {
        fclose(rom);
    }
    free(gray_ya);
    free_capture();
    free(rom_data);
    if (game_loaded) {
        retro_unload_game();
    }
    if (core_initialized) {
        retro_deinit();
    }
    if (handle != NULL) {
        dlclose(handle);
    }
    return exit_code;
}
