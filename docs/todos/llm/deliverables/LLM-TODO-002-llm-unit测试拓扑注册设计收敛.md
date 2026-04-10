# LLM-TODO-002 llm unit 测试拓扑注册设计收敛

日期：2026-04-10
任务：LLM-TODO-002
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 LLM-TODO-002 定义为“注册 llm unit 测试拓扑”，其完成判定不是业务语义实现，而是 `tests/unit/llm` 能被顶层 unit 聚合与测试发现链稳定识别。
2. [tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) 已经预留 `add_subdirectory(llm)`，说明 llm unit 子目录早已进入顶层 unit 目录树；当前缺口只剩子目录内真实 test target 和顶层聚合列表登记。
3. [tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt) 在本轮前只有占位注释，说明 llm unit discoverability 目前完全缺失；若直接进入 005~011，会导致接口任务只能“编译通过”而无法进入 `dasall_unit_tests` 聚合验证。
4. [tests/unit/platform/linux/CMakeLists.txt](../../../../tests/unit/platform/linux/CMakeLists.txt) 已存在名为 `InterfaceSurfaceTest` 的平台单测，因此 llm 侧不能复用同名 `ctest` 名称，否则 discoverability 会与既有 platform 用例冲突。
5. [tests/unit/services/CMakeLists.txt](../../../../tests/unit/services/CMakeLists.txt) 已展示仓库内稳定模式：子目录内创建 executable、链接模块库、调用 `dasall_apply_common_options()`、再用 `add_test()` 与 `set_tests_properties(... LABELS "unit")` 完成 discoverability 注册。

## 2. 外部参考

1. CMake 官方 `add_test(NAME ... COMMAND ...)` 文档明确指出，测试只有在 `enable_testing()` 打开后并通过 `add_test()` 注册，才会进入 CTest；测试属性也只能在创建该测试的目录中设置。本轮据此把 llm unit 注册收口在 `tests/unit/llm/CMakeLists.txt`，避免只在顶层聚合里追加 target 而漏掉真实测试注册。参考：https://cmake.org/cmake/help/latest/command/add_test.html
2. CTest 官方手册说明 `ctest -N` 用于列出可执行测试，`-L` 根据标签筛选测试集合。因此 llm unit 入口除了要有 executable target，还必须带上 `unit` 标签，并能在测试列表中以唯一名称出现。参考：https://cmake.org/cmake/help/latest/manual/ctest.1.html

## 3. Design 结论

1. `tests/unit/llm/CMakeLists.txt` 负责 llm unit topology 的本地注册：新增最小 executable、链接 `dasall_llm` 与 `dasall_test_support`、并在同目录里完成 `add_test()` 与 label 设置。
2. 顶层 [tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) 仍作为 `dasall_unit_tests` 的唯一聚合入口，因此必须显式把 llm executable target 加入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`，确保聚合构建和 `-L unit` 路径都包含 llm。
3. llm 侧最小 surface test 保持唯一的 `ctest` 名称 `LLMInterfaceSurfaceTest`，避免与 platform 侧既有 `InterfaceSurfaceTest` 冲突，同时为 005~011 后续扩展公共接口断言保留稳定测试壳。
4. 本轮只做 topology/discoverability，不提前实现 llm 公共头文件内容；测试源码只验证“名称唯一 + 标签可发现 + target 命名空间稳定”，把真实接口断言留给 005~011 在同一测试文件内继续补齐。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| llm unit 子目录内的最小 test target 注册 | [tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt) |
| llm unit 聚合 target 回传到顶层 unit 列表 | [tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |
| collision-free 的 llm surface test 占位用例 | [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |

## 5. Build 三件套

1. 代码目标：在 `tests/unit/llm/` 下新增最小 surface test executable 与 `LLMInterfaceSurfaceTest` 注册，并把对应 target 纳入 `dasall_unit_tests` 聚合列表。
2. 测试目标：验证 `LLMInterfaceSurfaceTest` 能在 unit 聚合构建中被编译执行、进入测试列表，并带有 `unit` 标签且不与既有 `InterfaceSurfaceTest` 冲突。
3. 验收命令：
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`
   - `ListTests_CMakeTools`
   - `RunCtest_CMakeTools` 运行 `LLMInterfaceSurfaceTest`

## 6. 风险与回退

1. 当前 `InterfaceSurfaceTest.cpp` 只承载 topology 锚点和命名冲突防护，不应被误解为 llm 公共接口已经冻结；后续接口任务必须在同一测试壳上继续补真实断言，而不是新造重复 surface test。
2. 顶层 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS` 现在显式登记了 llm target；后续新增 llm unit 用例时应继续由 `tests/unit/llm/CMakeLists.txt` 管理，并在需要时统一扩充聚合列表，避免再次回到“子目录有测试但顶层不构建”的漂移。