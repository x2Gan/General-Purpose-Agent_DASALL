# LLM-TODO-003 llm integration 测试拓扑设计收敛

日期：2026-04-10
任务：LLM-TODO-003
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 LLM-TODO-003 定义为“注册 llm integration 测试拓扑”，其完成判定是 `tests/integration/llm` 被顶层 integration 聚合发现，而不是本轮就完成 unary 主链语义闭环。
2. [docs/ssot/InfraIntegrationTopology.md](../../../ssot/InfraIntegrationTopology.md) 明确要求 integration 用例必须可被 `ctest -N` 发现，且命名应包含组件前缀；因此 llm integration 不能只在本地目录新建 executable，还必须进入顶层 `tests/integration/CMakeLists.txt` 的 discoverability 汇总链。
3. [tests/integration/CMakeLists.txt](../../../../tests/integration/CMakeLists.txt) 在本轮前只接了 `infra`、`profiles`、`platform`、`services` 四个子目录，并通过显式列表汇总 integration target；llm 尚未进入该聚合图。
4. [tests/integration/services/CMakeLists.txt](../../../../tests/integration/services/CMakeLists.txt) 已提供仓库内成熟模式：子目录维护模块级 integration target 列表，通过 `add_test()` + `LABELS "integration"` 注册 smoke/failure/profile 用例，再把 target 列表 `PARENT_SCOPE` 回传给顶层聚合。
5. 当前仓库内尚无 `tests/integration/llm/` 目录；LLM-TODO-003 的最小交付必须先把目录、CMake 注册和 smoke executable 建起来，为后续 029~035、042 的真实 llm 集成语义测试预留稳定入口。

## 2. 外部参考

1. CMake 官方 `add_test(NAME ... COMMAND ...)` 文档指出，测试属性只能在创建该测试的目录内设置，因此 llm integration label 应在 `tests/integration/llm/CMakeLists.txt` 内和 `add_test()` 一起落下，不能依赖顶层追加补丁。参考：https://cmake.org/cmake/help/latest/command/add_test.html
2. CTest 官方手册说明 `-N` 用于列出将要执行的测试而不实际运行，`-L` 通过标签选择测试集合。本轮据此把 llm smoke 用例设置为 `integration;llm`，既满足 SSOT 的必选 `integration` 标签，也为后续 llm 定向筛选保留模块标签。参考：https://cmake.org/cmake/help/latest/manual/ctest.1.html

## 3. Design 结论

1. 新增 `tests/integration/llm/` 子目录，并在该目录内维护 `DASALL_LLM_INTEGRATION_TEST_EXECUTABLE_TARGETS`，使 llm integration target 的注册与未来扩展都停留在模块边界内。
2. 顶层 [tests/integration/CMakeLists.txt](../../../../tests/integration/CMakeLists.txt) 负责 `add_subdirectory(llm)` 并把 llm target 列表并入 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS`，确保 `dasall_integration_tests` 聚合构建与执行路径覆盖 llm。
3. 首个 smoke executable 命名为 `LLMSubsystemSmokeIntegrationTest`，目标名为 `dasall_llm_smoke_integration_test`，与 SSOT 中“组件前缀 + 场景名”约定对齐，并显式避免与 services 侧 `CapabilityServicesSmokeIntegrationTest` 冲突。
4. 本轮 smoke 只承担 topology/discoverability 锚点，不提前声明 PromptPipeline、LLMManager 或 MockAdapter 主链已打通；真实 unary smoke 语义继续留给 LLM-TODO-029 在相同入口上扩展。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| llm integration 子目录与模块级 target 列表 | [tests/integration/llm/CMakeLists.txt](../../../../tests/integration/llm/CMakeLists.txt) |
| 顶层 integration 聚合纳入 llm 子目录与 target 列表 | [tests/integration/CMakeLists.txt](../../../../tests/integration/CMakeLists.txt) |
| llm smoke integration 占位用例 | [tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp](../../../../tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp) |

## 5. Build 三件套

1. 代码目标：新增 `tests/integration/llm/` 子目录和最小 smoke executable，把 llm integration target 纳入顶层 `dasall_integration_tests` 聚合与 discoverability 清单。
2. 测试目标：验证 `LLMSubsystemSmokeIntegrationTest` 被 integration 聚合构建执行、进入测试列表，并带有 `integration` 标签且命名不与现有 services smoke 冲突。
3. 验收命令：
   - `Build_CMakeTools` 构建目标 `dasall_integration_tests`
   - `ListTests_CMakeTools`
   - `RunCtest_CMakeTools` 运行 `LLMSubsystemSmokeIntegrationTest`

## 6. 风险与回退

1. 当前 `LLMSubsystemSmokeIntegrationTest.cpp` 只作为 discoverability 骨架，不代表 llm integration 语义已经覆盖 Prompt/Route/Adapter 主链；后续 029~035 应继续在该入口上补真实集成断言，避免再造重复 smoke 名称。
2. 顶层 integration 聚合现在显式依赖 `DASALL_LLM_INTEGRATION_TEST_EXECUTABLE_TARGETS`；后续新增 llm integration 用例时必须继续通过 `tests/integration/llm/CMakeLists.txt` 注册并回传列表，避免再次出现“子目录有测试、顶层无发现”的漂移。