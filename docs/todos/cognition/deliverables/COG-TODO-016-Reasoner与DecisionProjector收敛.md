# COG-TODO-016 Reasoner 与 DecisionProjector 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready reasoning stage implementation

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.2 已将 `Reasoner` 冻结为“基于 PlanGraph、BeliefState、ContextPacket 和最新 Observation 选择下一步 ActionDecision”，并要求它只输出动作意图，不直接生成 ToolRequest、不直接调用 tools、也不修改 PlanGraph。
2. 同一章节要求 `ActionDecision` 至少显式表达 `decision_kind`、`selected_node_id`、`rationale`、`confidence`、`clarification_needed`、`clarification_question`、`tool_intent_hint`、`delegate_hint`、`response_outline`；`Reasoner` 的内部职责应覆盖 `score_candidates()`、`evaluate_clarification_need()`、`project_response_outline()` 等语义。
3. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.14.1 已冻结 Runtime→Cognition 第一跳映射：`ExecuteAction -> ToolCalling`，`DirectResponse / ConvergeSafe -> Responding`，`AskClarification -> WaitingClarify`，`NoDecision -> Failed`。因此 016 必须稳定投影上述 decision kinds，而不是输出模糊状态。
4. 失败与降级语义已冻结：候选分数冲突或 Observation 与计划前提严重矛盾时，优先返回 `AskClarification` 或 reasoning conflict 语义；规则降级存在时也只能输出保守动作，例如 clarification、direct response 或 converge safe，不得伪造复杂工具意图。
5. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 已把 016 的完成判定绑定到四类候选行为：执行、澄清、收敛、冲突处理；验收出口收敛到三条 focused reasoner tests。

## 2. 外部参考

1. Prompt Engineering Guide 的 ReAct 文档指出：ReAct 把 reasoning 与 acting 交织起来，让模型通过 thought-action-observation 循环动态维护、更新行动计划，并利用 observation 修正后续动作选择；这种模式强调“基于最新观察更新下一步动作”，而不是把动作选择固化为与外界脱钩的静态模板：https://www.promptingguide.ai/techniques/react

本轮借鉴点：Reasoner 把 `latest_observation` 作为 candidate scoring 的一等输入，在 observation 与当前计划冲突时优先走 clarification / safe converge，而不是继续放大已失效的执行路径；同时通过 `DecisionProjector` 把运行时需要的 tool hint、response outline 和 explanation 字段一次性投影成稳定 `ActionDecision`。

## 3. 主结论

1. 新增私有 `Reasoner` 与 `DecisionProjector`，落在 `cognition/src/reasoning/Reasoner.h/.cpp` 与 `cognition/src/reasoning/DecisionProjector.h/.cpp`；`Reasoner` 实现 `IReasoner::decide()`，`DecisionProjector` 负责把被选中的候选投影为完整 `ActionDecision`。
2. `Reasoner` 首版规则式评分显式覆盖四类候选：`execute_action`、`direct_response`、`ask_clarification`、`converge_safe`。评分输入包括 `PerceptionResult.confidence`、`BeliefState.confidence`、plan open questions、budget pressure、latest observation conflict 和 direct-response terminal plan 形态。
3. `Reasoner` 现有保守路径如下：
   - 存在 open question、`requires_clarification=true` 或 perception 置信度低于澄清阈值时，优先输出 `AskClarification`；
   - terminal direct-response plan 或 `task_type=direct_response` 且分数超过直答阈值时，输出 `DirectResponse`；
   - budget 接近极限且已无可行动节点时，输出 `ConvergeSafe`；
   - 常规 actionable plan 则投影为 `ExecuteAction`，并附带 `tool_intent_hint`、`selected_node_id` 与 `response_outline`。
4. `DecisionProjector` 负责统一生成 `tool_intent_hint`、`response_outline` 与 `candidate_scores`，保证 `ActionDecision` 具备 explainability 字段，不把 ToolRequest 细节泄漏到 cognition 外部。
5. 在本轮局部修补后，terminal `direct_response` 计划不再被误判为 `ExecuteAction`；reasoner 现在会跳过 `direct_response` / `validation` 节点的“可执行节点”解析，并在单节点 direct-response plan 上显式提高直答候选分值。

