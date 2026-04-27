# COG-TODO-009 ActionDecision 与 BudgetContext 对象收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready module-public decision and budget supporting types

## 1. 本地证据

1. COG-TODO-002 已冻结 cognition↔llm canonical stage key 为 `planning/execution/reflection/response`，COG-BLK-002 已解阻，`StageModelHint` 可直接按该 taxonomy 落盘。
2. COG-TODO-005 已建立 `cognition/include/` 与 `dasall_cognition` public header file set，`decision/ActionDecision.h`、`belief/BeliefUpdateHint.h`、`CognitionTypes.h` 已具备稳定承载路径。
3. COG-TODO-006 已接线 `CognitionInterfaceSurfaceTest`，本轮可以继续用它冻结 supporting types 字段并覆盖越界字段负例。
4. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.5.3 / §6.13.2 要求 `ActionDecision` 至少显式表达 `decision_kind`、`selected_node_id`、`rationale`、`confidence`、`clarification_needed`、`clarification_question`、`tool_intent_hint`、`delegate_hint`、`response_outline`；§2.1 / §6.14.1 明确 cognition 只输出动作意图，不生成 ToolRequest。
5. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.14.2 已给出 `StageModelHint` 字段表，要求 `stage_name` 直接使用 canonical stage key，`preferred_provider` 保持空字符串默认值，由 llm 自主路由。
6. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.14.4 / §6.14.5 / §6.16 要求 `BeliefUpdateHint` 仅提供写回建议，不直写 memory；`ContextSufficiencySignal` 仅发信号，`BudgetContext` 首版保持 optional。
7. 代码现状显示 `ActionDecision.h` 仍保留 `tool_name`、`tool_arguments_payload`、`response_text` 等旧字符串字段，`BeliefUpdateHint.h` 仅有 `confirmed_facts/evidence_refs/merge_mode` 简单形状，尚未满足详设收口要求。

## 2. 外部参考

1. Protobuf best practices 强调已投入使用的结构应避免破坏性改型，不新增 required 字段，并优先使用可兼容扩展的字段形状：https://protobuf.dev/best-practices/dos-donts/
2. OpenTelemetry semantic conventions 强调跨代码库采用统一命名方案，避免同一语义在不同组件中出现多套名字：https://opentelemetry.io/docs/concepts/semantic-conventions/

本轮借鉴点：supporting types 的新增字段应以默认值、可选字段和小型嵌套对象保持演进余地；`StageModelHint.stage_name` 必须固定使用单一 canonical vocabulary，不能再把 stage alias 藏在 bridge 或测试里。

## 3. 主结论

1. `ActionDecision` 保持 module-public supporting type，不进入 `contracts/`；旧 `tool_name` / `tool_arguments_payload` / `response_text` string 字段退出 public surface，改由 `tool_intent_hint` 与 `response_outline` 承载语义提示。
2. `ActionDecision` 首版冻结 `selected_node_id`、`clarification_needed`、`candidate_scores` 等解释性字段，保证 Runtime / telemetry 能在不读取 provider-private payload 的前提下理解决策来源。
3. `BeliefUpdateHint` 改为 delta-oriented 结构，显式区分 `confirmed_facts_delta`、`hypotheses_delta`、`assumptions_delta`、`evidence_refs_delta`、`missing_evidence_refs`、`confidence_hint` 与 `merge_mode`，并继续只表达建议、不承担 memory 写回事务。
4. `StageModelHint` 在 `CognitionTypes.h` 落盘 `stage_name`、`task_type`、`capability_tier`、`max_output_tokens`、`deadline_ms`、`requires_structured_output`、`requires_reasoning_trace`、`cost_sensitivity`、`preferred_provider`；其中 `stage_name` 只接受 canonical key。
5. `BudgetContext` / `ContextSufficiencySignal` 继续保持 `CognitionTypes.h` module-public 位置，作为预算与上下文不足信号，不引入 memory retrieve/write 权限。
6. 为保持最小骨架可编译，`cognition/src/CognitionFacade.cpp` 已同步改为消费 `tool_intent_hint`、`response_outline` 与 delta 型 `BeliefUpdateHint`。

## 4. 边界与职责

