#define _POSIX_C_SOURCE 200809L

#include "profile.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

struct inkboy_profile {
    struct inkboy_profile_sample stages[INKBOY_PROFILE_STAGE_COUNT];
    uint64_t counters[INKBOY_PROFILE_COUNTER_COUNT];
};

static struct inkboy_profile inkboy_profile;

struct active_timer {
    uint64_t id;
    uint64_t started_ns;
    enum inkboy_profile_stage stage;
};

static struct active_timer active_timers[32];
static uint64_t next_timer_id = 1U;

static const char *phase_name = "startup";
static uint64_t started;
static uint64_t previous_report;
static double initial_cpu;

static void inkboy_profile_count(enum inkboy_profile_counter counter)
{
    if ((unsigned)counter < INKBOY_PROFILE_COUNTER_COUNT) {
        inkboy_profile.counters[counter]++;
    }
}

void inkboy_profile_frame_received(enum inkboy_profile_frame_result result)
{
    inkboy_profile_count(INKBOY_PROFILE_CALLBACKS);
    switch (result) {
    case INKBOY_PROFILE_FRAME_VALID: inkboy_profile_count(INKBOY_PROFILE_VALID_FRAMES); break;
    case INKBOY_PROFILE_FRAME_DUPLICATE: inkboy_profile_count(INKBOY_PROFILE_DUPLICATE_FRAMES); break;
    case INKBOY_PROFILE_FRAME_INVALID: inkboy_profile_count(INKBOY_PROFILE_INVALID_FRAMES); break;
    }
}

void inkboy_profile_frame_superseded(void)
{
    inkboy_profile_count(INKBOY_PROFILE_SUPERSEDED_FRAMES);
}

void inkboy_profile_emulation_ran(void) { inkboy_profile_count(INKBOY_PROFILE_RUNS); }
void inkboy_profile_refresh_attempted(void) { inkboy_profile_count(INKBOY_PROFILE_REFRESH_ATTEMPTS); }
void inkboy_profile_image_unchanged(void) { inkboy_profile_count(INKBOY_PROFILE_UNCHANGED_IMAGES); }

void inkboy_profile_refresh_failed(enum inkboy_profile_presentation kind)
{
    inkboy_profile_count(INKBOY_PROFILE_REFRESH_FAILURES);
    inkboy_profile_count(kind == INKBOY_PROFILE_PRESENT_INITIAL ? INKBOY_PROFILE_INITIAL_REFRESH_FAILURES :
                         kind == INKBOY_PROFILE_PRESENT_CLEANUP ? INKBOY_PROFILE_CLEANUP_REFRESH_FAILURES :
                         INKBOY_PROFILE_NORMAL_REFRESH_FAILURES);
}

void inkboy_profile_frame_presented(enum inkboy_profile_presentation kind)
{
    inkboy_profile_count(INKBOY_PROFILE_PRESENTATIONS);
    if (kind == INKBOY_PROFILE_PRESENT_INITIAL) inkboy_profile_count(INKBOY_PROFILE_INITIAL_PRESENTATIONS);
    if (kind == INKBOY_PROFILE_PRESENT_NORMAL) inkboy_profile_count(INKBOY_PROFILE_NORMAL_PRESENTATIONS);
    if (kind == INKBOY_PROFILE_PRESENT_CLEANUP) inkboy_profile_count(INKBOY_PROFILE_CLEANUPS);
    inkboy_profile_report(kind == INKBOY_PROFILE_PRESENT_CLEANUP ? "cleanup" : "presentation", "running", 0);
}

uint64_t inkboy_profile_counter_value(enum inkboy_profile_counter counter)
{
    return (unsigned)counter < INKBOY_PROFILE_COUNTER_COUNT
        ? inkboy_profile.counters[counter] : 0U;
}

struct inkboy_profile_sample inkboy_profile_stage_value(enum inkboy_profile_stage stage)
{
    if ((unsigned)stage >= INKBOY_PROFILE_STAGE_COUNT) return (struct inkboy_profile_sample){0};
    return inkboy_profile.stages[stage];
}

