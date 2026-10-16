#ifndef INKBOY_LIBRETRO_H
#define INKBOY_LIBRETRO_H

#include "frame.h"
#include "layout.h"

#include <stdbool.h>

struct inkboy_libretro;

int inkboy_libretro_open(struct inkboy_libretro **emulator_out,
                         const char *core_path,
                         const char *rom_path);
int inkboy_libretro_run(struct inkboy_libretro *emulator);
/* Select the latest frame and acknowledge it for superseded-frame accounting. */
bool inkboy_libretro_frame(struct inkboy_libretro *emulator,
                           struct inkboy_video_frame *frame);
double inkboy_libretro_fps(const struct inkboy_libretro *emulator);
int inkboy_libretro_control_id(enum inkboy_control control);
void inkboy_libretro_set_control(struct inkboy_libretro *emulator,
                                 enum inkboy_control control,
                                 bool pressed);
void inkboy_libretro_close(struct inkboy_libretro *emulator);

#endif
