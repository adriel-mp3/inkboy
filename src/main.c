#include "duck_bitmap.h"

#include "fbink.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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

#define TITLE_SCALE 4U
#define TITLE_GLYPH_WIDTH 5U
#define TITLE_GLYPH_HEIGHT 7U
#define TITLE_SPACING 2U
#define TITLE_LENGTH 6U
#define DUCK_MAX_SIZE 420U
#define DUCK_MIN_SIZE 192U
#define CONTROL_SCALE 4U
#define CONTROL_LINE_WIDTH 4U
#define HEADER_SAFE_PERCENT 10U
#define HEADER_CONTENT_OFFSET_PERCENT 1U
#define HEADER_SIDE_MARGIN_PERCENT 3U
#define EXIT_TOUCH_WIDTH_PERCENT 14U
#define EXIT_TOUCH_HEIGHT_PERCENT 6U
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

static volatile sig_atomic_t stop_requested;

enum control_id {
    CONTROL_NONE,
    CONTROL_EXIT,
    CONTROL_UP,
    CONTROL_DOWN,
    CONTROL_LEFT,
    CONTROL_RIGHT,
    CONTROL_A,
    CONTROL_B,
    CONTROL_L,
    CONTROL_R,
    CONTROL_START,
    CONTROL_SELECT
};

struct control_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct control_circle {
    uint32_t center_x;
    uint32_t center_y;
    uint32_t radius;
};

struct screen_layout {
    uint32_t width;
    uint32_t height;
    uint32_t header_bottom;
    uint32_t main_bottom;
    uint32_t primary_bottom;
    uint32_t title_x;
    uint32_t title_y;
    uint32_t duck_x;
    uint32_t duck_y;
    uint32_t duck_size;
    struct control_rect exit;
    struct control_rect dpad;
    uint32_t dpad_arm;
    struct control_circle a;
    struct control_circle b;
    struct control_rect l;
    struct control_rect r;
    struct control_rect start;
    struct control_rect select;
};

struct touch_device {
    char path[MAX_PATH_LENGTH];
    bool type_b;
    struct input_absinfo x;
    struct input_absinfo y;
    struct input_absinfo slot;
};

struct touch_state {
    int fd;
    bool grabbed;
    const struct touch_device *device;
    unsigned int current_slot;
    bool active[MAX_TOUCH_SLOTS];
    bool new_contact[MAX_TOUCH_SLOTS];
    bool started_inside[MAX_TOUCH_SLOTS];
    bool released[MAX_TOUCH_SLOTS];
    int raw_x[MAX_TOUCH_SLOTS];
    int raw_y[MAX_TOUCH_SLOTS];
    bool single_active;
    bool single_new_contact;
    bool single_started_inside;
    bool single_released;
    int single_raw_x;
    int single_raw_y;
    bool ignored[MAX_TOUCH_SLOTS];
    bool primary_active;
    bool primary_new_contact;
    bool primary_released;
    unsigned int primary_slot;
    enum control_id primary_control;
    bool primary_cancelled;
    uint32_t primary_x;
    uint32_t primary_y;
    const struct screen_layout *layout;
};

static const uint8_t title_glyphs[TITLE_LENGTH][TITLE_GLYPH_HEIGHT] = {
    /* I */ {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f},
    /* n */ {0x00, 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11},
    /* k */ {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    /* B */ {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
    /* o */ {0x00, 0x0e, 0x11, 0x11, 0x11, 0x11, 0x0e},
    /* y */ {0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x18},
};

static const uint8_t *
label_glyph(char character)
{
    static const uint8_t i[] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f};
    static const uint8_t e[] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    static const uint8_t x[] = {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
    static const uint8_t t[] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t a[] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    static const uint8_t r[] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    static const uint8_t s[] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    static const uint8_t l[] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
    static const uint8_t c[] = {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f};
    static const uint8_t b[] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};

    switch (character) {
    case 'I': return i;
    case 'E': return e;
    case 'X': return x;
    case 'T': return t;
    case 'A': return a;
    case 'R': return r;
    case 'S': return s;
    case 'L': return l;
    case 'C': return c;
    case 'B': return b;
    default: return NULL;
    }
}

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
        sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGHUP, &action, NULL) != 0) {
        return -1;
    }

    return 0;
}

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

    /* The PW5 uses a direct type-B panel; retain the validated single-touch fallback. */
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
        fprintf(stderr, "inkboy: unable to open %s: %s\n", INPUT_DIR, strerror(errno));
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
        path_length = snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, entry->d_name);
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
        fprintf(stderr, "inkboy: expected one touchscreen candidate; found %zu\n", matches);
        return -1;
    }
    return 0;
}

