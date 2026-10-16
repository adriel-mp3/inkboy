# TASK-005 — Validate 48 kHz audio negotiation with live E-Ink rendering

- Milestone: M4 — Continuous rendering and performance research
- Status: Complete — live validation finished; 48 kHz not adopted as default
- Dependencies: TASK-004 audio sample-rate experiment and its isolated frontend/core artifacts
- Parallel execution: Dependent on TASK-004 results; suitable for a separate Git worktree, subject to coordination of the shared live probe and physical PW5
- Coordination: Shares `tools/libretro_live.c`, the Docker ARM toolchain, the unchanged SameBoy core, the validated `stage541` deployment, isolated Kindle experiment directories, and physical PW5 access; never overwrite the validated deployment

## Objective

Determine whether negotiating a 48000 Hz target sample rate preserves reliable
continuous FBInk rendering while restoring nominal SameBoy emulation throughput,
and produce a measured recommendation on whether the setting should become the
default frontend configuration.

## Scope

- Inspect TASK-004's implementation, source references, measurements, hashes,
  and compatibility risks.
- Compare two live-rendering configurations using the same experimental
  frontend binary and unchanged SameBoy core:
  - A: omit `--target-sample-rate`, retaining the original audio fallback.
  - B: pass `--target-sample-rate 48000`.
- Use the established smooth scaling, `WFM_AUTO`, 1 FPS display cap, single
  thread, synchronous refresh submission, and refresh completion wait.
- Use identical ROM, 180-frame warmup, 10-second measurement window, input
  behavior, binary provenance, and capture definitions for every run.
- Repeat the cases in alternating order, with at least three A/B pairs, and
  retain complete raw logs.
- Measure emulation FPS, display FPS, `retro_run()` time, FBInk draw time,
  refresh submission and wait time, CPU, RSS, display updates, skipped frames,
  refresh failures, audio batch callbacks, audio sample frames, and errors.
- Verify framebuffer restoration and Kindle usability after each live run.
- Calculate means, standard deviations, paired differences, and relative
  changes, then determine whether 48000 Hz should become the default frontend
  configuration.

## Exclusions

- No SameBoy source or core build-flag changes.
- No changes to `src/main.c`, rendering behavior, scaling policy, refresh
  policy, waveform, timing policy, input behavior, or threading.
- No adoption of 48000 Hz as the default before physical validation and review.
- No modification or replacement of the validated `stage541` deployment.
- No deployment outside an isolated TASK-005 experiment directory.
- No unrelated optimization, profiling, audio-disable workaround, or product
  integration work.
- No commits are required as part of executing this task.

## Acceptance criteria

- TASK-004 implementation details and the pinned SameBoy callback path are
  confirmed before execution.
- Host smoke validation passes for both A and B, including target-rate query
  and response metrics, without changing the core.
- The ARM frontend builds through Docker, is identified as ARM hard-float, and
  the A/B runs use identical frontend and SameBoy core hashes.
- The validated `stage541` frontend and core hashes remain unchanged; all
  experimental artifacts and logs are isolated and documented.
- At least three alternating A/B live-rendering pairs complete on the same
  PW5 using the fixed ROM, warmup, duration, display cap, smooth scaling,
  `WFM_AUTO`, synchronous completion, and input behavior.
- Every run records emulation/display FPS, `retro_run()` timing, FBInk draw and
  refresh-wait timings, CPU, RSS, display/skipped-frame counts, audio counts,
  refresh failures, errors, shutdown reason, and restoration status.
- All successful runs have zero refresh failures, clean termination, valid
  framebuffer restoration, and no observed Kindle usability regression.
- The report includes mean, standard deviation, paired comparison, and relative
  improvement for the measured fields, separating emulation cost from display
  blocking.
- A documented recommendation states whether 48000 Hz is safe and beneficial
  as the default frontend configuration; otherwise the rollback preserves the
  original fallback behavior.

## Validation

Review [`TASK-004 — SameBoy profiling tools and instrumentation cost`](TASK-004-sameboy-profiling-tools.md),
`tools/libretro_live.c`, the pinned SameBoy Libretro source, and the existing
continuous-rendering experiment before changing anything. Run the existing
host live-probe target for both argument sets, then build the ARM frontend
through Docker without rebuilding or modifying SameBoy source or flags.

Copy the experimental frontend and unchanged core to a new remote directory,
for example `/mnt/us/inkboy/task005-live-audio`, and record local and remote
SHA-256 hashes before execution. Do not use or overwrite
`/mnt/us/inkboy/stage541`.

From the isolated directory, run the alternating physical protocol:

```sh
ROM="/mnt/us/games/Pokemon - Yellow Version - Special Pikachu Edition (USA, Europe) (GBC,SGB Enhanced).gb"
./libretro_live ./sameboy_libretro.so "$ROM" 10 1 \
  --warmup 180
./libretro_live ./sameboy_libretro.so "$ROM" 10 1 \
  --warmup 180 --target-sample-rate 48000
```

Repeat as A1, B1, A2, B2, A3, B3, retaining stderr/stdout logs. Confirm the
live path preserves smooth scaling, `WFM_AUTO`, one display update target per
second, synchronous completion, and normal framebuffer restoration. If any
run fails or restoration is not confirmed, stop the comparison, preserve its
log, remove only the isolated experimental deployment, and leave `stage541`
untouched. If B regresses live throughput, display reliability, audio
compatibility, or restoration, recommend retaining A and roll back by using
the omitted flag.

## Related experiments or decisions

