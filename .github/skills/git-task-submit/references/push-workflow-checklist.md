# Push Workflow Checklist

Use this checklist before running the final push.

## Pre-Stage Checks

1. Run `git status` and confirm which files belong to the current task.
2. Identify unrelated edits or untracked paths before staging anything.
3. Confirm the task scope is clear enough to choose `type` and `scope`.
4. If unrelated changes exist, stage only the task files. Do not use `git add .` for a partial-task submission.

## Staged Scope Checks

1. Review `git diff --staged --stat` after staging.
2. Review `git diff --staged` when the staged summary is not enough to confirm scope.
3. If the staged content exceeds the task boundary, unstage the extra files or hunks and regroup before committing.

## Commit Draft Checks

1. The title matches `<type>(<scope>): <subject>`.
2. The `type` is one of the allowed values.
3. The `scope` reflects the task, module, or delivery boundary.
4. The `subject` is specific and action-oriented.
5. A body is added when the change is larger than 3 files or includes design/test/gate context.
6. The title and body are drafted from `docs/development/Git提交信息规范.md`, with any skill-local summary treated only as a helper.

## Validation Checks

1. Record the commands already run.
2. Summarize key results instead of only saying `passed`.
3. If no validation was run, say so explicitly.

## Push Checks

1. Verify the intended remote is `origin`.
2. Verify the current branch and upstream/tracking state.
3. Verify whether the repository workflow allows direct push to that branch.
4. If the repository policy explicitly requires `master`, confirm that before pushing `origin master`.
5. Report the final commit title and push target back to the user.
6. After the push attempt, run a final status check and report any remaining local changes.
7. If push fails, return the exact failure reason and do not assume success.