static int
open_touch(struct touch_state *touch, const struct touch_device *device)
{
    int fd = open(device->path, O_RDONLY | O_NONBLOCK);

    if (fd < 0) {
        fprintf(stderr, "inkboy: unable to open touchscreen %s: %s\n",
                device->path, strerror(errno));
        return -1;
    }
    if (ioctl(fd, EVIOCGRAB, 1) != 0) {
        fprintf(stderr, "inkboy: unable to grab touchscreen %s: %s\n",
                device->path, strerror(errno));
        close(fd);
        return -1;
    }

    memset(touch, 0, sizeof(*touch));
    touch->fd = fd;
    touch->grabbed = true;
    touch->device = device;
    return 0;
}

static void
close_touch(struct touch_state *touch)
{
    if (touch->fd < 0) {
        return;
    }
    if (touch->grabbed && ioctl(touch->fd, EVIOCGRAB, 0) != 0) {
        fprintf(stderr, "inkboy: warning: unable to release touchscreen: %s\n",
                strerror(errno));
    }
    close(touch->fd);
    touch->fd = -1;
    touch->grabbed = false;
}

static uint32_t
normalize_axis(int raw, const struct input_absinfo *axis, uint32_t output_size)
{
    int64_t minimum = axis->minimum;
    int64_t maximum = axis->maximum;

    if (raw <= minimum) {
        return 0U;
    }
    if (raw >= maximum) {
        return output_size - 1U;
    }
    return (uint32_t)(((int64_t)raw - minimum) * (output_size - 1U) /
                      (maximum - minimum));
}

static bool
point_in_rect(uint32_t x, uint32_t y, const struct control_rect *rect)
{
    return x >= rect->x && x < rect->x + rect->width &&
           y >= rect->y && y < rect->y + rect->height;
}

static void
emit_control_down(enum control_id control, uint32_t x, uint32_t y)
{
    static const char *const names[] = {
        [CONTROL_UP] = "UP", [CONTROL_DOWN] = "DOWN",
        [CONTROL_LEFT] = "LEFT", [CONTROL_RIGHT] = "RIGHT",
        [CONTROL_A] = "A", [CONTROL_B] = "B", [CONTROL_L] = "L",
        [CONTROL_R] = "R", [CONTROL_START] = "START", [CONTROL_SELECT] = "SELECT"
    };

    printf("CONTROL_DOWN button=%s x=%u y=%u\n", names[control], x, y);
    fflush(stdout);
}

static void
emit_control_up(enum control_id control, uint32_t x, uint32_t y)
{
    static const char *const names[] = {
        [CONTROL_UP] = "UP", [CONTROL_DOWN] = "DOWN",
        [CONTROL_LEFT] = "LEFT", [CONTROL_RIGHT] = "RIGHT",
        [CONTROL_A] = "A", [CONTROL_B] = "B", [CONTROL_L] = "L",
        [CONTROL_R] = "R", [CONTROL_START] = "START", [CONTROL_SELECT] = "SELECT"
    };

    printf("CONTROL_UP button=%s x=%u y=%u\n", names[control], x, y);
    fflush(stdout);
}

static void
emit_control_cancel(enum control_id control)
{
    static const char *const names[] = {
        [CONTROL_UP] = "UP", [CONTROL_DOWN] = "DOWN",
        [CONTROL_LEFT] = "LEFT", [CONTROL_RIGHT] = "RIGHT",
        [CONTROL_A] = "A", [CONTROL_B] = "B", [CONTROL_L] = "L",
        [CONTROL_R] = "R", [CONTROL_START] = "START", [CONTROL_SELECT] = "SELECT"
    };

    printf("CONTROL_CANCEL button=%s\n", names[control]);
    fflush(stdout);
}

static bool
is_interactive_control(enum control_id control)
{
    return control >= CONTROL_UP && control <= CONTROL_SELECT;
}

static bool
point_in_circle(uint32_t x, uint32_t y, const struct control_circle *circle)
{
    int64_t dx = (int64_t)x - circle->center_x;
    int64_t dy = (int64_t)y - circle->center_y;

    return dx * dx + dy * dy <= (int64_t)circle->radius * circle->radius;
}

