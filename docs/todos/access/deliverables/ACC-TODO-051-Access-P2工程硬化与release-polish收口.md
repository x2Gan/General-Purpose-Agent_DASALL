# ACC-TODO-051 设计收敛文档

## 1. 任务定义

将 Access 最后一组 P2 工程硬化项收口到稳定的内部执行面：删除无意义 placeholder translation unit，收敛 entry/protocol 强语义字符串，去掉 `RequestNormalizer` 的进程内计数器并接入统一稳定 ID generator，让 gateway submit route 通过 `ProtocolAdapterRegistry` 完成 decoder/encoder round-trip，同时为 shutdown timeout 留下 abandoned audit 证据，并把 app-private internal include 边界明确限制在 access / gateway 内部实现文件。

## 2. 本地证据

1. 专项 TODO 将 `ACC-TODO-051` 定义为 P2 / R5 工程硬化任务，要求 release candidate 前把 placeholder、字符串漂移、ID 稳定性、gateway route registry 化与 shutdown audit 口径收口。
2. `access/CMakeLists.txt` 已删除 `src/placeholder.cpp`，并显式纳入 `src/AccessIdGenerator.cpp`；`dasall_access` 不再依赖空实现文件维持目标结构。
3. `access/src/AccessSemanticKinds.h` 已新增 `AccessEntryKind`、`AccessProtocolKind` 及 parse / `to_string(...)` helper；`RequestNormalizer` 的 channel 映射与 `apps/gateway/src/HttpProtocolAdapter.cpp` 的 `can_handle()` / `decode()` 都改为使用统一 canonical 语义，而不是散落字符串比较。
4. `access/src/AccessIdGenerator.h/.cpp` 与 `access/src/RequestNormalizer.h/.cpp` 已把 request / session / trace ID 生成从 `RequestNormalizer` 本地计数器收口为统一内部 generator，当前格式稳定为 `prefix:entry_type:packet_id:ordinal`，跨 normalizer 实例不再漂移。
5. `apps/gateway/src/HttpProtocolAdapter.cpp` 已把 gateway submit route 改为 registry-backed decoder / encoder round-trip，并把 success fallback envelope 与 error fallback envelope 分离，避免 success / error 响应在 fallback 路径继续共享隐式分支逻辑。
6. `tests/unit/access/AccessGatewayShutdownAuditTest.cpp`、`AccessStrongEnumContractTest.cpp`、`AccessIdGeneratorStabilityTest.cpp`、`ProtocolAdapterRegistryGatewayIntegrationTest.cpp` 已新增或补强 focused acceptance；`HttpProtocolAdapterTest.cpp`、`HttpProtocolAdapterStructuredDecodeTest.cpp`、`HttpProtocolAdapterSecurityInputTest.cpp`、`GatewaySubmitRouteContractTest.cpp`、`HttpProtocolAdapterErrorMappingTest.cpp` 则作为相邻回归面验证 registry route 与 fallback mapping 没有被打坏。
7. `tests/unit/access/CMakeLists.txt` 已补齐 `access/src` include path，确保 `HttpProtocolAdapter.cpp` 仅在 app-private internal 边界内使用 `AccessSemanticKinds.h` 与 `ProtocolAdapterRegistry.h`，而不是把内部头暴露到 public include。

## 3. 外部参考

1. OWASP Logging Cheat Sheet 要求安全审计事件保留可关联上下文，且日志失败不能改变主业务结论；本任务把该原则具体落为 shutdown abandoned audit 与主链结果解耦：https://cheatsheetseries.owasp.org/cheatsheets/Logging_Cheat_Sheet.html
2. HTTP Semantics RFC 9110 将成功响应语义与错误响应语义显式区分；本任务将 `HttpProtocolAdapter` 的 success / error fallback envelope 分离，避免 fallback 映射继续混合状态语义：https://www.rfc-editor.org/rfc/rfc9110
3. W3C Trace Context 强调跨边界关联需要稳定标识；本任务虽然没有引入新的全局 trace owner，但通过统一稳定 ID generator 避免 `RequestNormalizer` 在同一输入上生成漂移的 request / session / trace anchors：https://www.w3.org/TR/trace-context/

## 4. 边界与职责

### 4.1 边界

1. 本任务只收敛 Access / gateway 内部工程硬化，不扩写 public include、contracts、installed-package、qemu 或 release runner 级别 ABI。
2. 本任务不改变 DASALL 的 owner 边界：全局 orchestrator / recovery / context owner 仍分别留在 runtime / recovery / memory，Access 只负责 ingress normalization、route decoding 与 focused audit seam。
3. 本任务不把 build-tree focused evidence 外推成 installed-package 或 release-ready 结论；`ACC-BLK-010` 的剩余口径已从“当前执行 blocker”收紧为“不得外推的结论边界”。
4. `ProtocolAdapterRegistry` 与 `ResultReplayCache` 仍是 access 内部值语义对象；本轮不为其新增公共 shutdown ABI，只要求 gateway draining timeout 能留下 abandoned audit 证据。

