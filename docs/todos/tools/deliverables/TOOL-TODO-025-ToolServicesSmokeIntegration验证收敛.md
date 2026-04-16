# TOOL-TODO-025 ToolServicesSmokeIntegration 验证收敛

日期：2026-04-16
任务：TOOL-TODO-025
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 已把 `ToolServicesSmokeIntegrationTest` 冻结为 `TOOL-D6` 与 `Gate-TOOL-05` 的主出口，验收目标是 `Tool -> Services -> ToolResult -> ObservationDigest` 最小闭环。
2. docs/todos/tools/DASALL_tools子系统专项TODO.md 对 025 的完成判定明确要求两类事实同时成立：builtin query / action 基本路径通过，以及 `ToolResult / ObservationDigest` 关键字段能做二值断言。
3. 当前生产 builtin catalog 只内建 `agent.terminal` action 描述符；若直接依赖默认目录，无法在不扩张生产能力面的前提下验证 builtin query 路径。因此 025 应在同一 smoke integration 中注入 test-local builtin query descriptor，而不是改写生产 catalog。
4. TOOL-TODO-024 已经把 `tests/integration/tools/ToolServicesSmokeIntegrationTest.cpp` 接入顶层 topology，所以 025 应继续复用该文件收口 Gate-TOOL-05，而不是新开平行 integration 入口。

## 2. Design 结论

1. 保持 `ToolServicesSmokeIntegrationTest` 作为唯一的 services smoke gate 出口，在同一文件中补强 action / query / negative 三类断言，避免测试拓扑扩散。
2. 通过 test-local `ToolRegistry + BuiltinExecutorLane + ToolManagerDependencies.executor` 注入一个 builtin query 描述符，仅用于集成门验证，不把 `agent.dataset` 等 test capability 写回生产 builtin catalog。
3. 正例同时断言 `ToolResult -> Observation -> ObservationDigest -> route_facts / evidence_refs` 的链路一致性：tool name、payload、observation_id、ToolExecution source、citations、confidence、request tags 必须保持可验证。
4. 负例补一个 descriptor-missing 预检失败分支，验证 smoke gate 不会在前置失败时伪造 `Observation` / `ObservationDigest`，并保留 fail-closed reason code。

## 3. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| action + query 闭环断言 | tests/integration/tools/ToolServicesSmokeIntegrationTest.cpp |
| descriptor-missing fail-closed 负例 | tests/integration/tools/ToolServicesSmokeIntegrationTest.cpp |
| 025 证据回写 | docs/todos/tools/DASALL_tools子系统专项TODO.md、docs/worklog/DASALL_开发执行记录.md |

## 4. Build 三件套

1. 代码目标：
   - tests/integration/tools/ToolServicesSmokeIntegrationTest.cpp
2. 测试目标：
   - `ToolServicesSmokeIntegrationTest`
3. 验收基线：
   - Build_CMakeTools：`dasall_tools`
   - Build_CMakeTools：`dasall_tool_services_smoke_integration_test`
   - Build_CMakeTools：`dasall_tools` + `dasall_integration_tests`（用于确认聚合基线仍只剩既有 infra failures）
   - RunCtest_CMakeTools：`ToolServicesSmokeIntegrationTest`

## 5. 本地验证

1. 定向构建：
   - Build_CMakeTools：`dasall_tools`
   - Build_CMakeTools：`dasall_tool_services_smoke_integration_test`
2. 定向执行：
   - RunCtest_CMakeTools：`ToolServicesSmokeIntegrationTest`
3. 聚合基线检查：
   - Build_CMakeTools：`dasall_tools`、`dasall_integration_tests`
4. 结果摘要：
   - `ToolServicesSmokeIntegrationTest` 通过，action 路径、query 路径和 descriptor-missing fail-closed 负例全部通过。
   - smoke test 现已显式断言 `ToolResult`、`Observation`、`ObservationDigest`、`route_facts`、`evidence_refs` 的关键字段一致性，不再只是“有返回就算通过”。
   - `dasall_integration_tests` 聚合构建时仍会触发既有 infra diagnostics 失败：`InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest`；tools smoke 用例本身通过，不属于本任务回归。
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，不影响本任务 gate 结论。

## 6. 风险与回退

1. query 路径当前通过 test-local descriptor 注入验证，只用于证明 builtin query 能沿现有 `ToolServiceBridge -> IDataService -> ResultProjector` 走通；这不等于生产 builtin catalog 已新增 query capability。
2. 025 只验证 services smoke gate，不扩张到 observability / health 字段完整性；后续 026 仍需在 `ToolObservabilityIntegrationTest` 中补齐 audit / metrics / trace / health 的统一门。