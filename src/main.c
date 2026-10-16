#include "input.h"
#include "render.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t stop_requested;

static void
request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int
install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    if (sigemptyset(&action.sa_mask) != 0) {
        return -1;
    }

    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGHUP, &action, NULL) != 0) {
        return -1;
    }

    return 0;
}

static void
handle_input_event(const struct inkboy_input_event *event, void *userdata)
{
    const char *name;

    (void)userdata;
    if (event == NULL) {
        return;
    }
    name = inkboy_control_name(event->control);
    if (name == NULL) {
        return;
    }
    switch (event->phase) {
    case INKBOY_INPUT_DOWN:
        printf("CONTROL_DOWN button=%s x=%u y=%u\n",
               name, event->x, event->y);
        break;
    case INKBOY_INPUT_UP:
        printf("CONTROL_UP button=%s x=%u y=%u\n",
               name, event->x, event->y);
        break;
    case INKBOY_INPUT_CANCEL:
        printf("CONTROL_CANCEL button=%s\n", name);
        break;
    }
    fflush(stdout);
}

int
main(void)
{
    struct inkboy_renderer *renderer = NULL;
    struct inkboy_input *input = NULL;
    int input_result;
    int render_result;
    int exit_code = EXIT_FAILURE;

    if (install_signal_handlers() != 0) {
        fprintf(stderr, "inkboy: unable to install signal handlers: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    if (inkboy_renderer_open(&renderer) != 0) {
        return EXIT_FAILURE;
    }
    if (inkboy_input_open(&input,
                          inkboy_renderer_layout(renderer),
                          handle_input_event,
                          NULL) != 0) {
        (void)inkboy_renderer_close(renderer);
        return EXIT_FAILURE;
    }
    if (inkboy_renderer_present(renderer) != 0) {
        inkboy_input_close(input);
        (void)inkboy_renderer_close(renderer);
        return EXIT_FAILURE;
    }

    {
        const struct inkboy_layout *layout = inkboy_renderer_layout(renderer);
        printf("InkBoy rendered with FBInk %s on %ux%u; waiting for EXIT or termination\n",
               inkboy_renderer_version(renderer), layout->width, layout->height);
    }
    fflush(stdout);

    input_result = inkboy_input_wait(input, &stop_requested);
    inkboy_input_close(input);
    render_result = inkboy_renderer_close(renderer);

    if (input_result >= 0 && render_result == 0) {
        exit_code = EXIT_SUCCESS;
    }
    return exit_code;
}
