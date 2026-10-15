#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef EVIOCGPROP
#define EVIOCGPROP(len) _IOC(_IOC_READ, 'E', 0x09, len)
#endif

#ifndef INPUT_PROP_DIRECT
#define INPUT_PROP_DIRECT 0x01
#endif

#ifndef INPUT_PROP_MAX
#define INPUT_PROP_MAX 0x1f
#endif

#ifndef ABS_MT_SLOT
#define ABS_MT_SLOT 0x2f
#endif

#ifndef ABS_MT_TRACKING_ID
#define ABS_MT_TRACKING_ID 0x39
#endif

#ifndef ABS_MT_POSITION_X
#define ABS_MT_POSITION_X 0x35
#endif

#ifndef ABS_MT_POSITION_Y
#define ABS_MT_POSITION_Y 0x36
#endif

#define INPUT_DIR "/dev/input"
#define EVENT_PREFIX "event"
#define MAX_PATH_LENGTH 256U
#define MAX_CANDIDATES 64U
#define MAX_SLOTS 16U
#define DEFAULT_DISPLAY_WIDTH 1236U
#define DEFAULT_DISPLAY_HEIGHT 1648U
#define POLL_TIMEOUT_MS 250

#define BITS_PER_LONG (sizeof(unsigned long) * 8U)
#define BITSET_WORDS(max_value) (((max_value) + BITS_PER_LONG) / BITS_PER_LONG)
#define BIT_IS_SET(bit, set) \
    (((set)[(bit) / BITS_PER_LONG] & (1UL << ((bit) % BITS_PER_LONG))) != 0UL)

static volatile sig_atomic_t stop_requested;

struct device_candidate {
    char path[MAX_PATH_LENGTH];
    char name[256];
    bool direct;
    bool has_mt_axes;
    bool has_mt_protocol;
    bool has_st_axes;
    bool has_btn_touch;
    struct input_absinfo x;
    struct input_absinfo y;
    struct input_absinfo slot;
};

struct touch_slot {
    bool active;
    bool reported_active;
    bool changed;
    int raw_x;
    int raw_y;
    int reported_raw_x;
    int reported_raw_y;
};

struct probe_state {
    const struct device_candidate *device;
    unsigned int display_width;
    unsigned int display_height;
    unsigned int orientation;
    bool type_b;
    bool single_touch;
    bool type_a;
    unsigned int slot_count;
    int slot_min;
    unsigned int current_slot;
    int single_raw_x;
    int single_raw_y;
    bool single_active;
    bool single_reported_active;
    bool single_changed;
    bool type_a_frame_contact;
    bool discard_until_report;
    struct touch_slot slots[MAX_SLOTS];
};

static void
request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int
install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    if (sigemptyset(&action.sa_mask) != 0) {
        return -1;
    }

    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0) {
        return -1;
    }

    return 0;
}

static bool
is_event_name(const char *name)
{
    const char *suffix;

    if (strncmp(name, EVENT_PREFIX, strlen(EVENT_PREFIX)) != 0) {
        return false;
    }

    suffix = name + strlen(EVENT_PREFIX);
    if (*suffix == '\0') {
        return false;
    }

    while (*suffix != '\0') {
        if (*suffix < '0' || *suffix > '9') {
            return false;
        }
        suffix++;
    }

    return true;
}

static int
compare_candidates(const void *left, const void *right)
{
    const struct device_candidate *a = left;
    const struct device_candidate *b = right;

    return strcmp(a->path, b->path);
}

