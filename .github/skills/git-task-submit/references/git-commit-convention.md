# Git Commit Convention Summary

This reference summarizes the repository rule from `docs/development/Git提交信息规范.md` for use inside the skill.

Use `docs/development/Git提交信息规范.md` as the source of truth whenever there is any mismatch. This file is only a convenience summary.

## Title Format

Use this exact title structure:

```text
<type>(<scope>): <subject>
```

Rules:

1. `type` is required.
2. `scope` is strongly recommended.
3. `subject` should say what changed, not how it was done.
4. Keep the title concise, ideally within 72 characters.
5. Avoid vague titles.

## Allowed Types

1. `feat`: add new behavior or new files with external value.
2. `fix`: correct a defect or wrong behavior.
3. `refactor`: change structure without changing behavior.
4. `test`: add or update tests.
5. `build`: change build system, CMake, scripts, dependencies, or CI gates.
6. `docs`: update documentation.
7. `chore`: maintenance work that is not `docs`, `test`, or `build`.
8. `revert`: revert an earlier commit.

## Scope Guidance

Preferred scope order:

1. Task scope such as `wp01`, `wp04`, or a work-package identifier.
2. Module scope such as `contracts`, `runtime`, `tools`, `memory`.
3. Delivery scope such as `tests`, `docs`, `ci`, `cmake`.

Examples:

```text
build(wp01-contracts): converge contract test registration and gate deps
docs(wp01): sync build todo evidence and execution worklog
test(contracts): add layered violation matrix for request/result/worker
```

## When a Body Is Required

Add a body when one of these is true:

1. More than 3 files changed.
2. The change includes design, testing, gates, or traceability updates.
3. The reviewer needs context to understand why the change is safe.

## Body Structure

Use these sections when needed:

```text
Context:
- task source
- architectural constraint

Changes:
- change 1
- change 2

Validation:
- command + result

Traceability:
- TODO path
- Worklog path
```

## Submission Commands

The repository's standard minimal flow, when the repository policy and current branch both allow it, is:

```bash
git add .
git commit -m "<type>(<scope>): <subject>"
git push -u origin master
```

When the working tree contains unrelated changes, stage only the task files and verify them with `git diff --staged` before committing.

Before pushing, verify the current branch, upstream/tracking state, and repository branch policy instead of assuming `master`.

For larger changes, prefer interactive commit message entry so the full body can be added.