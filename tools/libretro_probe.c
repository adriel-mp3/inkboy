#define _POSIX_C_SOURCE 200809L
#include "libretro.h"

#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

typedef void (*fn_init)(void); typedef void (*fn_deinit)(void);
typedef unsigned (*fn_api)(void); typedef void (*fn_void_cb)(void *);
typedef void (*fn_run)(void); typedef bool (*fn_load)(const struct retro_game_info *);
typedef void (*fn_unload)(void); typedef void (*fn_get_system_info)(struct retro_system_info *);
typedef void (*fn_get_av_info)(struct retro_system_av_info *);

static unsigned long long frames;
static enum retro_pixel_format negotiated = RETRO_PIXEL_FORMAT_UNKNOWN;
static bool pixel_format_seen, pixel_format_ok;
static unsigned video_width, video_height;
static size_t video_pitch;

static void log_cb(enum retro_log_level level, const char *fmt, ...) {
    (void)level; (void)fmt;
}
static bool environment(unsigned cmd, void *data) {
    if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) {
        enum retro_pixel_format requested = *(enum retro_pixel_format *)data;
        pixel_format_seen = true;
        pixel_format_ok = requested == RETRO_PIXEL_FORMAT_XRGB8888;
        if (pixel_format_ok) negotiated = requested;
        fprintf(stderr, "environment: SET_PIXEL_FORMAT requested=%d %s\n", requested,
                pixel_format_ok ? "accepted" : "rejected");
        return pixel_format_ok;
    }
    if (cmd == RETRO_ENVIRONMENT_GET_LOG_INTERFACE) {
        struct retro_log_callback *cb = data; cb->log = log_cb; return true;
    }
    if (cmd == RETRO_ENVIRONMENT_GET_CAN_DUPE) { *(bool *)data = true; return true; }
    return false;
}
static void video(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (data) (void)((const unsigned char *)data)[0];
    video_width = width;
    video_height = height;
    video_pitch = pitch;
    frames++;
    if (frames == 1) fprintf(stderr, "video: width=%u height=%u pitch=%zu\n", width, height, pitch);
}
static void audio_sample(int16_t left, int16_t right) { (void)left; (void)right; }
static size_t audio_batch(const int16_t *data, size_t frames_in) { (void)data; return frames_in; }
static void input_poll(void) {}
static int16_t input_state(unsigned p, unsigned d, unsigned i, unsigned id) {
    (void)p; (void)d; (void)i; (void)id; return 0;
}
static uint64_t rss_kib(void) {
    struct rusage r; if (getrusage(RUSAGE_SELF, &r) != 0) return 0;
    return (uint64_t)r.ru_maxrss;
}
static void *symbol(void *h, const char *name) {
    void *p = dlsym(h, name); if (!p) fprintf(stderr, "missing symbol: %s\n", name); return p;
}
static int usage(const char *p) { fprintf(stderr, "usage: %s CORE ROM [frames]\n", p); return 2; }

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) return usage(argv[0]);
    char *end = NULL; unsigned long limit = 60;
    if (argc == 4) { errno = 0; limit = strtoul(argv[3], &end, 10); if (errno || !end || *end || limit == 0) { fprintf(stderr, "invalid frames: %s\n", argv[3]); return 2; } }
    const char *core_path = argv[1], *rom_path = argv[2];
    FILE *rom = fopen(rom_path, "rb"); if (!rom) { fprintf(stderr, "ROM: %s: %s\n", rom_path, strerror(errno)); return 2; }
    if (fseek(rom, 0, SEEK_END) != 0) { fclose(rom); return 2; }
    long size = ftell(rom); if (size <= 0 || fseek(rom, 0, SEEK_SET) != 0) { fclose(rom); fprintf(stderr, "invalid ROM size\n"); return 2; }
    void *rom_data = malloc((size_t)size); if (!rom_data || fread(rom_data, 1, (size_t)size, rom) != (size_t)size) { fclose(rom); free(rom_data); fprintf(stderr, "cannot read ROM\n"); return 2; } fclose(rom);
    void *handle = dlopen(core_path, RTLD_NOW | RTLD_LOCAL); if (!handle) { fprintf(stderr, "core: %s\n", dlerror()); free(rom_data); return 2; }
#define LOAD(n,t) t n = (t)symbol(handle, #n); if (!n) goto fail
    LOAD(retro_api_version, fn_api); LOAD(retro_set_environment, fn_void_cb); LOAD(retro_set_video_refresh, fn_void_cb); LOAD(retro_set_audio_sample, fn_void_cb); LOAD(retro_set_audio_sample_batch, fn_void_cb); LOAD(retro_set_input_poll, fn_void_cb); LOAD(retro_set_input_state, fn_void_cb); LOAD(retro_init, fn_init); LOAD(retro_deinit, fn_deinit); LOAD(retro_run, fn_run); LOAD(retro_load_game, fn_load); LOAD(retro_unload_game, fn_unload); LOAD(retro_get_system_info, fn_get_system_info); LOAD(retro_get_system_av_info, fn_get_av_info);
#undef LOAD
    struct retro_system_info info = {0}; retro_get_system_info(&info);
    printf("api=%u name=%s version=%s\n", retro_api_version(), info.library_name ?: "?", info.library_version ?: "?");
    retro_set_environment((void *)environment); retro_set_video_refresh((void *)video); retro_set_audio_sample((void *)audio_sample); retro_set_audio_sample_batch((void *)audio_batch); retro_set_input_poll((void *)input_poll); retro_set_input_state((void *)input_state); retro_init();
    struct retro_game_info game = { .path = rom_path, .data = rom_data, .size = (size_t)size, .meta = NULL };
    if (!retro_load_game(&game)) { fprintf(stderr, "retro_load_game failed\n"); retro_deinit(); goto fail; }
    struct retro_system_av_info av = {0}; retro_get_system_av_info(&av);
    struct timespec start, finish; clock_gettime(CLOCK_MONOTONIC, &start);
    for (unsigned long i = 0; i < limit; i++) retro_run();
    clock_gettime(CLOCK_MONOTONIC, &finish); retro_unload_game(); retro_deinit();
    double elapsed = (finish.tv_sec - start.tv_sec) + (finish.tv_nsec - start.tv_nsec) / 1e9;
    printf("format=%s dimensions=%ux%u pitch=%zu frames=%llu seconds=%.6f rss_kib=%" PRIu64 " status=ok\n", negotiated == RETRO_PIXEL_FORMAT_XRGB8888 ? "XRGB8888" : "unknown", video_width ? video_width : av.geometry.base_width, video_height ? video_height : av.geometry.base_height, video_pitch, frames, elapsed, rss_kib());
    if (!pixel_format_seen || !pixel_format_ok) fprintf(stderr, "format negotiation: XRGB8888 was not confirmed\n");
    free(rom_data); dlclose(handle); return (!pixel_format_seen || !pixel_format_ok) ? 1 : 0;
fail: free(rom_data); dlclose(handle); return 2;
}
