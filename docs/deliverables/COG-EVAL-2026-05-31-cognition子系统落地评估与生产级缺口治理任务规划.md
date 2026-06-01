# COG-EVAL-2026-05-31 cognition 子系统落地评估与生产级缺口治理任务规划

状态：Draft
日期：2026-05-31
来源：用户专项评估请求
评估范围：[docs/architecture/DASALL_Agent_architecture.md](../architecture/DASALL_Agent_architecture.md)、[docs/architecture/DASALL_cognition子系统详细设计.md](../architecture/DASALL_cognition子系统详细设计.md)、[cognition/](../../cognition/)（include 686 行 + src 9053 行 + 51 文件）、[tests/unit/cognition/CMakeLists.txt](../../tests/unit/cognition/CMakeLists.txt)、[tests/integration/cognition/CMakeLists.txt](../../tests/integration/cognition/CMakeLists.txt)
评估方法：以实际落地代码为核心判据，结合架构与详设硬约束、行业最佳实践（OpenAI Agents SDK / LangGraph / Reflexion / Self-Refine / DSPy / Letta / Outlines / OpenTelemetry semconv）做对账。

---

## 0. 文档定位与读者

1. 给项目治理与里程碑评审提供一份对 cognition 子系统**生产级达成度**的可追溯结论。
2. 给后续 work package（WP-COG-GAP-*）提供可执行的拆分基线与排序依据。
3. 任何条目都必须能回链到代码文件:行 或 文档章节;凡当前判定不确定的，标注 `待验证` 而非自圆其说。

---

## 1. 评估结论摘要

