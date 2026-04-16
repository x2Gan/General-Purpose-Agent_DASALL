# TOOL-TODO-026 ToolObservabilityIntegration 验证收敛

日期：2026-04-16
任务：TOOL-TODO-026
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 已把 `ToolObservabilityIntegrationTest` 冻结为 `Gate-TOOL-08` 的主出口，要求 audit / metrics / trace 字段完整且可断言。
2. docs/todos/tools/DASALL_tools子系统专项TODO.md 在 026 中进一步要求 observability gate 覆盖 audit / metrics / trace / health 四类证据，并验证 failure path 仍可观测。
3. `ToolAuditBridge`、`ToolMetricsBridge`、`ToolTraceBridge`、`ToolHealthProbe` 已分别在 019~022 落盘，因此 026 的主工作应集中在统一 integration 出口补齐 health 聚合断言，而不是再开新的 observability integration 文件。
4. /memories/repo/tools-observability-bridges.md 已记录 `ToolObservabilityIntegrationTest` 是 tools observability 的共享 integration 出口；026 应沿用同一文件扩展 health，而不是复制测试拓扑。

## 2. Design 结论

1. 保持 `ToolObservabilityIntegrationTest` 作为单一 observability gate，在同一测试中同时覆盖 success / failure / denied / compensation 与 exporter failure 场景。
2. health 不直接接 live runtime source，而是在 integration 内通过 bridge status 组装 `ToolHealthSample`，再用 `ToolHealthProbe` 验证聚合结果；这样可以把 019~022 的 bridge 行为和 022 的 probe 语义收敛到同一个 Gate-TOOL-08 出口。
3. 正常路径必须断言 `ToolHealthProbe` 输出 `Healthy`，且 `failed_components` 为空、route health 三开关保持可用，证明 audit / metrics / trace 不会错误地把 observability gate 判成 degraded。
4. exporter failure 路径必须断言 `ToolHealthProbe` 输出 `Degraded`，且 `tools.audit_bridge`、`tools.metrics_bridge`、`tools.trace_bridge` 全部出现在 `failed_components` 中，同时 route health 仍保持可用，证明 exporter 故障可观测但不阻断主链。

## 3. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| health 聚合正例断言 | tests/integration/tools/ToolObservabilityIntegrationTest.cpp |
| exporter failure 下的 health degraded 断言 | tests/integration/tools/ToolObservabilityIntegrationTest.cpp |
| 026 证据回写 | docs/todos/tools/DASALL_tools子系统专项TODO.md、docs/worklog/DASALL_开发执行记录.md |

## 4. Build 三件套

1. 代码目标：
   - tests/integration/tools/ToolObservabilityIntegrationTest.cpp
2. 测试目标：
   - `ToolObservabilityIntegrationTest`
3. 验收基线：
   - Build_CMakeTools：`dasall_tools`
   - Build_CMakeTools：`dasall_tool_observability_integration_test`
   - Build_CMakeTools：`dasall_tools` + `dasall_integration_tests`（用于确认聚合基线仍只剩既有 infra failures）
   - RunCtest_CMakeTools：`ToolObservabilityIntegrationTest`

## 5. 本地验证

1. 定向构建：
   - Build_CMakeTools：`dasall_tools`
   - Build_CMakeTools：`dasall_tool_observability_integration_test`
2. 定向执行：
   - RunCtest_CMakeTools：`ToolObservabilityIntegrationTest`
3. 聚合基线检查：
   - Build_CMakeTools：`dasall_tools`、`dasall_integration_tests`
4. 结果摘要：
   - `ToolObservabilityIntegrationTest` 通过，现已同时覆盖 audit、metrics、trace、health 四类 observability 证据。
   - 正常路径下 `ToolHealthProbe` 输出 healthy snapshot；exporter failure 路径下输出 degraded snapshot，并准确暴露 audit / metrics / trace failed components。
   - exporter failure 不影响主执行结果，route health 仍保持可用，符合“failure path 仍可观测但不阻断主链”的完成判定。
   - `dasall_integration_tests` 聚合构建时剩余失败仍仅为既有 infra diagnostics `InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest`；未引入新的 tools integration 回归。
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，不影响本任务 gate 结论。

## 6. 风险与回退

1. 026 通过 bridge status 组装 `ToolHealthSample` 来收敛 observability gate，验证的是“当前 health 聚合语义”而不是 live runtime wiring；后续若要把 probe 接到真实 signal provider，应继续保持“provider 采样、probe 聚合、integration 收口”的边界。
2. observability integration 仍只覆盖 builtin 主链；workflow / MCP observability 需要在对应执行链真正落地后再扩展同一 gate，不能提前把未实现路径写成已验证事实。