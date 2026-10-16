#define _POSIX_C_SOURCE 200809L

#include "profile.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    FILE *capture = tmpfile();
    int saved_stdout = dup(STDOUT_FILENO);
    char line[8192];
    struct inkboy_profile_timer first, second;

    assert(capture != NULL && saved_stdout >= 0);
    fflush(stdout);
    assert(dup2(fileno(capture), STDOUT_FILENO) >= 0);
    inkboy_profile_begin("test");
    inkboy_profile_report("summary", "empty", 0);
    first = inkboy_profile_timer_start(INKBOY_PROFILE_INPUT);
    second = inkboy_profile_timer_start(INKBOY_PROFILE_INPUT);
    inkboy_profile_timer_stop(first);
    inkboy_profile_timer_stop(second);
    assert(inkboy_profile_stage_value(INKBOY_PROFILE_INPUT).count == 2U);
    assert(inkboy_profile_stage_value(INKBOY_PROFILE_INPUT).total_ns >=
           inkboy_profile_stage_value(INKBOY_PROFILE_INPUT).max_ns);
    assert(inkboy_profile_stage_value(INKBOY_PROFILE_INPUT).last_ns <=
           inkboy_profile_stage_value(INKBOY_PROFILE_INPUT).max_ns);
    inkboy_profile_report("summary", "measured", 0);
    rewind(capture);
    assert(fgets(line, sizeof(line), capture) != NULL);
    assert(strstr(line, "phase=test") && strstr(line, "reason=empty"));
    assert(strstr(line, "emu_fps=0.000") && strstr(line, "display_fps=0.000"));
    assert(!strstr(line, "=nan") && !strstr(line, "=inf"));
    assert(fgets(line, sizeof(line), capture) != NULL);
    assert(strstr(line, "input_poll_count=2") && strstr(line, "reason=measured"));
    assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    close(saved_stdout);
    fclose(capture);
    puts("profile: zero counters, stage counts, finite output and report format pass");
    return 0;
}