| 维度 | 现状 | 结论 |
|---|---|---|
| 子系统骨架（5 阶段 + 3 入口 + 投影/校验/遥测/策略投影/LLM 桥） | 编译可跑、测试齐备 | **结构层达成度高（约 70%）** |
| 与 ADR-006 / ADR-007 / ADR-008 owner 边界 | 严格遵守：cognition/src 内 `grep ToolManager / MemoryStore / KnowledgeService / PromptRegistry / PromptComposer` 全部为空；`ILLMManager` 仅出现在 [CognitionDependencies.h](../../cognition/include/CognitionDependencies.h) 与 [CognitionLlmBridge.h](../../cognition/src/llm/CognitionLlmBridge.h) | **边界合规** |
| contracts 不被 cognition 侵入 | PlanGraph / ActionDecision 留在 [cognition/include/](../../cognition/include/)；contracts 仅留 `ReflectionDecision` 与 `ActionDecisionTag` | **契约纪律 OK** |
| Schema baseline `cognition.plan.v1` / `cognition.reasoning.v1` | 已冻结，见 [StageSchemaRegistry.cpp](../../cognition/src/validation/StageSchemaRegistry.cpp#L8) / [StageSchemaRegistry.cpp](../../cognition/src/validation/StageSchemaRegistry.cpp#L64) | **达成** |
| 五档 profile 兼容性 + 预算感知 + 模板/规则降级 | `CognitionProfileCompatibilityTest` / `BudgetAwareDecisionTest` / `CognitionFacadeDegradedModeTest` 均存在 | **达成** |
| 业务链贯通（Runtime ↔ Cognition ↔ LLM ↔ Memory ↔ Response） | 决策/反思/响应三链路 + 5 类失败注入均有集成测 | **可贯通，但深度有限** |
| 真实落地 vs 桩 | 无空跑/伪实现；但 perception / reasoner / reflection 仍属"启发式骨架"，**深度不足** | **无虚假，但语义薄** |
| 距离生产级 | 仍欠 prompt-eval 闭环、可重放、嵌入式 gate、生产 SLO、token 成本归因、入口安全 | **未到生产级** |

总体结论：cognition 已完成**架构/接口/治理面**的真实落地（骨架层），**语义面与生产治理面**仍有显著缺口；GA 前必须收敛 P0 项。

---

## 2. DASALL 整体架构目标 vs Cognition 落地（条目级对账）

| 架构原则 / 目标 | 落地证据 | 结论 |
|---|---|---|
| 控制与认知分离 | [CognitionFacade.cpp](../../cognition/src/CognitionFacade.cpp) 仅产出建议；不调 ToolManager / MemoryStore | 达成 |
| 认知与执行分离 | `ActionDecision.tool_intent_hint` 仅为提示，无完整工具参数 | 达成 |
| 平台与业务分离 | cognition/src 无 profile 相关 `#ifdef` 分支 | 达成 |
| 契约优先 | PlanGraph / ActionDecision / BeliefUpdateHint 留在 cognition/include | 达成 |
| 可裁剪可定制 | [StagePolicyResolver.cpp](../../cognition/src/StagePolicyResolver.cpp) + [CognitionConfigProjector.cpp](../../cognition/src/config/CognitionConfigProjector.cpp) 走 profile 投影 | 达成 |
| 可观测（log/trace/metric/audit） | [CognitionTelemetry.cpp](../../cognition/src/observability/CognitionTelemetry.cpp) 覆盖 stage_started / completed / failed / clarification / response_degraded；redaction 默认开 | 基本达成；**生产 sink live 验证 + token/cost 归因 待补** |
| 可恢复（超时/重试/回退/降级/恢复） | `run_stage_with_deadline` 给每个 stage 加 deadline；模板/规则降级；Reflection 出 `ReflectionDecision` 由 Runtime 裁定 | 达成；deadline 触发后**底层 LLM 无 cancel 通道**（token 仍会被烧），见 [CognitionFacade.cpp](../../cognition/src/CognitionFacade.cpp#L57) `run_stage_with_deadline` |
| 跨平台/资源裁剪 | 五档 profile 都开 cognition；StagePolicyResolver 走 budget 投影 | **未做嵌入式实测 gate**：无 `cognition_qemu_or_arm_smoke`，与 access / infra / runtime 风格不一致 |

**普遍性架构缺口**：cognition 未感知 LLM 子系统的 prompt release 状态（eval=blocked / retired），所有 prompt 治理失败都笼统降为 `cognition.llm_unavailable`，对 Runtime 降级裁定不利。生产级建议把"prompt release 失效"映射成 `cognition.policy_denied`。

---

## 3. Cognition 详细设计 vs 实际代码（差距矩阵）

下表只列**有差距/有风险**的条目；全部 223 项可验证要求清单见 [docs/architecture/DASALL_cognition子系统详细设计.md](../architecture/DASALL_cognition子系统详细设计.md)。

### 3.1 已完整落地（抽样）

- 三入口 `decide / reflect / build`：[CognitionFacade.cpp](../../cognition/src/CognitionFacade.cpp)。
- canonical stage key（planning/execution/reflection/response）：[CognitionFacade.cpp](../../cognition/src/CognitionFacade.cpp#L429)、[CognitionFacade.cpp](../../cognition/src/CognitionFacade.cpp#L480)、[CognitionFacade.cpp](../../cognition/src/CognitionFacade.cpp#L822)。
- ContextSufficiencySignal.recommend_context_reload 真实写入：[CognitionFacade.cpp](../../cognition/src/CognitionFacade.cpp#L585)、[CognitionFacade.cpp](../../cognition/src/CognitionFacade.cpp#L1080)。
- 结构化投影方案 A：[ActionDecisionStructuredProjector.cpp](../../cognition/src/projection/ActionDecisionStructuredProjector.cpp)、[PlanGraphStructuredProjector.cpp](../../cognition/src/projection/PlanGraphStructuredProjector.cpp)、[StageOutputValidator.cpp](../../cognition/src/validation/StageOutputValidator.cpp)。
- Reflection 通过 LLM 桥：由 [CognitionFacade.cpp](../../cognition/src/CognitionFacade.cpp#L1647) `consume_reflection_bridge_stage` 调用，本地兜底由 ReflectionEngine 持有。

### 3.2 真实存在但深度不足（"非虚假，但启发式过浅"）

| 设计 ID | 现状 | 风险 | 关联缺口 |
|---|---|---|---|
| COG-V-MB002 / COG-V-CFG007（Perception） | [PerceptionEngine.cpp](../../cognition/src/perception/PerceptionEngine.cpp) 全规则匹配；Facade **不通过 LLM 桥跑 perception** | 多语言/复杂指令准确率低；低 confidence 频繁触发 AskClarification | GAP-P2-A |
| COG-V-SUP005（Reasoner 决策评分） | [Reasoner.cpp](../../cognition/src/reasoning/Reasoner.cpp) 硬编码权重 0.30/0.35/0.20 | 无校准、无随回归 telemetry 调权 | GAP-P1-D |
| Reflection 假设失效 / 失败归因 | [ReflectionEngine.cpp](../../cognition/src/reflection/ReflectionEngine.cpp) 关键词字符串匹配；LLM 桥仅"加强"，主路径仍规则 | 失败归因解释力弱；abort_safe 偏保守 | GAP-P2-C |
| Response template fallback 文案 | [ResponseBuilder.cpp](../../cognition/src/response/ResponseBuilder.cpp) 模板字面量内嵌 | 不便多语言/品牌化 | GAP-P1-E |
| `cognition.reflection.v1` schema | StageSchemaRegistry **未冻结** reflection schema | reflection 桥侧错误码靠 diagnostics 字符串匹配，可观测但脆弱 | GAP-P1-A |
| `cognition.response.v1` schema | response 走 `StageSchemaKind::Text`，无结构 schema | skill_profile.output_schema 要求结构化时无 cognition 不变量校验 | GAP-P1-A |
| COG-V-TEST015（InputBoundaryValidatorTest） | tests CMake 中**未独立挂出**该单测 | 字段缺失拒绝路径回归覆盖率不高 | GAP-P0-E |

### 3.3 设计声明但代码层未显性兑现

| 设计要求 | 现状 | 缺口 | 关联缺口 |
|---|---|---|---|
| BudgetContext 三档行为分级（COG-V-BUD002–4） | Planner 有 node cap 收紧；0.8 阈值"必须优先 ConvergeSafe"未在 Reasoner 显式分支呈现 | 决策路径在审计中无可量化字段 | GAP-P1-F |
| 取消语义（COG-V-ISO003 / 004） | `run_stage_with_deadline` 用 `std::async` + `wait_for`；超时后**不取消**底层 future | LLM token 仍可能被烧 | GAP-P1-C |
| 阶段 metric 标签（COG-V-METRIC002） | `CognitionTelemetry` 以 log / audit 形式发出 | metric name 是否真被 metrics sink 收下 待验证 | GAP-P0-C |
| Production telemetry / production logging（§6.11.5 + §13） | `CognitionProductionTelemetryIntegrationTest` / `CognitionProductionLoggingIntegrationTest` 已存在 | OK；嵌入式 gate 未对齐 | GAP-P0-D |

### 3.4 未发现虚假实现（验证证据）

- `grep -rn "ToolManager\|MemoryStore\|KnowledgeService\|PromptRegistry\|PromptComposer" cognition/` 命中数为 0。
- `ILLMManager` 仅出现在桥与依赖头：[CognitionDependencies.h](../../cognition/include/CognitionDependencies.h)、[CognitionLlmBridge.h](../../cognition/src/llm/CognitionLlmBridge.h)、[CognitionLlmBridge.cpp](../../cognition/src/llm/CognitionLlmBridge.cpp#L270)。
- LLM 桥真实接 `ILLMManager::generate / stream_generate`，不是空跑。
- 抽样 24 个测试目标，23 个能在 tests CMake 中定位；唯一缺失的 `InputBoundaryValidatorTest` 已列入 GAP-P0-E。

---

## 4. 业务链 / 功能链贯通性

| 业务链 | 路径 | 状态 | 证据 |
|---|---|---|---|
| 主成功链 | Runtime → decide → (Plan/Reason via LLM Bridge) → Runtime tool exec → reflect → build | 贯通 | `CognitionRuntimeIntegrationTest`、`RuntimeCognitionLoopSmokeTest` |
| 澄清链 | decide → AskClarification → Runtime WaitingClarify → 用户补充 | 贯通 | `ReasonerClarificationThresholdTest`、`make_clarification_fallback` |
| 重规划链 | reflect → ReflectionDecision::Replan → Runtime 触发 Planner replan | 贯通 | `PlannerReplanTest` + Facade reflection 桥路径 |
| LLM 不可用降级链 | bridge 失败 → rule_fallback / template_fallback → `fallback_used=true` | 贯通 | `CognitionFacadeDegradedModeTest`、`ResponseBuilderTemplateFallbackTest` |
| 失败注入链（5 类） | llm unavailable / schema violation / missing belief / contradictory observation / response fallback | 贯通 | `CognitionFailureInjectionIntegrationTest` |
| 观测链 | Cognition emit → infra sink | 代码贯通；**生产 sink live 待核** | `CognitionProductionTelemetryIntegrationTest` |
| Memory 回写链 | Cognition `BeliefUpdateHint` → Runtime → Memory writeback | 接口贯通；**Memory 端原子性 / 冲突消解契约测**未落 cognition 仓 | 跨子系统协同缺口 |
| 多 Agent / WorkerTask | Multi-Agent owner | **未在 cognition 仓内验证**；属合规推迟 | 后续阶段任务 |
| 嵌入式 / ARM | 仅 ProfileCompatibility | **未做 qemu gate** | GAP-P0-D |

---

## 5. 行业最佳实践对照

| 维度 | 行业主流（2025/2026） | DASALL Cognition 现状 | 评价 / 行动 |
|---|---|---|---|
| Pipeline 形态 | ReAct / Plan-and-Solve / LangGraph 多以**有状态图** + 工具路由 | 线性五段 + Reflection 重规划 | 设计简洁；复杂任务上限低，建议引入多 plan candidate（GAP-P2-D） |
| 结构化输出 | OpenAI JSON mode / Anthropic tool_use / Outlines / Instructor | 自研 `StageSchemaSpec` + 投影 + 投影后 invariant 校验 | 方向正确；建议 schema 直送 provider JSON mode（GAP-P1-A 配套） |
| Prompt 治理 | LangSmith / Promptfoo / DSPy 做版本+评估+回归 | Prompt 由 LLM 子系统 owner；**release 状态不回流 cognition** | **GA 硬缺口**（GAP-P0-A） |
| 工具选择 | function calling 直接产 tool_call args | Cognition 仅产 `tool_intent_hint` | 与 ADR-002 一致；缺**工具候选预筛**（GAP-P2-E） |
| 反思 / 自我修复 | Reflexion / Self-Refine / Critic-of-Critic | 单轮 reflection + Runtime replan | 推理错误兜底弱（GAP-P2-C） |
| 内存与上下文 | Mem0 / Letta / GraphRAG | ContextOrchestrator 由 Memory owner | 架构对齐 |
| 评估 | LLM-as-judge / golden trace / replay | 仅契约 + 单测 + 失败注入 | **缺 replay & golden trace**（GAP-P0-B） |
| 性能 / SLO | p50/p95/p99 stage_latency、token cost per task | stage_latency_ms histogram 已声明 | **未发布 SLO 阈值**；token 成本未在 cognition metric（GAP-P1-B） |
| 安全 | Prompt-injection / PII redaction / output safety | 输出 redaction 完善；**输入侧无 PII / injection 检测** | 入口扫描信号待补（GAP-P3-A） |
| 可重放 | replay harness + deterministic seed | 无 | GA 硬缺口（GAP-P0-B） |
| 设备就绪 | ARM 嵌入式跑通 + qemu CI gate | 仅 ProfileCompatibility | **GAP-P0-D** |

---

## 6. 距离生产级交付的核心缺口（按优先级聚类）

### P0（GA blocker）

- **GAP-P0-A**：Prompt release 状态回流。`CognitionLlmBridge` 必须识别 LLM 侧 release/eval 信号，把 `prompt_retired / eval_blocked` 映射为 `cognition.policy_denied`，与 `cognition.llm_unavailable` 区分。
- **GAP-P0-B**：可重放 / Golden Trace。把 `CognitionStepRequest` + 投影后 LLM 输出 + `CognitionDecisionResult` 落盘为 replay 数据集；CI 增加 `CognitionReplayRegressionTest`。
- **GAP-P0-C**：生产 sink live 验证。抽查 `cognition_stage_latency_ms` / `cognition_stage_total` / `cognition_action_decision_total` 是否真正被 metrics provider 持有；发布 stage_latency 的 SLO 表（p95/p99）。
- **GAP-P0-D**：嵌入式 gate。补 `cognition_qemu_or_arm_smoke`，对齐 access / infra / runtime 的 qemu gate 模式；最小验证 edge_minimal profile 在 ARM 上的 decide+build。
- **GAP-P0-E**：`InputBoundaryValidatorTest` 单测独立化（详设 COG-V-TEST015）。

### P1（生产稳定性）

- **GAP-P1-A**：冻结 `cognition.reflection.v1` 与 `cognition.response.v1` schema，把现有 diagnostics 字符串判定升级为 schema 驱动。
- **GAP-P1-B**：Token / cost / finish_reason 注入 `CognitionTelemetry`，做预算消耗归因（不止依赖 BudgetContext 输入）。
- **GAP-P1-C**：超时取消传递。在 LlmBridge 增加 `abandon_call(call_id)`，Facade deadline 触发时调用一次，避免 token 泄漏。
- **GAP-P1-D**：Reasoner 决策权重 0.30/0.35/0.20 进 `CognitionConfig`，配合 profile 投影；预留离线校准接口。
- **GAP-P1-E**：Response template fallback 文案外置到 profile / skill 配置，去掉 [ResponseBuilder.cpp](../../cognition/src/response/ResponseBuilder.cpp) 内字面量。
- **GAP-P1-F**：BudgetContext ≥ 0.8 强制 ConvergeSafe / DirectResponse 的**显式分支**，并在审计写 `budget_pressure_decision_path` 字段。

### P2（认知质量与扩展）

- **GAP-P2-A**：Perception LLM 升级。LLM 充足档位走"LLM 意图分类 + 规则冗余校验"双路径；规则结果分歧时升级 clarification；保留规则做 ARM/factory 兜底。新增 canonical stage `perception`，需要在详设侧追加 schema 与 hint。
- **GAP-P2-B**：多候选 Plan。Planner 支持产生 N=2~3 plan candidate，Reasoner 用 budget+confidence 排序。
- **GAP-P2-C**：Reflection 多轮。对推理错误（非工具错误）开放 1 次 self-refine 循环，预算封顶。
- **GAP-P2-D**：（与 GAP-P2-B 协同）引入 plan-candidate 评估器（self-consistency vote），不破坏 ADR-007/008 边界。
- **GAP-P2-E**：Tool 候选预筛。Reasoner 收到 ToolDescriptor 列表后做 top-K（embedding/规则）过滤，下发给 Runtime 时仅给候选集合。

### P3（运营 / 演进 / 安全）

- **GAP-P3-A**：入口 PII / injection 扫描信号传入 `CognitionStepRequest.execution_hints`，cognition 显式拒绝高危输入；与 Memory ContextOrchestrator 协议对齐。
- **GAP-P3-B**：失败语料采样 + 离线人评，回喂 prompt 库与决策权重。
- **GAP-P3-C**：LLM-as-judge 自动评测主成功链回归。
- **GAP-P3-D**：多 Agent worker 内嵌 cognition 的隔离 / 延迟基准。

---

## 7. 任务拆分与规划（WP-COG-GAP-*）

任务命名规范遵循仓库现有 [docs/todos/cognition/deliverables/](../todos/cognition/deliverables/) 风格；每条任务三件套：**代码目标 / 测试目标 / 验收命令**，可直接进入 Design→Build 双轨。每条任务都配置阻塞依赖与解阻条件，避免内部死锁。

### 7.1 P0 任务（GA 前必须收敛）

#### WP-COG-GAP-001 Prompt release 状态回流（GAP-P0-A）

- **代码目标**
  - 在 [CognitionLlmBridge.cpp](../../cognition/src/llm/CognitionLlmBridge.cpp) 增加 release 状态识别：从 `LLMResponse` 的 release/eval 元字段（与 LLM 子系统对齐字段名）映射到 `ResultCode::PolicyDenied`。
  - 错误归一化中区分 `cognition.policy_denied`（prompt 退役/评估封禁）与 `cognition.llm_unavailable`（连接/超时）。
- **测试目标**
  - 新增 `CognitionLlmBridgePromptReleaseGuardTest`，覆盖 `prompt_retired / eval_blocked` 两种语义。
  - 扩展 `CognitionLlmBridgeErrorMappingTest`，断言 unavailable 与 policy_denied 不互相吞错。
- **验收命令**
  - `cmake --build build-ci --target dasall_cognition dasall_unit_tests`
  - `ctest --test-dir build-ci -R "CognitionLlmBridgePromptReleaseGuardTest|CognitionLlmBridgeErrorMappingTest" --output-on-failure`
- **阻塞 / 解阻**：已解阻。LLM 子系统的 `eval_status/release_scope` 命名已在 `PromptRelease` / `LLMResponse` 审计四元组中对齐，可直接实施。

**Closeout（2026-05-31）**

- 状态：代码切片与 focused regression 已闭合；聚合 `dasall_unit_tests` 验收仍受 repo 现有 infra baseline 阻断。
- 设计回链：
  - [docs/architecture/DASALL_llm子系统详细设计.md](../architecture/DASALL_llm子系统详细设计.md) 已冻结 `LLMResponse` 的 `prompt_id/prompt_version/eval_status/release_scope` 四元审计锚点。
  - [docs/architecture/DASALL_cognition子系统详细设计.md](../architecture/DASALL_cognition子系统详细设计.md) 已补充 `CognitionLlmBridge` 对 `prompt_retired` / `eval_blocked` 的 `PolicyDenied` 映射与验收出口。
- 代码结果：
  - `contracts/include/llm/LLMResponse.h`、`contracts/include/llm/LLMBoundaryGuards.h`、`llm/src/LLMManager.cpp` 与 `llm/src/execution/ResponseNormalizer.*` 已把 prompt release 审计元字段从 llm 归一化链路带回共享响应。
  - `cognition/src/llm/CognitionLlmBridge.cpp` 成功路径现在会把 `Deprecated` / `retired` / `blocked` 语义映射为 `ResultCode::PolicyDenied`，并保留 `prompt_retired` / `eval_blocked` diagnostics。
  - 已新增 `tests/unit/cognition/CognitionLlmBridgePromptReleaseGuardTest.cpp`，并扩展 `CognitionLlmBridgeErrorMappingTest.cpp` / `CognitionLlmBridgeProjectionTest.cpp`，防止 `policy_denied` 与 `llm_unavailable` 互相吞错。
- 验证结果：
  - `RunCtest_CMakeTools(tests=["CognitionLlmBridgePromptReleaseGuardTest","CognitionLlmBridgeErrorMappingTest"])`：通过，`100% tests passed, 0 tests failed out of 2`。
  - `cmake --build build-ci --target dasall_cognition dasall_unit_tests`：`dasall_cognition` 与本轮 touched bridge/llm test slice 构建通过，但聚合 `dasall_unit_tests` 在 repo 现有 infra baseline 处失败；复核显示 `MetricsConfigMergeTest` 在活动 build tree 同样失败，另有 `build-ci` 下 `SecretManagerLiveCompositionTest` 缺失 executable 的既有构建树问题。

#### WP-COG-GAP-002 可重放 Golden Trace（GAP-P0-B）

- **代码目标**
  - 在 cognition 内引入轻量 trace recorder（仅在 build-ci/replay profile 启用），把 `CognitionStepRequest`、桥后 LLM 投影 payload、`CognitionDecisionResult` 序列化到 `tests/data/cognition/replay/`。
  - Recorder 不入主路径，必须经 DI 注入；redaction 复用 `CognitionTelemetry` redactor。
- **测试目标**
  - 新增 `CognitionReplayRegressionTest`（unit 或 integration）：读取 golden 集，跑 `decide()` / `reflect()` / `build()`，对照预期结构化字段断言（不比 LLM 文本）。
- **验收命令**
  - `cmake --build build-ci --target dasall_unit_tests`
  - `ctest --test-dir build-ci -R "CognitionReplayRegressionTest" --output-on-failure`
- **阻塞 / 解阻**：需先固定 redaction 输出格式（已就绪）；与 GAP-P1-B 字段共享。

**Closeout（2026-05-31）**

- 状态：已完成（replay recorder、golden 数据集与 build-tree/build-ci focused regression 已闭合；聚合 `dasall_unit_tests` 验收仍受 repo 既有 runtime/memory baseline 阻断）。
- 设计回链：
  - [docs/architecture/DASALL_cognition子系统详细设计.md](../architecture/DASALL_cognition子系统详细设计.md) 已在 `CognitionTelemetry` 组件卡片补充 redaction-before-dispatch 与 owner 内部 sink seam 约束，明确 replay recorder 只能消费 request / bridge payload / result 的结构化语义字段。
  - replay golden 继续遵守 cognition 与 llm 的观测分层：不复制 raw prompt、provider-private payload 或 runtime 恢复审计事实。
- 代码结果：
  - `cognition/include/CognitionDependencies.h` 与 `cognition/src/observability/CognitionTelemetry.cpp` 已支持可选 `ICognitionTelemetrySink` 注入，并把 live sink 与自定义 sink 组合为 redaction 后的 fan-out 路径。
  - 新增 `cognition/src/observability/CognitionReplayTraceRecorder.h`，以 header-only recorder 形式在 `profile_id == build-ci/replay` 时落盘 replay trace。
  - `cognition/src/CognitionFacade.cpp` 与 `cognition/src/response/ResponseBuilder.cpp` 已发射 `replay.trace.decide.*`、`replay.trace.reflect.*`、`replay.trace.build.*` 事件，并在 bridge 成功时补齐 planning / execution payload trace。
  - 新增 `tests/unit/cognition/CognitionReplayRegressionTest.cpp`，更新 `tests/unit/cognition/CMakeLists.txt`，并在 `tests/data/cognition/replay/` 固定 11 个 golden trace 文件，覆盖 decide direct、decide planning fallback、reflect continue、build observation projection 四个场景。
  - `tests/unit/cognition/CognitionTelemetryFieldsTest.cpp` 已补 replay sink 注入回归，防止 redaction 与 fan-out 顺序回退。
- 验证结果：
  - `Build_CMakeTools(buildTargets=["dasall_cognition_replay_regression_unit_test"])`：通过。
  - `RunCtest_CMakeTools(tests=["CognitionReplayRegressionTest"])`：通过，`100% tests passed, 0 tests failed out of 1`。
  - `ctest --test-dir build-ci -R "CognitionReplayRegressionTest|CognitionTelemetryFieldsTest" --output-on-failure`：通过，`100% tests passed, 0 tests failed out of 2`。
  - `cmake --build build-ci --target dasall_unit_tests`：本轮 touched cognition slice 已完成重编译，但聚合命令仍命中 repo 既有 baseline：`RuntimeOwnerLoggingTest` / `RuntimeLoggingBridgeTest` 缺失 executable，`RuntimeCognitionLoopSmokeTest` 失败，且 memory 侧仍有 discoverability 缺口；这些红灯不由本轮 replay/golden 变更引入。
- 结果：
  - cognition 现在具备可重放 golden trace 回归，能够稳定锁定 decide / reflect / build 主链的 request、bridge payload、result 序列，不再依赖人工比对临时输出。
  - replay recorder 复用既有 telemetry redaction 与 DI seam，没有在主链引入新的 owner 越界依赖或旁路落盘逻辑。
  - `WP-COG-GAP-002` 的代码与 focused regression 已闭合；若后续需要把该任务升级为 repo-wide aggregate green gate，需先独立清理 runtime / memory 既有 baseline。

#### WP-COG-GAP-003 生产 metric live 验证 + SLO 表（GAP-P0-C）

- **代码目标**
  - 检查 `cognition_stage_latency_ms`、`cognition_stage_total`、`cognition_action_decision_total` 是否注册到 metrics provider；如缺失则在 [CognitionTelemetry.cpp](../../cognition/src/observability/CognitionTelemetry.cpp) 完成注册。
  - 在 [docs/architecture/DASALL_cognition子系统详细设计.md](../architecture/DASALL_cognition子系统详细设计.md) §6.11 追加 SLO 表：p50/p95/p99 stage_latency 阈值（按 desktop_full / edge_balanced / edge_minimal 三档）。
- **测试目标**
  - 扩展 `CognitionProductionTelemetryIntegrationTest`：断言 metrics registry 中三个 metric name 真实存在并带规范标签 `stage,result,decision_kind,profile`。
- **验收命令**
  - `ctest --test-dir build-ci -R "CognitionProductionTelemetryIntegrationTest" --output-on-failure`
- **阻塞 / 解阻**：依赖 infra metrics provider 已就位（已就绪）。

**Closeout（2026-06-01）**

- 状态：已完成（semantic metric live registration、focused telemetry integration regression 与 §6.11 SLO 文档已闭合）。
- 设计回链：
  - [docs/architecture/DASALL_cognition子系统详细设计.md](../architecture/DASALL_cognition子系统详细设计.md) §6.11 现已明确 `cognition_stage_latency_ms`、`cognition_stage_total`、`cognition_action_decision_total` 的 live metric 口径，并补齐 desktop_full / edge_balanced / edge_minimal 三档 stage_latency SLO。
  - 为保持 infra metrics schema 单一事实源，任务文案里的 `result` 维度在 provider 内继续沿用 canonical `outcome` 标签名，不再分叉出第二套 label key。
- 代码结果：
  - 更新 [infra/include/metrics/MetricTypes.h](../../infra/include/metrics/MetricTypes.h)、[infra/src/metrics/CardinalityGuard.cpp](../../infra/src/metrics/CardinalityGuard.cpp) 与 [infra/src/metrics/MetricsConfigPolicy.cpp](../../infra/src/metrics/MetricsConfigPolicy.cpp)，把 `decision_kind` 纳入 metrics allowlist、归一化与 series signature，保证 semantic metric 标签不会被 provider 丢弃。
  - 更新 [cognition/src/observability/CognitionTelemetry.h](../../cognition/src/observability/CognitionTelemetry.h) 与 [cognition/src/observability/CognitionTelemetry.cpp](../../cognition/src/observability/CognitionTelemetry.cpp)，让 telemetry sink 按 counter / histogram 注册真实 instrument，并发射 `cognition_stage_latency_ms`、`cognition_stage_total`、`cognition_action_decision_total` 三个 semantic metric。
  - 更新 [cognition/src/CognitionFacade.cpp](../../cognition/src/CognitionFacade.cpp) 与 [cognition/src/response/ResponseBuilder.cpp](../../cognition/src/response/ResponseBuilder.cpp)，在 completed / failed / degraded 发射前捕获 `latency_ms`，使 stage latency histogram 不再只有声明没有 live 样本。
  - 更新 [tests/integration/cognition/CognitionProductionTelemetryIntegrationTest.cpp](../../tests/integration/cognition/CognitionProductionTelemetryIntegrationTest.cpp)，断言 metrics registry 真实注册三个 metric name，并验证 `stage/profile/result(outcome)/decision_kind` 标签在 success、failure、degraded 三条路径上可见。
- 验证结果：
  - `Build_CMakeTools(buildTargets=["dasall_cognition_production_telemetry_integration_test"])`：通过。
  - `RunCtest_CMakeTools(tests=["CognitionProductionTelemetryIntegrationTest"])`：通过；`100% tests passed, 0 tests failed out of 1`。
- 结果：
  - cognition 的 live metrics provider 现在会真实持有 `cognition_stage_latency_ms`、`cognition_stage_total`、`cognition_action_decision_total`，不再只停留在详细设计表格和 generic event counter。
  - `WP-COG-GAP-003` 已为后续 token / cost / finish_reason 归因提供稳定指标底座；新增 SLO 阈值也与现有 profile timeout/degrade 预算保持一致，没有引入第二套 owner 口径。

#### WP-COG-GAP-004 cognition 嵌入式 / qemu gate（GAP-P0-D）

- **代码目标**
  - 新增 `scripts/packaging/run_local_qemu_cognition_gate.sh`（参考 access/infra 现有脚本），在 ARM qemu 镜像里跑 edge_minimal profile 下 cognition 的 decide+build smoke。
  - CI 增加 `cognition_qemu_or_arm_smoke` task。
- **测试目标**
  - 新增 `CognitionEdgeMinimalSmokeTest`（installed 形态），断言三入口在 ARM 镜像内可达预期 ResultCode。
- **验收命令**
  - `sh scripts/packaging/run_local_qemu_cognition_gate.sh`
- **阻塞 / 解阻**：依赖 packaging / qemu 镜像基线（已就绪），需要复用 RTSUP-FIX-005 的 qemu 基础设施。

#### WP-COG-GAP-005 InputBoundaryValidatorTest 独立化（GAP-P0-E）

- **代码目标**：无（仅测试）。
- **测试目标**
  - 在 [tests/unit/cognition/](../../tests/unit/cognition/) 新增 `InputBoundaryValidatorTest.cpp`，覆盖缺 GoalContract / ContextPacket / BeliefState 三类场景。
  - 在 [tests/unit/cognition/CMakeLists.txt](../../tests/unit/cognition/CMakeLists.txt) 注册。
- **验收命令**
  - `ctest --test-dir build-ci -R "InputBoundaryValidatorTest" --output-on-failure`
- **阻塞 / 解阻**：无。

### 7.2 P1 任务（生产稳定性）

#### WP-COG-GAP-006 reflection.v1 / response.v1 schema 冻结（GAP-P1-A）

- **代码目标**
  - 在 [StageSchemaRegistry.cpp](../../cognition/src/validation/StageSchemaRegistry.cpp) 追加 `kReflectionSchema`（schema_version `cognition.reflection.v1`）与 `kResponseSchema`（`cognition.response.v1`）。
  - Facade reflection / response 路径切到结构化校验，replace diagnostics 字符串匹配。
- **测试目标**
  - 新增 `StageOutputValidatorReflectionInvariantTest`、`StageOutputValidatorResponseEnvelopeTest` 扩展。
  - 集成测：`CognitionReflectionStructuredOutputIntegrationTest`。
- **验收命令**
  - `ctest --test-dir build-ci -R "StageOutputValidatorReflection|CognitionReflectionStructured" --output-on-failure`
- **阻塞 / 解阻**：依赖 reflection 桥已存在（已就绪）。

#### WP-COG-GAP-007 Token / cost / finish_reason 注入 telemetry（GAP-P1-B）

- **代码目标**
  - 在 `StageLlmCallResult` 内补 `prompt_tokens / completion_tokens / total_cost / finish_reason`（来自 LLM 子系统 normalized response）。
  - `CognitionTelemetry.emit_stage_completed` 增加 token/cost 字段；redaction 不剥离这类聚合数值。
- **测试目标**
  - 扩展 `CognitionTelemetryFieldsTest`：断言 token/cost/finish_reason 字段存在；redaction 不影响。
- **验收命令**
  - `ctest --test-dir build-ci -R "CognitionTelemetryFieldsTest" --output-on-failure`
- **阻塞 / 解阻**：依赖 LLM 子系统暴露 normalized usage 字段。

#### WP-COG-GAP-008 deadline 触发时 LLM cancel 通道（GAP-P1-C）

- **代码目标**
  - 在 [CognitionLlmBridge](../../cognition/src/llm/CognitionLlmBridge.h) 暴露 `abandon_call(call_id)`，由 LLM 子系统提供底层实现。
  - [CognitionFacade.cpp](../../cognition/src/CognitionFacade.cpp) `run_stage_with_deadline` 超时分支调用一次 abandon。
- **测试目标**
  - 新增 `CognitionFacadeDeadlineCancelPropagationTest`：mock LLM 验证 abandon_call 被调用且不阻塞。
- **验收命令**
  - `ctest --test-dir build-ci -R "CognitionFacadeDeadlineCancelPropagationTest" --output-on-failure`
- **阻塞 / 解阻**：依赖 LLM 子系统接口扩展（先冻结 LLM 侧 API）。

#### WP-COG-GAP-009 Reasoner 权重外置（GAP-P1-D）

- **代码目标**
  - 在 [CognitionConfig.h](../../cognition/include/CognitionConfig.h) 增加 `reasoner.candidate_weights`（DirectResponse / ToolCall / Clarification / ConvergeSafe 等条目）。
  - [Reasoner.cpp](../../cognition/src/reasoning/Reasoner.cpp) 读取配置；profile 投影补默认值表。
- **测试目标**
  - 新增 `ReasonerCandidateWeightProjectionTest`，断言 profile 差异生效；同输入不同 weight 得不同决策。
- **验收命令**
  - `ctest --test-dir build-ci -R "ReasonerCandidateWeight" --output-on-failure`
- **阻塞 / 解阻**：无。

#### WP-COG-GAP-010 Response template 文案外置（GAP-P1-E）

- **代码目标**
  - 在 `CognitionConfig.response.templates` 引入 `clarification / safe_converge / fallback_failure` 三类模板槽；profile 投影提供默认值。
  - [ResponseBuilder.cpp](../../cognition/src/response/ResponseBuilder.cpp) 移除字面量。
- **测试目标**
  - 扩展 `ResponseBuilderTemplateFallbackTest`，覆盖配置覆盖与 profile 差异。
- **验收命令**
  - `ctest --test-dir build-ci -R "ResponseBuilderTemplateFallbackTest" --output-on-failure`
- **阻塞 / 解阻**：无。

#### WP-COG-GAP-011 BudgetContext ≥0.8 显式分支（GAP-P1-F）

- **代码目标**
  - 在 [Reasoner.cpp](../../cognition/src/reasoning/Reasoner.cpp) 增加显式分支：`budget_utilization >= 0.8` 时优先 DirectResponse / ConvergeSafe，并在 ActionDecision.diagnostics 写 `budget_pressure_decision_path`。
- **测试目标**
  - 扩展 `BudgetAwareDecisionTest`：断言 ≥0.8 必走显式分支；diagnostics 含字段。
- **验收命令**
  - `ctest --test-dir build-ci -R "BudgetAwareDecisionTest" --output-on-failure`
- **阻塞 / 解阻**：无。

### 7.3 P2 任务（认知质量与扩展）

#### WP-COG-GAP-012 Perception LLM 升级（GAP-P2-A）

- **代码目标**
  - 在详设追加 canonical stage `perception`（schema `cognition.perception.v1`）。
  - PerceptionEngine 走"LLM 意图分类 + 规则冗余校验"双路径；规则结果与 LLM 分歧 → AskClarification。
  - StagePolicyResolver 暴露 `perception.llm_enabled`，profile 投影提供默认。
- **测试目标**
  - `PerceptionLlmDualPathTest`、`StageOutputValidatorPerceptionInvariantTest`、`CognitionPerceptionStructuredOutputIntegrationTest`。
- **验收命令**
  - `ctest --test-dir build-ci -R "PerceptionLlm|PerceptionInvariant|CognitionPerceptionStructured" --output-on-failure`
- **阻塞 / 解阻**：依赖 LLM 子系统支持新 stage canonical key。

#### WP-COG-GAP-013 Planner 多候选 + 候选评估（GAP-P2-B / GAP-P2-D）

- **代码目标**
  - Planner 支持 N=2~3 plan candidate；新增 `PlanCandidateRanker`，按 budget+confidence 排序输出主候选 + 备选。
  - 不进入共享 contracts；ranker 在 cognition/include 内。
- **测试目标**
  - `PlannerMultiCandidateRankingTest`、`PlannerNodeBudgetTest` 扩展（候选数与节点上限交互）。
- **验收命令**
  - `ctest --test-dir build-ci -R "PlannerMultiCandidate|PlannerNodeBudgetTest" --output-on-failure`
- **阻塞 / 解阻**：与 GAP-P0-B 共享 replay 集做收敛评估。

#### WP-COG-GAP-014 Reflection 多轮 self-refine（GAP-P2-C）

- **代码目标**
  - 在 [CognitionFacade.cpp](../../cognition/src/CognitionFacade.cpp) reflection 路径，对推理错误（非工具错误）允许 1 轮 self-refine（封顶预算）；产出最终 `ReflectionDecision`。
  - 必须保持 ADR-007 边界：reflect 仍是 suggestion-only，不直接执行 retry。
- **测试目标**
  - `ReflectionSelfRefineSingleRoundTest`、`ReflectionSelfRefineBudgetCapTest`。
- **验收命令**
  - `ctest --test-dir build-ci -R "ReflectionSelfRefine" --output-on-failure`
- **阻塞 / 解阻**：依赖 GAP-P1-A reflection schema 冻结。

#### WP-COG-GAP-015 Tool 候选预筛（GAP-P2-E）

- **代码目标**
  - 在 Reasoner 接收 `ToolDescriptor` 列表后做 top-K 过滤（先规则：name/intent 匹配；后续可换 embedding）。
  - 输出 `tool_intent_hint` 仅指向候选 top-K。
- **测试目标**
  - `ReasonerToolCandidateFilteringTest`：断言大工具空间下输出收敛到 K。
- **验收命令**
  - `ctest --test-dir build-ci -R "ReasonerToolCandidateFilteringTest" --output-on-failure`
- **阻塞 / 解阻**：依赖 ToolDescriptor 接入路径（Runtime 提供）。

### 7.4 P3 任务（运营/演进/安全）

#### WP-COG-GAP-016 入口 PII / injection 扫描信号（GAP-P3-A）

- **代码目标**
  - 在 `CognitionStepRequest.execution_hints` 增加 `input_safety_signal`（来自 access / memory ContextOrchestrator）。
  - InputBoundaryValidator 拒绝 `injection_detected=true`，返回 `cognition.policy_denied`。
- **测试目标**：扩展 `InputBoundaryValidatorTest`。
- **验收命令**：`ctest -R "InputBoundaryValidatorTest"`
- **阻塞 / 解阻**：依赖 access / memory 提供入口扫描签名。

#### WP-COG-GAP-017 失败语料采样（GAP-P3-B）

- **代码目标**：复用 GAP-P0-B replay recorder，对 `cognition.schema_violation / reflection.abort_safe / response.fallback_used` 增加采样开关与采样率。
- **测试目标**：`CognitionFailureSamplingTest`。
- **验收命令**：`ctest -R "CognitionFailureSampling"`
- **阻塞 / 解阻**：依赖 GAP-P0-B。

#### WP-COG-GAP-018 LLM-as-judge 主链回归（GAP-P3-C）

- **代码目标**：CI 离线作业，不入主路径；判官 prompt 由 LLM 子系统 release 治理。
- **测试目标**：定期作业脚本 + 报告归档。
- **阻塞 / 解阻**：依赖 GAP-P0-A、GAP-P0-B、GAP-P3-B。

#### WP-COG-GAP-019 多 Agent worker cognition 基准（GAP-P3-D）

- **代码目标**：与 multi_agent 子系统协作，cognition 提供 worker 内嵌时的 reentrancy 与隔离基线测试。
- **测试目标**：`MultiAgentCognitionWorkerIsolationBenchmark`。
- **阻塞 / 解阻**：依赖 multi_agent 子系统就绪。

---

## 8. 排序与依赖图

```mermaid
flowchart LR
  GAP_P0_A[WP-COG-GAP-001 prompt release] --> GAP_P3_C[WP-COG-GAP-018 judge]
  GAP_P0_B[WP-COG-GAP-002 replay] --> GAP_P2_B[WP-COG-GAP-013 multi-candidate]
  GAP_P0_B --> GAP_P3_B[WP-COG-GAP-017 sampling]
  GAP_P3_B --> GAP_P3_C
  GAP_P0_C[WP-COG-GAP-003 metrics SLO] --> GAP_P1_B[WP-COG-GAP-007 token cost]
  GAP_P0_D[WP-COG-GAP-004 qemu gate]
  GAP_P0_E[WP-COG-GAP-005 input validator test] --> GAP_P3_A[WP-COG-GAP-016 input safety]
  GAP_P1_A[WP-COG-GAP-006 reflection/response schema] --> GAP_P2_C[WP-COG-GAP-014 self-refine]
  GAP_P1_C[WP-COG-GAP-008 cancel] --> GAP_P1_B
  GAP_P1_D[WP-COG-GAP-009 weights]
  GAP_P1_E[WP-COG-GAP-010 templates]
  GAP_P1_F[WP-COG-GAP-011 budget branch]
  GAP_P2_A[WP-COG-GAP-012 perception llm]
  GAP_P2_E[WP-COG-GAP-015 tool prefilter]
  GAP_P3_D[WP-COG-GAP-019 multi-agent worker]
```

执行建议：
1. 先并发开 GAP-P0-A / GAP-P0-C / GAP-P0-D / GAP-P0-E（低耦合）。
2. GAP-P0-B 落地后，启动 GAP-P2-B、GAP-P3-B。
3. P1 中 GAP-P1-A 优先（后续 self-refine 依赖 schema），GAP-P1-B/C 配套发布 SLO。
4. P2 / P3 按容量逐步引入。

---

## 9. 验收门 / 收敛判据

| 阶段 | 通过条件 |
|---|---|
| **GA-Cog-Gate-P0** | GAP-P0-001..005 全部 Done；`ctest -R "Cognition\|InputBoundaryValidatorTest"` 全绿；qemu gate 在 CI 上有一次绿色记录 |
| **GA-Cog-Gate-P1** | GAP-P1-006..011 全部 Done；SLO 表 published；deadline cancel 路径有 e2e 证据 |
| **GA-Cog-Gate-P2** | GAP-P2-012..015 全部 Done；replay 集合在 multi-candidate / self-refine 下回归绿色 |
| **GA-Cog-Gate-P3** | GAP-P3-016..019 至少完成 P3-A 与 P3-B；P3-C/D 进入观测阶段 |

---

## 10. 文档回写约定

1. 每完成一项 WP-COG-GAP-* 任务，在 [docs/worklog/DASALL_开发执行记录.md](../worklog/DASALL_开发执行记录.md) 与本文件 §7 对应任务后追加 closeout 段落（含证据链接、测试命令输出摘要）。
2. 任何对详设的字段/schema 改动，必须先在 [docs/architecture/DASALL_cognition子系统详细设计.md](../architecture/DASALL_cognition子系统详细设计.md) 落档再写实现。
3. 阻塞项必须在本文件 §7 各任务"阻塞/解阻"小节内显式记录，禁止在代码 comment 内绕过。
