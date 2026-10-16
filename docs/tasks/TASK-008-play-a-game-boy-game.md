# TASK-008 — Play a Game Boy Game

- Milestone: M5 — Playable Prototype
- Status: Complete — gameplay, controls and shutdown validated on physical PW5 through TASK-009
- Dependencies: TASK-007 completed
- Parallel execution: Not independent; implement after TASK-007, with isolated worktrees allowed only when the modular interfaces are established
- Coordination: Shares the modular application interfaces, `Makefile`, SameBoy Libretro artifact, physical PW5, ROM, and deployment flow; coordinate Kindle access and never overwrite validated experiment deployments

## Objective

Run Pokémon Yellow on the Kindle Paperwhite 5 and allow continuous gameplay
through virtual touchscreen controls.

## Scope

- Integrate the Libretro core callbacks into the centralized application loop.
- Load an initial ROM from a fixed path.
- Map touchscreen regions to the Game Boy virtual controls.
- Start, run, and terminate a gameplay session safely.
- Preserve the existing scaling, timing, frame-update, e-ink refresh, and
  framebuffer-restoration behavior.

## Exclusions

- No menus, ROM management, runtime configuration, or machine-state system.
- No general-purpose frontend, dynamic ROM selection, or additional platforms.
- No change to the validated FBInk dependency or Kindle deployment model.

## Acceptance criteria

- The application starts Pokémon Yellow from the fixed ROM path on the PW5.
- Virtual touchscreen controls operate the Game Boy direction and action
  buttons during continuous play.
- Video callbacks reach the existing rendering path without known timing or
  refresh regressions.
- The application exits safely and restores the prior framebuffer on normal
  termination.
- Host validation and the complete physical PW5 gameplay test pass.

## Validation

Run the modular application's unit tests and `make host` before integration.
Build and inspect the ARM artifact through the existing Docker workflow, deploy
it to an isolated PW5 test directory with a verified Pokémon Yellow ROM, and
validate startup, touchscreen gameplay, sustained execution, safe termination,
zero refresh failures, and framebuffer restoration. Finish with `make inspect`
and `git diff --check`.

## Related experiments or decisions

- [`TASK-007 — Modularize Application`](TASK-007-modularize-application.md)
- [`05.3 — Libretro frame delivery evidence`](../experiments/05.3-libretro-frame.md)
- [`05.4 — Controlled continuous Libretro rendering research`](../experiments/05.4-libretro-continuous-rendering.md)

## Validation completion — 2026-10-16

Pokémon Yellow ran on the physical PW5 with the verified pinned core/ROM.
TASK-009 completed matched 30/60-second runs with zero refresh failures, plus a
69-second manual session with 67 control-to-core samples. The user confirmed
visible START/A response, EXIT/cancel behavior and framebuffer restoration.
EXIT, SIGINT, SIGTERM, SIGHUP and injected touchscreen-failure paths were
validated. Host checks, Docker host/ARM builds and ELF inspection passed.
The prototype retains the measured 1 FPS synchronous presentation limitations;
Power is not a supported controlled exit. Full evidence is in the
[TASK-009 report](../experiments/09-gameplay-runtime.md).
