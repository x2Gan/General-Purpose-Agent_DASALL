# LLM-TODO-005 ILLMAdapter SPI 与适配配置对象设计收敛

日期：2026-04-10
任务：LLM-TODO-005
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.5.1 已冻结 `ILLMAdapter::init()`、`generate()`、`stream_generate()`、`health_check()` 四个 SPI 入口，并明确 `generate()` 必须返回 `AdapterCallResult`、不得通过异常传播失败。
2. 同一设计文档的 6.4.2 已把 [llm/include/LLMAdapterConfig.h](../../../../llm/include/LLMAdapterConfig.h) 和 [llm/src/adapters/AdapterCallResult.h](../../../../llm/src/adapters/AdapterCallResult.h) 列为 005 的直接落盘对象，说明本轮应冻结 adapter 配置面与 module-local 成功/失败结果边界，而不是提前进入 transport、streaming 生命周期或 `LLMSubsystemConfig` 实现。
3. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 LLM-TODO-005 定义为阶段 B 的首个原子任务，完成判定是“`generate()` 返回 `AdapterCallResult`、接口不抛异常且编译通过”。
4. [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) 在本轮前仍只是 topology anchor，无法冻结 llm adapter SPI 的签名与字段边界，因此必须在本轮补成真实的接口 surface test。

## 2. 外部参考

1. C++ Core Guidelines 的 C.121 / C.127 明确指出，作为接口使用的基类应保持纯抽象类，并提供虚析构函数，避免把状态混入稳定接口面。本轮据此保持 [llm/include/ILLMAdapter.h](../../../../llm/include/ILLMAdapter.h) 只暴露纯虚方法和虚析构，不引入任何基类状态或实现细节。参考：https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class
2. 同一份指南的 I.4 强调接口应精确且强类型化，避免把多个语义参数散落成弱类型原语。本轮据此把 adapter 初始化输入固定收敛为 [llm/include/LLMAdapterConfig.h](../../../../llm/include/LLMAdapterConfig.h) 单一配置对象，而不是在 `init()` 上继续扩散 provider id、timeout、header refs、capability tags 等离散参数。参考：https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#i4-make-interfaces-precisely-and-strongly-typed
3. cppreference 关于 abstract class 的说明再次确认：抽象基类可以通过前向声明的返回类型和参数类型暴露稳定声明面，而具体定义可在后续实现阶段补齐。本轮据此只在 [llm/include/ILLMAdapter.h](../../../../llm/include/ILLMAdapter.h) 中前向声明 `HealthStatus`、`StreamSessionRef`、`IStreamObserver`，避免把 011/012/036 的后续对象提前拖入 005。参考：https://en.cppreference.com/w/cpp/language/abstract_class

## 3. Design 结论

1. `ILLMAdapter` 继续保持为四入口纯抽象 SPI：`init()`、`generate()`、`stream_generate()`、`health_check()`；接口只冻结方法签名，不在 005 中引入 transport 注入、streaming 生命周期管理或 health 状态结构体定义。
2. `generate()` 的错误传播方式在本轮正式冻结为返回 `AdapterCallResult`，而不是抛异常或直接返回 `LLMResponse`。这确保 adapter transport/protocol 层失败事实仍停留在 llm 模块内部边界内。
3. `ILLMAdapter.h` 只前向声明 `AdapterCallResult`，不直接包含 [llm/src/adapters/AdapterCallResult.h](../../../../llm/src/adapters/AdapterCallResult.h)。这样做的目的是保住“result type 位于 module-local 路径、仅被真正需要其定义的实现和测试显式引入”的边界，不把内部结果对象误塑造成 shared/public contract。
4. `LLMAdapterConfig` 字段冻结为 `adapter_id`、`adapter_family`、`base_url`、`auth_ref`、`header_refs`、`timeout_ms`、`max_retries`、`capability_tags`，与详细设计 6.4.2 一致，不提前扩张 profile 投影视图或 provider 运行时状态。
5. `AdapterCallResult` 采用 success-or-error 二选一值边界：成功时只暴露 `response`；失败时暴露 `error` 与 `result_code`；并通过 `has_consistent_values()` 显式拒绝“成功和失败字段同时出现”的混合态。
6. `HealthStatus`、`StreamSessionRef`、`IStreamObserver` 继续保持前向声明，以确保 005 只冻结 adapter SPI，不越权推进 supporting types、配置投影和 streaming 生命周期。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 adapter 初始化配置对象 | [llm/include/LLMAdapterConfig.h](../../../../llm/include/LLMAdapterConfig.h) |
| 冻结四入口 adapter SPI 声明面 | [llm/include/ILLMAdapter.h](../../../../llm/include/ILLMAdapter.h) |
| 冻结 module-local 成功/失败返回对象 | [llm/src/adapters/AdapterCallResult.h](../../../../llm/src/adapters/AdapterCallResult.h) |
| 把 llm unit anchor 升级为真实接口冻结测试 | [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |

## 5. Build 三件套

1. 代码目标：新增 `ILLMAdapter`、`LLMAdapterConfig` 和 module-local `AdapterCallResult`，并把 `LLMInterfaceSurfaceTest` 升级为真实的 adapter SPI/字段冻结测试。
2. 测试目标：验证 `ILLMAdapter` 仍是纯抽象接口，`generate()` 明确返回 `AdapterCallResult`，`LLMAdapterConfig` 与 `AdapterCallResult` 字段边界稳定可断言。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_llm`、`dasall_unit_tests`
   - `RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest`

## 6. 风险与回退

1. 本轮故意没有定义 `HealthStatus`、`StreamSessionRef`、`IStreamObserver`；若后续任务试图在 005 中补齐这些对象，会直接把 011、012 或 036 的边界拉平，属于越权扩张。
2. `AdapterCallResult` 当前位于 `llm/src/adapters/`，因此任何真正依赖其完整定义的实现或测试都必须显式包含内部头文件；若后续跨模块消费者持续增多，应先走 037 的 shared admission 评审，而不是直接把它搬进 shared contracts。
3. 若后续 006~011 发现 `HealthStatus` 或 `StreamSessionRef` 需要收敛到统一模块公共头，可以在保持 `ILLMAdapter` 方法签名不变的前提下补充定义；本轮不提前做这一步，避免把“接口冻结”变成“全量 supporting types 落盘”。