static enum control_id
control_at_point(const struct screen_layout *layout, uint32_t x, uint32_t y)
{
    uint32_t dpad_center_x = layout->dpad.x + layout->dpad.width / 2U;
    uint32_t dpad_center_y = layout->dpad.y + layout->dpad.height / 2U;
    uint32_t arm_left = dpad_center_x - layout->dpad_arm / 2U;
    uint32_t arm_right = arm_left + layout->dpad_arm;
    uint32_t arm_top = dpad_center_y - layout->dpad_arm / 2U;
    uint32_t arm_bottom = arm_top + layout->dpad_arm;

    if (point_in_rect(x, y, &layout->exit)) {
        return CONTROL_EXIT;
    }
    if ((x >= arm_left && x < arm_right &&
         y >= layout->dpad.y && y < layout->dpad.y + layout->dpad.height) ||
        (y >= arm_top && y < arm_bottom &&
         x >= layout->dpad.x && x < layout->dpad.x + layout->dpad.width)) {
        if (x >= arm_left && x < arm_right &&
            y >= arm_top && y < arm_bottom) {
            return CONTROL_NONE;
        }
        int64_t dx = (int64_t)x - dpad_center_x;
        int64_t dy = (int64_t)y - dpad_center_y;
        if (llabs(dx) > llabs(dy)) {
            return dx < 0 ? CONTROL_LEFT : CONTROL_RIGHT;
        }
        return dy < 0 ? CONTROL_UP : CONTROL_DOWN;
    }
    if (point_in_circle(x, y, &layout->a)) {
        return CONTROL_A;
    }
    if (point_in_circle(x, y, &layout->b)) {
        return CONTROL_B;
    }
    if (point_in_rect(x, y, &layout->l)) {
        return CONTROL_L;
    }
    if (point_in_rect(x, y, &layout->r)) {
        return CONTROL_R;
    }
    if (point_in_rect(x, y, &layout->start)) {
        return CONTROL_START;
    }
    if (point_in_rect(x, y, &layout->select)) {
        return CONTROL_SELECT;
    }
    return CONTROL_NONE;
}

static bool
control_contains_point(const struct screen_layout *layout, enum control_id control,
                       uint32_t x, uint32_t y)
{
    switch (control) {
    case CONTROL_EXIT:
        return point_in_rect(x, y, &layout->exit);
    case CONTROL_A:
        return point_in_circle(x, y, &layout->a);
    case CONTROL_B:
        return point_in_circle(x, y, &layout->b);
    case CONTROL_L:
        return point_in_rect(x, y, &layout->l);
    case CONTROL_R:
        return point_in_rect(x, y, &layout->r);
    case CONTROL_START:
        return point_in_rect(x, y, &layout->start);
    case CONTROL_SELECT:
        return point_in_rect(x, y, &layout->select);
    case CONTROL_UP:
    case CONTROL_DOWN:
    case CONTROL_LEFT:
    case CONTROL_RIGHT:
        return control_at_point(layout, x, y) == control;
    default:
        return false;
    }
}

static void
cancel_primary_control(struct touch_state *touch)
{
    if (is_interactive_control(touch->primary_control) &&
        !touch->primary_cancelled) {
        emit_control_cancel(touch->primary_control);
        touch->primary_cancelled = true;
    }
}

static void
reset_touch_state(struct touch_state *touch)
{
    touch->current_slot = 0U;
    memset(touch->active, 0, sizeof(touch->active));
    memset(touch->new_contact, 0, sizeof(touch->new_contact));
    memset(touch->started_inside, 0, sizeof(touch->started_inside));
    memset(touch->released, 0, sizeof(touch->released));
    memset(touch->ignored, 0, sizeof(touch->ignored));
    touch->single_active = false;
    touch->single_new_contact = false;
    touch->single_started_inside = false;
    touch->single_released = false;
    touch->primary_active = false;
    touch->primary_new_contact = false;
    touch->primary_released = false;
    touch->primary_slot = 0U;
    touch->primary_control = CONTROL_NONE;
    touch->primary_cancelled = false;
}

static int
finish_primary_frame(struct touch_state *touch, uint32_t x, uint32_t y)
{
    if (touch->primary_new_contact) {
        touch->primary_control = control_at_point(touch->layout, x, y);
        touch->primary_new_contact = false;
        if (is_interactive_control(touch->primary_control)) {
            emit_control_down(touch->primary_control, x, y);
        }
    } else if (touch->primary_active &&
               is_interactive_control(touch->primary_control) &&
               !control_contains_point(touch->layout, touch->primary_control, x, y)) {
        cancel_primary_control(touch);
    }

    if (!touch->primary_released) {
        return 0;
    }

    if (touch->primary_control == CONTROL_EXIT &&
        !touch->primary_cancelled &&
        control_contains_point(touch->layout, CONTROL_EXIT, x, y)) {
        return 1;
    }
    if (is_interactive_control(touch->primary_control)) {
        if (!touch->primary_cancelled &&
            control_contains_point(touch->layout, touch->primary_control, x, y)) {
            emit_control_up(touch->primary_control, x, y);
        } else {
            cancel_primary_control(touch);
        }
    }
    touch->primary_active = false;
    touch->primary_released = false;
    touch->primary_control = CONTROL_NONE;
    touch->primary_cancelled = false;
    return 0;
}

