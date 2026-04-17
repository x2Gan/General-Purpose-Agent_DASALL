# Skills Subsystem

Skills define bounded, composable tool invocation patterns that agents can discover and execute through the tools pipeline.

## Directory Layout

```
skills/
├── specs/              ← Internal skill definitions (.skill.yaml)
├── workflows/          ← Workflow templates (.workflow.yaml)
├── evals/              ← Evaluation suites (.eval.yaml)
└── external_dialects/  ← Third-party format samples
    ├── claude/         ← Claude SKILL.md dialect
    └── github/         ← GitHub .github/skills dialect
```

## Workflow YAML Format (S-2)

Each `.workflow.yaml` defines a multi-step tool execution plan:

| Field | Type | Required | Description |
|---|---|---|---|
| `workflow_id` | string | yes | Unique workflow identifier, format `skill.<name>` |
| `entry_step_ids` | string[] | yes | Steps with no dependencies (DAG roots) |
| `steps` | string[] | yes | Ordered list of all step IDs in the workflow |
| `step.<id>.tool_name` | string | yes | Qualified tool name to invoke |
| `step.<id>.route_kind` | string | yes | Routing hint: `LocalTool`, `MCPTool`, or `ExternalTool` |
| `step.<id>.timeout_ms` | int | yes | Per-step timeout in milliseconds |
| `step.<id>.depends_on` | string[] | no | Step IDs that must complete before this step runs |
| `binding.<src_step>.<output_key>.target_step_id` | string | no | Step ID receiving the output |
| `binding.<src_step>.<output_key>.target_argument_key` | string | no | Argument key in the target step |
| `delegation_mode` | string | yes | `Disabled` or `Enabled`; controls sub-agent delegation |
| `max_delegate_steps` | int | yes | Maximum delegation depth (0 = no delegation) |

### Execution Semantics

1. The workflow engine builds a DAG from `steps` and `depends_on` edges.
2. Cyclic dependencies are rejected at validation time.
3. Steps whose dependencies are all satisfied may execute concurrently (subject to lane capacity).
4. Bindings pipe outputs from completed steps into downstream step arguments.
5. If any step fails, the workflow halts and returns a failure envelope with the failed step's result.

### Example

```yaml
workflow_id: skill.device-health-check
entry_step_ids:
  - poll_sensors
steps:
  - poll_sensors
  - collect_diagnostics
  - generate_health_report
step.poll_sensors.tool_name: device.poll_sensors
step.poll_sensors.route_kind: LocalTool
step.poll_sensors.timeout_ms: 2000
step.collect_diagnostics.tool_name: device.collect_diagnostics
step.collect_diagnostics.route_kind: LocalTool
step.collect_diagnostics.timeout_ms: 3000
step.collect_diagnostics.depends_on:
  - poll_sensors
step.generate_health_report.tool_name: knowledge.generate_health_report
step.generate_health_report.route_kind: LocalTool
step.generate_health_report.depends_on:
  - poll_sensors
  - collect_diagnostics
binding.poll_sensors.readings.target_step_id: generate_health_report
binding.poll_sensors.readings.target_argument_key: sensor_readings
binding.collect_diagnostics.report.target_step_id: generate_health_report
binding.collect_diagnostics.report.target_argument_key: diagnostics_data
delegation_mode: Disabled
max_delegate_steps: 0
```

## External Dialect Field Mapping (S-3)

The skill importer normalizes third-party skill formats into the internal `SkillSpec` representation. The field mapping tables below document how each dialect's fields map to internal fields.

### Claude Dialect (`external_dialects/claude/`)

Entry point: `SKILL.md` with YAML front-matter.

| Claude Field | Internal Field | Notes |
|---|---|---|
| `name` | `skill_id` (prefixed `external.claude.`) | Importer adds namespace prefix |
| `description` | `description` | Direct copy |
| `when_to_use` | `intent_patterns[0]` | Mapped as first intent pattern |
| `argument-hint` | _metadata only_ | Not mapped to runtime fields |
| `disable-model-invocation` | _ignored_ | Internal routing handles this |
| `allowed-tools` | `allowed_tools` | Normalized to internal tool names |
| `workflow` | `workflow_template_ref` | Resolved relative to SKILL.md directory |
| `eval_suite` | `eval_suite_ref` | Resolved relative to SKILL.md directory |

### GitHub Dialect (`external_dialects/github/`)

Entry point: `SKILL.md` with YAML front-matter.

| GitHub Field | Internal Field | Notes |
|---|---|---|
| `name` | `skill_id` (prefixed `external.github.`) | Importer adds namespace prefix |
| `description` | `description` | Direct copy |
| `argument-hint` | _metadata only_ | Not mapped to runtime fields |
| `disable-model-invocation` | _ignored_ | Internal routing handles this |
| `tool-allowlist` | `allowed_tools` | Normalized to internal tool names |
| `workflow-template` | `workflow_template_ref` | Resolved relative to SKILL.md directory |
| `eval-suite` | `eval_suite_ref` | Resolved relative to SKILL.md directory |

### Key Differences

| Aspect | Claude | GitHub |
|---|---|---|
| Tool field name | `allowed-tools` | `tool-allowlist` |
| Workflow ref field | `workflow` | `workflow-template` |
| Has `when_to_use` | Yes | No |
| Fallback default | `reject` | `builtin-summary` |

### Adding a New Dialect

1. Create a directory under `external_dialects/<vendor>/<skill-name>/`.
2. Place `SKILL.md` with YAML front-matter as the entry point.
3. Include `workflow.yaml`, `eval.yaml`, and `reference.md` as companion assets.
4. Add a field mapping row to the table above.
5. Add importer test assertions in the corresponding eval.yaml.