uint64_t inkboy_profile_now(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0U;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static void inkboy_profile_record(enum inkboy_profile_stage stage, uint64_t start)
{
    uint64_t end = inkboy_profile_now();
    struct inkboy_profile_sample *sample;
    uint64_t duration;

    if ((unsigned)stage >= INKBOY_PROFILE_STAGE_COUNT || start == 0U || end < start) {
        return;
    }
    duration = end - start;
    sample = &inkboy_profile.stages[stage];
    sample->total_ns += duration;
    sample->last_ns = duration;
    sample->count++;
    if (duration > sample->max_ns) {
        sample->max_ns = duration;
    }
}

struct inkboy_profile_timer inkboy_profile_timer_start(enum inkboy_profile_stage stage)
{
    struct inkboy_profile_timer timer = {0U};
    if ((unsigned)stage >= INKBOY_PROFILE_STAGE_COUNT) return timer;
    for (size_t index = 0U; index < sizeof(active_timers) / sizeof(active_timers[0]); index++) {
        if (active_timers[index].id == 0U) {
            uint64_t id = next_timer_id++;
            if (id == 0U) id = next_timer_id++;
            active_timers[index] = (struct active_timer){id, inkboy_profile_now(), stage};
            timer.id = id;
            return timer;
        }
    }
    return timer;
}

static struct active_timer *find_timer(struct inkboy_profile_timer timer)
{
    if (timer.id == 0U) return NULL;
    for (size_t index = 0U; index < sizeof(active_timers) / sizeof(active_timers[0]); index++) {
        if (active_timers[index].id == timer.id) return &active_timers[index];
    }
    return NULL;
}

static void stop_timer(struct inkboy_profile_timer timer, enum inkboy_profile_stage stage)
{
    struct active_timer *active = find_timer(timer);
    if (active == NULL) return;
    inkboy_profile_record(stage, active->started_ns);
    active->id = 0U;
}

void inkboy_profile_timer_stop(struct inkboy_profile_timer timer)
{
    struct active_timer *active = find_timer(timer);
    if (active != NULL) stop_timer(timer, active->stage);
}

void inkboy_profile_timer_cancel(struct inkboy_profile_timer timer)
{
    struct active_timer *active = find_timer(timer);
    if (active != NULL) active->id = 0U;
}

void inkboy_profile_timer_stop_as(struct inkboy_profile_timer timer,
                                  enum inkboy_profile_stage stage)
{
    stop_timer(timer, stage);
}

uint64_t inkboy_profile_timer_elapsed_ns(struct inkboy_profile_timer timer)
{
    struct active_timer *active = find_timer(timer);
    uint64_t now = inkboy_profile_now();
    return active != NULL && now >= active->started_ns ? now - active->started_ns : 0U;
}

static double cpu_seconds(const struct rusage *usage)
{
    return usage->ru_utime.tv_sec + usage->ru_stime.tv_sec +
           (usage->ru_utime.tv_usec + usage->ru_stime.tv_usec) / 1000000.0;
}

void inkboy_profile_begin(const char *phase)
{
    struct rusage usage = {0};

    memset(&inkboy_profile, 0, sizeof(inkboy_profile));
    phase_name = phase;
    started = inkboy_profile_now();
    previous_report = started;
    (void)getrusage(RUSAGE_SELF, &usage);
    initial_cpu = cpu_seconds(&usage);
}

void inkboy_profile_startup(void) { inkboy_profile_begin("startup"); }
void inkboy_profile_warmup(void) { inkboy_profile_begin("warmup"); }

void inkboy_profile_begin_gameplay(void)
{
    inkboy_profile_report("summary", "warmup-complete", 0);
    inkboy_profile_begin("gameplay");
}

void inkboy_profile_finish(const char *reason, int status)
{
    inkboy_profile_report("summary", reason, status);
}

static const char *const profile_stage_names[INKBOY_PROFILE_STAGE_COUNT] = {
    [INKBOY_PROFILE_EMULATION] = "emulation",
    [INKBOY_PROFILE_INPUT] = "input_poll",
    [INKBOY_PROFILE_INPUT_GAP] = "input_gap",
    [INKBOY_PROFILE_CALLBACK_COPY] = "callback_copy",
    [INKBOY_PROFILE_CONVERT] = "convert",
    [INKBOY_PROFILE_SCALE] = "scale",
    [INKBOY_PROFILE_COMPOSE] = "compose",
    [INKBOY_PROFILE_DRAW] = "fbink_draw",
    [INKBOY_PROFILE_REFRESH_SUBMIT] = "refresh_submit",
    [INKBOY_PROFILE_REFRESH_WAIT] = "refresh_wait",
    [INKBOY_PROFILE_INPUT_TO_STATE] = "input_to_state",
    [INKBOY_PROFILE_INITIAL_SUBMIT] = "initial_submit",
    [INKBOY_PROFILE_INITIAL_WAIT] = "initial_wait",
    [INKBOY_PROFILE_CLEAN_SUBMIT] = "clean_submit",
    [INKBOY_PROFILE_CLEAN_WAIT] = "clean_wait",
    [INKBOY_PROFILE_INPUT_GAP_CLEAN] = "input_gap_clean",
    [INKBOY_PROFILE_RESTORE] = "restore",
    [INKBOY_PROFILE_RESTORE_WAIT] = "restore_wait"
};

static void
report_runtime_metrics(double elapsed, double cpu_percent,
                       unsigned long rss_kib, long peak_rss_kib,
                       const char *reason, int status)
{
    const uint64_t *counters = inkboy_profile.counters;
    fprintf(stdout,
            " runs=%" PRIu64 " callbacks=%" PRIu64 " valid_frames=%" PRIu64
            " duplicate_frames=%" PRIu64 " invalid_frames=%" PRIu64
            " superseded_frames=%" PRIu64 " presentations=%" PRIu64
            " refresh_attempts=%" PRIu64 " refresh_failures=%" PRIu64
            " emu_fps=%.3f display_fps=%.3f cpu_pct=%.3f"
            " rss_kib=%lu peak_rss_kib=%ld reason=%s status=%d",
            counters[INKBOY_PROFILE_RUNS],
            counters[INKBOY_PROFILE_CALLBACKS],
            counters[INKBOY_PROFILE_VALID_FRAMES],
            counters[INKBOY_PROFILE_DUPLICATE_FRAMES],
            counters[INKBOY_PROFILE_INVALID_FRAMES],
            counters[INKBOY_PROFILE_SUPERSEDED_FRAMES],
            counters[INKBOY_PROFILE_PRESENTATIONS],
            counters[INKBOY_PROFILE_REFRESH_ATTEMPTS],
            counters[INKBOY_PROFILE_REFRESH_FAILURES],
            elapsed > 0.0 ? counters[INKBOY_PROFILE_RUNS] / elapsed : 0.0,
            elapsed > 0.0
                ? counters[INKBOY_PROFILE_PRESENTATIONS] / elapsed : 0.0,
            cpu_percent, rss_kib, peak_rss_kib, reason, status);
    fprintf(stdout, " initial_presentations=%" PRIu64 " normal_presentations=%" PRIu64
            " cleanups=%" PRIu64 " unchanged_images=%" PRIu64
            " initial_refresh_failures=%" PRIu64 " normal_refresh_failures=%" PRIu64
            " cleanup_refresh_failures=%" PRIu64 " normal_fps=%.3f cleanup_fps=%.3f",
            counters[INKBOY_PROFILE_INITIAL_PRESENTATIONS],
            counters[INKBOY_PROFILE_NORMAL_PRESENTATIONS],
            counters[INKBOY_PROFILE_CLEANUPS],
            counters[INKBOY_PROFILE_UNCHANGED_IMAGES],
            counters[INKBOY_PROFILE_INITIAL_REFRESH_FAILURES],
            counters[INKBOY_PROFILE_NORMAL_REFRESH_FAILURES],
            counters[INKBOY_PROFILE_CLEANUP_REFRESH_FAILURES],
            elapsed > 0.0
                ? counters[INKBOY_PROFILE_NORMAL_PRESENTATIONS] / elapsed : 0.0,
            elapsed > 0.0 ? counters[INKBOY_PROFILE_CLEANUPS] / elapsed : 0.0);
}

static void
report_stage_metrics(void)
{
    for (unsigned index = 0U; index < INKBOY_PROFILE_STAGE_COUNT; index++) {
        const struct inkboy_profile_sample *sample = &inkboy_profile.stages[index];

        fprintf(stdout, " %s_count=%" PRIu64 " %s_total_ms=%.3f"
                " %s_avg_ms=%.3f %s_max_ms=%.3f %s_last_ms=%.3f",
                profile_stage_names[index], sample->count,
                profile_stage_names[index], sample->total_ns / 1000000.0,
                profile_stage_names[index],
                sample->count > 0U
                    ? sample->total_ns / 1000000.0 / sample->count : 0.0,
                profile_stage_names[index], sample->max_ns / 1000000.0,
                profile_stage_names[index], sample->last_ns / 1000000.0);
    }
}

void inkboy_profile_report(const char *event, const char *reason, int status)
{
    struct rusage usage = {0};
    struct timespec utc = {0};
    uint64_t now = inkboy_profile_now();
    double elapsed = now >= started ? (now - started) / 1000000000.0 : 0.0;
    double interval = now >= previous_report
                          ? (now - previous_report) / 1000000.0 : 0.0;
    double cpu_percent;
    unsigned long rss_kib = 0UL;
    FILE *status_file;
    char line[160];

    (void)getrusage(RUSAGE_SELF, &usage);
    (void)clock_gettime(CLOCK_REALTIME, &utc);
    status_file = fopen("/proc/self/status", "r");
    if (status_file != NULL) {
        while (fgets(line, sizeof(line), status_file) != NULL) {
            if (sscanf(line, "VmRSS: %lu kB", &rss_kib) == 1) {
                break;
            }
        }
        fclose(status_file);
    }
    cpu_percent = elapsed > 0.0
        ? (cpu_seconds(&usage) - initial_cpu) * 100.0 / elapsed : 0.0;
    fprintf(stdout,
            "profile event=%s phase=%s utc_s=%lld.%09ld monotonic_ns=%" PRIu64
            " elapsed_s=%.6f interval_ms=%.3f",
            event, phase_name, (long long)utc.tv_sec, utc.tv_nsec, now,
            elapsed, interval);
    report_runtime_metrics(elapsed, cpu_percent, rss_kib,
                           usage.ru_maxrss, reason, status);
    report_stage_metrics();
    fputc('\n', stdout);
    fflush(stdout);
    previous_report = now;
}