static bool
read_capabilities(int fd, struct device_candidate *candidate)
{
    unsigned long event_bits[BITSET_WORDS(EV_MAX + 1U)] = {0};
    unsigned long abs_bits[BITSET_WORDS(ABS_MAX + 1U)] = {0};
    unsigned long key_bits[BITSET_WORDS(KEY_MAX + 1U)] = {0};
    unsigned long property_bits[BITSET_WORDS(INPUT_PROP_MAX + 1U)] = {0};
    int name_length;

    name_length = ioctl(fd, EVIOCGNAME(sizeof(candidate->name)), candidate->name);
    if (name_length < 0) {
        return false;
    }
    candidate->name[sizeof(candidate->name) - 1U] = '\0';

    if (ioctl(fd, EVIOCGBIT(0, sizeof(event_bits)), event_bits) < 0 ||
        !BIT_IS_SET(EV_ABS, event_bits) ||
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0) {
        return false;
    }

    if (BIT_IS_SET(EV_KEY, event_bits) &&
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return false;
    }

    if (ioctl(fd, EVIOCGPROP(sizeof(property_bits)), property_bits) >= 0) {
        candidate->direct = BIT_IS_SET(INPUT_PROP_DIRECT, property_bits);
    }

    candidate->has_mt_axes = BIT_IS_SET(ABS_MT_POSITION_X, abs_bits) &&
                             BIT_IS_SET(ABS_MT_POSITION_Y, abs_bits);
    candidate->has_mt_protocol = candidate->has_mt_axes &&
                                 BIT_IS_SET(ABS_MT_SLOT, abs_bits) &&
                                 BIT_IS_SET(ABS_MT_TRACKING_ID, abs_bits);
    candidate->has_st_axes = BIT_IS_SET(ABS_X, abs_bits) &&
                             BIT_IS_SET(ABS_Y, abs_bits);
    candidate->has_btn_touch = BIT_IS_SET(BTN_TOUCH, key_bits);

    if (!candidate->has_mt_axes &&
        !(candidate->has_st_axes && candidate->has_btn_touch)) {
        return false;
    }

    if (candidate->has_mt_protocol) {
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &candidate->x) < 0 ||
            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &candidate->y) < 0 ||
            ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &candidate->slot) < 0) {
            return false;
        }
    } else if (candidate->has_st_axes) {
        if (ioctl(fd, EVIOCGABS(ABS_X), &candidate->x) < 0 ||
            ioctl(fd, EVIOCGABS(ABS_Y), &candidate->y) < 0) {
            return false;
        }
    } else {
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &candidate->x) < 0 ||
            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &candidate->y) < 0) {
            return false;
        }
    }

    if (candidate->x.maximum <= candidate->x.minimum ||
        candidate->y.maximum <= candidate->y.minimum) {
        return false;
    }

    return true;
}

static size_t
discover_devices(struct device_candidate *candidates, size_t capacity)
{
    DIR *directory;
    struct dirent *entry;
    size_t count = 0U;

    directory = opendir(INPUT_DIR);
    if (directory == NULL) {
        fprintf(stderr, "touch_probe: unable to open %s: %s\n",
                INPUT_DIR,
                strerror(errno));
        return 0U;
    }

    while ((entry = readdir(directory)) != NULL) {
        char path[MAX_PATH_LENGTH];
        int fd;
        int path_length;

        if (!is_event_name(entry->d_name) || count == capacity) {
            continue;
        }

        path_length = snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, entry->d_name);
        if (path_length < 0 || (size_t)path_length >= sizeof(path)) {
            continue;
        }

        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }

        memset(&candidates[count], 0, sizeof(candidates[count]));
        if (read_capabilities(fd, &candidates[count])) {
            memcpy(candidates[count].path, path, (size_t)path_length + 1U);
            count++;
        }
        close(fd);
    }

    closedir(directory);
    qsort(candidates, count, sizeof(candidates[0]), compare_candidates);
    return count;
}

static void
print_candidate(const struct device_candidate *candidate)
{
    const char *protocol = candidate->has_mt_protocol ? "type-b" :
                           candidate->has_mt_axes ? "type-a-or-single" :
                           "single-touch";

    printf("DEVICE path=%s name=\"%s\" protocol=%s direct=%s "
           "axes=x[%d,%d] y[%d,%d]\n",
           candidate->path,
           candidate->name,
           protocol,
           candidate->direct ? "yes" : "no",
           candidate->x.minimum,
           candidate->x.maximum,
           candidate->y.minimum,
           candidate->y.maximum);
}

static int
open_selected_device(const struct device_candidate *candidates,
                     size_t count,
                     const char *requested_path,
                     size_t *selected_index)
{
    size_t index;

    if (requested_path != NULL) {
        for (index = 0U; index < count; index++) {
            if (strcmp(candidates[index].path, requested_path) == 0) {
                *selected_index = index;
                return open(candidates[index].path, O_RDONLY | O_NONBLOCK);
            }
        }

        fprintf(stderr, "touch_probe: requested device is not a touchscreen candidate: %s\n",
                requested_path);
        return -1;
    }

    if (count != 1U) {
        fprintf(stderr,
                "touch_probe: expected one touchscreen candidate; found %zu. "
                "Use --device after reviewing the DEVICE lines.\n",
                count);
        return -1;
    }

    *selected_index = 0U;
    return open(candidates[0].path, O_RDONLY | O_NONBLOCK);
}

