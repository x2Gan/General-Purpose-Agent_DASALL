# TOOL-TODO-028 WorkflowEngine 收敛

日期：2026-04-16  
任务：TOOL-TODO-028  
状态：已完成

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-028 要求实现 WorkflowEngine，并以 topological ordering、failure stop、delegation sidecar、cyclic rejection 作为 unit 验收出口。
2. docs/architecture/DASALL_tools子系统详细设计.md 已在 027 中冻结 WorkflowPlan / WorkflowReceipt internal schema，因此 028 的最小实现边界已经明确：只落 DAG-only engine，不实现条件分支、循环或 runtime 第二主控平面。
3. tools/src/ToolManager.cpp 当前已经把 Workflow descriptor 路由到 `ToolIRRoute::WorkflowEngine`，因此本轮必须把 workflow route 从 default executor 替换为真实 WorkflowEngine，而不是继续返回占位成功结果。

## 2. 实现结论

1. 新增 tools/src/execution/WorkflowEngine.h 与 tools/src/execution/WorkflowEngine.cpp，落盘 WorkflowPlan/Step/Receipt/DelegationSidecar internal object，以及 `execute()`、`build_batches()`、`dispatch_step()`、`collect_step_result()`、`finalize_receipt()` 五个核心入口。
2. `build_batches()` 采用 DAG-only 拓扑构建：对 step id、depends_on、edges、step_output_mapping 做前置校验，并在图不可拓扑排序时直接返回 `InvalidWorkflowPlan` rejection。
3. `dispatch_step()` 支持两类 step：
   - tool step：基于 route hint 在 builtin/MCP executor 中执行，并在派发前完成静态 `step_output_mapping` 参数注入。
   - delegation step：不进入 executor，只生成 recommendation sidecar 并汇总到 WorkflowReceipt。
4. ToolManager 的 workflow route 现在显式走 WorkflowEngine，并把 WorkflowReceipt 序列化结果折叠进 workflow ToolResult payload，再沿既有 projector 生成 Observation / Digest。
5. 本轮不引入 CompensationLedger；workflow-scoped `compensation_hints` 仍保留为空，相关逻辑留给 TOOL-TODO-029 独立提交。

## 3. Design -> Build 映射闭合

| 设计项 | 代码落点 | 测试出口 |
|---|---|---|
| DAG-only 拓扑排序 | WorkflowEngine::build_batches() | WorkflowEngineTest.cpp、WorkflowCyclicRejectionTest.cpp |
| 静态 step_output_mapping | WorkflowEngine::dispatch_step() | WorkflowEngineTest.cpp |
| delegation sidecar recommendation | WorkflowEngine::dispatch_step() / collect_step_result() | WorkflowEngineTest.cpp |
| failure stop 与 skipped steps | WorkflowEngine::collect_step_result() / finalize_receipt() | WorkflowEngineTest.cpp |
| ToolManager workflow route 接线 | tools/src/ToolManager.cpp | WorkflowEngineTest.cpp + 后续 030 integration |

## 4. 验证

1. 构建：
   - Build_CMakeTools：`dasall_tools`
   - Build_CMakeTools：`dasall_workflow_engine_unit_test`
   - Build_CMakeTools：`dasall_workflow_cyclic_rejection_unit_test`
   - Build_CMakeTools：`dasall_unit_tests`
2. 定向执行：
   - RunCtest_CMakeTools：`WorkflowEngineTest`
   - RunCtest_CMakeTools：`WorkflowCyclicRejectionTest`
3. 结果摘要：
   - `WorkflowEngineTest` 通过，确认 batch 拓扑、静态 output mapping、delegation sidecar 与 failure stop 全部成立。
   - `WorkflowCyclicRejectionTest` 通过，确认 cyclic graph 在 build_batches 阶段被 reject，并把 rejection 写入 WorkflowReceipt / ToolResult error。
   - `dasall_unit_tests` 聚合构建通过，并自动执行当前 unit 集合，无新增 tools unit 回归。
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，不影响本任务通过结论。

## 5. 风险与回退

1. 当前 JSON pointer / arguments 注入只覆盖 v1 所需的静态字段映射，不支持表达式或动态默认值；若后续 workflow 需要更强表达能力，必须通过 schema 扩展而不是在 engine 内部偷偷长解释器。
2. 本轮 workflow route 默认 plan_loader 仍未接生产 parser，因此只有显式注入 plan_loader 的场景可执行；这符合当前“先落 engine 内核，再落 compensation/integration gate”的原子任务边界。
3. 028 故意没有把 workflow-level compensation_hints 提前做进 engine；如在本轮混入 ledger 逻辑，会破坏 TOOL-TODO-029 的独立提交面。