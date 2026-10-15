# TASK-004 — SameBoy profiling tools and instrumentation cost

- Milestone: M4 — Continuous rendering and performance research
- Status: In progress — audio sample-rate experiment complete
- Dependencies: TASK-003 completed SameBoy performance investigation
- Parallel execution: Independent research task; suitable for a separate Git worktree because it should use isolated profiling builds and documentation
- Coordination: Shares the SameBoy build configuration, ARM toolchain, capture-only probe, physical PW5, and M4 experiment logs; coordinate device access and keep the validated baseline artifact unchanged

## Objective

Produce an evidence-backed inventory of profiling and instrumentation options
available for SameBoy on the host, ARM build environment, and physical PW5,
including the runtime and measurement costs of each viable option.

## Scope

- Inventory compiler, linker, emulator, system, and manual instrumentation
  facilities available in the repository, toolchain, host environment, and
  PW5 workflow.
- Classify each option by profiling resolution, required symbols or runtime
  support, output format, applicability to `retro_run()`, and whether it can
  observe the ARM capture-only workload.
- Build isolated profiling variants or microbenchmarks for viable options and
  measure code-size, startup, memory, throughput, and per-frame overhead where
  the option permits measurement.
- Compare instrumentation results against the unchanged TASK-003 capture-only
  baseline using fixed ROM, warmup, duration, and binary provenance.
- Recommend the smallest next profiling experiment for locating the dominant
  emulation-side cost, with explicit confidence limits and rollback conditions.

### Research similar performance issues

- Search public SameBoy GitHub issues and discussions, SameBoy Libretro
  implementation and documentation, Libretro performance discussions, ARMv7
  emulation performance reports, and reports from other embedded or
  low-powered Linux devices.
- Look for concrete evidence of below-nominal SameBoy frame rates, high
  `retro_run()` costs, ARMv7-specific limitations, compiler optimization
  problems, and unnecessary audio processing overhead.
- Record source URLs, relevant versions or hardware, benchmark results,
  technical findings, implemented solutions, and applicability to InkBoy.
- Distinguish verified solutions and reproduced measurements from hypotheses,
  anecdotal reports, and speculation.

### Investigate disabling audio processing

- Determine whether SameBoy Libretro provides a supported way to disable audio
  generation, including core options, frontend configuration, or build flags.
- Determine whether ignoring audio callbacks avoids internal audio processing
  or only discards already-generated samples.
- Assess possible effects on `retro_run()` cost, emulation accuracy, timing,
  determinism, and compatibility.
- Prefer existing supported configuration options; if none exists, document
  the limitation rather than proposing a workaround.

## Exclusions

- No production optimization or adoption of a profiling configuration.
- No SameBoy source changes, rendering changes, refresh-policy changes,
  threading, or frontend integration.
- No modification of the validated baseline artifact or deployment flow.
- No claim that host-only profiler results represent PW5 performance without a
  matched physical measurement.
- No audio modifications, audio disabling, or workaround implementation during
  this task.

## Acceptance criteria

- A repository-backed inventory identifies which candidate profiling and
  instrumentation tools are available, unavailable, or unsuitable, and why.
- For every viable candidate, the report records instrumentation method,
  observability, required build/runtime changes, and measured or explicitly
  bounded overhead.
- Candidate variants are compared with the TASK-003 baseline using the same
  ROM, 180-frame warmup, bounded measurement window, capture-only behavior,
  and complete binary hashes.
- The report separates host-only evidence from physical PW5 evidence and
  records throughput, `retro_run()` timing, code size, memory, and measurement
  perturbation where applicable.
- Public-source research records links, versions or hardware context, concrete
  findings, applicability to InkBoy, and clearly labels verified solutions
  separately from speculation.
- The audio investigation documents whether supported SameBoy Libretro
  configuration can disable audio generation, whether ignored callbacks still
  incur processing, and the expected effects on performance, timing, accuracy,
  and compatibility; if unsupported, that limitation is explicit.
