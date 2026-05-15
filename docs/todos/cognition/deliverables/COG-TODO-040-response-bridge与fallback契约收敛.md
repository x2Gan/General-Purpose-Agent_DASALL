# COG-TODO-040 response bridge 与 fallback 契约收敛

状态：Done
日期：2026-05-15
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Design + Build response 行为口径冻结

## 1. 契约结论

1. runtime live cognition path 当前在存在 `latest_observation` 时，会通过 `make_response_build_request()` 把 `prefer_observation_projection` 置为 `true`；因此非 factory profile 不以“必须调用 response bridge”作为通过条件。
2. `ResponseBuilder::select_response_mode()` 的当前 owner 顺序是：
   - `TemplatePreferred` 时优先模板回退
   - 显式 `prefer_template` 时优先模板回退
   - 有 observation payload 且 `prefer_observation_projection=true` 时优先 observation projection
   - 其后才是 response LLM bridge
3. `StagePolicyResolver::resolve_response_plan()` 当前将 `factory_test` 冻结为 `TemplatePreferred`；因此该 profile 的权威行为是模板优先，并且不要求发出 response bridge request。
4. 非 factory profile 的权威行为口径冻结为：
   - 必须产生显式 terminal status 与非空 response text
   - 不要求必须存在 response bridge request
   - 若确实发出 response bridge request，则 `route` / `timeout` 必须来自真实 runtime policy snapshot
5. response bridge 若失败且模板回退被允许，仍必须回退到现有 template fallback；这一点继续由 `ResponseBuilderTemplateFallbackTest` 作为 owner regression。

## 2. 本轮收敛范围

1. 不改变 `runtime/src/AgentOrchestrator.cpp` 与 `cognition/src/response/ResponseBuilder.cpp` 的现有生产顺序，只把当前已通过 focused tests 的行为冻结为明确契约。
2. 通过 `tests/integration/cognition/CognitionProfileCompatibilityTest.cpp` 把“非 factory profile response stage 可选、factory_test 明确禁止 response bridge”落成可执行断言，避免测试口径再次漂移。
3. `CognitionReviewRegressionTest` 保持不强制 response bridge，作为与本轮冻结口径一致的 review regression owner。

## 3. 交付物

1. `tests/integration/cognition/CognitionProfileCompatibilityTest.cpp`
2. 本交付物
3. 专项 TODO 中的 COG-TODO-040 状态回写

## 4. Validation

1. `Build_CMakeTools(buildTargets=["dasall_runtime_cognition_loop_smoke_unit_test","dasall_cognition_runtime_integration_test","dasall_cognition_profile_compatibility_integration_test","dasall_cognition_review_regression_integration_test","dasall_response_builder_template_fallback_unit_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["RuntimeCognitionLoopSmokeTest","CognitionRuntimeIntegrationTest","CognitionProfileCompatibilityTest","CognitionReviewRegressionTest","ResponseBuilderTemplateFallbackTest"])`
   - 结果：通过，5 条聚焦 tests 全绿。
   - 覆盖点：
     - `RuntimeCognitionLoopSmokeTest` 与 `CognitionRuntimeIntegrationTest` 继续证明 runtime+cognition completion message 不依赖 response bridge 强制存在。
     - `CognitionProfileCompatibilityTest` 现在显式冻结：`factory_test` 禁止 response bridge；其它 profile 只要求显式 terminal result，且若发出 response request 必须继承 snapshot route / timeout。
     - `CognitionReviewRegressionTest` 继续不把 response bridge 作为必需 owner，从而与 profile compatibility 的“可选而非必需”口径保持一致。
     - `ResponseBuilderTemplateFallbackTest` 继续证明 response bridge 失败后模板回退仍可用。

## 5. 完成判定

COG-TODO-040 已完成。

1. response bridge 与 template fallback 的产品语义已冻结为二值规则，而不再同时被不同测试要求为“必需”和“可选”。
2. `factory_test` 的权威行为是模板优先且禁止 response bridge。
3. 非 factory profile 当前不以 response bridge 为必需条件；若 response bridge 被发出，其 route / timeout 仍必须来自真实 runtime policy snapshot。
4. focused acceptance 不再出现 completion message 与 response bridge request 断言冲突。