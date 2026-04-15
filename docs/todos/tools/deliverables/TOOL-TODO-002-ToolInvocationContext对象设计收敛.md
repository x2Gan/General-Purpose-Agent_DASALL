# TOOL-TODO-002 ToolInvocationContext 对象设计收敛

日期：2026-04-15  
任务：TOOL-TODO-002  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 6.5.1 已把 ToolInvocationContext 固定为 tools/include 下的 module-local public 对象，仅承接 runtime 注入的 caller/profile/trace/confirmation 调用上下文。
2. 同一设计文档 6.5.3 明确 `risk_tier`、confirmation proof 绑定、caller domain 不进入 ToolDescriptor，而应停留在 ToolInvocationContext、ToolAdmissionRequest/Decision 与 ToolPolicyView 这一组调用时态对象中。
3. 同一设计文档 6.7 与 6.12.1 说明 ToolManager 的 invoke 链路消费 ToolRequest 与 ToolInvocationContext，后续 PolicyGate 必须对缺 profile projection、缺 confirmation fact 等情况 fail-closed，这要求 Context 首版保留“可能缺失”的输入表达能力，而不是把字段做成静态注册元数据。
4. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-002 的测试目标只要求 caller_domain、profile snapshot ref、trace / confirmation 输入面可编译且不越权承载上下文/恢复语义；runtime caller fixture 的更细口径仍由 TOOL-TODO-023 继续解阻。
5. services/include/ServiceTypes.h 已冻结 ServiceCallContext 需要 request_id / session_id / trace_id / tool_call_id / goal_id 等调用期关联信息，因此 tools 侧必须至少为 session 与 trace 预留 invoke-scoped context 输入，而不能把 profile 或 confirmation 语义塞回 shared contracts。

## 2. 外部参考

1. OpenTelemetry tracing 文档指出 Context Propagation 是分布式追踪成立的核心，Span Context 是伴随分布式上下文传播的独立请求时态对象，至少承载 trace_id、span_id、trace flags 与 trace state。这支持本任务把 trace 相关输入保留在 ToolInvocationContext 的 invoke-scoped supporting object 中，而不是回写到 ToolRequest、ToolDescriptor 或 ToolResult 这类 shared ABI。

## 3. Design 结论

1. ToolInvocationContext 首版冻结为非 owning 的 invoke-scoped 输入聚合，只承载 caller_domain、session_id、profile snapshot ref、trace propagation identity 与 confirmation fact 集合。
2. profile 侧只保存 `RuntimePolicySnapshot` 的引用，不复制 generation、visibility rules 或 timeout policy 字段，避免 tools 在公共头里重新定义 profile 投影视图。
3. trace 侧只表达传播身份：`trace_id`、`span_id`、`parent_span_id`；不在该对象中引入 exporter、sampling、provider diagnostics 或 observability backend 句柄。
4. confirmation 侧只表达证明事实本身：`confirmation_id`、`subject_ref`、`proof_type`、`confirmed_at_ms`；准入裁定、risk tier 和恢复动作仍保留给 ToolPolicyGate / runtime。
5. ToolInvocationContext 不承载 ContextPacket、Prompt、Observation、RecoveryDecision、CheckpointRef 或 replan/retry/compensate 控制字段，保持 ADR-006/007/008 的边界不变。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 trace 传播输入面 | tools/include/ToolInvocationContext.h 中的 ToolTraceContext |
| 冻结 confirmation evidence 输入面 | tools/include/ToolInvocationContext.h 中的 ToolConfirmationFact |
| 冻结 invoke-scoped context 聚合 | tools/include/ToolInvocationContext.h 中的 ToolInvocationContext |
| 增加 compile-only surface 证据 | tests/unit/tools/ToolInvocationContextSurfaceTest.cpp |

## 5. Build 三件套

1. 代码目标：把 tools/include/ToolInvocationContext.h 从前向声明替换为真实对象定义，并在同文件内补齐 trace / confirmation supporting structs。
2. 测试目标：新增未接线的 compile-only surface 源文件 tests/unit/tools/ToolInvocationContextSurfaceTest.cpp，通过字段类型断言和样例初始化锁定 caller/profile/trace/confirmation 输入面，同时不提前侵入 TOOL-TODO-008 的 unit 拓扑接线 owner。
3. 验收命令：
   - `c++ -std=c++20 -I/home/gangan/DASALL/tools/include -x c++ -fsyntax-only /home/gangan/DASALL/tests/unit/tools/ToolInvocationContextSurfaceTest.cpp`
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`
   - `dasall_unit_tests` 目标构建期间自动执行 unit 集合，并以 `249/249 passed` 作为当前 CMake gate 依据；RunCtest_CMakeTools 的额外尝试仍带出历史 `DartConfiguration.tcl` 噪声，不作为本轮 gate 结论来源

## 6. 风险与回退

1. 若后续 TOOL-TODO-023 需要把 caller fixture 进一步细化为更明确的 runtime mock 输入，应只在 ToolInvocationContext 内补充最小 supporting 字段或 fixture 文档，不把本轮对象整体推入 shared contracts。
2. 若后续需要完整 trace state / baggage，必须先经过 infra tracing 或 runtime caller surface 评审，不能在 002 中直接扩大为 provider-specific trace carrier。
3. 若后续确认 confirmation facts 需要更严格的 freshness / proof binding 字段，应在 PolicyGate 或 TOOL-TODO-023 中补 internal mapping，不应让 ToolInvocationContext 直接承担 admission decision 结果。