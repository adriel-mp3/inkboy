#define _POSIX_C_SOURCE 200809L

#include "input.h"
#include "libretro.h"
#include "profile.h"
#include "render.h"
#include "timing.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef INKBOY_CORE_PATH
#define INKBOY_CORE_PATH "/mnt/us/inkboy/sameboy_libretro.so"
#endif
#define INKBOY_ROM_PATH \
    "/mnt/us/games/Pokemon - Yellow Version - Special Pikachu Edition (USA, Europe) (GBC,SGB Enhanced).gb"
#define WARMUP_FRAMES 180U

static volatile sig_atomic_t graceful_exit_requested;
static volatile sig_atomic_t exit_signal_number;
static struct inkboy_profile_timer last_input_poll;
static bool poll_after_cleanup;

struct inkboy_runtime {
    struct inkboy_renderer *renderer;
    struct inkboy_libretro *emulator;
    struct inkboy_input *input;
};

struct gameplay_clock {
    struct timespec next_run;
    uint64_t next_display;
    uint64_t last_presentation_attempt;
    uint64_t display_period;
    double emulation_fps;
};

enum warmup_result {
    WARMUP_ERROR = -1,
    WARMUP_READY = 0,
    WARMUP_EXIT_REQUESTED = 1,
    WARMUP_GRACEFUL_EXIT_REQUESTED = 2
};

static void request_graceful_exit(int cause)
{
    exit_signal_number = cause;
    graceful_exit_requested = 1;
}

static int configure_graceful_exit(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = request_graceful_exit;
    if (sigemptyset(&action.sa_mask) != 0 ||
        sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGHUP, &action, NULL) != 0) {
        return -1;
    }
    return 0;
}

static void handle_input_event(const struct inkboy_input_event *event,
                               void *userdata)
{
    if (event != NULL) {
        inkboy_libretro_set_control(userdata, event->control,
                                    event->phase == INKBOY_INPUT_DOWN);
    }
}

static enum inkboy_input_result poll_input(struct inkboy_input *input)
{
    struct inkboy_profile_timer input_timer =
        inkboy_profile_timer_start(INKBOY_PROFILE_INPUT);
    if (last_input_poll.id != 0U) {
        inkboy_profile_timer_stop_as(last_input_poll, poll_after_cleanup ?
            INKBOY_PROFILE_INPUT_GAP_CLEAN : INKBOY_PROFILE_INPUT_GAP);
    }
    last_input_poll = inkboy_profile_timer_start(INKBOY_PROFILE_INPUT_GAP);
    poll_after_cleanup = false;
    enum inkboy_input_result result = inkboy_input_poll(input);

    inkboy_profile_timer_stop(input_timer);
    return result;
}

