# InkBoy — v0.1

## Objective

Build a lightweight independent native application for Kindle devices,
initially targeting the Paperwhite 5. InkBoy is not an emulator frontend and
must not incorporate Gambatte-K2 or its game engine.

The first milestone was a GTK2 application written in C that displayed an
interactive "Hello, Kindle!" button. Stage 2 investigates rendering the
existing adriel.cloud duck mascot and the text "InkBoy" on the e-ink display.

## Target

- Device: Kindle Paperwhite 5 (2021)
- Firmware: 5.19.2
- Architecture: ARM 32-bit (armhf)
- Jailbreak: Véra
- Package manager: KPM
- Development host: Nexus (Ubuntu Server, x86-64)

## Stack

- C
- GTK2 for the completed milestone and as a compatibility reference
- FBInk or another small Kindle framebuffer abstraction for the Stage 2 proof
  of concept; final selection is documented in
  [docs/experiments/02-rendering.md](experiments/02-rendering.md)
- Docker (build environment)
- Make
- Git

## Functional Requirements

- FR01–FR05: Preserve the completed GTK2 milestone behavior.
- FR06: Render the original duck SVG as a monochrome bitmap; do not invent a
  replacement mascot.
- FR07: Display the label "InkBoy" with a restrained monochrome composition.
- FR08: Keep rendering independent of emulator or game-engine code.
- FR09: Restore or relinquish Kindle display ownership safely on exit.

## Build Requirements

- Use Docker for isolated cross-compilation.
- Provide a Makefile.
- Generate ARM executables in build/.
- Verify Kindle ABI and runtime compatibility.
- Document dependencies and build instructions.
- Docker must not be required on Kindle.

## Constraints

- Minimal dependencies.
- No emulator functionality.
- Do not modify Kindle system components.
- Do not assume Ubuntu ARM libraries are Kindle-compatible.
- Do not modify the Kindle root filesystem; deploy under `/mnt/us/inkboy`.
- Prefer C and preserve the existing Docker/cross-compilation/deployment flow.
- Implement incrementally.

## Licensing

InkBoy is licensed under GNU GPL-3.0-only. The complete license text is in
[`LICENSE`](../LICENSE). Any reused third-party code or assets must retain
their notices and have compatible licensing. Gambatte-K2 is a research
reference only; its source is not copied into InkBoy.

## Acceptance Criteria

- ARM executable builds successfully.
- Application launches on PW5.
- Window and button are visible.
- Touchscreen interaction works.
- Application exits cleanly.
- Stage 2 research identifies a rendering path and a smallest proof-of-concept
  milestone for the duck mascot and "InkBoy" label.
