#ifndef INKBOY_PRESENTATION_H
#define INKBOY_PRESENTATION_H

#include <stdbool.h>
#include <stdint.h>

enum inkboy_refresh_mode {
    INKBOY_REFRESH_FULL_FLASH,
    INKBOY_REFRESH_REGION_AUTO,
    INKBOY_REFRESH_REGION_GC16
};

struct inkboy_presentation_policy {
    unsigned fps;
    enum inkboy_refresh_mode mode;
    uint64_t cleanup_ns;
};

enum inkboy_present_result {
    INKBOY_PRESENT_ERROR = -1,
    INKBOY_PRESENT_INITIAL,
    INKBOY_PRESENT_PARTIAL,
    INKBOY_PRESENT_CLEANUP,
    INKBOY_PRESENT_UNCHANGED
};

/* Experimental settings are build profiles, never runtime UI options. */
static inline struct inkboy_presentation_policy inkboy_presentation_policy(void)
{
#ifdef INKBOY_EXPERIMENT_FPS
    _Static_assert(INKBOY_EXPERIMENT_FPS == 1 || INKBOY_EXPERIMENT_FPS == 2 ||
                   INKBOY_EXPERIMENT_FPS == 4 || INKBOY_EXPERIMENT_FPS == 8,
                   "unsupported presentation cadence");
    _Static_assert(INKBOY_EXPERIMENT_MODE == INKBOY_REFRESH_REGION_AUTO ||
                   INKBOY_EXPERIMENT_MODE == INKBOY_REFRESH_REGION_GC16,
                   "unsupported experimental refresh mode");
    _Static_assert(INKBOY_EXPERIMENT_CLEAN_SECONDS == 30 ||
                   INKBOY_EXPERIMENT_CLEAN_SECONDS == 60,
                   "unsupported cleanup interval");
    return (struct inkboy_presentation_policy){INKBOY_EXPERIMENT_FPS,
        INKBOY_EXPERIMENT_MODE,
        UINT64_C(1000000000) * INKBOY_EXPERIMENT_CLEAN_SECONDS};
#else
    return (struct inkboy_presentation_policy){2U, INKBOY_REFRESH_REGION_GC16,
        UINT64_C(60000000000)};
#endif
}

/* Keep the cadence's phase, discard expired slots, and never queue catch-up
 * presentations. Call with a fresh monotonic timestamp after blocking work. */
static inline uint64_t inkboy_next_deadline(uint64_t deadline, uint64_t now,
                                            uint64_t period)
{
    return deadline > now ? deadline
        : deadline + ((now - deadline) / period + 1U) * period;
}

#endif
