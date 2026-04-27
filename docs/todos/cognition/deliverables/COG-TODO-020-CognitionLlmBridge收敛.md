# COG-TODO-020 CognitionLlmBridge 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready bridge implementation

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.4 已把 `CognitionLlmBridge` 定义为 cognition→llm 的唯一内部桥接层，要求它负责 stage request 投影、结果归一化、provider-private 字段剥离与 fail-fast 错误上抛。
2. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.14.2 已冻结 `StageModelHint.stage_name` 的 canonical key 只能是 `planning/execution/reflection/response`，并要求 bridge 直接透传该 key，不得私有维护第二套 stage alias 映射。
3. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.14.3 明确 cognition 不向 llm 回报错误，也不自建 retry / circuit breaker；bridge 只负责把 `ILLMManager` 返回的失败事实映射成统一 `ErrorInfo`。
4. `docs/todos/cognition/deliverables/COG-TODO-002-stage-taxonomy与StageModelHint映射收敛.md`、`COG-TODO-011-CognitionConfigProjector收敛.md` 已冻结 `StageModelHint` 字段与 profile→stage route 的 owner 边界，020 不需要再发明新的 stage key 或 provider route 语义。
5. `docs/todos/cognition/deliverables/COG-TODO-024-cognition测试fixture实现收敛.md` 已提供 `MockLLMManager`，使 020 可以直接在 `ILLMManager` 级别验证投影、错误映射与 provider-private redaction，而不必借用 `MockLLMAdapter` 旧路径。

## 2. 外部参考

1. Azure Architecture Center 的 Circuit Breaker pattern 指出，retry 与 circuit breaker 属于对远端依赖的通用韧性代理，调用方应该在故障明确时尽快 fail-fast，避免在上层重复实现一套会导致级联失败的重试/熔断逻辑；这与本轮把 `CognitionLlmBridge` 约束为“只做投影与归一化、不在 cognition 内自建 breaker/retry”的边界一致：https://learn.microsoft.com/en-us/azure/architecture/patterns/circuit-breaker

## 3. 主结论

1. 新增 `cognition/src/llm/CognitionLlmBridge.h`、`cognition/src/llm/CognitionLlmBridge.cpp`，把桥接 owner 从文档卡片落为独立私有组件。
2. bridge 的最小内部数据面收敛为：
   - `StageSchemaSpec`：表达 text / json_object / json_schema 输出模式；
   - `StageBudgetHint`：表达 estimated input、target output、remaining tokens 与 budget lane；
   - `StageLlmCallRequest`：表达 request / trace / stage / task / messages / stage hint / budget / schema；
   - `StageLlmCallResult`：表达 normalized response、`ErrorInfo`、route、warnings、diagnostics；
   - `LlmFailureProjection`：表达统一的 cognition-facing 失败投影。
3. `build_llm_request()` 直接把 canonical `stage_name` 与 `task_type` 投影到 `LLMGenerateRequest.stage` / `task_type`，并把 `StageModelHint` 转为 `ModelSelectionHint`；bridge 不生成私有 route alias，也不选择 prompt release。
4. `derive_budget_hint()` 会把 request messages 与 `BudgetContext` 折叠为 `estimated_input_tokens`、`target_output_tokens`、`remaining_tokens` 和 `budget_tier`，使 llm 路由能看到预算压力而不获得 Runtime 控制权。
5. `normalize_llm_response()` 与 `project_llm_failure()` 会：
   - 把 `ILLMManager` 成功结果归一化为 provider-neutral response；
   - 裁剪 `reasoning_content`、`provider_payload`、`raw_prompt`、`authorization`、`secret_key` 等 provider-private 字段；
   - 把 llm 失败统一收敛为带 canonical stage、`cognition.llm_bridge` source ref 和明确 diagnostic 的 `ErrorInfo`；
   - 保持 fail-fast，不引入本地 retry、breaker 或 prompt governance 分支。

