#define _POSIX_C_SOURCE 200809L

/* Pipe-backed evdev stream exercises the real parser and poll error paths. */
#include "../src/input.c"

#include <assert.h>

static unsigned down_events;
static unsigned up_events;
static unsigned cancel_events;

static void capture(const struct inkboy_input_event *event, void *userdata)
{
    (void)userdata;
    down_events += event->phase == INKBOY_INPUT_DOWN;
    up_events += event->phase == INKBOY_INPUT_UP;
    cancel_events += event->phase == INKBOY_INPUT_CANCEL;
}

static void send_event(int writer, unsigned short type, unsigned short code, int value)
{
    struct input_event event = {.type = type, .code = code, .value = value};
    assert(write(writer, &event, sizeof(event)) == sizeof(event));
}

static void position(int writer, bool type_b, int x, int y)
{
    send_event(writer, EV_ABS, type_b ? ABS_MT_POSITION_X : ABS_X, x);
    send_event(writer, EV_ABS, type_b ? ABS_MT_POSITION_Y : ABS_Y, y);
}

static enum inkboy_input_result contact(struct inkboy_input *input, int writer,
                                        bool down, int x, int y)
{
    position(writer, input->device.type_b, x, y);
    send_event(writer, input->device.type_b ? EV_ABS : EV_KEY,
               input->device.type_b ? ABS_MT_TRACKING_ID : BTN_TOUCH,
               input->device.type_b ? (down ? 42 : -1) : down);
    send_event(writer, EV_SYN, SYN_REPORT, 0);
    return inkboy_input_poll(input);
}

static void test_stream(bool type_b)
{
    struct inkboy_layout layout;
    struct inkboy_input *input = calloc(1U, sizeof(*input));
    int descriptors[2];
    int x;
    int y;

    assert(input != NULL);
    assert(inkboy_layout_build(&layout, 1236U, 1648U) == 0);
    assert(pipe(descriptors) == 0);
    assert(fcntl(descriptors[0], F_SETFL, O_NONBLOCK) == 0);
    input->fd = descriptors[0];
    input->layout = &layout;
    input->device.type_b = type_b;
    input->device.x.maximum = (int)layout.width - 1;
    input->device.y.maximum = (int)layout.height - 1;
    input->callback = capture;
    down_events = up_events = cancel_events = 0U;
    assert(inkboy_input_poll(input) == INKBOY_INPUT_OK);
    assert(contact(input, descriptors[1], true, layout.a.center_x, layout.a.center_y) == INKBOY_INPUT_OK);
    assert(contact(input, descriptors[1], false, layout.a.center_x, layout.a.center_y) == INKBOY_INPUT_OK);
    assert(down_events == 1U && up_events == 1U && cancel_events == 0U);

    x = (int)(layout.exit.x + layout.exit.width / 2U);
    y = (int)(layout.exit.y + layout.exit.height / 2U);
    assert(contact(input, descriptors[1], true, x, y) == INKBOY_INPUT_OK);
    assert(contact(input, descriptors[1], false, x, y) == INKBOY_INPUT_STOP);
    assert(!input->primary_active && input->primary_control == INKBOY_CONTROL_NONE);
    assert(down_events == 1U && up_events == 1U);

    /* Leaving EXIT cancels the gesture even if the finger moves back in. */
    assert(contact(input, descriptors[1], true, x, y) == INKBOY_INPUT_OK);
    position(descriptors[1], type_b, 0, 0);
    send_event(descriptors[1], EV_SYN, SYN_REPORT, 0);
    assert(inkboy_input_poll(input) == INKBOY_INPUT_OK);
    assert(contact(input, descriptors[1], false, x, y) == INKBOY_INPUT_OK);

    assert(contact(input, descriptors[1], true, layout.a.center_x, layout.a.center_y) == INKBOY_INPUT_OK);
    send_event(descriptors[1], EV_SYN, SYN_DROPPED, 0);
    assert(inkboy_input_poll(input) == INKBOY_INPUT_OK);
    assert(cancel_events == 1U);
    assert(contact(input, descriptors[1], true, layout.a.center_x, layout.a.center_y) == INKBOY_INPUT_OK);
    assert(close(descriptors[1]) == 0);
    assert(inkboy_input_poll(input) == INKBOY_INPUT_ERROR);
    assert(cancel_events == 2U);
    inkboy_input_close(input);
}

static void test_read_errors(void)
{
    int descriptors[2];
    struct inkboy_input *input = calloc(1U, sizeof(*input));
    const char short_event = 0;
    volatile sig_atomic_t stopped = 1;

    assert(input != NULL && pipe(descriptors) == 0);
    input->fd = descriptors[0];
    assert(inkboy_input_wait(input, &stopped) == INKBOY_INPUT_OK);
    assert(write(descriptors[1], &short_event, 1U) == 1);
    assert(inkboy_input_poll(input) == INKBOY_INPUT_ERROR);
    assert(close(descriptors[0]) == 0);
    assert(inkboy_input_poll(input) == INKBOY_INPUT_ERROR);
    input->fd = -1;
    inkboy_input_close(input);
    assert(close(descriptors[1]) == 0);
}

int main(void)
{
    test_stream(true);
    test_stream(false);
    test_read_errors();
    assert(inkboy_input_poll(NULL) == INKBOY_INPUT_ERROR);
    assert(inkboy_input_wait(NULL, NULL) == INKBOY_INPUT_ERROR);
    puts("input: EXIT, cancelled gestures, controls, SYN_DROPPED and POLLHUP pass");
    return 0;
}
