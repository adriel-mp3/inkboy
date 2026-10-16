#define _POSIX_C_SOURCE 200809L

#include "libretro.h"
#include "profile.h"
#include "../third_party/sameboy/libretro/libretro.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

struct inkboy_libretro {
    void *handle;
    struct core_api api;
    void *rom_data;
    size_t rom_size;
    bool initialized;
    bool game_loaded;
    bool pixel_format_seen;
    bool pixel_format_ok;
    uint32_t *pixels;
    unsigned width;
    unsigned height;
    size_t pitch;
    bool frame_ready;
    bool frame_pending;
    uint64_t control_changed_at;
    double fps;
    uint16_t buttons;
};

static struct inkboy_libretro *active_emulator;

static void core_log(enum retro_log_level level, const char *format, ...)
{
    (void)level;
    (void)format;
}

static bool environment(unsigned command, void *data)
{
    if (active_emulator == NULL) {
        return false;
    }
    if (command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) {
        enum retro_pixel_format requested = *(enum retro_pixel_format *)data;

        active_emulator->pixel_format_seen = true;
        active_emulator->pixel_format_ok =
            requested == RETRO_PIXEL_FORMAT_XRGB8888;
        return active_emulator->pixel_format_ok;
    }
    if (command == RETRO_ENVIRONMENT_GET_LOG_INTERFACE) {
        struct retro_log_callback *callback = data;

        callback->log = core_log;
        return true;
    }
    if (command == RETRO_ENVIRONMENT_GET_CAN_DUPE) {
        *(bool *)data = true;
        return true;
    }
    return false;
}

static void video_refresh(const void *data, unsigned width, unsigned height,
                          size_t pitch)
{
    size_t row_bytes;
    size_t bytes;
    uint32_t *pixels;
    uint64_t start = inkboy_profile_now();

    inkboy_profile.callbacks++;
    if (data == NULL) {
        inkboy_profile.duplicate_frames++;
        return;
    }
    if (active_emulator == NULL || width == 0U ||
        height == 0U || width > 1024U || height > 1024U ||
        (size_t)height > SIZE_MAX / ((size_t)width * sizeof(uint32_t))) {
        inkboy_profile.invalid_frames++;
        return;
    }
    row_bytes = (size_t)width * sizeof(uint32_t);
    if (pitch < row_bytes || (size_t)height > SIZE_MAX / row_bytes) {
        inkboy_profile.invalid_frames++;
        return;
    }
    if (pitch > SIZE_MAX / height) {
        inkboy_profile.invalid_frames++;
        return;
    }
    bytes = (size_t)height * row_bytes;
    pixels = active_emulator->pixels;
    if (active_emulator->width != width || active_emulator->height != height) {
        pixels = malloc(bytes);
        if (pixels == NULL) {
            inkboy_profile.invalid_frames++;
            return;
        }
        free(active_emulator->pixels);
        active_emulator->pixels = pixels;
        active_emulator->width = width;
        active_emulator->height = height;
        active_emulator->pitch = row_bytes;
    }
    for (unsigned row = 0U; row < height; row++) {
        memcpy((unsigned char *)pixels + (size_t)row * row_bytes,
               (const unsigned char *)data + (size_t)row * pitch,
               row_bytes);
    }
    active_emulator->frame_ready = true;
    if (active_emulator->frame_pending) {
        inkboy_profile.superseded_frames++;
    }
    active_emulator->frame_pending = true;
    inkboy_profile.valid_frames++;
    inkboy_profile_record(INKBOY_PROFILE_CALLBACK_COPY, start);
}

static void audio_sample(int16_t left, int16_t right)
{
    (void)left;
    (void)right;
}

static size_t audio_batch(const int16_t *data, size_t frames)
{
    (void)data;
    return frames;
}

static void input_poll(void)
{
}

static int16_t input_state(unsigned port, unsigned device, unsigned index,
                           unsigned id)
{
    if (active_emulator == NULL || port != 0U ||
        (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD ||
        index != 0U || (id >= 16U && id != RETRO_DEVICE_ID_JOYPAD_MASK)) {
        return 0;
    }
    if (active_emulator->control_changed_at != 0U) {
        inkboy_profile_record(INKBOY_PROFILE_INPUT_TO_STATE,
                              active_emulator->control_changed_at);
        active_emulator->control_changed_at = 0U;
    }
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
        return (int16_t)active_emulator->buttons;
    }
    if (id >= 16U) {
        return 0;
    }
    return (active_emulator->buttons & (uint16_t)(1U << id)) != 0U;
}

static void *load_symbol(void *handle, const char *name)
{
    void *symbol = dlsym(handle, name);

    if (symbol == NULL) {
        fprintf(stderr, "inkboy: missing Libretro symbol %s\n", name);
    }
    return symbol;
}

static bool load_api(void *handle, struct core_api *api)
{
#define LOAD(field, type, name) \
    do { \
        void *symbol = load_symbol(handle, name); \
        if (symbol == NULL) return false; \
        memcpy(&api->field, &symbol, sizeof(api->field)); \
    } while (0)
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

static int read_rom(struct inkboy_libretro *emulator, const char *path)
{
    FILE *file = NULL;
    long size;

    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "inkboy: unable to open ROM %s: %s\n", path,
                strerror(errno));
        if (file != NULL) {
            fclose(file);
        }
        return -1;
    }
    emulator->rom_data = malloc((size_t)size);
    if (emulator->rom_data == NULL ||
        fread(emulator->rom_data, 1U, (size_t)size, file) != (size_t)size) {
        fprintf(stderr, "inkboy: unable to read ROM %s\n", path);
        fclose(file);
        free(emulator->rom_data);
        emulator->rom_data = NULL;
        return -1;
    }
    emulator->rom_size = (size_t)size;
    fclose(file);
    return 0;
}

