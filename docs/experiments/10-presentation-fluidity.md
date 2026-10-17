# TASK-010 — Presentation fluidity experiments

Archived experiment record. The old matrix builder and device launcher were
removed; the current Make profiling workflow builds and runs one selected
profile at a time.

Date: 2026-10-16. Status: implementation and local validation complete;
PW5 screening found no qualifying 2/4 FPS candidate. Further testing stopped
at the user’s request. No candidate adopted; the fluidity objective remains open.

## Implementation and preserved baseline

The baseline at the time of these experiments was full-screen flashing AUTO at
1 FPS. The standard build was subsequently changed to 2 FPS regional GC16 with
a 60-second flashing cleanup at the user's request. Experimental profiles
cache the complete initial composition and subsequently convert and nearest-scale
the source into a compact YA game buffer. They draw and refresh only that region,
using the actual FBInk rectangle so viewport origins stay FBInk's responsibility.
Controls are recomposed only for an initial/full presentation. Source dimension
or fitted rectangle changes clear/rebuild the full composition. Display layout is
established at open, as in TASK-009; live device rotation is not introduced.

Normal updates request AUTO or GC16 with `is_flashing=false`. Initial/full
updates, periodic regional cleanups and final full restoration request flashing
AUTO. No binary/monochrome conversion or dithering is introduced. The existing
grayscale conversion and nearest sampler are retained, with byte-equivalence
tests against the original scaler. The driver-selected waveform remains unknown.

Identical **scaled YA bytes** skip drawing and refresh. This detects unchanged
output even if source RGB values differ. A due cleanup takes precedence over
this skip. Cache contents and cleanup timestamp become authoritative only after
successful completion; cleanup intervals run from the preceding completed
initial/full refresh or cleanup. Cleanups do not accumulate a queue while blocked.

Display deadlines use a monotonic phase and advance past all expired slots after
blocking work. Time is re-read after sleep and refresh. Emulation debt is also
discarded after presentation, preserving single-threaded synchronous execution.
The scheduler correction also applies to the conservative baseline profile.
Compare contemporaneous builds; historical TASK-009 rates are context, not the
acceptance baseline for this changed scheduler.

Existing uncommitted TASK-009 changes were retained. Before edits, its working
diff was captured in ignored `build/task010/pre-task010.diff`, and hashes of
TASK-009 artifacts/deployment and TASK-008 deployment were recorded in
`build/task010/preserved-artifacts.sha256`. Those artifacts were not rebuilt or
overwritten; hash verification is part of final local validation. No SSH transfer
to an existing deployment was performed.

## Builds and artifacts

```sh
make test
make libretro-runtime-test
make libretro-scale-test
PYTHONDONTWRITEBYTECODE=1 python3 tests/test_profile_report.py
make docker-host-task010
make deploy-task010
make inspect INSPECT_BINARY=build/task010/kindle/inkboy-auto-4-60
sha256sum -c build/task010/preserved-artifacts.sha256
git diff --check
```

`docker-host-task010` recompiles test executables inside Docker, avoiding native
host libc artifacts. It builds all profiles under `build/task010/host/`.
`deploy-task010` builds ARM profiles under `build/task010/kindle/` and stages them
under `build/task010-deploy/`; it performs no network transfer. Objects are
isolated by platform and profile. Files are:

- `inkboy-baseline`: current conservative 1 FPS flashing full-screen policy.
- `inkboy-{auto,gc16}-{1,2,4,8}-{30,60}`: ordinary waveform, display FPS and
  cleanup interval in seconds. Building 8 FPS does not authorize skipping the
  physical four-FPS gate.
- `sameboy_libretro.so`: byte-identical copy of the existing pinned ARM core.
- `SHA256SUMS`: all executables and the core.

The core path is fixed to `/mnt/us/inkboy/task010-runtime/sameboy_libretro.so`.
The ROM and 180-frame warmup are unchanged. Do not run `deploy-task009` to
prepare TASK-010. Do not rebuild the validated historical probes or root filesystem.

## Measurement definitions

`enum inkboy_present_result` is `ERROR=-1`, `INITIAL=0`, `PARTIAL=1`,
`CLEANUP=2`, `UNCHANGED=3`. `display event=refresh kind=N` identifies each
requested update, rectangle, waveform and flash flag. INITIAL includes subsequent
full geometry rebuilds. PARTIAL means an ordinary presentation; the conservative
baseline still refreshes the full screen for that result.

