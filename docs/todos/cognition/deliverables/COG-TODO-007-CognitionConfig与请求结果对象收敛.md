# COG-TODO-007 CognitionConfig 与请求结果对象收敛

状态：Done
日期：2026-04-26
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready 对象 / module-public 类型冻结

## 1. 本地证据

1. COG-TODO-001 已把 cognition Runtime-facing 入口统一为 `ICognitionEngine::decide()`、`ICognitionEngine::reflect()` 与 `IResponseBuilder::build()`，并明确 `CognitionStepRequest` 不再承载反思结果或终态回复结果。
2. COG-TODO-005 已建立 `cognition/include/`、`dasall_cognition` public header file set 与真实 `src/CognitionFacade.cpp`，COG-TODO-006 已接线 `CognitionInterfaceSurfaceTest`。
3. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.6.2 要求落盘 `CognitionStepRequest`、`CognitionDecisionResult`、`ReflectionRequest`、`CognitionReflectionResult`、`ResponseBuildRequest`、`ResponseBuildResult`。
4. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.10 固定 `CognitionConfig` 默认项：enabled、plan 节点/深度、clarification/direct/replan 阈值、rule/template fallback、delegate hint 与 observability redaction。
5. `docs/architecture/DASALL_cognition子系统详细设计.md` §8.1 给出目录建议：`CognitionConfig.h`、`CognitionTypes.h`、`perception/PerceptionResult.h`、`response/ResponseBuildRequest.h`、`response/ResponseBuildResult.h`。
6. `docs/architecture/DASALL_cognition子系统详细设计.md` §14.1 COG-OQ01 采纳 `CognitionStepRequest` 必须显式携带完整 `BeliefState`，避免下游组件对可空 belief 做防御式分叉。

## 2. 外部参考

1. Protocol Buffers 官方文档的 message evolution 规则说明，已投入使用的消息需要保持字段身份稳定；新增字段通常安全，但重编号或复用删除字段会破坏兼容性：https://protobuf.dev/programming-guides/proto2/#updating

本轮借鉴点：公共对象先冻结职责边界和字段身份；后续新增 PlanGraph / ActionDecision 细节时走显式对象任务，不在 COG-TODO-007 中把未完成对象偷渡进共享契约。

## 3. 主结论

1. `CognitionConfig` 只表达 cognition 内部可读运行参数和 profile 投影结果，不携带平台宏、具体 provider 私有参数、工具执行策略或 runtime FSM 控制字段。
2. `CognitionStepRequest` 是 `decide()` 的唯一请求对象，必须携带 `GoalContract`、`ContextPacket`、`BeliefState`、可选 `Observation`、预算上下文和阶段执行 hints。
3. `CognitionDecisionResult` 只返回可选 `ActionDecision`、可选 `BeliefUpdateHint`、可选 `ErrorInfo`、可选 `ResultCode` 与 `ContextSufficiencySignal`，不得包含 `AgentResult`、`ReflectionDecision` 或工具执行请求。
4. `ReflectionRequest` 服务 `reflect()`，携带失败观察、Goal/Context/Belief 和阶段 hints；在 COG-TODO-008 之前不嵌入完整 `PlanGraph`，仅保留 `active_plan_ref` 作为后续接线锚点。
5. `CognitionReflectionResult` 返回 shared `ReflectionDecision` 建议和 belief hint；恢复准入、retry/backoff、checkpoint 仍属于 Runtime/RecoveryManager。
6. `ResponseBuildRequest` 与 `ResponseBuildResult` 移入 `response/` 头文件，仍在 `dasall::cognition` 命名空间暴露，以保持 COG-TODO-001 冻结的接口签名。
7. `PerceptionResult` 落在 `perception/` 头文件并保持 module-local 阶段对象，不进入 `contracts/`。

## 4. 边界与职责

| 对象 | 职责 | 非职责 |
|---|---|---|
| `CognitionConfig` | 表达 cognition 阈值、阶段规模、降级开关和观测裁剪默认值 | 不解析 profile 原始 yaml；不存 provider payload；不决定 Runtime 恢复 |
| `StageExecutionHints` | 调用级语义偏好：低延迟、允许降级、风险提示 | 不包含 deadline/backoff/retry counter/FSM state |
| `CognitionStepRequest` | Runtime 到 cognition 决策入口的语义输入包 | 不包含 `ToolRequest`、`AgentResult`、publish channel |
| `CognitionDecisionResult` | 决策结果和上下文充分性信号 | 不包含 `ReflectionDecision`、`RecoveryRequest`、执行结果 |
| `ReflectionRequest` | 反思入口输入，围绕 latest Observation 做失败语义分析 | 不携带恢复准入、checkpoint blob、retry 调度 |
| `CognitionReflectionResult` | 反思建议和 belief 写回 hint | 不执行恢复，不改变 Runtime 状态 |
| `ResponseBuildRequest` | 终态输出构造输入 | 不决定何时终态，不提交用户通道 |
| `ResponseBuildResult` | AgentResult 或显式错误出口 | 不 publish，不隐式吞掉失败 |
| `PerceptionResult` | 感知阶段结构化结果 | 不生成 PlanGraph，不选择 ActionDecision |

