# TASK-010 — Reduce flashing and improve gameplay fluidity

- Milestone: M4 — Continuous rendering and performance research
- Status: Closed on 2026-10-18 at the user's direction. Current implementation was revalidated; the original fluidity acceptance criteria remain unmet, so M4 remains open.
- Dependency: TASK-009, including its uncommitted runtime fixes and evidence
- Coordination: Run one device session at a time; preserve TASK-008/009
  binaries, manifests and deployments. Runtime execution remains single-threaded
  and has no refresh queue.

## Objective

Seek four completed screen updates per second, preserving grayscale and reducing
flashing. Adopt two FPS if four is unstable. Eight FPS is exploratory and may
only be run after four passes. These are presentation rates; measure emulation
speed separately.

## Implementation

- Typed presentation policy: cadence, refresh mode and cleanup interval, selected
  at build time. Results distinguish initial presentation, ordinary partial
  presentation, cleanup, unchanged image and error.
- Experimental profiles draw the complete composition initially. Later updates
  convert, nearest-scale and draw only the game region. Static controls stay
  cached. Source dimensions or fitted game geometry changes require a full
  composition and refresh to erase the old region.
- Compare nonflashing regional AUTO and GC16 at 1, 2 and 4 FPS; build 8 FPS only
  as an exploratory option. Initial/geometry refreshes and final full restoration
  use flashing AUTO. Periodic game-region cleanups also use flashing AUTO,
  holding the cleanup waveform constant across normal waveform comparisons.
- Compare 60-second and 30-second cleanups, preferring 60 when legible. Identical
  scaled grayscale bytes skip drawing/refresh but cannot suppress a due cleanup.
  In synchronous profiles, the comparison cache advances after successful
  completion. In the current standard runtime, ordinary regional updates defer
  the completion wait; their cache advances after successful submission, and
  pending work is waited before a full redraw or cleanup.
- Re-read monotonic time after sleep and presentation. Advance expired display
  deadlines to the next future slot and discard emulation debt after refresh;
  never issue catch-up bursts.
- Keep the TASK-009 input enum, signal handling and ordered cleanup paths.
  SameBoy, audio negotiation, FBInk and Power behavior remain as validated.

The standard build now targets 2 FPS with regional GC16 updates and a
60-second flashing AUTO cleanup, following the user's request to reduce
repeated full-screen flashing and prioritize smoother presentation. Initial
presentation, geometry changes and final restoration still refresh the full
screen. Physical screening reached about 1.31 completed updates/s at the 2 FPS
target; the actual cadence limit remains under investigation. The higher-cadence
fluidity objective remains open; this policy change does not mark the milestone
complete.

## Measurement and acceptance

Report initial, normal and cleanup completions separately, with normal/cleanup
refresh timings and failures, effective rates, polling gaps, emulation, CPU and
RSS. Attempts, skipped identical images and superseded producer frames do not
count as completed screen updates. Report cleanup-related polling stalls
separately from ordinary polling gaps.

A candidate must meet **all** of these conditions in moving scenes:

- Effective normal presentation rate at least 90% of target.
- Emulation rate at least 95% of a contemporaneous 1 FPS baseline.
- Polling intervals outside cleanup/initial full refresh below 300 ms.
- No flash requested by ordinary updates, refresh failures, loss of legibility,
  or shutdown/restoration regression.
- User confirmation of perceptibly improved fluidity and at most mild trails.

Prefer four FPS; use two if four fails. At equal approved cadence choose the
lower p95 normal completion wait, favoring AUTO for differences up to 5 ms.
Use 60-second cleanups if the five-minute session passes; otherwise test 30.
If no two-FPS candidate passes, keep the existing default and document the
blocking evidence without claiming the fluidity objective is complete.

## Validation

Host tests cover waveform/flash selection, identical frames, pending cleanup,
grayscale/nearest equivalence, geometry changes, expired deadlines, no catch-up
bursts and restoration after errors. Run existing tests and Docker host/ARM
builds, inspect ELF, verify preserved hashes and run `git diff --check`.

Physical PW5 sequence:

1. Compare AUTO and GC16 without flash at 1 FPS, then 2 and 4; only explore 8
   after 4 passes every criterion.
2. Confirm the strongest candidate with three alternating baseline/candidate
   pairs of 60 seconds, then a five-minute session including motion, text and
   scene transitions. Keep ROM, core, 180-frame warmup and device conditions
   constant. Use 60-second cleanup first, 30 if necessary.
3. Confirm grayscale, trails, static controls, EXIT, signals, injected touchscreen
   failure and full restoration visually and in the logs. Preserve Power behavior.

See the [experiment protocol and evidence](../experiments/10-presentation-fluidity.md)
for artifacts, measurement definitions and the pending hardware matrix.

## Physical screening outcome — 2026-10-16

Seven sessions completed: a contemporaneous baseline and AUTO/GC16 at 1, 2 and
4 FPS, with 60-second cleanup profiles. All finished through SIGTERM with no
refresh failures and successful restoration API results. The user reported a
clear visual improvement and preferred GC16 to AUTO. An initially reported
striped area was subsequently identified by the user as an open menu.

No candidate is approved: ordinary polling gaps reached 426–444 ms, exceeding
the 300 ms limit independently of duplicate-image skipping. Normal refresh p95
remained about 368–370 ms. The two-FPS screenings reported 1.46–1.50 completed
normal updates/s and 13.98–14.30 emulation FPS against the 18.42 FPS baseline.
Scene/interaction differences and identical-image skips mean these are screening
results, not the prescribed controlled motion confirmation.

At the user's direction on 2026-10-18, the production tree was simplified to
one fixed presentation policy and the unused profile matrix and standalone
benchmark sources were removed. Host tests, host and Kindle builds, and the
Libretro runtime smoke test passed. A current-build device smoke run is recorded
below. The original controlled-motion comparisons, candidate matrix, EXIT and
fault injection campaign were not completed; no candidate is approved and M4
remains open. The task is closed as a scoped decision, not as a claim that its
fluidity acceptance criteria passed. Historical measurements and their limits
remain in the experiment report.

## Final current-build device smoke — 2026-10-18

Run `task010-final-20261018a` lasted 79.27 seconds on the PW5. The user
confirmed that the game appeared and the screen was legible. The log records 65
normal submissions, one 60-second AUTO cleanup, zero refresh failures, and
successful full restoration after SIGTERM. Emulation averaged 55.65 FPS,
normal submissions 0.82 FPS, the initial completion wait 371.5 ms and the
cleanup wait 366.9 ms. This smoke run did not use controlled motion and does
not meet or replace the original candidate acceptance campaign.
