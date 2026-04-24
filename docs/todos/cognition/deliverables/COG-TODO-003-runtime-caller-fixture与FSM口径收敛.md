# COG-TODO-003 runtime caller fixture 与 FSM 口径收敛

状态：Done  
日期：2026-04-24  
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md  
任务类型：前置补设计 / 评审门禁  

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.14.1 已有 ActionDecision→FSM 映射，但旧表使用了 `ExecutingAction`、`DelegateEvaluating`、`ErrorHandling` 等并非 runtime 真实 FSM 的状态名。
2. `docs/architecture/DASALL_runtime子系统详细设计.md` §6.7.4 与 `runtime/include/fsm/StateTransitionTypes.h` 已冻结 RuntimeState：`Reasoning`、`ToolCalling`、`WaitingClarify`、`Responding`、`Failed`、`FailedSafe` 等才是可执行状态。
3. `runtime/src/fsm/TransitionGuardTable.cpp` 已把 `Reasoning -> ToolCalling`、`Reasoning -> WaitingClarify`、`Reasoning -> Responding`、`Reasoning -> Failed` 绑定到显式 guard。
4. `tests/unit/runtime/RuntimeSmokeTest.cpp` 仍是 `MockMemoryStore + MockLLMAdapter + MockTool` 直接串联，不能作为 runtime↔cognition caller fixture 的最终口径。
5. 当前 `runtime/src/AgentOrchestrator.cpp` 已出现 `make_cognition_step_request()` / `make_reflection_request()` 的生产雏形，说明 caller fixture 应围绕 Runtime caller 生成的 request 形状固化，而不是围绕 LLM/Tool mock 串接固化。

## 2. 外部参考

1. W3C SCXML 规范把 state、transition、event 作为状态机最小语义单元，启发本任务把 ActionDecision 视为 Runtime FSM 的输入事件，而不是把 cognition 设计成第二套状态机：https://www.w3.org/TR/scxml/
2. XState guard 文档强调 transition guard 满足后才允许转移，启发本任务把 ActionDecision→FSM 映射同时绑定 RuntimeState 与 TransitionGuardFact，而不是只写目标状态名：https://lecepin.github.io/xstate-docs-cn/guides/guards.html

## 3. 主结论

1. ActionDecision→FSM 的第一跳必须从 Runtime `Reasoning` 状态出发，由 Runtime guard table 裁定转移。
2. v1 ActionDecisionKind 的可执行集合为 `ExecuteAction`、`DirectResponse`、`AskClarification`、`ConvergeSafe`、`NoDecision`。
3. `DelegateHint` 不进入 v1 caller fixture；若后续开放，必须先补 Runtime FSM 状态/guard 和 COG-TODO-027 交互契约。
4. RuntimeCognitionLoopSmokeTest 的 caller fixture 必须由 Runtime 构造 `CognitionStepRequest` / `ReflectionRequest`，不得继续用 `MockLLMAdapter + MockTool` 旁路 cognition。
5. `caller_domain` 固定为 `runtime.agent_orchestrator`，作为测试与观测的调用来源锚点。

## 4. ActionDecision→FSM 映射

| ActionDecision.decision_kind | Runtime 起点 | Runtime 第一跳目标 | 必需 guard | 后续动作 |
|---|---|---|---|---|
| `ExecuteAction` | `Reasoning` | `ToolCalling` | `ToolCallPlanned` + `BudgetAllowsToolCall` | Runtime 构造 ToolRequest 并交 ToolManager |
| `DirectResponse` | `Reasoning` | `Responding` | `DirectResponseReady` | Runtime 调 `IResponseBuilder.build()` |
| `AskClarification` | `Reasoning` | `WaitingClarify` | `ClarificationNeeded` + `ProfileAllowsClarify` | Runtime 写 pending action / checkpoint 并等待用户补充 |
| `ConvergeSafe` | `Reasoning` | `Responding` | `DirectResponseReady` | Runtime 用已有结论构造安全终态响应 |
| `NoDecision` | `Reasoning` | `Failed` | `RecoveryRejected` | Runtime fail-fast 到失败 / 降级链，不执行工具 |

## 5. caller fixture 最小形状