## 5. 数据 / 接口说明

| 类型 | 字段冻结 |
|---|---|
| `CognitionConfig` | `enabled=true`、`max_plan_nodes=8`、`max_plan_depth=4`、`thresholds.ask_clarification=0.45`、`thresholds.direct_response=0.70`、`thresholds.replan_hint=0.50`、`perception.rule_fallback_enabled=true`、`response.template_fallback_enabled=true`、`reasoner.allow_delegate_hint=false`、`observability.emit_stage_spans=true`、`observability.redact_context_payload=true` |
| `CognitionStepRequest` | `caller_domain`、`request_id`、`trace_id`、`profile_id`、`goal_contract`、`context_packet`、`belief_state`、`latest_observation`、`budget_context`、`execution_hints` |
| `CognitionDecisionResult` | `result_code`、`action_decision`、`belief_update_hint`、`error_info`、`context_sufficiency`、`diagnostics` |
| `ReflectionRequest` | `caller_domain`、`request_id`、`trace_id`、`profile_id`、`goal_contract`、`context_packet`、`belief_state`、`latest_observation`、`active_plan_ref`、`execution_hints` |
| `CognitionReflectionResult` | `result_code`、`reflection_decision`、`belief_update_hint`、`error_info`、`diagnostics` |
| `ResponseBuildRequest` | `caller_domain`、`request_id`、`trace_id`、`profile_id`、`goal_contract`、`context_packet`、`belief_state`、`latest_observation`、`terminal_decision`、`build_hints` |
| `ResponseBuildResult` | `result_code`、`agent_result`、`error_info`、`fallback_used`、`diagnostics` |
| `PerceptionResult` | `intent_summary`、`task_type`、`entities`、`constraints_digest`、`ambiguities`、`clarification_questions`、`confidence`、`requires_clarification`、`diagnostics` |

## 6. 流程 / 时序

1. Runtime 构造 `CognitionStepRequest`，注入 caller、request、trace、profile、Goal/Context/Belief、latest Observation 与预算上下文。
2. CognitionFacade 使用 `CognitionConfig` 与 `StageExecutionHints` 解析阶段策略，然后把 `CognitionDecisionResult` 返回 Runtime。
3. Runtime 若遇到外部 Observation 失败或需要反思，构造 `ReflectionRequest` 调用 `reflect()`。
4. Runtime 判定进入终态后构造 `ResponseBuildRequest`，由 `IResponseBuilder::build()` 生成 `ResponseBuildResult`。
5. Runtime 负责提交或最小失败结果兜底；cognition 不越权 publish 或触发恢复执行。

## 7. D 原子项完成情况

| 原子项 | 目标 | 结果 |
|---|---|---|
| D1 | 校验 COG-TODO-001/005/006 与 COG-BLK-001 状态 | PASS：前置任务 Done，COG-BLK-001 已解阻 |
| D2 | 锁定对象字段与职责边界 | PASS：见 §4/§5 |
| D3 | 锁定目录与文件范围 | PASS：`CognitionConfig.h`、`CognitionTypes.h`、`response/*`、`perception/*`、surface test、直接消费者适配 |
| D4 | 锁定 Build 三件套 | PASS：见 §9 |

