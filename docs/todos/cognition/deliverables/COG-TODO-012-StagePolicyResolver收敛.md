# COG-TODO-012 StagePolicyResolver 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready stage policy resolution

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.1 已把 `StagePolicyResolver` 的职责固定为：按 profile、request hints 和 budget/context 信号生成 `StageExecutionPlan`，禁止在各阶段内部散落 profile 分支。
2. 同一章节明确 `StageExecutionPlan` 至少要表达 `enabled_stages`、`preferred_model_tier`、`fallback_mode`、`max_plan_nodes`、`max_plan_depth`、`clarification_threshold`。
3. §6.15.3 要求门面层在每个阶段遵守 `deadline_ms`，避免单阶段卡死拖垮全链。
4. §6.16.2 要求 budget-aware 行为在 `budget_utilization` 达到 0.5 / 0.8 两个阈值时分别触发 planning cap 收紧与更保守的决策策略。
5. COG-TODO-011 已提供 `CognitionConfigProjector`，因此 012 不再缺 profile→cognition 投影视图，resolver 可以直接消费 `RuntimePolicySnapshot` 与 projector 输出。

## 2. 外部参考

1. AWS Builders' Library 指出超时是防止依赖长时间占用客户端资源的基础手段，超时过大和无限等待都会放大级联故障风险：https://aws.amazon.com/builders-library/timeouts-retries-and-backoff-with-jitter/
2. Azure Bulkhead pattern 强调对依赖调用做资源隔离与故障域切分，避免一个不响应依赖拖垮整个消费者：https://learn.microsoft.com/en-us/azure/architecture/patterns/bulkhead

本轮借鉴点：`StagePolicyResolver` 负责在 cognition 入口统一导出阶段 deadline 和保守 fallback mode，使后续 facade 能按 plan 做隔离，而不是让各阶段各自发明超时和降级策略。

## 3. 主结论

1. `StageExecutionPlan` 作为 cognition 私有 supporting type 落在 `cognition/src/StagePolicyResolver.h`，不提升到 public include，也不进入 shared contracts。
2. `resolve_decide_plan()` 统一输出 planning + execution 两段链路，并把 `perception` / `plan` / `action_decision` 的 stage hint 一并挂入 plan，避免下游再做 profile 分支判断。
3. `resolve_reflection_plan()` 统一输出 reflection plan，并保持 conservative fallback 只在 low-latency 等受控场景激活。
4. `resolve_response_plan()` 统一裁定 response plan 的 template fallback 语义，区分 `TemplateAllowed` 与 `TemplatePreferred`；`factory_test` 默认模板优先。
5. budget-aware 规则只在已有 `BudgetContext` 的 decide 路径生效：
   - `0.5 ≤ utilization < 0.8` 时把 `max_plan_nodes` 收紧为默认值 50%
   - `utilization ≥ 0.8` 时限制为 1-2 节点浅层计划，并提升澄清保守度
6. legacy alias 或缺失 canonical route 时，resolver 继续 fail-closed，而不是兜底发明第二套 stage mapping。

## 4. 边界与职责

| 组件 | 职责 | 非职责 |
|---|---|---|
| `StagePolicyResolver` | 从 projector 输出和 request hints 解析三类 `StageExecutionPlan` | 不直接执行阶段；不重写 `RuntimePolicySnapshot`；不控制 llm/router 生命周期 |
| `StageExecutionPlan` | 承载阶段启停、deadline、fallback mode、plan cap 和澄清阈值 | 不暴露为 shared/public contract |
| `CognitionConfigProjector` | 提供 profile→cognition 默认值与 `StageModelHint` 派生 | 不负责 request 级 budget/hint 合并 |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| 阶段启停在 resolver 统一裁定 | `cognition/src/StagePolicyResolver.cpp` | decide / reflection / response 三类 plan 都能独立生成 |
| deadline_ms 作为阶段超时隔离入口 | 同上 | low-latency hint 能收紧 deadline；默认值来自 llm timeout lane |
| budget-aware planning cap 统一在 resolver 落地 | `BudgetAwareDecisionTest.cpp` | 中高预算压力下 `max_plan_nodes/max_plan_depth` 与 `clarification_threshold` 有可断言收紧 |
| profile 差异不散落在阶段组件 | `StagePolicyResolverProfileDiffTest.cpp` | `edge_minimal`、`cloud_full`、`factory_test` 的差异由 resolver 一次输出 |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增 `StageExecutionPlan` 与 resolver 私有实现 | 三类 plan 都能从 snapshot + request 解析 | `Build_CMakeTools(buildTargets=["dasall_stage_policy_resolver_unit_test","dasall_stage_policy_resolver_profile_diff_unit_test","dasall_budget_aware_decision_unit_test"])` | 若 supporting type 外溢，回退到 src 私有 header |
| B2 | 新增 resolver focused unit tests | 默认策略、profile diff、budget-aware 三类行为可独立断言 | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_policy_resolver_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_policy_resolver_profile_diff_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_budget_aware_decision_unit_test` | 若行为定义过宽，仅保留详设已冻结字段 |
| B3 | 接入 cognition / unit CMake | resolver 可供后续 014~019 直接消费 | 同上 | 若接线过大，限制在 cognition 与 cognition unit 目录 |

## 7. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_stage_policy_resolver_unit_test","dasall_stage_policy_resolver_profile_diff_unit_test","dasall_budget_aware_decision_unit_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["StagePolicyResolverTest","StagePolicyResolverProfileDiffTest","BudgetAwareDecisionTest"])`
   - 结果：失败，返回仓库已知通用错误“生成失败”。
3. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_policy_resolver_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_policy_resolver_profile_diff_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_budget_aware_decision_unit_test`
   - 结果：通过；三项 focused unit tests 全部零输出退出。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 代码注释 | PASS：实现以 plan 字段和 resolver 函数命名自解释，无需额外注释 |
| 正例覆盖 | PASS：覆盖默认 decide plan、factory_test response plan、cloud/edge profile diff、budget-aware 中高压场景 |
| 负例覆盖 | PASS：缺失 canonical response route 时 resolver fail-closed |
| 测试发现性 | PASS：新增三个独立 cognition unit targets |
| 范围控制 | PASS：仅修改 resolver、其 unit tests 与文档，不提前触及 façade 或阶段组件 |