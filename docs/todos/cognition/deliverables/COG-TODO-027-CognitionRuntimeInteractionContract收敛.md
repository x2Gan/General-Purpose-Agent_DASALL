# COG-TODO-027 CognitionRuntimeInteractionContract 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md

## 1. 实施结论

1. 新增 `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp`。
2. 通过可注入 cognition engine 的方式覆盖两类交互契约场景：
   - `ExecuteAction` 路径：Runtime 可继续推进到完成态。
   - 非可执行决策路径：Runtime 显式失败并回流到 `main_loop` 错误语义。
3. `tests/integration/cognition/CMakeLists.txt` 将 `CognitionRuntimeInteractionContractTest` 从 placeholder 切换为真实可执行目标。

## 2. 验证命令与结果

1. `cmake -S . -B build-ci`
2. `cmake --build build-ci --target dasall_cognition_runtime_interaction_contract_integration_test dasall_contract_tests`
3. `ctest --test-dir build-ci -R "CognitionRuntimeInteractionContractTest|ContextPacketFieldContractTest|ReflectionDecisionContractTest|AgentResultContractTest|MainFlowContractE2ETest" --output-on-failure`

结果：契约回归与 cognition-runtime 交互契约测试全部通过。

## 3. 约束符合性

1. 保持 `ActionDecision -> Runtime FSM` 边界验证，不扩张 shared contracts。
2. 错误回流仍由 Runtime 收敛，未在 cognition 内执行恢复动作。
