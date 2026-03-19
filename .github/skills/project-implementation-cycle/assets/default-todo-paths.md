# Default TODO Paths

Use this asset when the user asks to continue project implementation but does not specify a TODO document.

## Selection Policy

Choose the first path that matches the user's requested scope or the current repository focus.

## Priority List

1. `docs/todos/contracts-freeze/WP-05-子域细化与ContractTestsTODO.md`
   Use when the user asks to continue the current contracts-freeze wave and no narrower path is specified.

2. `docs/todos/contracts-freeze/WP-04-边界对象TODO.md`
   Use when the current work still depends on the boundary-object wave or references ADR-008 boundary tasks.

3. `docs/todos/contracts-freeze/WP-03-主链路对象TODO.md`
   Use when the request is about main-loop objects, prompt composition, reflection, or recovery chain prerequisites.

## Usage Rules

1. Prefer an explicit user-provided TODO path over this asset.
2. Do not silently switch between work packages once a round has started.
3. If no listed path matches the request, stop and ask for the TODO document instead of guessing.