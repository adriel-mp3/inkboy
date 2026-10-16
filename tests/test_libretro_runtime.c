#include "libretro.h"
#include "profile.h"

#include <assert.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    struct inkboy_libretro *emulator = NULL;
    struct inkboy_video_frame frame;

    if (argc != 3 ||
        inkboy_libretro_open(&emulator, argv[1], argv[2]) != 0) {
        return 1;
    }
    inkboy_profile_begin("host-smoke");
    for (unsigned index = 0U; index < 10U; index++) {
        if (inkboy_libretro_run(emulator) != 0) {
            inkboy_libretro_close(emulator);
            return 1;
        }
    }
    if (!inkboy_libretro_frame(emulator, &frame) || frame.width == 0U ||
        frame.height == 0U || frame.pitch < (size_t)frame.width * 4U) {
        inkboy_libretro_close(emulator);
        return 1;
    }
    assert(inkboy_profile_counter_value(INKBOY_PROFILE_RUNS) == 10U);
    assert(inkboy_profile_counter_value(INKBOY_PROFILE_VALID_FRAMES) > 0U);
    assert(inkboy_profile_counter_value(INKBOY_PROFILE_CALLBACKS) == inkboy_profile_counter_value(INKBOY_PROFILE_VALID_FRAMES) +
           inkboy_profile_counter_value(INKBOY_PROFILE_DUPLICATE_FRAMES) + inkboy_profile_counter_value(INKBOY_PROFILE_INVALID_FRAMES));
    assert(inkboy_profile_counter_value(INKBOY_PROFILE_SUPERSEDED_FRAMES) == inkboy_profile_counter_value(INKBOY_PROFILE_VALID_FRAMES) - 1U);
    assert(inkboy_profile_stage_value(INKBOY_PROFILE_EMULATION).count == 10U);
    assert(inkboy_profile_stage_value(INKBOY_PROFILE_CALLBACK_COPY).count == inkboy_profile_counter_value(INKBOY_PROFILE_VALID_FRAMES));
    uint64_t superseded = inkboy_profile_counter_value(INKBOY_PROFILE_SUPERSEDED_FRAMES);
    uint64_t valid = inkboy_profile_counter_value(INKBOY_PROFILE_VALID_FRAMES);
    inkboy_libretro_set_control(emulator, INKBOY_CONTROL_A, true);
    assert(inkboy_libretro_run(emulator) == 0);
    assert(inkboy_profile_counter_value(INKBOY_PROFILE_VALID_FRAMES) > valid);
    assert(inkboy_profile_counter_value(INKBOY_PROFILE_SUPERSEDED_FRAMES) - superseded == inkboy_profile_counter_value(INKBOY_PROFILE_VALID_FRAMES) - valid - 1U);
    assert(inkboy_profile_stage_value(INKBOY_PROFILE_INPUT_TO_STATE).count == 1U);
    printf("libretro runtime: %ux%u pitch=%zu fps=%.3f\n",
           frame.width, frame.height, frame.pitch,
           inkboy_libretro_fps(emulator));
    inkboy_libretro_close(emulator);
    return 0;
}
