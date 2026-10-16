#include "input.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define INPUT_DIR "/dev/input"
#define MAX_PATH_LENGTH 256U
#define MAX_TOUCH_SLOTS 32U
#define POLL_TIMEOUT_MS 250

#ifndef EVIOCGPROP
#define EVIOCGPROP(len) _IOC(_IOC_READ, 'E', 0x09, len)
#endif

#ifndef INPUT_PROP_MAX
#define INPUT_PROP_MAX 0x1f
#endif

#define BITS_PER_LONG (sizeof(unsigned long) * 8U)
#define BITSET_WORDS(max_value) (((max_value) + BITS_PER_LONG) / BITS_PER_LONG)
#define BIT_IS_SET(bit, set) \
    (((set)[(bit) / BITS_PER_LONG] & (1UL << ((bit) % BITS_PER_LONG))) != 0UL)

struct touch_device {
    char path[MAX_PATH_LENGTH];
    bool type_b;
    struct input_absinfo x;
    struct input_absinfo y;
    struct input_absinfo slot;
};

struct inkboy_input {
    int fd;
    bool grabbed;
    struct touch_device device;
    unsigned int current_slot;
    bool active[MAX_TOUCH_SLOTS];
    bool new_contact[MAX_TOUCH_SLOTS];
    bool released[MAX_TOUCH_SLOTS];
    int raw_x[MAX_TOUCH_SLOTS];
    int raw_y[MAX_TOUCH_SLOTS];
    bool single_active;
    bool single_new_contact;
    bool single_released;
    int single_raw_x;
    int single_raw_y;
    bool ignored[MAX_TOUCH_SLOTS];
    bool primary_active;
    bool primary_new_contact;
    bool primary_released;
    unsigned int primary_slot;
    enum inkboy_control primary_control;
    bool primary_cancelled;
    const struct inkboy_layout *layout;
    inkboy_input_callback callback;
    void *userdata;
};

static bool
is_event_name(const char *name)
{
    const char *suffix;

    if (strncmp(name, "event", 5U) != 0) {
        return false;
    }
    suffix = name + 5U;
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

static bool
read_touch_capabilities(int fd, struct touch_device *device)
{
    unsigned long event_bits[BITSET_WORDS(EV_MAX + 1U)] = {0};
    unsigned long abs_bits[BITSET_WORDS(ABS_MAX + 1U)] = {0};
    unsigned long key_bits[BITSET_WORDS(KEY_MAX + 1U)] = {0};
    unsigned long property_bits[BITSET_WORDS(INPUT_PROP_MAX + 1U)] = {0};
    bool has_mt_axes;
    bool has_mt_protocol;
    bool has_st_axes;
    bool has_btn_touch;

    if (ioctl(fd, EVIOCGBIT(0, sizeof(event_bits)), event_bits) < 0 ||
        !BIT_IS_SET(EV_ABS, event_bits) ||
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0) {
        return false;
    }
    if (BIT_IS_SET(EV_KEY, event_bits) &&
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return false;
    }
    (void)ioctl(fd, EVIOCGPROP(sizeof(property_bits)), property_bits);

    has_mt_axes = BIT_IS_SET(ABS_MT_POSITION_X, abs_bits) &&
                  BIT_IS_SET(ABS_MT_POSITION_Y, abs_bits);
    has_mt_protocol = has_mt_axes &&
                      BIT_IS_SET(ABS_MT_SLOT, abs_bits) &&
                      BIT_IS_SET(ABS_MT_TRACKING_ID, abs_bits);
    has_st_axes = BIT_IS_SET(ABS_X, abs_bits) && BIT_IS_SET(ABS_Y, abs_bits);
    has_btn_touch = BIT_IS_SET(BTN_TOUCH, key_bits);

    /* The PW5 uses a direct type-B panel; retain the validated fallback. */
    if (!has_mt_protocol && !(has_st_axes && has_btn_touch)) {
        return false;
    }

    memset(device, 0, sizeof(*device));
    device->type_b = has_mt_protocol;
    if (device->type_b) {
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &device->x) < 0 ||
            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &device->y) < 0 ||
            ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &device->slot) < 0) {
            return false;
        }
    } else if (ioctl(fd, EVIOCGABS(ABS_X), &device->x) < 0 ||
               ioctl(fd, EVIOCGABS(ABS_Y), &device->y) < 0) {
        return false;
    }

    return device->x.maximum > device->x.minimum &&
           device->y.maximum > device->y.minimum;
}