## 4. 边界与职责

| 组件 | 职责 | 非职责 |
|---|---|---|
| `Reasoner` | 读取 plan / perception / belief / observation，评分四类候选并选出最终 `ActionDecision` | 不生成 ToolRequest；不直接调用 tools；不修改 PlanGraph；不执行 replan |
| `DecisionProjector` | 把已选候选投影为完整 `ActionDecision`，补齐 tool hint、response outline、candidate scores | 不直接做候选评分；不持有 runtime/FSM 状态 |
| `ActionDecision` | 承载 reasoning 阶段的模块内公共动作意图 | 不等于 ToolRequest；不扩张进 shared contracts |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| actionable plan 要投影为可执行动作意图 | `cognition/src/reasoning/Reasoner.cpp` + `DecisionProjector.cpp` | `ExecuteAction` 带 `selected_node_id`、`tool_intent_hint`、`response_outline` |
| 信息不足要优先澄清 | `Reasoner.cpp` + `ReasonerClarificationThresholdTest.cpp` | open question / 低置信输入输出 `AskClarification` |
| direct-response terminal plan 不能误判为执行 | `Reasoner.cpp` + `ReasonerActionDecisionTest.cpp` | 单节点 `direct_response` 计划输出 `DirectResponse` |
| 冲突 observation 要保守处理 | `Reasoner.cpp` + `ReasonerConflictResolutionTest.cpp` | conflict 下优先 `AskClarification` |
| budget 紧张且无可行动节点时安全收敛 | `Reasoner.cpp` + `ReasonerConflictResolutionTest.cpp` | near-budget-limit 且无 active node 时输出 `ConvergeSafe` |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增私有 `Reasoner` / `DecisionProjector` 与规则评分 helper | 执行 / 直答 / 澄清 / 安全收敛四类候选可局部断言 | `Build_CMakeTools(buildTargets=["dasall_reasoner_action_decision_unit_test","dasall_reasoner_clarification_threshold_unit_test","dasall_reasoner_conflict_resolution_unit_test"])` | 若 direct-response / execute 边界混淆，优先收紧 active node 解析与候选阈值 |
| B2 | 把 ActionDecision explainability 字段统一投影到 projector | tool hint、response outline、candidate scores 可独立验证 | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_reasoner_action_decision_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reasoner_clarification_threshold_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reasoner_conflict_resolution_unit_test` | 若 projector 失配，回退到保守 clarification / converge safe，而不是输出不完整决策 |
| B3 | 注册三个 reasoner-focused unit targets | discoverability 与直接执行成立 | 同上 | 若测试接线扩大，只保留 cognition unit slice 内变更 |

## 7. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_reasoner_action_decision_unit_test","dasall_reasoner_clarification_threshold_unit_test","dasall_reasoner_conflict_resolution_unit_test"])`
   - 结果：通过。
2. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_reasoner_action_decision_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reasoner_clarification_threshold_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reasoner_conflict_resolution_unit_test`
   - 第一次结果：失败；局部定位到 `ReasonerActionDecisionTest` 的 terminal direct-response plan 被误判为 `ExecuteAction`。
   - 修补同一 slice 后复跑：通过；三条 reasoner-focused unit tests 均零输出退出。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 范围控制 | PASS：只新增 reasoning 私有组件、三条 focused tests 与最小 CMake 接线 |
| 正例覆盖 | PASS：覆盖 `ExecuteAction` 与 `DirectResponse` 两类正向决策 |
| 负例与降级 | PASS：覆盖 `AskClarification`、冲突 observation 和 `ConvergeSafe` |
| 测试发现性 | PASS：新增三个 cognition reasoner unit targets，可单独 build 和直接执行 |
| 架构边界 | PASS：未生成 ToolRequest，未把 ActionDecision 字段推进 shared contracts，仍保持 cognition→runtime 第一跳边界 |