In synchronous profiles, completed counters advance only after submission
**and** wait succeed. The asynchronous presentation follow-up defers waits for
ordinary updates, so its normal presentation counters measure accepted
submissions; the driver may merge queued requests, and these counters do not
prove that every request became a separate visible update.

| Metric | Interpretation |
| --- | --- |
| `presentations` | All successfully completed initial, normal and cleanup updates |
| `initial_presentations` | Initial/full geometry updates |
| `normal_presentations`, `normal_fps` | Successful ordinary updates / phase wall time; asynchronous runs count accepted submissions |
| `cleanups`, `cleanup_fps` | Completed periodic regional cleanups / phase wall time |
| `unchanged_images` | Selected images skipped without drawing or refreshing |
| `refresh_attempts`, `refresh_failures` | Submitted attempts and failed submission/completion calls |
| `initial_refresh_failures`, `normal_refresh_failures`, `cleanup_refresh_failures` | Failures attributed to the requested update kind |
| `initial_submit_*`, `initial_wait_*` | Initial/full refresh timing |
| `refresh_submit_*`, `refresh_wait_*` | Ordinary refresh timing, excluding initial and cleanup |
| `clean_submit_*`, `clean_wait_*` | Cleanup refresh timing |
| `input_gap_*` | Ordinary intervals between polls, excluding intervals containing cleanup/full rebuild |
| `input_gap_clean_*` | Intervals containing a periodic cleanup or later full geometry rebuild |

The initial first-refresh delay is exposed by `initial_*`; the first gameplay
poll starts a fresh polling interval. `input_gap_clean` includes the whole poll
interval containing that refresh, not just the wait. Final signal delivery may
end a run before another poll; the refresh timing is still recorded. Stage
timings include failed attempts; distributions must select only completed events.

Existing emulation, conversion, scaling, drawing, CPU/RSS and producer-frame
metrics remain. `display_fps` includes all successful kinds; use `normal_fps`
for the 90% criterion. Phase time includes initial and cleanup stalls, giving a
conservative effective rate. Static scenes intentionally reduce normal FPS;
test the cadence criterion in scenes with movement. Producer `superseded_frames`
counts overwrites before selection, including selections subsequently skipped;
it is not a count of completed updates.

Each successful update emits a cumulative profile line. `*_last_ms` values from
successive normal completion events provide the normal p95 distribution; cleanups
and initial updates have independent distributions. CPU/RSS remain phase/run
measurements rather than being attributed artificially to a waveform. The final
`runtime event=final` and restore/restore-wait lines remain authoritative for
cleanup status.

The host report tool validates completion accounting, rejects incomplete logs,
and computes nearest-rank p95 without counting attempts or repeated summary
samples. It recomputes rates from exact counts and elapsed time:

```sh
python3 tools/profile_report.py build/task010-candidate-60s.log \
  --baseline build/task010-baseline-60s.log --target 4
```

`numeric_checks_pass` checks the numerical gates and restoration API results.
It cannot approve ghosting, motion, legibility or visual restoration. Those
require user observations. Compare both approved waveforms at a cadence before
choosing the lower normal wait p95; differences up to 5 ms favor AUTO.

## Local validation evidence

Existing input, layout, runtime, conversion/scaling, renderer, profiling,
touch-fault, real SameBoy runtime smoke and reference scaler tests passed.
Four additional renderer builds cover AUTO/GC16 with both 30/60-second cleanup
policies. They check output bytes and viewport-origin handling, no-flash normal
refreshes, static composition reuse, unchanged images, a due cleanup on an
identical image, geometry clearing, EINTR, and restoration after normal/cleanup
draw, submission, wait and empty-rectangle errors. Runtime tests exercise an
expired deadline, a multi-period overrun, signal/EXIT/error cleanup and dropping
missed display slots. The Python report test verifies separated samples,
failed-attempt exclusion and rejection of incomplete logs.

Docker host and ARM profiles build successfully. The first Docker attempt
encountered a native cached scaler executable linked against the host's newer
libc; forced in-container test compilation fixed that without changing any
dependency. The existing reference scaler emits its pre-existing pedantic
`dlsym` function-pointer warnings. Application/profile builds add no warnings.
ARM inspection confirms ELF32 ARM EABI5 hard-float, interpreter
`/lib/ld-linux-armhf.so.3`, and the existing libc/libm/libdl dependencies.
All preserved artifact hashes checked before device screening pass. AddressSanitizer
and UndefinedBehaviorSanitizer also passed the experimental GC16 renderer;
LeakSanitizer was disabled under the sandbox. Logs and manifests remain in ignored
`build/task010/`; ELF inspection alone does not establish PW5 compatibility.