### 4.2 职责

| 对象 | 职责 | 非职责 |
|---|---|---|
| `AccessIdGenerator` | 为 Access 生成稳定、可复现的内部 request/session/trace ID | 不成为全局 trace/id owner，不暴露 public ABI |
| `AccessSemanticKinds` | 收敛 entry/protocol canonical parse 与 `to_string(...)` | 不扩写新的 contracts schema |
| `HttpProtocolAdapter` | 通过 registry 执行 gateway route decode/encode，并维护 success/error fallback 语义分离 | 不把内部 registry 头导出到 public include |
| `AccessGateway` | 在 shutdown drain timeout 上发出 abandoned audit | 不负责 installed-package 或多实例 authoritative sync 证明 |

## 5. 数据与接口说明

1. `access/src/AccessIdGenerator.h/.cpp`
   - 新增 `AccessIdGenerator::generate(...)`，输入为 `prefix + RuntimeDispatchRequest + ordinal`。
   - 输出格式统一为 `prefix:entry_type:packet_id:ordinal`，去掉进程内自增计数器对稳定性的影响。
2. `access/src/RequestNormalizer.h/.cpp`
   - 删除旧的 `id_counter_` 生成路径。
   - `generate_stable_id(...)` 现在统一委托给 `AccessIdGenerator`。
   - `map_request_channel(...)` 使用 `AccessSemanticKinds` 归一化 channel / entry / protocol 语义。
3. `access/src/AccessSemanticKinds.h`
   - 新增 `AccessEntryKind`、`AccessProtocolKind`。
   - 新增 `parse_access_entry_kind(...)`、`parse_access_protocol_kind(...)` 与 `to_string(...)`，用于 gateway route 与 request normalization 共享 canonical 字符串。
4. `apps/gateway/src/HttpProtocolAdapter.cpp`
   - `can_handle()` 改为基于 enum parse 做语义匹配。
   - `decode()` 使用 canonical `gateway/http_unary` 语义。
   - `handle_submit_request(...)` 通过 `ProtocolAdapterRegistry` 执行 decoder / encoder round-trip。
   - `make_success_fallback_envelope(...)` 与 `make_error_fallback_envelope(...)` 分离 success/error fallback 语义。
5. `tests/unit/access/CMakeLists.txt`
   - 为单独编译 `HttpProtocolAdapter.cpp` 的 unit targets 增加 `${CMAKE_SOURCE_DIR}/access/src` include path，保证 internal headers 只在 app-private 编译边界可见。

## 6. 流程与时序

1. 请求进入 Access 时，`RequestNormalizer` 先用 `AccessSemanticKinds` 归一化 entry/protocol/channel 语义。
2. 同一步骤中，`AccessIdGenerator` 基于稳定输入生成 request / session / trace anchors，不再依赖 normalizer 生命周期内的自增状态。
3. gateway submit route 进入 `HttpProtocolAdapter::handle_submit_request(...)` 后，先通过 registry 解析 decoder，再把 decoded 请求送入 access gateway。
4. access gateway 返回结果后，route 继续通过 registry 选择 encoder；若 route 不可编码或出现 protocol error，则 success / error fallback 分别走独立 envelope builder。
5. 若 gateway 在 shutdown drain timeout 时仍有 inflight 请求，则 `AccessGateway` 通过 shutdown observer 发出 abandoned audit 事件。

## 7. Design -> Build 映射

| 设计项 | Build 落点 | 完成判定 |
|---|---|---|
| 删除 placeholder 并显式化内部 generator | `access/CMakeLists.txt`、`access/src/AccessIdGenerator.*` | `dasall_access` 不再依赖空实现文件，ID 生成有统一内部入口 |
| 强语义 entry/protocol 收敛 | `access/src/AccessSemanticKinds.h`、`access/src/RequestNormalizer.cpp`、`apps/gateway/src/HttpProtocolAdapter.cpp` | 请求 normalization 与 gateway route 不再漂移为散落字符串常量 |
| 稳定 ID 生成 | `access/src/RequestNormalizer.*`、`tests/unit/access/AccessIdGeneratorStabilityTest.cpp` | 同一输入跨 normalizer 实例生成相同 ID |
| registry-backed gateway route | `apps/gateway/src/HttpProtocolAdapter.cpp`、`tests/unit/access/ProtocolAdapterRegistryGatewayIntegrationTest.cpp` | gateway submit route 通过 registry 执行 decode/encode round-trip |
| shutdown abandoned audit | `tests/unit/access/AccessGatewayShutdownAuditTest.cpp` | drain timeout 会留下 audit 证据，且不依赖事件顺序假设 |
| adapter / route 相邻回归 | `tests/unit/access/HttpProtocolAdapter*.cpp`、`GatewaySubmitRouteContractTest.cpp` | registry route、structured decode、安全输入与 error mapping 回归不破 |

## 8. 文件范围