static void sleep_until(struct timespec deadline)
{
    struct timespec now;
    struct timespec remaining;

    if (graceful_exit_requested || clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
        !inkboy_timespec_before(now, deadline)) {
        return;
    }
    remaining.tv_sec = deadline.tv_sec - now.tv_sec;
    remaining.tv_nsec = deadline.tv_nsec - now.tv_nsec;
    if (remaining.tv_nsec < 0) {
        remaining.tv_sec--;
        remaining.tv_nsec += 1000000000L;
    }
    while (!graceful_exit_requested && nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

static const char *graceful_exit_reason(void)
{
    switch (exit_signal_number) {
    case SIGINT: return "SIGINT";
    case SIGTERM: return "SIGTERM";
    case SIGHUP: return "SIGHUP";
    default: return "signal";
    }
}

static int initialize_runtime(struct inkboy_runtime *runtime)
{
    fprintf(stdout, "runtime core=%s rom=\"%s\" warmup_frames=%u\n",
            INKBOY_CORE_PATH, INKBOY_ROM_PATH, WARMUP_FRAMES);
    if (configure_graceful_exit() != 0) {
        fprintf(stderr, "inkboy: unable to configure graceful exit: %s\n",
                strerror(errno));
        return -1;
    }
    if (inkboy_renderer_open(&runtime->renderer) != 0 ||
        inkboy_libretro_open(&runtime->emulator, INKBOY_CORE_PATH,
                             INKBOY_ROM_PATH) != 0 ||
        inkboy_input_open(&runtime->input,
                          inkboy_renderer_layout(runtime->renderer),
                          handle_input_event, runtime->emulator) != 0) {
        return -1;
    }
    return 0;
}

static enum warmup_result warmup_runtime(struct inkboy_runtime *runtime,
                                         struct gameplay_clock *clock,
                                         const char **reason)
{
    struct inkboy_video_frame frame;

    inkboy_profile_warmup();
    for (unsigned index = 0U; index < WARMUP_FRAMES && !graceful_exit_requested; index++) {
        enum inkboy_input_result input_result = poll_input(runtime->input);

        if (input_result == INKBOY_INPUT_STOP) {
            *reason = "EXIT";
            return WARMUP_EXIT_REQUESTED;
        }
        if (input_result == INKBOY_INPUT_ERROR) {
            *reason = "input-error";
            return WARMUP_ERROR;
        }
        if (graceful_exit_requested) {
            return WARMUP_GRACEFUL_EXIT_REQUESTED;
        }
        if (inkboy_libretro_run(runtime->emulator) != 0) {
            *reason = "emulation-error";
            return WARMUP_ERROR;
        }
    }
    if (graceful_exit_requested) {
        return WARMUP_GRACEFUL_EXIT_REQUESTED;
    }

    if (last_input_poll.id != 0U) {
        inkboy_profile_timer_stop(last_input_poll);
    }
    last_input_poll.id = 0U;
    inkboy_profile_begin_gameplay();
    *reason = "presentation-error";
    if (!inkboy_libretro_frame(runtime->emulator, &frame) ||
        inkboy_renderer_present_frame(runtime->renderer, &frame) < 0) {
        return WARMUP_ERROR;
    }

    *reason = "clock-error";
    if (clock_gettime(CLOCK_MONOTONIC, &clock->next_run) != 0) {
        return WARMUP_ERROR;
    }
    clock->last_presentation_attempt = inkboy_timespec_to_ns(&clock->next_run);
    clock->emulation_fps = inkboy_libretro_fps(runtime->emulator);
    clock->next_display = clock->last_presentation_attempt + clock->display_period;
    last_input_poll.id = 0U;
    return WARMUP_READY;
}

static int present_latest_frame(struct inkboy_runtime *runtime,
                                struct gameplay_clock *clock,
                                struct timespec *next_run,
                                struct timespec *now,
                                const char **reason)
{
    struct inkboy_video_frame frame;
    uint64_t present_start_ns = inkboy_timespec_to_ns(now);
    uint64_t interval_ns = present_start_ns - clock->last_presentation_attempt;
    uint64_t deadline_late_ns = present_start_ns - clock->next_display;
    enum inkboy_present_result presented = INKBOY_PRESENT_UNCHANGED;
    bool frame_available = inkboy_libretro_frame(runtime->emulator, &frame);
    uint64_t now_ns;

    if (frame_available) {
        presented = inkboy_renderer_present_frame(runtime->renderer, &frame);
        if (presented < 0) {
            *reason = "presentation-error";
            return -1;
        }
        poll_after_cleanup = presented == INKBOY_PRESENT_CLEANUP ||
                             presented == INKBOY_PRESENT_INITIAL;
    }
    if (clock_gettime(CLOCK_MONOTONIC, now) != 0) {
        *reason = "clock-error";
        return -1;
    }
    now_ns = inkboy_timespec_to_ns(now);
    fprintf(stdout, "runtime event=presentation result=%s"
            " since_previous_attempt_ms=%.3f deadline_late_ms=%.3f"
            " presentation_call_ms=%.3f\n",
            presented == INKBOY_PRESENT_INITIAL ? "initial_completed" :
            presented == INKBOY_PRESENT_PARTIAL ? "normal_submitted" :
            presented == INKBOY_PRESENT_CLEANUP ? "cleanup_completed" :
            frame_available ? "unchanged" : "no_frame",
            interval_ns / 1000000.0, deadline_late_ns / 1000000.0,
            (now_ns - present_start_ns) / 1000000.0);
    clock->last_presentation_attempt = present_start_ns;
    clock->next_display = inkboy_next_deadline(clock->next_display, now_ns,
                                                clock->display_period);
    /* Drop emulation debt too; no burst after a blocking refresh. */
    if (inkboy_timespec_before(*next_run, *now)) {
        *next_run = *now;
    }
    return 0;
}

static int run_game_loop(struct inkboy_runtime *runtime,
                         struct gameplay_clock *clock,
                         const char **reason)
{
    while (!graceful_exit_requested) {
        struct timespec now;
        enum inkboy_input_result input_result = poll_input(runtime->input);

        if (input_result == INKBOY_INPUT_STOP) {
            *reason = "EXIT";
            return EXIT_SUCCESS;
        }
        if (input_result == INKBOY_INPUT_ERROR) {
            *reason = "input-error";
            return EXIT_FAILURE;
        }
        if (graceful_exit_requested) {
            break;
        }
        if (inkboy_libretro_run(runtime->emulator) != 0) {
            *reason = "emulation-error";
            return EXIT_FAILURE;
        }
        clock->next_run = inkboy_timespec_add_seconds(
            clock->next_run, 1.0 / clock->emulation_fps);
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            *reason = "clock-error";
            return EXIT_FAILURE;
        }
        if (inkboy_timespec_before(clock->next_run, now)) {
            clock->next_run = now;
        } else {
            sleep_until(clock->next_run);
        }
        if (graceful_exit_requested) {
            break;
        }
        /* Sleep and synchronous presentation can both invalidate prior time. */
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            *reason = "clock-error";
            return EXIT_FAILURE;
        }
        if (inkboy_timespec_to_ns(&now) >= clock->next_display &&
            present_latest_frame(runtime, clock, &clock->next_run, &now,
                                 reason) != 0) {
            return EXIT_FAILURE;
        }
    }
    *reason = graceful_exit_reason();
    return EXIT_SUCCESS;
}