## 4. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| bridge 是 cognition→llm 的唯一内部桥接 owner | `cognition/src/llm/CognitionLlmBridge.h`、`cognition/src/llm/CognitionLlmBridge.cpp` | 020 不修改 façade 主链，也不把桥接逻辑散落到五段 owner |
| `StageModelHint.stage_name` 必须直接透传 canonical key | `build_llm_request()`、`StageModelHintProjectionTest.cpp` | `planning/execution/reflection/response` 以原样进入 `LLMGenerateRequest.stage` |
| bridge 必须做预算提示投影，但不越权做 Runtime 决策 | `derive_budget_hint()`、`CognitionLlmBridgeProjectionTest.cpp` | output token clamp、budget tier 与 runtime budget hint 可被 llm 请求看到 |
| bridge 必须 fail-fast 归一化错误且不自建 retry/breaker | `project_llm_failure()`、`CognitionLlmBridgeErrorMappingTest.cpp` | 失败结果转为统一 `ErrorInfo`，diagnostic 保留 failure category |
| provider-private 内容不得穿透到阶段结果 | `normalize_llm_response()`、`CognitionLlmBridgeProjectionTest.cpp` | `reasoning_content` 等字段被显式 redaction |

## 5. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增 bridge 私有 owner 与 supporting structs | 让测试能直接构造 stage call 并观察 normalized result | `Build_CMakeTools(buildTargets=["dasall_cognition_llm_bridge_projection_unit_test","dasall_cognition_llm_bridge_error_mapping_unit_test","dasall_stage_model_hint_projection_unit_test"])` | 若 llm include / linking 边界错误，只回修 bridge 私有接线，不提前修改 façade |
| B2 | 实现 stage hint / schema / budget 到 `LLMGenerateRequest` 的投影 | 验证 canonical stage、schema ref、output token clamp 与 selection hint lane | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_llm_bridge_projection_unit_test` | 若 stage projection 偏移，只回修 `build_llm_request()` 和 `derive_budget_hint()` |
| B3 | 实现 llm failure projection 与 provider-private redaction | 验证 canonical stage error surface 与 redaction marker | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_llm_bridge_error_mapping_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_model_hint_projection_unit_test` | 若失败映射或 redaction 不稳，只回修 bridge 内部归一化逻辑，不扩到 021 / 023 |

## 6. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_cognition_llm_bridge_projection_unit_test","dasall_cognition_llm_bridge_error_mapping_unit_test","dasall_stage_model_hint_projection_unit_test"])`
   - 第一次结果：失败；`CognitionLlmBridge.h` 误把 llm public 头写成 `llm/...` include 路径。
   - 第二次结果：失败；桥接测试只拿到 `ModelSelectionHint` 前置声明，无法解引用，同时 `sanitize_payload()` 内部 helper 的 `nodiscard` 产生无意义告警。
   - 同一 slice 修补后复跑：通过，三条目标全部完成编译链接。
2. `RunCtest_CMakeTools(tests=["CognitionLlmBridgeProjectionTest","CognitionLlmBridgeErrorMappingTest","StageModelHintProjectionTest"])`
   - 结果：失败，工具返回仓库既有通用错误 `生成失败`；不作为代码失败证据。
3. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_llm_bridge_projection_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_llm_bridge_error_mapping_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_model_hint_projection_unit_test`
   - 结果：通过；三条桥接测试二进制全部零输出退出。

## 7. 完成判定与边界

1. COG-TODO-020 已完成：bridge private owner、stage hint projection、budget hint、错误映射和 provider-private redaction 均已落盘并通过 focused tests。
2. 本轮没有提前实现 `StageOutputValidator`、`CognitionTelemetry` 或 `CognitionFacade` 主链接线；021、022、023 继续分别收口 schema invariant、观测 fail-open 与 façade orchestration。
3. bridge 仍不持有 prompt asset 选择、message 组装、provider lifecycle、retry 或 circuit breaker 逻辑，继续遵守 ADR-006 / ADR-007 边界。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| owner 边界 | PASS：bridge 只负责 llm request/response projection，不越权进入 façade 或 runtime 控制面 |
| canonical stage key | PASS：`planning/execution/reflection/response` 直接进入 llm 请求，不维护第二套 alias |
| provider-private redaction | PASS：`reasoning_content` 等字段在 normalized payload 中被显式替换为 `[REDACTED]` |
| 错误语义 | PASS：llm failure 统一投影为带 canonical stage 与 `cognition.llm_bridge` source ref 的 `ErrorInfo` |
| 韧性边界 | PASS：bridge fail-fast，不自建 retry / breaker；韧性控制继续留在 llm / runtime owner |