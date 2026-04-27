# COG-TODO-031 CognitionLlmBridge 主链接入收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Design + Build 主链补强

## 1. 本地证据

1. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 中 COG-TODO-031 的评审发现指出：`CognitionLlmBridge` 已实现，但 `CognitionFacade` / `ResponseBuilder` 生产路径尚未注入 `ILLMManager`，bridge tests 只证明独立投影。
2. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.4 约束 `CognitionLlmBridge` 是 cognition -> llm 的唯一内部桥接 owner，只负责 stage request 投影、结果归一化、provider-private 字段裁剪与 fail-fast 错误上抛。
3. 同文档 §6.14.2 冻结 canonical stage key：`planning`、`execution`、`reflection`、`response`，并要求 bridge 直接透传 `StageModelHint.stage_name` 与 `task_type`，不得维护第二套 stage alias。
4. 同文档 §6.14.3 明确 cognition 不自建 retry / circuit breaker，bridge 只把 `ILLMManager` 返回的失败事实投影为 `ErrorInfo`；Runtime 继续保持错误路由裁定权。
5. `COG-TODO-020`、`COG-TODO-023`、`COG-TODO-024` 均已 Done：bridge、façade orchestration、`MockLLMManager` / `MockCognitionFixture` 已具备本轮最小接线前置。
6. `runtime/include/RuntimeDependencySet.h` 已有 `std::shared_ptr<llm::ILLMManager> llm_manager` 字段，可作为后续 runtime composition root 的传入端；本轮只把 cognition factory 侧打开非破坏性依赖注入面，不扩张 runtime 初始化策略。

## 2. 外部参考

1. Microsoft .NET dependency injection 文档说明，硬编码依赖会导致替换实现困难、配置散落且难以单测；DI 通过接口抽象和构造注入降低这些问题。COG-TODO-031 因此选择新增 cognition-side dependency struct，而不是让 `CognitionFacade` / `ResponseBuilder` 自行 new 或查找 `ILLMManager`：https://learn.microsoft.com/en-us/dotnet/core/extensions/dependency-injection/overview
2. Azure Architecture Center 的 Circuit Breaker pattern 指出，对于可能失败的远程依赖，重试 / 熔断是独立韧性代理职责；调用方应能快速返回失败或降级。COG-TODO-031 保持 bridge fail-fast，不在 cognition 主链补本地 retry / breaker：https://learn.microsoft.com/en-us/azure/architecture/patterns/circuit-breaker

## 3. 主结论

1. 新增 `CognitionRuntimeDependencies` 作为 cognition composition root 的窄依赖面，当前只接收 `std::shared_ptr<llm::ILLMManager>`；保留既有 `create_cognition_engine(config)` / `create_response_builder(config)` 调用不变，并新增带 dependencies 的 overload。
2. `CognitionFacade` 在 `planning`、`execution`、`reflection` 三个 canonical stage 上可选调用 `CognitionLlmBridge::invoke_stage()`：
   - 有 bridge 且成功：记录 canonical stage / task diagnostics，主链继续由现有 rule owners 产出语义结果；
   - 有 bridge 但失败：若 `degraded_path_allowed=true`，记录降级 diagnostics 并回退现有 deterministic rule path；若不允许降级，则 fail-fast 返回 bridge 投影的 `ErrorInfo`；
   - 无 bridge：记录 `llm_bridge.unavailable:<stage>`，继续 deterministic path。
3. `ResponseBuilder` 把 response 路径拆成三类明确模式：
   - `llm_bridge`：有 `ILLMManager` 时通过 bridge 调用 `response/final_response`；
   - `observation_projection`：无 bridge 但存在 observation payload 时走 deterministic projection，不再伪装为 `llm_projection`；
   - `template_fallback`：无 payload、prefer template 或 bridge failure 可降级时走模板。
4. `MockLLMManager` 补充记录全量 `generate` 请求序列，使 focused tests 可断言 façade / response builder 是否真的经 bridge 触达 canonical stage。
5. Runtime true-integration fixture 可把 `RuntimeDependencySet::llm_manager` 传给 cognition factories，证明现有 runtime dependency set 已能承载后续 production composition root，不在本轮扩张 runtime policy projection。

## 4. D 原子项完成情况