static int close_runtime(struct inkboy_runtime *runtime, int result,
                         const char *reason)
{
    inkboy_profile_finish(reason, result);
    fprintf(stdout, "cleanup stage=input_begin\n");
    inkboy_input_close(runtime->input);
    fprintf(stdout, "cleanup stage=input_end\ncleanup stage=core_begin\n");
    inkboy_libretro_close(runtime->emulator);
    fprintf(stdout, "cleanup stage=core_end\ncleanup stage=renderer_begin\n");
    if (inkboy_renderer_close(runtime->renderer) != 0) {
        result = EXIT_FAILURE;
    }
    fprintf(stdout, "cleanup stage=renderer_end result=%d\n"
            "runtime event=final reason=%s signal=%d status=%d\n",
            result, reason, (int)exit_signal_number, result);
    fflush(stdout);
    return result;
}

int main(void)
{
    struct inkboy_runtime runtime = {0};
    struct gameplay_clock clock = {0};
    const struct inkboy_presentation_policy policy = inkboy_presentation_policy();
    enum warmup_result warmup_result;
    const char *reason = "initialization-error";
    int result = EXIT_FAILURE;

    graceful_exit_requested = 0;
    exit_signal_number = 0;
    if (last_input_poll.id != 0U) inkboy_profile_timer_stop(last_input_poll);
    last_input_poll.id = 0U;
    poll_after_cleanup = false;
    inkboy_profile_startup();
    clock.display_period = UINT64_C(1000000000) / policy.fps;
    if (initialize_runtime(&runtime) == 0) {
        warmup_result = warmup_runtime(&runtime, &clock, &reason);
        if (warmup_result == WARMUP_READY) {
            result = run_game_loop(&runtime, &clock, &reason);
        } else if (warmup_result == WARMUP_EXIT_REQUESTED ||
                   warmup_result == WARMUP_GRACEFUL_EXIT_REQUESTED) {
            result = EXIT_SUCCESS;
            if (warmup_result == WARMUP_GRACEFUL_EXIT_REQUESTED) {
                reason = graceful_exit_reason();
            }
        }
    }
    return close_runtime(&runtime, result, reason);
}