static int
discover_touch(struct touch_device *device)
{
    DIR *directory;
    struct dirent *entry;
    size_t matches = 0U;

    directory = opendir(INPUT_DIR);
    if (directory == NULL) {
        fprintf(stderr, "inkboy: unable to open %s: %s\n",
                INPUT_DIR, strerror(errno));
        return -1;
    }

    while ((entry = readdir(directory)) != NULL) {
        char path[MAX_PATH_LENGTH];
        struct touch_device candidate;
        int path_length;
        int fd;

        if (!is_event_name(entry->d_name)) {
            continue;
        }
        path_length = snprintf(path, sizeof(path), "%s/%s", INPUT_DIR,
                               entry->d_name);
        if (path_length < 0 || (size_t)path_length >= sizeof(path)) {
            continue;
        }
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        if (read_touch_capabilities(fd, &candidate)) {
            if (matches == 0U) {
                candidate.path[0] = '\0';
                memcpy(candidate.path, path, (size_t)path_length + 1U);
                *device = candidate;
            }
            matches++;
        }
        close(fd);
    }
    closedir(directory);

    if (matches != 1U) {
        fprintf(stderr, "inkboy: expected one touchscreen candidate; found %zu\n",
                matches);
        return -1;
    }
    return 0;
}

static int
open_touch(struct inkboy_input *input)
{
    int fd = open(input->device.path, O_RDONLY | O_NONBLOCK);

    if (fd < 0) {
        fprintf(stderr, "inkboy: unable to open touchscreen %s: %s\n",
                input->device.path, strerror(errno));
        return -1;
    }
    if (ioctl(fd, EVIOCGRAB, 1) != 0) {
        fprintf(stderr, "inkboy: unable to grab touchscreen %s: %s\n",
                input->device.path, strerror(errno));
        close(fd);
        return -1;
    }

    input->fd = fd;
    input->grabbed = true;
    return 0;
}

static void
emit_event(struct inkboy_input *input,
           enum inkboy_control control,
           enum inkboy_input_phase phase,
           uint32_t x,
           uint32_t y)
{
    struct inkboy_input_event event = {
        .control = control,
        .phase = phase,
        .x = x,
        .y = y
    };

    if (input->callback != NULL) {
        input->callback(&event, input->userdata);
    }
}

static void
cancel_primary_control(struct inkboy_input *input)
{
    if (inkboy_is_interactive_control(input->primary_control) &&
        !input->primary_cancelled) {
        emit_event(input, input->primary_control, INKBOY_INPUT_CANCEL, 0U, 0U);
        input->primary_cancelled = true;
    }
}

static void
reset_touch_state(struct inkboy_input *input)
{
    input->current_slot = 0U;
    memset(input->active, 0, sizeof(input->active));
    memset(input->new_contact, 0, sizeof(input->new_contact));
    memset(input->released, 0, sizeof(input->released));
    memset(input->ignored, 0, sizeof(input->ignored));
    input->single_active = false;
    input->single_new_contact = false;
    input->single_released = false;
    input->primary_active = false;
    input->primary_new_contact = false;
    input->primary_released = false;
    input->primary_slot = 0U;
    input->primary_control = INKBOY_CONTROL_NONE;
    input->primary_cancelled = false;
}

