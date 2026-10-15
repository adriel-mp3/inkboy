# Stage 2 — Rendering research

Status: research and proof-of-concept planning only. No rendering code is
implemented by this stage.

## Scope and evidence

Gambatte-K2 was inspected as a Kindle rendering reference, not as a source for
InkBoy. Its README describes two paths: GTK2/XCB shared-memory drawing, which
uses the operating system's e-ink refresh behavior, and FBInk direct
framebuffer output, which is faster but more prone to ghosting. The reference
project is GPL-3.0 and targets several Kindle firmware/device combinations;
those facts do not establish compatibility with the PW5/5.19.2 runtime.

References: [Gambatte-K2 README](https://github.com/crazy-electron/gambatte-k2/blob/main/README.md),
[`main.c`](https://github.com/crazy-electron/gambatte-k2/blob/main/main.c),
[`gray_shm.c`](https://github.com/crazy-electron/gambatte-k2/blob/main/gray_shm.c),
and [`fbink.c`](https://github.com/crazy-electron/gambatte-k2/blob/main/fbink.c).

## Findings

### GTK2/X11 initialization

The reference calls `gtk_init()` and enters `gtk_main()`. Its UI is a GTK
window containing a `GtkDrawingArea`. The rendering object is backed by a
small XCB shared-memory helper (`gray_shm.c`), which exposes a pixel buffer,
resize operation, and `commit_rect()` operation. In this mode X11 owns the
window and the Kindle's display integration performs the e-ink updates.

This path fits the existing InkBoy dependency set, but it also means InkBoy
must own a window under the Kindle window manager and retain GTK2/X11 runtime
compatibility. It is useful for a visible first UI, not necessarily the
smallest path for a single static mascot.

### FBInk and framebuffer output

The reference's FBInk wrapper opens a framebuffer with `fbink_open()`, calls
`fbink_init()`, writes a raw grayscale buffer with `fbink_print_raw_data()`,
and explicitly requests a region update with `fbink_refresh()`. The wrapper's
configuration includes waveform mode, dithering, inversion, offsets,
overlay/no-refresh flags, and animation-related controls. It also scans input
devices, including touchscreen and scaled-tablet types.

FBInk therefore sits between InkBoy and the device-specific framebuffer and
EPDC details. InkBoy should not write `/dev/fb*`, issue Kindle-specific ioctls,
or guess pixel formats directly. The FBInk API can perform that translation
and refresh request, provided a PW5-compatible library/header is available in
the SDK or is supplied as a separately verified dependency.

### How pixels reach the display

In the GTK path, InkBoy writes the XCB shared-memory image and commits a
rectangle; X11/Kindle integration schedules the display update. In the FBInk
path, InkBoy supplies a grayscale image and rectangle to FBInk; FBInk copies or
converts it for the framebuffer and submits the e-ink refresh. These are
separate ownership models and should not be mixed in the first proof of
concept.

### Refresh behavior and artifacts

E-ink refresh is not a normal immediate LCD blit. Partial/fast waveforms can
leave ghosting, while stronger full grayscale refreshes are slower and visibly
flash. Gambatte-K2 explicitly characterizes FBInk as faster but more quirky and
more prone to ghosting. For one mostly static screen, InkBoy should render once
and request one deliberate full or high-quality update, rather than maintain a
frame loop. A later interactive version can measure partial refresh behavior.

### Dependencies and device constraints

The existing build already provides the cross-compiler, Kindle sysroot, GTK2,
and deployment to `/mnt/us/inkboy`. GTK2/X11 requires the matching Kindle
runtime and a usable X display/window-manager session. FBInk adds a Kindle
framebuffer library and requires access to the framebuffer device; permissions,
device path, pixel format, screen orientation, and waveform support must be
verified on the actual PW5. Touch input is out of scope for this static proof
of concept.

Neither reference code nor its tested firmware list proves that an FBInk
binary built for another Kindle generation is safe on 5.19.2. The next
implementation must fail cleanly if the framebuffer or library cannot be
opened.

### Ownership and restoration

Direct framebuffer rendering bypasses the normal application window. It must
not leave the device showing a stale screen after exit. The proof of concept
should save the prior display state where the chosen API permits it, render
only a bounded region, avoid changing persistent system settings, and issue a
final restoration/refresh before closing. If safe restoration cannot be
demonstrated, use a GTK window first and treat direct FBInk output as an
explicitly experimental mode.

## Recommendation

Use a minimal FBInk-backed renderer for the static mascot proof of concept,
with a GTK2 fallback only if PW5 framebuffer access or a compatible FBInk
dependency cannot be established. This keeps the application small, avoids an
unnecessary emulator/UI stack, and makes the e-ink refresh request explicit.
The renderer should contain only:

1. a monochrome raster generated from the original adriel.cloud duck SVG;
2. a tiny built-in bitmap font for `InkBoy` (or a verified existing system
   font path, if that does not add a dependency);
3. a single grayscale buffer and bounded placement calculation; and
4. one FBInk raw-data write plus one refresh, followed by safe cleanup.

The SVG remains the visual authority. Conversion to a device-sized monochrome
bitmap should happen on the development host, keeping SVG parsing and raster
libraries off the Kindle. The asset's provenance and license must be recorded
before it is bundled.

## Smallest next milestone

Implement `render-duck-poc`: add the original SVG-derived monochrome asset,
link the smallest PW5-compatible FBInk interface available through the SDK,
render the duck and `InkBoy` once, deploy with the existing `bin/deploy`, and
verify on hardware that the image is visible, refresh artifacts are acceptable,
and the screen/application exits safely. Do not add input, animation, GTK
widgets, emulator code, or root-filesystem installation in that milestone.

Acceptance evidence should include the exact framebuffer/FBInk dependency,
display geometry and pixel format, refresh mode used, observed ghosting, and
the restoration behavior on exit.
