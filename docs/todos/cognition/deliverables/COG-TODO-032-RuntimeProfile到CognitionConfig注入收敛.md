# COG-TODO-032 Runtime profile 到 CognitionConfig 注入收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Design + Build 生产注入补强

## 1. 本地证据

1. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 中 COG-TODO-032 明确指出：现有 `CognitionProfileCompatibilityTest` 主要切换 `profile_id`，没有证明 `RuntimePolicySnapshot -> CognitionConfig` 投影被 runtime 初始化路径真实消费。
2. `tests/fixtures/runtime/CognitionRuntimeIntegrationFixture.h` 当前在 true integration fixture 中直接以 `CognitionConfig{}` 构建 `create_cognition_engine()` / `create_response_builder()`，没有把 `policy_snapshot` 投影为 cognition 配置。
3. 同一 fixture 的 `make_true_integration_policy_snapshot()` 仅注册了 `main` route，而 `cognition/src/config/CognitionConfigProjector.cpp` 明确要求 `planning`、`execution`、`reflection`、`response` 四个 canonical stage route 全部存在，否则 `project_config()` fail-closed。
4. `cognition/src/CognitionFacade.cpp` 和 `cognition/src/response/ResponseBuilder.cpp` 当前 bridge 请求仍使用硬编码 `StageModelHint`，没有消费 `StagePolicyResolver` 产生的 deadline、template preference 与 route 绑定。
5. `cognition/src/llm/CognitionLlmBridge.cpp` 会把 `StageModelHint` 投影为 `LLMGenerateRequest`，但当前 `StageModelHint.preferred_provider` 为空，`LLMRequest.model_route` 没有从 runtime profile stage route 写入，因此 profile route 差异在真实 bridge 请求上不可观察。
6. `tests/unit/cognition/StagePolicyResolverProfileDiffTest.cpp` 已经证明 profile 差异在 resolver 层成立，因此 032 的最小缺口是把这条差异链接到 runtime composition 和 true integration evidence，而不是重复实现另一套 profile 分支。

## 2. 外部参考

1. Microsoft 的依赖注入指南强调，运行期配置和外部依赖应在 composition root 统一装配，避免业务对象隐式查找配置来源。032 因此把 `RuntimePolicySnapshot` 消费收敛到 runtime init / cognition factory 边界，而不是散落在 tests 或阶段 owner 内部：https://learn.microsoft.com/en-us/dotnet/core/extensions/dependency-injection-guidelines
2. Azure Architecture Center 的 External Configuration Store pattern 指出，配置快照必须在受控边界投影为组件内可执行配置，并且缺失关键配置时应 fail fast，而不是带默认值继续运行。032 因此要求 canonical stage route 缺失时直接拒绝初始化或拒绝 stage 计划投影：https://learn.microsoft.com/en-us/azure/architecture/patterns/external-configuration-store

## 3. 边界与职责收敛

1. `RuntimePolicySnapshot` 继续是 Runtime/Profile 子系统的唯一外部策略输入面；cognition 不直接持有 profile provider，也不自建 profile 分支。
2. `CognitionConfigProjector` 继续作为 cognition 内部 owner，把 snapshot 投影为 `CognitionConfig` 与 `StageModelHint`；032 不把 projector 扩张成 shared utility。
3. `StagePolicyResolver` 继续是 cognition 内部阶段策略 owner；032 只要求它的输出进入真实运行链，不把 resolver 暴露成 Runtime 直接依赖的公共类。
4. runtime 继续只拥有组合权和 fail-closed 准入权：当 live cognition ports 尚未装配时，可通过 cognition factory 基于 snapshot 完成装配；如果 snapshot 缺失 canonical route，则由 runtime init 拒绝继续。
5. true integration fixture 继续是 production composition 的测试替身，但不能再预先塞入 `CognitionConfig{}` 假配置；它必须通过 `policy_snapshot` 驱动实际装配。

## 4. 数据与接口说明

### 4.1 输入与输出

| 接口 / 数据 | 方向 | 本轮约束 |
|---|---|---|
| `profiles::RuntimePolicySnapshot` | Runtime -> cognition factory | 作为唯一 profile 策略快照输入；canonical `stage_routes` 缺失时 fail-closed |
| `cognition::CognitionConfig` | projector -> cognition components | 只在 cognition 内部使用；通过 snapshot factory overload 注入，不让 runtime 包含 cognition 私有头 |
| `cognition::StageModelHint` | resolver -> bridge | 必须携带 `deadline_ms`、`max_output_tokens` 和来自 stage route 的 `preferred_provider` |
| `contracts::LLMRequest.model_route` | bridge -> llm | 必须真实反映 snapshot 的 stage route，而不是按 stage 名拼默认值 |
| `runtime::AgentInitResult` | runtime init -> integration evidence | 用于报告 snapshot 驱动的组合成功/失败，不宣称 degraded success 覆盖 fail-closed 缺口 |