static int
parse_unsigned(const char *text, unsigned int *value)
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX) {
        return -1;
    }

    *value = (unsigned int)parsed;
    return 0;
}

static int
parse_orientation(const char *text, unsigned int *orientation)
{
    unsigned int parsed;

    if (parse_unsigned(text, &parsed) != 0 ||
        (parsed != 0U && parsed != 90U && parsed != 180U && parsed != 270U)) {
        return -1;
    }

    *orientation = parsed;
    return 0;
}

static void
print_usage(const char *program)
{
    printf("Usage: %s [--device /dev/input/eventX] [--width N] [--height N] "
           "[--orientation 0|90|180|270]\n",
           program);
    printf("Defaults: --width %u --height %u --orientation 0\n",
           DEFAULT_DISPLAY_WIDTH,
           DEFAULT_DISPLAY_HEIGHT);
}

static int
parse_arguments(int argc,
                char **argv,
                const char **requested_path,
                unsigned int *display_width,
                unsigned int *display_height,
                unsigned int *orientation)
{
    int index;

    for (index = 1; index < argc; index++) {
        const char *argument = argv[index];
        const char *value = NULL;

        if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
            print_usage(argv[0]);
            return 1;
        }

        if (strcmp(argument, "--device") == 0 ||
            strcmp(argument, "--width") == 0 ||
            strcmp(argument, "--height") == 0 ||
            strcmp(argument, "--orientation") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "touch_probe: missing value for %s\n", argument);
                return -1;
            }
            value = argv[++index];
        }

        if (strcmp(argument, "--device") == 0) {
            *requested_path = value;
        } else if (strcmp(argument, "--width") == 0) {
            if (parse_unsigned(value, display_width) != 0 || *display_width == 0U) {
                fprintf(stderr, "touch_probe: invalid display width: %s\n", value);
                return -1;
            }
        } else if (strcmp(argument, "--height") == 0) {
            if (parse_unsigned(value, display_height) != 0 || *display_height == 0U) {
                fprintf(stderr, "touch_probe: invalid display height: %s\n", value);
                return -1;
            }
        } else if (strcmp(argument, "--orientation") == 0) {
            if (parse_orientation(value, orientation) != 0) {
                fprintf(stderr, "touch_probe: invalid orientation: %s\n", value);
                return -1;
            }
        } else if (value == NULL) {
            fprintf(stderr, "touch_probe: unknown argument: %s\n", argument);
            return -1;
        }
    }

    return 0;
}

static int
clamp_int64(int64_t value, int64_t minimum, int64_t maximum)
{
    if (value < minimum) {
        return (int)minimum;
    }
    if (value > maximum) {
        return (int)maximum;
    }
    return (int)value;
}

static int
normalize_axis(int raw, const struct input_absinfo *axis, unsigned int output_size)
{
    int64_t bounded_raw = raw;
    int64_t minimum = axis->minimum;
    int64_t maximum = axis->maximum;
    int64_t normalized;

    if (output_size == 0U || maximum <= minimum) {
        return 0;
    }

    if (bounded_raw < minimum) {
        bounded_raw = minimum;
    } else if (bounded_raw > maximum) {
        bounded_raw = maximum;
    }

    normalized = ((bounded_raw - minimum) * ((int64_t)output_size - 1)) /
                (maximum - minimum);
    return clamp_int64(normalized, 0, (int64_t)output_size - 1);
}

static void
map_coordinates(const struct probe_state *state,
                 int raw_x,
                 int raw_y,
                 int *screen_x,
                 int *screen_y)
{
    int x = normalize_axis(raw_x, &state->device->x, state->display_width);
    int y = normalize_axis(raw_y, &state->device->y, state->display_height);

    switch (state->orientation) {
    case 90U:
        *screen_x = (int)state->display_height - 1 - y;
        *screen_y = x;
        break;
    case 180U:
        *screen_x = (int)state->display_width - 1 - x;
        *screen_y = (int)state->display_height - 1 - y;
        break;
    case 270U:
        *screen_x = y;
        *screen_y = (int)state->display_width - 1 - x;
        break;
    default:
        *screen_x = x;
        *screen_y = y;
        break;
    }
}

static void
print_touch_event(const char *event_name,
                  unsigned int slot,
                  const struct probe_state *state,
                  int raw_x,
                  int raw_y)
{
    int screen_x;
    int screen_y;

    map_coordinates(state, raw_x, raw_y, &screen_x, &screen_y);
    if (slot == 0U) {
        printf("TOUCH_%s x=%d y=%d\n", event_name, screen_x, screen_y);
    } else {
        printf("TOUCH_%s slot=%u x=%d y=%d\n",
               event_name,
               slot,
               screen_x,
               screen_y);
    }
    fflush(stdout);
}

