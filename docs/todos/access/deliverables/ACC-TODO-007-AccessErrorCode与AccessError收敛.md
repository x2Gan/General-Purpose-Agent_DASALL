# ACC-TODO-007 AccessErrorCode 与 AccessError 收敛

日期：2026-04-23  
任务：ACC-TODO-007  
状态：Task PASS

## 1. 本地证据

1. [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 的 6.7 已给出 `AccessErrorCode` 与 `AccessError` 的首版对象形状，但当前仓库中的 [access/include/AccessErrors.h](/home/gangan/DASALL/access/include/AccessErrors.h) 仍只是 ACC-TODO-006 留下的 skeleton header。
2. 同一份详设的 6.17.1 已冻结 error taxonomy 的分组和码段：Validation `100–199`、Authentication `200–299`、Authorization `300–399`、Admission `400–499`、RuntimeDispatch `500–599`、Publish `600–699`、Receipt `700–799`、Internal `900–999`。
3. 6.17.2 已显式列出一批 HTTP / CLI / gRPC 预留映射，但并未把 `UnsupportedProtocol`、`AdmissionRejected`、`PublishTimeout`、`PublishEncodingFailed`、`CancellationFailed`、`InternalError` 等所有代码逐项展开；如果本轮不收成唯一口径，后续 `ResultPublisher`、CLI publisher、HTTP adapter 会各自发散。
4. [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md) 已把 ACC-TODO-007 明确绑定到 `access/include/AccessErrors.h`、`access/src/ProtocolErrorMapper.cpp` 和 `AccessErrorCodeTest` / `AccessErrorMappingTest`，说明这轮既要冻结错误对象，也要补齐最小协议映射可执行面。

## 2. 设计结论

1. `AccessErrorCode` 本轮采用 `std::uint16_t` 作为底层类型，并严格保持详设 6.7 中的冻结值，不额外增删码点。
2. `AccessError` 的公共面只保留四个稳定字段：`code`、`reason`、`detail`、`retryable`，以及可选的 `upstream_error`；它是 access module-local 的统一错误载体，不反向污染 contracts。
3. 为了让 `AccessError` 可被稳定构造且后续组件不再手写分散规则，本轮补一个最小 `describe_access_error()` / `make_access_error()` 约定：默认 `reason` 取稳定 snake_case code name，`retryable` 由 error taxonomy 统一给出。
4. `ProtocolErrorMapper` 本轮冻结为 access core 内的最小协议映射函数，输出 HTTP status、CLI exit code、gRPC 预留状态和最小 header hint；具体 HTTP body JSON、stderr 格式和 challenge scheme 文本仍由后续 adapter 负责。
5. 对于 6.17.2 未逐项展开的代码，本轮采用“保持同组邻近语义”的保守推断：
   - `UnsupportedProtocol` 归 Validation，映射为 `400 / 1 / INVALID_ARGUMENT`
   - `AdmissionRejected` 归 Admission，映射为 `503 / 75 / UNAVAILABLE`
   - `PublishTimeout` 归 Publish，映射为 `504 / 75 / DEADLINE_EXCEEDED`
   - `PublishEncodingFailed` 归 Publish，映射为 `500 / 1 / INTERNAL`
   - `CancellationFailed` 归 Receipt，映射为 `409 / 75 / ABORTED`
   - `InternalError` 归 Internal，映射为 `500 / 1 / INTERNAL`
6. `AuthenticationChallengeRequired` 只冻结“必须带 `WWW-Authenticate` header hint”这一事实，不在本轮把 challenge scheme 文本写死到 adapter-level ABI；因此 mapper 输出最小稳定 hint，而不假装已经完成具体 auth scheme 设计。
7. `IdempotencyReplayHit` 虽然处于 error taxonomy 中，但协议映射必须保留其“命中已完成结果”的 success-like 语义：`200 / 0 / OK + X-Replay-Hit: true`。

## 3. 边界 / 职责

| 对象 | 边界与职责 | 本轮不做的事 |
|---|---|---|
| `AccessErrorCode` | 冻结 access 私有错误码域、分组边界与稳定命名 | 不扩展 lifecycle / admission / receipt supporting types |
| `AccessError` | 提供 access 侧统一错误对象，承接本地 reason/detail/retryable 与可选上游 `ErrorInfo` | 不把 `failure_type`、`source_ref` 等 contracts 字段直接抬成 access public ABI |
| `describe_access_error()` | 统一导出 domain 与 retryable 默认语义 | 不承担 HTTP/CLI 协议映射 |
| `ProtocolErrorMapper` | 导出最小协议映射和 header hint，供后续 publisher / adapter 复用 | 不负责渲染 HTTP JSON body、CLI stderr 文本或挑战协商细节 |
| `AccessErrorCodeTest` / `AccessErrorMappingTest` | 固定码值、命名、retryable 语义与协议映射表 | 不替代后续 `ResultPublisherTest` / `ProtocolErrorMapperTest` 的真实发布链断言 |

## 4. 数据 / 接口说明

1. `AccessErrorCode` 冻结值：
   - Validation：`ValidationRejected=100`、`PayloadTooLarge=101`、`UnsupportedProtocol=102`、`MalformedInput=103`
   - Authentication：`AuthenticationFailed=200`、`AuthenticationChallengeRequired=201`、`CredentialExpired=202`
   - Authorization：`AuthorizationDenied=300`、`ConfirmationRequired=301`、`OverrideSourceInvalid=302`
   - Admission：`AdmissionRejected=400`、`RateLimitExceeded=401`、`ConcurrencyLimitExceeded=402`、`IdempotencyConflict=403`、`IdempotencyReplayHit=404`、`QueueFull=405`
   - RuntimeDispatch：`RuntimeDispatchFailed=500`、`RuntimeDispatchTimeout=501`、`RuntimeBridgeUnavailable=502`
   - Publish：`PublishChannelUnavailable=600`、`PublishTimeout=601`、`PublishEncodingFailed=602`
   - Receipt：`ReceiptNotFound=700`、`ReceiptExpired=701`、`ReceiptOwnerMismatch=702`、`CancellationFailed=703`
   - Internal：`InternalError=900`、`ShuttingDown=901`
2. `AccessError` 结构：
   - `code`：模块私有码点
   - `reason`：稳定 machine-readable reason，默认取 code name
   - `detail`：人类可读细节或入口上下文
   - `retryable`：由 taxonomy 收敛出的默认重试语义
   - `upstream_error`：可选 shared `contracts::ErrorInfo`
3. `AccessProtocolErrorMapping` 最小输出：
   - `http_status`
   - `cli_exit_code`
   - `grpc_status`
   - `http_header_name`
   - `http_header_value`
   - `reason`

## 5. 流程 / 时序

1. access 组件识别失败场景后，先选定 `AccessErrorCode`。
2. 组件调用 `describe_access_error()` / `make_access_error()` 得到统一 `AccessError`，不再各自硬编码 retryable 或默认 reason。
3. 入口 adapter / publisher 需要对外响应时，调用 `map_access_error()` 取得协议无关映射事实。
4. HTTP/CLI/gRPC 具体适配层在此基础上各自渲染 body、stderr 或 status envelope，但不得背离本轮冻结的状态码 / exit code / header hint。

## 6. 文件范围

1. 代码：
   - [access/include/AccessErrors.h](/home/gangan/DASALL/access/include/AccessErrors.h)
   - [access/src/ProtocolErrorMapper.cpp](/home/gangan/DASALL/access/src/ProtocolErrorMapper.cpp)
   - [access/CMakeLists.txt](/home/gangan/DASALL/access/CMakeLists.txt)
2. 测试：
   - [tests/unit/access/CMakeLists.txt](/home/gangan/DASALL/tests/unit/access/CMakeLists.txt)
   - [tests/unit/access/AccessErrorCodeTest.cpp](/home/gangan/DASALL/tests/unit/access/AccessErrorCodeTest.cpp)
   - [tests/unit/access/AccessErrorMappingTest.cpp](/home/gangan/DASALL/tests/unit/access/AccessErrorMappingTest.cpp)
3. 追踪文档：
   - [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md)
   - [docs/todos/access/deliverables/ACC-TODO-007-AccessErrorCode与AccessError收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-007-AccessErrorCode与AccessError收敛.md)

## 7. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| error taxonomy 码段与命名 | `AccessErrors.h` |
| `AccessError` 公共对象 | `AccessErrors.h` |
| 协议映射最小真值源 | `ProtocolErrorMapper.cpp` |
| 码值 / retryable / 默认 reason gate | `AccessErrorCodeTest.cpp` |
| HTTP / CLI / gRPC 预留映射 gate | `AccessErrorMappingTest.cpp` |

## 8. Build 三件套

1. 代码目标：让 `AccessErrorCode`、`AccessError` 和最小 `ProtocolErrorMapper` 进入可编译、可链接、可测试状态。
2. 测试目标：新增 `AccessErrorCodeTest`、`AccessErrorMappingTest`，固定码值、命名、retryable 默认值与协议映射表。
3. 验收命令：
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci -R "AccessErrorCodeTest|AccessErrorMappingTest" --output-on-failure`

## 9. 后续任务接口

1. ACC-TODO-021 / 024 / 032 可以直接复用本轮冻结的 `map_access_error()`，不再自行发明 HTTP status / CLI exit code 对照表。
2. ACC-TODO-010 / 011 / 020 / 022 在需要返回拒绝或失败对象时，应直接复用 `AccessError` 和 `make_access_error()`。
3. 如果后续 challenge scheme、HTTP error body schema 或 CLI stderr layout 需要更细粒度约束，应在 adapter 层文档和测试中扩展，但不得推翻本轮冻结的状态码、exit code 与 header hint。 

## 10. 验证结果

1. `cmake --build build-ci --target dasall_access dasall_access_error_code_unit_test dasall_access_error_mapping_unit_test` 已通过，说明 `AccessErrors.h`、`ProtocolErrorMapper.cpp` 和两条新增单测本身可编译可链接。
2. `ctest --test-dir build-ci -R "AccessErrorCodeTest|AccessErrorMappingTest" --output-on-failure` 已通过，两条新增 Access gate 都能执行并通过。
3. `cmake --build build-ci --target dasall_unit_tests` 在本轮环境中未能全绿，但阻塞点来自仓库内既有的 [tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp](/home/gangan/DASALL/tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp) 编译错误：文件内存在 `}#include <algorithm>` 拼接问题和重复 `main()`，与本轮 access 改动无关。
4. 因此本轮按“任务自身代码与 gate 可执行”判定为 `Task PASS`；若后续需要把 `dasall_unit_tests` 恢复为全绿，应单独处理 knowledge 侧现存故障。 
