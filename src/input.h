#ifndef INKBOY_INPUT_H
#define INKBOY_INPUT_H

#include "layout.h"

#include <signal.h>
#include <stdint.h>

enum inkboy_input_phase {
    INKBOY_INPUT_DOWN,
    INKBOY_INPUT_UP,
    INKBOY_INPUT_CANCEL
};

enum inkboy_input_result {
    INKBOY_INPUT_ERROR = -1,
    INKBOY_INPUT_OK = 0,
    INKBOY_INPUT_STOP = 1
};

struct inkboy_input_event {
    enum inkboy_control control;
    enum inkboy_input_phase phase;
    uint32_t x;
    uint32_t y;
};

typedef void (*inkboy_input_callback)(
    const struct inkboy_input_event *event,
    void *userdata);

struct inkboy_input;

int inkboy_input_open(struct inkboy_input **input_out,
                      const struct inkboy_layout *layout,
                      inkboy_input_callback callback,
                      void *userdata);
/* OK: continue; STOP: EXIT released inside its target; ERROR: cleanup required. */
enum inkboy_input_result inkboy_input_wait(struct inkboy_input *input,
                      const volatile sig_atomic_t *stop_requested);
enum inkboy_input_result inkboy_input_poll(struct inkboy_input *input);
void inkboy_input_close(struct inkboy_input *input);

#endif
