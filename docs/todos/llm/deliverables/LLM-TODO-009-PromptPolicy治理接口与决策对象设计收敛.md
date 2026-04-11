# LLM-TODO-009 PromptPolicy 治理接口与决策对象设计收敛

日期：2026-04-11
任务：LLM-TODO-009
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.5.5 已冻结 `IPromptPolicy::init()` 与 `evaluate()` 两个入口，并明确 PromptPolicy 只消费共享 `PromptComposeResult` 与 module-local `PromptPolicyInput`，输出 `PromptPolicyDecision`，不得替代 ContextOrchestrator 做语义裁剪，也不得替代 Tool Policy Gate 做真实权限裁定。
2. 同一设计文档的 6.4.2 / 6.4.3 已将 [llm/include/prompt/PromptPolicyDecision.h](../../../../llm/include/prompt/PromptPolicyDecision.h)、[llm/include/prompt/PromptPolicyInput.h](../../../../llm/include/prompt/PromptPolicyInput.h)、[llm/include/prompt/PromptPolicyConfig.h](../../../../llm/include/prompt/PromptPolicyConfig.h) 定义为 module-local supporting types，并把治理决策收敛为 `Allow`、`Deny`、`OverBudget`、`RequireRecompose` 四态。
3. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.15.3 进一步冻结了治理顺序：trusted source -> allowlist -> tool visibility patch -> redaction -> render budget。009 只能冻结治理输入输出边界，不能越权引入 Prompt 实现、tool 权限执行或二次上下文裁剪。
4. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 LLM-TODO-009 的完成判定收敛为“policy 输入输出完整且保持 fail-closed 语义”，因此本轮只冻结 PromptPolicy SPI 与 supporting types，不提前推进 018 的具体治理实现。
5. [contracts/include/prompt/PromptComposeResult.h](../../../../contracts/include/prompt/PromptComposeResult.h) 已提供共享的 compose 输出边界；009 必须直接复用它，而不是在 llm/include 下复制第二份 compose result 对象。
6. [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) 已作为 llm 公共接口冻结门出口，因此 009 继续在同一测试文件补齐 PromptPolicy 签名、治理输入、默认 fail-closed 配置，以及决策对象一致性断言。

## 2. 外部参考

1. OWASP Authorization Cheat Sheet 的 Deny by Default 建议要求：即使没有匹配到显式规则，也必须做出默认拒绝决策，不能把“无规则”当成中立状态。本轮据此将 [llm/include/prompt/PromptPolicyConfig.h](../../../../llm/include/prompt/PromptPolicyConfig.h) 冻结为 `deny_on_missing_allowlist = true` 的 fail-closed 默认值，并把 `PromptPolicyDecision` 的默认 disposition 固定为 `Deny`。参考：https://cheatsheetseries.owasp.org/cheatsheets/Authorization_Cheat_Sheet.html
2. C++ Core Guidelines 的 C.121 要求：若基类被用作接口，应保持为纯抽象类，只暴露纯虚函数和默认虚析构。本轮据此将 [llm/include/prompt/IPromptPolicy.h](../../../../llm/include/prompt/IPromptPolicy.h) 保持为纯抽象 SPI，不夹带状态字段或实现细节。参考：https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines

## 3. Design 结论

1. `IPromptPolicy` 保持为 Prompt 发送前治理面的纯抽象 SPI，签名冻结为 `init(const PromptPolicyConfig&)` 与 `evaluate(const PromptComposeResult&, const PromptPolicyInput&) const`。
2. `PromptPolicyDecision` 在本轮冻结为四态 `PromptPolicyDisposition` 加审计载体：`governed_messages`、`redactions`、`tool_visibility_patch` 与 `reason`。其中 `Allow` 是唯一允许带 `governed_messages` 的 disposition，其余三态必须保持 fail-closed。
3. `PromptPolicyInput` 只冻结 profile 投影后的治理输入：`profile_id`、`allowed_prompt_releases`、`trusted_sources`、`tool_visibility_rules`、`render_budget_tokens`、`active_scene`、`active_persona`。它不承载原始 memory 候选、不承载 provider 私有参数，也不承载真实工具权限结果。
4. `PromptPolicyConfig` 只冻结 PromptPolicy 初始化默认值：`default_allowed_releases`、`default_trusted_sources` 与 `deny_on_missing_allowlist`。本轮不引入平行 profile 配置系统，具体 profile 差异继续留给 012 的配置投影任务。
5. 009 将 fail-closed 语义直接编码到类型默认值和一致性断言中，而不是依赖调用方约定：缺少 allowlist 时默认拒绝，`PromptPolicyDecision` 默认 disposition 为 `Deny`。
6. 本轮不把 `PromptPolicyDecision` 推入 shared contracts。它继续保持 module-local supporting type，等待 037 的 shared admission 评审，而不是先行扩张 shared ABI。
7. PromptPolicy 的职责止于治理裁定与消息边界收敛，不进行模型调用、不做 recovery 决策，也不改写 ADR-006/007/008 确立的 memory/runtime/tool 边界。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 PromptPolicy SPI | [llm/include/prompt/IPromptPolicy.h](../../../../llm/include/prompt/IPromptPolicy.h) |
| 冻结治理决策对象与 disposition 四态 | [llm/include/prompt/PromptPolicyDecision.h](../../../../llm/include/prompt/PromptPolicyDecision.h) |
| 冻结治理输入对象 | [llm/include/prompt/PromptPolicyInput.h](../../../../llm/include/prompt/PromptPolicyInput.h) |
| 冻结 PromptPolicy 初始化配置对象 | [llm/include/prompt/PromptPolicyConfig.h](../../../../llm/include/prompt/PromptPolicyConfig.h) |
| 在 llm 公共接口冻结测试中补齐 PromptPolicy 边界断言 | [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |

## 5. Build 三件套

1. 代码目标：新增 `IPromptPolicy`、`PromptPolicyDecision`、`PromptPolicyInput`、`PromptPolicyConfig`，并扩展 `LLMInterfaceSurfaceTest` 覆盖治理 SPI、输入字段、默认 fail-closed 配置和决策一致性边界。
2. 测试目标：验证 `IPromptPolicy` 仍是纯抽象治理 SPI；`evaluate()` 明确消费 `PromptComposeResult` 与 `PromptPolicyInput`；`PromptPolicyDecision` 能稳定表达 Allow / Deny / OverBudget / RequireRecompose 四态且只在 Allow 时暴露 governed messages。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_llm`、`dasall_unit_tests`
   - `RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest`

## 6. 风险与回退

1. 本轮只冻结了 PromptPolicy 的接口面，没有落 redaction 规则、tool visibility diff 算法或 render budget 估算实现；若 018 在实现期发现还缺少治理输入维度，应先回到接口评审，而不是在实现阶段绕过 009 扩字段。
2. `PromptPolicyDecision` 当前把 `Allow` 作为唯一允许输出 governed messages 的 disposition；若后续实现需要在 `RequireRecompose` 场景保留半成品消息，必须先重新评审 009 的边界，而不是在实现中静默突破。
3. 009 故意不把 `PromptPolicyDecision` 升格为 shared contracts；若后续跨模块复用压力增大，必须等待 037 的 shared admission 评审，而不是直接把 module-local 对象复制进 contracts。