# InkBoy — v0.1

## Objective

Build a lightweight native application for Kindle devices,
initially targeting the Paperwhite 5.

The first milestone is a GTK2 application written in C
that displays an interactive "Hello, Kindle!" button.

## Target

- Device: Kindle Paperwhite 5 (2021)
- Firmware: 5.19.2
- Architecture: ARM 32-bit (armhf)
- Jailbreak: Véra
- Package manager: KPM
- Development host: Nexus (Ubuntu Server, x86-64)

## Stack

- C
- GTK2
- Docker (build environment)
- Make
- Git

## Functional Requirements

- FR01: Launch a GTK2 window.
- FR02: Display a "Hello, Kindle!" button.
- FR03: Handle touchscreen interaction.
- FR04: Print a message when the button is pressed.
- FR05: Exit cleanly.

## Build Requirements

- Use Docker for isolated cross-compilation.
- Provide a Makefile.
- Generate ARM executables in build/.
- Verify Kindle ABI and runtime compatibility.
- Document dependencies and build instructions.
- Docker must not be required on Kindle.

## Constraints

- Minimal dependencies.
- No emulator functionality in v0.1.
- Do not modify Kindle system components.
- Do not assume Ubuntu ARM libraries are Kindle-compatible.
- Implement incrementally.

## Acceptance Criteria

- ARM executable builds successfully.
- Application launches on PW5.
- Window and button are visible.
- Touchscreen interaction works.
- Application exits cleanly.
