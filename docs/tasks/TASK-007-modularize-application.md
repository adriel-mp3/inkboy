# TASK-007 — Modularize Application

- Milestone: M5 — Playable Prototype
- Status: Complete — modular runtime validated on physical PW5 through TASK-009
- Dependencies: None
- Parallel execution: Independent of M4 research; suitable for a separate Git worktree, but gameplay integration must wait for this task
- Coordination: Shares `src/main.c`, `Makefile`, generated build inputs, and the host/Kindle validation flow with future M5 work; preserve the validated renderer, touchscreen behavior, and physical PW5 deployment

## Objective

Separate the current application responsibilities into maintainable C modules
while preserving the existing behavior and keeping `main.c` responsible for
initialization, the main loop, and shutdown.

## Scope

- Inspect and partition the responsibilities currently concentrated in
  `src/main.c` into rendering, input, and shared layout modules.
- Establish a future emulation boundary without implementing Libretro or
  gameplay in this task.
- Define public interfaces in `.h` files and implementations in `.c` files.
- Preserve the existing FBInk rendering, framebuffer dump/restore, touchscreen
  handling, timing, scaling, and termination behavior.
- Add host-side unit tests for functions that can be isolated from Kindle
  hardware, and retain the existing build and deployment flow.

## Exclusions

- No new gameplay behavior, ROM loading, menus, configuration system, or state
  machine.
- No changes to the validated rendering policy, refresh behavior, or Kindle
  root filesystem.
- No replacement of the original duck asset or introduction of new runtime
  dependencies.

## Acceptance criteria

- Application responsibilities are separated into clear rendering, input, and
  shared layout modules with stable headers.
- The future emulation boundary does not introduce gameplay or Libretro
  behavior before TASK-008.
- `main.c` owns initialization, the main loop, and normal shutdown only.
- Existing behavior is preserved without known regressions, including normal
  framebuffer restoration and touchscreen handling.
- Host unit tests cover the newly isolated pure functions and pass reliably.
- Host and Kindle-oriented builds complete through the existing Makefile flow.

## Validation

Run the new unit-test target, `make host`, `make inspect`, and `git diff --check`.
Run the existing host probes as applicable. Build the Kindle artifact through
the established Docker workflow and perform a physical PW5 smoke test that
confirms rendering, touchscreen input, and normal framebuffer restoration.

## Related experiments or decisions

- [`05.4 — Controlled continuous Libretro rendering research`](../experiments/05.4-libretro-continuous-rendering.md)

## Validation completion — 2026-10-16

The modular renderer, shared layout, input and application lifecycle were
exercised by TASK-009's physical PW5 runs. Host checks and Docker host/ARM builds
passed. The user confirmed display, virtual control response, EXIT gesture
behavior and visual restoration. Signal and injected input-failure cleanup
also completed successfully. See the
[TASK-009 evidence](../experiments/09-gameplay-runtime.md). This validates the
modules in the integrated gameplay runtime; the earlier static duck-screen
verification remains historical evidence.
- [`03 — Touch input evidence`](../experiments/03-touch-input.md)
- [`TASK-008 — Play a Game Boy Game`](TASK-008-play-a-game-boy-game.md)
