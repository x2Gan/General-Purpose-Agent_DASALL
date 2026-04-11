# LLM-TODO-016 TokenEstimator 预估器设计收敛

日期：2026-04-11
任务：LLM-TODO-016
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.15.7 已将 TokenEstimator 收敛为“预调用阶段对输入 token 做预估的唯一工具组件”，明确它只输出 `TokenEstimate`，不替代 provider usage，也不直接发起 provider 调用。
2. 同一设计文档要求 v1 阶段允许采用字符换算近似：英文约 `1 token ≈ 4 chars`，中文约 `1 token ≈ 1.5 chars`，并附加默认 `5%` 安全余量；这与现有 [llm/include/TokenEstimate.h](../../../../llm/include/TokenEstimate.h) 中 `safety_margin = 0.05` 的 supporting type 默认值一致。
3. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 016 的完成判定冻结为“`TokenEstimate` 计算、`over_budget` 判定和安全余量可自动化断言”，同时把 `PromptComposerOverBudgetTest` 与 `ModelRouterPolicyTest` 写入验收命令，要求本轮至少提供可消费 `TokenEstimate` 的测试锚点。
4. 旧指引 [docs/todos/llm/DASALL_llm子系统TODO落地实施步骤指引.md](../DASALL_llm子系统TODO落地实施步骤指引.md) 的 T-024 只要求 `TokenEstimatorTest`，而专项 TODO 额外把下游消费测试纳入 016 验收。因此本轮除了 `TokenEstimatorTest` 外，还需要最小化补入 `PromptComposerOverBudgetTest` 与 `ModelRouterPolicyTest` 的消费锚点，避免专项 TODO 的验收命令悬空。
5. 016 最终落地了 [llm/src/TokenEstimator.h](../../../../llm/src/TokenEstimator.h)、[llm/src/TokenEstimator.cpp](../../../../llm/src/TokenEstimator.cpp)、[tests/unit/llm/TokenEstimatorTest.cpp](../../../../tests/unit/llm/TokenEstimatorTest.cpp)、[tests/unit/llm/PromptComposerOverBudgetTest.cpp](../../../../tests/unit/llm/PromptComposerOverBudgetTest.cpp)、[tests/unit/llm/ModelRouterPolicyTest.cpp](../../../../tests/unit/llm/ModelRouterPolicyTest.cpp)，并更新 [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt)。

## 2. 外部参考

1. OpenAI 关于 token 计数的说明给出英文文本的常见经验值 `1 token ≈ 4 chars`，并强调非英语文本通常会有更高的 token/character 比率。016 直接采用这一经验值作为英文粗估基线，并对中文使用更保守的 `1.5 chars/token` 比例。参考：https://help.openai.com/en/articles/4936856-what-are-tokens-and-how-to-count-them
2. OpenAI Tokenizer 页面同样将“常见英文文本约 4 字符 / token”作为实用规则，并将精确 tokenizer 能力定位为更高成本但可替换的实现路线。016 依此保持 v1 为纯离线启发式实现，同时通过 `TokenEstimatorConfig` 给未来更精确 tokenizer 接入预留扩展位置。参考：https://platform.openai.com/tokenizer

## 3. Design 结论

1. TokenEstimator 在本轮保持 module-local 内部组件，不引入新的 public interface；实现落位采用 [llm/src/TokenEstimator.h](../../../../llm/src/TokenEstimator.h) 与 [llm/src/TokenEstimator.cpp](../../../../llm/src/TokenEstimator.cpp)，以便同时服务 PromptComposer 与 ModelRouter 两个未来消费者，而不把它局限在 prompt 子目录。
2. v1 预估算法固定为 UTF-8 码点级字符分类：ASCII 按 `4 chars/token` 估算，CJK 按 `1.5 chars/token` 估算，其余非 ASCII / 非 CJK 字符按 `2 chars/token` 的保守比例估算，再统一乘以可配置安全余量并向上取整。
3. `TokenEstimator` 同时支持单段文本与消息列表输入；消息列表路径对每条消息分别预估后求和，不擅自加入 provider 私有开销常量，从而保持它只是 provider-neutral 的预调用预算器。
4. `TokenEstimate.over_budget` 的唯一判定规则是 `estimated_input_tokens + reserved_output_tokens > context_window`。TokenEstimator 只负责设置这项事实，不负责执行裁剪、recompose 或 route fallback。
5. 为了闭合专项 TODO 的验收命令，本轮增加了 `PromptComposerOverBudgetTest` 与 `ModelRouterPolicyTest` 两个最小消费型测试锚点：前者只验证 `ModelBudgetHint` 驱动下的 over-budget 判定，后者只验证不同 `context_window` 候选如何消费同一个 `TokenEstimate`。这两个测试不会提前实现 017/020 的组件逻辑，而是为后续任务保留可扩展的测试壳。
6. 若 `TokenEstimatorConfig` 被传入非法比例或负安全余量，016 选择回退到默认配置而不是抛异常，以延续 llm 子系统当前的“非异常错误面优先”风格。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 落地 TokenEstimator 内部组件与启发式配置 | [llm/src/TokenEstimator.h](../../../../llm/src/TokenEstimator.h)、[llm/src/TokenEstimator.cpp](../../../../llm/src/TokenEstimator.cpp) |
| 覆盖空输入、英文/中文混合、配置安全余量 | [tests/unit/llm/TokenEstimatorTest.cpp](../../../../tests/unit/llm/TokenEstimatorTest.cpp) |
| 为 017 的 over-budget 行为准备消费锚点 | [tests/unit/llm/PromptComposerOverBudgetTest.cpp](../../../../tests/unit/llm/PromptComposerOverBudgetTest.cpp) |
| 为 020 的上下文窗口硬过滤准备消费锚点 | [tests/unit/llm/ModelRouterPolicyTest.cpp](../../../../tests/unit/llm/ModelRouterPolicyTest.cpp) |
| 将新实现与三条测试接入 llm / unit 聚合目标 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：实现 `TokenEstimator`，输出稳定的 `TokenEstimate`，支持文本/消息列表输入、默认 5% 安全余量和 `over_budget` 判定。
2. 测试目标：`TokenEstimatorTest` 覆盖空输入、中英混合和自定义安全余量；`PromptComposerOverBudgetTest` 与 `ModelRouterPolicyTest` 作为最小消费锚点验证 `TokenEstimate` 能被未来下游组件直接消费。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 运行 `TokenEstimatorTest`
   - `RunCtest_CMakeTools` 运行 `PromptComposerOverBudgetTest`
   - `RunCtest_CMakeTools` 运行 `ModelRouterPolicyTest`

## 6. 风险与回退

1. 016 当前仍是纯启发式预估，不做 tiktoken-compatible 精确 tokenizer 接入；若后续需要更高精度，应在保持 `TokenEstimator` 调用面稳定的前提下替换内部实现，而不是把 tokenizer 依赖直接散落到 PromptComposer/ModelRouter。
2. 本轮新增的 `PromptComposerOverBudgetTest` 与 `ModelRouterPolicyTest` 只是最小消费锚点，后续 017/020 需要在同名测试文件内继续扩展到真实组件行为断言，而不是另起平行测试名导致验收出口分裂。
3. `TokenEstimator` 当前没有额外计入 provider 私有 message framing 开销，因此只适合作为治理前的保守近似值；一旦未来 provider family 需要更具体的 framing 系数，也应由 ModelRouter/adapter metadata 提供输入，而不是让 TokenEstimator 直接拥有 provider 私有分支。