static int
handle_touch_event(struct touch_state *touch, const struct input_event *event)
{
    unsigned int slot = touch->current_slot;

    if (event->type == EV_SYN && event->code == SYN_DROPPED) {
        cancel_primary_control(touch);
        reset_touch_state(touch);
        return 0;
    }

    if (touch->device->type_b) {
        if (event->type == EV_ABS && event->code == ABS_MT_SLOT) {
            int64_t selected = (int64_t)event->value - touch->device->slot.minimum;
            touch->current_slot = selected >= 0 && selected < MAX_TOUCH_SLOTS
                                      ? (unsigned int)selected : MAX_TOUCH_SLOTS;
            return 0;
        }
        if (slot >= MAX_TOUCH_SLOTS &&
            !(event->type == EV_SYN && event->code == SYN_REPORT)) {
            return 0;
        }
        if (event->type == EV_ABS && event->code == ABS_MT_POSITION_X) {
            touch->raw_x[slot] = event->value;
        } else if (event->type == EV_ABS && event->code == ABS_MT_POSITION_Y) {
            touch->raw_y[slot] = event->value;
        } else if (event->type == EV_ABS && event->code == ABS_MT_TRACKING_ID) {
            if (event->value >= 0) {
                touch->active[slot] = true;
                touch->new_contact[slot] = true;
                touch->released[slot] = false;
                if (!touch->primary_active && !touch->primary_released &&
                    !touch->ignored[slot]) {
                    touch->primary_active = true;
                    touch->primary_new_contact = true;
                    touch->primary_slot = slot;
                    touch->primary_control = CONTROL_NONE;
                    touch->primary_cancelled = false;
                } else if (slot != touch->primary_slot) {
                    touch->ignored[slot] = true;
                }
            } else {
                touch->active[slot] = false;
                touch->released[slot] = true;
                if (slot == touch->primary_slot) {
                    touch->primary_active = false;
                    touch->primary_released = true;
                } else {
                    touch->ignored[slot] = false;
                }
            }
        }
        if (event->type != EV_SYN || event->code != SYN_REPORT) {
            return 0;
        }

        for (slot = 0U; slot < MAX_TOUCH_SLOTS; slot++) {
            uint32_t x;
            uint32_t y;

            if (slot != touch->primary_slot ||
                (!touch->active[slot] && !touch->released[slot])) {
                continue;
            }
            x = normalize_axis(touch->raw_x[slot], &touch->device->x, touch->layout->width);
            y = normalize_axis(touch->raw_y[slot], &touch->device->y, touch->layout->height);
            touch->new_contact[slot] = false;
            touch->released[slot] = false;
            return finish_primary_frame(touch, x, y);
        }
        return 0;
    }

    if (event->type == EV_ABS && event->code == ABS_X) {
        touch->single_raw_x = event->value;
    } else if (event->type == EV_ABS && event->code == ABS_Y) {
        touch->single_raw_y = event->value;
    } else if (event->type == EV_KEY && event->code == BTN_TOUCH) {
        if (event->value != 0) {
            touch->single_active = true;
            touch->single_new_contact = true;
            touch->single_released = false;
            touch->primary_active = true;
            touch->primary_new_contact = true;
            touch->primary_control = CONTROL_NONE;
            touch->primary_cancelled = false;
        } else {
            touch->single_active = false;
            touch->single_released = true;
            touch->primary_active = false;
            touch->primary_released = true;
        }
    }
    if (event->type == EV_SYN && event->code == SYN_REPORT) {
        uint32_t x = normalize_axis(touch->single_raw_x, &touch->device->x, touch->layout->width);
        uint32_t y = normalize_axis(touch->single_raw_y, &touch->device->y, touch->layout->height);
        touch->single_new_contact = false;
        touch->single_released = false;
        return finish_primary_frame(touch, x, y);
    }
    return 0;
}

static int
wait_for_exit(struct touch_state *touch)
{
    struct pollfd descriptor = {.fd = touch->fd, .events = POLLIN};

    while (!stop_requested) {
        int poll_result = poll(&descriptor, 1U, POLL_TIMEOUT_MS);

        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "inkboy: touchscreen poll failed: %s\n", strerror(errno));
            cancel_primary_control(touch);
            return -1;
        }
        if (poll_result == 0) {
            continue;
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            fprintf(stderr, "inkboy: touchscreen poll error: 0x%x\n", descriptor.revents);
            cancel_primary_control(touch);
            return -1;
        }

        for (;;) {
            struct input_event event;
            ssize_t bytes_read = read(touch->fd, &event, sizeof(event));

            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (bytes_read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "inkboy: touchscreen read failed: %s\n", strerror(errno));
                cancel_primary_control(touch);
                return -1;
            }
            if (bytes_read == 0) {
                cancel_primary_control(touch);
                return -1;
            }
            if ((size_t)bytes_read != sizeof(event)) {
                fprintf(stderr, "inkboy: short touchscreen event read: %zd bytes\n",
                        bytes_read);
                cancel_primary_control(touch);
                return -1;
            }
            if (handle_touch_event(touch, &event) != 0) {
                return 1;
            }
        }
    }
    cancel_primary_control(touch);
    return 0;
}

