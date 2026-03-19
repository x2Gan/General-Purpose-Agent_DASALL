# Blocker Recovery Rules

Use this reference when the selected task cannot continue safely.

## Blocker Triage

Classify the blocker into one of these groups:

1. Dependency blocker: predecessor task or required deliverable is incomplete.
2. Context blocker: the required architecture rule, TODO detail, or contract boundary is still unclear.
3. Environment blocker: build, test, tool, or configuration state prevents progress.
4. Scope blocker: the task can only continue by expanding outside the allowed work-package scope.
5. Validation blocker: the acceptance command cannot run or cannot pass due to an external issue.

## Recovery Procedure

1. State the blocker explicitly with file-based evidence.
2. Decide whether the blocker is repairable inside the same round.
3. If repairable, define one minimal blocker-fix action and execute only that fix.
4. Re-check whether the original task is now executable.
5. If the blocker is cleared, resume the original task.
6. If the blocker remains, stop the round with a blocked outcome and provide precise unblock conditions.

## Recovery Limits

1. Do not drift into a new work package just to keep moving.
2. Do not hide a blocker by marking the task done without acceptance evidence.
3. Do not merge unrelated blocker fixes into the same round if they belong to a different task stream.

## Blocked Output Requirements

If the round ends blocked, report:

1. Selected task ID.
2. Blocker type.
3. Evidence showing why it is blocked.
4. The minimum unblock action.
5. Whether any partial files changed and whether they should be kept or deferred.