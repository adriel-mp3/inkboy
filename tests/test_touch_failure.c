#define _GNU_SOURCE

#include <assert.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool match_touch;
static struct timespec fake_time = {100, 0};

static int fake_clock_gettime(clockid_t clock_id, struct timespec *value)
{
    assert(clock_id == CLOCK_MONOTONIC);
    *value = fake_time;
    return 0;
}

static ssize_t fake_readlink(const char *path, char *output, size_t capacity)
{
    const char *target = match_touch ? "/dev/input/event7" : "pipe:[123]";
    size_t length = strlen(target);

    (void)path;
    assert(length <= capacity);
    memcpy(output, target, length);
    return (ssize_t)length;
}

/* Exercise the diagnostic's actual warmup gate and monotonic deadline while
 * delegating non-injected calls to libc poll on a real pipe. */
#define poll injected_poll
#define clock_gettime fake_clock_gettime
#define readlink fake_readlink
#include "../tools/touch_failure.c"
#undef poll
#undef clock_gettime
#undef readlink

int main(void)
{
    int descriptors[2];

    assert(pipe(descriptors) == 0);
    struct pollfd descriptor = {.fd = descriptors[0], .events = POLLIN};
    assert(injected_poll(&descriptor, 1U, 0) == 0);
    match_touch = true;
    for (unsigned index = 0U; index < 180U; index++) {
        assert(injected_poll(&descriptor, 1U, 0) == 0);
    }
    /* First gameplay poll starts the timer, irrespective of warmup duration. */
    fake_time.tv_sec = 200;
    assert(injected_poll(&descriptor, 1U, 0) == 0);
    fake_time = (struct timespec){204, 999999999L};
    assert(injected_poll(&descriptor, 1U, 0) == 0);
    fake_time = (struct timespec){205, 0};
    assert(injected_poll(&descriptor, 1U, 0) == 1);
    assert(descriptor.revents == POLLHUP);
    match_touch = false;
    assert(injected_poll(&descriptor, 1U, 0) == 0);
    assert(close(descriptors[0]) == 0 && close(descriptors[1]) == 0);
    puts("touch failure injection: warmup gate, deadline and descriptor filtering pass");
    return 0;
}
