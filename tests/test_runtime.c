#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <signal.h>
#include <time.h>

static int fake_clock_gettime(clockid_t clock_id, struct timespec *value);
static int fake_nanosleep(const struct timespec *request, struct timespec *remaining);

/* Compile the actual application loop with hardware adapters and a fast clock. */
#define clock_gettime fake_clock_gettime
#define nanosleep fake_nanosleep
#define main inkboy_application_main
#include "../src/main.c"
#undef main
#undef clock_gettime
#undef nanosleep

struct inkboy_input { int unused; };
struct inkboy_libretro { int unused; };
struct inkboy_renderer { int unused; };

static struct inkboy_input fake_input;
static struct inkboy_libretro fake_core;
static struct inkboy_renderer fake_renderer;
static struct inkboy_layout fake_layout;
static enum inkboy_input_result terminal_input;
static unsigned stop_at_poll;
static unsigned poll_count;
static unsigned run_count;
static unsigned present_count;
static unsigned fail_at_present;
static unsigned fail_at_run;
static int signal_at_poll;
static bool signal_in_sleep;
static bool fail_open;
static bool fail_restore;
static unsigned cleanup_count;
static char cleanup_order[4];
static long fake_ns;

static int fake_clock_gettime(clockid_t clock_id, struct timespec *value)
{
    (void)clock_id;
    fake_ns += 20000000L;
    *value = (struct timespec){100 + fake_ns / 1000000000L, fake_ns % 1000000000L};
    return 0;
}

static int fake_nanosleep(const struct timespec *request, struct timespec *remaining)
{
    (void)request;
    (void)remaining;
    if (signal_in_sleep) {
        signal_in_sleep = false;
        assert(raise(SIGTERM) == 0);
        errno = EINTR;
        return -1;
    }
    return 0;
}

int inkboy_input_open(struct inkboy_input **out, const struct inkboy_layout *layout,
                      inkboy_input_callback callback, void *userdata)
{
    (void)layout; (void)callback; (void)userdata;
    *out = &fake_input;
    return 0;
}

enum inkboy_input_result inkboy_input_poll(struct inkboy_input *input)
{
    assert(input == &fake_input);
    poll_count++;
    if (poll_count == stop_at_poll) {
        if (signal_at_poll) assert(raise(signal_at_poll) == 0);
        return terminal_input;
    }
    assert(poll_count < 300U);
    return INKBOY_INPUT_OK;
}

void inkboy_input_close(struct inkboy_input *input)
{
    (void)input;
    cleanup_order[cleanup_count++] = 'I';
}

int inkboy_libretro_open(struct inkboy_libretro **out, const char *core, const char *rom)
{
    (void)core; (void)rom;
    *out = &fake_core;
    return fail_open ? -1 : 0;
}

int inkboy_libretro_run(struct inkboy_libretro *core)
{
    assert(core == &fake_core);
    run_count++;
    return run_count == fail_at_run ? -1 : 0;
}

bool inkboy_libretro_frame(struct inkboy_libretro *core, struct inkboy_video_frame *frame)
{
    (void)core;
    memset(frame, 0, sizeof(*frame));
    return true;
}

double inkboy_libretro_fps(const struct inkboy_libretro *core)
{
    (void)core;
    return signal_in_sleep ? 1.0 : 60.0;
}

void inkboy_libretro_set_control(struct inkboy_libretro *core,
                                 enum inkboy_control control, bool pressed)
{
    (void)core; (void)control; (void)pressed;
}

void inkboy_libretro_close(struct inkboy_libretro *core)
{
    (void)core;
    cleanup_order[cleanup_count++] = 'C';
}

int inkboy_renderer_open(struct inkboy_renderer **out)
{
    *out = &fake_renderer;
    return 0;
}

const struct inkboy_layout *inkboy_renderer_layout(const struct inkboy_renderer *renderer)
{
    (void)renderer;
    return &fake_layout;
}

int inkboy_renderer_present_frame(struct inkboy_renderer *renderer,
                                  const struct inkboy_video_frame *frame)
{
    (void)renderer; (void)frame;
    present_count++;
    return present_count == fail_at_present ? -1 : 0;
}

int inkboy_renderer_close(struct inkboy_renderer *renderer)
{
    (void)renderer;
    cleanup_order[cleanup_count++] = 'R';
    return fail_restore ? -1 : 0;
}

static void reset(void)
{
    terminal_input = INKBOY_INPUT_STOP;
    stop_at_poll = 183U;
    poll_count = run_count = present_count = cleanup_count = 0U;
    fail_at_present = fail_at_run = 0U;
    fail_open = fail_restore = signal_in_sleep = false;
    signal_at_poll = 0;
    fake_ns = 0;
    memset(cleanup_order, 0, sizeof(cleanup_order));
}

static void check(int expected)
{
    assert(inkboy_application_main() == expected);
    assert(cleanup_count == 3U);
    assert(strcmp(cleanup_order, "ICR") == 0);
}

int main(void)
{
    reset(); check(EXIT_SUCCESS);
    assert(run_count == stop_at_poll - 1U && present_count == 1U);
    reset(); stop_at_poll = 1U; check(EXIT_SUCCESS);
    assert(run_count == 0U && present_count == 0U);
    reset(); terminal_input = INKBOY_INPUT_ERROR; check(EXIT_FAILURE);
    reset(); terminal_input = INKBOY_INPUT_ERROR; stop_at_poll = 1U; check(EXIT_FAILURE);
    assert(run_count == 0U);
    const int signals[] = {SIGINT, SIGTERM, SIGHUP};
    for (unsigned i = 0U; i < sizeof(signals) / sizeof(signals[0]); i++) {
        reset(); terminal_input = INKBOY_INPUT_OK; signal_at_poll = signals[i];
        check(EXIT_SUCCESS);
        assert(run_count == stop_at_poll - 1U);
        reset(); terminal_input = INKBOY_INPUT_OK; signal_at_poll = signals[i];
        stop_at_poll = 1U; check(EXIT_SUCCESS);
        assert(run_count == 0U && present_count == 0U);
    }
    reset(); signal_in_sleep = true; check(EXIT_SUCCESS);
    assert(present_count == 1U && poll_count == 181U);
    reset(); fail_at_present = 1U; check(EXIT_FAILURE);
    reset(); fail_at_present = 2U; stop_at_poll = 240U; check(EXIT_FAILURE);
    reset(); fail_at_run = 2U; check(EXIT_FAILURE);
    reset(); fail_at_run = 181U; check(EXIT_FAILURE);
    reset(); fail_open = true; check(EXIT_FAILURE);
    reset(); fail_restore = true; check(EXIT_FAILURE);
    puts("runtime: EXIT/errors/signals/warmup/sleep/cleanup pass");
    return 0;
}
