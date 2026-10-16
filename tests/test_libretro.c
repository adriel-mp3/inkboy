#include "libretro.h"

#include <assert.h>

int main(void)
{
    assert(inkboy_libretro_control_id(INKBOY_CONTROL_UP) == 4);
    assert(inkboy_libretro_control_id(INKBOY_CONTROL_DOWN) == 5);
    assert(inkboy_libretro_control_id(INKBOY_CONTROL_LEFT) == 6);
    assert(inkboy_libretro_control_id(INKBOY_CONTROL_RIGHT) == 7);
    assert(inkboy_libretro_control_id(INKBOY_CONTROL_A) == 8);
    assert(inkboy_libretro_control_id(INKBOY_CONTROL_B) == 0);
    assert(inkboy_libretro_control_id(INKBOY_CONTROL_START) == 3);
    assert(inkboy_libretro_control_id(INKBOY_CONTROL_SELECT) == 2);
    assert(inkboy_libretro_control_id(INKBOY_CONTROL_EXIT) == -1);
    assert(inkboy_libretro_control_id(INKBOY_CONTROL_L) == -1);
    assert(inkboy_libretro_control_id(INKBOY_CONTROL_R) == -1);
    return 0;
}