- [`TASK-004 — SameBoy profiling tools and instrumentation cost`](TASK-004-sameboy-profiling-tools.md)
- [`05.4 — Controlled continuous Libretro rendering research`](../experiments/05.4-libretro-continuous-rendering.md)
- [`TASK-002 — Investigate synchronous refresh blocking`](TASK-002-synchronous-refresh-blocking.md)
- The decision to preserve single-threaded rendering, smooth scaling,
  `WFM_AUTO`, 1 FPS display limiting, and synchronous refresh completion while
  measuring emulation-side costs

## Completion — live-rendering A/B validation

Date: 2026-10-15. The host smoke tests, Docker ARM artifact verification,
isolated deployment, and three alternating physical A/B pairs were completed.
No SameBoy source, core build flag, `src/main.c`, rendering policy, refresh
policy, input behavior, or default sample rate was changed.

### Provenance and protocol

Host A and B both exited with status 0. A queried the target sample rate once
and returned false; B queried once and returned 48000. The pinned SameBoy core
remained commit `26a56bd6a122a6318a456abd80558408742d5adf`.

| Artifact | SHA-256 |
| --- | --- |
| Experimental ARM frontend | `f70a92c61a23a5feed547e91f720346fbde8f2c2c7b6885c2a722ba9bf9fce55` |
| SameBoy ARM core, A and B | `d0c4893187b491f509765d94aeb153af214d47cbc611d88d0581032734654cc2` |
| Pokémon Yellow ROM | `464e14b7d42e7feea0b7ede42be7071dc88913f75b9ffa444299424b63d1dff1` |
| Preserved `stage541` frontend | `9417743a535394078af264e60bef3a93c03d9018bbb6a69cecfe984495ec2993` |

The ARM frontend was an ELF32 ARM hard-float executable. The experimental
files were deployed only to `/mnt/us/inkboy/task005-live-audio`; the existing
`/mnt/us/inkboy/stage541` hashes remained unchanged. Every run used the same
ROM, frontend, core, PW5, smooth scaling, `WFM_AUTO`, 1 FPS display cap,
synchronous refresh completion, 180-frame warmup, and 10-second measurement
window. A omitted `--target-sample-rate`; B passed
`--target-sample-rate 48000`. Logs were collected with `pipefail`, preserving
the real remote process exit code.

Raw logs:

- `build/task-005-A1-live.log`, `build/task-005-B1-live.log`
- `build/task-005-A2-live.log`, `build/task-005-B2-live.log`
- `build/task-005-A3-live.log`, `build/task-005-B3-live.log`

### Physical results

Values are the final measured-window metrics. Means and SDs are sample
statistics over three runs. Synchronous display-blocking time is calculated as
`display_updates × (draw + refresh_submit + refresh_wait)`; conversion is not
included because it is frontend CPU work rather than synchronous display
blocking.

| Metric | A — fallback | B — 48000 Hz | Relative B vs A |
| --- | ---: | ---: | ---: |
| Emulation FPS | 29.737 ± 0.035 | 45.053 ± 0.025 | +51.508% |
| Display FPS | 0.883 ± 0.006 | 0.800 ± 0.000 | −9.434% |
| `retro_run()` | 26.652 ± 0.025 ms | 10.329 ± 0.023 ms | −61.245% |
| CPU | 88.323 ± 0.006% | 54.580 ± 0.040% | −38.204% |
| RSS | 13,344 ± 45 KiB | 13,260 ± 11 KiB | −0.629% |
| FBInk draw | 104.606 ± 0.135 ms | 106.390 ± 0.567 ms | +1.706% |
| Refresh submit | 1.275 ± 0.390 ms | 0.977 ± 0.360 ms | −23.353% |
| Refresh wait | 126.841 ± 0.503 ms | 207.728 ± 0.099 ms | +63.770% |
| Total sync display block | 2,094.492 ± 2.444 ms | 2,520.757 ± 7.866 ms | +20.352% |
| Display updates | 9.000 ± 0.000 | 8.000 ± 0.000 | −11.111% |
| Skipped frames | 294.333 ± 1.155 | 442.000 ± 0.000 | +50.170% |
| Audio batch callbacks | 303.333 ± 1.155 | 451.000 ± 0.000 | +48.681% |
| Audio frames | 10,620,178 ± 40,544 | 361,749 ± 0 | −96.594% |

All six physical runs exited with status 0, ended by duration, negotiated
XRGB8888, reported zero refresh failures, and reported
`restoration_status=complete-or-not-requested` after the normal restore path.
No error, failed refresh, invalid frame, or restoration-failure message was
present in any log. The PW5 remained operational between runs; the expected
full first refresh and subsequent smooth-scaled partial refresh rectangles
were produced in every run. No visual usability regression was observed
during the repeated live-rendering checks.

### Interpretation and decision

The 48 kHz setting clearly improves emulation-side cost: `retro_run()` is
approximately 61% faster, while overall live emulation throughput improves by
approximately 52%. The smaller throughput gain is expected because synchronous
display work remains on the same thread and consumes a larger fraction of the
measurement window.

The higher emulation rate also changes display scheduling. B consistently
produced 8 display updates and 0.80 display FPS versus 9 updates and about
0.88 FPS for A, while skipped frames rose by about 50%. This is not video
callback loss or a refresh failure—the display cap intentionally retains only
the newest frame—but it is a repeatable live-path scheduling difference. B's
per-update refresh wait and total synchronous display-blocking time were also
higher.

Decision: do not make 48000 Hz the default frontend configuration yet. The
setting is a confirmed emulation optimization and remains available through
the isolated optional flag, but the live-rendering scheduling and refresh-wait
regression must be resolved or explained before default adoption. Rollback is
simply to omit `--target-sample-rate 48000`; the validated `stage541`
deployment remains the preserved fallback.
