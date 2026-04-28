# COG-TODO-036 cognition integration 证据口径收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md

## 1. 边界与职责

1. 本任务只处理 cognition integration 测试注册与 gate 证据口径，不改动 `CognitionProfileCompatibilityTest` 的业务语义、运行逻辑或 profile 矩阵断言。
2. `tests/integration/cognition/CMakeLists.txt` 的唯一职责是注册真实 integration executable，并把真实 target 汇总到 `DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS`。
3. gate 证据只统计真实 `add_test(NAME ... COMMAND <executable>)` 项；不得再用空跑命令 placeholder 冒充 discoverability 或通过率证据。

## 2. 评审结论

1. 旧实现为满足早期 TODO 正则，引入了 profile compatibility 历史空跑别名，并以空跑命令注册。
2. 该 alias 会进入 `ctest -N` 与 gate regex 统计，导致 `COG-TODO-030` 中的 cognition 聚焦 gate 存在“真实 profile test + 空跑 alias”混合计数风险。
3. 本轮收敛策略是删除 placeholder 注册，让 profile compatibility 证据只保留真实测试名 `CognitionProfileCompatibilityTest`。

## 3. 数据与接口说明

1. 输入面：`tests/integration/cognition/CMakeLists.txt` 中的 integration test registration macro 和 `DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS` 聚合变量。
2. 输出面：
   - `ctest -N` 中 cognition integration 项只对应真实 executable。
   - gate 文档不再把历史空跑别名视为通过项。
3. 保持不变：
   - `dasall_cognition_profile_compatibility_integration_test` target。
   - `CognitionProfileCompatibilityTest` 的源文件、标签与测试语义。

## 4. 流程与时序

1. 删除 `dasall_add_cognition_integration_placeholder()` 及其 profile compatibility alias 注册。
2. 重新配置 `build-ci`，刷新 CTest 注册表。
3. 执行 `ctest --test-dir build-ci -N | rg "Cognition"`，确认 cognition integration discoverability 仍成立，且旧 alias 已消失。
4. 更新 `COG-TODO-030` 交付物，让 Gate-COG-10 的证据口径回落到真实 executable。

## 5. 文件范围

1. `tests/integration/cognition/CMakeLists.txt`
2. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md`
3. `docs/todos/cognition/deliverables/COG-TODO-030-cognition专项Gate与交付证据回写收敛.md`
4. 本交付物

## 6. Validation

1. `cmake -S . -B build-ci`
2. `ctest --test-dir build-ci -N | rg "Cognition"`

结果摘要：

1. cognition integration discoverability 仍保留：`CognitionRuntimeIntegrationTest`、`CognitionRuntimeInteractionContractTest`、`CognitionFailureInjectionIntegrationTest`、`CognitionProfileCompatibilityTest`。
2. 历史 profile compatibility 空跑别名已不再出现在 `ctest -N` 输出中。
3. cognition integration CMake 中已无空跑 placeholder 注册。

## 7. 完成判定

COG-TODO-036 已完成。判定依据：

1. cognition integration 聚合变量只包含真实 executable target。
2. `ctest -N` 中不存在由空跑命令驱动的 cognition gate。
3. gate 交付物已切换到真实测试名与真实命令证据口径。