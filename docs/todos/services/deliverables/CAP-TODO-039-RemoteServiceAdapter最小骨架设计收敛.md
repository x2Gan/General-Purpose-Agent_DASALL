# CAP-TODO-039 RemoteServiceAdapter 最小骨架设计收敛

日期：2026-04-09  
任务：CAP-TODO-039  
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.4 已冻结 `RemoteServiceAdapter` 的职责是“远程业务服务调用最小适配骨架”，其 route_kind 固定为 `remote_service`。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.8.1 和 9.3 已把远端超时、不可达与部分成功定义为后续 failure injection / ResultMapper 的关键输入，因此 039 至少要把 timeout 与 unreachable 两类事实稳定落盘。
3. CAP-TODO-036 已提供 `IAdapterInvoker` 与 `AdapterReceipt` 统一桥接面，因此 039 的最小实现应复用 invoker 协议，把远端 transport outcome、provider status、payload 与 latency 事实统一给 Bridge。
4. 本专项 TODO 允许 V1 采用 stub 形态，只要求超时和不可达语义不被误判为本地成功，因此 injected remote handler 的最小骨架足以满足当前阶段。

## 2. 外部参考

1. Azure Gateway Routing pattern 强调版本、实例和区域差异应被网关侧抽象，客户端不应直接感知后端变化；这支持本轮让 `RemoteServiceAdapter` 通过统一 invoker 接口接入 Bridge，而不是把远端协议细节上抛到 lane。
2. OWASP Authorization Cheat Sheet 的 fail safely 原则支持本轮把远端超时和不可达统一收敛为结构化失败事实，而不是在 services 层把远端失败伪装成本地成功。
3. 同一原则也支持本轮显式保留 timeout / unreachable 的 transport outcome，为 040 后续映射 `AdapterUnavailable` 留足证据。

## 3. Design 结论

1. `RemoteServiceAdapter` 通过 `IAdapterInvoker` 接口接入 AdapterBridge，route_kind 固定为 `remote_service`，保持 internal-only。
2. `remote_endpoint_available=false` 时，adapter 直接返回 `adapter_unavailable` + `unreachable`；`timeout_on_invoke=true` 时返回 `adapter_unavailable` + `timeout`。
3. 真正的远端调用通过 injected `invoke_remote` handler 完成，使 V1 stub / fake remote fixture 能复用同一协议面。
4. 本轮不实现部分成功或高级重试逻辑；只要 transport outcome、provider_status_code、payload 与 latency 事实被稳定保留即可。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 固定 `remote_service` route kind 与 invoker 协议 | services/src/adapters/RemoteServiceAdapter.h |
| 固定 timeout / unreachable / stub 的 fail-safe 行为 | services/src/adapters/RemoteServiceAdapter.cpp |
| 覆盖 identity、timeout、unreachable、loopback success 四类 unit 场景 | tests/unit/services/adapters/RemoteServiceAdapterTest.cpp |
| 将 RemoteServiceAdapter 纳入 services 与 unit 聚合构建 | services/CMakeLists.txt、tests/unit/services/adapters/CMakeLists.txt、tests/unit/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/adapters/RemoteServiceAdapter.h/.cpp`，实现 `RemoteServiceAdapterOptions` 与 `RemoteServiceAdapter` 最小骨架。
2. 测试目标：新增 `tests/unit/services/adapters/RemoteServiceAdapterTest.cpp`，覆盖 timeout、unreachable、loopback success 等正负例。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 6. 风险与回退

1. 当前 `RemoteServiceAdapter` 只定义统一骨架和 injected remote handler，不绑定真实网络/SDK；后续若接入具体远端协议栈，仍应维持 `IAdapterInvoker` 协议不变。
2. timeout 与 unreachable 语义目前只落在 `AdapterInvocationResult` / `AdapterReceipt` 事实面，040 需基于这些事实完成最终 `ServiceErrorClass` 映射，不能反向把映射逻辑塞回 039。
3. V1 允许 stub 形态，但 stub 不能把远端失败当成本地成功返回；任何后续增强都必须保持这一 fail-safe 底线。