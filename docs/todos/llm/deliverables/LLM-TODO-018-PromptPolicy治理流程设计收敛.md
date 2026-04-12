# LLM-TODO-018 PromptPolicy 治理流程设计收敛

日期：2026-04-12
任务：LLM-TODO-018
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.5.5 与 6.15.3 已冻结 PromptPolicy 的职责边界：它是 `PromptComposeResult` 到实际发送前的最后一道治理门，必须按 `trusted source -> allowlist -> tool visibility -> redaction -> render budget` 的固定顺序裁定，并且 over-budget 只能返回 `OverBudget` 回流 Runtime，不能在 llm 内自行做二次语义裁剪。
2. 同一设计文档在 supporting types 表和 `IPromptPolicy` 章节中把 [llm/include/prompt/PromptPolicyInput.h](../../../../llm/include/prompt/PromptPolicyInput.h) 固定为 module-local 策略输入，承载 profile 投影的 allowlist、trusted sources、tool visibility。018 的 direct blocker 在于：旧版 `PromptPolicyInput` 只有 profile 侧静态维度，缺少 `selected_release_scope`、`selected_trusted_source` 与 `visible_tools`，导致 Policy 无法对“本次实际选中的 Prompt release / trusted source / 可见工具集合”做最终裁定。
3. [llm/src/LLMSubsystemConfig.cpp](../../../../llm/src/LLMSubsystemConfig.cpp) 与 [profiles/cloud_full/runtime_policy.yaml](../../../../profiles/cloud_full/runtime_policy.yaml)、[profiles/edge_minimal/runtime_policy.yaml](../../../../profiles/edge_minimal/runtime_policy.yaml) 已证明 PromptPolicy 的 allowlist / trusted source / tool visibility 必须来自 profile 投影视图，而不是硬编码在 llm 实现中。018 的 profile 差异测试也直接复用了这组策略差异：`cloud_full` 风格配置允许 `canary + infra_config + mcp:trusted`，`edge_minimal` 只允许 `stable + profiles + builtin:essential`。
4. [llm/include/prompt/PromptPolicyDecision.h](../../../../llm/include/prompt/PromptPolicyDecision.h) 已冻结 `Allow / Deny / OverBudget / RequireRecompose` 四态治理输出，并要求只有 `Allow` 路径可以带 `governed_messages`。因此 018 必须把 deny、tool visibility mismatch、redaction 和记账原因统一收敛到 `PromptPolicyDecision`，而不是再造第二套结果对象。
5. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 已把 018 的完成判定冻结为“严格按固定顺序治理，缺少 allowlist / trusted source 时默认拒绝”，验收出口固定为 `PromptPolicyAllowlistTest`、`PromptPolicyToolVisibilityTest` 与 `PromptPolicyProfileDiffTest`。
6. 018 最终落地了 [llm/src/prompt/PromptPolicy.h](../../../../llm/src/prompt/PromptPolicy.h)、[llm/src/prompt/PromptPolicy.cpp](../../../../llm/src/prompt/PromptPolicy.cpp)、[tests/unit/llm/PromptPolicyAllowlistTest.cpp](../../../../tests/unit/llm/PromptPolicyAllowlistTest.cpp)、[tests/unit/llm/PromptPolicyToolVisibilityTest.cpp](../../../../tests/unit/llm/PromptPolicyToolVisibilityTest.cpp)、[tests/unit/llm/PromptPolicyProfileDiffTest.cpp](../../../../tests/unit/llm/PromptPolicyProfileDiffTest.cpp)，并更新了 [llm/include/prompt/PromptPolicyInput.h](../../../../llm/include/prompt/PromptPolicyInput.h)、[llm/src/LLMSubsystemConfig.cpp](../../../../llm/src/LLMSubsystemConfig.cpp)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp)、[llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt)。

## 2. Design 结论

1. 018 在 [llm/src/prompt/PromptPolicy.h](../../../../llm/src/prompt/PromptPolicy.h) 内新增 module-local `PromptPolicy` concrete owner，实现 `IPromptPolicy`，并通过构造注入 [llm/src/TokenEstimator.h](../../../../llm/src/TokenEstimator.h) 以复用 016 的统一 token 预算事实源。
2. 治理顺序在 [llm/src/prompt/PromptPolicy.cpp](../../../../llm/src/prompt/PromptPolicy.cpp) 内被固定为：先验 `selected_trusted_source` 是否存在且属于 profile 允许集合；再验 `allowed_prompt_releases` 是否允许本次实际选中的 release identity / prompt id / version / release scope；再校验 `visible_tools` 是否与 `tool_visibility_rules` 一致；最后做 redaction 与 render budget 复核。这样 018 与 6.15.3 的顺序约束一一对齐。
3. 018 的 direct blocker fix 仅扩展 module-local [llm/include/prompt/PromptPolicyInput.h](../../../../llm/include/prompt/PromptPolicyInput.h)：新增 `selected_release_scope`、`selected_trusted_source` 与 `visible_tools`。这使 Policy 能消费 015 的选择结果和 Tool Policy Gate 的可见工具事实，同时避免反向扩张 shared prompt contracts。
4. tool visibility 规则收敛为两类安全语法：`domain:policy` 和 `tool_id=policy`。若本次 `visible_tools` 找不到匹配规则，或命中 `hidden` / `none` / `blocked` 一类拒绝性策略，Policy 返回 `RequireRecompose` 并产出 `hide:<tool>` patch；若规则本身格式非法，则直接 `Deny`。Policy 只做一致性校验与 patch 输出，不授予工具执行权限。
5. 敏感片段治理保持 provider-neutral：当前 redaction 会对 `secret://`、`token=`、`api_key=`、`password=`、`bearer ` 等高风险字样做文本级替换，并把 redaction 标签记入 `PromptPolicyDecision.redactions`。随后使用 `TokenEstimator` 对 redacted payload 重新估算 render budget；只有 redaction 后仍超限时才返回 `OverBudget`，否则允许继续发送。这保证了“redaction 后长度变化”测试门真实可执行。
6. 018 保持 fail-closed：若 trusted source 过滤集合缺失、`selected_trusted_source` 缺失、实际 source 不在 allowlist 内，或 allowed prompt releases 缺失且配置要求缺省拒绝，都会直接 `Deny`。Allow 路径才会返回 redacted `governed_messages`。
7. 018 没有改动 shared contracts，也没有让 PromptPolicy 读取 memory 原始候选、重排 ContextPacket 或越权发起 provider 调用；它仍然只是 Prompt 发送前的治理 owner。唯一遗留的 per-request 事实传递问题留给 019：PromptPipeline 需要把 Registry/Composer 产出的 selected metadata 和 visible tools 真实传入 Policy，而不是长期停留在测试注入态。

