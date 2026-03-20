# WP05-T005 PromptSpec 与 PromptRelease 定义

最近更新时间：2026-03-20
任务状态：Done
任务编号：WP05-T005
上游输入：ADR-006 ContextOrchestrator 与 PromptComposer 边界、WP04-T002 PromptComposeRequest 语义说明、WP04-T004 PromptComposeResult 语义说明、DASALL Agent 架构文档 5.4.5-5.4.7、DASALL contracts 冻结实施计划

## 0. Phase 0 研究学习证据链

### 本地证据清单

1. 架构文档 5.4.5 明确 Prompt 必须被视为正式资产，至少覆盖 system_instructions、task_template、output_schema、few_shots、policy_notes，不允许继续散落在代码字符串中。
2. 架构文档 5.4.6 明确 Prompt Registry 按 stage、task_type、language、available_tools 等条件选择 PromptSpec，并返回对应 PromptRelease；Prompt Composer 只消费这些资产与 ContextPacket。
3. 架构文档 5.4.7 明确 PromptRegistry 管理 PromptSpec、PromptRelease、适用 stage、版本、评测状态、信任来源，并给出了 PromptSpec/PromptRelease 的最小建议字段。
4. ADR-006 §3.3/§4/§6.2/§6.3 冻结了 Prompt 链路：PromptComposeRequest 负责装配请求，PromptComposeResult 负责装配产物，二者之间仍需要 PromptSpec/PromptRelease 作为正式资产层，且 PromptComposer 不得回写 memory/context。
5. WP04-T002-D 说明 PromptComposeRequest 只携带 `prompt_release_id` 引用，不定义 Prompt 资产本体，T005 需要承接这部分语义而不回改 Request 对象。
6. WP04-T004-D 说明 PromptComposeResult 只输出 `selected_prompt_id` 和 `selected_version`，T005 需要定义这两个元数据在资产层分别指向何物，并保证灰度、回滚和审计可追溯。
7. WP05-T001 规定 prompt 属于 Wave2 子域，主要承接 WP04 Prompt 边界，不允许把 memory/tool/llm 运行态职责回灌到 Prompt 资产对象。

### 外部参考清单

1. Protobuf Language Guide: Updating A Message Type：新增字段通常是兼容安全的，但修改既有字段语义、复用编号或改变默认值会引入演进风险，适合借鉴到 Prompt 资产的兼容优先策略。
2. Consumer-Driven Contracts Pattern：契约应围绕真实消费方的最小必要依赖构建，provider 不应为了“未来可能需要”把内部运行细节暴露给所有消费者；这直接支持 PromptSpec/PromptRelease 只暴露 Registry/Composer/Policy 真正消费的最小面。

### 对本任务的可落地启发

1. PromptSpec 应保持版本中立，表达“一个 Prompt 模板族的稳定选择面”，而不是带着某次发布状态或灰度信息。
2. PromptRelease 应表达“一个可发布、可回滚、可审计的 Prompt 资产实例”，承载版本、评测状态和正式内容，但不能混入 ContextPacket 或 PromptComposeResult 的运行态字段。
3. Prompt 资产层必须服务真实消费链路：Registry 选择、Composer 装配、Policy 审计，而不是提前承担 provider payload、memory write-back、tool permission 等职责。
4. 兼容性上应默认新增字段优于修改旧字段，因此对象都保留可选字段扩展位，并沿用 Unspecified 哨兵枚举约束。
5. Contract tests 必须验证最小正例与边界负例，尤其是 release 生命周期字段不能回流到 PromptSpec，运行时上下文字段不能渗入 PromptRelease。

## 1. 任务理解

本任务只处理 WP05-T005：冻结 PromptSpec 与 PromptRelease 对象边界，并落盘 contracts 契约对象、守卫和 contract test。

本任务不处理：

1. PromptComposeRequest/PromptComposeResult 字段回改，保持 WP04 冻结结论不变。
2. provider-specific request payload、message 序列化或模型厂商格式差异，这些仍归 PromptPolicy/LLMAdapter。
3. PromptRegistry 的实现算法与灰度发布逻辑，只定义稳定 contracts 面。

## 2. 约束与边界

### 2.1 直接约束

1. PromptSpec 必须服务 PromptRegistry 的选择，不承担发布状态、回滚链和线上审计职责。
2. PromptRelease 必须服务 PromptRegistry/PromptComposer/PromptPolicy 的发布与追溯，不承担 ContextPacket ownership 或 PromptComposeResult write-back 职责。
3. T005 不得回改 PromptComposeRequest/Result，只能让 `prompt_release_id`、`selected_prompt_id`、`selected_version` 有明确资产语义落点。
4. 默认向后兼容，使用显式枚举哨兵、可选扩展字段和最小必填集合。

### 2.2 PromptSpec 允许与禁止

PromptSpec 允许承载：

1. 稳定标识：`prompt_id`。
2. 选择维度：`stage`、`task_types`、`language`、`model_family`。
3. 模板槽位：`template_slots`。
4. 选择提示：`tool_hints`、`tags`。
5. 结构化输出锚点：`output_schema_ref`。

PromptSpec 明确禁止承载：

