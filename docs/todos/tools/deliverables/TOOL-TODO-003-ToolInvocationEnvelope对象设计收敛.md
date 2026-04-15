# TOOL-TODO-003 ToolInvocationEnvelope 对象设计收敛

日期：2026-04-15  
任务：TOOL-TODO-003  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 6.5.1 已把 ToolInvocationEnvelope 固定为 tools/include 下的 module-local public 返回面，职责是向 runtime 同时交付 ToolResult、Observation、ObservationDigest 与 route facts。
2. 同一设计文档 6.5.3 明确 `RouteUnavailable`、`CapabilityUnsupported`、`PartialSideEffect` 等细粒度原因码，以及 `evidence_refs`、`compensation_hints`、route facts 都应继续停留在 ToolInvocationEnvelope / ToolFailureEnvelope / CompensationRecord 这组 module-local supporting object 中，不回写 ToolResult 或 ResultCode。
3. 同一设计文档 6.8、6.10、6.12.2、6.12.5 说明 ToolManager 返回 Envelope 后，runtime 才决定 memory 写回、继续推理或恢复动作；ResultProjector 负责构造 Observation / ObservationDigest，CompensationLedger 负责在 invoke-scoped 生命周期内生成 compensation_hints 并通过 Envelope 交给 runtime。
4. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-003 的验收目标只要求 ToolResult、Observation、ObservationDigest、route facts、compensation_hints 的组合面可编译，且 supporting object 不升格到 shared contracts。
5. contracts/include/tool/ToolResult.h 与 contracts/include/observation/Observation.h / ObservationDigest.h 已冻结 shared 结果层：ToolResult 只保留最小执行结果面，ObservationDigest 禁止混入 payload、error、side_effects、tool_call_id 等执行语义，因此 Envelope 必须承担组合与补充 supporting facts 的职责。

## 2. 外部参考

1. Microsoft Azure 补偿事务模式指出，补偿流程应记录撤销所需信息，并由后续工作流决定如何、何时、按何顺序执行补偿；补偿步骤还需要保持幂等和可恢复。这支持本任务把 `compensation_hints` 设计为随 Envelope 传递的 supporting data，而不是把补偿控制计划直接写进 ToolResult 或其它 shared contract。

## 3. Design 结论

1. ToolInvocationEnvelope 首版冻结为 runtime-facing 统一返回面，直接聚合 shared `ToolResult`、`Observation`、`ObservationDigest`，不复制这些对象的内部字段。
2. route facts 以 module-local `ToolRouteFacts` 表达，首版只保留 `route_kind`、`route_ref`、`decision_reason`、`plugin_id`、`server_id` 这组最小选择事实，不在此任务中引入新的 shared route enum。
3. compensation handoff 以 module-local `ToolCompensationHint` 表达，承载 `compensation_action`、`target_ref`、`reason_code`、`evidence_refs`；是否真正执行补偿仍由 runtime / RecoveryManager 决定。
4. Envelope 允许以 `failure_reason_code` 保留比 `ErrorInfo.failure_type` 更细粒度的 module-local 原因码，但这类 reason code 不应等价为新的 shared `ResultCode`。
5. Envelope 不承载 ContextPacket、Prompt、RecoveryDecision、CheckpointRef、retry/replan/abort 裁定或 runtime 主控字段，保持 ADR-006/007/008 的边界不变。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 route facts supporting object | tools/include/ToolInvocationEnvelope.h 中的 ToolRouteFacts |
| 冻结 compensation handoff supporting object | tools/include/ToolInvocationEnvelope.h 中的 ToolCompensationHint |
| 冻结 unified return surface | tools/include/ToolInvocationEnvelope.h 中的 ToolInvocationEnvelope |
| 增加 compile-only surface 证据 | tests/unit/tools/ToolInvocationEnvelopeSurfaceTest.cpp |

## 5. Build 三件套

1. 代码目标：把 tools/include/ToolInvocationEnvelope.h 从前向声明替换为真实对象定义，聚合 ToolResult、Observation、ObservationDigest 与 route/compensation supporting facts。
2. 测试目标：新增未接线的 compile-only surface 源文件 tests/unit/tools/ToolInvocationEnvelopeSurfaceTest.cpp，通过字段类型断言与样例初始化锁定 Envelope 组合面，同时不提前侵入 TOOL-TODO-008 的 unit 拓扑接线 owner。
3. 验收命令：
   - `c++ -std=c++20 -I/home/gangan/DASALL/tools/include -I/home/gangan/DASALL/contracts/include -x c++ -fsyntax-only /home/gangan/DASALL/tests/unit/tools/ToolInvocationEnvelopeSurfaceTest.cpp`
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`、`dasall_contract_tests`
   - `dasall_unit_tests` 与 `dasall_contract_tests` 目标构建期间自动执行当前 unit / contract 集合，并分别得到 `249/249 passed`、`152/152 passed`

## 6. 风险与回退

1. 若后续发现 route facts 需要更强类型约束，应优先在 tools module-public 或 internal supporting object 内收敛，而不是直接扩张 contracts shared enum。
2. 若后续补偿链需要更复杂的执行顺序、幂等 token 或人工介入提示，应在 CompensationLedger / runtime 恢复链任务中扩 internal object，不在 003 中把控制计划写死到 Envelope ABI。
3. 若后续需要 failure-only envelope 变体，可在 tools 内部补充 ToolFailureEnvelope，但不应回头把 003 的成功/失败组合面拆回 shared contract。