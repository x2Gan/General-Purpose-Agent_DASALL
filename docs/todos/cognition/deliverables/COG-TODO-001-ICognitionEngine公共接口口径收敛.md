# COG-TODO-001 ICognitionEngine 公共接口口径收敛

状态：Done  
日期：2026-04-24  
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md  
任务类型：前置补设计 / 评审门禁  

## 1. 本地证据

1. `docs/architecture/DASALL_Agent_architecture.md` §5.8.4 原先给出 `ICognitionEngine::step()` 与 `CognitionStepResult` 草图，混合了承接动作、反思和错误的单入口口径。
2. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.6.1 已明确把 Runtime-facing cognition 接口拆为 `decide()`、`reflect()` 与终态 `build_response` 路径。
3. `docs/architecture/DASALL_Engineering_Blueprint.md` §3.4 只描述 cognition 五段职责与边界，没有给出可执行接口名，本轮需要显式回链详设口径，避免后续 fixture 继续引用旧草图。
4. 专项 TODO 中 COG-BLK-001 指向同一冲突：若不先统一入口，COG-TODO-007/010/023 会在请求结果对象、公共接口和 facade 处反复返工。

## 2. 外部参考

1. Pact 文档说明 consumer/provider contract 由 consumer-side interaction 与 provider verification 成对约束，启发本任务把 Runtime 作为 consumer 的调用面先冻结，再允许 cognition 作为 provider 落盘实现：https://docs.pact.io/getting_started/how_pact_works
2. OpenTelemetry semantic convention 文档强调跨代码库采用统一命名方案的价值，启发本任务把 `step()`、`decide()`、`reflect()`、`build_response` 的语义边界一次性收敛，避免同一概念在不同文档中多名漂移：https://opentelemetry.io/docs/concepts/semantic-conventions/

## 3. 主结论

1. Runtime 对 cognition 的可执行公共入口为三条语义面：
   - `ICognitionEngine::decide(const CognitionStepRequest&) -> CognitionDecisionResult`
   - `ICognitionEngine::reflect(const ReflectionRequest&) -> CognitionReflectionResult`
   - `IResponseBuilder::build(const ResponseBuildRequest&) -> ResponseBuildResult`
2. `build_response` 是终态构造路径名，不作为 `ICognitionEngine` 的第三个方法名落盘；具体接口保持 `IResponseBuilder::build()`，以隔离“下一步动作决策”和“最终用户输出构造”。
3. `CognitionStepRequest` 只服务 `decide()`，不得再与 `ReflectionDecision`、`AgentResult` 或 publish 行为绑定。
4. `CognitionStepResult` 不再作为 Build-ready 类型；后续对象任务应落盘 `CognitionDecisionResult` 与 `CognitionReflectionResult`。
5. Runtime 的 `IRuntimeEngine::step(const RuntimeEvent&)` 属于 runtime 内部 FSM 入口，不受本任务变更影响；验收检索中出现该名称不构成 cognition 接口冲突。

## 4. 边界与职责

| 对象 | 职责 | 非职责 |
|---|---|---|
| `ICognitionEngine::decide()` | 从 Goal、Context、Belief、Observation 和 hints 生成下一步 `ActionDecision` 与可选 `BeliefUpdateHint` | 不执行工具、不提交结果、不做恢复裁定 |
| `ICognitionEngine::reflect()` | 基于外部 Observation/ErrorInfo 输出 suggestion-only `ReflectionDecision` 与可选 `BeliefUpdateHint` | 不持有 retry counter、不执行 retry/replan、不访问 checkpoint |
| `IResponseBuilder::build()` | 在 Runtime 判定终态后构造 `AgentResult` 或显式错误/降级结果 | 不 publish 用户通道、不重新驱动执行链、不承担 streaming v1 |

## 5. 数据 / 接口说明