1. 发布生命周期字段：`version`、`eval_status`、`release_scope`、`rollback_from`。
2. 运行态装配控制字段：`context_packet_id`、`visible_tools`、`model_route`、`response_format`。
3. 装配结果字段：`messages`、`estimated_tokens`、`selected_version`、`composition_warnings`。

### 2.3 PromptRelease 允许与禁止

PromptRelease 允许承载：

1. 版本追溯：`prompt_id`、`version`、`stage`、`eval_status`、`release_scope`。
2. 正式资产内容：`system_instructions`、`task_template`、`output_schema_ref`、`few_shot_refs`、`policy_notes`。
3. 发布治理元数据：`rollback_from`、`trusted_source`、`tags`。

PromptRelease 明确禁止承载：

1. Context ownership 字段：`context_packet_id`、`memory_snapshot`、`retrieval_candidates`、`knowledge_fragments`。
2. 运行态请求控制字段：`visible_tools`、`model_route`、`response_format`。
3. 装配结果或写回字段：`messages`、`estimated_tokens`、`memory_write_back`、`context_update`。

## 3. Design 原子清单

1. D1：冻结 PromptSpec 的稳定选择面与必填约束。
- 输入依据：架构文档 5.4.6/5.4.7、WP04-T002-D。
- 产出：contracts/include/prompt/PromptSpec.h。
- 完成判定：可表达 PromptRegistry 的选择基础，且不含 release/runtime/result 字段。
- 风险回退：若发现字段更像发布或运行态语义，则迁回 PromptRelease 或 PromptComposeRequest/Result。

2. D2：冻结 PromptRelease 的发布实例面与必填约束。
- 输入依据：架构文档 5.4.5/5.4.7、ADR-006、WP04-T004-D。
- 产出：contracts/include/prompt/PromptRelease.h；contracts/include/prompt/PromptReleaseGuards.h。
- 完成判定：可表达一个可发布、可回滚的 Prompt 资产实例，且不含上下文 ownership 或 memory write-back 字段。
- 风险回退：若出现运行态字段混入，则回退到 release 元数据与正式资产内容最小集合。

3. D3：设计 PromptSpec/PromptRelease 边界可验证测试矩阵。
- 输入依据：WP04 prompt tests 风格、PromptComposeRequest/Result 已冻结字段。
- 产出：tests/contract/prompt/PromptSpecReleaseContractTest.cpp + CMake 注册。
- 完成判定：至少 1 个正例 + 1 个负例，并显式验证 PromptSpec/PromptRelease 的越界字段拒绝。
- 风险回退：若测试只校验 happy path，则补充 release lifecycle 渗入和 context/runtime 渗入负例。

## 4. Design -> Build 映射

| D 原子项 | 设计结论 | 对应 Build 动作 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|
| D1 | PromptSpec 只承载稳定选择面 | 新增 PromptSpec 契约与字段边界函数 | contracts/include/prompt/PromptSpec.h | 正例：最小合法 PromptSpec 通过；负例：缺失 prompt_id、重复 template_slots、release 字段边界拒绝 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R PromptSpecReleaseContractTest --output-on-failure |
| D2 | PromptRelease 只承载版本化发布实例 | 新增 PromptRelease 契约与守卫 | contracts/include/prompt/PromptRelease.h；contracts/include/prompt/PromptReleaseGuards.h | 正例：最小合法 PromptRelease 通过；负例：缺失 version、非法 eval_status、rollback_from 自指、context 字段边界拒绝 | 同上 |
| D3 | Prompt 子域边界需可自动验证 | 新增 PromptSpecReleaseContractTest 并接入 CMake | tests/contract/prompt/PromptSpecReleaseContractTest.cpp；tests/contract/CMakeLists.txt | 断言 PromptSpec 不吸收发布生命周期字段，PromptRelease 不吸收上下文/结果字段 | 同上 |

## 5. D Gate 结果

1. D 文档已落盘。
2. Design 原子清单已冻结，且每项具备二值完成判定。
3. Build 三件套已锁定：
- 代码目标：PromptSpec.h、PromptRelease.h、PromptReleaseGuards.h
- 测试目标：PromptSpecReleaseContractTest.cpp + CMake 注册
- 验收命令：cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R PromptSpecReleaseContractTest --output-on-failure
4. 范围未越界，不回改 PromptComposeRequest/Result，可进入 B。

Gate 结论：PASS。

## 6. Build 执行清单

1. B1：新增 PromptSpec 契约头文件与字段边界函数。
2. B2：新增 PromptRelease 契约头文件与共享守卫。
3. B3：新增 contract test，覆盖正负例与越界字段拒绝断言。
4. B4：接入 tests/contract/CMakeLists.txt，保证测试可发现、可执行。

## 7. 风险与回退

1. 风险：把 PromptRelease 生命周期字段写进 PromptSpec，导致选择面和发布面耦合。
- 回退：PromptSpec 只保留稳定选择条件与模板槽位。
2. 风险：把上下文或路由控制字段写进 PromptRelease，导致 Prompt 资产沦为运行态请求对象。
- 回退：PromptRelease 只保留发布实例语义与正式资产内容。
3. 风险：测试没有验证越界字段，后续容易把 Prompt 子域重新混层。
- 回退：保留边界决策枚举与字段拒绝断言作为固定回归点。

## 8. Blocker 状态

当前无 blocker。