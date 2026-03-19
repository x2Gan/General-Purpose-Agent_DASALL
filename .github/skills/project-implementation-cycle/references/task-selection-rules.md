# Task Selection Rules

Use these rules to choose one executable atomic task from a TODO document.

## Selection Goal

Pick the smallest task that can be completed safely in the current round without violating ordering, scope, or acceptance constraints.

## Selection Order

1. Read the TODO table or task list from top to bottom.
2. Filter to tasks that are not done.
3. Exclude tasks with unfinished prerequisites or unmet gate conditions.
4. Prefer rows that are already phrased as one atomic delivery.
5. If both `-D` and `-B` exist for the same task family, do not choose `-B` before `-D` is done.
6. Prefer the earliest executable row unless the TODO explicitly says another row should be prioritized.

## Executable Task Criteria

A task is executable when all of the following are true:

1. Its scope is clear enough to define deliverables.
2. Its predecessor or gate dependencies are already satisfied.
3. The required inputs can be gathered from repository files or accessible tools.
4. The task can produce a binary completion result or an explicit blocked result.
5. The expected change can fit into one submission round.

## Atomicity Checks

Reject a task for this round if one row actually hides multiple independent goals, for example:

1. It changes multiple unrelated modules at once.
2. It mixes broad design refactoring with unrelated build fixes.
3. It requires a full work-package sweep rather than one bounded increment.

If the TODO row is too broad, still select it only if the repository process expects the row to be implemented as one bounded task. Otherwise stop and report that the TODO needs refinement before execution.

## Selection Output

When selecting the task, record:

1. TODO path.
2. Selected task ID.
3. Current status.
4. Why it is executable now.
5. The completion evidence expected for this round.