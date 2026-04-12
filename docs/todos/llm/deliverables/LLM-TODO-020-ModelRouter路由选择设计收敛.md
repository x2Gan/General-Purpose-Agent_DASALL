# LLM-TODO-020 ModelRouter 路由选择设计收敛

日期：2026-04-13
任务：LLM-TODO-020
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.15.1 已把 ModelRouter 冻结为 llm 内部唯一的模型挡位选择 owner，并明确要求固定执行 `候选集装配 -> 硬过滤 -> 确定性评分 -> fallback chain 展开` 四步，且不得越过 `profile/degrade_policy` 私自追加 hop。
2. 同一设计文档的 6.6.6、6.6.7 与 6.10.5 已给出 DeepSeek chat/reasoner 的 vendor-neutral 抽象边界：`deepseek-chat` / `deepseek-reasoner` 属于同一 provider instance 下的多个 model entry，`reasoning_content` 是 provider-private 字段，在线主链路禁止再额外发起一次 LLM 推理来决定路由，route 选择必须由显式 `ModelSelectionHint` 和 catalog 元数据驱动。
3. [llm/include/route/ResolvedModelRoute.h](../../../../llm/include/route/ResolvedModelRoute.h)、[llm/include/route/ModelSelectionHint.h](../../../../llm/include/route/ModelSelectionHint.h)、[llm/include/LLMSubsystemConfig.h](../../../../llm/include/LLMSubsystemConfig.h) 与 [llm/src/provider/ProviderCatalogRepository.h](../../../../llm/src/provider/ProviderCatalogRepository.h) 已在 011、012、014 冻结了 020 可以消费的 supporting types、profile 投影视图和 immutable Provider Catalog snapshot；当前 020 不需要也不允许去扩 shared ModelRoute。
4. [profiles/cloud_full/runtime_policy.yaml](../../../../profiles/cloud_full/runtime_policy.yaml) 等 profile 资产已经证明 route 输入是 `cloud.reasoning`、`cloud.general`、`lan.general`、`local.small` 这类抽象 route 名，而不是具体 provider/model 名。因此 020 的核心不是“直接返回 profile 里的 route 字符串”，而是把抽象 route 偏好映射到当前 profile 允许的具体 `provider_id/model_id`。
5. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 已把 020 的完成判定冻结为“路由选择稳定、可解释且不绕过 profile/degrade_policy”，测试门要求同时覆盖 Policy、Fallback、ReasoningModeSelection、Stability 四组单测。
6. 020 本轮最终落盘了 [llm/src/route/ModelRouter.h](../../../../llm/src/route/ModelRouter.h)、[llm/src/route/ModelRouter.cpp](../../../../llm/src/route/ModelRouter.cpp)、[tests/unit/llm/ModelRouterTestSupport.h](../../../../tests/unit/llm/ModelRouterTestSupport.h)、[tests/unit/llm/ModelRouterPolicyTest.cpp](../../../../tests/unit/llm/ModelRouterPolicyTest.cpp)、[tests/unit/llm/ModelRouterFallbackTest.cpp](../../../../tests/unit/llm/ModelRouterFallbackTest.cpp)、[tests/unit/llm/ModelRouterReasoningModeSelectionTest.cpp](../../../../tests/unit/llm/ModelRouterReasoningModeSelectionTest.cpp)、[tests/unit/llm/ModelRouterStabilityTest.cpp](../../../../tests/unit/llm/ModelRouterStabilityTest.cpp)，并更新了 [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt)。

## 2. 外部参考