| 对象 | 职责 | 非职责 |
|---|---|---|
| `ActionDecision` | 向 Runtime 表达“下一步做什么”及其解释性上下文 | 不生成 `ToolRequest`；不暴露 provider payload；不持有 runtime FSM / recovery state |
| `ToolIntentHint` | 向 Runtime 提示建议的工具名、语义意图与参数线索 | 不等于真实工具执行请求；不承载服务路由、超时、重试策略 |
| `ResponseOutline` | 向 ResponseBuilder 提供终态输出摘要与关键点 | 不直接 publish 用户结果 |
| `BeliefUpdateHint` | 向 Runtime / Memory 提供 best-effort 写回建议 | 不触发 memory write transaction |
| `StageModelHint` | 向 LLM bridge 提供结构化模型选择提示 | 不固定 provider；不维护第二套 stage alias |
| `BudgetContext` / `ContextSufficiencySignal` | 传递预算压力与上下文充分性信号 | 不直接驱动 memory 重装配或恢复执行 |

## 5. 数据 / 接口说明

| 类型 | 字段冻结 |
|---|---|
| `ToolIntentHint` | `tool_name`、`intent_summary`、`argument_hints`、`evidence_refs` |
| `DelegateHint` | `delegate_target`、`rationale`、`confidence` |
| `ResponseOutline` | `summary`、`key_points` |
| `CandidateDecisionScore` | `candidate_name`、`score`、`rationale` |
| `ActionDecision` | `decision_kind`、`selected_node_id`、`rationale`、`confidence`、`clarification_needed`、`clarification_question`、`tool_intent_hint`、`delegate_hint`、`response_outline`、`candidate_scores` |
| `FactDelta` / `HypothesisDelta` / `AssumptionDelta` / `EvidenceRefDelta` | `{value field}` + `delta_kind` |
| `BeliefUpdateHint` | `confirmed_facts_delta`、`hypotheses_delta`、`assumptions_delta`、`evidence_refs_delta`、`missing_evidence_refs`、`confidence_hint`、`merge_mode` |
| `StageModelHint` | `stage_name`、`task_type`、`capability_tier`、`max_output_tokens`、`deadline_ms`、`requires_structured_output`、`requires_reasoning_trace`、`cost_sensitivity`、`preferred_provider` |
| `BudgetContext` | `total_budget_tokens`、`consumed_tokens`、`remaining_tokens`、`budget_utilization`、`context_was_truncated`、`near_budget_limit` |
| `ContextSufficiencySignal` | `context_sufficient`、`context_confidence`、`missing_evidence_hints`、`recommend_context_reload` |

## 6. 流程 / 时序

1. `ICognitionEngine::decide()` 在最小骨架中构造 `ActionDecision` 与 `BeliefUpdateHint`，但只输出语义意图与写回提示。
2. Runtime 后续根据 `ActionDecision.tool_intent_hint`、`decision_kind` 与 FSM guard 决定是否真正构造 ToolRequest 或转入 Responding / WaitingClarify。
3. `IResponseBuilder::build()` 在 observation 缺失时优先读取 `terminal_decision.response_outline.summary` 作为终态降级摘要，而不是复用旧 `response_text`。
4. Context 不足时 cognition 通过 `ContextSufficiencySignal.recommend_context_reload` 通知 Runtime；是否重装配仍由 Runtime 决定。

## 7. D 原子项完成情况

| 原子项 | 目标 | 结果 |
|---|---|---|
| D1 | 校验 COG-TODO-002/005/006 与 blocker 状态 | PASS：依赖 Done，COG-BLK-002 已解阻 |
| D2 | 冻结 ActionDecision / BeliefUpdateHint / StageModelHint / BudgetContext / ContextSufficiencySignal 字段 | PASS：见 §5 |
| D3 | 锁定 module-local 边界，不推进 shared contracts | PASS：本轮不修改 `contracts/` |
| D4 | 锁定 Build 三件套 | PASS：见 §9 |

