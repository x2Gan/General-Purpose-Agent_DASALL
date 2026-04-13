# LLM-TODO-030 DeepSeek 双模式 integration 设计收敛

日期：2026-04-13
任务：LLM-TODO-030
状态：D Gate PASS / B Gate TODO

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.6.6 已冻结 DeepSeek 双模式的 owner：`deepseek-chat` 与 `deepseek-reasoner` 必须建模为同一 provider instance 下的两个 model entry，由 `ModelRouter` 按复杂度、SLA、预算与 reasoning 需求做策略驱动选择，而不是在调用代码里硬编码分支。
2. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 9.3 已明确要求新增 `DeepSeekDualModeSelectionIntegrationTest`，用于验证“同一 provider 下 chat / reasoner 双模式按复杂度、SLA、预算切换”。
3. [tests/unit/llm/ModelRouterReasoningModeSelectionTest.cpp](../../../../tests/unit/llm/ModelRouterReasoningModeSelectionTest.cpp) 已证明路由评分规则本身成立：显式 reasoning 工作负载会选择 `deepseek-prod/deepseek-reasoner`，interactive + hard-cap + tools 的工作负载会降档到 `deepseek-prod/deepseek-chat`。
4. [tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp](../../../../tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp) 已在 029 建立真实 llm smoke 基座：真实 `PromptPipeline + LLMManager + ModelRouter + MockLLMAdapter + ResponseNormalizer` 能在 integration 层打通，并把 `selection_reason`、`reasoning_mode_*`、`route`、`prompt`、`usage` 等字段投影到 response tags、structured log、metrics 与 trace。
5. [llm/assets/providers/deepseek/models.yaml](../../../../llm/assets/providers/deepseek/models.yaml) 与 [tests/unit/llm/ModelRouterTestSupport.h](../../../../tests/unit/llm/ModelRouterTestSupport.h) 已冻结 dual-mode 元数据：`deepseek-chat` 是 `reasoning_mode = non_thinking`、`tier_family = default`，`deepseek-reasoner` 是 `reasoning_mode = thinking`、`tier_family = reasoning`，且 reasoner 的 `response_private_fields` 包含 `reasoning_content`。
6. [llm/src/route/ModelRouter.cpp](../../../../llm/src/route/ModelRouter.cpp) 与 [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 暴露了 030 的最小可验收证据：reasoning 升档依赖 `requires_reasoning`、`reasoning_task_bias`、`visible_reasoning_preferred`，chat 降档依赖 `tier_degraded`、`interactive_latency_bias`、`budget_low_cost`、`interactive_hard_cap_downgrade`，这些 reason code 会进入 response tags 和 observability 摘要。

## 2. 外部参考

1. DeepSeek 官方 reasoning model 文档明确说明：`deepseek-reasoner` 会返回 `reasoning_content` 与 `content` 两个层次，其中 `reasoning_content` 只用于查看、展示或蒸馏；如果把 `reasoning_content` 回灌到下一轮输入，API 会返回 400。这直接支撑 DASALL 对 “thinking mode provider-private 输出必须在 normalizer 后剥离” 的边界约束。参考：https://api-docs.deepseek.com/guides/reasoning_model

## 3. Design 结论

1. 030 继续复用 029 的真实 smoke 基座，不再引入 fake pipeline 或只测 `ModelRouter` 的轻量夹具；dual-mode integration 的完成证据必须来自真实 `LLMManager` 闭环，而不是单测级别的 route resolve 结果。
2. 本轮至少覆盖两个正例：
   - reasoning 升档：复杂规划/诊断类请求在 `requires_reasoning = true`、`prefers_visible_reasoning = true` 下选择 `deepseek-reasoner`。
   - chat 降档：interactive + hard-cap + tools 的请求在候选模型都可用时，从 reasoning tier 降档到 `deepseek-chat`。
3. 为了避免“chat 之所以被选中只是因为 reasoner 不可用”的伪通过，integration fixture 需要像 `ModelRouterReasoningModeSelectionTest` 那样把 test catalog 中 reasoner 的 tools verification 调整为 verified，使 chat 路径真正体现复杂度/SLA/预算触发的降档，而不是 capability reject。
4. dual-mode 的验收证据除了 `resolved_route` 之外，还必须验证 reason code 可解释性：至少把关键 `selection_reason=` tags、structured log 中的 `selection_reason_codes`、以及 trace attrs 中的 `reasoning_mode_requested/effective` 断言完整保留下来。
5. thinking mode 的 `reasoning_content` 仍然只能作为 provider-private 侧信道存在。030 允许通过 `ResponseNormalizer` 继续把它剥离为 `audit=reasoning_content_stripped` 与 `reasoning_content_stripped=true` 事实，但不把原始内容写回 shared `LLMResponse` 或下一轮请求。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| Dual-mode integration fixture 与双正例断言 | [tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp](../../../../tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp) |
| integration test 注册 | [tests/integration/llm/CMakeLists.txt](../../../../tests/integration/llm/CMakeLists.txt) |
| 030 证据与状态回写 | [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) |

## 5. Build 三件套

1. 代码目标：新增 `DeepSeekDualModeSelectionIntegrationTest`，在真实 llm integration 基座中验证 reasoning 升档与 chat 降档两条 dual-mode 路径，并把 `selection_reason_codes`、`reasoning_mode_requested/effective`、`reasoning_content_stripped` 等字段保留在可观测输出中。
2. 测试目标：
   - 正例 1：`requires_reasoning + prefers_visible_reasoning` 选择 `deepseek-prod/deepseek-reasoner`
   - 正例 2：`interactive + hard_cap + requires_tools` 选择 `deepseek-prod/deepseek-chat`
   - 复用既有负向边界：reasoning 响应中的 `reasoning_content` 仍被 normalizer 剥离，不进入 shared response payload
3. 验收命令：
   - `Build_CMakeTools` 构建目标 `dasall_deepseek_dual_mode_selection_integration_test`
   - `RunCtest_CMakeTools` 运行 `DeepSeekDualModeSelectionIntegrationTest`
   - `ListTests_CMakeTools`

## 6. 风险与回退

1. 若 dual-mode integration 依赖的 reasoner tools verification 仍保持 `needs_integration_validation`，interactive 降档路径会退化成 capability 过滤而不是策略降档。测试内可最小调整 catalog snapshot 的 verification state，但不能把生产资产静默改成 verified。
2. 若实现过程中发现 029 的 smoke 夹具代码复用成本过高，可以抽出 module-local 的 integration test support header；但 support 抽取必须服务于当前 030 用例，不得演变成跨子系统通用测试框架重构。
3. 若 `DeepSeekDualModeSelectionIntegrationTest` 只能证明 route resolve 而无法证明 reason code 与 observability 字段保留，则本轮只能记为部分完成，不能把 030 标为 Done。