1. DeepSeek 官方 [Reasoning Model (`deepseek-reasoner`)](https://api-docs.deepseek.com/guides/reasoning_model) 文档明确给出两条与 020 直接相关的工程事实：`reasoning_content` 是与最终 `content` 并列的 provider-private 输出字段，且在多轮对话里不得回拼进下一轮输入；同时该页面明确标记 `deepseek-reasoner` 不支持 Function Calling。这直接支持了 020 的两个设计结论：reasoner/chat 必须建模为可解释的路由选择，而不是在调用代码里硬编码切换；当 `requires_tools=true` 时，未完成工具验证的 reasoning candidate 必须被硬过滤或降档。
2. OpenRouter 官方 [Provider Routing](https://openrouter.ai/docs/features/provider-routing) 文档给出的默认策略是：先过滤不满足参数/能力约束的候选，再按显式排序策略与 fallback 顺序做确定性路由，而不是随机选一个“差不多”的后端。020 借鉴的是其中“先能力过滤，再排序，再 fallback”的工程结构，而不是其 price-based load balancing；DASALL 仍然坚持由 profile route envelope 和 degrade_policy 控制候选范围，不引入运行时负载均衡随机性。

## 3. Design 结论

1. 020 在 [llm/src/route/ModelRouter.h](../../../../llm/src/route/ModelRouter.h) 中新增 module-local `ModelRouter` concrete owner，`init()` 只接受 `LLMSubsystemConfig` 投影视图，`resolve()` 则显式消费 `ModelSelectionHint`、immutable `ProviderCatalogSnapshot` 与注入式 `ModelRouterHealthSnapshot`。这样 020 已为 021 预留健康快照注入点，但不提前引入 AdapterRegistry。
2. 路由输入继续保留 profile 里的抽象 route 名，例如 `cloud.reasoning`、`cloud.general`、`lan.general`、`local.small`；但 `resolve()` 的输出不再停留在抽象 route 层，而是落成具体 `provider_id/model_id` 形式的 `ResolvedModelRoute.primary_route` 与 `fallback_routes`。这样 020 既保持了 profile 的 vendor-neutral route 面，又为后续 021/024 提供了可直接消费的具体 route 键。
3. 候选集装配分两层进行：第一层按 `stage_route.route`、`fallback_route` 与 `degrade_policy.fallback_chain` 生成 route envelope；第二层按 provider locality tag、provider activation、provider trusted source、model summary verification_state 与 health snapshot 过滤实际 catalog entries。`fallback_route` 始终属于 stage 的显式恢复路径；`degrade_policy.fallback_chain` 仅在 `allow_model_failover=true`，或 `allow_budget_degrade=true` 且本次请求确实是 `hard_cap` 预算场景时才会继续展开。
4. 硬过滤在 020 中已固定为 fail-closed：上下文窗口不足、目标输出长度超过 `max_output_tokens_hard_limit`、`requires_tools=true` 但 tools 未 verified、`requires_reasoning=true` 但候选不支持 reasoning、provider source 不受信、summary verification_state 为 blocked，或 health snapshot 标记为 blocked 时，候选直接淘汰，不进入评分。
5. 020 的评分函数坚持 deterministic 而非 load balancing：先给 route envelope 顺序一个重权重，再叠加 preferred tier match / adjacent tier、stage/task 的 reasoning 倾向、interactive SLA、hard-cap budget、tool requirement、visible reasoning preference、大输出支持、长上下文摘要场景、verification 状态、previous_route_failures 与 health failure penalty。为满足 6.6.6 的“interactive 或 hard_cap 可降档到 chat”要求，020 额外显式引入了 `interactive + hard_cap + reasoning 非必需` 的降档偏置，避免 planner 类 workload 永远因为 stage 偏好把请求钉死在 reasoning tier。
6. 020 的 reason code 只保留在 module-local `ModelRouterResolveResult.selection_reason_codes` 中，不向 shared contracts 外溢。它记录的是本次最终选择为何成立，例如 `selected_primary_route`、`selected_from_fallback_chain`、`preferred_tier_match`、`tier_upgraded`、`tier_degraded`、`interactive_latency_bias`、`budget_low_cost`、`interactive_hard_cap_downgrade`、`requires_reasoning` 等。这满足了 observability 需要的“可解释”，但不提前冻结跨模块 ABI。
7. 稳定性规则在 020 中固定为：按 `score desc -> route_order asc -> route_id lexicographic asc` 排序。同分情况下使用 route id 做最终 tie-break，从而保证重复调用不会因为容器遍历顺序或 map/hash 偶然性改变 primary/fallback 顺序。
8. 020 没有在代码中硬编码任何“除了 DeepSeek 以外的特殊厂商分支”。DeepSeek 只作为 catalog 数据和单测场景出现；真正进入评分的是 `tier_family`、`latency_tier`、`cost_tier`、`reasoning_depth_tier`、feature verification_state 与 locality tags。这保持了设计文档要求的 vendor-neutral tier traits 抽象。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 落地 ModelRouter concrete owner、health snapshot 注入面与 route resolve 结果对象 | [llm/src/route/ModelRouter.h](../../../../llm/src/route/ModelRouter.h)、[llm/src/route/ModelRouter.cpp](../../../../llm/src/route/ModelRouter.cpp) |
| 把 profile 的抽象 route envelope 映射到具体 `provider_id/model_id` 路由 | [llm/src/route/ModelRouter.cpp](../../../../llm/src/route/ModelRouter.cpp) |
| 固定候选集装配、硬过滤、确定性评分与 fallback chain 展开顺序 | [llm/src/route/ModelRouter.cpp](../../../../llm/src/route/ModelRouter.cpp) |
| 覆盖上下文/输出硬过滤、tools 验证降档、fallback 顺序、双模式切换与稳定性 | [tests/unit/llm/ModelRouterPolicyTest.cpp](../../../../tests/unit/llm/ModelRouterPolicyTest.cpp)、[tests/unit/llm/ModelRouterFallbackTest.cpp](../../../../tests/unit/llm/ModelRouterFallbackTest.cpp)、[tests/unit/llm/ModelRouterReasoningModeSelectionTest.cpp](../../../../tests/unit/llm/ModelRouterReasoningModeSelectionTest.cpp)、[tests/unit/llm/ModelRouterStabilityTest.cpp](../../../../tests/unit/llm/ModelRouterStabilityTest.cpp) |
| 统一测试夹具里的 provider/model snapshot 与 route config 生成 | [tests/unit/llm/ModelRouterTestSupport.h](../../../../tests/unit/llm/ModelRouterTestSupport.h) |
| 将 020 实现与四条新用例接入 llm / unit 聚合目标 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：实现 `ModelRouter`，完成抽象 route envelope 到具体 provider/model route 的解析，并落地 health/verification/context/output/tools/reasoning 的硬门禁与可解释评分。
2. 测试目标：
   - `ModelRouterPolicyTest`：覆盖 context window / output hard limit 双硬过滤，以及未验证 reasoning-tools 候选降档到 chat。
   - `ModelRouterFallbackTest`：覆盖 explicit `fallback_route` 优先于 degrade chain、以及 `allow_model_failover=false` 时不展开 degrade chain。
   - `ModelRouterReasoningModeSelectionTest`：覆盖 reasoning-first workload 选 reasoner、interactive + hard_cap + reasoning 非必需 workload 降档到 chat。
   - `ModelRouterStabilityTest`：覆盖同分候选的稳定 tie-break 和重复调用稳定性。
3. 验证动作：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 运行 `ModelRouterPolicyTest`
   - `RunCtest_CMakeTools` 运行 `ModelRouterFallbackTest`
   - `RunCtest_CMakeTools` 运行 `ModelRouterReasoningModeSelectionTest`
   - `RunCtest_CMakeTools` 运行 `ModelRouterStabilityTest`

## 6. 风险与回退

1. 020 里的 `ModelRouterHealthSnapshot` 仍是 route owner 的 module-local 输入类型；这正是当前阶段需要的最小注入面。021 若要把 health 快照与 AdapterRegistry 绑定，只应在 llm 内部扩展 copy-on-write snapshot owner，不应把这套结构提前推到 shared contracts。
2. 当前 concrete route string 采用 `provider_id/model_id` 形式，足以支撑 020/021/024 的单元级编排。如果后续调用治理还需要 adapter instance id、base_url alias 或 deployment source version，应继续在 module-local route result 上补充，而不是回退到 profile 抽象 route 字符串。
3. 012 当前并没有单独投影“模型 allowlist”字段，所以 020 现阶段能强制执行的是 `stage_routes + fallback_route + degrade_policy` 构成的 route envelope、provider source trust 与 model verification_state；更细粒度的 instance allowlist 若确有需要，应在 041 的 ProviderConfig 投影或后续 profile schema 中显式补面，而不是在 ModelRouter 内部偷藏新策略输入。