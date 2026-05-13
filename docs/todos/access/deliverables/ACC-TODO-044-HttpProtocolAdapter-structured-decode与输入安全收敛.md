# ACC-TODO-044 设计收敛文档

## 1. 任务定义

修复 gateway HTTP unary 入口的首版 `HttpProtocolAdapter` decode/encode 与 submit route 输入安全，使 HTTP 请求在进入 Access 主链前完成结构化 JSON 解码、协议级 fail-closed 校验，以及 `Idempotency-Key` 白名单头向 `request_context` 的受控投影。

## 2. 本地证据

1. 专项 TODO 将 ACC-TODO-044 定义为 P0-5 / R4 主链整改项，明确要求 HTTP decode 固定 `entry_type="gateway"`、`protocol_kind="http_unary"`，并对 method、path、content-type、body-size、idempotency-key 做 fail-closed 校验。
2. `apps/gateway/src/HttpProtocolAdapter.cpp` 的旧实现依赖 ad hoc 字符串扫描 JSON，既不处理转义，也不能识别嵌套值类型；同时把 `entry_type` 直接信任为 body 输入，并把 `protocol_kind` 固定成 `http`。
3. `apps/gateway/src/main.cpp` 的旧 `/v1/submit` route 只检查 `packet.entry_type.empty()`，未对 content-type、header 注入、body 超限和幂等键格式做入口裁剪。
4. `access/src/AccessGatewayFactory.cpp` 旧实现把 `idempotency_key` 一律回退为 `packet.packet_id`，使 HTTP `Idempotency-Key` 无法穿过 validator/normalizer 主链。

## 3. 外部参考

1. RFC 8259 规定 JSON object/string 的结构和转义规则，协议适配层不能用无结构字符串扫描替代合法 JSON 解码：https://www.rfc-editor.org/rfc/rfc8259
2. IETF `Idempotency-Key` 草案要求 HTTP 幂等键由 header 提供，值应是受约束的 token 字符集而不是任意自由文本：https://datatracker.ietf.org/doc/html/draft-ietf-httpapi-idempotency-key-header
3. HTTP 415 / 413 / 405 的常见语义分别对应不支持的 media type、payload 超限和 method 不允许；本轮 route helper 按这些语义返回协议级错误响应：https://www.rfc-editor.org/rfc/rfc9110

## 4. 边界与职责

### 4.1 边界

1. 本任务只收敛 gateway HTTP unary submit 入口，不扩展 health route、receipt query/cancel 路径，也不触碰 daemon IPC frame codec。
2. 本任务不重新设计 Access 认证来源；`peer_ref` 仍沿用当前 gateway 主链的 body 输入语义，后续认证/信任来源硬化留给后续任务处理。
3. 本任务不改变 Runtime owner、Recovery owner 和 Context owner 边界；header 只以 module-local sidecar 形式进入 Access，不进入 shared contracts。

### 4.2 职责

| 对象 | 职责 | 非职责 |
|---|---|---|
| `HttpProtocolAdapter::decode()` | 校验 HTTP submit 请求并产出 `InboundPacket` | 不直接做 auth/policy/admission 裁定 |
| `handle_submit_request()` | 封装 gateway `/v1/submit` route 的 decode -> submit -> encode 流程 | 不管理 health route 或全局 server 生命周期 |
| `InboundPacket.headers` | 承载 allowlisted HTTP sidecar facts | 不变成 contracts 公共字段 |
| `AccessGatewayFactory` header projection | 将 allowlisted sidecar 投影到 `RuntimeDispatchRequest.request_context` | 不接受任意 header 透传 |

## 5. 数据与接口说明

1. `access/include/AccessTypes.h`
   - `InboundPacket` 新增 `headers` sidecar，用于保存协议层已裁剪过的 allowlisted header facts。
2. `apps/gateway/src/HttpProtocolAdapter.h`
   - 新增 `HttpDecodeErrorCode` / `HttpDecodeError`。
   - `HttpProtocolAdapter` 新增 `set_max_request_body_bytes()` 与 `last_decode_error()`。
   - 新增 `handle_submit_request()` 作为可测试的 submit route helper。
