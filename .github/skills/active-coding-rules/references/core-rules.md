# Core Rules

This checklist adapts the Karpathy-inspired guidelines from `andrej-karpathy-skills` for Codex coding work.

## 1. Think Before Coding

Do not assume. Do not hide confusion. Surface tradeoffs.

- State assumptions explicitly before implementation when they affect behavior.
- If multiple interpretations exist, present them instead of silently choosing one.
- If a simpler path exists, say so and prefer it unless the user needs the heavier option.
- If something is unclear enough to change the result, stop and ask.

## 2. Simplicity First

Minimum code that solves the problem. Nothing speculative.

- Add no features beyond what was asked.
- Add no abstraction for a single use.
- Add no flexibility or configurability that was not requested.
- Add no error handling for impossible scenarios.
- If the implementation is much longer than the problem warrants, simplify it.

Use the senior-engineer test: would this look overcomplicated in review?

## 3. Surgical Changes

Touch only what is necessary. Clean up only the effects of this change.

- Keep the write set narrow and directly tied to the request.
- Match existing style even if another style is personally preferable.
- Do not rewrite adjacent code, comments, imports, or formatting as a side effect.
- Mention unrelated dead code rather than deleting it.
- Delete only the imports, variables, functions, or files that your change made unused.

Every changed line should trace back to the user's request.

## 4. Goal-Driven Execution

Define success criteria. Loop until verified.

- Convert "add validation" into invalid-input tests plus passing implementation.
- Convert "fix the bug" into a reproduction or failing test plus passing implementation.
- Convert "refactor" into behavior preservation plus before/after verification when practical.
- For multi-step work, state each step with its check.

Example plan shape:

```text
1. Reproduce the reported behavior. Verify: failing test or observed failure.
2. Implement the narrow fix. Verify: reproduction now passes.
3. Check nearby behavior. Verify: focused suite, lint, build, or manual check.
```

## Before Editing

- Identify the smallest observable behavior that satisfies the request.
- Inspect the files that own that behavior before planning the change.
- Check for uncommitted work and preserve changes you did not make.
- Ask only when a reasonable assumption could create the wrong behavior or touch the wrong area.

## Verification

- Run the tightest relevant check first.
- Broaden verification when the change crosses module boundaries or touches shared utilities.
- If a check cannot be run, record the reason and the residual risk.

## Signals It Is Working

- Diffs contain fewer unnecessary changes.
- Clarifying questions happen before implementation, not after mistakes.
- Code is simple enough to avoid an immediate rewrite.
- Reviews and PRs stay small, focused, and verifiable.

## Final Report

- Name the files changed and the behavior affected.
- Mention the verification command and result.
- Call out unresolved assumptions, skipped checks, or follow-up work only when they matter.
