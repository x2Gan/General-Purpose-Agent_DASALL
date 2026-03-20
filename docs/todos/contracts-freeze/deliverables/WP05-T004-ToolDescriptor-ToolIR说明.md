# WP05-T004 ToolDescriptor 与 ToolIR 分层说明

最近更新时间：2026-03-20
任务状态：Done
任务编号：WP05-T004
上游输入：WP05-T002 ToolRequest 语义说明、WP05-T003 ToolResult 语义说明、架构 Tool System 章节、DASALL contracts 冻结实施计划

## 0. Phase 0 研究学习证据链

### 本地证据清单

1. 架构文档 5.2.2：ITool 暴露 descriptor()，说明 ToolDescriptor 是注册与可发现能力的稳定描述对象。
2. 架构文档 5.2.5：Tool Schema、Function Calling、Tool IR 必须显式分层；所有模型输出先归一化为 Tool IR 后才能进入执行链路。
3. 架构文档 3.6 tools 治理链路：ActionDecision -> Tool Route -> Tool Registry -> Validator -> Policy Gate -> Executor，说明 ToolDescriptor 属于 Registry/Validator 侧，ToolIR 属于 Validator/Executor 侧。
4. WP05-T002：ToolRequest 明确禁止持有 tool_schema/tool_descriptor/tool_ir 字段，T004 需承接这些语义并保持分层。
5. WP05-T003：ToolResult 明确禁止持有 tool_schema/tool_descriptor/tool_ir 字段，T004 需提供与结果对象平行且不混层的注册/执行表示对象。
6. 工程蓝图 tools 子域：ToolRequest/ToolResult/ToolDescriptor/ToolIR 同属 tool 子域但职责不同，要求避免“万能对象”回流。

### 外部参考清单

1. Protobuf Updating A Message Type：新增字段通常是安全的，但复用或重排既有字段语义会造成兼容风险；对象边界应尽量稳定且单一职责。
2. Consumer-Driven Contracts：契约应围绕消费者最小必要依赖设计，避免把 provider 内部实现细节暴露给所有消费者。

### 对本任务的可落地启发

1. ToolDescriptor 只描述“可注册、可发现、可治理”的静态能力面，不携带一次调用的动态参数与预算快照。
2. ToolIR 只描述“一次归一化执行单元”的动态执行面，不反向携带注册目录、能力清单、文档元信息。
3. ToolRequest 与 ToolResult 已冻结禁止字段，T004 必须通过契约结构和测试保证这种分层是可自动验证的。
4. 设计上保留 Unspecified 哨兵枚举值，沿用 WP02/WP03/WP05 的兼容约束风格。
5. 采用最小必填字段守卫，避免注册对象和执行对象在后续演进中互相渗透。

## 1. 任务理解

本任务只处理 WP05-T004：冻结 ToolDescriptor 与 ToolIR 的分层职责，并落盘 contracts 对象与 contract test。

本任务不处理：

1. Prompt/Memory/LLM 子域对象，归后续 T005+。
2. ToolRequest/ToolResult 语义扩张，保持 T002/T003 既有冻结结论。
3. provider 私有协议细节映射，仅定义 provider-neutral 契约面。

## 2. 约束与边界

### 2.1 直接约束

1. ToolDescriptor 面向 ToolRegistry/list_tools 语义，必须可稳定列举与审计。
2. ToolIR 面向 Validator/Executor 语义，必须表达一次调用归一化结果。
3. ToolDescriptor 与 ToolIR 都不得承担 ToolRequest/ToolResult 的主责字段集合。
4. 默认向后兼容，保留 Unspecified 哨兵与可选字段演进空间。

### 2.2 边界与非目标

ToolDescriptor 允许承载：

1. 能力标识：tool_name、display_name。
2. 能力分类：category、capability_tier。
3. 注册约束：is_read_only、supports_compensation、default_timeout_ms。
4. 输入/输出契约锚点：input_schema_ref、output_schema_ref。
5. 治理元数据：required_scopes、tags、version。

ToolDescriptor 明确禁止承载：

1. 一次调用动态参数：arguments_payload、request_id、tool_call_id。
2. 执行结果字段：payload、error、side_effects。
3. 运行态统计：duration_ms、spent_tokens、budget_snapshot。

