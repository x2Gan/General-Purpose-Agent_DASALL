# TOOL-TODO-019 ToolAuditBridge 设计收敛

日期：2026-04-16  
任务：TOOL-TODO-019  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 第 6.10、6.12.6 已冻结 tools observability 边界：`ToolAuditBridge` 负责统一 `tool.execution.*`、`tool.compensation.*` 事件口径，并通过 infra `IAuditLogger` 写入审计面。
2. `ToolManager` 在 `invoke()` / `compensate()` 上已经具备 `on_requested`、`on_completed`、`on_failed`、`on_compensation` 四个 hook，因此 019 的根任务不是改执行链，而是把默认 hook 从空实现收敛成标准桥接实现。
3. `tests/integration/tools` 已由 TOOL-TODO-024 接入顶层 integration 图，所以 019 可以合法补入 `ToolObservabilityIntegrationTest`，不再受 discoverability blocker 影响。

## 2. Design 结论

1. 新增 `tools/src/ops/ToolAuditBridge.h/.cpp` 作为 tools 内部审计桥，提供 `emit_requested()`、`emit_completed()`、`emit_failed()`、`emit_compensation()` 四个接口，并输出 `ToolAuditEmitResult` / `ToolAuditBridgeStatus` 供失败可观测性判断。
2. `ToolAuditBridge` 采用 fail-open 语义：audit sink 缺失、payload 无效或 write failure 都会被结构化记录到 bridge status，但不会阻断 `ToolManager` 主返回面。
3. 为解决 `on_completed` / `on_failed` hook 不再携带原始 `ToolInvocationContext` 的问题，bridge 会按 `tool_call_id` 缓存请求关联事实（session / trace / goal / worker / caller），在 terminal audit 阶段回填 `AuditContext`。
4. 当前 `ToolManager` 只有一个 `on_compensation(request, envelope)` 补偿出口，没有独立的 suggestion hook，因此本轮把补偿审计事件收敛为 `tool.compensation.executed`；`tool.compensation.suggested` 留待后续补偿账本任务统一引入。
5. 审计 side effects 只保留结构化字段：tool identity、route facts、failure reason、error stage/source、digest confidence、compensation facts、evidence refs、declared side effects；严禁嵌入原始 `ToolRequest.arguments_payload` 或 `ToolResult.payload`。

## 3. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| tools 内部审计桥本体 | tools/src/ops/ToolAuditBridge.h；tools/src/ops/ToolAuditBridge.cpp |
| 默认 ToolManager hook 接线 | tools/src/ToolManager.cpp |
| tools target / include 接线 | tools/CMakeLists.txt |
| unit 审计桥验证 | tests/unit/tools/ToolAuditBridgeTest.cpp；tests/unit/tools/CMakeLists.txt；tests/unit/CMakeLists.txt |
| integration 审计桥验证 | tests/integration/tools/ToolObservabilityIntegrationTest.cpp；tests/integration/tools/CMakeLists.txt |
| traceability 回写 | docs/todos/tools/DASALL_tools子系统专项TODO.md；docs/worklog/DASALL_开发执行记录.md |

## 4. Build 三件套

1. 代码目标：
   - tools/src/ops/ToolAuditBridge.h
   - tools/src/ops/ToolAuditBridge.cpp
   - tools/src/ToolManager.cpp
   - tests/unit/tools/ToolAuditBridgeTest.cpp
   - tests/integration/tools/ToolObservabilityIntegrationTest.cpp
2. 测试目标：
   - `ToolAuditBridgeTest`
   - `ToolManagerPipelineTest`
   - `ToolObservabilityIntegrationTest`
   - `ToolServicesSmokeIntegrationTest`
3. 验收基线：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_tools dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L integration`

## 5. 本地验证

1. 构建：
   - Build_CMakeTools: `dasall_tool_audit_bridge_unit_test`
   - Build_CMakeTools: `dasall_tool_observability_integration_test`
   - Build_CMakeTools: `dasall_tool_manager_pipeline_unit_test`
   - Build_CMakeTools: `dasall_tool_services_smoke_integration_test`
   - Build_CMakeTools: `dasall_tools`
   - Build_CMakeTools: `dasall_unit_tests`
   - Build_CMakeTools: `dasall_integration_tests`
2. 定向执行：
   - RunCtest_CMakeTools: `ToolAuditBridgeTest`
   - RunCtest_CMakeTools: `ToolManagerPipelineTest`
   - RunCtest_CMakeTools: `ToolObservabilityIntegrationTest`
   - RunCtest_CMakeTools: `ToolServicesSmokeIntegrationTest`
3. discoverability：
   - `ctest --test-dir build/vscode-linux-ninja -N | rg "ToolObservabilityIntegrationTest|ToolServicesSmokeIntegrationTest"`
4. 结果摘要：
   - 新增定向用例全部通过。
   - `dasall_unit_tests` 聚合回归通过，结果为 `279/279 passed`。
   - `dasall_integration_tests` 聚合执行时，本轮新增 tools integration 用例通过，但存在两个既有 infra diagnostics 失败：`InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest`；它们位于 tools 改动范围之外，不影响 019 的直接验收结论。
   - `ctest -N` 已显式发现 `ToolObservabilityIntegrationTest` 与 `ToolServicesSmokeIntegrationTest`。
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，不影响通过结论。

## 6. 风险与回退

1. 当前 bridge 通过 `tool_call_id` 缓存请求相关事实来回填 terminal audit context；这满足 v1 需要，但还没有配套容量/TTL/health 约束，后续可由 ToolHealthProbe 或 observability health 任务继续收口。
2. 由于 `ToolManager` 现阶段没有 `tool.compensation.suggested` hook，本轮只能落 `tool.compensation.executed`；若未来补偿建议阶段进入主链，必须在 bridge 上补充新的事件族，而不是复用现有 executed 语义。
3. 本轮只实现 audit bridge，不覆盖 metrics / trace / health；完整 Observability 门仍需 020~022、026 继续补齐。