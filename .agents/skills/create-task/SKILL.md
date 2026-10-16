---
name: create-task
description: Create a concise InkBoy task document and update roadmap references without implementing the task.
---

# Create an InkBoy task

Use this skill when the user asks to define or create a new bounded InkBoy
task. It plans and documents work; it never implements, tests, deploys, or
executes the task.

## Read before writing

Read these repository files before creating anything:

- `AGENTS.md`
- `docs/roadmap.md`
- every existing file under `docs/tasks/`

Treat the roadmap as the source for milestone names and statuses. Treat
existing tasks as active planning context, including tasks from other
milestones. Do not modify `AGENTS.md`, application code, experiments, or
existing task files.

## Check for duplicate work

Before assigning an ID, compare the proposed objective, scope, deliverables,
and validation against every existing task. Check for exact duplicates and
substantial overlap, even when the titles differ.

If a duplicate or substantially overlapping task exists, do not create or
overwrite a task. Report the matching task and explain the overlap; ask whether
the user wants to revise the existing task or define a genuinely separate
scope.

## Assign an ID and milestone

Find all existing `TASK-NNN` IDs in task filenames and contents. Use the next
integer after the highest existing ID, formatted with at least three digits.
Never reuse a gap, and verify that the target filename does not already exist.
Never overwrite an existing task.

Select the milestone whose objective best matches the proposed work. If no
milestone is appropriate or the choice would materially change scope, ask for
direction before writing. Creating a task must not change milestone status;
milestone status reflects actual development progress, not task creation.

## Define concurrency and coordination

Identify whether the task can run independently of currently listed tasks and
whether it could use a separate Git worktree. Record this explicitly in the
task as `Parallel execution`.

Compare the proposed scope with existing task scopes and flag overlapping
shared files, shared generated artifacts, or shared interfaces under
`Coordination`. Physical Kindle access, deployment, or hardware validation is
also a coordination requirement. These are coordination requirements, not
automatic blockers; describe the affected task or resource and any needed
ordering without changing the task status.

## Create the task

Create exactly one concise file at:

`docs/tasks/TASK-NNN-short-slug.md`

Use this structure:

```markdown
# TASK-NNN — Title

- Milestone: M# — Name
- Status: Planned
- Dependencies: None or explicit dependencies
- Parallel execution: Independent, or describe the compatible parallel work
- Coordination: None identified, or list shared-file/device coordination

## Objective

One measurable outcome.

## Scope

Included work.

## Exclusions

Explicitly excluded work.

## Acceptance criteria

- Observable completion condition.

## Validation

Commands, measurements, or device procedure required after implementation.

## Related experiments or decisions

- Relevant repository links.
```

Keep the objective and acceptance criteria independently reviewable. Include
dependencies only when they are real; coordination concerns should remain in
`Coordination`.

## Update only roadmap references

After creating the task, add one link for it under the roadmap's active-task
references, preserving the roadmap's existing structure. If the roadmap has no
appropriate task-reference section, add the smallest such section needed.
Do not change milestone statuses automatically and do not update unrelated
roadmap content.

The only permitted installation/use mutations are the new task document and
the corresponding roadmap reference. Do not create scripts, auxiliary files,
commits, or implementation changes. If duplicate work or an unclear milestone
prevents safe creation, make no changes and report the reason.