static int
finish_primary_frame(struct inkboy_input *input, uint32_t x, uint32_t y)
{
    if (input->primary_new_contact) {
        input->primary_control = inkboy_control_at_point(input->layout, x, y);
        input->primary_new_contact = false;
        if (inkboy_is_interactive_control(input->primary_control)) {
            emit_event(input, input->primary_control, INKBOY_INPUT_DOWN, x, y);
        }
    } else if (input->primary_active &&
               inkboy_is_interactive_control(input->primary_control) &&
               !inkboy_control_contains_point(input->layout,
                                              input->primary_control, x, y)) {
        cancel_primary_control(input);
    }

    if (!input->primary_released) {
        return 0;
    }

    if (input->primary_control == INKBOY_CONTROL_EXIT &&
        !input->primary_cancelled &&
        inkboy_control_contains_point(input->layout, INKBOY_CONTROL_EXIT, x, y)) {
        return 1;
    }
    if (inkboy_is_interactive_control(input->primary_control)) {
        if (!input->primary_cancelled &&
            inkboy_control_contains_point(input->layout,
                                          input->primary_control, x, y)) {
            emit_event(input, input->primary_control, INKBOY_INPUT_UP, x, y);
        } else {
            cancel_primary_control(input);
        }
    }
    input->primary_active = false;
    input->primary_released = false;
    input->primary_control = INKBOY_CONTROL_NONE;
    input->primary_cancelled = false;
    return 0;
}

static int
handle_touch_event(struct inkboy_input *input, const struct input_event *event)
{
    unsigned int slot = input->current_slot;

    if (event->type == EV_SYN && event->code == SYN_DROPPED) {
        cancel_primary_control(input);
        reset_touch_state(input);
        return 0;
    }

    if (input->device.type_b) {
        if (event->type == EV_ABS && event->code == ABS_MT_SLOT) {
            int64_t selected = (int64_t)event->value - input->device.slot.minimum;
            input->current_slot = selected >= 0 && selected < MAX_TOUCH_SLOTS
                                      ? (unsigned int)selected : MAX_TOUCH_SLOTS;
            return 0;
        }
        if (slot >= MAX_TOUCH_SLOTS &&
            !(event->type == EV_SYN && event->code == SYN_REPORT)) {
            return 0;
        }
        if (event->type == EV_ABS && event->code == ABS_MT_POSITION_X) {
            input->raw_x[slot] = event->value;
        } else if (event->type == EV_ABS && event->code == ABS_MT_POSITION_Y) {
            input->raw_y[slot] = event->value;
        } else if (event->type == EV_ABS && event->code == ABS_MT_TRACKING_ID) {
            if (event->value >= 0) {
                input->active[slot] = true;
                input->new_contact[slot] = true;
                input->released[slot] = false;
                if (!input->primary_active && !input->primary_released &&
                    !input->ignored[slot]) {
                    input->primary_active = true;
                    input->primary_new_contact = true;
                    input->primary_slot = slot;
                    input->primary_control = INKBOY_CONTROL_NONE;
                    input->primary_cancelled = false;
                } else if (slot != input->primary_slot) {
                    input->ignored[slot] = true;
                }
            } else {
                input->active[slot] = false;
                input->released[slot] = true;
                if (slot == input->primary_slot) {
                    input->primary_active = false;
                    input->primary_released = true;
                } else {
                    input->ignored[slot] = false;
                }
            }
        }
        if (event->type != EV_SYN || event->code != SYN_REPORT) {
            return 0;
        }

        for (slot = 0U; slot < MAX_TOUCH_SLOTS; slot++) {
            uint32_t x;
            uint32_t y;

            if (slot != input->primary_slot ||
                (!input->active[slot] && !input->released[slot])) {
                continue;
            }
            x = inkboy_normalize_axis(input->raw_x[slot],
                                      input->device.x.minimum,
                                      input->device.x.maximum,
                                      input->layout->width);
            y = inkboy_normalize_axis(input->raw_y[slot],
                                      input->device.y.minimum,
                                      input->device.y.maximum,
                                      input->layout->height);
            input->new_contact[slot] = false;
            input->released[slot] = false;
            return finish_primary_frame(input, x, y);
        }
        return 0;
    }

    if (event->type == EV_ABS && event->code == ABS_X) {
        input->single_raw_x = event->value;
    } else if (event->type == EV_ABS && event->code == ABS_Y) {
        input->single_raw_y = event->value;
    } else if (event->type == EV_KEY && event->code == BTN_TOUCH) {
        if (event->value != 0) {
            input->single_active = true;
            input->single_new_contact = true;
            input->single_released = false;
            input->primary_active = true;
            input->primary_new_contact = true;
            input->primary_control = INKBOY_CONTROL_NONE;
            input->primary_cancelled = false;
        } else {
            input->single_active = false;
            input->single_released = true;
            input->primary_active = false;
            input->primary_released = true;
        }
    }
    if (event->type == EV_SYN && event->code == SYN_REPORT) {
        uint32_t x = inkboy_normalize_axis(input->single_raw_x,
                                           input->device.x.minimum,
                                           input->device.x.maximum,
                                           input->layout->width);
        uint32_t y = inkboy_normalize_axis(input->single_raw_y,
                                           input->device.y.minimum,
                                           input->device.y.maximum,
                                           input->layout->height);
        input->single_new_contact = false;
        input->single_released = false;
        return finish_primary_frame(input, x, y);
    }
    return 0;
}

