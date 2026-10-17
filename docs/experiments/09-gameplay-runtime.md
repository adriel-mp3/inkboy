# TASK-009 — Gameplay runtime instrumentation and validation

Archived experiment record. Its task-specific build and staging targets were
removed; use the current profiling workflow in the root README for new runs.

Date: 2026-10-16. Status: complete — implementation, local checks, Docker rebuild,
matched PW5 profiling, physical shutdown and Power observation validated.
The controlled results below are physical measurements; input-to-display latency
and the driver-selected waveform remain unmeasured.

## Behavior and measurement boundaries

`inkboy_input_poll()` and `inkboy_input_wait()` return
`enum inkboy_input_result`: `INKBOY_INPUT_OK` continues polling,
`INKBOY_INPUT_STOP` requests normal termination on a valid EXIT release,
and `INKBOY_INPUT_ERROR` requires cleanup and a nonzero process status.
EXIT is handled during warmup as well as gameplay. Moving out of EXIT cancels
that contact, including when it subsequently returns inside the button.

SIGINT, SIGTERM and SIGHUP handlers only record a `sig_atomic_t` signal number.
The application checks for a stop before another emulation run or presentation,
including after an interrupted sleep. Hardware calls already in progress remain
synchronous; the application cannot promise an immediate exit while they block.

The ordered cleanup releases/cancels input, closes the core, restores the saved
framebuffer, waits for restoration, frees the dump/buffers, and closes FBInk.
Logs expose each cleanup stage and result. Failure of presentation completion,
framebuffer restoration, restoration completion, or FBInk close produces a
failure status. Touchscreen release/close errors are logged explicitly; closing
the descriptor is still attempted after a release error. The final
`runtime event=final` line is authoritative for process status; the preceding
profile summary is taken before cleanup. Restoration cannot be guaranteed after
SIGKILL, power loss, or crashes.

Interrupted completion waits retry the same marker before buffers are reused or
restored. Other completion failures are reported and trigger cleanup.

Profiling uses CLOCK_MONOTONIC nanoseconds. Each `profile` line also includes a
CLOCK_REALTIME UTC epoch timestamp. Metrics reset after warmup, and warmup has
its own summary. Each presentation emits a cumulative report and shutdown emits
a summary even for zero completed frames. Timings include count, total, mean,
maximum, and last sample in milliseconds:

| Field | Boundary / interpretation |
| --- | --- |
| `emulation_*` | `retro_run`, including its callbacks |
| `input_poll_*` | Application input polling, including event dispatch |
| `input_gap_*` | Interval between polls; exposes refresh-induced input stalls; not exclusive CPU work |
| `callback_copy_*` | Valid video callback storage allocation/copy; nested in emulation |
| `convert_*` | XRGB8888 source to YA grayscale, including buffer growth |
| `scale_*` | Nearest sampling of YA into the game viewport |
| `compose_*` | Screen allocation/clear and static UI drawing; excludes conversion/scaling |
| `region_pack_*` | Experimental-only packing of the viewport into a contiguous draw buffer |
| `fbink_draw_*` | `fbink_print_raw_data` |
| `refresh_submit_*` | Full-screen `fbink_refresh` or experimental `fbink_refresh_rect` |
| `refresh_wait_*` | `fbink_wait_for_complete` for the presentation |
| `input_to_state_*` | First changed application control dispatch to next valid core joypad query |

`input_to_state` does not include time an event waited in evdev, nor the time
until a panel update becomes visible. Multiple changes before the next core
query are coalesced and timed from the first change. No input produces zero
samples rather than an invented latency measurement.

Conversion now occurs at source resolution before scaling; TASK-008 performed
both in a single output-pixel loop. Pixel equivalence is tested with padded
pitch, portrait and non-integral scaling, but the separation changes the work
distribution. Compare regions using the same instrumented implementation;
do not present the change as measured performance improvement over TASK-008.

`runs`, `callbacks`, `valid_frames`, `duplicate_frames`, `invalid_frames`,
`superseded_frames`, `presentations`, `refresh_attempts`, and `refresh_failures`
are phase-local counts. Superseded frames are valid producer frames overwritten
before selection for display, not core callback failures. The initial gameplay
presentation selects the final warmup frame and is counted as a presentation
but not as a newly generated gameplay frame. Rates use the current phase's wall
time. `cpu_pct` uses user+system `getrusage` time since the phase began;
`rss_kib` comes from `/proc/self/status`, and `peak_rss_kib` is process-lifetime
`ru_maxrss`. `interval_ms` includes time between emitted reports, including
synchronous display work; the first interval starts at phase initialization.

