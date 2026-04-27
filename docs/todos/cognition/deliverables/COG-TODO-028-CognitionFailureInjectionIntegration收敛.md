# COG-TODO-028 CognitionFailureInjectionIntegration 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md

## 1. 实施结论

1. 新增 `tests/integration/cognition/CognitionFailureInjectionIntegrationTest.cpp`。
2. 通过 failure-inject cognition engine + 可选 fallback response builder 覆盖五类故障/降级路径：
   - llm unavailable
   - schema violation
   - missing belief state
   - contradictory observation
   - response fallback
3. `tests/integration/cognition/CMakeLists.txt` 将 `CognitionFailureInjectionIntegrationTest` 从 placeholder 切换为真实可执行目标。

## 2. 验证命令与结果

1. `cmake -S . -B build-ci`
2. `cmake --build build-ci --target dasall_cognition_failure_injection_integration_test`
3. `ctest --test-dir build-ci -R "CognitionFailureInjectionIntegrationTest" --output-on-failure`

结果：测试通过，失败链路均以显式失败/降级结果返回，无静默吞错。
