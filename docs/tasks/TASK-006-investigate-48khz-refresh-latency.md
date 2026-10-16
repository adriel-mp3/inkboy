# TASK-006 — Investigate E-Ink refresh latency regression

- Milestone: M4 — Continuous rendering and performance research
- Status: Complete — refresh-wait regression explained; 48 kHz remains experimental
- Dependencies: TASK-005 completed live-rendering A/B logs and timing instrumentation
- Parallel execution: Independent analysis task after TASK-005; suitable for a separate Git worktree because it does not modify application or SameBoy sources
- Coordination: Shares TASK-005 raw logs, `tools/libretro_live.c`, M4 experiment documentation, and physical PW5 access; coordinate any future device experiment and preserve the validated fallback deployment

## Objective

Explain why negotiating 48000 Hz increased measured FBInk refresh wait from
approximately 127 ms to 208 ms and reduced display updates from 9 to 8, then
produce an evidence-backed recommendation on whether 48000 Hz is safe to adopt.

## Scope

- Review all TASK-005 raw logs, aggregate results, and the existing timing
  instrumentation and display-scheduling definitions.
- Analyze individual refresh durations, refresh ordering, frame-skipping,
  display-cap decisions, synchronous wait behavior, and measurement boundaries.
- Separate likely scheduling effects from E-Ink refresh variability and
  instrumentation or aggregation artifacts.
- Determine whether the existing evidence supports adopting 48000 Hz or
  requires retaining the omitted-rate fallback.
- If the logs cannot resolve the cause, propose one minimal controlled PW5
  experiment with fixed binaries, ROM, display policy, and measurements.
- Recommend at most one minimal correction, and only when the evidence supports
  it.

## Exclusions

- No application-code, `tools/libretro_live.c`, or SameBoy changes.
- No threads, asynchronous refresh, rendering features, or architectural
  redesign.
- Preserve `WFM_AUTO`, smooth scaling, the 1 FPS display cap, and synchronous
  refresh completion.
- No new benchmark execution during task creation; do not run unrelated
  performance experiments.
- No adoption of 48000 Hz or other production configuration change as part of
  this investigation.

## Acceptance criteria

- The TASK-005 logs and timing boundaries are reviewed, with per-refresh and
  display-scheduling evidence summarized where available.
- The likely cause is identified, or the remaining uncertainty is explicitly
  documented with the missing evidence stated.
- A clear recommendation states whether 48000 Hz can be safely adopted under
  the preserved rendering policy.
- If uncertainty remains, exactly one minimal controlled PW5 experiment is
  specified with its hypothesis, fixed conditions, measurements, and decision
  rule.
- No more than one evidence-supported minimal correction is recommended, and
  no code or configuration is changed.

## Validation

Start with static review of `build/task-005-*-live.log`, TASK-005’s reported
statistics, `tools/libretro_live.c`, and the related M4 experiment. Recompute
refresh-duration and scheduling summaries from the retained logs where the raw
fields permit, checking counts, elapsed intervals, display-cap behavior,
skipped frames, and synchronous wait attribution. Compare A and B using the
same run boundaries and distinguish observed measurements from inference.

Do not execute benchmarks for this task unless a separately approved PW5
follow-up is required by unresolved uncertainty. If required, use only the
single controlled experiment defined in the report and retain its complete
log, binary hashes, restoration result, and decision against the stated rule.

## Related experiments or decisions

- [`TASK-005 — Validate 48 kHz audio negotiation with live E-Ink rendering`](TASK-005-validate-48khz-live-rendering.md)
- [`TASK-004 — SameBoy profiling tools and instrumentation cost`](TASK-004-sameboy-profiling-tools.md)
- [`05.4 — Controlled continuous Libretro rendering research`](../experiments/05.4-libretro-continuous-rendering.md)
- The decision to preserve single-threaded rendering, smooth scaling,
  `WFM_AUTO`, the 1 FPS display cap, and synchronous refresh completion

## Completion — refresh-wait regression analysis

Date: 2026-10-16. The TASK-005 raw logs, reported statistics, timing
definitions, and `tools/libretro_live.c` scheduling path were reviewed. No
benchmarks or application changes were made during this analysis.

### Reconstructed refresh-wait distribution

The logs report a cumulative `avg_refresh_wait_ms`, so individual waits were
reconstructed from successive cumulative averages and update counts. The
values show that the difference is a change in the mix of short and long
completion waits, not that every B refresh takes approximately 208 ms.

| Configuration | Refresh-wait distribution per run | Total wait | Updates |
| --- | --- | ---: | ---: |
| A — omitted target rate | One full wait at approximately 365 ms; two partial waits near 300 ms; six waits near 30 ms | Approximately 1.14 s | 9 |
| B — 48000 Hz | One full wait at approximately 365 ms; four partial waits near 300 ms; three waits near 30 ms | Approximately 1.66 s | 8 |

Across the three runs, A measured approximately 126–127 ms average wait and B
approximately 208 ms. Draw time stayed near 105–107 ms and refresh submission
near 0.6–1.7 ms, so neither explains the increase. All refreshes succeeded,
with zero failures and successful normal restoration.

### Interpretation

The most likely relationship is:

1. Negotiating 48000 Hz reduces SameBoy emulation cost, so the single-threaded
   loop reaches more video callbacks and presents different, later frame
   contents at the same one-second display deadlines.
2. Later updates remain partial and use `WFM_AUTO`. On the PW5 MTK path, AUTO
   delegates waveform selection to the display driver using the refresh-region
   characteristics. The changed frame content can therefore produce more
   approximately 300 ms completion waits.
3. The synchronous loop schedules `next_display` one second after each refresh
   completes. Those additional long waits extend the refresh-relative display
   intervals, leaving room for eight B updates rather than nine A updates.

The higher B skipped-frame count is consistent with the faster producer and
newest-frame selection; it is not evidence of video callback loss. The
instrumentation timer directly surrounds `fbink_wait_for_complete()`, so the
wait increase is a real measured completion-time difference rather than a
measurement-only artifact. However, the logs do not record the driver's
selected waveform or a dedicated per-refresh timer, so the exact panel-side
mechanism cannot be proven from this data alone. The aggregate average is also
composition-sensitive because A and B completed different numbers of updates.

Because the exact driver-side waveform choice remains unobserved, the one
minimal follow-up, if later approved, is a single additional matched A/B pair
using the existing TASK-005 artifacts and protocol: the same PW5, ROM, 180
warmup frames, 10-second window, 1 FPS cap, smooth scaling, `WFM_AUTO`, and
synchronous completion, with complete logs and normal restoration between
runs. Reconstruct the per-refresh distribution from the existing metrics and
classify the result as repeatable if B again shows materially more ~300 ms
waits and eight updates while A shows two such waits and nine updates. This
experiment was proposed only; it was not executed for TASK-006.

### Decision

The evidence supports retaining the omitted-rate fallback as the default
configuration. 48000 Hz remains a valid experimental option because it
improves emulation throughput, but it is not adopted as the default while it
causes a repeatable live-path refresh-wait and display-update regression under
the preserved `WFM_AUTO`, smooth-scaling, 1 FPS, synchronous policy.

No application, instrumentation, waveform, scheduling, or architectural
correction is supported by the evidence. No additional benchmark was executed
for this task.