- One next bounded profiling experiment is selected without authorizing an
  optimization implementation.

## Validation

Inspect the existing Makefile, Docker toolchain, SameBoy Libretro build rules,
probe outputs, and TASK-003 logs. Run host smoke tests for each supported
variant, inspect ARM artifacts through the existing Docker workflow, and use
the established capture-only PW5 protocol for candidates that can run on the
device. Retain commands, tool versions, artifact hashes, raw logs, and the
baseline comparison; do not alter the validated live-rendering path.

For the external research, retain direct source links and access dates, and
record the evidence supporting each finding. For audio, inspect the SameBoy
Libretro source/configuration and official documentation for supported options,
then document callback behavior and performance implications without changing
audio settings or introducing a workaround. Confirm that the task leaves the
application and SameBoy sources unchanged.

## Related experiments or decisions

- [`TASK-003 — SameBoy Emulation Performance Investigation`](TASK-003-sameboy-emulation-performance.md)
- [`TASK-002 — Investigate synchronous refresh blocking`](TASK-002-synchronous-refresh-blocking.md)
- [`05.4 — Controlled continuous Libretro rendering research`](../experiments/05.4-libretro-continuous-rendering.md)
- The M4 decision to preserve the single-threaded renderer and validated
  synchronous display behavior while isolating emulation-side costs

## Completed research and controlled audio sample-rate experiment

Date: 2026-10-15. This section records the research result and the first
bounded experiment selected from it. The broader profiling-tool inventory and
any future profiling variants remain open; this experiment did not modify the
SameBoy source, core build flags, rendering path, refresh behavior, threading,
or input behavior.

### Verified SameBoy callback path

The pinned submodule is SameBoy commit
`26a56bd6a122a6318a456abd80558408742d5adf`.
In [`libretro.c`](../../third_party/sameboy/libretro/libretro.c),
`retro_load_game()` calls `init_for_current_model()` after XRGB8888
negotiation and before `GB_load_rom_from_buffer()` (approximately lines
1592–1600). `init_for_current_model()` requests
`RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE` (approximately lines 738–750),
accepts rates between 8000 and 1048576 Hz, and otherwise calls
`GB_set_sample_rate()` with `GB_get_clock_rate() / 2` (the ordinary DMG clock
therefore selects 2097152 Hz). The APU callback is installed immediately
afterward, so ignoring the frontend audio callback does not avoid internal
audio generation.

The frontend now parses `--target-sample-rate HZ` before `retro_init()` and
`retro_load_game()`. The omitted-flag path still returns `false` for the
target-rate query. With `--target-sample-rate 48000`, it returns `true` and
writes 48000. Only `tools/libretro_live.c` changed. The measured-window
counters report audio batch callback calls and total stereo frames; the
existing `retro_run()` timing boundary and all other metric definitions are
unchanged.

