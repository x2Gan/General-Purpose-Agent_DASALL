# CAP-TODO-038 LocalServiceAdapter 最小骨架设计收敛

日期：2026-04-09  
任务：CAP-TODO-038  
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.4 已冻结 `LocalServiceAdapter` 的职责是“本地业务服务调用适配”，并将其 route_kind 定位为 `local_service`。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.8.1 已把 `AdapterUnavailable` 定义为“后端不可达、连接池耗尽、超时”，这与 038 的“服务不可达时返回 AdapterUnavailable（retryable=true）”直接对齐。
3. CAP-TODO-036 已提供 `IAdapterInvoker` 和 `AdapterReceipt` 统一桥接面，因此 038 的最小实现应复用 invoker 协议，把“可达性失败”和“正路径 payload/latency/evidence facts”收束成统一 adapter invocation 结果。
4. 本专项 TODO 已把 038 定义为 D1 的直接任务，要求正路径能返回结构化 AdapterReceipt，负路径不能吞掉服务不可达事实。

## 2. 外部参考

1. Azure Gateway Routing pattern 强调后端实例变化和可用性切换应由路由层统一抽象，客户端不应感知具体实例；这支持本轮让 `LocalServiceAdapter` 通过统一 invoker 接口接入 Bridge，而不是把本地服务调用协议泄漏到 lane 层。
2. OWASP Authorization Cheat Sheet 的 fail safely 原则支持本轮把本地服务不可达和执行异常都固定为结构化、可诊断的失败结果，而不是静默重试或隐藏错误。
3. 同一原则也支持在成功路径保留 payload / latency / evidence facts，以便后续 ResultMapper 和 failure integration 有稳定输入。

## 3. Design 结论

1. `LocalServiceAdapter` 通过 `IAdapterInvoker` 接口接入 AdapterBridge，route_kind 固定为 `local_service`，保持 internal-only。
2. `service_endpoint_available=false` 时，adapter 直接返回 `adapter_unavailable` 语义，不调用底层 handler。
3. 真实本地服务调用通过注入的 `invoke_service` handler 完成，使 loopback/fake service fixture 能复用相同的接口面。
4. handler 抛异常时，adapter 仍返回结构化 `adapter_unavailable` 结果，并保留异常信息到 payload，保证负路径不吞错。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 固定 `local_service` route kind 与 invoker 协议 | services/src/adapters/LocalServiceAdapter.h |
| 固定 endpoint unavailable / exception 的 fail-safe 行为 | services/src/adapters/LocalServiceAdapter.cpp |
| 覆盖 identity、unavailable、loopback success、exception 四类 unit 场景 | tests/unit/services/adapters/LocalServiceAdapterTest.cpp |
| 将 LocalServiceAdapter 纳入 services 与 unit 聚合构建 | services/CMakeLists.txt、tests/unit/services/adapters/CMakeLists.txt、tests/unit/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/adapters/LocalServiceAdapter.h/.cpp`，实现 `LocalServiceAdapterOptions` 与 `LocalServiceAdapter` 最小骨架。
2. 测试目标：新增 `tests/unit/services/adapters/LocalServiceAdapterTest.cpp`，覆盖 endpoint unavailable、loopback success、exception 等正负例。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 6. 风险与回退

1. 当前 `LocalServiceAdapter` 只定义统一骨架和 injected handler，不绑定真实本地服务注册中心；后续若接入 service registry，仍应维持 `IAdapterInvoker` 协议不变。
2. 本轮把不可达和异常都收敛为 `adapter_unavailable` 语义，040 需基于 receipt facts 完成最终 `ServiceErrorClass` / `ErrorInfo` 映射，不能反向把映射逻辑塞回 038。
3. loopback/fake service fixture 未来应优先复用 `invoke_service` 注入点，避免与 bridge/adapter 协议分叉。