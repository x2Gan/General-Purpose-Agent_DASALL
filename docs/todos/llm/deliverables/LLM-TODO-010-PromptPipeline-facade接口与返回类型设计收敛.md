# LLM-TODO-010 PromptPipeline facade 接口与返回类型设计收敛

日期：2026-04-11
任务：LLM-TODO-010
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.5.6 已明确 `IPromptPipeline` 是 Prompt 三段治理的统一 facade，只负责 `select -> compose -> evaluate` 的固定顺序编排，不承担模型调用、memory 访问或治理逻辑扩张。
2. 同一设计文档的 6.4.2 / 6.4.3 已将 [llm/include/prompt/PromptPipelineConfig.h](../../../../llm/include/prompt/PromptPipelineConfig.h) 与 [llm/include/prompt/PromptPipelineResult.h](../../../../llm/include/prompt/PromptPipelineResult.h) 定位为 module-local supporting types，分别承载 facade 的 init 配置和 run 返回类型。
3. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.7.1 约束 Runtime 默认通过 `IPromptPipeline.run()` 加 `ILLMManager.generate()` 的两步模式工作，因此 010 必须把 llm 对 Runtime 的 Prompt 编排出口收敛成单一 facade，而不能把内部步骤顺序泄漏回 Runtime。
4. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 7.1 映射表把 `IPromptPipeline` 与 `InterfaceSurfaceTest` 明确挂到 LLM-D1，因此 010 的 Build 面仍应以公共头文件冻结和接口面单测为主，而不是提前进入 PromptPipeline 实现。
5. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 010 的完成判定写死为“pipeline 接口固定表达 select -> compose -> evaluate 且不引入模型调用”，因此本轮不能把 `LLMGenerateRequest`、provider routing 或 runtime recovery 参数塞进 `run()`。

## 2. 外部参考

1. C++ Core Guidelines 的 C.121 要求接口基类保持纯抽象，只暴露纯虚函数和默认虚析构。因此 [llm/include/prompt/IPromptPipeline.h](../../../../llm/include/prompt/IPromptPipeline.h) 在本轮继续保持纯 abstract facade，不夹带状态字段或默认实现。参考：https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
2. Facade 模式的核心价值在于对外隐藏子系统内部调用顺序，同时不吞掉子系统边界。010 据此把 PromptRegistry、PromptComposer、PromptPolicy 的编排聚合到单一入口，但仍保留分步 SPI 作为高级用法，不把模型调用合并进来。参考：https://refactoring.guru/design-patterns/facade

## 3. Design 结论

1. `IPromptPipeline` 被冻结为 Prompt 三段治理的统一 facade，签名仅保留 `init(const PromptPipelineConfig&)` 与 `run(const PromptQuery&, const PromptComposeRequest&, const PromptPolicyInput&) const` 两个入口。
2. `PromptPipelineConfig` 只承载三段组件的 init 配置聚合：`PromptRegistryConfig`、`PromptComposerConfig`、`PromptPolicyConfig`。本轮不向其引入运行态路由、provider 或模型调用参数。
3. `PromptPipelineResult` 只承载三段编排产物与终态：`disposition`、`compose_result`、`policy_decision`、`registry_result`、`reason`。其中 `compose_result` 仍保持 shared contract 类型，`policy_decision` 与 `registry_result` 继续保持 module-local。
4. `run()` 的输入面只接受 `PromptQuery`、共享 `PromptComposeRequest` 与 `PromptPolicyInput`，从接口层阻止 pipeline 越权吸纳 `LLMGenerateRequest`、memory handle、recovery token 或 provider execution 参数。
5. facade 的职责止于固定表达 `select -> compose -> evaluate` 编排，并把 `OverBudget` 等治理结果透传给 Runtime；模型调用仍由 `ILLMManager` 负责，ADR-006/007/008 的主控边界不在本轮被改写。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 PromptPipeline facade SPI | [llm/include/prompt/IPromptPipeline.h](../../../../llm/include/prompt/IPromptPipeline.h) |
| 冻结 facade init 配置聚合类型 | [llm/include/prompt/PromptPipelineConfig.h](../../../../llm/include/prompt/PromptPipelineConfig.h) |
| 冻结 facade 运行结果类型 | [llm/include/prompt/PromptPipelineResult.h](../../../../llm/include/prompt/PromptPipelineResult.h) |
| 在 llm 公共接口冻结测试中补齐 Pipeline 断言 | [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |

## 5. Build 三件套

1. 代码目标：新增 `IPromptPipeline`、`PromptPipelineConfig`、`PromptPipelineResult`，并扩展 `LLMInterfaceSurfaceTest` 覆盖 facade SPI、三段配置聚合以及 run 返回结果类型。
2. 测试目标：验证 `IPromptPipeline` 仍是纯抽象 facade；`run()` 只接受 select / compose / policy 三段输入；`PromptPipelineResult` 能稳定表达 `Allow` 与 `OverBudget` 等终态，同时不夹带模型调用输入对象。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_llm`、`dasall_unit_tests`
   - `RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest`

## 6. 风险与回退

1. 本轮只冻结了 facade 的接口面，没有落 `PromptPipeline` 的实现类、失败码映射或组件注入方式；若 019 在实现阶段发现仍需额外运行态字段，应先回到接口评审，而不是直接扩大 010 的签名。
2. `PromptPipelineResult` 当前仅表达三段编排产物，不承载模型请求对象；若后续确有需要把 pipeline 直接连到模型调用，必须先重审 6.5.6 对“llm 核心公共接口 ≤ 3”的约束，而不是在实现阶段绕开 010。
3. facade 聚合并不意味着 Runtime 失去分步编排能力；若未来 over-budget 回流需要保留更多中间态，也应优先在 `PromptPipelineResult` 中评审新增字段，而不是让 Runtime 再次硬编码 llm 内部顺序。