## 8. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| 配置字段必须来自 §6.10 默认策略 | `cognition/include/CognitionConfig.h` | surface test 断言默认值 |
| 请求 / 结果对象保持 module-public | `cognition/include/CognitionTypes.h`、`response/*` | `static_assert` 字段类型 |
| Response 对象拆到 `response/` 目录 | `response/ResponseBuildRequest.h`、`response/ResponseBuildResult.h`、`cognition/CMakeLists.txt` | public header file set 包含新增头 |
| PerceptionResult 有阶段落点但不进 contracts | `perception/PerceptionResult.h` | include 与类型断言通过 |
| 直接消费者需随字段名调整 | `cognition/src/CognitionFacade.cpp`、`runtime/src/AgentOrchestrator.cpp` | `dasall_unit_tests` 编译通过 |
| 契约边界不得回退 | 既有 contract tests | Goal/Belief/Context/Observation 回归通过 |

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 更新 `CognitionConfig.h` | 默认配置值正例；无平台/provider 控制字段负例 | `CognitionInterfaceSurfaceTest` | 若字段超出 §6.10，回退到配置表 |
| B2 | 更新 `CognitionTypes.h` | request/result 字段类型正例；职责串扰负例 | `CognitionInterfaceSurfaceTest` | 若 PlanGraph 未定义导致阻塞，保留 ref 字段并交给 COG-TODO-008 |
| B3 | 新增 `response/ResponseBuildRequest.h` 与 `response/ResponseBuildResult.h` | response build 输入输出字段断言 | `CognitionInterfaceSurfaceTest` | 若命名空间影响 COG-TODO-001 签名，保持 root namespace |
| B4 | 新增 `perception/PerceptionResult.h` | PerceptionResult 可 include 且不依赖 shared contracts 扩张 | `CognitionInterfaceSurfaceTest` | 若阶段对象过宽，移除执行/工具字段 |
| B5 | 更新直接消费者与 CMake file set | `dasall_cognition`、`dasall_unit_tests` 可构建 | build target | 仅做适配，不扩张 runtime 行为 |
| B6 | 回归 contracts | Goal/Belief/Context/Observation 不回退 | contract ctest 正则 | 若失败，优先定位是否误改 contracts |

## 10. D Gate

| 检查项 | 结论 |
|---|---|
| D 文档已落盘 | PASS |
| Design->Build 映射完整 | PASS |
| Build 三件套已锁定 | PASS |
| 范围未越界 | PASS：不实现 projector/resolver/Planner/Reasoner/ResponseBuilder 真实算法 |
| 是否允许进入 Build | PASS |

## 11. Build 结果

| 原子项 | 结果 |
|---|---|
| B1 | PASS：`CognitionConfig` 默认值按 §6.10 落盘，并移除 placeholder-only `default_tool_name` 语义 |
| B2 | PASS：`CognitionStepRequest` / `CognitionDecisionResult` / `ReflectionRequest` / `CognitionReflectionResult` 字段冻结到 `CognitionTypes.h` |
| B3 | PASS：`ResponseBuildRequest` / `ResponseBuildResult` 移入 `response/` public headers，保持 `dasall::cognition` namespace |
| B4 | PASS：`PerceptionResult` 落盘到 `perception/` public header，未引入 shared contracts 扩张 |
| B5 | PASS：`CognitionFacade` 与 `AgentOrchestrator` 已按新字段适配，`dasall_cognition` / unit 聚合可构建 |
| B6 | PASS：Goal/Belief/Context/Observation contract 回归通过 |

## 12. 验证证据

1. `cmake -S . -B build-ci-cog007 -G "Unix Makefiles"`
   - 结果：通过。
2. `cmake --build build-ci-cog007 --target dasall_cognition dasall_unit_tests dasall_contract_tests`
   - 结果：通过；`dasall_unit_tests` 464/464 passed，`dasall_contract_tests` 152/152 passed。
3. `ctest --test-dir build-ci-cog007 -R "CognitionInterfaceSurfaceTest|GoalContractFieldContractTest|BeliefStateContractTest|ContextPacketFieldContractTest|ObservationContractTest" --output-on-failure`
   - 结果：通过；5/5 passed。
4. `cmake --build build-ci --target dasall_cognition dasall_unit_tests dasall_contract_tests`
   - 结果：通过；既有 Ninja `build-ci` 完成重配置、构建与 unit / contract 聚合测试。输出包含既有 policy catalog `-Waddress` 编译告警，不影响本轮 gate。

## 13. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 代码注释 | PASS：未新增解释性噪声注释，仅保留自解释结构体字段 |
| 正例覆盖 | PASS：surface test 覆盖默认配置、request/result 字段类型、response/perception public header |
| 负例覆盖 | PASS：surface test 断言 cognition 对象不含 `AgentResult`、`ReflectionDecision`、`RecoveryRequest`、publish channel、provider payload 等越界字段 |
| 测试发现性 | PASS：`CognitionInterfaceSurfaceTest` 已在 unit 聚合中发现并运行 |
| 直接消费者 | PASS：runtime/cognition skeleton 均已显式处理 optional 结果和新字段名 |
| validation blocker | PASS：修复 plugin contract target 聚合未向父作用域回传的问题，避免 Makefiles 下 `dasall_contract_tests` 注册后未构建 |
| TODO / worklog 证据 | PASS：本交付物、专项 TODO、开发执行记录均回写 |
