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
| M4 | Continuous rendering and performance research | In progress | [`docs/experiments/05.4-libretro-continuous-rendering.md`](experiments/05.4-libretro-continuous-rendering.md); TASK-001 |
| M5 | Product integration | Not started | Independent frontend tasks may begin while M4 continues |

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