### 4.2 目标文件范围

1. `cognition/include/CognitionDependencies.h`
2. `cognition/include/ICognitionEngine.h`
3. `cognition/include/IResponseBuilder.h`
4. `cognition/src/config/CognitionConfigProjector.cpp`
5. `cognition/src/CognitionFacade.cpp`
6. `cognition/src/response/ResponseBuilder.cpp`
7. `runtime/src/AgentFacade.cpp`
8. `tests/fixtures/runtime/CognitionRuntimeIntegrationFixture.h`
9. `tests/integration/cognition/CognitionProfileCompatibilityTest.cpp`
10. `tests/integration/cognition/CognitionRuntimePolicyProjectionIntegrationTest.cpp`
11. `tests/integration/cognition/CMakeLists.txt`
12. 如需编译面校验，补 `tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp`

## 5. 流程与时序

### 5.1 初始化路径

1. Runtime 收到 `AgentInitRequest(policy_snapshot, dependency_set)`。
2. 若 `dependency_set` 尚未预装 `cognition_engine` / `response_builder`，则 runtime 通过 cognition 的 snapshot-aware factory overload 触发 `RuntimePolicySnapshot -> CognitionConfig` 投影。
3. 若 projector 失败或缺失 canonical stage route，则 init fail-closed，`AgentInitResult.accepted=false`，并带明确 diagnostics。
4. 若投影成功，则用同一 snapshot-aware dependency 构造 `CognitionFacade` 与 `ResponseBuilder`，把 profile 差异绑定到真正的 live ports。

### 5.2 运行路径

1. `CognitionFacade::decide()` / `reflect()` 在存在 runtime policy snapshot 时，经 `StagePolicyResolver` 解析 stage plan。
2. bridge 请求优先使用 resolver 产出的 `StageModelHint`，将 `preferred_provider`、`deadline_ms`、`max_output_tokens` 写入真实 `LLMGenerateRequest`。
3. `ResponseBuilder::build()` 在 `factory_test` 等 profile 下按照 resolver 的 `TemplatePreferred` / `TemplateAllowed` 策略决定是否优先模板降级。
4. 缺失 canonical route 的 snapshot 不得回退到默认 stage 名推导 route；必须返回显式错误或初始化拒绝。

## 6. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| runtime 需要 snapshot-aware cognition 装配面 | `AgentFacade.cpp` + cognition public factory overload | true integration path 不再用 `CognitionConfig{}` 假配置 |
| projector / resolver 输出必须进入真实 bridge request | `CognitionFacade.cpp`、`ResponseBuilder.cpp`、`CognitionLlmBridge` 输入 | `LLMGenerateRequest.model_route` / `timeout_ms` / `max_output_tokens` 可观察 |
| canonical route 缺失必须 fail-closed | snapshot factory overload + `AgentFacade::init()` | profile negative case init 被拒绝 |
| factory_test 必须真实偏向模板策略 | `ResponseBuilder.cpp` + profile integration test | `factory_test` 不再与其它 profile 共享相同 response mode |
| true integration fixture 不能预埋假 cognition config | `CognitionRuntimeIntegrationFixture.h` | profile test 真实依赖 `policy_snapshot` 驱动装配 |