3. `apps/gateway/src/HttpProtocolAdapter.cpp`
   - 使用结构化 JSON parser wrapper 解析 top-level object，并对字符串/布尔字段做类型断言。
   - decode 固定写入 `entry_type="gateway"`、`protocol_kind="http_unary"`。
   - `Idempotency-Key` 经格式校验后写入 `packet.headers["idempotency_key"]`。
   - encode 改为 JSON escape-safe 输出，不再手工直拼未转义 payload。
4. `access/src/AccessGatewayFactory.cpp`
   - gateway/daemon submit pipeline 在写入 `request_id/trace_id/session_id` 后，再把 `packet.headers` 投影到 `request_context`；若无 allowlisted `idempotency_key` 才回退到 `packet.packet_id`。

## 6. 流程与时序

1. `apps/gateway/src/main.cpp` 从 `httplib::Request` 组装 `HttpRequestContext`。
2. `handle_submit_request()` 创建 `HttpProtocolAdapter`，注入 body-size 上限，并调用 `decode()`。
3. decode 先校验 method/path/content-type/header 安全/幂等键格式，再执行结构化 JSON 解析。
4. 合法请求被映射为 `InboundPacket{entry_type="gateway", protocol_kind="http_unary", headers["idempotency_key"]?}`。
5. `IAccessGateway::submit()` 进入 Access 主链；`AccessGatewayFactory` 将 allowlisted headers 投影到 `RuntimeDispatchRequest.request_context`，由 validator/admission/normalizer 消费。
6. 返回结果通过 `encode()` 或 route helper fallback 统一映射为 JSON HTTP 响应。

## 7. Design -> Build 映射

| 设计项 | Build 落点 | 完成判定 |
|---|---|---|
| HTTP unary 固定协议事实 | `apps/gateway/src/HttpProtocolAdapter.cpp` | decode 输出固定 `gateway/http_unary` |
| 结构化 JSON 解码 | `apps/gateway/src/HttpProtocolAdapter.cpp` | 转义字符串可解析，嵌套对象 fail-closed |
| 协议级输入安全 | `apps/gateway/src/HttpProtocolAdapter.cpp`、`apps/gateway/src/main.cpp` | method/path/content-type/body-size/header/idempotency-key 均在入口 fail-closed |
| header allowlist 投影 | `access/include/AccessTypes.h`、`access/src/AccessGatewayFactory.cpp` | `Idempotency-Key` 可进入 `request_context` 和 `AgentRequest.idempotency_key` |
| route 合约可测试化 | `apps/gateway/src/HttpProtocolAdapter.h/.cpp` | `GatewaySubmitRouteContractTest` 可直接验证 submit route helper |

## 8. 文件范围