static void
reset_slots(struct probe_state *state)
{
    unsigned int slot;

    for (slot = 0U; slot < MAX_SLOTS; slot++) {
        state->slots[slot].active = false;
        state->slots[slot].reported_active = false;
        state->slots[slot].changed = false;
    }
}

static void
commit_type_b(struct probe_state *state)
{
    unsigned int slot;

    for (slot = 0U; slot < state->slot_count; slot++) {
        struct touch_slot *touch = &state->slots[slot];

        if (touch->active && !touch->reported_active) {
            print_touch_event("DOWN", slot, state, touch->raw_x, touch->raw_y);
        } else if (touch->active && touch->reported_active && touch->changed) {
            print_touch_event("MOVE", slot, state, touch->raw_x, touch->raw_y);
        } else if (!touch->active && touch->reported_active) {
            print_touch_event("UP", slot, state,
                              touch->reported_raw_x,
                              touch->reported_raw_y);
        }

        if (touch->active) {
            touch->reported_raw_x = touch->raw_x;
            touch->reported_raw_y = touch->raw_y;
        }
        touch->reported_active = touch->active;
        touch->changed = false;
    }
}

static void
commit_single_touch(struct probe_state *state)
{
    bool active = state->single_active;

    if (active && !state->single_reported_active) {
        print_touch_event("DOWN", 0U, state, state->single_raw_x, state->single_raw_y);
    } else if (active && state->single_reported_active && state->single_changed) {
        print_touch_event("MOVE", 0U, state, state->single_raw_x, state->single_raw_y);
    } else if (!active && state->single_reported_active) {
        print_touch_event("UP", 0U, state,
                          state->single_raw_x,
                          state->single_raw_y);
    }

    state->single_reported_active = active;
    state->single_changed = false;
    state->type_a_frame_contact = false;
}

static void
handle_sync_dropped(struct probe_state *state)
{
    printf("TOUCH_CANCEL reason=SYN_DROPPED\n");
    fflush(stdout);
    state->single_active = false;
    state->single_reported_active = false;
    state->single_changed = false;
    state->type_a_frame_contact = false;
    state->discard_until_report = true;
    reset_slots(state);
}

static void
handle_event(struct probe_state *state, const struct input_event *event)
{
    if (state->discard_until_report) {
        if (event->type == EV_SYN && event->code == SYN_REPORT) {
            state->discard_until_report = false;
        }
        return;
    }

    if (event->type == EV_ABS && state->type_b) {
        struct touch_slot *touch;
        int slot_index;

        if (event->code == ABS_MT_SLOT) {
            slot_index = event->value - state->slot_min;
            if (slot_index >= 0 && (unsigned int)slot_index < state->slot_count) {
                state->current_slot = (unsigned int)slot_index;
            }
            return;
        }

        touch = &state->slots[state->current_slot];
        if (event->code == ABS_MT_TRACKING_ID) {
            touch->active = event->value >= 0;
            touch->changed = true;
        } else if (event->code == ABS_MT_POSITION_X) {
            touch->raw_x = event->value;
            touch->changed = true;
        } else if (event->code == ABS_MT_POSITION_Y) {
            touch->raw_y = event->value;
            touch->changed = true;
        }
        return;
    }

    if (event->type == EV_ABS && state->single_touch) {
        if (event->code == ABS_X || event->code == ABS_MT_POSITION_X) {
            state->single_raw_x = event->value;
            state->single_changed = true;
            state->type_a_frame_contact = state->type_a || state->type_a_frame_contact;
        } else if (event->code == ABS_Y || event->code == ABS_MT_POSITION_Y) {
            state->single_raw_y = event->value;
            state->single_changed = true;
            state->type_a_frame_contact = state->type_a || state->type_a_frame_contact;
        }
        return;
    }

    if (event->type == EV_KEY && event->code == BTN_TOUCH && state->single_touch) {
        state->single_active = event->value != 0;
        state->single_changed = true;
        return;
    }

    if (event->type != EV_SYN) {
        return;
    }

    if (event->code == SYN_DROPPED) {
        handle_sync_dropped(state);
    } else if (event->code == SYN_REPORT) {
        if (state->type_b) {
            commit_type_b(state);
        } else {
            if (state->type_a) {
                state->single_active = state->type_a_frame_contact;
            }
            commit_single_touch(state);
        }
    }
}

