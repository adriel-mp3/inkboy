#ifndef INKBOY_PROFILE_H
#define INKBOY_PROFILE_H

#include <stdint.h>

/* Single-threaded application metrics. Timings may nest: callback copy is
 * included in emulation; convert and scale are excluded from composition. */
enum inkboy_profile_stage {
    INKBOY_PROFILE_EMULATION,
    INKBOY_PROFILE_INPUT,
    INKBOY_PROFILE_INPUT_GAP,
    INKBOY_PROFILE_CALLBACK_COPY,
    INKBOY_PROFILE_CONVERT,
    INKBOY_PROFILE_SCALE,
    INKBOY_PROFILE_COMPOSE,
    INKBOY_PROFILE_DRAW,
    INKBOY_PROFILE_REFRESH_SUBMIT,
    INKBOY_PROFILE_REFRESH_WAIT,
    INKBOY_PROFILE_INPUT_TO_STATE,
    INKBOY_PROFILE_INITIAL_SUBMIT,
    INKBOY_PROFILE_INITIAL_WAIT,
    INKBOY_PROFILE_CLEAN_SUBMIT,
    INKBOY_PROFILE_CLEAN_WAIT,
    INKBOY_PROFILE_INPUT_GAP_CLEAN,
    INKBOY_PROFILE_RESTORE,
    INKBOY_PROFILE_RESTORE_WAIT,
    INKBOY_PROFILE_STAGE_COUNT
};

struct inkboy_profile_sample {
    uint64_t total_ns;
    uint64_t max_ns;
    uint64_t last_ns;
    uint64_t count;
};

enum inkboy_profile_counter {
    INKBOY_PROFILE_RUNS,
    INKBOY_PROFILE_CALLBACKS,
    INKBOY_PROFILE_VALID_FRAMES,
    INKBOY_PROFILE_DUPLICATE_FRAMES,
    INKBOY_PROFILE_INVALID_FRAMES,
    INKBOY_PROFILE_SUPERSEDED_FRAMES,
    INKBOY_PROFILE_PRESENTATIONS,
    INKBOY_PROFILE_INITIAL_PRESENTATIONS,
    INKBOY_PROFILE_NORMAL_PRESENTATIONS,
    INKBOY_PROFILE_CLEANUPS,
    INKBOY_PROFILE_UNCHANGED_IMAGES,
    INKBOY_PROFILE_REFRESH_ATTEMPTS,
    INKBOY_PROFILE_REFRESH_FAILURES,
    INKBOY_PROFILE_INITIAL_REFRESH_FAILURES,
    INKBOY_PROFILE_NORMAL_REFRESH_FAILURES,
    INKBOY_PROFILE_CLEANUP_REFRESH_FAILURES,
    INKBOY_PROFILE_COUNTER_COUNT
};

struct inkboy_profile_timer {
    uint64_t id;
};

enum inkboy_profile_frame_result { INKBOY_PROFILE_FRAME_VALID, INKBOY_PROFILE_FRAME_DUPLICATE,
                                   INKBOY_PROFILE_FRAME_INVALID, INKBOY_PROFILE_FRAME_SUPERSEDED };
enum inkboy_profile_presentation { INKBOY_PROFILE_PRESENT_INITIAL,
                                   INKBOY_PROFILE_PRESENT_NORMAL,
                                   INKBOY_PROFILE_PRESENT_CLEANUP };

uint64_t inkboy_profile_now(void);
struct inkboy_profile_timer inkboy_profile_timer_start(enum inkboy_profile_stage stage);
void inkboy_profile_timer_stop(struct inkboy_profile_timer timer);
void inkboy_profile_timer_cancel(struct inkboy_profile_timer timer);
void inkboy_profile_timer_stop_as(struct inkboy_profile_timer timer,
                                  enum inkboy_profile_stage stage);
uint64_t inkboy_profile_timer_elapsed_ns(struct inkboy_profile_timer timer);
void inkboy_profile_frame_received(enum inkboy_profile_frame_result result);
void inkboy_profile_frame_superseded(void);
void inkboy_profile_emulation_ran(void);
void inkboy_profile_refresh_attempted(void);
void inkboy_profile_refresh_failed(enum inkboy_profile_presentation kind);
void inkboy_profile_frame_presented(enum inkboy_profile_presentation kind);
void inkboy_profile_image_unchanged(void);
void inkboy_profile_startup(void);
void inkboy_profile_warmup(void);
void inkboy_profile_begin_gameplay(void);
void inkboy_profile_finish(const char *reason, int status);
uint64_t inkboy_profile_counter_value(enum inkboy_profile_counter counter);
struct inkboy_profile_sample inkboy_profile_stage_value(enum inkboy_profile_stage stage);
void inkboy_profile_begin(const char *phase);
void inkboy_profile_report(const char *event, const char *reason, int status);

#endif
