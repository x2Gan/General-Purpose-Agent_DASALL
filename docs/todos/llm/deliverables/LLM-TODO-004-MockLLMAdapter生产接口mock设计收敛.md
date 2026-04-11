# LLM-TODO-004 MockLLMAdapter 生产接口 mock 设计收敛

日期：2026-04-11
任务：LLM-TODO-004
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 已将 LLM-TODO-004 定义为“升级 MockLLMAdapter 为生产接口 mock”，完成判定明确要求 `MockLLMAdapter` 继承 `ILLMAdapter`，并补 `MockLLMAdapterSurfaceTest` 覆盖可编程返回、调用计数与 `health_check()`。
2. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.13 明确要求 tests/mocks 中的 MockLLMAdapter 升级为基于生产接口的 mock，但该动作只属于 Build 任务，不写回 shared contracts。
3. [llm/include/ILLMAdapter.h](../../../../llm/include/ILLMAdapter.h) 已在 005 冻结 `init()`、`generate()`、`stream_generate()`、`health_check()` 四个 SPI 入口；[llm/include/stream/StreamSessionRef.h](../../../../llm/include/stream/StreamSessionRef.h) 与 [llm/src/adapters/AdapterCallResult.h](../../../../llm/src/adapters/AdapterCallResult.h) 也已落盘，足以支撑 mock 升级到真实接口面。
4. [tests/mocks/include/MockLLMAdapter.h](../../../../tests/mocks/include/MockLLMAdapter.h) 在本轮之前仍是字符串 `invoke()` 脚手架；[tests/unit/runtime/RuntimeSmokeTest.cpp](../../../../tests/unit/runtime/RuntimeSmokeTest.cpp) 仍直接依赖这一 legacy helper，因此 004 需要在升级生产接口 mock 的同时保留现有 smoke fixture 兼容性。
5. [tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt) 与 [tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) 已提供 llm unit 注册点，允许本轮新增独立的 `MockLLMAdapterSurfaceTest` 并接入 `dasall_unit_tests` 聚合目标。

## 2. 外部参考

1. cppreference 对 abstract class 的说明确认：抽象基类适合作为稳定接口面，具体测试替身只需在派生类中实现纯虚函数即可。这支撑了把 `MockLLMAdapter` 升级为 `ILLMAdapter` 派生 mock，而不是继续维持独立于生产接口之外的字符串脚手架。参考：https://en.cppreference.com/w/cpp/language/abstract_class
2. Martin Fowler 在 Mocks Aren't Stubs 中强调 test double 不仅可以提供 canned answer，也可以记录调用信息用于验证。这与本轮为 `MockLLMAdapter` 增加调用计数、最后一次请求记录和可编程返回值的目标一致。参考：https://martinfowler.com/articles/mocksArentStubs.html

## 3. Design 结论

1. `MockLLMAdapter` 在本轮升级为 `ILLMAdapter` 派生 mock，并实现 `init()`、`generate()`、`stream_generate()`、`health_check()` 四个入口，直接对齐 llm 已冻结的生产接口面。
2. mock 需要同时支持两类验证模式：
   - 生产接口模式：通过 `set_generate_handler()`、`set_generate_result()`、`set_stream_session()`、`set_health_status()` 等入口提供可编程返回。
   - 现有脚手架兼容模式：保留 `set_handler()`、`set_default_response()` 和 `invoke()`，确保既有 runtime smoke test 不被 004 破坏。
3. mock 必须显式记录 `init()`、`generate()`、`stream_generate()`、`health_check()` 的调用次数，以及最近一次 init config、unary request、stream request、observer 和 prompt 字符串，以支撑后续 unary/fallback/integration 测试夹具复用。
4. 由于 `ILLMAdapter.h` 目前只前向声明 `HealthStatus`，本轮在 mock 头内补齐一个最小 test-local `dasall::llm::HealthStatus` 定义，只表达 `ready`、`degraded` 和 `message` 三个健康断言所需字段；这用于夹具落地，不等价于推进新的 shared 或 public contract。
5. `stream_generate()` 仍只返回 `StreamSessionRef` 占位，不在 004 中提前引入 streaming 生命周期、取消语义或 observer 分发逻辑。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| MockLLMAdapter 升级为生产接口 mock | [tests/mocks/include/MockLLMAdapter.h](../../../../tests/mocks/include/MockLLMAdapter.h) |
| 新增独立的 mock surface 单测 | [tests/unit/llm/MockLLMAdapterSurfaceTest.cpp](../../../../tests/unit/llm/MockLLMAdapterSurfaceTest.cpp) |
| 将新单测接入 llm unit CMake 与 unit 聚合目标 | [tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：升级 `MockLLMAdapter` 为 `ILLMAdapter` 派生 mock，补齐四个 SPI 入口、调用计数、最近一次调用记录与 legacy helper 兼容层。
2. 测试目标：新增 `MockLLMAdapterSurfaceTest`，覆盖可编程 success/failure 返回、`health_check()` 状态、`stream_generate()` 占位返回和 legacy `invoke()` 兼容行为。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 定向执行 `MockLLMAdapterSurfaceTest`

## 6. 风险与回退

1. `HealthStatus` 当前仅在 mock 头内提供最小定义；若后续 llm 正式落盘统一健康类型，004 的 test-local 定义必须让位于正式头文件，而不是反向主导生产语义。
2. 为兼容 [tests/unit/runtime/RuntimeSmokeTest.cpp](../../../../tests/unit/runtime/RuntimeSmokeTest.cpp)，本轮保留了 legacy `invoke()` 路径；待 runtime 真正切换到生产接口夹具后，可再评估是否移除该兼容层。
3. `stream_generate()` 本轮仍是 lifecycle placeholder；任何取消、backpressure 或 observer 分发能力都必须留给 streaming 后置任务，而不是在 004 中偷偷扩面。
