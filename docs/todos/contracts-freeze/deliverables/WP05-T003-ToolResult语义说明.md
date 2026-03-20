# WP05-T003 ToolResult 语义说明

最近更新时间：2026-03-20
任务状态：Done
任务编号：WP05-T003
上游输入：WP05-T002 ToolRequest 语义说明、WP03-T006 Observation 语义冻结、WP03-T008 ObservationDigest 边界、架构 Tool System 章节、DASALL contracts 冻结实施计划

## 0. Phase 0 研究学习证据链

### 本地证据清单

1. 架构文档 5.2.2：ITool.execute 返回 ToolResult，CompensationManager 同时消费 ToolRequest 与 ToolResult，说明 ToolResult 是工具执行链的稳定输出契约，而不是 ObservationDigest 或恢复控制对象。
2. 架构文档 5.2.6：原始工具结果适合程序消费，不适合直接回灌推理；Tool 子系统必须先产出 ObservationDigest，说明 ToolResult 与 ObservationDigest 必须分层。
3. 架构文档 3.8.2：系统将工具结果统一折叠为 Observation，Observation 至少包含 source、success、payload、error、side_effects，说明 ToolResult 只需承载可折叠的执行输出面，不应抢占 Observation 的统一来源语义。
4. 架构文档失败时序：Tool 执行失败后返回 ToolResult(error + side_effects)，随后进入 Observation 分析和补偿链，说明 ToolResult 需要保留错误与副作用，但不应携带 recovery_action、checkpoint_ref 等控制决策。
5. 实施计划 8 阶段观测链：ToolResult -> Observation -> ObservationDigest，说明 ToolResult 处于 Observation 上游，必须与 Observation/ObservationDigest 保持单向折叠关系。
6. WP05-T002 ToolRequest 语义说明：ToolRequest 已冻结为执行输入面，禁止混入结果、Observation、Prompt/provider、Descriptor/IR；ToolResult 应作为对应输出面，沿相同最小边界设计。
7. WP03-T006 Observation：Observation 统一拥有 source、observation_id 等跨来源字段，ToolResult 不应重复拥有这些顶层归一化字段。

### 外部参考清单

1. Martin Fowler / Ian Robinson《Consumer-Driven Contracts》：消费者应做 just-enough validation，只断言自己真实消费的字段，避免把 provider 的整份内部 schema 暴露给所有消费者。
2. Protobuf Best Practices：共享消息默认保持向后兼容，新增字段应最小化；不要把 API 与存储/内部表示混成同一种消息类型，避免后续演进被同一对象绑死。

### 对本任务的可落地启发

1. ToolResult 只保留执行输出消费者真实依赖的字段：成功性、payload/error、副作用、完成时间、追溯锚点。
2. Observation 与 ObservationDigest 应分别作为“统一折叠对象”和“推理友好投影”，ToolResult 不能提前拥有 observation_id、source、summary 等字段。
3. 补偿与恢复需要消费 ToolResult，但恢复动作本身属于 runtime 决策，不能把 checkpoint_ref、recovery_action、compensation_plan 预埋进共享结果对象。
4. 越界字段守卫应显式阻断 Observation ownership、runtime accounting、Prompt/provider、Descriptor/IR 与 recovery control 五类字段。
5. Contract test 应验证两类真实消费面：成功结果可折叠到 Observation，失败结果可携带 ErrorInfo 与 side_effects 进入补偿/反思链。

## 1. 任务理解

本任务只处理 WP05-T003：冻结 ToolResult 的职责边界，并同步落盘 ToolResult 契约对象、守卫与 contract test。

本任务不处理：

1. ToolDescriptor / ToolIR 的注册与内部统一表示分层，归 WP05-T004。
2. Prompt、Memory、LLM 等其他子域对象冻结。
3. Observation/ObservationDigest 字段扩张，只验证 ToolResult 与两者的对接边界。

## 2. 约束与边界

### 2.1 直接约束

