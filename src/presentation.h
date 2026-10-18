#ifndef INKBOY_PRESENTATION_H
#define INKBOY_PRESENTATION_H

#include <stdbool.h>
#include <stdint.h>

struct inkboy_presentation_policy {
    unsigned fps;
    uint64_t cleanup_ns;
};

enum inkboy_present_result {
    INKBOY_PRESENT_ERROR = -1,
    INKBOY_PRESENT_INITIAL,
    INKBOY_PRESENT_PARTIAL,
    INKBOY_PRESENT_CLEANUP,
    INKBOY_PRESENT_UNCHANGED
};

/* Fixed product policy. Change only with a new measured display decision. */
static inline struct inkboy_presentation_policy inkboy_presentation_policy(void)
{
    return (struct inkboy_presentation_policy){2U, UINT64_C(60000000000)};
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
