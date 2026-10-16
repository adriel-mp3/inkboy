# InkBoy roadmap

This roadmap tracks milestone outcomes separately from the status of individual
tasks. Independent tasks may remain active at the same time, including tasks
from different milestones.

## Milestones

| ID | Objective | Status | Evidence or next work |
| --- | --- | --- | --- |
| M0 | Kindle build, ABI, and deployment foundation | Complete | [`docs/compatibility.md`](compatibility.md) |
| M1 | Static PW5 renderer with normal restoration | Complete | [`docs/experiments/02-rendering.md`](experiments/02-rendering.md) |
| M2 | Touch input evidence | Complete as a standalone probe | [`docs/experiments/03-touch-input.md`](experiments/03-touch-input.md) |
| M3 | SameBoy and Libretro feasibility probes | Complete as isolated probes | [`docs/experiments/05-libretro-probe.md`](experiments/05-libretro-probe.md), [`docs/experiments/05.3-libretro-frame.md`](experiments/05.3-libretro-frame.md) |
| M4 | Continuous rendering and performance research | In progress | [`docs/experiments/05.4-libretro-continuous-rendering.md`](experiments/05.4-libretro-continuous-rendering.md); TASK-001; [`TASK-010`](tasks/TASK-010-reduce-flashing-and-improve-fluidity.md) |
| M5 | Playable Prototype | Complete | TASK-007/008/009 validated on PW5: game/control response, sustained execution and restoration. Performance and Power-integration limitations remain documented in [`TASK-009`](experiments/09-gameplay-runtime.md). |

Milestone status describes the state of the objective, not whether every
related task is closed. A milestone may remain in progress while independent
work advances elsewhere.

## Workflow

Use: Research → Plan → Implement → Test → Validate → Document.

Experiments record hypotheses and evidence. Tasks describe bounded deliverables
that can be implemented, tested, and reviewed independently.

## Task conventions

- Use unique sequential IDs: `TASK-001`, `TASK-002`, and so on.
- Every task states its milestone, status, objective, scope, acceptance
  criteria, validation, and dependencies when applicable.
- Independent tasks may run in separate Git worktrees.
- Coordinate changes to shared files and all physical Kindle deployments.
- Multiple tasks may be active concurrently; task status does not determine
  milestone status.

## Active tasks

- [`TASK-001 — Repeat instrumented PW5 performance baseline`](tasks/TASK-001-performance-investigation.md) — Complete.
- [`TASK-002 — Investigate synchronous refresh blocking`](tasks/TASK-002-synchronous-refresh-blocking.md) — Complete.
- [`TASK-003 — SameBoy Emulation Performance Investigation`](tasks/TASK-003-sameboy-emulation-performance.md) — Complete.
- [`TASK-004 — SameBoy profiling tools and instrumentation cost`](tasks/TASK-004-sameboy-profiling-tools.md) — In progress; audio sample-rate experiment complete.
- [`TASK-005 — Validate 48 kHz audio negotiation with live E-Ink rendering`](tasks/TASK-005-validate-48khz-live-rendering.md) — Complete; 48 kHz not adopted as default.
- [`TASK-006 — Investigate E-Ink refresh latency regression`](tasks/TASK-006-investigate-48khz-refresh-latency.md) — Complete; 48 kHz remains experimental.
- [`TASK-007 — Modularize Application`](tasks/TASK-007-modularize-application.md) — Complete; modular runtime physically validated through TASK-009.
- [`TASK-008 — Play a Game Boy Game`](tasks/TASK-008-play-a-game-boy-game.md) — Complete; game/control response and shutdown physically validated through TASK-009.
- [`TASK-009 — Profile and Stabilize PW5 Gameplay Runtime`](tasks/TASK-009-profile-and-stabilize-gameplay-runtime.md) — Complete; measured full/region comparisons and safe EXIT/signal/error cleanup; Power-to-exit explicitly unsupported.
- [`TASK-010 — Reduce flashing and improve gameplay fluidity`](tasks/TASK-010-reduce-flashing-and-improve-fluidity.md) — In progress; regional AUTO/GC16 implemented and screened on PW5; no 2/4 FPS profile meets the polling limit. Default preserved; further tests stopped at the user’s request.
