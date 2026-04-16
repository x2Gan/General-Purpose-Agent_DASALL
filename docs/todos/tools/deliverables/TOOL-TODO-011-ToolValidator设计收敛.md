# TOOL-TODO-011 ToolValidator 设计收敛

日期：2026-04-16  
任务：TOOL-TODO-011  
状态：D Gate PASS

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-011 的验收条件要求：ToolValidator 必须实现 `validate()`、`inject_defaults()`、`normalize_arguments()`、`derive_operation()`，并覆盖 idempotency_key 透传、request 缺失、default timeout、DryRun / ValidateOnly 分支。
2. docs/architecture/DASALL_tools子系统详细设计.md 6.12.1 明确 ToolValidator 负责把 `ToolRequest` 与 `ToolDescriptor` 收敛为 `ToolIR`，必须复用 `ToolRequestGuards` / `ToolIRGuards`，且除显式默认值外不允许静默纠偏。
3. contracts/include/tool/ToolRequestGuards.h、contracts/include/tool/ToolIR.h 与 tests/contract/tool/ToolRequestContractTest.cpp、ToolDescriptorIRContractTest.cpp 已冻结 request / IR 语义边界，因此 011 只能复用现有 contracts，不得把 DryRun / ValidateOnly 或 route supporting object 推进到 shared contracts。
4. TOOL-TODO-009 已提供稳定的 ToolDescriptor 来源，本轮 validator 只消费 request + descriptor，不提前承担 PolicyGate、RouteSelector、ResultProjector 的职责。

## 2. Design 结论

1. ToolValidator 采用 `ToolValidationResult` + `ValidationDiagnostics` 收口成功/失败状态：成功时输出 `ToolIR`，失败时输出稳定 error_code 和 message，保持 fail-closed。
2. `validate()` 先执行 `ToolRequest` 与 `ToolDescriptor` 的 contract guard，再核对 `request.tool_name == descriptor.tool_name` 与 invocation/category 一致性，随后派生 operation、规范化参数、注入显式默认值并走 `ToolIR` guard 收口。
3. 默认值注入只接受显式来源：`request.timeout_ms` 优先，其次才是 `descriptor.default_timeout_ms`；若两者都缺失，则保持 timeout 未设置，而不是静默猜测一个系统默认值。
4. `DryRun` / `ValidateOnly` 作为 module-local 非执行态分支，在当前 contracts 不扩张的前提下通过 tags `tool.mode.dry_run` 与 `tool.mode.validate_only` 显式选择；若二者同时出现则直接拒绝，避免猜测优先级。
5. route hint 仍保持最小化：Workflow / AgentDelegation 映射到 `WorkflowEngine`，其余 descriptor 默认走 `LocalTool`；仅在显式 tag `tool.route.mcp` 出现时才给出 `MCPRemote` hint，为后续 RouteSelector 留下可消费但非强制的提示位。

## 3. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| ToolValidator 内部实现 | tools/src/validation/ToolValidator.h；tools/src/validation/ToolValidator.cpp |
| validator 行为 / defaulting / 非执行态单测 | tests/unit/tools/ToolValidatorTest.cpp；tests/unit/tools/ToolValidatorDefaultingTest.cpp；tests/unit/tools/ToolValidatorDryRunTest.cpp |
| tools unit discoverability 接线 | tools/CMakeLists.txt；tests/unit/tools/CMakeLists.txt；tests/unit/CMakeLists.txt |

## 4. Build 三件套

1. 代码目标：让 `dasall_tools` 编译真实 ToolValidator 源文件，并移除 validation placeholder 编译入口。
2. 测试目标：通过三条 validator unit tests 覆盖归一化、defaulting、DryRun / ValidateOnly 分支，并保持 `ToolRequestContractTest` 与 `ToolDescriptorIRContractTest` 不回退。
3. 验收命令：
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`、`dasall_contract_tests`
   - RunCtest_CMakeTools 运行 `ToolValidatorTest`、`ToolValidatorDefaultingTest`、`ToolValidatorDryRunTest`

## 5. 风险与回退

1. 当前 DryRun / ValidateOnly 触发仍是 module-local tag 约定，不是 shared contract 字段；若后续 runtime caller fixture 或 ToolRequest 设计冻结了新的显式控制位，应优先迁移到正式上游输入，而不是在 validator 内同时维护多套等价语义。
2. route hint 目前只做最小分类，不承担最终 route 决策；后续 RouteSelector 任务必须把 descriptor、policy、capability、health 组合起来做真正选择，不能把 011 的 hint 误当最终路由。
3. timeout 缺失时保持未设置是刻意的 deny-oriented 行为；后续若需要全局 tool timeout fallback，应先通过 ToolConfigAdapter 显式提供投影视图，而不是在 validator 内硬编码默认值。