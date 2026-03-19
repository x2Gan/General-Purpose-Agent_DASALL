---
name: git-task-submit
description: 'Stage current task changes, draft a project-compliant git commit message, commit, and push to the repository-approved remote branch. Use when the user asks to add changes, write a commit message from docs/development/Git提交信息规范.md, and push local modifications to the remote.'
argument-hint: 'Describe the task scope, changed files, and validation evidence to include in the commit message.'
---

# Git Task Submit

Use this skill when the user wants to submit the current task changes with the repository commit convention and the standard flow:

1. `git status` is used first to separate current-task files from unrelated edits.
2. `git add` stages only the files for the current task.
3. `git diff --staged` confirms the staged scope matches the task.
4. `git commit` uses the repository format from [Git提交信息规范](../../../docs/development/Git提交信息规范.md), with [git commit convention summary](./references/git-commit-convention.md) only as a helper.
5. `git push` targets the current branch or repository-required branch after verifying branch, upstream, and direct-push policy.

This skill is for project-aware submission work, not generic git help. It should be used when the request includes one or more of these intents:

1. Push local changes to the remote.
2. Generate a commit message from the project convention.
3. Stage the current task changes and submit them.
4. Summarize the modified files into a compliant commit title and body.

## Required Inputs

Before submitting, gather these inputs from the workspace state or the user request:

1. The set of files that belong to the current task.
2. The task scope, such as `docs`, `contracts`, `tests`, or `wp04`.
3. The validation commands already run, if any.
4. The traceability references, if the change should point to TODO or worklog entries.
5. The current branch, upstream/tracking state, and any repository rule that constrains the push target.

## Procedure

1. Run a preflight check with `git status`.
2. Classify the working tree into two sets: files that belong to the current task, and unrelated edits or untracked paths.
3. If unrelated changes exist, explicitly keep them out of scope. Do not use `git add .` when the task is a subset of the working tree.
4. Read [Git提交信息规范](../../../docs/development/Git提交信息规范.md) as the source of truth. Use [git commit convention summary](./references/git-commit-convention.md) only to speed up drafting, not to override the repository rule.
5. Determine the commit `type`, `scope`, and `subject` using the repository rule.
6. If the change affects more than 3 files, or includes design, tests, gates, or traceability updates, prepare a full commit body using [commit message template](./assets/commit-message-template.txt).
7. Stage only the task files with `git add <paths...>`.
8. Verify the staged scope with `git diff --staged --stat` and, when needed, `git diff --staged`.
9. If the staged content exceeds the task scope, stop, unstage the extra files or hunks, and return to grouping instead of committing a mixed change.
10. Create the commit with a title that matches `<type>(<scope>): <subject>`.
11. Before pushing, check the current branch, upstream/tracking state, and whether the repository workflow allows direct push.
12. Push to the repository-approved target. If the repository rule explicitly requires `master`, push `origin master`; otherwise push the validated current branch or upstream target.
13. Run a final status check so the user can see any remaining local changes after the submission.
14. Report the final staged scope, commit title, push target, and remaining local changes back to the user.

## Decision Rules

1. Prefer the smallest scope that still explains the task.
2. Use `docs` when the change is documentation-only.
3. Use `build` for CMake, CI, dependency, or gate-related changes.
4. Use `test` for test-only changes.
5. Use `feat`, `fix`, `refactor`, `docs`, `test`, `build`, `chore`, or `revert` only.
6. Do not use vague subjects such as `update files` or `fix bug`.
7. If the current task mixes unrelated changes, stop and split the submission scope instead of staging everything blindly.
8. If the staged diff and the intended task scope do not match, stop and fix the staged set before creating a commit.
9. Prefer the repository's formal document when the skill summary and the repo rule appear inconsistent.
10. Do not assume `master` is always the push target; verify the current branch and repository policy first.

## Output Expectations

The final response should clearly include:

1. The staged file scope.
2. The chosen commit title.
3. The commit body summary if one was used.
4. The push command and target branch.
5. The remaining uncommitted changes after the commit and push attempt.
6. Any blockers that prevented submission.

## References

1. [Git提交信息规范](../../../docs/development/Git提交信息规范.md)
2. [git commit convention summary](./references/git-commit-convention.md)
3. [push workflow checklist](./references/push-workflow-checklist.md)
4. [commit message template](./assets/commit-message-template.txt)