## Physical PW5 screening results

After the initial SSH timeout, the user woke the PW5 and SSH recovered. A new
`/mnt/us/inkboy/task010-runtime` deployment was created. All 18 manifest entries
verified on-device, as did the preserved TASK-009 manifest. Firmware 5.19.2,
kernel 4.9.77-lab126, battery 83%, not charging; core and ROM hashes matched.
No firmware services, governor, root filesystem, FBInk or core were changed.
Metadata: `build/task010/device-metadata.log`; artifacts:
`build/task010/kindle/SHA256SUMS` and `build/task010-deploy/`.

Baseline executable SHA-256:
`84d7b223ab4454909d0485357577c4e3e1af265b5595c2f022bbc9c9c5fef5c7`.
The manifest identifies every experimental binary; the baseline uses the current
scheduler with the unchanged conservative full-screen flashing policy.

Sessions ran sequentially: baseline, AUTO 1, GC16 1, AUTO 2, GC16 2, AUTO 4,
GC16 4. Each timer ran 65 seconds after the initial completed gameplay update;
reported phase times also include initial presentation and termination delivery.
All experimental sessions used a 60-second cleanup and completed exactly one
periodic cleanup. Each also completed one initial full-screen update. No 8 FPS
profile was run.

| Profile | Phase seconds | Normal completions / FPS | Identical skips | Emulation FPS | CPU % | RSS KiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Baseline 1 FPS | 66.060 | 65 / 0.984 | 0 | 18.423 | 58.341 | 14632 |
| AUTO 1 FPS | 65.948 | 38 / 0.576 | 26 | 27.355 | 78.622 | 15256 |
| GC16 1 FPS | 65.948 | 55 / 0.834 | 9 | 22.912 | 67.993 | 15256 |
| AUTO 2 FPS | 65.965 | 99 / 1.501 | 30 | 13.977 | 44.357 | 15264 |
| GC16 2 FPS | 65.926 | 96 / 1.456 | 33 | 14.304 | 45.172 | 15228 |
| AUTO 4 FPS | 66.402 | 97 / 1.461 | 69 | 14.262 | 46.294 | 15212 |
| GC16 4 FPS | 65.908 | 99 / 1.502 | 61 | 13.306 | 43.470 | 15284 |

| Profile | Normal wait p95 ms | Cleanup wait ms | Maximum ordinary poll gap ms | Poll gap containing cleanup ms |
| --- | ---: | ---: | ---: | ---: |
| Baseline 1 FPS | 417.723 | — | 511.903 | — |
| AUTO 1 FPS | 370.148 | 367.991 | 426.985 | 423.217 |
| GC16 1 FPS | 368.087 | 364.966 | 426.184 | 422.574 |
| AUTO 2 FPS | 370.046 | 363.454 | 429.500 | 422.388 |
| GC16 2 FPS | 368.716 | 369.567 | 427.688 | 427.616 |
| AUTO 4 FPS | 369.786 | 366.291 | 444.058 | 422.355 |
| GC16 4 FPS | 369.160 | 366.194 | 435.004 | 424.603 |

All normal experimental requests logged `flashing=0` and the intended waveform;
cleanups used flashing AUTO in the `420x378 @ (408,346)` game rectangle. There
were zero refresh failures. All seven final statuses were SIGTERM/status=0,
with restore=0 and restore-wait=0. This establishes API success for SIGTERM;
it does not substitute for visual restoration confirmation or other exit paths.

The user reported that AUTO looked much better, then preferred GC16 visually.
An initially reported striped area was identified by the user as an open menu
and withdrawn as a rendering concern. These observations support reduced visible
flashing; they do not establish the complete grayscale/ghosting/long-session
acceptance matrix.

These were screenings, not matched movement trials. AUTO 1 recorded two
control-to-state samples while the other screenings recorded none. Different
emulation rates advance intro/title content at different wall-clock speeds,
and static images intentionally suppress updates. Thus the low normal FPS
values cannot alone establish the sustainable cadence in continuously moving
scenes, and CPU/emulation differences are not controlled causal estimates.

The ordinary poll gaps independently reject every candidate: the normal refresh
wait alone exceeds the 300 ms polling limit in this synchronous design. Both
2 FPS modes also reported below the 1.8 presentation FPS and 95% baseline
emulation screening thresholds. Neither mode is eligible for the p95 tie-break
or default adoption. Flash suppression improved the appearance but did not
remove the dominant synchronous blocking delay.

