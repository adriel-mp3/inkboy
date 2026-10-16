#define _POSIX_C_SOURCE 200809L

#include "profile.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

struct inkboy_profile inkboy_profile;

static const char *phase_name = "startup";
static uint64_t started;
static uint64_t previous_report;
static double initial_cpu;

uint64_t inkboy_profile_now(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0U;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

void inkboy_profile_record(enum inkboy_profile_stage stage, uint64_t start)
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

void inkboy_profile_report(const char *event, const char *reason, int status)
{
    static const char *const names[] = {
        "emulation", "input_poll", "input_gap", "callback_copy", "convert", "scale",
        "compose", "region_pack", "fbink_draw", "refresh_submit", "refresh_wait",
        "input_to_state"
    };
    struct rusage usage = {0};
    struct timespec utc = {0};
    uint64_t now = inkboy_profile_now();
    double elapsed = now >= started ? (now - started) / 1000000000.0 : 0.0;
    double interval = now >= previous_report
                          ? (now - previous_report) / 1000000.0 : 0.0;
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
    fprintf(stdout,
            "profile event=%s phase=%s utc_s=%lld.%09ld monotonic_ns=%" PRIu64
            " elapsed_s=%.6f interval_ms=%.3f runs=%" PRIu64
            " callbacks=%" PRIu64 " valid_frames=%" PRIu64
            " duplicate_frames=%" PRIu64 " invalid_frames=%" PRIu64
            " superseded_frames=%" PRIu64 " presentations=%" PRIu64
            " refresh_attempts=%" PRIu64 " refresh_failures=%" PRIu64
            " emu_fps=%.3f display_fps=%.3f cpu_pct=%.3f"
            " rss_kib=%lu peak_rss_kib=%ld reason=%s status=%d",
            event, phase_name, (long long)utc.tv_sec, utc.tv_nsec, now,
            elapsed, interval, inkboy_profile.runs, inkboy_profile.callbacks,
            inkboy_profile.valid_frames, inkboy_profile.duplicate_frames,
            inkboy_profile.invalid_frames, inkboy_profile.superseded_frames,
            inkboy_profile.presentations, inkboy_profile.refresh_attempts,
            inkboy_profile.refresh_failures,
            elapsed > 0.0 ? inkboy_profile.runs / elapsed : 0.0,
            elapsed > 0.0 ? inkboy_profile.presentations / elapsed : 0.0,
            elapsed > 0.0 ? (cpu_seconds(&usage) - initial_cpu) * 100.0 / elapsed : 0.0,
            rss_kib, usage.ru_maxrss, reason, status);
    for (unsigned index = 0U; index < INKBOY_PROFILE_STAGE_COUNT; index++) {
        const struct inkboy_profile_sample *sample = &inkboy_profile.stages[index];

        fprintf(stdout, " %s_count=%" PRIu64 " %s_total_ms=%.3f"
                " %s_avg_ms=%.3f %s_max_ms=%.3f %s_last_ms=%.3f", names[index], sample->count,
                names[index], sample->total_ns / 1000000.0, names[index],
                sample->count > 0U
                    ? sample->total_ns / 1000000.0 / sample->count : 0.0,
                names[index], sample->max_ns / 1000000.0,
                names[index], sample->last_ns / 1000000.0);
    }
    fputc('\n', stdout);
    fflush(stdout);
    previous_report = now;
}
