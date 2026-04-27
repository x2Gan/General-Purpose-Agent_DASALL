# COG-TODO-026 CognitionRuntimeIntegration 主成功链收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md

## 1. 实施结论

1. 新增 runtime 单测 `tests/unit/runtime/RuntimeCognitionLoopSmokeTest.cpp`，验证 Runtime handoff 到 cognition decide/response 的主成功链。
2. 新增集成测试 `tests/integration/cognition/CognitionRuntimeIntegrationTest.cpp`，验证 runtime↔cognition 主链路可稳定产出 `AgentResult`。
3. 新增共享 fixture `tests/fixtures/runtime/CognitionRuntimeIntegrationFixture.h`，复用 sqlite memory + cognition + tool manager 真端口组装，避免测试重复拼装。
4. 修复 runtime 侧 live unary tool request 投影与 `ActionDecision` 新结构对齐：
   - `runtime/src/AgentOrchestrator.cpp` 改为读取 `tool_intent_hint.tool_name`
   - 参数提示改为由 `tool_intent_hint.argument_hints` 生成 payload。

## 2. 相关接线

1. `tests/unit/runtime/CMakeLists.txt` 增加 `dasall_runtime_cognition_loop_smoke_unit_test` 目标与 `RuntimeCognitionLoopSmokeTest` 注册。
2. `tests/integration/cognition/CMakeLists.txt` 将 `CognitionRuntimeIntegrationTest` 从 placeholder 切换为真实可执行测试目标。

## 3. 验证命令与结果

1. `cmake -S . -B build-ci`
2. `cmake --build build-ci --target dasall_runtime_cognition_loop_smoke_unit_test dasall_cognition_runtime_integration_test`
3. `ctest --test-dir build-ci -R "RuntimeCognitionLoopSmokeTest|CognitionRuntimeIntegrationTest" --output-on-failure`

结果：两项测试均通过。

## 4. 备注

1. TODO 原命令中 `-G "Unix Makefiles"` 与当前 `build-ci` 既有 Ninja cache 冲突；本轮按仓库基线使用现有 `build-ci` 生成器执行等效验证链路。
2. 本任务未回退为 stub 路径，验证的是 runtime live unary 真端口主链。
