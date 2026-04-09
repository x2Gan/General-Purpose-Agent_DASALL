# CAP-TODO-028 loopback / mock target 集成夹具设计收敛

日期：2026-04-09
任务：CAP-TODO-028
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 的 8.3、9.1 与 12.2 已经把 Phase 5 的直接 blocker 收敛为“至少有 1 组 loopback adapter 或 mock target 可用于集成回路”，并且明确 smoke integration 必须覆盖 `Tool -> IExecutionService / IDataService -> Adapter loopback -> result` 的最小闭环。
2. [services/src/adapters/LocalServiceAdapter.h](../../../../services/src/adapters/LocalServiceAdapter.h)、[services/src/adapters/RemoteServiceAdapter.h](../../../../services/src/adapters/RemoteServiceAdapter.h) 与 [services/src/adapters/AdapterBridge.h](../../../../services/src/adapters/AdapterBridge.h) 已经提供稳定的回调注入缝隙：tests 可以在不扩张 production ABI 的前提下，用 handler 驱动本地 loopback、remote timeout 和 scripted partial-side-effect 场景。
3. [tests/mocks/CMakeLists.txt](../../../../tests/mocks/CMakeLists.txt) 当前通过 `dasall_test_support` 暴露扁平 include 根，因此集成夹具最合适的落点是 `tests/mocks/include/` 下的 header-only 支撑，而不是在 services/ 内新增只服务测试的 adapter 变体。
4. [tests/integration/CMakeLists.txt](../../../../tests/integration/CMakeLists.txt) 与 [docs/ssot/InfraIntegrationTopology.md](../../../../docs/ssot/InfraIntegrationTopology.md) 已冻结 integration discoverability 约束：顶层聚合 target 必须显式依赖 services integration 可执行目标，且所有用例都要带 `integration` 或更细标签；因此 028 只负责把可复用夹具收敛到可注册状态，029 再做 discoverability 接线。
5. [tests/mocks/include/MockExecutionService.h](../../../../tests/mocks/include/MockExecutionService.h) 仍是旧的 `bool execute(const std::string&)` smoke 占位，不能承载 ServiceTypes 中的 request/result 语义；因此本轮应新增 Capability Services 专用夹具，而不是继续在旧 mock 上叠语义。

## 2. 外部参考

1. Martin Fowler 对 Test Double 的定义明确区分 fake/stub/mock，其中 fake 是“具备可运行实现但为测试采用捷径的对象”。这直接支持本轮采用 header-only loopback fixture：通过可运行的 handler 驱动 adapter/lane/facade 闭环，但不把该捷径实现带入 production。参考：https://martinfowler.com/bliki/TestDouble.html
2. Azure Architecture Center 的 Health Endpoint Monitoring 模式建议把功能性检查保持为最小、可重复、不会伤及主系统的 smoke 验证，而不是把完整业务复杂性都塞进 health/smoke 路径。这支持 028 把 fixture 设计为最小闭环，仅覆盖 execute/query/catalog 所需输入，并把 failure/profile 扩展延后给 030~032。参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/health-endpoint-monitoring

## 3. Design 结论

1. 新增 header-only `CapabilityServicesLoopbackFixture`，落点固定在 [tests/mocks/include/CapabilityServicesLoopbackFixture.h](../../../../tests/mocks/include/CapabilityServicesLoopbackFixture.h)。它只存在于 tests/，不进入 services 公共 include，也不新增 production-only loopback adapter。
2. fixture 直接复用 production `LocalServiceAdapter`、`RemoteServiceAdapter`、`AdapterBridge`、`ExecutionCommandLane`、`DataProjectionCache`、`DataQueryLane` 与 `ServiceFacade`，通过 handler 注入形成真实 services 主链闭环，而不是重新复制一套 test-only 语义实现。
3. smoke 默认路径固定为 `local_service`：`CapabilityServicesLoopbackFixture -> ServiceFacade -> ExecutionCommandLane / DataQueryLane -> AdapterBridge -> LocalServiceAdapter -> handler`。这样 029/030 可以先验证最短闭环，不碰高风险 action 或 runtime 治理语义。
4. failure/profile 变体只允许在 tests 内切换三类输入：`RemoteServiceAdapter` handler、remote timeout、candidate availability。也就是说，031/032 的扩展应继续基于同一 fixture，不允许再引入新的 production adapter 分支或 `services.*` 顶层 schema。
5. fixture 必须自带最小 request 生成 helper，统一补齐 `request_id / session_id / trace_id / tool_call_id / goal_id / deadline_ms`，避免 integration test 每次手写上下文时偏离 `ServiceContextBuilder` 的固定校验边界。
6. 因为 `dasall_test_support` 已经暴露 tests/mocks include 根，本轮不需要修改 tests/mocks 的 CMake 接线；029 只需要让 integration tests 消费新 header 并完成顶层 discoverability 注册。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 收敛 header-only loopback fixture 与默认 local/remote 变体边界 | tests/mocks/include/CapabilityServicesLoopbackFixture.h |
| 回写 Phase 5 integration fixture 方案与 smoke 路径 | docs/architecture/DASALL_capability_services子系统详细设计.md |
| 回写专项 TODO 中 028 状态、CAP-BLK-004 解阻与 029 可执行性 | docs/todos/services/DASALL_capability_services子系统专项TODO.md |

## 5. Build 三件套

1. 代码目标：新增 `CapabilityServicesLoopbackFixture`，让 tests 可以直接复用现有 services adapter/lane/facade 形成最小 execute/query/catalog 闭环，同时记录 local/remote invocations 供后续 smoke/failure/profile 用例断言。
2. 测试目标：先用 syntax-only 编译验证 fixture 头文件可独立构造 `ServiceFacade`、`LocalServiceAdapter`、`DataQueryLane` 组合；029 再把它接入真正的 integration discoverability 与 smoke 用例。
3. 验收命令：
   - `rg -n "loopback|integration fixture|mock target|LocalPlatformAdapter|LocalServiceAdapter|RemoteServiceAdapter" docs/architecture/DASALL_capability_services子系统详细设计.md tests/mocks/include services/CMakeLists.txt`
   - `cat <<'EOF' | c++ -std=c++20 -Iservices/src -Iservices/include -Itests/mocks/include -Iinfra/include -Iprofiles/include -Icontracts/include -x c++ - -fsyntax-only
#include "CapabilityServicesLoopbackFixture.h"

int main() {
  dasall::tests::mocks::CapabilityServicesLoopbackFixture fixture;
  auto execute_request = fixture.make_execute_request();
  auto query_request = fixture.make_query_request();
  (void)fixture.execution_service().execute(execute_request);
  (void)fixture.data_service().query(query_request);
  return 0;
}
EOF`

## 6. 风险与回退

1. 该 fixture 当前是 header-only 测试支撑，尚未进入顶层 integration discoverability；如果 029 的 CMake 注册失败，本轮仍然只关闭“夹具缺失”这个 blocker，不应冒充 smoke integration 已完成。
2. fixture 复用的仍是 production `ExecutionCommandLane` / `DataQueryLane`，因此默认 action/query 选择必须保持低风险、只读、无 shared-contract admission 语义；高风险动作和 remote partial-side-effect 继续留给 031 的 failure injection。
3. 若后续发现 tests 需要更多 profile/runtime 输入，应优先扩 `CapabilityServicesLoopbackFixtureOptions`，而不是回头在 services/src/adapters 新增 test-only 入口。