`display` lines record screen/viewport origins, actual FBInk draw rectangle,
submitted refresh rectangle, requested AUTO waveform, and `flashing=1`.
The driver-selected waveform remains `unknown`; the public API does not
provide it here. Refresh failures count unsuccessful submissions/completion
waits; drawing failures are reported separately as presentation errors.

For latency attribution, compare stage totals and per-refresh `*_last_ms` values.
Subtract callback-copy time from emulation before adding exclusive stage totals.
Exclude input-gap and input-to-state latency from that sum. Physical evidence
is still needed to identify the dominant PW5 stage. Do not infer it from mock
FBInk or host timings.

## Build and isolated artifacts

Normal reproducible build/staging entrypoints:

```sh
make test
make libretro-runtime-test
make libretro-scale-test
make host
make deploy-task009
make inspect INSPECT_BINARY=build/task009/inkboy
make inspect INSPECT_BINARY=build/task009/inkboy-game-region
git diff --check
```

`deploy-task009` stages artifacts under `build/task009-deploy`; like
`deploy-task008`, it does not perform an SSH transfer. The two diagnostic
executables use distinct objects and a fixed core path under
`/mnt/us/inkboy/task009-runtime`. Existing `build/inkboy`,
`build/task008-deploy`, validated research tools, and previous device deployments
are not replaced by these targets.

Artifacts in `build/task009/`:

- `inkboy`: instrumented full-screen reference.
- `inkboy-game-region`: compile-time experimental comparison. The first frame
  is a full composition/refresh; later frames draw only the fitted game region.
  If the fitted game geometry changes, it redraws the full composition to clear
  the old viewport. Both cases keep AUTO, `is_flashing=true`, 1 FPS and synchronous completion.
  The refresh rectangle is taken from FBInk after drawing so viewport origin
  handling is delegated to FBInk. Compare steady-state separately from frame 1.
- `sameboy_libretro.so`: unchanged existing pinned ARM core.
- `touch-failure.so`: test-only LD_PRELOAD shim that skips the fixed 180 warmup
  polls, then reports POLLHUP after five seconds of gameplay touchscreen polling,
  ensuring that a framebuffer dump and first presentation already exist. It leaves the device,
  driver and grab untouched. This is fault injection, not physical removal.
- `power_probe`: read-only 30-second observer for evdev sources advertising
  KEY_POWER. It never grabs an input source. Exit 2 means no advertised evdev
  source, not proof that firmware exposes no other power notifications.

Deploy only into a new isolated directory. Check that the destination does not
already contain a validation run before copying. Once staged, compare host and
device SHA-256 hashes of every artifact and record the verified ROM's hash.
The generated `SHA256SUMS` manifest supports `sha256sum -c SHA256SUMS` in the
deployment directory.
Do not modify the root filesystem, driver binding or system power services.

## Local validation evidence

All seven new host test executables pass, alongside existing layout/control
mapping, SameBoy runtime smoke and reference scaler tests:

- Pipe-backed type-B and single-touch input: ordinary controls, EXIT release,
  cancellation, SYN_DROPPED, POLLHUP, invalid descriptor and short read.
- Actual application loop with injected adapters and clock: normal/error input,
  warmup and gameplay EXIT, SIGINT/SIGTERM/SIGHUP, interrupted sleep, startup/
  emulation/presentation failures, and cleanup ordering/status propagation.
- Byte-equivalent frame conversion/scaling against the original fused
  TASK-008 implementation, including padding, bounds and different aspect ratios.
- Renderer with a fake FBInk adapter, for full and region builds: initial full
  refresh, matching cropped pixels, viewport-origin-aware rectangles, draw/
  submission/wait failures, EINTR retry, restore/restore-wait/close failures, empty rectangles,
  and restoration before dump free and framebuffer close.
- Profiling with zero counts, finite report fields, stage sample accounting,
  and counters checked against the real host SameBoy core.
- Touch fault injection: ignore unrelated descriptors, skip warmup, inject only
  after the five-second gameplay deadline, and delegate other polls normally.

The initial sandbox denied Docker and SSH access. After the user enabled
escalated execution, both became available. A forced Docker rebuild revealed
that the earlier native host FBInk cache referenced `__isoc23_strtoul`, which
the container libc could not resolve. Rebuilding the same pinned FBInk sources
inside Docker fixed the host link; no dependency sources or system libraries
were changed. Both host application and task-specific ARM application objects
were then forcibly recompiled in Docker, rather than merely checking cached
outputs. `make host deploy-task009` also completed. The earlier native fallback
commands are retained here as provenance:

