# TASK-001 — Repeat instrumented PW5 performance baseline

- Milestone: M4 — Continuous rendering and performance research
- Status: Planned
- Dependencies: None; coordinate physical Kindle access and deployment

## Objective

Measure the remaining live-rendering costs before selecting the next
performance change.

## Scope

Repeat the existing instrumented Pokémon Yellow run on the physical PW5 using
the same 10-second duration, 1 FPS display rate, 180-frame warmup, and
`WFM_AUTO` policy. Record callback copy, grayscale conversion, FBInk draw,
refresh submission, completion wait, returned refresh rectangles, display
updates, skipped frames, CPU usage, and restoration.

Do not change the refresh policy, add threading, change the CLI default, or
integrate the probe into `src/main.c`.

## Acceptance criteria

- Host validation passes using the existing live-probe targets.
- The ARM artifact builds and is inspected through the existing workflow.
- The physical run completes with zero refresh failures and successful
  restoration.
- Returned refresh rectangles and per-stage timings are recorded.
- The results identify the next performance investigation or implementation
  task.

## Validation

Use the host and Kindle build/deployment commands documented in
[`docs/experiments/05.4-libretro-continuous-rendering.md`](../experiments/05.4-libretro-continuous-rendering.md),
then run the documented physical benchmark protocol and retain its output.

## Related experiments or decisions

- [`05.4 — Controlled continuous Libretro rendering research`](../experiments/05.4-libretro-continuous-rendering.md)
- The documented single-threaded rendering and conservative 1 FPS decisions
