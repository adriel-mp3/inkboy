#ifndef INKBOY_DRAW_H
#define INKBOY_DRAW_H

#include "layout.h"

#include <stdint.h>

void inkboy_draw_duck(uint8_t *buffer, uint32_t screen_width,
                      uint32_t origin_x, uint32_t origin_y, uint32_t size);
void inkboy_draw_title(uint8_t *buffer, uint32_t screen_width,
                       uint32_t origin_x, uint32_t origin_y);
void inkboy_draw_controls(uint8_t *buffer, const struct inkboy_layout *layout);
void inkboy_draw_centered_label(uint8_t *buffer, uint32_t width,
                                const struct inkboy_rect *rect,
                                const char *text, uint32_t scale);

#endif
