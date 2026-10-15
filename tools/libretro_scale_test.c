#define main libretro_live_probe_main
#include "libretro_live.c"
#undef main

static void reference_dimensions(unsigned source_width, unsigned source_height,
                                 unsigned view_width, unsigned view_height,
                                 unsigned *scaled_width, unsigned *scaled_height)
{
    *scaled_width = view_width;
    *scaled_height = view_height;
    if ((uint64_t)*scaled_width * source_height <=
        (uint64_t)*scaled_height * source_width) {
        *scaled_height = (unsigned)(((uint64_t)*scaled_width * source_height) /
                                    source_width);
    } else {
        *scaled_width = (unsigned)(((uint64_t)*scaled_height * source_width) /
                                   source_height);
    }
}

static int reference_scale(const unsigned char *source, unsigned source_width,
                           unsigned source_height, unsigned view_width,
                           unsigned view_height, unsigned char *destination,
                           unsigned *scaled_width, unsigned *scaled_height)
{
    size_t source_row_bytes;
    size_t destination_row_bytes;

    if (source_width == 0U || source_height == 0U || view_width == 0U ||
        view_height == 0U) {
        return -1;
    }
    reference_dimensions(source_width, source_height, view_width, view_height,
                        scaled_width, scaled_height);
    if (*scaled_width == 0U || *scaled_height == 0U ||
        !size_mul((size_t)source_width, 2U, &source_row_bytes) ||
        !size_mul((size_t)*scaled_width, 2U, &destination_row_bytes)) {
        return -1;
    }
    for (unsigned y = 0U; y < *scaled_height; y++) {
        unsigned source_y = (unsigned)(((uint64_t)y * source_height) /
                                       *scaled_height);
        const unsigned char *source_row = source +
            (size_t)source_y * source_row_bytes;
        unsigned char *destination_row = destination +
            (size_t)y * destination_row_bytes;

        for (unsigned x = 0U; x < *scaled_width; x++) {
            unsigned source_x = (unsigned)(((uint64_t)x * source_width) /
                                           *scaled_width);
            const unsigned char *source_pixel = source_row +
                (size_t)source_x * 2U;

            destination_row[(size_t)x * 2U] = source_pixel[0];
            destination_row[(size_t)x * 2U + 1U] = source_pixel[1];
        }
    }
    return 0;
}

static int run_case(unsigned source_width, unsigned source_height,
                    unsigned view_width, unsigned view_height,
                    unsigned pattern)
{
    struct display_context screen = {0};
    unsigned expected_width;
    unsigned expected_height;
    size_t source_size;
    size_t output_size;
    unsigned char *expected;
    int result = 1;

    if (!size_mul((size_t)source_width, source_height, &source_size) ||
        !size_mul(source_size, 2U, &source_size)) {
        return 1;
    }
    reference_dimensions(source_width, source_height, view_width, view_height,
                         &expected_width, &expected_height);
    if (!size_mul((size_t)expected_width, expected_height, &output_size) ||
        !size_mul(output_size, 2U, &output_size)) {
        return 1;
    }
    screen.ya = malloc(source_size);
    expected = malloc(output_size);
    if (screen.ya == NULL || expected == NULL) {
        goto cleanup;
    }
    screen.ya_size = source_size;
    screen.ya_width = source_width;
    screen.ya_height = source_height;
    screen.state.view_width = view_width;
    screen.state.view_height = view_height;
    for (size_t index = 0U; index < source_size; index++) {
        screen.ya[index] = (unsigned char)((index * 37U + pattern * 19U +
                                            (index >> 3U)) & 0xffU);
    }
    if (reference_scale(screen.ya, source_width, source_height, view_width,
                        view_height, expected, &expected_width,
                        &expected_height) != 0 ||
        scale_nearest(&screen) != 0 || screen.scaled_width != expected_width ||
        screen.scaled_height != expected_height ||
        memcmp(screen.scaled_ya, expected, output_size) != 0) {
        fprintf(stderr, "scale mismatch source=%ux%u view=%ux%u pattern=%u\n",
                source_width, source_height, view_width, view_height, pattern);
        goto cleanup;
    }
    {
        unsigned char *first_buffer = screen.scaled_ya;

        for (size_t index = 0U; index < source_size; index++) {
            screen.ya[index] = (unsigned char)((index * 11U + pattern * 71U +
                                                0xa5U) & 0xffU);
        }
        if (scale_nearest(&screen) != 0 || screen.scaled_ya != first_buffer ||
            reference_scale(screen.ya, source_width, source_height,
                            view_width, view_height, expected, &expected_width,
                            &expected_height) != 0 ||
            memcmp(screen.scaled_ya, expected, output_size) != 0) {
            fprintf(stderr, "reuse mismatch source=%ux%u view=%ux%u pattern=%u\n",
                    source_width, source_height, view_width, view_height,
                    pattern);
            goto cleanup;
        }
    }
    result = 0;

cleanup:
    free(screen.ya);
    free(screen.scaled_ya);
    free(screen.nearest_x);
    free(screen.nearest_y);
    free(expected);
    return result;
}

int main(void)
{
    static const unsigned cases[][4] = {
        {1U, 1U, 1U, 1U},
        {2U, 3U, 7U, 5U},
        {3U, 2U, 5U, 7U},
        {4U, 4U, 9U, 9U},
        {7U, 5U, 16U, 11U},
        {160U, 144U, 1236U, 1648U},
        {161U, 143U, 1237U, 1647U},
        {320U, 200U, 1236U, 1648U}
    };

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        for (unsigned pattern = 0U; pattern < 4U; pattern++) {
            if (run_case(cases[index][0], cases[index][1], cases[index][2],
                         cases[index][3], pattern) != 0) {
                return 1;
            }
        }
    }
    puts("libretro scale test: all reference comparisons passed");
    return 0;
}
