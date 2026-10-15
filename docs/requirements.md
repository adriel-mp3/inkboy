# InkBoy — v0.1

## Objective

Build a lightweight independent native application for Kindle devices,
initially targeting the Paperwhite 5. InkBoy is not an emulator frontend and
must not incorporate Gambatte-K2 or its game engine.

The first milestone was a GTK2 application written in C that displayed an
interactive "Hello, Kindle!" button. Stage 2 now uses a minimal, statically
linked FBInk renderer to display the existing adriel.cloud duck mascot and the
text "InkBoy" directly on the e-ink display. It remains an independent
application, not an emulator frontend.

## Target

- Device: Kindle Paperwhite 5 (2021)
- Firmware: 5.19.2
- Architecture: ARM 32-bit (armhf)
- Jailbreak: Véra
- Package manager: KPM
- Development host: Nexus (Ubuntu Server, x86-64)

## Stack

- C
- FBInk sources vendored as a minimal static dependency for the Stage 2 proof
  of concept; the exact selection and PW5 observations are documented in
  [docs/experiments/02-rendering.md](experiments/02-rendering.md)
- librsvg/Cairo on the development host only, for SVG-to-bitmap conversion
- Docker (build environment)
- Make
- Git

## Functional Requirements

- FR01–FR05: Preserve the completed milestone behavior where still applicable;
  the first graphical screen supersedes the GTK2 demo as the Stage 2 runtime.
- FR06: Render the original duck SVG as a monochrome bitmap; do not invent a
  replacement mascot.
- FR07: Display the label "InkBoy" with a restrained monochrome composition.
- FR08: Keep rendering independent of emulator or game-engine code.
- FR09: Restore the saved Kindle display state safely on normal exit.
- FR10: Render once and perform one explicit e-ink refresh; do not run a
  refresh or animation loop.

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
- The Stage 1 window/button criteria remain historical; Stage 2 has no input or
  interactive menu.
- The duck and `InkBoy` title render in a centered monochrome composition.
- The application exits cleanly and attempts restoration on normal signals.
- Physical rendering and restoration are verified on the Kindle Paperwhite 5
  after a normal termination test.