| 原子项 | 设计目标 | 完成判定 | 风险与回退 |
|---|---|---|---|
| D1 | 确认前置和 blocker | PASS：020 / 023 / 024 已 Done，`RuntimeDependencySet` 已有 llm manager 字段 | 若 runtime composition root 尚未生产化，本轮只落 cognition-side dependencies |
| D2 | 冻结依赖注入边界 | PASS：新增 `CognitionRuntimeDependencies`，不改变原 factory 调用 | 若 public header 引入 llm 编译负担，使用 forward declaration |
| D3 | 冻结主链消费语义 | PASS：façade 只消费 bridge diagnostics / failure，不替代 deterministic owner；response 用 bridge 生成最终文本 | 若 bridge 失败，按阶段降级策略返回 deterministic path 或 error |
| D4 | 锁定测试出口 | PASS：focused unit + cognition integration fixture 可观察 canonical stage request | 若 ctest 工具态异常，回退显式二进制执行并记录 |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| cognition composition root 需要非破坏性 DI 面 | `cognition/include/CognitionDependencies.h`、`ICognitionEngine.h`、`IResponseBuilder.h` | 旧 factory 调用继续通过，新 overload 可传入 `ILLMManager` |
| façade 主链必须能经 bridge 触达 canonical stages | `cognition/src/CognitionFacade.cpp` | `MockLLMManager` 记录 `planning`、`execution`、`reflection` |
| response 不再把 observation projection 伪装为 LLM | `cognition/src/response/ResponseBuilder.cpp` | 无 bridge 时 diagnostic/tag 为 `observation_projection`；有 bridge 时为 `llm_bridge` |
| bridge failure 必须显式降级或失败 | `ResponseBuilderTemplateFallbackTest.cpp`、façade flow test | manager failure 可触发模板降级或 fail-fast error |
| runtime fixture 可承载 llm manager 依赖 | `tests/fixtures/runtime/CognitionRuntimeIntegrationFixture.h` | cognition integration happy path 仍通过且走 response bridge |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增 public dependency struct 与 factory overload | interface surface static_assert 覆盖旧签名与新 overload | `cmake --build build-ci --target dasall_cognition_interface_surface_unit_test` | 若 overload 造成歧义，使用显式 function pointer static_cast |
| B2 | 接线 `CognitionFacade` 到 bridge | flow test 断言 `planning/execution/reflection` 被记录 | `cmake --build build-ci --target dasall_cognition_facade_flow_unit_test` | 若 bridge failure 影响旧 deterministic 流程，限定为有依赖时才调用 |
| B3 | 接线 `ResponseBuilder` 到 bridge 并拆分 observation projection | response tests 覆盖 bridge success、无 bridge projection、bridge failure fallback | `cmake --build build-ci --target dasall_response_builder_agent_result_mapping_unit_test dasall_response_builder_template_fallback_unit_test` | 若旧集成依赖响应文本前缀，fixture 的 response stage result 保留兼容文本 |
| B4 | 更新 mock / integration fixture 与 CMake | mock 能记录请求序列；integration 可传入 `llm_manager` | `ctest --test-dir build-ci -R "CognitionFacadeFlowTest|ResponseBuilder.*|CognitionLlmBridgeProjectionTest" --output-on-failure` | 若全量 integration 受既有非 cognition 问题影响，只执行本轮 focused targets |

## 7. D Gate

Gate = PASS。

进入 Build 的条件已满足：设计文档已落盘、Design -> Build 映射完整、Build 三件套已锁定、范围限定在 cognition factory / façade / response builder / mocks / focused tests / fixture 接线，不扩张到 runtime profile 投影或恢复策略。

## 8. Build 验证证据

1. `cmake --build build-ci --target dasall_cognition dasall_cognition_interface_surface_unit_test dasall_cognition_facade_flow_unit_test dasall_response_builder_agent_result_mapping_unit_test dasall_response_builder_template_fallback_unit_test dasall_cognition_llm_bridge_projection_unit_test`
   - 结果：通过；公共 factory overload、façade flow、response mapping / fallback 与 bridge projection targets 均完成编译链接。
2. `ctest --test-dir build-ci -R "CognitionFacadeFlowTest|ResponseBuilder.*|CognitionLlmBridgeProjectionTest" --output-on-failure`
   - 结果：通过，`5/5` tests passed。
   - 覆盖点：`CognitionFacadeFlowTest` 断言 `MockLLMManager` 记录 `planning`、`execution`、`reflection`、`response` canonical stage；`ResponseBuilderAgentResultMappingTest` 断言无 bridge 时为 `observation_projection`；`ResponseBuilderTemplateFallbackTest` 断言 bridge failure 显式回退模板。
3. `cmake --build build-ci --target dasall_cognition_runtime_integration_test && ctest --test-dir build-ci -R "CognitionRuntimeIntegrationTest" --output-on-failure`
   - 结果：通过，`1/1` tests passed；runtime cognition true-integration fixture 已把 `RuntimeDependencySet::llm_manager` 传入 cognition factories。
4. `cmake --build build-ci --target dasall_mock_cognition_fixture_surface_unit_test && ctest --test-dir build-ci -R "MockCognitionFixtureSurfaceTest" --output-on-failure`
   - 结果：通过，`1/1` tests passed；`MockLLMManager` 请求序列记录未破坏既有 fixture surface。
5. `git diff --check`
   - 结果：通过；无 whitespace error。

## 9. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 代码注释 | PASS：新增逻辑围绕 factory overload、bridge stage consumption、response mode selection 等自解释函数命名展开；未引入需要额外长注释才能理解的算法块 |
| 正负例覆盖 | PASS：正例覆盖 façade + response bridge canonical stage；负例覆盖 response bridge failure fallback；无 bridge deterministic path 覆盖 observation projection diagnostics |
| 测试发现性 | PASS：focused targets 可构建，ctest regex 可发现并执行 `CognitionFacadeFlowTest`、`ResponseBuilder.*`、`CognitionLlmBridgeProjectionTest` |
| TODO / worklog / deliverable 回写 | PASS：专项 TODO COG-TODO-031 已置 Done，worklog 记录 #491 与本交付物均回写命令证据 |
| 提交前状态隔离 | PASS：当前改动均属于 COG-TODO-031 设计、代码、测试、fixture 和证据范围；未发现无关工作区改动 |

## 10. 完成判定

COG-TODO-031 已完成：cognition 现在具备非破坏性 `ILLMManager` 注入面，`CognitionFacade` 和 `ResponseBuilder` 可通过 `CognitionLlmBridge::invoke_stage()` 消费 canonical stages；无 bridge / bridge failure 均有 deterministic diagnostics 或显式 fallback / error。后续 COG-TODO-032 继续处理 Runtime profile 到 CognitionConfig 的生产注入闭环。
