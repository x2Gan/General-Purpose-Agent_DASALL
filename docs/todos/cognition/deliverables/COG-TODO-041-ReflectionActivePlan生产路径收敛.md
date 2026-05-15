# COG-TODO-041 Reflection active PlanGraph 生产路径收敛

状态：Done
日期：2026-05-15
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build reflection active plan 透传收敛

## 1. 契约结论

1. `ReflectionRequest` 现在以 additive 方式新增 `std::optional<plan::PlanGraph> active_plan`，并继续保留 `active_plan_ref`，因此 public surface 可以携带完整 active plan，同时不破坏既有 caller 的兼容性。
2. `CognitionFacade::reflect()` 不再把 `ReflectionAnalysisRequest.active_plan` 固定置空；当 caller 已提供 active plan 时，façade 会把它原样传给 `ReflectionEngine`。
3. `ReflectionEngine` 既有基于 active node / success signal 的分析逻辑无需修改；041 的根因是 public request 到 internal analysis request 的生产映射缺口，而不是 reflection owner 本体缺功能。
4. `runtime/src/AgentOrchestrator.cpp` 的 `make_reflection_request()` 本轮不伪造 `PlanGraph`。当前 runtime reflection 边界只持有 request/context/latest observation 与最小 belief projection，没有权威 active plan graph owner；因此 041 只打通“已提供 active plan 时不再被 façade 静默丢弃”的合同。

## 2. 本轮收敛范围

1. 更新 `cognition/include/CognitionTypes.h`，为 `ReflectionRequest` 增加 optional `active_plan` 字段。
2. 更新 `cognition/src/CognitionFacade.cpp`，把 `request.active_plan` 透传到 `ReflectionAnalysisRequest.active_plan`。
3. 更新 `tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp`，冻结 `ReflectionRequest.active_plan` 的公共接口类型。
4. 更新 `tests/unit/cognition/CognitionFacadeFlowTest.cpp`，新增 façade-level regression，证明 retryable failure 场景下 active plan node id 会进入 reflection rationale，从而能捕获 future regression 再次把 active plan 置空。

## 3. 交付物

1. `cognition/include/CognitionTypes.h`
2. `cognition/src/CognitionFacade.cpp`
3. `tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp`
4. `tests/unit/cognition/CognitionFacadeFlowTest.cpp`
5. 本交付物
6. 专项 TODO 中的 COG-TODO-041 状态回写

## 4. Validation

1. `Build_CMakeTools(buildTargets=["dasall_cognition","dasall_cognition_interface_surface_unit_test","dasall_reflection_engine_decision_unit_test","dasall_cognition_facade_flow_unit_test","dasall_cognition_runtime_interaction_contract_integration_test"])`
   - 结果：通过。
   - 备注：构建期间仍出现 `MockCognitionFixture.h` / `CognitionRuntimeInteractionContractTest.cpp` 的 `policy_snapshot` missing initializer warning；这是既有 044 范围内问题，未被 041 放大。
2. `RunCtest_CMakeTools(tests=["CognitionInterfaceSurfaceTest","ReflectionEngineDecisionTest","CognitionFacadeFlowTest","CognitionRuntimeInteractionContractTest"])`
   - 结果：通过，4 条聚焦 tests 全绿。
   - 覆盖点：
     - `CognitionInterfaceSurfaceTest` 冻结 `ReflectionRequest.active_plan` 的 public type。
     - `ReflectionEngineDecisionTest` 继续证明 engine 本体直接消费 active plan 的既有能力没有回退。
     - `CognitionFacadeFlowTest` 新增回归，证明 public request 中的 active plan 会影响 reflection rationale，不再在 façade 处丢失。
     - `CognitionRuntimeInteractionContractTest` 继续证明 runtime↔cognition 合同未被 041 的 additive 字段扩展破坏。

## 5. 完成判定

COG-TODO-041 已完成。

1. public `ReflectionRequest` 已能非破坏性承载 active `PlanGraph`。
2. production façade 路径在 caller 已提供 active plan 时，会将其传递给 `ReflectionEngine`。
3. 041 没有把 `PlanGraph` 推入 shared contracts，也没有改变 runtime / recovery owner 边界。
4. focused acceptance 已能捕获“再次把 active plan 置空”的回归。