1. 来源 WP05-T003 完成判定：ToolResult 必须“可折叠到 Observation”。
2. 来源架构 Tool System：ToolResult 是 ITool.execute 与 ITool.compensate 的共享输出面，不是恢复决策对象。
3. 来源实施计划观测链：ToolResult 位于 Observation 之前，Observation 位于 ObservationDigest 之前，必须严格单向分层。
4. 来源 WP03-T006 / WP03-T008：Observation 持有统一 source 与 observation_id；ObservationDigest 持有 summary、key_facts、confidence；ToolResult 不得越权承载这些字段。
5. 来源 WP05-T002：ToolRequest/ToolResult 需要构成输入面/输出面成对冻结，默认向后兼容，禁止把 provider 或内部 IR 细节写进共享对象。

### 2.2 边界与非目标

ToolResult 允许承载的语义：

1. 执行标识：request_id、tool_call_id、tool_name。
2. 执行结果：success、payload、error。
3. 副作用：side_effects。
4. 时间基线：completed_at、duration_ms。
5. 关联锚点：goal_id、worker_task_id、tags。

ToolResult 明确禁止承载的语义：

1. Observation ownership：observation、observation_id、source、observation_digest、summary。
2. Runtime accounting：budget_snapshot、remaining_budget、spent_tokens、retry_count、backoff_ms。
3. Prompt/provider 语义：rendered_prompt、provider_payload、final_messages。
4. 注册与内部表示语义：tool_schema、tool_descriptor、tool_ir。
5. 恢复控制语义：checkpoint_ref、recovery_action、compensation_plan、compensation_result。

### 2.3 前置依赖检查

1. WP05-T001-D/B、WP05-T002-D/B 已完成，tool 子域可继续细化。
2. WP03 Observation / ObservationDigest 已冻结，足以为 ToolResult 提供折叠目标。
3. build-ci 已存在，当前工作树在本轮开始前干净，可进入实现与验证。

结论：本任务可执行，无 blocker。

## 3. 方案对比与决策

### 3.1 方案 A：最小执行输出对象（采纳）

定义方式：

1. ToolResult 只描述工具执行完成后的稳定输出：成功性、payload 或 error、副作用、时间、追溯锚点。
2. Observation 负责统一 source 与归一化主键；ObservationDigest 负责推理友好摘要；RecoveryManager 负责恢复动作选择。

优点：

1. 与 ITool.execute / compensate 的真实消费面一致。
2. 与 Observation、ObservationDigest、RecoveryManager 形成稳定的单向折叠链路。
3. 默认保持向后兼容，后续新增治理层不会强迫 ToolResult 演化为万能对象。

缺点：

1. 调用方需要在折叠到 Observation 时补充 source=ToolExecution 与 observation_id，而不能直接把 ToolResult 当 Observation 使用。

### 3.2 方案 B：在 ToolResult 中直接持有 Observation/恢复控制字段（不采纳）

定义方式：

1. 在 ToolResult 中直接塞入 source、observation_id、summary、checkpoint_ref、recovery_action。
2. 试图让 ToolResult 一步覆盖执行输出、统一归一化和恢复决策。

缺点：

1. 会破坏 ToolResult -> Observation -> ObservationDigest 的既定观测链路。
2. 让 ToolResult 同时承担执行输出、推理摘要和恢复控制三类职责，后续演进风险高。
3. 会导致 tool 子域反向侵入 observation/runtime 边界，与 WP05 的子域细化目标相违背。

### 3.3 决策

采用方案 A。

## 4. 最终语义冻结

### 4.1 ToolResult 最小语义范围

ToolResult 只表达以下五类语义：

1. 工具执行标识：request_id、tool_call_id、tool_name。
2. 执行结果：success、payload、error。
3. 副作用声明：side_effects。
4. 完成时间：completed_at、duration_ms。
5. 追溯关联：goal_id、worker_task_id、tags。

### 4.2 明确排除语义

1. ToolResult 不是 Observation，不持有 observation_id、source。
2. ToolResult 不是 ObservationDigest，不持有 summary、key_facts、confidence。
3. ToolResult 不是 runtime accounting 容器，不持有 budget_snapshot、spent_tokens、retry_count。
4. ToolResult 不是 Prompt/provider 载荷，不持有 rendered_prompt、provider_payload。
5. ToolResult 不是恢复控制面，不持有 checkpoint_ref、recovery_action、compensation_plan。

### 4.3 与相邻对象的边界

