# InkBoy architecture

## Overview

InkBoy is a small native C application for the Kindle Paperwhite 5. It runs as
one process on the device and owns a simple rendering lifecycle: initialize the
display, prepare the monochrome composition, submit it through FBInk, wait for
the e-ink refresh to complete, and restore the previous framebuffer on normal
termination.

The architecture is intentionally close to the hardware boundary. The current
M5 application uses a dynamically loaded pinned SameBoy Libretro core, virtual
touchscreen controls and a single-threaded application loop. Gameplay/control
response and safe shutdown were validated on PW5 through TASK-009; the earlier Stage 2 renderer remains a validated
historical baseline. There is no desktop window system or background service.

## Why C

C is used because it matches the target and the available Kindle interfaces:

- it produces a small native ARM executable with predictable startup and
  memory behavior;
- it can call the public FBInk C API directly without a runtime or foreign
  language bridge;
- it keeps framebuffer buffers, pixel conversion, signal handling, and refresh
  ordering explicit;
- it works with the existing KindleHF cross-toolchain, Docker build, and Make
  workflow; and
- it minimizes dependencies on a constrained device whose root filesystem must
  remain untouched.

C is therefore a platform and dependency decision, not an invitation to copy
Kindle-specific kernel code into the application. Device-specific display
behavior remains behind FBInk's public API.

## Main components

### InkBoy application

The application code under `src/` owns process lifecycle, signal handling,
composition layout, bitmap/text drawing, framebuffer state, and normal
restoration. It does not map `/dev/fb0` or issue Kindle-specific ioctls.

The Stage 2 runtime is single-threaded. The ordering is deliberate: the
application finishes preparing a stable image before FBInk reads it, performs
one explicit refresh, waits synchronously for completion, and restores the
saved state during normal shutdown.

### FBInk

FBInk is the display adapter. InkBoy uses its public C API for display
discovery, framebuffer dump/restore, raw image drawing, refresh submission,
waveform selection, and completion waits. The project builds a minimal static
FBInk dependency into the executable rather than installing a display library
on the Kindle.

This boundary keeps Kindle and e-ink details in one dependency while leaving
the application responsible for the higher-level composition and lifecycle.

### Visual asset pipeline

`assets/duck.svg` is the source visual asset. During development, the host
build converts it into the generated `src/duck_bitmap.h`. The Kindle receives
the compiled bitmap, so it does not need SVG parsing, librsvg, Cairo, or other
host-side graphics libraries at runtime.

### Build and deployment

The host uses Make and Docker with the KindleHF SDK/toolchain to produce the
ARM hard-float executable. Deployment places the application under
`/mnt/us/inkboy`; Docker, the SDK, and host conversion tools are not required
on the Kindle. The root filesystem is not modified.

The continuous Libretro/SameBoy probe under `tools/` remains a separate research
tool. The M5 application has its own Libretro adapter in `src/` and does not
run or modify the validated probes. TASK-009 instrumentation and isolated
comparison builds are described in the
[runtime validation report](experiments/09-gameplay-runtime.md).

## Historical Stage 2 runtime flow

1. Open and initialize FBInk and query the Kindle display geometry.
2. Save the current framebuffer with `fbink_dump`.
3. Render the duck bitmap and `InkBoy` text into the application buffer.
4. Draw the prepared image through FBInk without implicitly refreshing it.
5. Submit one explicit e-ink refresh and wait for completion.
6. On normal termination, restore the saved framebuffer and wait for that
   restore to complete.

This flow favors deterministic ownership and safe restoration over animation,
throughput, or general-purpose UI features.

## M5 gameplay runtime flow

The application initializes FBInk, the pinned core and input, then runs 180
warmup frames with input polling. It selects the latest core frame, composes
the screen, draws it through FBInk, submits a full flashing AUTO refresh, and
waits synchronously. The loop polls input, runs the core with nominal pacing,
and presents at the retained conservative 1 FPS cadence. Conversion and nearest
scaling have separate measurement boundaries and retain TASK-008 pixel output.

The input API uses an enum to distinguish continued execution, a normal EXIT
request and an input error. EXIT/signals/errors converge on ordered cleanup:
release input, unload the core, restore/wait/free the saved framebuffer and close
FBInk. Completion failures propagate to process status. Profiling logs are sent
to stdout/stderr, including actual refresh rectangles and restoration results.
An independently built experimental executable measures game-region updates;
the standard executable continues using full-screen presentation.

## Dependency boundary

Runtime dependencies are kept small: the native C runtime, the statically
linked minimal FBInk build, and the Kindle's supported system interfaces. The
host-only SVG conversion path is deliberately excluded from the device.

InkBoy uses FBInk's public API and does not depend on the KOReader executable,
an undocumented installed `libfbink.so`, or a system-wide rendering package.

## Why FBInk is currently vendored

InkBoy uses a minimal, statically linked FBInk build for direct Kindle e-ink
rendering. The source is kept under `third_party/fbink/` and is built into the
InkBoy executable; it is not installed on the Kindle root filesystem.

The tested KOReader installation provides the FBInk executable at
`/mnt/us/koreader/fbink`, but does not provide a linkable core `libfbink.so`.
The available `libfbink_input.so.1` is an input helper and does not replace the
rendering API. Invoking the KOReader executable from InkBoy would add a runtime
process dependency and would not provide the C API used by the renderer.

The vendored source is pinned to upstream commit `886f25f`, built with the
minimal image and drawing features required by InkBoy. Its upstream license
and notices are preserved in `third_party/fbink/LICENSE` and the related
third-party license files.

## Conditions for removing `third_party/fbink`

The directory may be removed only after a replacement has been built and
verified on the physical PW5. Acceptable alternatives are:

1. a compatible linkable FBInk library already present on the device;
2. a separately packaged FBInk library supplied through the Kindle SDK and
   deployed without modifying the root filesystem; or
3. a replacement InkBoy renderer that safely handles the PW5 framebuffer,
   pixel format, refresh operations, and restoration behavior.

The replacement must preserve the current guarantees: no direct framebuffer
access in application code unless explicitly justified, one controlled
refresh for the static screen, and restoration after normal termination.
Compilation on the host alone is not sufficient evidence for removal.

Keeping the dependency in the repository makes the executable reproducible,
avoids relying on undocumented KOReader installation details, and keeps the
current workflow independent of system files. This is an implementation
decision, not a permanent commitment to FBInk.
