#include <cairo/cairo.h>
#include <dlfcn.h>
#include <glib-object.h>

#include <stdio.h>
#include <stdlib.h>

static void
fail(const char *message)
{
    fprintf(stderr, "svg_to_bitmap: %s\n", message);
    exit(EXIT_FAILURE);
}

typedef struct _RsvgHandle RsvgHandle;
typedef RsvgHandle *(*RsvgHandleNewFromFile)(const char *, GError **);
typedef gboolean (*RsvgHandleRenderCairo)(RsvgHandle *, cairo_t *);

static void *
load_symbol(void *library, const char *name)
{
    void *symbol = dlsym(library, name);

    if (symbol == NULL) {
        fprintf(stderr, "svg_to_bitmap: missing librsvg symbol: %s\n", name);
        dlclose(library);
        exit(EXIT_FAILURE);
    }

    return symbol;
}

int
main(int argc, char **argv)
{
    GError *error = NULL;
    RsvgHandle *handle;
    cairo_surface_t *surface;
    cairo_t *cr;
    unsigned char *pixels;
    void *rsvg;
    RsvgHandleNewFromFile new_from_file;
    RsvgHandleRenderCairo render_cairo;
    FILE *output;
    int width;
    int height;
    int x;
    int y;

    if (argc != 5) {
        fprintf(stderr, "Usage: %s INPUT.svg OUTPUT.h WIDTH HEIGHT\n", argv[0]);
        return EXIT_FAILURE;
    }

    width = atoi(argv[3]);
    height = atoi(argv[4]);
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
        fail("bitmap dimensions must be between 1 and 1024");
    }

    rsvg = dlopen("librsvg-2.so.2", RTLD_NOW | RTLD_LOCAL);
    if (rsvg == NULL) {
        fprintf(stderr, "svg_to_bitmap: %s\n", dlerror());
        return EXIT_FAILURE;
    }

    new_from_file = (RsvgHandleNewFromFile)load_symbol(
        rsvg, "rsvg_handle_new_from_file");
    render_cairo = (RsvgHandleRenderCairo)load_symbol(
        rsvg, "rsvg_handle_render_cairo");
    handle = new_from_file(argv[1], &error);
    if (handle == NULL) {
        fprintf(stderr, "svg_to_bitmap: %s\n", error->message);
        g_error_free(error);
        dlclose(rsvg);
        return EXIT_FAILURE;
    }

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cr = cairo_create(surface);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_scale(cr, (double)width / 512.0, (double)height / 512.0);
    if (!render_cairo(handle, cr)) {
        g_object_unref(handle);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        dlclose(rsvg);
        fail("librsvg failed to render the SVG");
    }
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    pixels = cairo_image_surface_get_data(surface);
    output = fopen(argv[2], "w");
    if (output == NULL) {
        g_object_unref(handle);
        cairo_surface_destroy(surface);
        dlclose(rsvg);
        perror("svg_to_bitmap: fopen");
        return EXIT_FAILURE;
    }

    fprintf(output,
            "/* Generated from assets/duck.svg; do not edit by hand. */\n"
            "#ifndef INKBOY_DUCK_BITMAP_H\n"
            "#define INKBOY_DUCK_BITMAP_H\n\n"
            "#include <stdint.h>\n\n"
            "#define DUCK_BITMAP_WIDTH %d\n"
            "#define DUCK_BITMAP_HEIGHT %d\n"
            "#define DUCK_BITMAP_STRIDE ((DUCK_BITMAP_WIDTH + 7) / 8)\n\n"
            "static const uint8_t duck_bitmap[DUCK_BITMAP_STRIDE * "
            "DUCK_BITMAP_HEIGHT] = {\n",
            width, height);

    for (y = 0; y < height; y++) {
        const unsigned char *row = pixels + (y * width * 4);

        for (x = 0; x < width; x += 8) {
            unsigned int packed = 0;
            int bit;

            for (bit = 0; bit < 8 && x + bit < width; bit++) {
                const unsigned char *pixel = row + ((x + bit) * 4);

                if (pixel[3] >= 128) {
                    packed |= 1U << (7 - bit);
                }
            }

            fprintf(output, "    0x%02x,", packed);
            if ((x / 8 + 1) % 8 == 0) {
                fputc('\n', output);
            }
        }
    }

    fprintf(output, "\n};\n\n#endif\n");
    if (fclose(output) != 0) {
        g_object_unref(handle);
        cairo_surface_destroy(surface);
        dlclose(rsvg);
        perror("svg_to_bitmap: fclose");
        return EXIT_FAILURE;
    }

    g_object_unref(handle);
    cairo_surface_destroy(surface);
    dlclose(rsvg);
    return EXIT_SUCCESS;
}