static int
run_probe(int fd, struct probe_state *state)
{
    struct pollfd descriptor;

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.fd = fd;
    descriptor.events = POLLIN;

    while (!stop_requested) {
        struct input_event event;
        int poll_result;

        poll_result = poll(&descriptor, 1U, POLL_TIMEOUT_MS);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "touch_probe: poll failed: %s\n", strerror(errno));
            return -1;
        }
        if (poll_result == 0) {
            continue;
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            fprintf(stderr, "touch_probe: input device poll error: 0x%x\n",
                    descriptor.revents);
            return -1;
        }

        while (!stop_requested) {
            ssize_t bytes_read = read(fd, &event, sizeof(event));

            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (bytes_read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "touch_probe: read failed: %s\n", strerror(errno));
                return -1;
            }
            if (bytes_read == 0) {
                break;
            }
            if ((size_t)bytes_read != sizeof(event)) {
                fprintf(stderr, "touch_probe: short input event read: %zd bytes\n",
                        bytes_read);
                return -1;
            }

            handle_event(state, &event);
        }
    }

    return 0;
}

int
main(int argc, char **argv)
{
    struct device_candidate candidates[MAX_CANDIDATES];
    struct probe_state state;
    const char *requested_path = NULL;
    unsigned int display_width = DEFAULT_DISPLAY_WIDTH;
    unsigned int display_height = DEFAULT_DISPLAY_HEIGHT;
    unsigned int orientation = 0U;
    size_t candidate_count;
    size_t selected_index = 0U;
    int fd = -1;
    int parse_result;
    int result = EXIT_FAILURE;

    parse_result = parse_arguments(argc,
                                   argv,
                                   &requested_path,
                                   &display_width,
                                   &display_height,
                                   &orientation);
    if (parse_result != 0) {
        return parse_result > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (install_signal_handlers() != 0) {
        fprintf(stderr, "touch_probe: unable to install signal handlers: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    candidate_count = discover_devices(candidates, MAX_CANDIDATES);
    for (selected_index = 0U; selected_index < candidate_count; selected_index++) {
        print_candidate(&candidates[selected_index]);
    }

    fd = open_selected_device(candidates,
                              candidate_count,
                              requested_path,
                              &selected_index);
    if (fd < 0) {
        if (candidate_count == 0U) {
            fprintf(stderr, "touch_probe: no touchscreen candidate was found\n");
        } else if (requested_path != NULL) {
            fprintf(stderr, "touch_probe: unable to open selected device %s: %s\n",
                    requested_path,
                    strerror(errno));
        }
        return EXIT_FAILURE;
    }

    memset(&state, 0, sizeof(state));
    state.device = &candidates[selected_index];
    state.display_width = display_width;
    state.display_height = display_height;
    state.orientation = orientation;
    state.type_b = state.device->has_mt_protocol;
    state.single_touch = !state.type_b;
    state.type_a = state.single_touch && state.device->has_mt_axes &&
                   !state.device->has_st_axes && !state.device->has_btn_touch;
    state.slot_min = state.device->slot.minimum;
    state.slot_count = 1U;

    if (state.type_b) {
        int64_t reported_slots = (int64_t)state.device->slot.maximum -
                                 (int64_t)state.device->slot.minimum + 1;

        if (reported_slots <= 0) {
            fprintf(stderr, "touch_probe: invalid multitouch slot range\n");
            close(fd);
            return EXIT_FAILURE;
        }
        state.slot_count = reported_slots > MAX_SLOTS ? MAX_SLOTS : (unsigned int)reported_slots;
        if (reported_slots > MAX_SLOTS) {
            fprintf(stderr,
                    "touch_probe: warning: limiting %lld reported slots to %u\n",
                    (long long)reported_slots,
                    MAX_SLOTS);
        }
    }

    printf("SELECTED path=%s name=\"%s\" display=%ux%u orientation=%u protocol=%s\n",
           state.device->path,
           state.device->name,
           state.display_width,
           state.display_height,
           state.orientation,
           state.type_b ? "type-b" : state.type_a ? "type-a" : "single-touch");
    printf("Listening for touch input; press Ctrl-C to stop.\n");
    fflush(stdout);

    if (run_probe(fd, &state) == 0) {
        result = EXIT_SUCCESS;
    }

    close(fd);
    return result;
}
