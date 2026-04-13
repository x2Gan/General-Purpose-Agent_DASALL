# LLM-TODO-035 profile 差异 integration 设计收敛

日期：2026-04-13
任务：LLM-TODO-035
状态：Done

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.10 已冻结 profile 驱动的 route / allowlist / timeout 默认策略，9.3 已明确要求补齐 `LLMProfileIntegrationTest`，9.5 则把“contracts 不回退、避免平行配置系统”列为兼容性检查点。
2. [profiles/include/RuntimePolicySnapshot.h](../../../../profiles/include/RuntimePolicySnapshot.h) 已冻结 profile 侧共享输入面：`ModelProfile.stage_routes`、`PromptPolicy.allowed_prompt_releases/trusted_sources`、`TimeoutPolicy.llm` 与 `DegradePolicy.fallback_chain` 都必须先进入 `RuntimePolicySnapshot`，再由 llm 侧投影消费。
3. [llm/src/LLMSubsystemConfig.cpp](../../../../llm/src/LLMSubsystemConfig.cpp) 的 `project_llm_subsystem_config(...)` 已证明 route、prompt allowlist、trusted sources、degrade policy 与 llm timeout 都直接来自 `RuntimePolicySnapshot`，overlay 只允许追加 llm module-local 的 prompt/provider asset roots 与 selector hints。
4. [tests/unit/llm/LLMSubsystemConfigProjectionTest.cpp](../../../../tests/unit/llm/LLMSubsystemConfigProjectionTest.cpp) 已固定“llm 配置必须来自投影视图而不是第二套配置系统”的 projector 语义；035 的 owner 是把这条 projector 语义带入真实 `PromptPipeline + LLMManager` integration 闭环。
5. [tests/unit/llm/PromptPolicyProfileDiffTest.cpp](../../../../tests/unit/llm/PromptPolicyProfileDiffTest.cpp) 已固定 profile 之间的 allowlist / trusted source 差异，但仍停留在 policy 单元层；035 需要进一步证明这些差异确实能通过投影配置改变 manager 的真实 dispatch 结果。
6. [tests/integration/llm/LLMGovernanceFailureIntegrationTest.cpp](../../../../tests/integration/llm/LLMGovernanceFailureIntegrationTest.cpp) 与 [tests/integration/llm/LLMFallbackIntegrationTest.cpp](../../../../tests/integration/llm/LLMFallbackIntegrationTest.cpp) 已提供可复用的真实 prompt / manager integration 基座，说明 035 不应再手写 fake config 或 fake pipeline。

## 2. 外部参考

1. 本轮未引入新的外部 provider 协议或 profile 标准；035 的 owner 完全由 DASALL 已冻结的 RuntimePolicySnapshot 投影边界与 llm integration 设计驱动。

## 3. Design 结论

1. 035 不需要新增生产代码修补。`RuntimePolicySnapshot -> project_llm_subsystem_config(...) -> PromptPipeline + LLMManager` 的主链已经存在，缺失的是 integration 证据，而不是 route/policy/timeout 逻辑本身。
2. 035 必须避免平行配置系统，因此测试不能手写四份 `LLMSubsystemConfig`；应直接构造不同 profile 的 `RuntimePolicySnapshot`，再通过 `project_llm_subsystem_config(...)` 生成 llm consumer view。
3. 035 的最小矩阵应覆盖三类 profile 差异：
   - route 差异：`cloud_full` 命中 cloud route，`edge_minimal` 命中 local route
   - prompt allowlist 差异：同一 canary release 在 `cloud_full` 下允许，在 `edge_minimal` 下被治理拒绝
   - timeout 差异：`desktop_full` 与 `edge_balanced` 向 adapter 传入不同的 `timeout_ms`
4. 035 需要显式记录一个设计接缝：projection unit fixture 使用了 `planner/responder` 这类 profile stage key，而真实 `PromptRegistry` 当前只接受 `planning/execution/reflection/response`。因此 integration 必须使用真实 llm stage 名称 `planning`，保持 projector 行为不变，同时避免把 stage 归一化逻辑偷偷塞进测试任务。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| RuntimePolicySnapshot 投影到真实 manager 闭环 | [tests/integration/llm/LLMProfileIntegrationTest.cpp](../../../../tests/integration/llm/LLMProfileIntegrationTest.cpp) |
| integration test 注册 | [tests/integration/llm/CMakeLists.txt](../../../../tests/integration/llm/CMakeLists.txt) |
| 035 证据与状态回写 | [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) |

## 5. Build 三件套

1. 代码目标：新增 `LLMProfileIntegrationTest`，在真实 `RuntimePolicySnapshot -> project_llm_subsystem_config(...) -> PromptPipeline + LLMManager` 闭环中验证 route、prompt allowlist 与 timeout 三类 profile 差异。
2. 测试目标：
   - `cloud_full` 与 `edge_minimal` 的主 route 不同，且 dispatch 到不同 adapter
   - 同一 canary prompt release 在 `cloud_full` 下允许、在 `edge_minimal` 下返回 `PromptGovernance / PolicyDenied`
   - `desktop_full` 与 `edge_balanced` 向 adapter 下发不同 `timeout_ms`
   - 现有 contract gate 不回退
3. 验收命令：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建目标 `dasall_llm_profile_integration_test`
   - `RunCtest_CMakeTools` 运行 `LLMProfileIntegrationTest`
   - `Build_CMakeTools` 构建目标 `dasall_integration_tests` 与 `dasall_contract_tests`

## 6. 风险与回退

1. 035 当前固定的是“profile 差异来自投影视图”的真实行为；若后续要把 `planner/responder` 这类 profile stage key 与 llm `planning/response` 统一成共享 taxonomy，应通过独立 owner 任务完成，而不能在 035 的 integration 测试里静默引入 stage 映射逻辑。
2. 035 继续通过 prompt policy allowlist 固定 `cloud_full` 与 `edge_minimal` 的行为差异，但 trusted-source 的 profile 差异仍沿用 034 已记录的真实 manager 映射；若后续希望把 profile 的 trusted-source 差异也纳入 integration 矩阵，应单独扩 owner，而不是在本任务里把三类治理差异一次性耦合。
3. 本轮为隔离 profile 变量继续使用最小 prompt package 与单次 unary dispatch；若后续想叠加 streaming、fallback exhausted 或 provider onboarding 变量，需要在新的 owner 用例中明确说明多变量交织带来的断言复杂度。