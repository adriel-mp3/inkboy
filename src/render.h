#ifndef INKBOY_RENDER_H
#define INKBOY_RENDER_H

#include "layout.h"

struct inkboy_renderer;

int inkboy_renderer_open(struct inkboy_renderer **renderer_out);
int inkboy_renderer_present(struct inkboy_renderer *renderer);
const struct inkboy_layout *inkboy_renderer_layout(
    const struct inkboy_renderer *renderer);
const char *inkboy_renderer_version(const struct inkboy_renderer *renderer);
int inkboy_renderer_close(struct inkboy_renderer *renderer);

#endif
