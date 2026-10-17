#include "draw.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    struct inkboy_layout layout;
    size_t size;
    uint8_t *buffer;
    uint64_t hash = UINT64_C(14695981039346656037);

    assert(inkboy_layout_build(&layout, 1236U, 1648U) == 0);
    assert(inkboy_buffer_size(layout.width, layout.height, &size) == 0);
    buffer = malloc(size);
    assert(buffer != NULL);
    for (size_t i = 0U; i < size; i += 2U) {
        buffer[i] = UINT8_MAX;
        buffer[i + 1U] = UINT8_MAX;
    }

    inkboy_draw_title(buffer, layout.width, layout.title_x, layout.title_y);
    inkboy_draw_duck(buffer, layout.width, layout.duck_x, layout.duck_y,
                     layout.duck_size);
    inkboy_draw_controls(buffer, &layout);

    for (size_t i = 0U; i < size; i++) {
        hash = (hash ^ buffer[i]) * UINT64_C(1099511628211);
    }
    assert(hash == UINT64_C(0x8ea3a2626237aaa5));
    printf("draw: composition hash=%016llx\n", (unsigned long long)hash);
    free(buffer);
    return 0;
}