## 7. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增 snapshot-aware cognition factory overload，并把 route 写入 `StageModelHint.preferred_provider` | interface surface 编译不回退 | `cmake --build build-ci --target dasall_cognition_interface_surface_unit_test dasall_cognition_config_projection_unit_test` | 若 public overload 造成歧义，保留 config overload 不动并为 snapshot overload 使用显式类型 |
| B2 | 在 `CognitionFacade` / `ResponseBuilder` 真实消费 `StagePolicyResolver` 输出 | focused unit 继续通过，bridge request 可观察 deadline/route | `cmake --build build-ci --target dasall_stage_policy_resolver_profile_diff_unit_test dasall_cognition_profile_compatibility_integration_test` | 若 resolver 接线影响既有 deterministic path，仅在存在 snapshot 时启用 |
| B3 | 在 `AgentFacade::init()` 对缺失 live cognition ports 的 true integration 路径执行装配，并新增 provider-driven projection gate | focused integration 证明 runtime init 消费真实 profile asset，而不是手工 snapshot fixture | `Build_CMakeTools()`；`RunCtest_CMakeTools(tests=["CognitionRuntimePolicyProjectionIntegrationTest"])` | 若更宽的 response 语义尚未冻结，projection gate 只观察 planning/execution/reflection 的 route/deadline/output budget，不把 COG-TODO-040 混入 032 |
| B4 | 保持 canonical route schema、overlay、runtime profile 与 capability integration focused regression 不回退 | provider/runtime/profile 基线继续稳定，032 结论不依赖更宽的 response terminal-status 口径 | `RunCtest_CMakeTools(tests=["ProfileRuntimePolicySchemaContractTest","ProfileOverlayComposerTest","ProfilesBuildRuntimeIntegrationTest","RuntimeProfileCompatibilityTest","CapabilityServicesProfileIntegrationTest","CognitionRuntimePolicyProjectionIntegrationTest"])` | 若 broader profile compatibility 与 response bridge 语义仍有张力，保留为独立 TODO，不回退本轮 provider canonicalization 结论 |

## 8. D Gate

Gate = PASS。

进入 Build 的条件已满足：本地缺口已定位为 true integration 预装默认 config、resolver 输出未入主链、route/deadline 不可观察；Build 三件套已锁定；范围限定在 cognition factory/runtime init/fixture/profile integration，不扩张到 memory writeback、reflection recovery 或 schema parser。

## 9. Build 验证证据

1. `Build_CMakeTools()`
	- 结果：通过，受影响的 cognition integration targets 已重新编译并成功链接，包括 `dasall_cognition_profile_compatibility_integration_test` 与 `dasall_cognition_runtime_policy_projection_integration_test`。
2. `RunCtest_CMakeTools(tests=["ProfileRuntimePolicySchemaContractTest","ProfileOverlayComposerTest","ProfilesBuildRuntimeIntegrationTest","RuntimeProfileCompatibilityTest","CapabilityServicesProfileIntegrationTest","CognitionRuntimePolicyProjectionIntegrationTest"])`
	- 结果：通过，6 条 tests 全部通过；stderr 中 `DartConfiguration.tcl` 缺失仍是仓库既有 CMake Tools 噪声，不影响 focused 结论。
	- 关键覆盖点：
	  - `ProfileRuntimePolicySchemaContractTest` 固定 canonical stage route schema，不再允许回退到 `planner` / `responder` 旧词汇。
	  - `ProfileOverlayComposerTest` 与 `RuntimeProfileCompatibilityTest` 证明真实 provider snapshot 与 overlay/runtime profile 断言已切换到 `planning` / `execution` / `reflection` / `response`。
	  - `CapabilityServicesProfileIntegrationTest` 证明依赖真实 provider snapshot 的周边 profile integration 没有被 canonicalization 回归破坏。
	  - `CognitionRuntimePolicyProjectionIntegrationTest` 以真实 profile asset -> `RuntimePolicyProvider` -> `AgentFacade::init()` -> planning/execution/reflection bridge request 为权威证据，断言 route、deadline、max_output_tokens 都来自真实 snapshot，而不是手工 fixture。

## 10. 完成判定

COG-TODO-032 已完成。

1. Runtime 现在可以在缺失 live cognition ports 时，通过 snapshot-aware cognition factories 在 `AgentFacade::init()` 中显式执行 `RuntimePolicySnapshot -> CognitionConfig` 投影，不再依赖 fixture 预埋 `CognitionConfig{}`。
2. `CognitionFacade` 与 `ResponseBuilder` 在存在 runtime policy snapshot 时真实消费 `StagePolicyResolver` 输出；bridge 请求上的 `model_route`、`timeout_ms`、`max_output_tokens` 由 snapshot 投影而来，不再退回 stage-name 默认值。
3. 032 的当前权威 integration evidence 已切换为 `CognitionRuntimePolicyProjectionIntegrationTest`：它直接证明真实 profile asset 经 provider/runtime init 进入 live cognition stage request，不再依赖 `CognitionProfileCompatibilityTest` 所携带的手工 snapshot fixture 或更宽的 response 语义假设。
4. non-factory profile 是否必须发出 response bridge request、`edge_minimal` 的 terminal status 应如何冻结，继续留在 COG-TODO-040 处理；这些未冻结语义不再作为 032 的完成判据。
5. 032 没有把 projector 或 resolver 扩张成 shared utility，也没有覆写故障注入或契约测试中显式装配的自定义 cognition ports；自定义 ports 仍由 runtime 组合根保留注入优先级。