---
name: project-implementation-cycle
description: 'Drive one full project implementation cycle from a TODO document: open a work-package TODO or choose one from default repository TODO paths, select one executable atomic task, assemble a new task prompt from docs/development/WP双轨任务执行提示词模板.md, execute the task, switch to blocker recovery if blocked, then commit and push the task changes to the remote using the repository git convention.'
argument-hint: 'Provide the TODO document path and, if known, the preferred task ID or work-package scope.'
---

# Project Implementation Cycle

Use this skill when the user wants Copilot to advance the project by one controlled execution round starting from a TODO document.

This skill is designed for the recurring repository workflow:

1. Open a task TODO document such as `docs/todos/contracts-freeze/WP-05-子域细化与ContractTestsTODO.md`, or resolve one from the asset file `assets/default-todo-paths.md`.
2. Choose one executable atomic task.
3. Fill and launch a task prompt from `docs/development/WP双轨任务执行提示词模板.md`.
4. Execute the task until it either completes or becomes blocked.
5. If blocked, switch to blocker recovery.
6. When the blocker is repaired, resume the task cycle.
7. Submit the task changes with a repository-compliant commit and push.
8. Finish the current round and report the result.

This skill is not for generic brainstorming. It is for driving a concrete implementation round inside this repository.

## Required Inputs

Before starting, gather these inputs:

1. The TODO document path, or a default path resolved from the asset file `assets/default-todo-paths.md`.
2. The current task table or task list inside that TODO document.
3. The project execution template in `docs/development/WP双轨任务执行提示词模板.md`.
4. The repository commit rule in `docs/development/Git提交信息规范.md`.
5. The current git working tree state so unrelated changes are not mixed into the submission.
6. Whether `.github/skills/git-task-submit/SKILL.md` exists, because submission should default to that skill when available.

## Primary Procedure

1. Determine the target TODO document. If the user did not specify one, resolve it from the asset file `assets/default-todo-paths.md` using the highest-priority applicable entry.
2. Read the target TODO document.
3. Identify candidate atomic tasks using [task selection rules](./references/task-selection-rules.md).
4. Pick exactly one executable atomic task for this round.
5. Build a launch prompt by filling [WP task launch template](./assets/wp-task-launch-template.txt) from the selected task and the TODO source.
6. Execute the selected task in the current chat using the repository's dual-track discipline from `docs/development/WP双轨任务执行提示词模板.md`, including the Build compliance review defined there.
7. If the task completes without blocker, move to submission.
8. If the task is blocked, switch to [blocker recovery rules](./references/blocker-recovery-rules.md), and start the blocker branch with the asset file `assets/blocker-recovery-launch-template.txt`, then either:
   - resume the current task if the blocker is cleared in the same round, or
   - stop the round with a clear blocked status if the blocker cannot be cleared safely.
9. If the implementation work for this round is complete, submit the task changes using [submission handoff](./references/submission-handoff.md).
10. End the round with a structured report: selected task, completion or blocked state, changed files, validation evidence, commit title, push target, and next recommended task.

## Operating Rules

1. Always choose one task only. Do not batch multiple TODO rows into one round.
2. Prefer the smallest executable atomic task.
3. Respect dependency order from the TODO document. Do not pick a task whose predecessor is incomplete.
4. If no TODO path is provided, use the default TODO asset rather than guessing an arbitrary document.
5. Use the dual-track template as the execution backbone when the task requires design plus build.
6. If the selected task is documentation-only, still require completion criteria and validation evidence.
7. If a blocker appears, do not silently skip it. Switch to blocker recovery explicitly and use the blocker launch template in `assets/blocker-recovery-launch-template.txt` so the recovery branch stays structured.
8. Do not push unrelated changes. The round submission must contain only files that belong to the selected task or its direct blocker fix.
9. Submission must follow the repository git convention, not an ad hoc commit message.
10. Treat `docs/development/WP双轨任务执行提示词模板.md` as an executable checklist, not background reading; the round is incomplete if the Build compliance review is missing.
11. When `.github/skills/git-task-submit/SKILL.md` exists, use it as the default submission handler instead of improvising commit/push steps.
12. If commit or push fails, report a `Submission Blocked` outcome. Do not report the round as fully complete.

## Blocker Policy

A blocker exists when one of these is true:

1. A required dependency task is not done.
2. Required project context or constraints are still unknown.
3. The environment, build, test, or tool access prevents safe continuation.
4. The task requires out-of-scope changes to continue.
5. The task cannot satisfy its acceptance command in the current state.

When blocked, do not pretend the task is complete. Follow [blocker recovery rules](./references/blocker-recovery-rules.md).

## Submission Policy

When the round completes and changes are ready:

1. Read [submission handoff](./references/submission-handoff.md).
2. If `.github/skills/git-task-submit/SKILL.md` is available, read and use that skill as the primary submission path.
3. Stage only the files from the current round.
4. Draft a repository-compliant commit title and body.
5. Commit and push to the repository-approved remote target.
6. If submission fails at any point, downgrade the round outcome to `Submission Blocked` and report the exact failing step.
7. Report any remaining local changes after the push attempt.

## Output Expectations

The final response for one cycle should include:

1. The TODO document used.
2. The selected task ID and why it was executable.
3. The launch prompt that was assembled or a concise summary of its filled fields.
4. Whether the task completed or was blocked.
5. If blocked, the blocker type, evidence, and unblock condition.
6. The files changed in this round.
7. Validation commands and results.
8. The commit title and push target, if submission occurred.
9. If submission failed, the exact blocker and recovery action.
10. The next recommended task for the following round.

## References

1. [task selection rules](./references/task-selection-rules.md)
2. [blocker recovery rules](./references/blocker-recovery-rules.md)
3. [submission handoff](./references/submission-handoff.md)
4. [WP task launch template](./assets/wp-task-launch-template.txt)
5. Default TODO paths: `assets/default-todo-paths.md`
6. Blocker recovery launch template: `assets/blocker-recovery-launch-template.txt`