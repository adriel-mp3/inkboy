#ifndef INKBOY_RENDER_H
#define INKBOY_RENDER_H

#include "layout.h"
#include "frame.h"
#include "presentation.h"

struct inkboy_renderer;

int inkboy_renderer_open(struct inkboy_renderer **renderer_out);
enum inkboy_present_result inkboy_renderer_present_frame(struct inkboy_renderer *renderer,
                                  const struct inkboy_video_frame *frame);
const struct inkboy_layout *inkboy_renderer_layout(
    const struct inkboy_renderer *renderer);
const char *inkboy_renderer_version(const struct inkboy_renderer *renderer);
int inkboy_renderer_close(struct inkboy_renderer *renderer);

#endif
