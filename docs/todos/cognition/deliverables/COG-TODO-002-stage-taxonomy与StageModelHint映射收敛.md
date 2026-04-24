# COG-TODO-002 stage taxonomy 与 StageModelHint 映射收敛

状态：Done  
日期：2026-04-24  
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md  
任务类型：前置补设计 / 评审门禁  

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.14.2 原先把 `StageModelHint.stage_name` 标注为 `perception/planning/reasoning/reflection/response`，与 llm 侧真实 stage key 不完全一致。
2. `docs/architecture/DASALL_llm子系统详细设计.md` 的 ModelRouter / PromptRegistry / PromptQuery 以 `stage` 作为第一选择维度，当前公开设计已强调 `stage_routes` 与 `LLMGenerateRequest.stage` 的一致性。
3. `docs/todos/llm/deliverables/LLM-TODO-035-profile-diff-integration设计收敛.md` 明确记录了 `planner/responder` profile key 与真实 `planning/response` llm stage key 的接缝，不允许在 035 的 integration 测试里偷偷归一化。
4. `profiles/include/RuntimePolicySnapshot.h` 已冻结 `ModelProfile.stage_routes` 为 `std::map<std::string, ModelRoutePolicy>`，因此 stage key 必须在设计层收敛，否则 projector / bridge / profile gate 会各自发明映射。

## 2. 外部参考

1. OpenTelemetry semantic conventions 将跨组件语义命名统一为可复用 convention，启发本任务把 stage key 固定为单一 canonical vocabulary，而不是让每个测试或 bridge 自行翻译：https://opentelemetry.io/docs/concepts/semantic-conventions/
2. Pact 文档强调 provider verification 应按 consumer contract 执行，启发本任务把 cognition 作为 llm consumer 时的 stage key contract 先冻结，再由后续 bridge/profile gate 验证：https://docs.pact.io/getting_started/how_pact_works

## 3. 主结论

1. canonical llm stage key 只有四个：`planning`、`execution`、`reflection`、`response`。
2. `StageModelHint.stage_name` 必须直接使用 canonical llm stage key，不得写 `perception`、`reasoning`、`planner` 或 `responder`。
3. `StageModelHint.task_type` 用于表达 cognition 组件语义，例如 `perception`、`plan`、`action_decision`、`failure_analysis`、`final_response`。
4. `RuntimePolicySnapshot.model_profile.stage_routes` 的投影视图必须使用 canonical key；旧 profile YAML 若仍存在 `planner/responder`，只能在 profile provider / projector 边界归一化，不能由 cognition bridge、profile compatibility test 或 fixture 私有转换。
5. `perception` 和 `reasoning` 仍是 cognition 内部组件名与 telemetry 组件标签，不等于 llm route key。

## 4. 映射表

| cognition 组件 | canonical `stage_name` / `stage_routes` key | `task_type` 建议 | schema / 输出语义 | 说明 |
|---|---|---|---|---|
| PerceptionEngine | `planning` | `perception` | `perception_result` | 感知阶段使用 planning route 的轻量分类/抽取能力 |
| Planner | `planning` | `plan` / `replan` | `plan_graph` | 计划与重规划共用 planning route，通过 task_type 区分 |
| Reasoner | `execution` | `action_decision` | `action_decision` | execution 是 llm route 维度，不表示 cognition 执行工具 |
| ReflectionEngine | `reflection` | `failure_analysis` / `replan_advice` | `reflection_decision` | 可请求 reasoning-heavy tier，但 provider-private trace 不外泄 |
| ResponseBuilder | `response` | `final_response` | `response_envelope` | 终态构造 route，允许模板降级 |

## 5. 边界与职责

| Owner | 职责 | 禁止事项 |
|---|---|---|
| CognitionConfigProjector / StagePolicyResolver | 从 `RuntimePolicySnapshot` 读取 canonical stage route，生成 `StageModelHint` | 不维护第二套 profile stage key |
| CognitionLlmBridge | 透传 `stage_name`，把 `task_type`、schema、budget、redaction hint 投影到 llm 请求 | 不做 `perception -> planning` 等私有映射 |
| Profile provider / projector | 如需兼容旧 profile-source alias，在进入 `RuntimePolicySnapshot` 前统一归一化 | 不把 legacy alias 泄漏到 bridge 或测试 |
| Tests / fixture | 只断言 canonical key 与映射表结果 | 不在测试局部 hardcode 替代映射 |

## 6. D 原子项完成情况

| 原子项 | 目标 | 结果 |
|---|---|---|
| D1 | 找出 cognition / llm / profile stage key 漂移点 | PASS：漂移点为 `perception/reasoning` 与 `planner/responder` |
| D2 | 冻结 canonical stage key 集合 | PASS：`planning/execution/reflection/response` |
| D3 | 给出 StageModelHint 映射表与 owner 边界 | PASS：映射表与 owner 表已落盘 |

## 7. Design -> Build 映射

| 设计结论 | 后续 Build 任务 | 验收点 |
|---|---|---|
| `StageModelHint.stage_name` 使用 canonical key | COG-TODO-009、011、012、020 | `StageModelHintProjectionTest` 不接受 `perception/reasoning/planner/responder` 作为 stage key |
| cognition 组件语义进入 `task_type` / schema_kind | COG-TODO-020、021 | bridge 与 validator 可区分 perception schema 和 planning route |
| profile alias 归一化 owner 在 provider/projector 边界 | COG-TODO-011、029 | profile compatibility test 不在 fixture 内自建 stage mapping |
| `execution` route 不等于工具执行权 | COG-TODO-003、016、027 | runtime 仍通过 FSM 消费 ActionDecision，cognition 不直接执行 |

## 8. Build 三件套与验收

代码目标：更新 cognition 详设、llm 详设、LLM-TODO-035 追认说明、cognition 专项 TODO 与本交付物，不新增生产代码。  
测试目标：文档一致性检索，确认 canonical stage key、StageModelHint、stage_routes 与 legacy alias 边界可检索。  
验收命令：

```bash
rg -n "planning|execution|reflection|response|perception|reasoning|StageModelHint|stage_routes" docs/architecture/DASALL_cognition子系统详细设计.md docs/architecture/DASALL_llm子系统详细设计.md docs/todos/llm/deliverables/LLM-TODO-035-profile-diff-integration设计收敛.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md
```

验收结论：PASS。检索可定位 canonical key 集合、映射表、legacy alias 禁入边界与下游 Build 映射。

## 9. D Gate 与合规复核

| 检查项 | 结论 |
|---|---|
| D 文档已落盘 | PASS |
| Design->Build 映射完整 | PASS |
| Build 三件套已锁定 | PASS |
| 代码注释要求 | 不适用，本轮为文档门禁 |
| 正负例覆盖 | 正例：四个 canonical key；负例：`perception/reasoning/planner/responder` 不得作为投影后 stage key |
| TODO / 交付物 / worklog 可追溯 | PASS |
| COG-BLK-002 | 已由本任务解阻 |
