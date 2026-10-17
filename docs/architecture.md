# InkBoy architecture

## Overview

InkBoy is a native C application for the Kindle Paperwhite 5. SameBoy runs the
game through Libretro; Linux evdev provides touchscreen input; FBInk's public C
API draws images and controls e-ink refreshes. The runtime is single-threaded,
so input, emulation, and synchronous display updates follow one explicit
sequence. InkBoy does not map the framebuffer or issue Kindle-specific display
ioctls directly.

## Components

| Module | Responsibility |
| --- | --- |
| `src/main.c` | Lifecycle, signals, warmup, gameplay loop, and pacing |
| `src/input.c`, `src/layout.c` | Touchscreen events, screen geometry, and control mapping |
| `src/libretro.c` | Load the core and ROM, connect callbacks, retain frames and controls |
| `src/frame.c` | Validate, convert, and scale core video frames |
| `src/render.c` | Compose the screen; draw, refresh, wait, and restore through FBInk |
| `src/profile.c`, `src/presentation.h` | Runtime measurements and presentation policy |

## Runtime

```text
InkBoy runtime
├── Startup              — Initialize display, core, ROM, and touchscreen
├── Warmup               — Poll input and run up to 180 emulation frames
├── Initial Presentation — Display the latest frame
├── Gameplay             — Poll input, emulate, pace, and present on schedule
└── Shutdown             — Release input, unload core, and restore display
```

The main data flows are:

- **Input:** evdev touch events → layout/control mapping → Libretro joypad state.
- **Emulation:** SameBoy produces XRGB8888 video → Libretro adapter retains the
  latest valid frame.
- **Rendering:** frame conversion/scaling → screen composition → FBInk draw,
  refresh submission, with completion waits deferred during ordinary gameplay.

The standard presentation policy targets 2 FPS with regional GC16 refreshes.
The first frame and geometry changes refresh the full screen; ordinary updates
refresh only the game rectangle without flashing. A flashing AUTO cleanup
refreshes the game rectangle every 60 seconds. Shutdown restores the saved full
screen. Compile-time profiles remain available for cadence and waveform
experiments.

## Memory & Resources

| Resource | Ownership and lifetime |
| --- | --- |
| Input descriptor | Owned by `input.c`; release of its grab is attempted before close. |
| Core, ROM, and video pixels | Core adapter owns the dynamic-library handle and allocations; it unloads the game before freeing them. |
| Layout and video frame views | Input borrows the renderer's layout. A frame view points into adapter-owned storage; later core runs may overwrite or reallocate it, and closing the adapter frees it. |
| Display and render buffers | Renderer owns the FBInk descriptor, saved framebuffer dump, and image buffers; it restores and waits when possible before freeing them and closing FBInk. |

`main.c` closes input, the core, and the renderer in that order. The input
callback borrows the core handle as userdata; input does not own either that
handle or the layout.

## Build & Dependencies

Make and Docker use the KindleHF cross-toolchain to build the ARM executable.
Deployment places the application under `/mnt/us/inkboy`; the SameBoy core and
ROM are loaded from their configured paths. Deployment does not modify the
Kindle root filesystem. See [README.md](../README.md) for commands and
[compatibility.md](compatibility.md) for the target ABI details.

FBInk is built from the minimal vendored source and statically linked into the
application; the Kindle does not need a separate FBInk shared library. SameBoy
is supplied as a Libretro shared core and loaded at runtime. The reasons for
vendoring FBInk and the conditions for replacing it are summarized in
[decisions.md](decisions.md), with device investigation and validation in
[the rendering report](experiments/02-rendering.md).
