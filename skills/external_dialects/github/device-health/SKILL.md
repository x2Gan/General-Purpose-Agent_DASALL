---
name: device-health-check
description: 'Perform non-destructive device health assessment via sensor polling and diagnostics collection.'
argument-hint: '[device-id]'
disable-model-invocation: true
tool-allowlist:
  - device.poll_sensors
  - device.collect_diagnostics
  - knowledge.generate_health_report
workflow-template: workflow.yaml
eval-suite: eval.yaml
---

# Device Health Check

This GitHub-style sample models a checked-in skill bundle for importer tests.

## Assets

- [workflow.yaml](workflow.yaml) defines the imported workflow reference.
- [eval.yaml](eval.yaml) freezes the importer regression expectations.
- [reference.md](reference.md) explains the intended use and limits.

1. Poll sensor readings.
2. Collect device diagnostics.
3. Generate a health status report without corrective actions.
