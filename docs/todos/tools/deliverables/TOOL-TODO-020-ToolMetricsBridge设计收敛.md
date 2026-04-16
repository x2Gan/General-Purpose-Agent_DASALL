# TOOL-TODO-020 ToolMetricsBridge 设计收敛

日期：2026-04-16
任务：TOOL-TODO-020
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 第 6.10、6.12.6 已冻结 tools observability 边界：`ToolMetricsBridge` 负责把 tool request、admission denied、execution latency、stale snapshot、workflow step failure 等信号映射到 infra metrics 面。
2. TOOL-TODO-019 已把 `ToolObservabilityIntegrationTest` 接入 `tests/integration/tools`，因此 020 应复用同一个 integration 出口扩展 metrics 断言，而不是再开平行 observability 用例。
3. infra metrics 合同当前固定为 `module/stage/profile/outcome/error_code` 五维标签和 `IMetricsProvider -> IMeter -> MetricSample` 记录路径，所以 020 必须把 tools 维度编码进 stage/outcome/error_code，而不能引入 provider 私有标签。

## 2. Design 结论

1. 新增 `tools/src/ops/ToolMetricsBridge.h/.cpp` 作为 tools 内部 metrics bridge，提供 `record_preflight_failure()`、`record_admission_denied()`、`record_route_selection()`、`record_execution_terminal()`、`record_workflow_step_failure()` 五类高层记录接口。
2. `ToolMetricsBridge` 冻结六个指标族：`tool_request_total`、`tool_admission_denied_total`、`tool_execution_latency_ms`、`tool_partial_side_effect_total`、`tool_mcp_stale_snapshot_total`、`tool_workflow_step_failure_total`；其中仅 latency 使用 histogram，其余保持 counter。
3. bridge 采用 fail-open 语义：provider 缺失、meter/instrument 创建失败、record exporter 失败都只进入 `ToolMetricsEmitResult` / degraded 状态，不阻断 `ToolManager` 主返回面。
4. 与 019 的 audit hook 模式不同，metrics 直接由 `ToolManager` 在请求失败、policy deny、route stale、terminal execution 等关键节点同步调用，从而保留 profile、route、descriptor 与 envelope 上下文，不再额外维护 request fact cache。
5. stage 字段统一编码 route/tool/category/workflow 等 tools 事实，避免向 infra metrics 合同泄漏 provider 私有 labels；profile 优先取 `RuntimePolicySnapshot::effective_profile_id()`，缺失时退回 bridge 默认 profile。

## 3. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| tools 内部 metrics bridge 本体 | tools/src/ops/ToolMetricsBridge.h；tools/src/ops/ToolMetricsBridge.cpp |
| ToolManager metrics 接线 | tools/src/ToolManager.h；tools/src/ToolManager.cpp |
| tools target 接线 | tools/CMakeLists.txt |
| unit metrics bridge 验证 | tests/unit/tools/ToolMetricsBridgeTest.cpp；tests/unit/tools/CMakeLists.txt；tests/unit/CMakeLists.txt |
| integration metrics bridge 验证 | tests/integration/tools/ToolObservabilityIntegrationTest.cpp |
| traceability 回写 | docs/todos/tools/DASALL_tools子系统专项TODO.md；docs/worklog/DASALL_开发执行记录.md |

## 4. Build 三件套

1. 代码目标：
   - tools/src/ops/ToolMetricsBridge.h
   - tools/src/ops/ToolMetricsBridge.cpp
   - tools/src/ToolManager.h
   - tools/src/ToolManager.cpp
   - tests/unit/tools/ToolMetricsBridgeTest.cpp
   - tests/integration/tools/ToolObservabilityIntegrationTest.cpp
2. 测试目标：
   - `ToolMetricsBridgeTest`
   - `ToolManagerPipelineTest`
   - `ToolObservabilityIntegrationTest`
   - `ToolServicesSmokeIntegrationTest`
3. 验收基线：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_tools dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L integration`

## 5. 本地验证

1. 构建：
   - Build_CMakeTools: `dasall_tool_metrics_bridge_unit_test`
   - Build_CMakeTools: `dasall_tool_observability_integration_test`
   - Build_CMakeTools: `dasall_tool_manager_pipeline_unit_test`
   - Build_CMakeTools: `dasall_tools`
   - Build_CMakeTools: `dasall_unit_tests`
   - Build_CMakeTools: `dasall_integration_tests`
2. 定向执行：
   - RunCtest_CMakeTools: `ToolMetricsBridgeTest`
   - RunCtest_CMakeTools: `ToolManagerPipelineTest`
   - RunCtest_CMakeTools: `ToolObservabilityIntegrationTest`
   - RunCtest_CMakeTools: `ToolServicesSmokeIntegrationTest`
3. 结果摘要：
   - 新增定向用例全部通过。
   - `dasall_unit_tests` 聚合回归通过，结果为 `280/280 passed`。
   - `dasall_integration_tests` 聚合执行时，本轮新增 tools integration 用例通过，剩余失败仍是两个既有 infra diagnostics：`InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest`；它们位于 tools 改动范围之外，不影响 020 的直接验收结论。
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，不影响通过结论。

## 6. 风险与回退

1. 020 当前只补 metrics bridge，不覆盖 trace / health；完整 observability 门仍需 021、022、026 继续收口。
2. metrics 维度当前通过 stage token 编码 route/tool/workflow 等 tools 事实，这满足现有 infra label 合同；若未来 metrics 合同扩展更多 frozen labels，应新增显式字段而不是回退为 provider 私有 label。
3. `ToolMetricsBridge` 对 exporter failure 采用 degraded fail-open 语义，能保护主链结果，但也意味着 aggregate integration 只能证明“metrics 故障不阻断”，不能替代后续 trace/health 对 observability 全面健康性的验收。