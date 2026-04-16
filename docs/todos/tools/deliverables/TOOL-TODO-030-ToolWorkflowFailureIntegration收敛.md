# TOOL-TODO-030 ToolWorkflowFailureIntegration 收敛

日期：2026-04-16  
任务：TOOL-TODO-030  
状态：已完成

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-030 要求新增 `ToolWorkflowFailureIntegrationTest`，并把 workflow step failure、delegation sidecar、compensation_hints、failure digest 作为 integration 验收出口。
2. 028 已落地 WorkflowEngine 的 DAG/failure stop/delegation sidecar，029 已落地 invoke-scoped CompensationLedger 与 workflow-scoped hints 汇总，因此 030 的职责是做 integration gate，而不是再改 shared contract。
3. TOOL-TODO-030 的 blocker `TOOL-BLK-003` 已在 024 完成（ToolManager 与 ToolServiceBridge 接口一致性），本轮可以直接进入 integration test 收口。

## 2. 实现结论

1. 新增 tests/integration/tools/ToolWorkflowFailureIntegrationTest.cpp，使用 ToolManager + 注入式 WorkflowEngineDependencies 构造 workflow failure 场景。
2. 该测试覆盖以下事实链：
   - workflow step 失败后，top-level `ToolResult.success=false` 且 `ToolResult.error` 可断言。
   - delegation step 会以 recommendation sidecar 形式进入 workflow payload。
   - workflow-scoped `compensation_hints` 会在 ToolInvocationEnvelope 返回，并且只保留 reversible upstream side effects。
   - `failure_reason_code` 与 failure digest message 对齐，确保 runtime 恢复策略读到同一主失败语义。
3. 更新 tests/integration/tools/CMakeLists.txt，注册 `dasall_tool_workflow_failure_integration_test` 目标和 `ToolWorkflowFailureIntegrationTest` 用例。

## 3. Design -> Build 映射闭合

| 设计项 | 代码落点 | 测试出口 |
|---|---|---|
| workflow step failure 顶层投影 | ToolManager::run_invoke_pipeline + WorkflowEngine::finalize_receipt | ToolWorkflowFailureIntegrationTest.cpp |
| delegation sidecar 保留 | WorkflowEngine delegation step 路径 + receipt 序列化 | ToolWorkflowFailureIntegrationTest.cpp |
| workflow-scoped compensation_hints | CompensationLedger + WorkflowEngine receipt 汇总 | ToolWorkflowFailureIntegrationTest.cpp |
| failure digest / reason code 一致性 | WorkflowEngine outcome -> ToolManager envelope 映射 | ToolWorkflowFailureIntegrationTest.cpp |

## 4. 验证

1. 构建：
   - Build_CMakeTools：`dasall_tool_workflow_failure_integration_test`
2. 定向执行：
   - RunCtest_CMakeTools：`ToolWorkflowFailureIntegrationTest`
3. 结果摘要：
   - `ToolWorkflowFailureIntegrationTest` 通过。
   - 断言 workflow 失败后 `ToolResult.error`、`failure_reason_code`、delegation sidecar、`compensation_hints` 同时可见，满足 Gate-TOOL-05 对 027~030 的 integration 收口要求。
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，不影响本任务通过结论。

## 5. 风险与回退

1. 当前 integration test 通过注入式 plan_loader 构造 workflow 场景，尚未覆盖“生产 parser 解析真实 workflow payload”的路径；该缺口不属于 030 的原子任务边界。
2. 用例聚焦单工作流失败链路；若后续引入并行批次和多失败聚合策略，应补充多批次失败优先级与 evidence 聚合顺序的 integration 覆盖。
3. 本轮未改动 shared contracts；若后续尝试把 sidecar/hints 迁入 shared ToolResult，将直接破坏当前收敛边界和已有 surface tests。