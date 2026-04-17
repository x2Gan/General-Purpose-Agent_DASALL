---
name: runtime-incident
description: Diagnose runtime incidents with a bounded inspection workflow. Use when the task is to inspect health, collect logs, and summarize degradation without mutation.
when_to_use: Trigger on runtime triage, degradation analysis, or post-deploy incident review.
argument-hint: [incident-summary]
disable-model-invocation: true
allowed-tools:
  - Read
  - Grep
workflow: workflow.yaml
eval_suite: eval.yaml
---

# Runtime Incident

Use this skill as a diagnosis-only playbook.

## Supporting files

- [workflow.yaml](workflow.yaml) contains the workflow template consumed by importer tests.
- [eval.yaml](eval.yaml) captures the expected regression surface.
- [reference.md](reference.md) records domain notes and non-goals.

1. Inspect runtime status.
2. Collect relevant logs.
3. Summarize findings and stop before taking side effects.