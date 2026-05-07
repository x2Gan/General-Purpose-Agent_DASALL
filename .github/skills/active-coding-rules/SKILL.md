---
name: active-coding-rules
description: Karpathy-inspired behavioral guardrails for coding agents. Use when Codex is writing, reviewing, debugging, or refactoring code and should avoid wrong assumptions, hidden confusion, overcomplicated abstractions, unrelated edits, and vague success criteria.
---

# Active Coding Rules

## Overview

Use these rules to reduce common LLM coding mistakes: assuming too much, hiding uncertainty, overengineering, making drive-by edits, and working without a concrete verification target.

**Tradeoff:** These rules bias toward caution over speed. For trivial one-line tasks, use judgment.

## Operating Loop

1. Clarify the goal, constraints, and expected behavior.
2. Inspect the local code before deciding on an approach.
3. State meaningful assumptions when ambiguity affects the implementation.
4. Make the smallest change that satisfies the request.
5. Verify with the most relevant available command, test, build, lint, or manual check.
6. Report what changed, what was verified, and any remaining risk.

## Four Principles

### 1. Think Before Coding

Do not assume, hide confusion, or silently choose among plausible interpretations.

- State assumptions explicitly.
- Present tradeoffs when multiple approaches are reasonable.
- Push back when the requested direction is more complex than needed.
- Stop and ask when the ambiguity would change the implementation.

### 2. Simplicity First

Use the minimum code that solves the present problem.

- Do not add features beyond what was asked.
- Do not create abstractions for single-use code.
- Do not add flexibility, configurability, or error handling for hypothetical futures.
- If a shorter direct implementation would be clearer, choose it.

### 3. Surgical Changes

Touch only what the request requires and clean up only the mess created by the current change.

- Prefer existing project patterns, helpers, naming, and file organization.
- Do not improve adjacent code, comments, or formatting as a side effect.
- Do not refactor unrelated code.
- Remove imports, variables, functions, or files only when your change made them unused.
- Treat uncommitted changes as user-owned unless you made them.

### 4. Goal-Driven Execution

Turn the task into verifiable success criteria and loop until they are met.

- "Add validation" means identify invalid inputs, test them, then make the tests pass.
- "Fix a bug" means reproduce the bug or define the failing behavior before changing code.
- "Refactor" means preserve behavior and verify before and after when practical.
- For multi-step work, pair every step with its verification check.

## Review Mode

When reviewing code, lead with concrete findings ordered by severity. Include file and line references, explain the impact, and keep summaries secondary.

## References

- Read `references/core-rules.md` for the fuller adapted checklist before non-trivial or multi-file edits.
- Read `references/karpathy-examples.md` when examples would help calibrate assumptions, simplicity, surgical changes, or verification.

## Source Note

This skill ports the reusable guidance from the local `andrej-karpathy-skills` project into a Codex Skill structure while omitting Claude/Cursor-specific installation files.
