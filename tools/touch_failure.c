/* TASK-009 test-only preload: skip the fixed 180 warmup polls, then report
 * POLLHUP after five seconds of gameplay touch polling. No driver unbind,
 * device removal, EVIOCGRAB or global changes. */
#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int poll(struct pollfd *fds, nfds_t count, int timeout)
{
    static int (*real_poll)(struct pollfd *, nfds_t, int);
    static struct timespec started;
    static bool timer_started;
    static unsigned warmup_polls;
    struct timespec now;
    char path[64];
    char target[256];
    ssize_t length;

    if (real_poll == NULL) {
        void *symbol = dlsym(RTLD_NEXT, "poll");
        if (symbol == NULL) {
            errno = ENOSYS;
            return -1;
        }
        memcpy(&real_poll, &symbol, sizeof(real_poll));
    }
    if (count == 1U && timeout == 0 && clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
        (void)snprintf(path, sizeof(path), "/proc/self/fd/%d", fds[0].fd);
        length = readlink(path, target, sizeof(target) - 1U);
        if (length > 0) {
            target[length] = '\0';
            if (strncmp(target, "/dev/input/event", 16U) == 0) {
                /* Match main.c's fixed WARMUP_FRAMES. The next poll happens
                 * after the first frame has been displayed and dumped. */
                if (warmup_polls < 180U) {
                    warmup_polls++;
                    return real_poll(fds, count, timeout);
                }
                if (!timer_started) {
                    started = now;
                    timer_started = true;
                }
                if (now.tv_sec - started.tv_sec > 5 ||
                    (now.tv_sec - started.tv_sec == 5 && now.tv_nsec >= started.tv_nsec)) {
                    fprintf(stderr, "fault source=touch-failure.so event=POLLHUP fd=%d\n",
                            fds[0].fd);
                    fds[0].revents = POLLHUP;
                    return 1;
                }
            }
        }
    }
    return real_poll(fds, count, timeout);
}
