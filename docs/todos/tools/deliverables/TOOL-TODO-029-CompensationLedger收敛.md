# TOOL-TODO-029 CompensationLedger 收敛

日期：2026-04-16  
任务：TOOL-TODO-029  
状态：已完成

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-029 要求落盘 `register()`、`lookup()`、`build_hints()`、`record_irreversible_effect()`，并验证 LIFO hints、invoke-scoped 生命周期、irreversible effect 不伪造补偿建议。
2. docs/architecture/DASALL_tools子系统详细设计.md 已明确约束：CompensationLedger 只输出 hints / evidence，不跨 invoke 持久化，不决定是否执行 compensate，且默认建议顺序为 LIFO。
3. 028 已将 WorkflowReceipt 固定为可承载 `compensation_hints`，因此 029 的正确收敛方式是“新增 invoke-scoped ledger，并由 WorkflowEngine 在单次 execute() 内汇总 hints”，而不是扩 shared ToolResult ABI。

## 2. 实现结论

1. 新增 tools/src/execution/CompensationLedger.h 与 tools/src/execution/CompensationLedger.cpp，落盘 `CompensationRecord` internal object，以及 `register_result()`、`lookup()`、`build_hints()`、`record_irreversible_effect()` 四个最小接口。
2. ledger 只在当前 invoke 内保留 `records_` 向量，不依赖静态单例、checkpoint 或持久 store，满足 invoke-scoped 生命周期边界。
3. `build_hints()` 采用逆序遍历 reversible records，按 LIFO 生成 `ToolCompensationHint`；irreversible records 仍保留在 ledger 中供 audit/evidence 使用，但不会伪造 rollback hint。
4. WorkflowEngine::execute(plan, ...) 现在在 step dispatch 后按 side_effects 写入 invoke-local CompensationLedger，并在 finalize 前把 ledger 产出的 hints 汇总回 WorkflowReceipt 与 top-level WorkflowExecutionOutcome。
5. 本轮不实现真正的 compensate 执行入口；runtime 仍是唯一有权决定是否持久化 hints、何时执行补偿、是否跳过某条补偿建议的主控层。

## 3. Design -> Build 映射闭合

| 设计项 | 代码落点 | 测试出口 |
|---|---|---|
| invoke-scoped ledger 生命周期 | CompensationLedger::records_ / WorkflowEngine::execute() 局部实例 | CompensationLedgerTest.cpp |
| LIFO compensation hints | CompensationLedger::build_hints() | CompensationLedgerTest.cpp |
| irreversible effect 不生成补偿建议 | CompensationLedger::record_irreversible_effect() | CompensationLedgerTest.cpp |
| workflow receipt 汇总 compensation_hints | WorkflowEngine::execute() / finalize_receipt() | WorkflowEngineTest.cpp |
| 不扩 shared ToolResult ABI | tools/include/ToolInvocationEnvelope.h 维持不变 | ToolInvocationEnvelopeSurfaceTest.cpp（回归） |

## 4. 验证

1. 构建：
   - Build_CMakeTools：`dasall_tools`
   - Build_CMakeTools：`dasall_compensation_ledger_unit_test`
   - Build_CMakeTools：`dasall_workflow_engine_unit_test`
   - Build_CMakeTools：`dasall_unit_tests`
2. 定向执行：
   - RunCtest_CMakeTools：`CompensationLedgerTest`
   - RunCtest_CMakeTools：`WorkflowEngineTest`
   - RunCtest_CMakeTools：`WorkflowCyclicRejectionTest`
3. 聚合回归：
   - `dasall_unit_tests` 聚合构建触发 285 个 unit tests，全部通过。
4. 结果摘要：
   - `CompensationLedgerTest` 通过，确认 reversible side effects 按 LIFO 产出 hints，irreversible effect 不会伪造 compensation_action。
   - `WorkflowEngineTest` 通过，确认 workflow 失败时仍能把上游 reversible side effects 汇总为 workflow-scoped compensation hints。
   - `WorkflowCyclicRejectionTest` 通过，确认引入 ledger 后未破坏 028 的 DAG rejection 行为。
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，不影响本任务通过结论。

## 5. 风险与回退

1. ledger 当前只消费 `ToolResult.side_effects`，没有引入 side-effect 类型系统或幂等级别枚举；若后续 runtime 需要更精细的补偿优先级，必须先回到 schema/design 层扩 supporting object。
2. 当前 irreversible record 仅做“保留 evidence，不生成 hint”；真正的失败后补偿 orchestration 仍要依赖 030 的 integration gate 去验证 runtime-facing envelope 行为。
3. 029 故意不做跨 invoke store；任何尝试把 records_ 落盘到 tools 模块内部都会直接违反 ADR-007 与 tools 详设中的恢复边界。