static int
checked_size(uint32_t width, uint32_t height, size_t *size)
{
    uint64_t pixels;

    if (width == 0U || height == 0U) {
        return -1;
    }

    pixels = (uint64_t)width * (uint64_t)height;
    if (pixels > (uint64_t)(SIZE_MAX / 2U)) {
        return -1;
    }

    *size = (size_t)pixels * 2U;
    return 0;
}

static void
set_pixel(uint8_t *buffer, uint32_t width, uint32_t x, uint32_t y, uint8_t gray)
{
    size_t offset = ((size_t)y * width + x) * 2U;

    /* FBInk's YA input is luminance followed by alpha. */
    buffer[offset] = gray;
    buffer[offset + 1U] = UINT8_MAX;
}

static void
draw_duck(uint8_t *buffer,
          uint32_t screen_width,
          uint32_t origin_x,
          uint32_t origin_y,
          uint32_t size)
{
    uint32_t output_y;

    for (output_y = 0U; output_y < size; output_y++) {
        uint32_t source_y = (output_y * DUCK_BITMAP_HEIGHT) / size;
        uint32_t output_x;

        for (output_x = 0U; output_x < size; output_x++) {
            uint32_t source_x = (output_x * DUCK_BITMAP_WIDTH) / size;
            size_t source_offset =
                (size_t)source_y * DUCK_BITMAP_STRIDE + (source_x / 8U);
            uint8_t source_pixel = duck_bitmap[source_offset];

            if ((source_pixel & (uint8_t)(1U << (7U - (source_x % 8U)))) != 0U) {
                set_pixel(buffer,
                          screen_width,
                          origin_x + output_x,
                          origin_y + output_y,
                          0U);
            }
        }
    }
}

static void
draw_title(uint8_t *buffer, uint32_t screen_width, uint32_t origin_x, uint32_t origin_y)
{
    uint32_t glyph;

    for (glyph = 0U; glyph < TITLE_LENGTH; glyph++) {
        uint32_t row;

        for (row = 0U; row < TITLE_GLYPH_HEIGHT; row++) {
            uint32_t column;

            for (column = 0U; column < TITLE_GLYPH_WIDTH; column++) {
                uint8_t mask = (uint8_t)(1U << (TITLE_GLYPH_WIDTH - 1U - column));

                if ((title_glyphs[glyph][row] & mask) != 0U) {
                    uint32_t scale_y;

                    for (scale_y = 0U; scale_y < TITLE_SCALE; scale_y++) {
                        uint32_t scale_x;

                        for (scale_x = 0U; scale_x < TITLE_SCALE; scale_x++) {
                            set_pixel(buffer,
                                      screen_width,
                                      origin_x + glyph * (TITLE_GLYPH_WIDTH + TITLE_SPACING) * TITLE_SCALE + column * TITLE_SCALE + scale_x,
                                      origin_y + row * TITLE_SCALE + scale_y,
                                      0U);
                        }
                    }
                }
            }
        }
    }
}

static void
draw_label(uint8_t *buffer, uint32_t screen_width, uint32_t origin_x, uint32_t origin_y,
           const char *text, uint32_t scale)
{
    uint32_t glyph_index;

    for (glyph_index = 0U; text[glyph_index] != '\0'; glyph_index++) {
        const uint8_t *glyph = label_glyph(text[glyph_index]);
        uint32_t row;

        if (glyph == NULL) {
            continue;
        }
        for (row = 0U; row < TITLE_GLYPH_HEIGHT; row++) {
            uint32_t column;
            for (column = 0U; column < TITLE_GLYPH_WIDTH; column++) {
                if ((glyph[row] & (uint8_t)(1U << (TITLE_GLYPH_WIDTH - 1U - column))) != 0U) {
                    uint32_t y;
                    for (y = 0U; y < scale; y++) {
                        uint32_t x;
                        for (x = 0U; x < scale; x++) {
                            set_pixel(buffer, screen_width,
                                      origin_x + glyph_index * (TITLE_GLYPH_WIDTH + 1U) * scale + column * scale + x,
                                      origin_y + row * scale + y, 0U);
                        }
                    }
                }
            }
        }
    }
}

