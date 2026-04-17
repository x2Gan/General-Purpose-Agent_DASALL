---
name: device-health-check
description: Perform a non-destructive device health assessment by polling sensors, collecting diagnostics, and generating a status report. Use when the task involves inspecting device state without mutation.
when_to_use: Trigger on device health checks, sensor inspection, or post-update hardware diagnostics.
argument-hint: [device-id]
disable-model-invocation: true
allowed-tools:
  - Read
  - Grep
workflow: workflow.yaml
eval_suite: eval.yaml
---

# Device Health Check

Use this skill as a read-only device inspection playbook.

## Supporting files

- [workflow.yaml](workflow.yaml) contains the workflow template consumed by importer tests.
- [eval.yaml](eval.yaml) captures the expected regression surface.
- [reference.md](reference.md) records domain notes and non-goals.

1. Poll sensor readings from the target device.
2. Collect diagnostics output.
3. Generate a health status report and stop before taking corrective actions.