| 数据对象 | 所属入口 | 口径 |
|---|---|---|
| `CognitionStepRequest` | `decide()` | 必须含 Goal / Context / Belief，latest Observation 可选；`execution_hints` 只表达阶段策略输入 |
| `CognitionDecisionResult` | `decide()` | 承接 `ActionDecision`、`BeliefUpdateHint`、`ErrorInfo`，不含 `ReflectionDecision` 或 `AgentResult` |
| `ReflectionRequest` | `reflect()` | 必须含最新 Observation，可选 active PlanGraph；由 Runtime 保证触发时机 |
| `CognitionReflectionResult` | `reflect()` | 只返回 shared `ReflectionDecision` 建议和可选 belief hint |
| `ResponseBuildRequest` | `build()` | 终态构造输入，含 terminal decision / build hints |
| `ResponseBuildResult` | `build()` | 返回 `AgentResult` 或显式 `ErrorInfo`，由 Runtime 决定提交 / 最小失败结果 |

## 6. 流程 / 时序

1. Runtime 组装 `ContextPacket` 与 `BeliefState` 后调用 `ICognitionEngine::decide()`。
2. cognition 返回 `ActionDecision` 后，Runtime 根据 FSM、Tool/Service 治理和预算策略执行或转终态。
3. 外部执行产生 Observation 或 ErrorInfo 后，Runtime 决定是否调用 `ICognitionEngine::reflect()`。
4. Runtime 判定进入终态输出路径后调用 `IResponseBuilder::build()`，再由 Runtime / AccessGateway 负责提交。

## 7. D 原子项完成情况

| 原子项 | 目标 | 结果 |
|---|---|---|
| D1 | 识别 `step()` 与三入口冲突来源 | PASS：冲突限定在架构草图与详设口径之间 |
| D2 | 冻结 Runtime consumer 可执行调用面 | PASS：三入口消费方式已定稿 |
| D3 | 明确不进入 shared contracts 的边界 | PASS：全部对象保持 cognition module-public |

## 8. Design -> Build 映射

| 设计结论 | 后续 Build 任务 | 验收点 |
|---|---|---|
| `step()` 退出 cognition 可执行口径 | COG-TODO-007、010、023 | 头文件与 facade 不生成 `ICognitionEngine::step()` |
| `CognitionStepRequest` 仅服务 `decide()` | COG-TODO-007 | Interface surface test 验证 request/result 字段不混入反思和终态职责 |
| `IResponseBuilder::build()` 承接终态构造 | COG-TODO-010、019、023 | ResponseBuilder 单测验证 AgentResult 映射与模板降级 |
| Runtime 仍可保留自身 FSM `step()` | COG-TODO-003、026 | runtime fixture 消费 `ActionDecision`，不把 cognition 恢复成单入口 |

## 9. Build 三件套与验收

代码目标：更新架构、蓝图、认知详设、专项 TODO 与本交付物，不新增生产代码。  
测试目标：文档一致性检索，确认 cognition 公共入口不再同时保留冲突 `step()` 与三入口。  
验收命令：

```bash
rg -n "ICognitionEngine|step\(|decide\(|reflect\(|build\(" docs/architecture/DASALL_Agent_architecture.md docs/architecture/DASALL_Engineering_Blueprint.md docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md
```

验收结论：PASS。检索仍会命中 runtime 自身 `IRuntimeEngine::step()` 和本任务历史说明，但 cognition 可执行接口已统一为 `decide()` / `reflect()` / `IResponseBuilder::build()`，不再存在 `ICognitionEngine::step()` 的 Build-ready 描述。

## 10. D Gate 与合规复核

| 检查项 | 结论 |
|---|---|
| D 文档已落盘 | PASS |
| Design->Build 映射完整 | PASS |
| Build 三件套已锁定 | PASS |
| 代码注释要求 | 不适用，本轮为文档门禁 |
| 正负例覆盖 | 正例：三入口命中；负例：`ICognitionEngine::step()` 不再作为接口签名出现 |
| TODO / 交付物 / worklog 可追溯 | PASS |
| COG-BLK-001 | 已由本任务解阻 |
