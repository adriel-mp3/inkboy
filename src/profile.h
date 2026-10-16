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
    INKBOY_PROFILE_REGION_PACK,
    INKBOY_PROFILE_DRAW,
    INKBOY_PROFILE_REFRESH_SUBMIT,
    INKBOY_PROFILE_REFRESH_WAIT,
    INKBOY_PROFILE_INPUT_TO_STATE,
    INKBOY_PROFILE_INITIAL_SUBMIT,
    INKBOY_PROFILE_INITIAL_WAIT,
    INKBOY_PROFILE_CLEAN_SUBMIT,
    INKBOY_PROFILE_CLEAN_WAIT,
    INKBOY_PROFILE_INPUT_GAP_CLEAN,
    INKBOY_PROFILE_STAGE_COUNT
};

struct inkboy_profile_sample {
    uint64_t total_ns;
    uint64_t max_ns;
    uint64_t last_ns;
    uint64_t count;
};

struct inkboy_profile {
    struct inkboy_profile_sample stages[INKBOY_PROFILE_STAGE_COUNT];
    uint64_t runs;
    uint64_t callbacks;
    uint64_t valid_frames;
    uint64_t duplicate_frames;
    uint64_t invalid_frames;
    uint64_t superseded_frames;
    uint64_t presentations;
    uint64_t initial_presentations;
    uint64_t normal_presentations;
    uint64_t cleanups;
    uint64_t unchanged_images;
    uint64_t refresh_attempts;
    uint64_t refresh_failures;
    uint64_t initial_refresh_failures;
    uint64_t normal_refresh_failures;
    uint64_t cleanup_refresh_failures;
};

extern struct inkboy_profile inkboy_profile;

uint64_t inkboy_profile_now(void);
void inkboy_profile_record(enum inkboy_profile_stage stage, uint64_t start);
void inkboy_profile_begin(const char *phase);
void inkboy_profile_report(const char *event, const char *reason, int status);

#endif
