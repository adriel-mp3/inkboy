#include "timing.h"

bool inkboy_timespec_before(struct timespec left, struct timespec right)
{
    return left.tv_sec < right.tv_sec ||
           (left.tv_sec == right.tv_sec && left.tv_nsec < right.tv_nsec);
}

struct timespec inkboy_timespec_add_seconds(struct timespec value,
                                            double seconds)
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

uint64_t inkboy_timespec_to_ns(const struct timespec *value)
{
    return (uint64_t)value->tv_sec * UINT64_C(1000000000) +
           (uint64_t)value->tv_nsec;
}