static void
draw_rect(uint8_t *buffer, uint32_t width, uint32_t x, uint32_t y, uint32_t rect_width,
          uint32_t rect_height)
{
    uint32_t offset;
    uint32_t thickness;

    for (offset = 0U; offset < rect_width; offset++) {
        for (thickness = 0U; thickness < CONTROL_LINE_WIDTH && thickness < rect_height; thickness++) {
            set_pixel(buffer, width, x + offset, y + thickness, 0U);
            set_pixel(buffer, width, x + offset, y + rect_height - 1U - thickness, 0U);
        }
    }
    for (offset = 1U; offset + 1U < rect_height; offset++) {
        for (thickness = 0U; thickness < CONTROL_LINE_WIDTH && thickness < rect_width; thickness++) {
            set_pixel(buffer, width, x + thickness, y + offset, 0U);
            set_pixel(buffer, width, x + rect_width - 1U - thickness, y + offset, 0U);
        }
    }
}

static void
draw_circle(uint8_t *buffer, uint32_t width, uint32_t center_x, uint32_t center_y, uint32_t radius)
{
    uint32_t y;
    uint32_t inner_radius = radius > CONTROL_LINE_WIDTH ? radius - CONTROL_LINE_WIDTH : 0U;
    uint32_t inner_radius_squared = inner_radius * inner_radius;
    uint32_t radius_squared = radius * radius;

    for (y = 0U; y <= radius * 2U; y++) {
        uint32_t x;
        for (x = 0U; x <= radius * 2U; x++) {
            int32_t dx = (int32_t)x - (int32_t)radius;
            int32_t dy = (int32_t)y - (int32_t)radius;
            uint32_t distance = (uint32_t)(dx * dx + dy * dy);
            if (distance <= radius_squared && distance >= inner_radius_squared) {
                set_pixel(buffer, width, center_x + x - radius, center_y + y - radius, 0U);
            }
        }
    }
}

static uint32_t
percent_of(uint32_t value, uint32_t percent)
{
    return (uint32_t)(((uint64_t)value * percent) / 100U);
}

static uint32_t
label_width(const char *text, uint32_t scale)
{
    uint32_t length = 0U;

    while (text[length] != '\0') {
        length++;
    }
    return length * (TITLE_GLYPH_WIDTH + 1U) * scale;
}

static void
draw_centered_label(uint8_t *buffer, uint32_t width, const struct control_rect *rect,
                   const char *text, uint32_t scale)
{
    uint32_t text_width = label_width(text, scale);
    uint32_t text_height = TITLE_GLYPH_HEIGHT * scale;

    draw_label(buffer, width,
               rect->x + (rect->width - text_width) / 2U,
               rect->y + (rect->height - text_height) / 2U,
               text, scale);
}

static void
draw_centered_circle_label(uint8_t *buffer, uint32_t width,
                           const struct control_circle *circle,
                           const char *text, uint32_t scale)
{
    uint32_t text_width = label_width(text, scale);
    uint32_t text_height = TITLE_GLYPH_HEIGHT * scale;

    draw_label(buffer, width,
               circle->center_x - text_width / 2U,
               circle->center_y - text_height / 2U,
               text, scale);
}

static void
draw_controls(uint8_t *buffer, const struct screen_layout *layout)
{
    uint32_t dpad_x = layout->dpad.x + (layout->dpad.width - layout->dpad_arm) / 2U;
    uint32_t dpad_y = layout->dpad.y + (layout->dpad.height - layout->dpad_arm) / 2U;

    draw_rect(buffer, layout->width, dpad_x, layout->dpad.y,
              layout->dpad_arm, layout->dpad.height);
    draw_rect(buffer, layout->width, layout->dpad.x, dpad_y,
              layout->dpad.width, layout->dpad_arm);
    draw_circle(buffer, layout->width, layout->a.center_x, layout->a.center_y, layout->a.radius);
    draw_circle(buffer, layout->width, layout->b.center_x, layout->b.center_y, layout->b.radius);
    draw_centered_circle_label(buffer, layout->width, &layout->a, "A", CONTROL_SCALE);
    draw_centered_circle_label(buffer, layout->width, &layout->b, "B", CONTROL_SCALE);
    draw_rect(buffer, layout->width, layout->l.x, layout->l.y,
              layout->l.width, layout->l.height);
    draw_rect(buffer, layout->width, layout->r.x, layout->r.y,
              layout->r.width, layout->r.height);
    draw_centered_label(buffer, layout->width, &layout->l, "L", CONTROL_SCALE);
    draw_centered_label(buffer, layout->width, &layout->r, "R", CONTROL_SCALE);
    draw_rect(buffer, layout->width, layout->select.x, layout->select.y,
              layout->select.width, layout->select.height);
    draw_rect(buffer, layout->width, layout->start.x, layout->start.y,
              layout->start.width, layout->start.height);
    draw_centered_label(buffer, layout->width, &layout->select, "SELECT", 3U);
    draw_centered_label(buffer, layout->width, &layout->start, "START", 3U);
}

