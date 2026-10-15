# TASK-002 — Investigate synchronous refresh blocking

- Milestone: M4 — Rendering Performance Research
- Status: Complete
- Dependencies: TASK-001 completed baseline
- Parallel execution: Independent after TASK-001; suitable for a separate Git worktree for analysis and documentation
- Coordination: Shares `tools/libretro_live.c`, the live-probe build/deployment workflow, and the physical PW5 with other M4 work; coordinate Kindle access and preserve the existing deployed probe

## Objective

Quantify how synchronous FBInk rendering and refresh completion affect
SameBoy emulation throughput on the Kindle Paperwhite 5, and determine
whether display blocking is the dominant cause of reduced throughput.

## Scope

- Analyze the current single-threaded execution flow and its timing boundaries.
- Distinguish `retro_run()` cost from callback, conversion, FBInk draw,
  refresh submission, refresh completion, scheduling, and sleep costs.
- Design and execute controlled comparisons using the existing live rendering
  and capture-only modes with matching ROM, duration, and warmup parameters.
- Make physical PW5 capture-only versus physical PW5 live-rendering the
  primary performance comparison, using the completed TASK-001 baseline as
  additional evidence.
- Use host capture-only only as functional validation of the probe, core
  loading, callback path, and argument handling; do not use it as a direct
  hardware-performance comparison.
- Preserve the current E-Ink rendering and refresh behavior while identifying
  the dominant blocking interval and the next bounded investigation.

## Exclusions

- No threads or producer/display split.
- No asynchronous refresh.
- No waveform or refresh-policy changes.
- No frontend or control integration.
- No SameBoy modifications.

## Acceptance criteria

- The current single-threaded timing flow is documented with clear ownership
  of `retro_run()` and display-related intervals, including the fact that
  callback copy is nested within `retro_run()` and is separately attributed.
- Physical PW5 capture-only and live-rendering comparisons use the same binary,
  ROM, warmup, duration, display-rate argument, and measurement definitions.
- Per-stage timings, emulation FPS, display FPS, CPU usage, refresh counts,
  skipped frames, and refresh failures are recorded for each comparison.
- The contribution of synchronous FBInk draw and refresh completion to the
  throughput gap is quantified without changing rendering behavior.
- Display-path fraction is calculated as
  `(convert_seconds + nearest_scale_seconds + draw_seconds +
  refresh_submit_seconds + refresh_wait_seconds) / elapsed_seconds`.
- Synchronous-refresh fraction is calculated as
  `(draw_seconds + refresh_submit_seconds + refresh_wait_seconds) /
  elapsed_seconds`.
- Physical validation completes with zero refresh failures and successful
  framebuffer restoration.
- The results identify whether synchronous display blocking is dominant and
  define the next performance task or investigation.

## Validation

Run the existing host capture-only live probe as a smoke test only. Build and
inspect the ARM artifact using the existing Docker workflow, deploy through
`make deploy-libretro-live`, and run two physical PW5 cases with the same
SameBoy binary, ROM, 10-second duration, 1 FPS argument, and 180-frame
warmup: one with `--capture-only` and one with live rendering using the
established `WFM_AUTO` and synchronous completion behavior. Retain separate
complete logs. Compare physical `emu_fps`, `avg_emu_frame_ms`, and sleep time;
calculate display-path fraction, synchronous-refresh fraction, and throughput
loss as `1 - (live_emu_fps / capture_only_emu_fps)`. Host results do not count
as hardware-performance evidence.

## Related experiments or decisions

- [`TASK-001 — Repeat instrumented PW5 performance baseline`](TASK-001-performance-investigation.md)
- [`05.4 — Controlled continuous Libretro rendering research`](../experiments/05.4-libretro-continuous-rendering.md)
- The documented decision to retain the single-threaded architecture and
  synchronous completion until measurements justify a separate design change

## Completion

The host smoke test, ARM artifact inspection, deployment, and paired physical
PW5 capture-only/live-rendering comparison were completed. Both physical runs
used the same verified ARM binaries, ROM, 10-second duration, 1 FPS argument,
and 180-frame warmup. Results are recorded in the related experiment.
