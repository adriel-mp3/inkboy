#ifndef INKBOY_TIMING_H
#define INKBOY_TIMING_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

bool inkboy_timespec_before(struct timespec left, struct timespec right);
struct timespec inkboy_timespec_add_seconds(struct timespec value,
                                            double seconds);
uint64_t inkboy_timespec_to_ns(const struct timespec *value);

#endif
