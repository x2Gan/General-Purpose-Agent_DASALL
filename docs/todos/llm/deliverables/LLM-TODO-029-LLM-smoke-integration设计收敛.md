# LLM-TODO-029 LLM smoke integration 设计收敛

日期：2026-04-13
任务：LLM-TODO-029
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 9.3 与 9.6 已冻结 029 的 owner：至少要有 1 条 llm smoke integration 打通 `PromptPipeline -> ModelRouter -> LLMManager -> MockLLMAdapter -> ResponseNormalizer` 的 unary 主路径，并保留 prompt/model/route/latency/token/cost/reasoning 等关键观测字段。
2. [docs/todos/llm/deliverables/LLM-TODO-028-LLM-observability-bridges设计收敛.md](LLM-TODO-028-LLM-observability-bridges%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md) 已明确 028 只冻结 bridge 与 signal contract，不把 observability 真正织入 [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 的 runtime hot path；因此 029 必须补齐主链消费，而不是继续停留在 unit-testable bridge 层。
3. [tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp](../../../../tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp) 在本轮前仍只是 discoverability 锚点，只验证 test 名称与 target 命名不冲突，没有覆盖真实 Prompt 资产加载、route 解析、MockAdapter 调用和 observability 字段投影，因此不能作为 029 的完成证据。
4. [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 的既有 unary 主路径已经在 024 中冻结：成功路径会把 `route=`、`selection_reason=`、`provider_trace_id=`、`audit=`、`reasoning_content_stripped=true` 与 `usage:*` tags 追加到 provider-neutral `LLMResponse`，因此 029 的最小增量应集中在 manager hot path observability 接线、真实 PromptPipeline 命中和 smoke fixture 证据增强，而不回写 shared `LLMResponse` / `LLMManagerResult` ABI。
5. [llm/src/prompt/PromptRegistry.cpp](../../../../llm/src/prompt/PromptRegistry.cpp) 与 [llm/assets/prompts/planner/default/manifest.yaml](../../../../llm/assets/prompts/planner/default/manifest.yaml) 暴露了真实 smoke 会踩到的两个集成条件：`stage` 必须使用 `planning` 才能命中资产，`language` 必须与资产清单的 `zh-cn` 保持一致；如果继续沿用 unit fake pipeline 隐藏这些条件，029 无法验证真实 Prompt 三段主链。
6. [tests/integration/llm/CMakeLists.txt](../../../../tests/integration/llm/CMakeLists.txt) 的 llm integration target 在本轮前只引入了 tests/mocks/contracts/llm include 根；由于 029 smoke 需要直接消费 infra 的 logging/metrics/tracing/audit 接口，这个 target 还必须补入 `infra/include` 才能承载真实 observability fixture。

## 2. 外部参考

1. CTest 官方手册说明 `ctest -N` 只列出测试而不执行，适合证明 discoverability 没有回退；同时 `ctest -L integration` 可以作为 integration 聚合门禁的稳定执行入口。029 依此把“target 仍在 integration 聚合里”和“smoke 用例本身通过”分成两条证据链。参考：https://cmake.org/cmake/help/latest/manual/ctest.1.html
2. GoogleTest Advanced Topics 强调集成/夹具测试应优先通过共享 fixture 复用真实依赖拼装，并使用可读的断言消息保留失败上下文。029 虽未引入 gtest 宏，但沿用同一原则：让 smoke 直接复用 production `PromptPipeline`、`ModelRouter`、`LLMManager`、`ResponseNormalizer`，只把 provider 调用替换为 `MockLLMAdapter`，避免在测试内复制 llm 主链逻辑。参考：https://google.github.io/googletest/advanced.html

## 3. Design 结论

1. 029 采用“真实主链 + 单一 mock provider”的最小闭环：`PromptPipeline` 使用仓库内真实 prompt 资产，[llm/src/route/ModelRouter.cpp](../../../../llm/src/route/ModelRouter.cpp) 与 provider catalog snapshot 继续走 production 选择逻辑，[tests/mocks/include/MockLLMAdapter.h](../../../../tests/mocks/include/MockLLMAdapter.h) 只替代外部 provider 调用，避免把 smoke 退化成 unit fake pipeline。
2. observability hot path 只在 029 内最小接入 `LLMMetricsBridge` 与 `LLMTraceBridge`。原因是 [llm/include/LLMRequest.h](../../../../contracts/include/llm/LLMRequest.h) 只冻结了 request/call identity、prompt/model/budget/tags，并不携带完整 `InfraContext`；`LLMAuditBridge` 需要 `request_id/session_id/trace_id/task_id/lease_id` 全量上下文，不能由 `LLMManager` 在不扩 ABI 的前提下强行造假。因此 029 让 manager hot path 直接发 log/metrics/trace，而把 `reasoning_content_stripped` 审计事实留给 smoke fixture 使用固定 `LLMAuditContext` 验证 bridge 消费能力。
3. 为了让真实 PromptRegistry 命中 planner baseline 资产，029 同步修正 [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 的 `PromptQuery.language` 为 `zh-cn`，并在 smoke fixture 中显式使用 `stage = planning`、`profile_id = desktop_full`、`scene_id = general`、`persona_id = planner` 与绝对 prompt asset 根路径。这些是 029 的真实集成条件，不应继续藏在 fake pipeline 之后。
4. metrics 断言必须服从 028 已冻结的低基数 stage token 规则：`MetricLabels.stage` 不是纯 `planning`，而是 `call/planning/...`、`selection/planning/...`、`cost/provider/model` 这类 token。029 因此在 smoke 中只断言 token 前缀、`profile_id`、`outcome` 和必需 family，不再错误地把 stage label 当成自由文本字段。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| manager hot path 直接消费 metrics/log 与 trace bridge | [llm/src/LLMManager.h](../../../../llm/src/LLMManager.h)、[llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) |
| 真实 PromptPipeline + MockLLMAdapter + observability fixture smoke | [tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp](../../../../tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp) |
| llm integration target 引入 infra/include 编译依赖 | [tests/integration/llm/CMakeLists.txt](../../../../tests/integration/llm/CMakeLists.txt) |
| 029 状态、证据、后继任务与工作日志回写 | [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) |

## 5. Build 三件套

1. 代码目标：给 `LLMManager` 增加可选的 `LLMMetricsBridge` / `LLMTraceBridge` 注入点，在 unary 成功路径上把 route resolve、adapter invoke、response normalize 与最终 call summary 直接投影到 observability sink；同时把 `PromptQuery.language` 调整为资产可命中的 `zh-cn`，并补齐 llm integration target 的 `infra/include` 编译依赖。
2. 测试目标：将 [tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp](../../../../tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp) 从 discoverability 锚点升级为真实 smoke fixture，覆盖 real PromptPipeline、route 选择、MockAdapter unary 返回、ResponseNormalizer 的 `reasoning_content` 剥离，以及 log/metrics/trace/audit 证据。
3. 验收命令：
   - `Build_CMakeTools` 构建目标 `dasall_llm_smoke_integration_test`
   - `RunCtest_CMakeTools` 运行 `LLMSubsystemSmokeIntegrationTest`
   - `Build_CMakeTools` 构建目标 `dasall_integration_tests`
   - `ListTests_CMakeTools`

## 6. 风险与回退

1. 029 没有扩 shared `LLMRequest` / `LLMResponse` 去承载完整 `InfraContext`，因此 audit bridge 仍保持 smoke fixture 显式提供上下文的形态；若后续要把 audit 也正式织入 manager hot path，必须先走 contracts/runtime 评审，而不是在 llm 内私加 session/trace/task 字段。
2. planner baseline prompt 资产当前仍包含 `{{user_goal}}` / `{{constraints}}` 槽位，而 [contracts/include/prompt/PromptComposeRequest.h](../../../../contracts/include/prompt/PromptComposeRequest.h) 尚未直接承载这两个变量。029 允许 smoke 以 “真实资产命中 + 模板变量保留 warning” 的方式通过，但若后续要把这两个槽位转为硬验收项，应另起 Prompt 输入映射任务，而不是在 smoke 内悄悄放松模板语义。
3. 029 新增的 metrics/trace 接线默认是可选注入，不会让 sink 不可用反向打断 llm 主链；如果未来有人把 bridge 写成强依赖并让 sink failure 直接影响 `LLMManagerResult`，需要回到 028/029 的边界结论重新评审。