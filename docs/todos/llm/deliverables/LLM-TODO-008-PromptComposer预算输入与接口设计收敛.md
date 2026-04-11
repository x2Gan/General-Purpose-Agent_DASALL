# LLM-TODO-008 PromptComposer 预算输入与接口设计收敛

日期：2026-04-11
任务：LLM-TODO-008
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.5.4 已冻结 `IPromptComposer::init()` 与 `compose()` 两个入口，并明确 PromptComposer 只消费共享 `PromptComposeRequest` 与已选择的 `PromptRelease`，输出 `PromptComposeResult`，不得回写 memory 或改变 ContextPacket ownership。
2. 同一设计文档的 6.4.2 / 6.4.3 已将 [llm/include/prompt/ModelBudgetHint.h](../../../../llm/include/prompt/ModelBudgetHint.h) 与 [llm/include/prompt/PromptComposerConfig.h](../../../../llm/include/prompt/PromptComposerConfig.h) 定义为 module-local supporting type，字段分别收敛为 `context_window`、`max_output_tokens`、`reserved_output_tokens` 以及 `template_engine`、`max_few_shot_count`。
3. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.5.4 与 12.3 明确了 COMP-5 闭环结论：PromptComposer 必须显式接收模型预算提示，才能在不越权访问 ModelRouter/Provider Catalog 的前提下输出 estimated token 与 over-budget warning。
4. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 LLM-TODO-008 的完成判定收敛为“`compose()` 明确依赖 budget hint 且不回写 memory”，因此本轮只冻结 PromptComposer SPI 与 budget/config supporting types，不提前推进 TokenEstimator、TemplateRenderer 或 PromptComposer 实现逻辑。
5. [contracts/include/prompt/PromptComposeRequest.h](../../../../contracts/include/prompt/PromptComposeRequest.h)、[contracts/include/prompt/PromptComposeResult.h](../../../../contracts/include/prompt/PromptComposeResult.h)、[contracts/include/prompt/PromptRelease.h](../../../../contracts/include/prompt/PromptRelease.h) 已提供 Prompt 装配链共享对象；008 必须直接复用这些 contracts，而不是在 llm/include 下复制第二份 compose request/result/release 结构。
6. [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) 已作为 llm 公共接口冻结门出口，因此 008 继续在同一测试文件补齐 PromptComposer 签名、budget hint 字段与 config 边界断言，而不是新建并行 surface test。

## 2. 外部参考

1. C++ Core Guidelines 的 C.121 明确要求：“If a base class is used as an interface, make it a pure abstract class.” 本轮据此将 [llm/include/prompt/IPromptComposer.h](../../../../llm/include/prompt/IPromptComposer.h) 保持为纯抽象 SPI，仅暴露稳定接口签名，不夹带实现态状态。参考：https://raw.githubusercontent.com/isocpp/CppCoreGuidelines/master/CppCoreGuidelines.md

## 3. Design 结论

1. `IPromptComposer` 保持为 Prompt 消息装配面的纯抽象 SPI，签名冻结为 `init(const PromptComposerConfig&)` 与 `compose(const PromptComposeRequest&, const PromptRelease&, const ModelBudgetHint&) const`。
2. 008 只冻结 PromptComposer 的输入输出边界，不冻结模板渲染规则、few-shot 注入策略、budget warning 文案或 slot mapping 实现；这些实现细节继续留给 016/017/039。
3. `compose()` 直接复用共享 `PromptComposeRequest`、`PromptRelease` 和 `PromptComposeResult`，确保 Prompt 装配边界仍与 contracts 的共享语义一致，而不是在 llm/include 下引入第二套 provider-neutral prompt payload 对象。
4. `ModelBudgetHint` 在本轮冻结为三项预算字段：`context_window`、`max_output_tokens`、`reserved_output_tokens`。PromptComposer 只读取这些提示做预估与 warning，不因此获得模型路由选择权或 Provider Catalog ownership。
5. `PromptComposerConfig` 只冻结 `template_engine` 与 `max_few_shot_count` 两项初始化输入，表达 PromptComposer 的最小运行态配置面，不把 profile 投影、trusted source 或 allowlist 规则混入 008。
6. 008 明确把 over-budget 语义限定为“生成 warning / 信号”的接口能力，而不是在接口层定义自动裁剪或二次上下文重排；真正的治理结论继续由 009 的 PromptPolicy 输出。
7. `IPromptComposer` 的职责止于渲染级裁剪、模板槽位映射与 prompt payload 组装，不能回写 memory、不能重排 ContextPacket ownership、不能越过 llm 边界读取 ContextOrchestrator 的原始候选。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 PromptComposer SPI | [llm/include/prompt/IPromptComposer.h](../../../../llm/include/prompt/IPromptComposer.h) |
| 冻结模型预算提示对象 | [llm/include/prompt/ModelBudgetHint.h](../../../../llm/include/prompt/ModelBudgetHint.h) |
| 冻结 PromptComposer 初始化配置对象 | [llm/include/prompt/PromptComposerConfig.h](../../../../llm/include/prompt/PromptComposerConfig.h) |
| 在 llm 公共接口冻结测试中补齐 PromptComposer 边界断言 | [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |

## 5. Build 三件套

1. 代码目标：新增 `IPromptComposer`、`ModelBudgetHint`、`PromptComposerConfig`，并扩展 `LLMInterfaceSurfaceTest` 覆盖 compose 签名、预算提示字段和配置边界。
2. 测试目标：验证 `IPromptComposer` 仍是纯抽象装配 SPI；`compose()` 明确消费 `PromptComposeRequest`、`PromptRelease` 与 `ModelBudgetHint`；`ModelBudgetHint` 和 `PromptComposerConfig` 的字段能稳定表达预算和 renderer 初始化输入。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_llm`、`dasall_unit_tests`
   - `RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest`

## 6. 风险与回退

1. 本轮只冻结了 PromptComposer 的接口面，没有定义模板安全规则、槽位映射和 few-shot 注入细节；若 017/039 在实现期发现还缺少输入维度，应先回到设计评审，而不是在实现阶段临时扩展 `IPromptComposer`。
2. `ModelBudgetHint` 当前只承载预算上限，不承载 resolved route、provider pricing 或 profile 差异；若后续实现尝试把路由/计费信息直接塞入 008 supporting type，会破坏 PromptComposer 与 ModelRouter/Provider Catalog 的权责隔离。
3. 008 故意不在 `compose()` 返回类型中引入 policy disposition；若后续需要 Allow / Deny / OverBudget 等治理结论，必须继续通过 009 的 `PromptPolicyDecision` 收口，而不是让 PromptComposer 演化为第二个 policy center。