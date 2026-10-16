# TASK-009 — Profile and Stabilize PW5 Gameplay Runtime

- Milestone: M5 — Playable Prototype
- Status: Planned
- Dependencies: TASK-008 gameplay build and first physical PW5 run
- Parallel execution: Not independent; shares the application loop, renderer,
  input path, SameBoy core, ROM, PW5, and isolated deployment directories
- Coordination: Preserve the TASK-008 artifacts and previous experiment
  deployments; do not change the validated probe binaries during profiling

## Objective

Measure the real TASK-008 gameplay runtime on the Kindle Paperwhite 5 and fix
the observed control and shutdown defects before selecting an optimization for
e-ink presentation.

## Scope

- Instrument the application with separate timings for emulation, input
  polling, video callback copying, grayscale conversion, scaling, composition,
  FBInk drawing, refresh submission, and refresh completion wait.
- Record emulation rate, display rate, dropped frames, refresh failures, CPU,
  memory, update geometry, and shutdown/restoration status.
- Reproduce and fix the EXIT control path, including the current handling of
  the input poll return value.
- Observe and document whether the PW5 Power button produces an event or
  signal available to the application. Add handling only if the device makes a
  safe application-level event available.
- Verify normal termination through EXIT, `SIGINT`, `SIGTERM`, and touchscreen
  failure, with framebuffer restoration in each supported case.
- Compare full-screen and game-region presentation costs using the same ROM,
  core, refresh mode, and display cadence.

## Exclusions

- No final refresh optimization or policy change based on unmeasured guesses.
- No increase to the display rate above the current conservative 1 FPS policy.
- No threading, display queue, menu, runtime configuration, save-state
  system, ROM manager, or additional platform.
- No change to the pinned SameBoy core, the validated standalone probes, the
  FBInk dependency, or the Kindle root filesystem.
- No guarantee of framebuffer restoration after `SIGKILL`, power loss, or
  process crash.

## Known observations to reproduce

- TASK-008 currently redraws and refreshes the full screen once per second,
  which can cause a complete flash even when only the game viewport changed.
- The EXIT release path is reported by the input module but is not currently
  propagated as a stop request by the main loop.
- The application does not currently consume a dedicated Power-button event;
  the PW5 behavior must be confirmed rather than assumed.
- Synchronous e-ink completion can stall emulation and input processing.

## Acceptance criteria

- A repeatable PW5 profiling run produces a timestamped report for all timing
  stages and identifies the dominant sources of latency.
- EXIT reliably terminates the application and restores the previous
  framebuffer on the tested PW5.
- `SIGINT` and `SIGTERM` terminate safely and report restoration status.
- Touchscreen failure does not leave the process running indefinitely and
  causes the normal cleanup path to run.
- The Power-button behavior is documented with the observed device event or
  signal evidence; unsupported behavior is explicitly recorded.
- Full-screen and game-region presentation measurements use identical ROM,
  core, duration, cadence, and device conditions.
- Host tests, ARM build inspection, and the complete physical validation pass.
- No validated experiment deployment is overwritten.

## Validation

Run the existing host checks and the TASK-008 runtime smoke test before the
device run:

```sh
make test
make libretro-runtime-test
make libretro-scale-test
make host
make inspect
git diff --check
```

Build and stage the ARM artifacts in an isolated directory, then use the
verified Pokémon Yellow ROM on the PW5. Run at least one 30-second baseline
and one 60-second confirmation run. Record:

- device model and firmware;
- ROM and binary hashes;
- emulation and display rates;
- input-to-state and refresh timing where available;
- FBInk draw, refresh, and completion timings;
- refresh rectangle and waveform details;
- dropped frames and refresh failures;
- exit trigger, cleanup order, and framebuffer restoration result.

Do not merge a refresh-policy change as part of this task. Use the collected
measurements to define a separate optimization task if the evidence supports
one.

## Related work

- [`TASK-008 — Play a Game Boy Game`](TASK-008-play-a-game-boy-game.md)
- [`05.4 — Controlled continuous Libretro rendering research`](../experiments/05.4-libretro-continuous-rendering.md)
- [`TASK-002 — Investigate synchronous refresh blocking`](TASK-002-synchronous-refresh-blocking.md)
