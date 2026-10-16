# TASK-010 — Reduce flashing and improve gameplay fluidity

- Milestone: M4 — Continuous rendering and performance research
- Status: In progress — implementation validated; PW5 screening found no qualifying 2/4 FPS policy; further tests stopped at the user’s request
- Dependency: TASK-009, including its uncommitted runtime fixes and evidence
- Coordination: Single-threaded execution, synchronous completion, no refresh queue.
  Preserve TASK-008/009 binaries, manifests and deployments.

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
  The comparison cache advances only after successful synchronous completion.
- Re-read monotonic time after sleep and presentation. Advance expired display
  deadlines to the next future slot and discard emulation debt after refresh;
  never issue catch-up bursts.
- Keep the TASK-009 input enum, signal handling and ordered cleanup paths.
  SameBoy, audio negotiation, FBInk and Power behavior remain as validated.

The default remains 1 FPS, full-screen, flashing AUTO until physical approval.
No milestone or refresh decision is marked complete by implementation alone.

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

The user requested an end to testing. The active 4 FPS session had already
terminated successfully; the queued 30-second cleanup/shutdown tests never
started. No 8 FPS, three-pair confirmation or five-minute session was run.
Thirty-second cleanup is host-tested but physically unverified. Candidate-specific
EXIT, other signals and touchscreen-failure validation remain unperformed;
TASK-009’s existing evidence is preserved and is not relabeled as TASK-010 proof.
The default and milestone statuses remain unchanged. The fluidity objective is
not complete. Detailed data and limitations are in the experiment report.
