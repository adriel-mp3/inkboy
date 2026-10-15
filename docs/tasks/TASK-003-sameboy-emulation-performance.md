# TASK-003 — SameBoy Emulation Performance Investigation

- Milestone: M4 — Continuous rendering and performance research
- Status: Complete
- Dependencies: TASK-002 completed capture-only baseline
- Parallel execution: Independent research task; suitable for a separate Git worktree before any implementation is authorized
- Coordination: Shares the SameBoy build configuration, ARM toolchain, live-probe artifacts, physical PW5, and M4 experiment documentation; coordinate Kindle access and preserve validated binaries

## Objective

Determine why SameBoy reaches approximately 37.64 FPS in capture-only mode on
the Kindle Paperwhite 5 instead of its nominal 59.73 FPS, and define the
smallest controlled next investigation.

## Scope

- Inspect the ARMv7 compiler flags, optimization levels, ABI, and linker
  configuration used by the existing SameBoy Libretro build.
- Inspect SameBoy Libretro build configuration and identify relevant
  platform, core, and feature settings.
- Analyze `retro_run()` timing and available instrumentation to identify
  likely emulation-side bottlenecks independently of display blocking.
- Compare the TASK-002 physical capture-only result with nominal core timing
  and existing host functional measurements.
- Propose controlled, reversible experiments on the physical PW5 that isolate
  compiler/build configuration effects without changing rendering behavior.
- Preserve all existing validated rendering, restoration, deployment, and
  single-threaded behavior.

## Exclusions

- No SameBoy source modifications.
- No optimization implementation yet.
- No rendering behavior or refresh-policy changes.
- No threads or asynchronous execution.
- No frontend or control integration.
- No changes to the validated application or probe behavior.

## Acceptance criteria

- The current ARMv7 compiler, linker, ABI, and optimization configuration is
  documented with repository evidence.
- SameBoy Libretro build settings relevant to performance are identified and
  distinguished from frontend and FBInk costs.
- The `retro_run()` measurement boundary and capture-only timing limitations
  are documented without attributing nested callback work twice.
- At least one evidence-backed emulation-side bottleneck hypothesis is stated,
  with its uncertainty and expected observable effect.
- A controlled physical PW5 experiment matrix is proposed, including fixed
  ROM, warmup, duration, binary provenance, measured fields, and rollback
  conditions.
- The plan preserves current rendering, refresh, restoration, and deployment
  behavior and does not authorize an optimization implementation.

## Validation

Inspect the existing Makefile, Docker toolchain configuration, SameBoy Libretro
build rules, compiler/linker output, and TASK-002 logs. Use the existing host
capture-only target only to confirm functional reproducibility. For any future
approved experiment, build and inspect each ARM artifact through Docker, run
matched capture-only measurements on the same PW5 with the established ROM,
180-frame warmup, bounded duration, and complete metrics, then compare against
the TASK-002 37.64 FPS baseline. Do not alter the validated live-rendering
path during this task.

## Related experiments or decisions

- [`TASK-002 — Investigate synchronous refresh blocking`](TASK-002-synchronous-refresh-blocking.md)
- [`TASK-001 — Repeat instrumented PW5 performance baseline`](TASK-001-performance-investigation.md)
- [`05.4 — Controlled continuous Libretro rendering research`](../experiments/05.4-libretro-continuous-rendering.md)
- The M4 decision to keep the single-threaded rendering architecture and
  conservative refresh behavior until measurements justify a separate change

## Completion — Experiments A and B

Experiments A and B were completed on the physical PW5 without application
source changes, SameBoy submodule changes, rendering changes, refresh-policy
changes, threading, or changes to the validated deployment.

Experiment A used the validated ARM core three times. Experiment B used an
isolated ARM build with only `-ftree-vectorize` appended after the effective
`-O2`; all other SameBoy, toolchain, ABI, and probe settings were preserved.
Both configurations used the Pokémon Yellow ROM, 180-frame warmup, 10-second
measurement window, 1 FPS display argument, and `--capture-only`.

The baseline mean was 37.713 FPS with 0.057 FPS sample standard deviation and
26.473 ms `retro_run()` time with 0.039 ms standard deviation. The vectorized
mean was 37.687 FPS with 0.071 FPS standard deviation and 26.492 ms
`retro_run()` time with 0.050 ms standard deviation. The candidate therefore
did not improve throughput: it was 0.027 FPS slower on average (0.07%) and
0.019 ms slower per `retro_run()` (0.07%), within run-to-run variability.

All six runs loaded the ROM, produced 377–378 valid callbacks, ended by the
requested duration, and reported zero refresh failures or runtime errors.
Capture-only reported no display attempts, as expected. The validated
baseline deployment remained unchanged. The candidate artifact hash was
`42a79b81270fa336ed4a1275944940d835f8d91c888442a94dee0f74addbe9c0`; the
validated core remained
`d0c4893187b491f509765d94aeb153af214d47cbc611d88d0581032734654cc2`.

The first follow-up should inspect a more targeted emulation-side cost or
SameBoy build feature, using the same paired physical methodology. Broad
`-ftree-vectorize` should not be adopted based on this experiment.