ToolIR 允许承载：

1. 一次执行标识：request_id、tool_call_id、tool_name。
2. 归一化调用语义：operation、normalized_arguments。
3. 执行控制：timeout_ms、idempotency_key、priority。
4. 路由与追溯：route, goal_id, worker_task_id。

ToolIR 明确禁止承载：

1. 注册目录元数据：display_name、input_schema_ref、required_scopes、version。
2. 执行结果字段：payload、error、side_effects。
3. Prompt/provider 渲染字段。

## 3. Design 原子清单

1. D1：冻结 ToolDescriptor 字段边界并定义必填约束。
- 输入依据：架构 5.2.2 + 3.6 工具治理链路。
- 产出：contracts/include/tool/ToolDescriptor.h
- 完成判定：可表达注册能力且不含运行态字段。
- 风险回退：若发现动态调用字段渗入，回退到“注册静态面”最小集合。

2. D2：冻结 ToolIR 字段边界并定义必填约束。
- 输入依据：架构 5.2.5 Tool IR 分层约束。
- 产出：contracts/include/tool/ToolIR.h
- 完成判定：可表达单次执行归一化且不含注册目录字段。
- 风险回退：若出现 schema/version/scopes 等注册元数据，立即迁回 ToolDescriptor。

3. D3：设计分层可验证测试矩阵。
- 输入依据：WP05-T002/T003 禁止字段与现有 contract test 风格。
- 产出：tests/contract/tool/ToolDescriptorIRContractTest.cpp + CMake 注册。
- 完成判定：至少 1 正例 + 1 负例，并验证跨层字段拒绝。
- 风险回退：若测试仅验证编译不验证分层，补充字段拒绝断言。

## 4. Design -> Build 映射

| D 原子项 | 设计结论 | 对应 Build 动作 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|
| D1 | ToolDescriptor 只承载注册能力面 | 新增 ToolDescriptor 契约与校验函数 | contracts/include/tool/ToolDescriptor.h | 正例：最小合法 descriptor 通过校验；负例：缺失 tool_name 或 category 非法 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ToolDescriptorIRContractTest --output-on-failure |
| D2 | ToolIR 只承载单次归一化执行面 | 新增 ToolIR 契约与校验函数 | contracts/include/tool/ToolIR.h | 正例：最小合法 IR 通过校验；负例：operation 非法或 tool_call_id 缺失 | 同上 |
| D3 | 分层约束可自动验证 | 新增 ToolDescriptorIRContractTest 并接入 CMake | tests/contract/tool/ToolDescriptorIRContractTest.cpp；tests/contract/CMakeLists.txt | 断言 descriptor 元数据不会进入 IR、IR 动态字段不会进入 descriptor 语义入口 | 同上 |

## 5. D Gate 结果

1. D 文档已落盘。
2. Design 原子清单已冻结，且每项具备二值完成判定。
3. Build 三件套已锁定：
- 代码目标：ToolDescriptor.h、ToolIR.h
- 测试目标：ToolDescriptorIRContractTest.cpp + CMake 注册
- 验收命令：cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ToolDescriptorIRContractTest --output-on-failure
4. 范围未越界，可进入 B。

Gate 结论：PASS。

## 6. Build 执行清单

1. B1：新增 ToolDescriptor 契约头文件与必填校验。
2. B2：新增 ToolIR 契约头文件与必填校验。
3. B3：新增 contract test，覆盖正负例与跨层拒绝断言。
4. B4：接入 tests/contract/CMakeLists.txt，保证测试可执行。

## 7. 风险与回退

1. 风险：把动态执行字段塞回 ToolDescriptor，导致注册对象漂移。
- 回退：仅保留静态能力、治理元数据与 schema 引用。
2. 风险：把注册字段塞进 ToolIR，导致运行时输入膨胀。
- 回退：ToolIR 仅表达一次调用归一化。
3. 风险：测试只验证 happy path，无法捕获混层回归。
- 回退：保留 category/operation 范围检查与跨层字段拒绝断言。

## 8. Blocker 状态

当前无 blocker。