static int
build_screen(uint8_t **buffer_out, uint32_t width, uint32_t height,
             struct screen_layout *layout)
{
    size_t buffer_size;
    uint8_t *buffer;

    memset(layout, 0, sizeof(*layout));
    layout->width = width;
    layout->height = height;
    layout->header_bottom = percent_of(height, HEADER_SAFE_PERCENT);
    layout->main_bottom = percent_of(height, 55U);
    layout->primary_bottom = percent_of(height, 90U);
    layout->title_x = percent_of(width, HEADER_SIDE_MARGIN_PERCENT);
    layout->title_y = layout->header_bottom + percent_of(height, HEADER_CONTENT_OFFSET_PERCENT);
    layout->exit.width = percent_of(width, EXIT_TOUCH_WIDTH_PERCENT);
    layout->exit.height = percent_of(height, EXIT_TOUCH_HEIGHT_PERCENT);
    layout->exit.x = width - percent_of(width, HEADER_SIDE_MARGIN_PERCENT) - layout->exit.width;
    layout->exit.y = layout->header_bottom + percent_of(height, HEADER_CONTENT_OFFSET_PERCENT);

    layout->duck_size = percent_of(width, 38U);
    if (layout->duck_size > DUCK_MAX_SIZE) {
        layout->duck_size = DUCK_MAX_SIZE;
    }
    if (layout->duck_size < DUCK_MIN_SIZE) {
        layout->duck_size = DUCK_MIN_SIZE;
    }
    if (layout->duck_size > percent_of(layout->main_bottom - layout->header_bottom, 70U)) {
        layout->duck_size = percent_of(layout->main_bottom - layout->header_bottom, 70U);
    }
    layout->duck_x = (width - layout->duck_size) / 2U;
    layout->duck_y = layout->header_bottom +
                     (layout->main_bottom - layout->header_bottom - layout->duck_size) / 2U;

    layout->dpad.width = percent_of(width, 36U);
    layout->dpad.height = layout->dpad.width;
    layout->dpad.x = percent_of(width, 7U);
    layout->dpad.y = layout->main_bottom +
                     percent_of(layout->primary_bottom - layout->main_bottom, 22U);
    layout->dpad_arm = percent_of(layout->dpad.width, 34U);

    layout->a.radius = percent_of(width, 15U) / 2U;
    layout->a.center_x = percent_of(width, 79U);
    layout->a.center_y = layout->main_bottom +
                         percent_of(layout->primary_bottom - layout->main_bottom, 36U);
    layout->b.radius = layout->a.radius;
    layout->b.center_x = percent_of(width, 71U);
    layout->b.center_y = layout->main_bottom +
                         percent_of(layout->primary_bottom - layout->main_bottom, 70U);

    layout->l.width = percent_of(width, 27U);
    layout->l.height = percent_of(height, 6U);
    layout->l.x = percent_of(width, 5U);
    layout->l.y = layout->main_bottom + percent_of(height, 1U);
    layout->r = layout->l;
    layout->r.x = width - percent_of(width, 5U) - layout->r.width;

    layout->select.width = percent_of(width, 20U);
    layout->select.height = percent_of(height, 6U);
    layout->select.x = percent_of(width, 29U);
    layout->select.y = layout->primary_bottom + percent_of(height, 2U);
    layout->start = layout->select;
    layout->start.x = percent_of(width, 51U);

    if (width == 0U || height == 0U ||
        layout->duck_size < DUCK_MIN_SIZE ||
        layout->main_bottom <= layout->header_bottom ||
        layout->primary_bottom <= layout->main_bottom ||
        layout->primary_bottom + percent_of(height, 10U) > height ||
        layout->title_x +
            (TITLE_LENGTH * TITLE_GLYPH_WIDTH + (TITLE_LENGTH - 1U) * TITLE_SPACING) * TITLE_SCALE > width ||
        layout->exit.x + layout->exit.width > width ||
        layout->dpad.y + layout->dpad.height > layout->primary_bottom ||
        layout->r.x + layout->r.width > width ||
        layout->start.x + layout->start.width > width) {
        fprintf(stderr, "inkboy: screen is too small for the composition\n");
        return -1;
    }

    if (checked_size(width, height, &buffer_size) != 0) {
        fprintf(stderr, "inkboy: framebuffer dimensions overflow memory size\n");
        return -1;
    }

    buffer = malloc(buffer_size);
    if (buffer == NULL) {
        fprintf(stderr, "inkboy: unable to allocate %zu-byte screen buffer: %s\n",
                buffer_size,
                strerror(errno));
        return -1;
    }

