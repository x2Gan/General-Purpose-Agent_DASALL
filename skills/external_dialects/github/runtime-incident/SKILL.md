---
name: runtime-incident
description: 'Investigate runtime degradation with a fixed workflow and bounded tool references.'
argument-hint: '[incident-summary]'
disable-model-invocation: true
tool-allowlist:
  - runtime.inspect_status
  - runtime.collect_logs
  - knowledge.summarize_findings
workflow-template: workflow.yaml
eval-suite: eval.yaml
---

# Runtime Incident

This GitHub-style sample models a checked-in skill bundle for importer tests.

## Assets

- [workflow.yaml](workflow.yaml) defines the imported workflow reference.
- [eval.yaml](eval.yaml) freezes the importer regression expectations.
- [reference.md](reference.md) explains the intended use and limits.

1. Inspect runtime state.
2. Review recent logs.
3. Produce a concise diagnosis and a safe next step.