int
inkboy_input_open(struct inkboy_input **input_out,
                  const struct inkboy_layout *layout,
                  inkboy_input_callback callback,
                  void *userdata)
{
    struct inkboy_input *input;

    if (input_out == NULL || layout == NULL) {
        return -1;
    }
    *input_out = NULL;
    input = calloc(1U, sizeof(*input));
    if (input == NULL) {
        fprintf(stderr, "inkboy: unable to allocate input: %s\n", strerror(errno));
        return -1;
    }
    input->fd = -1;
    input->layout = layout;
    input->callback = callback;
    input->userdata = userdata;
    if (discover_touch(&input->device) != 0 || open_touch(input) != 0) {
        free(input);
        return -1;
    }
    *input_out = input;
    return 0;
}

static int
poll_input(struct inkboy_input *input, int timeout_ms)
{
    struct pollfd descriptor;

    if (input == NULL || input->fd < 0) {
        return -1;
    }
    descriptor.fd = input->fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;

    {
        int poll_result = poll(&descriptor, 1U, timeout_ms);

        if (poll_result < 0) {
            if (errno == EINTR) {
                return 0;
            }
            fprintf(stderr, "inkboy: touchscreen poll failed: %s\n", strerror(errno));
            cancel_primary_control(input);
            return -1;
        }
        if (poll_result == 0) {
            return 0;
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            fprintf(stderr, "inkboy: touchscreen poll error: 0x%x\n",
                    descriptor.revents);
            cancel_primary_control(input);
            return -1;
        }

        for (;;) {
            struct input_event event;
            ssize_t bytes_read = read(input->fd, &event, sizeof(event));

            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (bytes_read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "inkboy: touchscreen read failed: %s\n",
                        strerror(errno));
                cancel_primary_control(input);
                return -1;
            }
            if (bytes_read == 0) {
                cancel_primary_control(input);
                return -1;
            }
            if ((size_t)bytes_read != sizeof(event)) {
                fprintf(stderr, "inkboy: short touchscreen event read: %zd bytes\n",
                        bytes_read);
                cancel_primary_control(input);
                return -1;
            }
            if (handle_touch_event(input, &event) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

int
inkboy_input_wait(struct inkboy_input *input,
                  const volatile sig_atomic_t *stop_requested)
{
    if (input == NULL || input->fd < 0) {
        return -1;
    }
    while (stop_requested == NULL || !*stop_requested) {
        int result = poll_input(input, POLL_TIMEOUT_MS);

        if (result != 0) {
            return result;
        }
    }
    cancel_primary_control(input);
    return 0;
}

int
inkboy_input_poll(struct inkboy_input *input)
{
    return poll_input(input, 0);
}

void
inkboy_input_close(struct inkboy_input *input)
{
    if (input == NULL) {
        return;
    }
    cancel_primary_control(input);
    if (input->fd >= 0) {
        if (input->grabbed && ioctl(input->fd, EVIOCGRAB, 0) != 0) {
            fprintf(stderr, "inkboy: warning: unable to release touchscreen: %s\n",
                    strerror(errno));
        }
        close(input->fd);
    }
    free(input);
}
