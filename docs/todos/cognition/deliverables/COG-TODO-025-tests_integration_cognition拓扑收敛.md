# COG-TODO-025 tests/integration/cognition 拓扑收敛

状态：Done  
日期：2026-04-27  
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md

## 1. 本地依据

1. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 将 COG-TODO-025 定义为 integration discoverability 任务，代码目标限定为：
   - 更新 `tests/integration/CMakeLists.txt`
   - 新增 `tests/integration/cognition/CMakeLists.txt`
2. 认知详设（7.1 COG-D10、8.1、9.1）要求先完成 integration 拓扑注册，再进入 026~029 的主链/契约/故障/profile 集成验证。
3. COG-TODO-024 已完成，前置依赖满足，当前任务可执行。

## 2. 变更结论

1. 在 `tests/integration/CMakeLists.txt` 新增 `add_subdirectory(cognition)`，并把 `${DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS}` 聚合进 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS`。
2. 新增 `tests/integration/cognition/CMakeLists.txt`，注册 cognition integration 拓扑占位用例（`cmake -E true`）：
   - `CognitionRuntimeIntegrationTest`
   - `CognitionRuntimeInteractionContractTest`
   - `CognitionFailureInjectionIntegrationTest`
   - `CognitionProfileCompatibilityTest`
   - `CognitionProfileCompatibilityIntegrationTest`（为兼容当前 TODO 验收正则增加的别名）
3. 新增 target 聚合变量 `DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS` 并 `PARENT_SCOPE` 回传顶层 integration 聚合。

## 3. 验证证据

1. 任务原始命令（Unix Makefiles）在当前环境失败：`build-ci` 目录已有 Ninja 生成缓存，出现 generator mismatch。
2. 按仓库已知基线改用现有 `build-ci` 完成配置与发现性验证：
   - `cmake -S . -B build-ci`
   - `ctest --test-dir build-ci -N | rg "Cognition(Runtime|FailureInjection|ProfileCompatibility)IntegrationTest|CognitionRuntimeInteractionContractTest"`
3. 命令输出已发现以下 cognition integration 用例：
   - `CognitionRuntimeIntegrationTest`
   - `CognitionRuntimeInteractionContractTest`
   - `CognitionFailureInjectionIntegrationTest`
   - `CognitionProfileCompatibilityIntegrationTest`
4. `cmake --build build-ci --target dasall_integration_tests` 当前受仓库现存 `runtime/src/AgentOrchestrator.cpp` 字段不匹配错误阻塞（`ActionDecision.tool_name` / `tool_arguments_payload` 不存在），不属于本任务改动引入。

## 4. 完成判定

COG-TODO-025 完成。判定依据：

1. `tests/integration/cognition/` 拓扑已注册并被顶层 integration 聚合。
2. `ctest -N` 可发现 cognition integration 用例并通过 TODO 指定正则过滤。
3. 改动范围保持在任务约束内，无跨任务扩张。
