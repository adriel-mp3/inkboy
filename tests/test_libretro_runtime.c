#include "libretro.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    struct inkboy_libretro *emulator = NULL;
    struct inkboy_video_frame frame;

    if (argc != 3 ||
        inkboy_libretro_open(&emulator, argv[1], argv[2]) != 0) {
        return 1;
    }
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
    printf("libretro runtime: %ux%u pitch=%zu fps=%.3f\n",
           frame.width, frame.height, frame.pitch,
           inkboy_libretro_fps(emulator));
    inkboy_libretro_close(emulator);
    return 0;
}
