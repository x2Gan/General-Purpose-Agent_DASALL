# TOOL-TODO-027 WorkflowPlan 与 WorkflowReceipt internal schema 收敛

日期：2026-04-16  
任务：TOOL-TODO-027  
状态：D Gate PASS

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-027 明确要求把 WorkflowPlan、WorkflowReceipt、delegation sidecar、step_output_mapping、cyclic rejection 约束补成 internal schema，并且不得扩张 shared contracts。
2. docs/architecture/DASALL_tools子系统详细设计.md 已经冻结 WorkflowEngine v1 的边界：只支持 DAG-only、失败早停、delegation recommendation sidecar、静态 step_output_mapping，不允许条件分支、循环或 tools 内部第二主循环。
3. 当前仓库中尚无 tools/src/execution/WorkflowEngine.cpp 与 CompensationLedger.cpp 实现，说明 027 的最小可执行动作应先把 internal schema 和 Design->Build 映射落盘，再给 028/029 提供稳定实现出口。

## 2. Design 结论

1. WorkflowPlan 保持 module-local internal 对象，不进入 contracts；其最小 schema 由 workflow_id、entry_step_ids、steps、edges、step_output_mapping、delegation_policy、metadata 组成。
2. WorkflowStep 固定区分 tool 与 delegation 两类 step。tool step 只走 builtin 或 MCP 执行，delegation step 只产出 recommendation sidecar，不直接触发 multi_agent。
3. DAG-only 约束必须在 build_batches() 前显式校验：依赖边不得形成环，step_output_mapping 只能使用静态 JSON Pointer，把上游成功 step 的 payload 字段映射到下游 normalized_arguments。
4. WorkflowReceipt 继续保持 module-local internal，对 workflow 级状态、step 级结果、workflow-scoped compensation_hints、failure_digest 和 delegation_sidecar 做统一汇总；它负责汇总，不覆盖 step 原始 ToolResult.error。
5. CompensationLedger 与 WorkflowReceipt 的耦合点只体现在 workflow-scoped compensation_hints 汇总；CompensationRecord 继续停留在 tools/src/execution，不升格到 ToolInvocationEnvelope 之外的 shared/public ABI。

## 3. Design -> Build 映射

| Design 项 | Build / 测试落点 |
|---|---|
| WorkflowPlan DAG-only schema | tools/src/execution/WorkflowEngine.cpp 的 plan 校验与 build_batches() |
| step_output_mapping 静态字段映射 | WorkflowEngine 的 dispatch_step() 前参数注入逻辑 |
| WorkflowReceipt / WorkflowStepReceipt / delegation sidecar | WorkflowEngine 的 collect_step_result() / finalize_receipt() |
| workflow-scoped compensation_hints 汇总 | tools/src/execution/CompensationLedger.cpp 与 WorkflowEngine receipt 汇总路径 |
| cyclic rejection / failure digest | WorkflowCyclicRejectionTest.cpp、ToolWorkflowFailureIntegrationTest.cpp |

## 4. Build 三件套

1. 代码目标：更新 docs/architecture/DASALL_tools子系统详细设计.md，补齐 WorkflowPlan / WorkflowReceipt internal schema 表、补充 schema 约束，并锁定 028/029 所需的内部对象与函数出口。
2. 测试目标：通过文档检索确认 WorkflowPlan、WorkflowReceipt、step_output_mapping、delegation sidecar、cyclic rejection 已在 architecture/TODO 中形成一致表述。
3. 验收命令：
   - rg -n "WorkflowPlan|WorkflowReceipt|step_output_mapping|cyclic|delegation" docs/architecture/DASALL_tools子系统详细设计.md docs/todos/tools/DASALL_tools子系统专项TODO.md

## 5. 风险与回退

1. 若后续 028 需要额外字段支撑执行实现，优先补 module-local internal schema，而不是把字段提前塞进 shared contracts。
2. `route_kind_hint` 只能作为 workflow step 的静态提示，不能覆盖 RouteSelector 的最终执行通道；若实现时出现“由 plan 强推 route”的趋势，需要回退到当前设计边界。
3. `step_output_mapping` v1 不支持表达式和动态默认值；若 028 尝试把运行时推断逻辑混入 engine，应视为越界并回退到静态映射约束。