```sh
make TARGET=host build/inkboy-host build/touch_probe-host build/libretro_probe-host
make TARGET=kindle \
  KINDLE_TC="$PWD/.toolchain/arm-kindlehf-linux-gnueabihf" \
  KINDLE_CC="$PWD/.toolchain/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc" \
  task009-artifacts
```

Both executables and the observer are ELF32 ARMv7 EABI5 hard-float, using
`/lib/ld-linux-armhf.so.3`. The application dependencies remain `libdl.so.2`,
`libm.so.6` and `libc.so.6`. The failure-injection artifact is an ARM shared
object. ELF inspection does not establish physical runtime compatibility.
Complete local outputs are retained in ignored `build/task009-*-*.log` files.

AddressSanitizer and UndefinedBehaviorSanitizer also passed for the input,
application loop, frame conversion/scaling and experimental renderer tests.
LeakSanitizer cannot run under this sandbox's tracing restrictions and was
disabled for those runs; no leak-check result is claimed.

The preserved TASK-008 executable (`build/inkboy` and
`build/task008-deploy/inkboy`) has SHA-256
`3eb76b54bedd8e93d187a45186006fa16eea552b53ddca4fbe45861741cf830e`.
The unchanged ARM core has SHA-256
`d8ee424a1294db4919470cae7e76bc9be629d38063806ea6a3668f22e4b05fa9`.
Recompute task-specific executable hashes after any rebuild and record them in
the physical run metadata, rather than relying on stale documentation hashes.

## Physical PW5 profiling results

Escalated SSH access succeeded. The artifacts were deployed only into the new
`/mnt/us/inkboy/task009-runtime` directory, and every entry in `SHA256SUMS`
verified on the device. TASK-008 binary/core hashes matched their original
host artifacts. Device: PW5/MT8110, firmware 5.19.2, kernel 4.9.77-lab126,
battery 84%, not charging. Existing firmware services were left running;
no power-service, governor, root filesystem or driver changes were made.

ROM SHA-256:
`8cbaa499397e4f1a679c992ea9382a2dd7942ab398b48c19829c2d9529de47bf`.
Full-screen binary SHA-256:
`f050099c1685fc9c92f0267cd0514cc725f5a188a9d08545090a2d9a6ad477f4`.
Region binary SHA-256:
`73b7dc5816677d5e063a5c6b8c2c0e4466e4df7bdd5cbb38c4282da34a61db58`.
Raw metadata is retained in `build/task009-device-metadata.log`, and all
artifact hashes are in `build/task009/SHA256SUMS`.

Runs were sequential, in the order full 30 s, region 30 s, full 60 s, region
60 s. Each timer began after the first completed gameplay presentation and
sent SIGTERM to the exact PID after verifying `/proc/PID/exe`. Thus reported
gameplay elapsed time also includes the initial presentation and SSH signal
delivery. All runs used 180 warmup frames, the same core/ROM, no manual input,
the retained 1 FPS deadlines, AUTO flashing and synchronous completion. Both
60-second runs performed 60 presentations. The user confirmed correct game
display and visual restoration in both modes.

| Metric | Full 30 s | Region 30 s | Full 60 s | Region 60 s |
| --- | ---: | ---: | ---: | ---: |
| Gameplay elapsed (s) | 30.770 | 30.768 | 60.755 | 60.829 |
| Emulation runs | 658 | 685 | 1232 | 1289 |
| Emulation FPS | 21.384 | 22.263 | 20.278 | 21.191 |
| Display updates / FPS | 30 / 0.975 | 30 / 0.975 | 60 / 0.988 | 60 / 0.986 |
| Superseded frames | 628 | 655 | 1172 | 1229 |
| Invalid frames / refresh failures | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| CPU (%) | 63.990 | 63.963 | 63.403 | 63.457 |
| RSS (KiB) | 14588 | 14940 | 14652 | 14968 |
| Average emulation frame (ms) | 27.007 | 27.076 | 28.202 | 28.270 |
| Average callback copy (ms) | 0.099 | 0.091 | 0.104 | 0.105 |
| Average grayscale conversion (ms) | 0.450 | 0.467 | 0.450 | 0.456 |
| Average scaling (ms) | 21.664 | 21.679 | 21.706 | 21.681 |
| Average composition (ms) | 9.455 | 9.447 | 9.402 | 9.383 |
| Average region packing (ms) | — | 0.635 | — | 0.666 |
| Average FBInk draw (ms) | 30.532 | 3.377 | 30.576 | 2.896 |
| Average refresh submission (ms) | 0.562 | 1.373 | 0.601 | 1.402 |
| Average refresh completion wait (ms) | 366.549 | 366.196 | 367.556 | 366.908 |
| Maximum interval between input polls (ms) | 458.076 | 432.034 | 463.209 | 437.050 |

