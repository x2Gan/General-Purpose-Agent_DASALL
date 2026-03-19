# Submission Handoff

Use this reference when the implementation round is complete and the changes should be submitted.

## Source of Truth

The repository commit rule is defined in `docs/development/Git提交信息规范.md`.

## Submission Steps

1. Run a git status preflight check.
2. If `.github/skills/git-task-submit/SKILL.md` exists, read and use that skill as the default submission mechanism.
3. Stage only the files that belong to the selected task round.
4. Draft the commit title using `<type>(<scope>): <subject>`.
5. Add a commit body when the change is larger than 3 files or includes design, tests, gates, or traceability content.
6. Verify the staged diff matches the selected task scope.
7. Commit.
8. Push to the repository-approved remote target.
9. If any submission step fails, report `Submission Blocked` with the exact failure point and leave the round in non-complete state.
10. Report the final commit title, push target, and any remaining local changes.

## Scope Discipline

1. If the working tree includes unrelated edits, do not use `git add .`.
2. Stage the round files explicitly.
3. Keep blocker-fix files only if they were required to finish the selected task in the same round.

## Commit Body Fields

Use these sections when needed:

1. `Context`
2. `Changes`
3. `Validation`
4. `Traceability`

## Suggested Interaction

When the round is done, the agent should prefer the following order:

1. use the project `git-task-submit` skill if that skill is available in the workspace.
2. fall back to the repository git rule directly only when that skill is unavailable.

In both cases, the repository rule remains authoritative, and push failure must be surfaced as `Submission Blocked`.