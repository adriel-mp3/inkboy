#define _POSIX_C_SOURCE 200809L

#include "input.h"
#include "libretro.h"
#include "profile.h"
#include "render.h"

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

static volatile sig_atomic_t stop_signal;
static struct inkboy_profile_timer last_input_poll;
static bool poll_after_cleanup;

static uint64_t monotonic_ns(const struct timespec *value)
{
    return (uint64_t)value->tv_sec * UINT64_C(1000000000) +
           (uint64_t)value->tv_nsec;
}

static void request_stop(int signal_number)
{
    stop_signal = signal_number;
}

static int install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    if (sigemptyset(&action.sa_mask) != 0 ||
        sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGHUP, &action, NULL) != 0) {
        return -1;
    }
    return 0;
}

static struct timespec add_seconds(struct timespec value, double seconds)
{
    time_t whole_seconds = (time_t)seconds;
    long nanoseconds = (long)((seconds - (double)whole_seconds) * 1000000000.0);

    value.tv_sec += whole_seconds;
    value.tv_nsec += nanoseconds;
    if (value.tv_nsec >= 1000000000L) {
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

    if (stop_signal || clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
        !before(now, deadline)) {
        return;
    }
    remaining.tv_sec = deadline.tv_sec - now.tv_sec;
    remaining.tv_nsec = deadline.tv_nsec - now.tv_nsec;
    if (remaining.tv_nsec < 0) {
        remaining.tv_sec--;
        remaining.tv_nsec += 1000000000L;
    }
    while (!stop_signal && nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

static const char *signal_reason(void)
{
    switch (stop_signal) {
    case SIGINT: return "SIGINT";
    case SIGTERM: return "SIGTERM";
    case SIGHUP: return "SIGHUP";
    default: return "signal";
    }
}

int main(void)
{
    struct inkboy_renderer *renderer = NULL;
    struct inkboy_libretro *emulator = NULL;
    struct inkboy_input *input = NULL;
    struct inkboy_video_frame frame;
    struct timespec next_run;
    uint64_t next_display;
    uint64_t last_presentation_attempt = 0U;
    const struct inkboy_presentation_policy policy = inkboy_presentation_policy();
    const uint64_t display_period = UINT64_C(1000000000) / policy.fps;
    double emulation_fps;
    const char *reason = "initialization-error";
    int result = EXIT_FAILURE;

    stop_signal = 0;
    if (last_input_poll.id != 0U) inkboy_profile_timer_stop(last_input_poll);
    last_input_poll.id = 0U;
    poll_after_cleanup = false;
    inkboy_profile_startup();
    fprintf(stdout, "runtime core=%s rom=\"%s\" warmup_frames=%u\n",
            INKBOY_CORE_PATH, INKBOY_ROM_PATH, WARMUP_FRAMES);
    if (install_signal_handlers() != 0) {
        fprintf(stderr, "inkboy: unable to install signal handlers: %s\n",
                strerror(errno));
        goto cleanup;
    }
    if (inkboy_renderer_open(&renderer) != 0 ||
        inkboy_libretro_open(&emulator, INKBOY_CORE_PATH, INKBOY_ROM_PATH) != 0 ||
        inkboy_input_open(&input, inkboy_renderer_layout(renderer),
                          handle_input_event, emulator) != 0) {
        goto cleanup;
    }

    inkboy_profile_warmup();
    for (unsigned index = 0U; index < WARMUP_FRAMES && !stop_signal; index++) {
        enum inkboy_input_result input_result = poll_input(input);

        if (input_result == INKBOY_INPUT_STOP) {
            reason = "EXIT";
            result = EXIT_SUCCESS;
            goto cleanup;
        }
        if (input_result == INKBOY_INPUT_ERROR) {
            reason = "input-error";
            goto cleanup;
        }
        if (stop_signal) {
            goto signaled;
        }
        if (inkboy_libretro_run(emulator) != 0) {
            reason = "emulation-error";
            goto cleanup;
        }
    }
    if (stop_signal) {
        goto signaled;
    }
    if (last_input_poll.id != 0U) inkboy_profile_timer_stop(last_input_poll);
    last_input_poll.id = 0U;
    inkboy_profile_begin_gameplay();
    reason = "presentation-error";
    if (!inkboy_libretro_frame(emulator, &frame) ||
        inkboy_renderer_present_frame(renderer, &frame) < 0) {
        goto cleanup;
    }
    reason = "clock-error";
    if (clock_gettime(CLOCK_MONOTONIC, &next_run) != 0) {
        goto cleanup;
    }
    last_presentation_attempt = monotonic_ns(&next_run);
    emulation_fps = inkboy_libretro_fps(emulator);
    next_display = (uint64_t)next_run.tv_sec * UINT64_C(1000000000) +
                   (uint64_t)next_run.tv_nsec + display_period;
    last_input_poll.id = 0U;
    while (!stop_signal) {
        struct timespec now;
        enum inkboy_input_result input_result = poll_input(input);

        if (input_result == INKBOY_INPUT_STOP) {
            reason = "EXIT";
            result = EXIT_SUCCESS;
            goto cleanup;
        }
        if (input_result == INKBOY_INPUT_ERROR) {
            reason = "input-error";
            goto cleanup;
        }
        if (stop_signal) {
            break;
        }
        if (inkboy_libretro_run(emulator) != 0) {
            reason = "emulation-error";
            goto cleanup;
        }
        next_run = add_seconds(next_run, 1.0 / emulation_fps);
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            reason = "clock-error";
            goto cleanup;
        }
        if (before(next_run, now)) {
            next_run = now;
        } else {
            sleep_until(next_run);
        }
        if (stop_signal) {
            break;
        }
        /* Sleep and synchronous presentation can both invalidate prior time. */
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            reason = "clock-error";
            goto cleanup;
        }
        uint64_t now_ns = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
        if (now_ns >= next_display) {
            uint64_t present_start_ns = now_ns;
            uint64_t interval_ns = present_start_ns - last_presentation_attempt;
            uint64_t deadline_late_ns = present_start_ns - next_display;
            enum inkboy_present_result presented = INKBOY_PRESENT_UNCHANGED;
            bool frame_available = inkboy_libretro_frame(emulator, &frame);
            if (frame_available) {
                presented = inkboy_renderer_present_frame(renderer, &frame);
                if (presented < 0) {
                    reason = "presentation-error";
                    goto cleanup;
                }
                poll_after_cleanup = presented == INKBOY_PRESENT_CLEANUP ||
                                     presented == INKBOY_PRESENT_INITIAL;
            }
            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
                reason = "clock-error";
                goto cleanup;
            }
            now_ns = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
            fprintf(stdout, "runtime event=presentation result=%s"
                    " since_previous_attempt_ms=%.3f deadline_late_ms=%.3f"
                    " presentation_call_ms=%.3f\n",
                    presented == INKBOY_PRESENT_INITIAL ? "initial_completed" :
                    presented == INKBOY_PRESENT_PARTIAL ?
                        "normal_submitted" :
                    presented == INKBOY_PRESENT_CLEANUP ? "cleanup_completed" :
                    frame_available ? "unchanged" : "no_frame",
                    interval_ns / 1000000.0, deadline_late_ns / 1000000.0,
                    (now_ns - present_start_ns) / 1000000.0);
            last_presentation_attempt = present_start_ns;
            next_display = inkboy_next_deadline(next_display, now_ns, display_period);
            /* Drop emulation debt too; no burst after a blocking refresh. */
            if (before(next_run, now)) next_run = now;
        }
    }

signaled:
    reason = signal_reason();
    result = EXIT_SUCCESS;
cleanup:
    inkboy_profile_finish(reason, result);
    fprintf(stdout, "cleanup stage=input_begin\n");
    inkboy_input_close(input);
    fprintf(stdout, "cleanup stage=input_end\ncleanup stage=core_begin\n");
    inkboy_libretro_close(emulator);
    fprintf(stdout, "cleanup stage=core_end\ncleanup stage=renderer_begin\n");
    if (inkboy_renderer_close(renderer) != 0) {
        result = EXIT_FAILURE;
    }
    fprintf(stdout, "cleanup stage=renderer_end result=%d\n"
            "runtime event=final reason=%s signal=%d status=%d\n",
            result, reason, (int)stop_signal, result);
    fflush(stdout);
    return result;
}