1. `access/CMakeLists.txt`
2. `access/src/AccessIdGenerator.h`
3. `access/src/AccessIdGenerator.cpp`
4. `access/src/AccessSemanticKinds.h`
5. `access/src/RequestNormalizer.h`
6. `access/src/RequestNormalizer.cpp`
7. `apps/gateway/src/HttpProtocolAdapter.cpp`
8. `tests/unit/access/CMakeLists.txt`
9. `tests/unit/access/AccessGatewayShutdownAuditTest.cpp`
10. `tests/unit/access/AccessStrongEnumContractTest.cpp`
11. `tests/unit/access/AccessIdGeneratorStabilityTest.cpp`
12. `tests/unit/access/ProtocolAdapterRegistryGatewayIntegrationTest.cpp`
13. `docs/todos/access/DASALL_access子系统专项TODO.md`
14. `docs/todos/access/deliverables/ACC-TODO-051-Access-P2工程硬化与release-polish收口.md`
15. `docs/worklog/DASALL_开发执行记录.md`

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| B1 | 删除 placeholder、补齐 `AccessIdGenerator` 与 `AccessSemanticKinds` | `AccessStrongEnumContractTest`、`AccessIdGeneratorStabilityTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessStrongEnumContractTest|AccessIdGeneratorStabilityTest" --output-on-failure` |
| B2 | 让 gateway route 走 registry-backed decode/encode，并保持 success/error fallback 分离 | `ProtocolAdapterRegistryGatewayIntegrationTest`、`HttpProtocolAdapterTest`、`HttpProtocolAdapterStructuredDecodeTest`、`HttpProtocolAdapterSecurityInputTest`、`GatewaySubmitRouteContractTest`、`HttpProtocolAdapterErrorMappingTest` | `ctest --test-dir build/vscode-linux-ninja -R "ProtocolAdapterRegistryGatewayIntegrationTest|HttpProtocolAdapterTest|HttpProtocolAdapterStructuredDecodeTest|HttpProtocolAdapterSecurityInputTest|GatewaySubmitRouteContractTest|HttpProtocolAdapterErrorMappingTest" --output-on-failure` |
| B3 | 为 gateway shutdown timeout 保留 abandoned audit 证据 | `AccessGatewayShutdownAuditTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessGatewayShutdownAuditTest" --output-on-failure` |

## 10. 验收结果

1. `Build_CMakeTools(buildTargets=["dasall_access_gateway_shutdown_audit_unit_test","dasall_access_strong_enum_contract_unit_test","dasall_access_id_generator_stability_unit_test","dasall_access_protocol_adapter_registry_gateway_integration_unit_test"])`
   - 结果：通过。
   - 说明：`AccessIdGenerator` 引入后，`dasall_access` 与 4 个 focused acceptance targets 重新编译成功。
2. `RunCtest_CMakeTools(tests=["AccessGatewayShutdownAuditTest","AccessStrongEnumContractTest","AccessIdGeneratorStabilityTest","ProtocolAdapterRegistryGatewayIntegrationTest"])`
   - 结果：通过，4/4 passed。
3. `Build_CMakeTools(buildTargets=["dasall_access_http_protocol_adapter_unit_test","dasall_access_http_protocol_adapter_structured_decode_unit_test","dasall_access_http_protocol_adapter_security_input_unit_test","dasall_access_gateway_submit_route_contract_unit_test","dasall_access_http_protocol_adapter_error_mapping_unit_test"])`
   - 结果：通过。
   - 说明：相邻 Http adapter / route 回归面构建状态正常；最后一次增量构建输出 `ninja: no work to do.`。
4. `RunCtest_CMakeTools(tests=["HttpProtocolAdapterTest","HttpProtocolAdapterStructuredDecodeTest","HttpProtocolAdapterSecurityInputTest","GatewaySubmitRouteContractTest","HttpProtocolAdapterErrorMappingTest"])`
   - 结果：通过，5/5 passed。
5. 额外观察
   - `AccessGatewayShutdownAuditTest` 已改为只断言 shutdown abandoned event 存在，不再假设并发路径下事件总数或顺序稳定。
   - `ProtocolAdapterRegistryGatewayIntegrationTest` 证明 registry-backed gateway route 已进入真实 submit 路径，而不是停留在 adapter isolated unit。

## 11. D Gate 结果

Gate = PASS。

1. `ACC-TODO-051` 已完成：Access 的最后一组 P2 工程硬化项已具 focused build-tree 证据，placeholder、字符串漂移、稳定 ID、registry-backed gateway route 与 shutdown audit 都已收口。
2. 当前 Access 专项 TODO 的 034~051 已全部完成；后续若出现新评审项、回归或 packaging / qemu 级问题，应新开增量任务承接，而不是回滚当前 Done 口径。
3. 本轮不把结果外推为 installed-package / qemu / release-ready 证明；focused ingress / observability / ownership / route 证据仍只覆盖当前 build-tree 与 focused tests 边界。