| 对象 | 主责 | ToolResult 与其关系 | 不可混入字段 |
|---|---|---|---|
| ToolRequest | 工具执行输入 | ToolResult 是输入面的执行输出对应物，沿 request_id/tool_call_id 追溯 | arguments_payload、runtime_budget、timeout_ms |
| Observation | 统一观测折叠 | ToolResult 折叠为 Observation，并由 Observation 增补统一 source 与 observation_id | observation_id、source |
| ObservationDigest | 推理友好投影 | ToolResult 不能越级直接承载 digest 语义 | summary、key_facts、confidence |
| ToolDescriptor / ToolIR | 注册描述 / 内部表示 | ToolResult 只描述执行后输出，不拥有注册元数据或内部 IR | tool_schema、tool_descriptor、tool_ir |
| RecoveryManager | 恢复动作裁定 | ToolResult 提供 error/side_effects 事实，恢复动作由 runtime 决策 | checkpoint_ref、recovery_action |

## 5. Design -> Build 映射

| D 原子项 | 设计结论 | 对应 Build 动作 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|
| D1 | 冻结 ToolResult 只承载执行输出、追溯锚点与副作用 | 定义 ToolResult struct | contracts/include/tool/ToolResult.h | ToolResultContractTest 正例覆盖成功结果与失败结果 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ToolResultContractTest --output-on-failure |
| D2 | 明确禁止 Observation ownership、runtime accounting、Prompt/provider、Descriptor/IR、recovery control 语义进入 ToolResult | 在 ToolResultGuards.h 中实现 required/boundary/field/forbidden-field 守卫 | contracts/include/tool/ToolResultGuards.h | ToolResultContractTest 负例覆盖 observation、spent_tokens、checkpoint_ref 等越界字段 | 同上 |
| D3 | 锁定 Build 三件套并验证测试发现性 | 新增 tool 组 contract test 并接入 tests/contract/CMakeLists.txt | tests/contract/tool/ToolResultContractTest.cpp；tests/contract/CMakeLists.txt | ctest 可发现 ToolResultContractTest，正负例可二值判定 | ctest --test-dir build-ci -N -R ToolResultContractTest |

## 6. D Gate 结果

1. D 文档已落盘。
2. ToolResult 最小语义范围已冻结。
3. Build 三件套已锁定：
   - 代码目标：ToolResult.h、ToolResultGuards.h
   - 测试目标：ToolResultContractTest.cpp + CMake 注册
   - 验收命令：cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ToolResultContractTest --output-on-failure
4. 范围未越界，可进入 -B。

Gate 结论：PASS。

## 7. Build 执行清单

### B1 代码目标

1. 新增 ToolResult 契约对象。
2. 新增 ToolResult 三层守卫：required、boundary、field rules。
3. 新增 forbidden-field guard，阻断 Observation ownership、runtime accounting、Prompt/provider、Descriptor/IR、recovery control 越界字段。

### B2 测试目标

1. 至少 1 个正例：成功 ToolResult 可通过校验。
2. 至少 1 个负例：失败缺少 error、成功缺少 payload、越界字段被阻断。
3. 触及 CMake 注册时，额外验证测试发现性。

### B3 验收命令

1. ctest --test-dir build-ci -N -R ToolResultContractTest
2. cmake --build build-ci --target dasall_contract_tests
3. ctest --test-dir build-ci -R ToolResultContractTest --output-on-failure

## 8. 风险与回退

1. 风险：后续为省事把 Observation/ObservationDigest 字段直接塞回 ToolResult。
   回退：继续通过 forbidden-field 守卫和 contract test 阻断。
2. 风险：把恢复动作决策字段放进 ToolResult，导致 runtime 与 tool 子域混层。
   回退：只保留 error/side_effects 作为事实输入，把 recovery_action 留给 runtime。
3. 风险：把预算/重试统计塞进 ToolResult，导致与 checkpoint/runtime accounting 重复建模。
   回退：保持 ToolResult 只表述执行结果，不表达执行预算状态。

## 9. Blocker 状态

当前无 blocker。

若后续出现阻塞，最小解阻路径为：

1. 若 build-ci 无法重新发现新增测试，先修复 tests/contract/CMakeLists.txt 注册入口。
2. 若 ToolResult 与 Observation 折叠规则冲突，优先回退到本文件 4.2 的禁止项，以分层约束优先而不是实现便利优先。