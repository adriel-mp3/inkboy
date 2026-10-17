# Stage 2 — Rendering research and first screen

Status: the first FBInk renderer and host asset conversion path are
implemented and verified on the physical Kindle, including normal screen
restoration.

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
compatibility. It is useful for a visible first UI, not the smallest path for
a single static mascot.

### FBInk and framebuffer output

The reference's FBInk wrapper opens a framebuffer with `fbink_open()`, calls
`fbink_init()`, writes a raw grayscale buffer with `fbink_print_raw_data()`,
and explicitly requests a region update with `fbink_refresh()`. The wrapper's
configuration includes waveform mode, dithering, inversion, offsets,
overlay/no-refresh flags, and animation-related controls. It also scans input
devices, including touchscreen and scaled-tablet types.

FBInk therefore sits between InkBoy and the device-specific framebuffer and
EPDC details. InkBoy should not write `/dev/fb*`, issue Kindle-specific ioctls,
or guess pixel formats directly. The FBInk API performs that translation and
refresh request.

### How pixels reach the display

In the GTK path, InkBoy writes the XCB shared-memory image and commits a
rectangle; X11/Kindle integration schedules the display update. In the FBInk
path, InkBoy supplies a grayscale image and rectangle to FBInk; FBInk copies or
converts it for the framebuffer and submits the e-ink refresh. These are
separate ownership models and are not mixed in this proof of concept.

### Refresh behavior and artifacts

E-ink refresh is not a normal immediate LCD blit. Partial/fast waveforms can
leave ghosting, while stronger full grayscale refreshes are slower and visibly
flash. Gambatte-K2 characterizes FBInk as faster but more quirky and more prone
to ghosting. For one mostly static screen, InkBoy renders once and requests one
deliberate full or high-quality update, rather than maintaining a frame loop.

## PW5 dependency verification

The physical PW5 was probed over SSH. The relevant FBInk/device state was:

```text
FBINK_VERSION=92e1270
FBINK_TARGET=2
FBINK_FEATURES=0x17
viewWidth=1236 viewHeight=1648 screenWidth=1236 screenHeight=1648
viewVertOrigin=8 DPI=300 BPP=8 lineLength=1248 pixelFormat=Y8
FBID=hwtcon_v2 deviceName="PaperWhite 5" deviceCodename=Malbec
devicePlatform=Bellatrix isMTK=1 hasEclipseWfm=1
```

The device contains `/mnt/us/koreader/fbink`, but does not contain a core
linkable `libfbink.so`. The discovered shared object is
`/mnt/us/koreader/libs/libfbink_input.so.1`, which is the input helper and is
not the rendering API needed here. The available KOReader FBInk CLI also
reports `Image=No` in its options, so it is not used as InkBoy's image
renderer.

The POC therefore vendors the minimal rendering sources from [FBInk
upstream](https://github.com/NiLuJe/FBInk) at commit `886f25f`, built with
`FBINK_MINIMAL`, `FBINK_WITH_IMAGE`, and `FBINK_WITH_DRAW`. The exact source
revision embedded in the device's
`92e1270` executable was not available as a public Git reference, so the
device run remains a required compatibility check. The vendor copy retains
its upstream GPL notice in `third_party/fbink/LICENSE`; no Gambatte-K2 source
was copied.

The asset conversion tool runs on the development host and uses the host's
`librsvg`/Cairo stack. The Kindle receives only the generated 1-bit bitmap
header and the statically linked FBInk renderer; no SVG or raster library is
needed at runtime.

## Ownership and restoration

Direct framebuffer rendering bypasses the normal application window. The POC
uses `fbink_dump()` before drawing and `fbink_restore()` after SIGINT, SIGTERM,
or SIGHUP. It does not change persistent settings or the root filesystem. The
process remains alive after its one draw so the previous screen can be
restored on an ordinary exit; SIGKILL or a power loss cannot be handled by the
application.

## Recommendation

The historical proof of concept established that FBInk can be built into the
application without a runtime library dependency and can issue an explicit
refresh request. Its temporary static artwork was removed when the project
moved to gameplay; this report remains as hardware and dependency evidence.

The screen buffer is allocated as YA (luminance plus alpha), which FBInk
documents as the preferred Kindle raw-image format. The current PW5 geometry
requires 1236 × 1648 × 2 bytes, approximately 3.9 MiB, for the temporary
buffer. InkBoy never maps `/dev/fb0`, writes framebuffer memory, or issues
Kindle-specific ioctls itself; those operations remain inside FBInk.

## Historical proof of concept

The static composition implementation and its build-only artwork conversion
tool have been removed. The current runtime and build instructions are in the
repository README and architecture overview.

## Hardware verification status

The FBInk CLI text path was previously confirmed on the physical PW5, and the
device probe above confirms the framebuffer geometry and device identity. The
user visually confirmed the historical static composition on the physical PW5
after launching it through KOReader's terminal plugin. Normal termination
restored the previous screen. This remains evidence for the FBInk path and
restoration behavior. No specific ghosting measurement was recorded.

## Smallest next milestone

Stage 2 acceptance is complete. Any next milestone should be specified
separately; do not add input, animation, GTK widgets, emulator code, or
root-filesystem installation as part of this rendering experiment.
