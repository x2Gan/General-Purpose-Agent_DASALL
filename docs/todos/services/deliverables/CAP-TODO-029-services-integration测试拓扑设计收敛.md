# CAP-TODO-029 services integration 测试拓扑设计收敛

日期：2026-04-09
任务：CAP-TODO-029
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/ssot/InfraIntegrationTopology.md](../../../ssot/InfraIntegrationTopology.md) 已冻结 integration discoverability 约束：tests 必须通过 `add_test()` 进入顶层 `ctest -N`，并以 `integration` 作为必选标签。因此 services integration 的 CMake 接线不能只停留在子目录内自洽，还必须回传到顶层聚合 target。
2. [tests/integration/CMakeLists.txt](../../../../tests/integration/CMakeLists.txt) 在本轮前仍手工硬编码 services integration executable 列表，意味着新增 services integration test 时必须同时修改两处 CMake，容易造成 discoverability 与 `dasall_integration_tests` 编译依赖漂移。
3. [tests/integration/services/CMakeLists.txt](../../../../tests/integration/services/CMakeLists.txt) 在本轮前使用四段重复的 `add_executable() + add_test() + set_tests_properties()` 样板，缺少可复用注册入口，也没有导出 services integration target 列表给顶层聚合。
4. [tests/mocks/include/CapabilityServicesLoopbackFixture.h](../../../../tests/mocks/include/CapabilityServicesLoopbackFixture.h) 已在 CAP-TODO-028 关闭 CAP-BLK-004，为 smoke executable 提供 execute/query/catalog 最小闭环，因此 029 可以聚焦“注册与 discoverability”，不需要再设计新的测试支撑对象。
5. [tests/integration/services/CapabilityServicesAuditIntegrationTest.cpp](../../../../tests/integration/services/CapabilityServicesAuditIntegrationTest.cpp) 等已有 services integration 用例已经证明 `dasall_services` 与 `dasall_test_support` 的链接组合可行；本轮只需把这类注册方式收敛为统一入口，并新增一个最小 smoke executable 作为拓扑基线。

## 2. 外部参考

1. CMake 官方 `add_test(NAME ... COMMAND ...)` 文档明确指出：测试只有在 `add_test()` 注册后才会进入 CTest，且测试属性必须在创建该测试的目录内设置。这支持 029 在 `tests/integration/services/CMakeLists.txt` 中集中封装 services integration 的注册宏和标签设置。参考：https://cmake.org/cmake/help/latest/command/add_test.html
2. CTest 官方手册说明 `ctest -N` 用于列出将会执行的测试而不实际运行，`-L` 基于 label 过滤测试集合。这直接支持 029 把验收拆成 `ctest -N` 的 discoverability 验证和 `-L integration` 的标签执行验证。参考：https://cmake.org/cmake/help/latest/manual/ctest.1.html

## 3. Design 结论

1. `tests/integration/services/CMakeLists.txt` 收敛为单一注册入口：通过 `dasall_add_services_integration_test(...)` 宏统一负责 `add_executable()`、`target_link_libraries()`、`target_include_directories()`、`dasall_apply_common_options()`、`add_test()` 和 `integration` 标签设置，消除重复样板。
2. services integration executable 列表以 `DASALL_SERVICES_INTEGRATION_TEST_EXECUTABLE_TARGETS` 形式从子目录导出到顶层，由 [tests/integration/CMakeLists.txt](../../../../tests/integration/CMakeLists.txt) 统一并入 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS`，避免以后新增 services integration target 时再去手工同步顶层列表。
3. 新增 [tests/integration/services/CapabilityServicesSmokeIntegrationTest.cpp](../../../../tests/integration/services/CapabilityServicesSmokeIntegrationTest.cpp) 作为最小 smoke executable：它消费 `CapabilityServicesLoopbackFixture`，只验证 execute/query/catalog 的 loopback round-trip 和默认 local route，不提前承诺 audit/trace/metrics 的全量观测断言。
4. 029 的完成标准是“registration + discoverability”而不是“全量 smoke 语义完成”。因此本轮只把 CAP-TODO-030 从 Blocked 解到 Todo；030 仍负责补齐更强的 smoke 断言和 observability 证据。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| services integration 统一注册宏与导出 target 列表 | tests/integration/services/CMakeLists.txt |
| 顶层 integration 聚合 target 改为消费 services 导出列表 | tests/integration/CMakeLists.txt |
| 新增最小 services smoke executable 并赋予 `integration` 标签 | tests/integration/services/CapabilityServicesSmokeIntegrationTest.cpp |
| 回写 029 状态、030 可执行性与 discoverability 证据 | docs/todos/services/DASALL_capability_services子系统专项TODO.md |

## 5. Build 三件套

1. 代码目标：统一 services integration 的 CMake 注册入口，导出 services integration target 列表，并新增最小 smoke executable。
2. 测试目标：验证 `CapabilityServicesSmokeIntegrationTest` 进入顶层 `ctest -N` 列表、保留 `integration` 标签，并能随 `dasall_integration_tests` 聚合 target 一起构建和执行。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests`
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -L integration`
   - `ctest --test-dir build-ci --output-on-failure -R CapabilityServicesSmokeIntegrationTest`

## 6. 风险与回退

1. 当前 smoke executable 只验证最小 loopback round-trip 与默认 local route，不涵盖 audit/trace/metrics 字段可观测性；这些更强断言继续留给 CAP-TODO-030，避免在 029 里把“注册”与“语义验收”混成一轮。
2. 顶层聚合现在依赖 services 子目录导出的 target 列表；若后续有人绕开宏直接新建 services integration target，就会再次引入 discoverability 漂移，因此新增 services integration 用例应始终通过 `dasall_add_services_integration_test(...)` 注册。