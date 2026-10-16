/* Observe advertised KEY_POWER evdev sources without grabbing any device. */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define MAX_SOURCES 32U
#define BITS_PER_LONG (sizeof(unsigned long) * 8U)

static volatile sig_atomic_t stopped;

static void stop(int signal_number)
{
    stopped = signal_number;
}

int main(void)
{
    struct pollfd sources[MAX_SOURCES];
    nfds_t count = 0U;
    DIR *directory = opendir("/dev/input");
    struct dirent *entry;
    struct sigaction action = {0};
    struct timespec start;
    int result = 0;

    if (directory == NULL || clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        if (directory != NULL) closedir(directory);
        return 1;
    }
    action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGHUP, &action, NULL) != 0) {
        closedir(directory);
        return 1;
    }
    while ((entry = readdir(directory)) != NULL) {
        char path[300];
        char name[128] = "unknown";
        unsigned long keys[(KEY_MAX + BITS_PER_LONG) / BITS_PER_LONG] = {0};
        int fd;

        if (strncmp(entry->d_name, "event", 5U) != 0) continue;
        (void)snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0 &&
            (keys[KEY_POWER / BITS_PER_LONG] & (1UL << (KEY_POWER % BITS_PER_LONG)))) {
            if (count == MAX_SOURCES) {
                close(fd);
                result = 1;
                break;
            }
            (void)ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            sources[count++] = (struct pollfd){.fd = fd, .events = POLLIN};
            printf("power source=%s fd=%d name=\"%s\" capability=KEY_POWER grab=0\n", path, fd, name);
        } else {
            close(fd);
        }
    }
    closedir(directory);
    printf("power sources=%lu observation_seconds=30\n", (unsigned long)count);
    fflush(stdout);
    if (count == 0U) {
        printf("power result=no-advertised-evdev-source application_handling=unsupported\n");
        return 2;
    }
    while (!stopped && result == 0) {
        struct timespec now;
        int ready;

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) { result = 1; break; }
        if (now.tv_sec - start.tv_sec > 30 ||
            (now.tv_sec - start.tv_sec == 30 && now.tv_nsec >= start.tv_nsec)) break;
        ready = poll(sources, count, 250);
        if (ready < 0) {
            if (errno == EINTR) continue;
            result = 1;
            break;
        }
        for (nfds_t index = 0U; index < count; index++) {
            struct input_event event;
            ssize_t bytes;

            if (sources[index].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                result = 1;
                break;
            }
            if (!(sources[index].revents & POLLIN)) continue;
            while ((bytes = read(sources[index].fd, &event, sizeof(event))) == sizeof(event)) {
                if (event.type == EV_KEY && event.code == KEY_POWER) {
                    printf("power fd=%d event_s=%lld.%06ld type=%u code=%u value=%d\n",
                            sources[index].fd, (long long)event.time.tv_sec,
                            (long)event.time.tv_usec, event.type, event.code, event.value);
                    fflush(stdout);
                }
            }
            if (bytes == 0 || (bytes > 0 && bytes != sizeof(event)) ||
                (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                result = 1;
            }
        }
    }
    for (nfds_t index = 0U; index < count; index++) close(sources[index].fd);
    printf("power result=%d signal=%d\n", result, (int)stopped);
    return result;
}