int inkboy_libretro_open(struct inkboy_libretro **emulator_out,
                         const char *core_path,
                         const char *rom_path)
{
    struct inkboy_libretro *emulator;
    struct retro_game_info game;
    struct retro_system_av_info av_info = {0};

    if (emulator_out == NULL || core_path == NULL || rom_path == NULL) {
        return -1;
    }
    *emulator_out = NULL;
    emulator = calloc(1U, sizeof(*emulator));
    if (emulator == NULL || read_rom(emulator, rom_path) != 0) {
        free(emulator);
        return -1;
    }
    emulator->handle = dlopen(core_path, RTLD_NOW | RTLD_LOCAL);
    if (emulator->handle == NULL || !load_api(emulator->handle, &emulator->api)) {
        fprintf(stderr, "inkboy: unable to load core %s: %s\n", core_path,
                dlerror());
        inkboy_libretro_close(emulator);
        return -1;
    }
    active_emulator = emulator;
    emulator->api.set_environment(environment);
    emulator->api.set_video_refresh(video_refresh);
    emulator->api.set_audio_sample(audio_sample);
    emulator->api.set_audio_batch(audio_batch);
    emulator->api.set_input_poll(input_poll);
    emulator->api.set_input_state(input_state);
    emulator->api.init();
    emulator->initialized = true;
    memset(&game, 0, sizeof(game));
    game.path = rom_path;
    game.data = emulator->rom_data;
    game.size = emulator->rom_size;
    if (!emulator->api.load_game(&game)) {
        fprintf(stderr, "inkboy: retro_load_game failed\n");
        inkboy_libretro_close(emulator);
        return -1;
    }
    emulator->game_loaded = true;
    emulator->api.get_system_av_info(&av_info);
    emulator->fps = av_info.timing.fps > 1.0 && av_info.timing.fps < 1000.0
                        ? av_info.timing.fps : 60.0;
    if (!emulator->pixel_format_seen || !emulator->pixel_format_ok) {
        fprintf(stderr, "inkboy: core did not negotiate XRGB8888\n");
        inkboy_libretro_close(emulator);
        return -1;
    }
    *emulator_out = emulator;
    return 0;
}

int inkboy_libretro_run(struct inkboy_libretro *emulator)
{
    if (emulator == NULL || !emulator->game_loaded) {
        return -1;
    }
    emulator->frame_ready = false;
    uint64_t start = inkboy_profile_now();
    emulator->api.run();
    inkboy_profile.runs++;
    inkboy_profile_record(INKBOY_PROFILE_EMULATION, start);
    return 0;
}

bool inkboy_libretro_frame(struct inkboy_libretro *emulator,
                           struct inkboy_video_frame *frame)
{
    if (emulator == NULL || frame == NULL || !emulator->frame_ready) {
        return false;
    }
    frame->pixels = emulator->pixels;
    frame->width = emulator->width;
    frame->height = emulator->height;
    frame->pitch = emulator->pitch;
    emulator->frame_pending = false;
    return true;
}

double inkboy_libretro_fps(const struct inkboy_libretro *emulator)
{
    return emulator == NULL ? 60.0 : emulator->fps;
}

void inkboy_libretro_set_control(struct inkboy_libretro *emulator,
                                 enum inkboy_control control,
                                 bool pressed)
{
    int id = inkboy_libretro_control_id(control);

    if (emulator == NULL || id < 0) {
        return;
    }
    if (((emulator->buttons & (uint16_t)(1U << (unsigned)id)) != 0U) == pressed) {
        return;
    }
    if (emulator->control_changed_at == 0U) {
        emulator->control_changed_at = inkboy_profile_now();
    }
    if (pressed) {
        emulator->buttons |= (uint16_t)(1U << (unsigned)id);
    } else {
        emulator->buttons &= (uint16_t)~(1U << (unsigned)id);
    }
}

int inkboy_libretro_control_id(enum inkboy_control control)
{
    switch (control) {
    case INKBOY_CONTROL_UP: return RETRO_DEVICE_ID_JOYPAD_UP;
    case INKBOY_CONTROL_DOWN: return RETRO_DEVICE_ID_JOYPAD_DOWN;
    case INKBOY_CONTROL_LEFT: return RETRO_DEVICE_ID_JOYPAD_LEFT;
    case INKBOY_CONTROL_RIGHT: return RETRO_DEVICE_ID_JOYPAD_RIGHT;
    case INKBOY_CONTROL_A: return RETRO_DEVICE_ID_JOYPAD_A;
    case INKBOY_CONTROL_B: return RETRO_DEVICE_ID_JOYPAD_B;
    case INKBOY_CONTROL_START: return RETRO_DEVICE_ID_JOYPAD_START;
    case INKBOY_CONTROL_SELECT: return RETRO_DEVICE_ID_JOYPAD_SELECT;
    default: return -1;
    }
}

void inkboy_libretro_close(struct inkboy_libretro *emulator)
{
    if (emulator == NULL) {
        return;
    }
    if (emulator->game_loaded) {
        emulator->api.unload_game();
    }
    if (emulator->initialized) {
        emulator->api.deinit();
    }
    if (active_emulator == emulator) {
        active_emulator = NULL;
    }
    free(emulator->pixels);
    free(emulator->rom_data);
    if (emulator->handle != NULL) {
        dlclose(emulator->handle);
    }
    free(emulator);
}