This interpretation is consistent with the [official SameBoy changelog](https://sameboy.github.io/changelog/),
which documents frontend-rate audio rendering in SameBoy 1.0.4, and the
[official Libretro SameBoy documentation](https://docs.libretro.com/library/sameboy/),
which lists 48000 Hz as the core sample rate. The public Libretro discussion
of [audio resampling cost on Raspberry Pi-class hardware](https://forums.libretro.com/t/alsa-vs-tinyalsa-some-experiments-in-performance-and-latency/35706)
is supporting historical evidence, not a direct measurement of this pinned
core. SameBoy issue [#189](https://github.com/LIJI32/SameBoy/issues/189)
records a historical Libretro audio compatibility regression, so audio-rate
negotiation remains a compatibility surface even though this experiment uses a
documented API path.

### Build, provenance, and deployment preservation

Host validation passed for both modes. A queried once and returned false;
B queried once, returned true with 48000, and reduced the host audio frame
count from approximately 2.1 million per second to approximately 48000 per
second. ARM build validation through the existing Docker workflow produced an
ARMv7 hard-float executable. The SameBoy core hash was unchanged between
configurations.

| Artifact | Original/preserved | Experiment |
| --- | --- | --- |
| Validated frontend (`stage541`) | `9417743a535394078af264e60bef3a93c03d9018bbb6a69cecfe984495ec2993` | — |
| ARM frontend | — | `f70a92c61a23a5feed547e91f720346fbde8f2c2c7b6885c2a722ba9bf9fce55` |
| ARM SameBoy core | `d0c4893187b491f509765d94aeb153af214d47cbc611d88d0581032734654cc2` | same hash |
| Benchmark ROM | — | `464e14b7d42e7feea0b7ede42be7071dc88913f75b9ffa444299424b63d1dff1` |

The experiment frontend and unchanged core were copied to
`/mnt/us/inkboy/task004-audio`. The existing `/mnt/us/inkboy/stage541`
deployment was not overwritten and was rechecked with its original hashes.
Raw physical logs are retained as
`build/task-004-{A1,A2,A3,B1,B2,B3}-audio.log`.

### Physical PW5 capture-only results

All six runs used the same PW5, firmware, Pokémon Yellow ROM, ARM core,
180-frame warmup, 10-second duration, 1 FPS argument, and `--capture-only`.
Runs were alternating A1, B1, A2, B2, A3, B3. A is the unchanged frontend
behavior; B passes `--target-sample-rate 48000`. Values below are the final
10-second metrics from each raw log. Mean and SD are sample statistics over
the three repetitions.

| Configuration | FPS mean ± SD | `retro_run()` ms mean ± SD | CPU % mean ± SD | RSS KiB mean ± SD | Audio batches mean ± SD | Audio frames mean ± SD | Errors |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| A — fallback | 37.700 ± 0.036 | 26.484 ± 0.026 | 99.583 ± 0.025 | 4134.7 ± 37.8 | 377.7 ± 0.6 | 13,230,170 ± 20,272 | 0 |
| B — 48000 Hz | 59.717 ± 0.006 | 10.335 ± 0.004 | 61.143 ± 0.058 | 4213.3 ± 22.0 | 598.0 ± 0.0 | 479,885 ± 0 | 0 |

Relative to A, B improved measured throughput by 58.400%, reduced
`retro_run()` time by 60.976%, reduced CPU by 38.601%, increased RSS by
1.903% (78.7 KiB), increased callback count by 58.341% because batches are
smaller, and reduced generated audio frames by 96.373%. All runs negotiated
XRGB8888, had zero refresh failures, ended by duration, and delivered valid
video callbacks. The nominal core timing remained `59.727501 FPS` in every
run. The effect is repeatable and far outside the within-configuration
measurement variation, so it is meaningful for the PW5 capture-only workload;
no arbitrary minimum-effect threshold was applied.

### Findings, risks, and decision

The experiment verifies the hypothesis for this workload: the unchanged
frontend's ignored target-rate query leaves SameBoy on its high-rate fallback,
and negotiating 48000 Hz materially reduces `retro_run()` cost. The result is
not evidence that audio may be removed: audio generation remains active in B,
and the frontend still accepts/discards the batch callback. It also does not
claim that 48000 is universally correct for every Libretro host.

The supported Libretro target-rate negotiation is preferable to an audio-off
workaround. Potential risks are altered audio sample density, compatibility
with a host that expects another target rate, and audio behavior in a future
frontend that actually consumes samples. CPU/PPU frame timing and nominal
video timing were unchanged in this test, but audio-rate changes can affect
APU scheduling and should be revalidated if InkBoy later adds audio output.
InkBoy currently does not require audio output, and no SameBoy or production
configuration was changed as part of this task. The validated next action is
to preserve this result as an isolated profiling finding; adopting the flag
in the application remains a separate decision.
