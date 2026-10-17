#ifndef INKBOY_DRAW_H
#define INKBOY_DRAW_H

#include "layout.h"

#include <stdint.h>

void inkboy_draw_duck(uint8_t *buffer, uint32_t screen_width,
                      uint32_t origin_x, uint32_t origin_y, uint32_t size);
void inkboy_draw_title(uint8_t *buffer, uint32_t screen_width,
                       uint32_t origin_x, uint32_t origin_y);
void inkboy_draw_controls(uint8_t *buffer, const struct inkboy_layout *layout);

#endif