    for (size_t offset = 0U; offset < buffer_size; offset += 2U) {
        buffer[offset] = UINT8_MAX;
        buffer[offset + 1U] = UINT8_MAX;
    }

    draw_title(buffer, width, layout->title_x, layout->title_y);
    draw_duck(buffer, width, layout->duck_x, layout->duck_y, layout->duck_size);
    draw_controls(buffer, layout);
    draw_centered_label(buffer, width, &layout->exit, "EXIT", CONTROL_SCALE);

    *buffer_out = buffer;
    return 0;
}

static void
free_dump(FBInkDump *dump, int fbfd)
{
    if (dump->data != NULL && fbink_free_dump_data(dump) != 0) {
        fprintf(stderr, "inkboy: warning: unable to free framebuffer dump\n");
    }
    if (fbink_close(fbfd) != 0) {
        fprintf(stderr, "inkboy: warning: unable to close framebuffer\n");
    }
}

int
main(void)
{
    FBInkConfig config = {0};
    FBInkConfig refresh_config = {0};
    FBInkDump dump = {0};
    FBInkState state = {0};
    uint8_t *buffer = NULL;
    struct screen_layout layout;
    struct touch_device touch_device;
    struct touch_state touch = {.fd = -1};
    size_t buffer_size;
    int fbfd = FBFD_AUTO;
    int exit_code = EXIT_FAILURE;
    int input_result = 0;
    int result;

    if (install_signal_handlers() != 0) {
        fprintf(stderr, "inkboy: unable to install signal handlers: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (discover_touch(&touch_device) != 0 ||
        open_touch(&touch, &touch_device) != 0) {
        close_touch(&touch);
        return EXIT_FAILURE;
    }

    fbfd = fbink_open();
    if (fbfd < 0) {
        fprintf(stderr, "inkboy: fbink_open failed: %s\n", strerror(errno));
        close_touch(&touch);
        return EXIT_FAILURE;
    }

    config.is_quiet = true;
    result = fbink_init(fbfd, &config);
    if (result != 0) {
        fprintf(stderr, "inkboy: fbink_init failed: %d\n", result);
        close_touch(&touch);
        free_dump(&dump, fbfd);
        return EXIT_FAILURE;
    }

    fbink_get_state(&config, &state);
    if (state.view_width == 0U || state.view_height == 0U) {
        fprintf(stderr, "inkboy: FBInk returned an invalid screen geometry\n");
        close_touch(&touch);
        free_dump(&dump, fbfd);
        return EXIT_FAILURE;
    }

    if (checked_size(state.view_width, state.view_height, &buffer_size) != 0 ||
        build_screen(&buffer, state.view_width, state.view_height, &layout) != 0) {
        close_touch(&touch);
        free_dump(&dump, fbfd);
        return EXIT_FAILURE;
    }
    touch.layout = &layout;

    result = fbink_dump(fbfd, &dump);
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to save the previous screen: %d\n", result);
        free(buffer);
        close_touch(&touch);
        free_dump(&dump, fbfd);
        return EXIT_FAILURE;
    }

    config.no_refresh = true;
    result = fbink_print_raw_data(fbfd,
                                  buffer,
                                  (int)state.view_width,
                                  (int)state.view_height,
                                  buffer_size,
                                  0,
                                  0,
                                  &config);
    free(buffer);
    buffer = NULL;
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to draw the composition: %d\n", result);
        (void)fbink_restore(fbfd, &refresh_config, &dump);
        close_touch(&touch);
        free_dump(&dump, fbfd);
        return EXIT_FAILURE;
    }

    refresh_config.is_flashing = true;
    result = fbink_refresh(fbfd, 0U, 0U, 0U, 0U, &refresh_config);
    if (result != 0) {
        fprintf(stderr, "inkboy: unable to refresh the composition: %d\n", result);
        (void)fbink_restore(fbfd, &refresh_config, &dump);
        close_touch(&touch);
        free_dump(&dump, fbfd);
        return EXIT_FAILURE;
    }
    (void)fbink_wait_for_complete(fbfd, LAST_MARKER);

    fprintf(stdout,
            "InkBoy rendered with FBInk %s on %ux%u; waiting for EXIT or termination\n",
            fbink_version(),
            state.view_width,
            state.view_height);
    fflush(stdout);

    input_result = wait_for_exit(&touch);
    close_touch(&touch);

    result = fbink_restore(fbfd, &refresh_config, &dump);
    if (result != 0) {
        fprintf(stderr, "inkboy: warning: screen restoration failed: %d\n", result);
    } else {
        (void)fbink_wait_for_complete(fbfd, LAST_MARKER);
        if (input_result >= 0) {
            exit_code = EXIT_SUCCESS;
        }
    }

    free_dump(&dump, fbfd);
    return exit_code;
}