The first presentation was full-screen in every case and is included in the
table's averages. Steady-state region refreshes were `420x378 @ (408,346)`;
full-screen refreshes were `1236x1648 @ (0,0)`, as returned/submitted through
FBInk. Excluding the initial update, 60-second wait distributions were:
full min/mean/p95/max = 365.285/367.572/369.341/369.591 ms;
region = 364.852/366.916/369.755/370.618 ms.

All four sessions ended with reason=SIGTERM/status=0. Touchscreen release,
close, framebuffer restore, restore completion and FBInk close returned zero.
Restoration completion waits were 365.340, 366.222, 367.897 and 367.829 ms.
Complete raw logs: `build/task009-full-30s.log`,
`build/task009-region-30s.log`, `build/task009-full-60s.log`,
`build/task009-region-60s.log`.

### Shutdown and manual control evidence

| Trigger | Physical result | Restore / completion result | Raw log |
| --- | --- | --- | --- |
| SIGTERM | status=0 in all four matched runs | 0 / 0 | Full/region duration logs above |
| SIGINT | reason=SIGINT, status=0 | 0 / 0; completion 365.615 ms | `build/task009-sigint.log` |
| SIGHUP | reason=SIGHUP, status=0 | 0 / 0; completion 370.395 ms | `build/task009-sighup.log` |
| Injected touchscreen POLLHUP | reason=input-error, status=1 | 0 / 0; completion 366.028 ms | `build/task009-touch-failure.log` |
| Touchscreen EXIT | reason=EXIT, signal=0, status=0 | 0 / 0; completion 366.219 ms | `build/task009-exit-controls.log` |

The user confirmed visual restoration after the signal and injected-fault
sessions and confirmed that leaving/re-entering EXIT cancels the gesture,
followed by successful normal EXIT. The touchscreen fault was injected after
the first presentation using the diagnostic preload; it was not a driver
unbind or physical disconnection. In the fault log,
`cleanup stage=renderer_end result=1` carries the cumulative application status;
the independent restore/restore-wait result fields are zero. The final status 1
is the expected input failure, not a restoration failure.

The first manual EXIT session produced no control-to-state samples, so it is
not used to establish physical joypad delivery. A separate held-control session
(`build/task009-held-controls.log`) ran for 69.259 s, with 68 successful
presentations, zero refresh failures and 67 control-to-state samples. The user
held START/A and confirmed a visible game response. Maximum dispatch-to-core
latency was 0.168 ms; maximum polling gap was 466.188 ms. These two boundaries
must not be conflated. The user then exited through EXIT and confirmed visual
restoration; final reason=EXIT/status=0, restore=0, restore-wait=0 (365.040 ms).
This validates sustained gameplay/control delivery through the modular runtime.

### Power-button evidence and support boundary

The no-grab observer found `/dev/input/event0`, named `bd71828-pwrkey`, with
KEY_POWER capability. During the separate Power session it recorded:

```text
power fd=4 event_s=1792169979.991125 type=1 code=116 value=1
power fd=4 event_s=1792169979.991154 type=1 code=116 value=0
```

The runtime continued presenting after that event and did not receive a
termination signal attributable to Power. It completed 60 updates without
refresh failures and was terminated by the host's safety SIGTERM at gameplay
elapsed 60.878 s. Restore and restore-wait both returned zero (368.427 ms).
The user observed the screen close/reappear during the Power action. Continued
application drawing competing with firmware display changes is consistent with
that observation, but the exact visual transition was not instrumented.

Subsequent SSH attempts timed out or reported no route while the Kindle was
asleep. After the user woke it, SSH recovered and powerd reported `active`.
Firmware logs independently recorded `suspending` at local 14:01:34.301 and
`resuming` at local 14:03:17.027, on 2026-10-16 (device UTC was three hours
ahead of those local syslog stamps). Thus the later loss of connectivity was
associated with actual suspend, not the earlier Codex sandbox permission error.
The user confirmed the display no longer showed a running/redrawing game after
termination. No InkBoy or power observer process remained on reconnection.

Evidence: `build/task009-power-events.log`, `build/task009-power-runtime.log`,
`build/task009-power-transitions.log`, `build/task009-power-system-history.log`
and `build/task009-power-system-awake-retry.log`.

