# COG-TODO-014 PerceptionEngine 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready perception stage implementation

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.2 已将 `PerceptionEngine` 冻结为“从 GoalContract、ContextPacket、BeliefState 提取任务意图、关键实体、约束、歧义点和澄清问题”的阶段组件，且明确非职责边界是不生成 PlanGraph、不选择 ActionDecision、不写回 Memory。
2. 同一章节要求 `PerceptionResult` 至少覆盖 `intent_summary`、`task_type`、`entities`、`constraints_digest`、`ambiguities`、`clarification_questions`、`confidence`、`requires_clarification`，并建议内部接口包含 `perceive()`、`extract_entities()`、`detect_ambiguities()`、`derive_clarification_questions()`、`run_rule_fallback()`、`validate_perception_output()`。
3. 失败与降级语义已冻结：输入边界不满足时返回 `cognition.invalid_input`；llm 不可用且 `rule_fallback_enabled=true` 时允许规则感知降级；若关键信息缺失但仍可安全澄清，则优先输出 `clarification_questions` 与低置信度结果，而不是伪造完整意图。
4. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 中 014 的验收出口已明确绑定 `PerceptionEngineTest`、`PerceptionBoundaryValidationTest`、`PerceptionClarificationRuleTest` 三条路径。
5. COG-TODO-013 已完成 `InputBoundaryValidator`，因此 014 可以直接复用现有 boundary validator 作为感知阶段入口校验，不必在 `PerceptionEngine` 内再散落 required-field 检查。

## 2. 外部参考

1. Google Cloud Dialogflow ES 文档说明：当参数被标记为 required 且用户未提供完整数据时，代理会先通过 prompts 继续收集缺失参数，直到意图完整后才进入后续 fulfillment；这是一种典型的“缺槽先澄清”策略：https://docs.cloud.google.com/dialogflow/es/docs/intents-actions-parameters

本轮借鉴点：PerceptionEngine 在发现目标未指明、证据不足或工具路由缺失时，不直接向下游伪造可执行意图，而是优先产出 clarification question 和更低的置信度，保持感知阶段的保守收敛。

## 3. 主结论

1. 新增私有 `PerceptionEngine` 组件，放在 `cognition/src/perception/PerceptionEngine.h/.cpp`，通过 `perceive()` 输出结构化 `PerceptionResult`。
2. `PerceptionEngine` 在入口直接复用 `InputBoundaryValidator::validate_decide_request()`；无效输入直接返回 `nullopt`，保持输入边界权责集中在已有 validator 上。
3. 首版感知链采用规则降级实现：
   - `extract_entities()` 从 goal、user turn、active tools、latest observation 提取基础实体；
   - `derive_constraints()` 把 goal constraints、success criteria 与 policy digest 投影到 `ConstraintDigest`；
   - `detect_ambiguities()` 针对 pronoun-only 输入、低置信 hypotheses 和缺失 tool route 产出显式歧义标记；
   - `derive_clarification_questions()` 为每类歧义生成保守澄清问题；
   - `validate_perception_output()` 在输出前做最小结构校验。
4. 当 `rule_fallback_enabled=false` 且请求仍是 pronoun-only 的未解歧义输入时，PerceptionEngine 会 fail-closed 返回 `nullopt`；当规则降级开启时，则保守地产出 clarification path，而不凭空生成具体目标。
5. 014 只落私有感知组件与 focused tests，不提前把它接入完整 façade 决策主链；真正的串联收口仍留给 COG-TODO-023。

## 4. 边界与职责

| 组件 | 职责 | 非职责 |
|---|---|---|
| `PerceptionEngine` | 从当前 request 中抽取意图、实体、约束、歧义与澄清问题，输出 `PerceptionResult` | 不生成计划；不做动作选择；不调用 tools；不写 Memory；不控制恢复 |
| `InputBoundaryValidator` | 作为感知入口的 required-field gate | 不负责意图抽取和澄清推导 |
| `PerceptionResult` | 承载感知阶段结构化输出 | 不包含 ToolRequest、provider payload 或恢复执行字段 |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| 感知阶段先抽意图和实体，再决定是否澄清 | `cognition/src/perception/PerceptionEngine.cpp` | actionable 输入输出 `task_type=action_decision` 且不要求澄清 |
| 低置信歧义优先走 clarification | `PerceptionEngine.cpp` + `PerceptionEngineTest.cpp` | pronoun-only / low-confidence 输入输出 `clarification_questions` |
| llm 未落地前允许规则降级 | `PerceptionEngine.cpp` + `PerceptionClarificationRuleTest.cpp` | `rule_fallback_enabled=true` 时保留保守澄清路径 |
| 规则降级关闭时必须 fail-closed | `PerceptionClarificationRuleTest.cpp` | 同类歧义输入在 no-fallback 配置下返回 `nullopt` |
| 输入边界由既有 validator 复用 | `PerceptionEngine.cpp` + `PerceptionBoundaryValidationTest.cpp` | boundary 负例继续通过，不在引擎内部复制 required-field 校验 |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增私有 `PerceptionEngine` 和核心 helper | actionable / ambiguous 两类感知路径可二值断言 | `Build_CMakeTools(buildTargets=["dasall_perception_engine_unit_test","dasall_perception_clarification_rule_unit_test","dasall_perception_boundary_validation_unit_test"])` | 若私有接口外溢，回退为 `cognition/src/perception` 私有 header |
| B2 | 把规则降级、澄清问题和 fail-closed 条件固化到组件内部 | 正例 / 负例均可独立验证 | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_engine_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_clarification_rule_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_boundary_validation_unit_test` | 若过早耦合 façade，限制回 PerceptionEngine 私有实现 |
| B3 | 注册两个新感知 unit targets | discoverability 与直接执行成立 | 同上 | 若测试接线过大，仅修改 cognition unit 范围 |

## 7. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_perception_engine_unit_test","dasall_perception_clarification_rule_unit_test","dasall_perception_boundary_validation_unit_test"])`
   - 第一次结果：通过，但 `PerceptionEngineTest` 暴露歧义路径置信度未压到澄清阈值以下的局部行为偏差。
   - 修补同一 slice 后复跑：通过。
2. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_engine_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_clarification_rule_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_boundary_validation_unit_test`
   - 第一次结果：失败，`PerceptionEngineTest` 断言“ambiguous input should lower perception confidence below the clarification threshold”未满足。
   - 修补同一 slice 后复跑：通过；三项 focused unit tests 均零输出退出。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 范围控制 | PASS：只新增 PerceptionEngine 私有组件、两项 focused tests 与最小 CMake 接线 |
| 正例覆盖 | PASS：覆盖 actionable 感知路径与实体/约束提取 |
| 负例覆盖 | PASS：覆盖 pronoun-only 歧义输入在 fallback on/off 两种配置下的保守澄清与 fail-closed |
| 测试发现性 | PASS：新增两个 cognition unit targets，复用已有 `PerceptionBoundaryValidationTest` 作为边界负例复验 |
| 架构边界 | PASS：未侵入 ADR-006/007/008；感知组件仍不直接生成计划、动作或恢复执行 |