## 8. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| `ActionDecision` 不再暴露 ToolRequest 风格字符串字段 | `cognition/include/decision/ActionDecision.h` | `CognitionInterfaceSurfaceTest` 断言旧 `tool_name` / `tool_arguments_payload` / `response_text` 不存在 |
| `BeliefUpdateHint` 改为 delta-oriented supporting type | `cognition/include/belief/BeliefUpdateHint.h` | surface test 断言 delta 容器、merge mode 与 confidence hint |
| `StageModelHint` 采用 canonical stage key | `cognition/include/CognitionTypes.h` | surface test 断言 `StageModelHint` 字段类型；后续 `StageModelHintProjectionTest` 继续验证 canonical key |
| `BudgetContext` / `ContextSufficiencySignal` 保持 module-public 信号对象 | `cognition/include/CognitionTypes.h` | surface test 断言字段类型与默认值 |
| 最小 façade 直接消费者同步更新 | `cognition/src/CognitionFacade.cpp` | `dasall_cognition` 构建通过，response fallback 改走 `response_outline.summary` |

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 更新 `ActionDecision.h` | 决策字段正例与旧字段负例 | `CognitionInterfaceSurfaceTest` | 若误回到 ToolRequest 风格字段，回退到语义 hint 对象 |
| B2 | 更新 `BeliefUpdateHint.h` | delta 结构与 merge mode 正例 | `CognitionInterfaceSurfaceTest` | 若写回语义越权，回退 memory 事务相关字段 |
| B3 | 更新 `CognitionTypes.h` | `StageModelHint` / `BudgetContext` / `ContextSufficiencySignal` 正例 | `CognitionInterfaceSurfaceTest` | 若出现第二套 stage alias，回退到 canonical key |
| B4 | 更新 `CognitionFacade.cpp` | 直接消费者可编译 | `cmake --build build-ci --target dasall_cognition` | 若骨架仍引用旧字段，按编译错误回收同一 slice |
| B5 | 扩展 `CognitionInterfaceSurfaceTest` | 至少 1 组正例 + 1 组负例 | `ctest --test-dir build-ci -R "^CognitionInterfaceSurfaceTest$" --output-on-failure` | 若负例误伤既有 surface，则只约束 009 对象 |

## 10. D Gate

| 检查项 | 结论 |
|---|---|
| D 文档已落盘 | PASS |
| Design->Build 映射完整 | PASS |
| Build 三件套已锁定 | PASS |
| 范围未越界 | PASS：不引入 llm bridge / runtime interaction / memory 写回事务 |
| 是否允许进入 Build | PASS |

## 11. Build 结果

| 原子项 | 结果 |
|---|---|
| B1 | PASS：`ActionDecision.h` 已从旧 string payload 形状切换到 `tool_intent_hint` / `response_outline` / `candidate_scores` |
| B2 | PASS：`BeliefUpdateHint.h` 已显式区分 facts / hypotheses / assumptions / evidence deltas，并增加 `missing_evidence_refs`、`confidence_hint` |
| B3 | PASS：`CognitionTypes.h` 已落盘 `ModelCapabilityTier` 与 `StageModelHint`，预算与上下文信号仍保持 module-public |
| B4 | PASS：`CognitionFacade.cpp` 已切到新字段形状，并通过 `dasall_cognition` 构建 |
| B5 | PASS：`CognitionInterfaceSurfaceTest` 已覆盖 009 supporting types 的字段正例与旧字段负例 |

## 12. 验证证据

1. `cmake --build build-ci --target dasall_cognition`
   - 结果：通过；第一次失败直接暴露 `CognitionFacade.cpp` 仍引用旧字段，修补后复跑通过。
2. `Build_CMakeTools(buildTargets=["dasall_cognition_interface_surface_unit_test"])`
   - 结果：通过；专用 unit target 已完成构建。
3. `RunCtest_CMakeTools(tests=["CognitionInterfaceSurfaceTest"])`
   - 结果：失败，返回通用错误“生成失败”；与仓库已知 CMake Tools test generation 问题一致，本轮不将其判定为 009 代码回归。
4. `cmake --build build-ci --target dasall_cognition_interface_surface_unit_test && ctest --test-dir build-ci -R "^CognitionInterfaceSurfaceTest$" --output-on-failure`
   - 结果：通过；`CognitionInterfaceSurfaceTest` 1/1 passed。

## 13. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 代码注释 | PASS：仅在 supporting type 头中保留 schema baseline 注释，其余字段名自解释 |
| 正例覆盖 | PASS：surface test 覆盖 ActionDecision、BeliefUpdateHint、StageModelHint、BudgetContext、ContextSufficiencySignal 全部字段形状 |
| 负例覆盖 | PASS：surface test 断言旧 `tool_name` / `tool_arguments_payload` / `response_text` 字段不再出现在 `ActionDecision` 中 |
| 测试发现性 | PASS：`CognitionInterfaceSurfaceTest` 已由 COG-TODO-006 接线并可在 `build-ci` 运行 |
| TODO / worklog 证据 | PASS：专项 TODO 与本交付物已回写 |
| 提交前状态隔离 | PASS：本轮只包含 009 supporting types、直接消费者、surface test 与证据文档 |