| 字段 | `CognitionStepRequest` | `ReflectionRequest` | 断言 |
|---|---|---|---|
| `caller_domain` | 必填：`runtime.agent_orchestrator` | 必填：`runtime.agent_orchestrator` | 匿名 caller 拒绝进入 fixture |
| `request_id` / `trace_id` | 必填 | 必填 | 与 Runtime stage_trace、tool request、AgentResult 可关联 |
| `profile_id` | 必填 | 必填 | 用于 profile 策略和 stage route 投影 |
| `goal_contract` / `goal_id` | 必填 | 必填 | Runtime 负责从 AgentRequest 归一化 |
| `context_packet` | 必填 | 必填 | 已由 Memory 装配，cognition 不 retrieve |
| `belief_state` | 必填 | 必填 | 缺失时 fail-fast |
| `latest_observation` | 可选 | 必填 | 首轮 decide 可为空；reflect 必须有外部 Observation |
| `budget_context` | 建议必填 | 建议必填 | 用于预算感知与降级判定 |

## 6. RuntimeCognitionLoopSmokeTest 口径

1. 正例：Runtime 构造 caller fixture，cognition 返回 `ExecuteAction`，FSM 从 `Reasoning` 第一跳到 `ToolCalling`。
2. 正例：cognition 返回 `DirectResponse` / `ConvergeSafe`，FSM 从 `Reasoning` 第一跳到 `Responding`，并调用 response builder。
3. 正例：cognition 返回 `AskClarification`，FSM 从 `Reasoning` 第一跳到 `WaitingClarify`。
4. 负例：cognition 返回 `NoDecision` 或 unknown future decision_kind，Runtime 不进入 `ToolCalling` / `Responding`，而是 fail-fast 到 `Failed` / recovery rejected 语义。
5. 旁路禁止：`MockLLMAdapter + MockTool` 直接串接只能作为 legacy runtime smoke，不能作为 COG-TODO-026 / 027 的验收 fixture。

## 7. D 原子项完成情况

| 原子项 | 目标 | 结果 |
|---|---|---|
| D1 | 对齐 cognition 表与 Runtime 真实 FSM 状态 | PASS：目标状态改为 `ToolCalling` / `WaitingClarify` / `Responding` / `Failed` |
| D2 | 固化 caller fixture 字段 | PASS：caller_domain、goal/context/belief/observation/budget 全部落表 |
| D3 | 明确 RuntimeCognitionLoopSmoke 最小正负例 | PASS：四类正例与一类负例已定义 |

## 8. Design -> Build 映射

| 设计结论 | 后续 Build 任务 | 验收点 |
|---|---|---|
| ExecuteAction -> Reasoning/ToolCalling | COG-TODO-016、026、027 | RuntimeCognitionLoopSmokeTest 断言 ToolCallPlanned / BudgetAllowsToolCall |
| DirectResponse / ConvergeSafe -> Responding | COG-TODO-019、026、027 | response builder 被调用，AgentResult 可提交 |
| AskClarification -> WaitingClarify | COG-TODO-016、026、027 | pending action / checkpoint 语义可验证 |
| NoDecision -> Failed | COG-TODO-016、027 | 不误入工具执行或成功响应 |
| caller_domain 固定为 runtime.agent_orchestrator | COG-TODO-007、010、024、026 | MockCognitionFixture 与 runtime smoke 不允许匿名 caller |

## 9. Build 三件套与验收

代码目标：更新 cognition 详设、专项 TODO 与本交付物，不新增生产代码。  
测试目标：文档一致性检索，确认 `ActionDecision.decision_kind`、`CognitionStepRequest`、`ReflectionRequest`、`RuntimeCognitionLoopSmoke`、`caller_domain` 与 FSM 口径可检索。  
验收命令：

```bash
rg -n "ActionDecision\.decision_kind|CognitionStepRequest|ReflectionRequest|RuntimeCognitionLoopSmoke|caller_domain|FSM" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md
```

验收结论：PASS。检索可定位真实 Runtime FSM 状态映射、caller fixture 字段、RuntimeCognitionLoopSmoke 断言与 COG-BLK-003 解阻记录。

## 10. D Gate 与合规复核

| 检查项 | 结论 |
|---|---|
| D 文档已落盘 | PASS |
| Design->Build 映射完整 | PASS |
| Build 三件套已锁定 | PASS |
| 代码注释要求 | 不适用，本轮为文档门禁 |
| 正负例覆盖 | 正例：ExecuteAction/DirectResponse/AskClarification；负例：NoDecision 不误执行 |
| TODO / 交付物 / worklog 可追溯 | PASS |
| COG-BLK-003 | 已由本任务解阻设计口径；生产 smoke 仍由 COG-TODO-026 落地 |