Power-to-exit remains explicitly unsupported: a raw key event is available,
but a safe cleanup/restore ordering relative to firmware screensaver and suspend
has not been established. No Power handler or power-service changes are added
based on that assumption. EXIT and the tested signals are the supported
controlled exits; pressing Power must not be described as terminating InkBoy.
The observation/documentation acceptance criterion is met without claiming
safe Power integration.

On final reconnection the device was active, battery 83%, not charging. Both
TASK-008 hashes were unchanged and all TASK-009 artifact hashes reverified.

### Latency attribution and decision

Emulation is the largest accumulated work cost: at roughly 28.2 ms/frame,
the full-screen confirmation spent about 34.7 s of its 60.8 s phase in
`retro_run`, including approximately 0.13 s of callback copying. Emulation alone
exceeds the nominal roughly 16.7 ms core-frame budget. Refresh completion is
the largest presentation delay: approximately 22.1 s total in that run, or
36% of wall time, synchronously blocking further input/emulation work.

Changing geometry reduced FBInk drawing substantially but did not materially
change the roughly 367 ms completion wait under the same flashing AUTO policy.
The measured polling gaps of up to 463 ms demonstrate application input stalls;
they do not measure a physical touch-to-visible-response latency. The higher
superseded-frame counts reflect newest-frame selection at the display cap.

These are two controlled duration pairs, not a statistical study or a ghosting
assessment. Keep the production policy unchanged. Any follow-up should evaluate
the refresh/waveform policy and its visual behavior as a separate optimization
task, using these full/region results as evidence rather than assuming smaller
rectangles alone remove the dominant wait.

## Repeatable physical validation protocol

Record UTC date, PW5 model/firmware, kernel, ROM and artifact hashes, battery/
charger state, orientation, relevant competing processes and device conditions.
Keep these conditions constant. Preserve full stdout/stderr on the host via
SSH/tee, with shell pipeline exit-status handling. Do not store reports on the
Kindle. Use the verified Pokémon Yellow path and the same core for every run.

Start the program in one terminal; the printed PID belongs to the shell that
is replaced by the executable:

```sh
set -o pipefail
ssh kindle 'cd /mnt/us/inkboy/task009-runtime && printf "control pid=%s\n" "$$" && exec ./inkboy' \
  2>&1 | tee build/task009-full-30s.log
```

After `profile event=presentation phase=gameplay` appears, allow at least
30 seconds of gameplay in the baseline and at least 60 in confirmation, then
send the intended signal to that exact printed PID from a second terminal.
Do not use a broad `pkill`/`killall`. Treat measured `elapsed_s` and timestamps
as the run boundaries rather than assuming remote startup was instantaneous.
For comparisons, use the same timed window and report actual elapsed time and
update count. Keep matching full/game runs free of manual gameplay input.

| Case | Action and required evidence |
| --- | --- |
| Full-screen baseline, >=30 s gameplay | SIGTERM to the recorded PID; all stage metrics, zero refresh failures, restoration API result and visual confirmation |
| Full-screen confirmation, >=60 s gameplay | Same binary/core/ROM/conditions; compare rates and timing distributions |
| Region baseline and confirmation | Repeat both durations with `exec ./inkboy-game-region`; same cadence, waveform and flashing; separate initial full frame from steady-state |
| EXIT | Press/release EXIT inside its target during gameplay; verify reason=EXIT, status=0 and visual restoration; repeat during warmup if reachable |
| Cancelled EXIT | Move outside and return before release; verify continued gameplay, then exit normally |
| SIGINT / SIGTERM | Separate sessions with each signal, checking final reason, cleanup order/status and visual restoration |
| Touchscreen fault | Run `LD_PRELOAD=./touch-failure.so ./inkboy`; require injected POLLHUP, input-error, nonzero status, touch release and successful visual restoration |
| Power observation | Run `./power_probe`, perform a brief press/release while observing; retain advertised source/name, timestamps and raw type/code/value or no-source evidence |
| Power during gameplay | Observe application signal/final logs and actual firmware behavior while pressing briefly; safely terminate via SSH if it remains running |

For each session, record control-to-state samples when present, draw/submission/
completion durations and rectangles, superseded/invalid frames, RSS/CPU,
refresh failures, exit trigger, cleanup stages, final status and visual outcome.
Rank the exclusive timing totals and per-refresh distributions to identify the
dominant latency sources; retain differences between warmup and gameplay.

No dedicated Power handling has been added: the observed raw key event and
firmware suspend behavior do not establish a safe application-level lifecycle
contract. Preserve the explicit unsupported behavior recorded above and the
firmware's existing power behavior.

The production refresh policy remains unchanged. Use the matched physical
measurements above to define a separate optimization task.