1. `access/include/AccessTypes.h`
2. `access/src/AccessGatewayFactory.cpp`
3. `access/src/SubjectResolver.cpp`
4. `apps/gateway/src/HttpProtocolAdapter.h`
5. `apps/gateway/src/HttpProtocolAdapter.cpp`
6. `apps/gateway/src/main.cpp`
7. `tests/unit/access/HttpProtocolAdapterTest.cpp`
8. `tests/unit/access/HttpProtocolAdapterErrorMappingTest.cpp`
9. `tests/unit/access/HttpProtocolAdapterStructuredDecodeTest.cpp`
10. `tests/unit/access/HttpProtocolAdapterSecurityInputTest.cpp`
11. `tests/unit/access/GatewaySubmitRouteContractTest.cpp`
12. `tests/unit/access/CMakeLists.txt`
13. `tests/integration/access/GatewayAccessSubmitCompositionTest.cpp`
14. `tests/integration/access/AccessObservabilityMainChainIntegrationTest.cpp`
15. `tests/integration/access/AccessPublishFailureAuditTest.cpp`
16. `tests/integration/access/AccessHealthReadinessIntegrationTest.cpp`
17. `tests/integration/access/GatewayBinaryMissingBackendFixture.cpp`
18. `docs/todos/access/DASALL_access子系统专项TODO.md`
19. `docs/todos/access/deliverables/ACC-TODO-044-HttpProtocolAdapter-structured-decode与输入安全收敛.md`
20. `docs/worklog/DASALL_开发执行记录.md`

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| B1 | 为 `InboundPacket` 增加 allowlisted `headers` sidecar | `GatewaySubmitRouteContractTest` | `ctest --test-dir build/vscode-linux-ninja -R "GatewaySubmitRouteContractTest" --output-on-failure` |
| B2 | 将 HTTP decode 改为结构化 parser wrapper | `HttpProtocolAdapterStructuredDecodeTest` | `ctest --test-dir build/vscode-linux-ninja -R "HttpProtocolAdapterStructuredDecodeTest" --output-on-failure` |
| B3 | 增加 method/path/content-type/body-size/header/idempotency fail-closed | `HttpProtocolAdapterSecurityInputTest` | `ctest --test-dir build/vscode-linux-ninja -R "HttpProtocolAdapterSecurityInputTest" --output-on-failure` |
| B4 | 更新 route helper、旧 adapter 单测和 gateway 协议口径 | `HttpProtocolAdapterTest`、`HttpProtocolAdapterErrorMappingTest`、gateway 相关 integration tests | `ctest --test-dir build/vscode-linux-ninja -R "HttpProtocolAdapterTest|HttpProtocolAdapterErrorMappingTest|GatewayAccessSubmitCompositionTest|AccessObservabilityMainChainIntegrationTest|AccessPublishFailureAuditTest|AccessHealthReadinessIntegrationTest" --output-on-failure` |

## 10. 验收结果

1. `Build_CMakeTools()`
   - 结果：通过。
   - 说明：增量构建成功编译 `dasall_gateway`、`dasall_access`、新旧 HTTP adapter 单测目标和 gateway 相关集成目标。
2. `RunCtest_CMakeTools(tests=["HttpProtocolAdapterTest","HttpProtocolAdapterErrorMappingTest","HttpProtocolAdapterStructuredDecodeTest","HttpProtocolAdapterSecurityInputTest","GatewaySubmitRouteContractTest"])`
   - 结果：通过，5/5 passed。
3. `RunCtest_CMakeTools(tests=["GatewayAccessSubmitCompositionTest","AccessObservabilityMainChainIntegrationTest","AccessPublishFailureAuditTest","AccessHealthReadinessIntegrationTest"])`
   - 结果：通过，4/4 passed。
4. 手工 E2E（临时 `DASALL_STATE_ROOT` + `build/vscode-linux-ninja/apps/gateway/dasall_gateway`）
   - `/health/ready`：`HTTP 200`。
   - `/v1/submit` with `Idempotency-Key: idem-manual-044-ok` and `peer_ref="jwt:user://tenant-a/alice"`：`HTTP 200`，响应包含 `result_id/status/payload` JSON 字段。
5. 补充事实
   - `GatewayBinaryUnarySmokeTest` 在临时 `DASALL_STATE_ROOT` 下已越过 `/var/lib/dasall/memory` 权限阻塞，但当前仍卡在 readiness label 期望 `degraded-ready` 与实际 `default-ready` 的既有差异；该断言不属于 044 submit/input-security 完成判定。

## 11. D Gate 结果

Gate = PASS。

1. HTTP unary submit 入口已经固定为 `gateway/http_unary`，不再把 body 中的 `entry_type` 当作协议事实。
2. `Idempotency-Key` 已能经 allowlisted sidecar 进入 `request_context`，并被 `RequestNormalizer` 投影到 `AgentRequest.idempotency_key`。
3. 结构化 decode、header 注入防护、content-type/body-size/非法幂等键 fail-closed 已由 focused tests 锁定。
4. 当前仍未处理 `peer_ref` 来源硬化和 readiness label 期望漂移，这两项不构成 044 的完成阻断，留待后续任务继续收口。