At the user’s request, further testing stopped. The last 4 FPS run had already
completed and restored through the API. The next batch had not started:
`gc16-2-clean30-sigint.log`, `gc16-2-sighup.log` and
`gc16-2-touch-failure.log` do not exist. No candidate EXIT session, 30-second
cleanup comparison, three-pair confirmation or five-minute session was run.
Their absence is recorded explicitly; the existing TASK-009 physical checks
remain historical evidence. Host tests cover those code paths but are not
physical validation. No additional device experiment is scheduled.

Raw logs: `build/task010/{baseline,auto-1,gc16-1,auto-2,gc16-2,auto-4,gc16-4}-screening.log`.
Consolidated data: `build/task010/screening-results.json`. Keep the default
1 FPS flashing full-screen policy, preserve milestone statuses, and leave the
fluidity objective open. A future approach must address normal refresh blocking
before repeating an acceptance campaign.

## Retained protocol for future work (not executed beyond screening)

Before deployment, ensure `/mnt/us/inkboy/task010-runtime` does not already
contain an experiment. Create a fresh isolated destination (and corresponding
build core path if a different name is necessary), copy only TASK-010 artifacts,
then verify every manifest hash on the device. Record firmware, kernel, battery,
charging, orientation, services, ROM/core hashes and executable hash before each
comparison. The expected unchanged core hash is
`d8ee424a1294db4919470cae7e76bc9be629d38063806ea6a3668f22e4b05fa9`;
ROM hash is `8cbaa499397e4f1a679c992ea9382a2dd7942ab398b48c19829c2d9529de47bf`.

The host runner writes logs with exclusive creation, starts its timer after the
first completed gameplay update and verifies `/proc/PID/exe` before signaling
that exact PID. It never changes the application threading model:

```sh
python3 tools/task010_run.py auto-1-60 build/task010/auto-1-screening.log --seconds 65
python3 tools/task010_run.py auto-2-60 build/task010/auto-2-long.log --seconds 300
python3 tools/task010_run.py auto-2-60 build/task010/auto-2-int.log --seconds 10 --signal INT
python3 tools/task010_run.py auto-2-60 build/task010/auto-2-touch.log --touch-failure
```

Use `gc16` instead of `auto` when that is the candidate being tested. The initial
65-second screenings include one 60-second cleanup. A premature stop, startup
failure or incomplete log must be investigated rather than treated as a timed
success. The preserved touchscreen shim is expected to produce final status 1.
For manual EXIT/controls, use the TASK-009 isolated launch/PID protocol directly
or let a timed runner provide a safety stop. For example:

```sh
set -o pipefail
ssh kindle 'cd /mnt/us/inkboy/task010-runtime && printf "control pid=%s\n" "$$" && exec ./inkboy-auto-1-60' \
  2>&1 | tee build/task010-auto-1-60-screening.log
```

Start the duration after the first completed gameplay presentation. Stop only
the recorded PID after verifying `/proc/PID/exe`; retain final status and visual
restoration. Preserve host stdout/stderr, avoiding reports written on the Kindle.
Never use broad `pkill` or `killall`.

| Stage | Required runs and observations |
| --- | --- |
| Screening | Baseline and AUTO/GC16 at 1 FPS with 60-second cleanup, then both modes at 2 and 4 FPS; observe motion, gray shades, text, static controls, scene transitions and trails |
| Exploratory 8 FPS | Only if 4 passes all numerical and visual criteria |
| Confirmation | Three alternating baseline/candidate pairs of 60 seconds (B/C, B/C, B/C), with matched motion/content and stable device conditions |
| Long session | Five minutes, 60-second cleanup first; repeat with 30 seconds if ghosting/legibility fails |
| Shutdown | EXIT, SIGINT, SIGTERM, SIGHUP and the preserved TASK-009 touchscreen-failure shim in separate candidate sessions; require expected status, full API restoration and user visual confirmation |

Keep baseline comparisons at the same content/interaction pattern; startup/title
animation alone does not validate active gameplay. Do not add Power handling or
alter firmware services. The preserved shim can be referenced by its TASK-009
path without overwriting it.

Adopt 4 FPS only after every criterion in [TASK-010](../tasks/TASK-010-reduce-flashing-and-improve-fluidity.md)
passes. Otherwise try 2; if neither passes, record the impediment and preserve
the existing default. Refresh decisions and milestone statuses remain unchanged
until physical acceptance.