## 3. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 落地 PromptPolicy module-local owner 与固定治理顺序 | [llm/src/prompt/PromptPolicy.h](../../../../llm/src/prompt/PromptPolicy.h)、[llm/src/prompt/PromptPolicy.cpp](../../../../llm/src/prompt/PromptPolicy.cpp) |
| 补齐 Policy 对实际 release/source/tools 的 direct blocker 输入 | [llm/include/prompt/PromptPolicyInput.h](../../../../llm/include/prompt/PromptPolicyInput.h)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |
| 消除 PromptPolicyInput 新字段引入的配置投影缺省初始化告警 | [llm/src/LLMSubsystemConfig.cpp](../../../../llm/src/LLMSubsystemConfig.cpp) |
| 覆盖 trusted source 缺失、allowlist deny 与 exact release mismatch | [tests/unit/llm/PromptPolicyAllowlistTest.cpp](../../../../tests/unit/llm/PromptPolicyAllowlistTest.cpp) |
| 覆盖 tool visibility patch、RequireRecompose、redaction 后预算复核与 OverBudget | [tests/unit/llm/PromptPolicyToolVisibilityTest.cpp](../../../../tests/unit/llm/PromptPolicyToolVisibilityTest.cpp) |
| 覆盖 profile 差异来自 profiles 投影而非代码硬编码 | [tests/unit/llm/PromptPolicyProfileDiffTest.cpp](../../../../tests/unit/llm/PromptPolicyProfileDiffTest.cpp) |
| 将 Policy 实现与三条单测接入 llm / unit 聚合目标 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 4. Build 三件套

1. 代码目标：实现 `PromptPolicy` 的固定治理顺序、fail-closed trusted source / allowlist 检查、tool visibility patch 校验、redaction 与 render budget 复核。
2. 测试目标：`PromptPolicyAllowlistTest` 覆盖 trusted source 缺失与 allowlist deny；`PromptPolicyToolVisibilityTest` 覆盖 tool visibility patch、RequireRecompose、redaction 后预算回落与 OverBudget；`PromptPolicyProfileDiffTest` 覆盖 profile 差异治理；`LLMSubsystemConfigProjectionTest` 用于验证 direct blocker fix 带来的配置投影不回退。
3. 验收动作：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建目标 `dasall_prompt_policy_allowlist_unit_test`、`dasall_prompt_policy_tool_visibility_unit_test`、`dasall_prompt_policy_profile_diff_unit_test`
   - `RunCtest_CMakeTools` 运行 `PromptPolicyAllowlistTest`、`PromptPolicyToolVisibilityTest`、`PromptPolicyProfileDiffTest`
   - `Build_CMakeTools` 构建目标 `dasall_llm_subsystem_config_projection_unit_test` 与 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 运行 `LLMSubsystemConfigProjectionTest`

## 5. 风险与回退

1. 当前 `tool_visibility_rules` 只实现了 exact/domain 两类匹配和最小 deny 语义；如果后续 Tool Policy Gate 需要更细粒度的 capability、trusted scope 或参数级治理，应优先扩 profile/schema 与 Tool Policy Gate 的正式规则模型，而不是在 PromptPolicy 内临时拼接字符串协议。
2. 018 新增的 `selected_release_scope`、`selected_trusted_source`、`visible_tools` 属于 per-request 事实，并不应由 [llm/src/LLMSubsystemConfig.cpp](../../../../llm/src/LLMSubsystemConfig.cpp) 长期静态填充；019 必须把 Registry/Composer/Tool Policy Gate 的真实结果接入这些字段，否则 Policy 仍只能在调用方手动注入下工作。
3. 当前 redaction 仍是 provider-neutral 文本启发式，不解析 provider 私有结构化 payload；如果后续某类 provider 响应需要结构化敏感字段治理，应继续保持 shared contracts 不变，把增强逻辑放在 llm module-local 的 Policy/Normalizer 内部完成。