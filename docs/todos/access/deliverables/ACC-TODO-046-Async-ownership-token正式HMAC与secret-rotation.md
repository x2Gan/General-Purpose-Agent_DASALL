# ACC-TODO-046 设计收敛文档

## 1. 任务定义

将 Access 异步 receipt 的 ownership token 从 `std::hash + 默认静态 secret` 升级为正式 `HMAC-SHA256 + base64url`，并补齐 `current/previous key` 验证窗口、deployment secret materialization，以及 secret missing 时的 fail-closed 语义。

## 2. 本地证据

1. 专项 TODO 将 `ACC-TODO-046` 定义为 P1-2 / R3 安全治理任务，要求 token 包含 `key_id/issued_at/expires_at/receipt_id/actor_ref/request_id`，采用 `HMAC-SHA256 + base64url`、constant-time compare，并在 secret 缺失时禁用 async 或 fail-closed。
2. `docs/architecture/DASALL_access子系统详细设计.md` 的 receipt ownership 章节已经冻结双因子规则：`actor_ref` 必须匹配原始请求主体，`ownership_token` 必须由 `HMAC-SHA256(server_secret, receipt_id || actor_ref || request_id)` 派生，并通过 constant-time comparison 校验。
3. 改造前 `access/src/AsyncTaskRegistry.cpp` 仍使用 `std::hash` 和默认静态 secret fallback；`ownership_token_hmac_secret_ref` 虽然已在 `AccessBootstrapConfig` 落盘，但还没有被 Access 主链消费。
4. `access/include/AccessGatewayFactory.h` 与 `access/src/AccessGatewayFactory.cpp` 现已新增 `ownership_secret_manager` 注入点，并在 daemon/gateway `AcceptedAsync` 分支通过统一的 registry receipt attach 路径处理 secret materialization、receipt 生成与 missing-secret fail-closed。
5. `tests/unit/access/AsyncTaskRegistryOwnershipTest.cpp`、`tests/unit/access/AsyncTaskRegistryMissingSecretFailClosedTest.cpp` 与 query-handler 相关单测已经能直接证明 HMAC token、rotation window、missing-secret reject 和既有 query/cancel 兼容。

## 3. 外部参考

1. RFC 2104 定义 HMAC 结构与基于共享密钥的消息认证方式；本任务直接采用其 `HMAC-SHA256` 语义替换不稳定的 `std::hash`：https://www.rfc-editor.org/rfc/rfc2104
2. RFC 4648 定义 URL-safe Base64 alphabet；本任务使用 base64url 编码 token claims 与签名，避免 receipt token 在 HTTP/header/query 传递时引入 `+`、`/`、`=` 兼容问题：https://www.rfc-editor.org/rfc/rfc4648
3. OWASP 将 fail secure / deny by default 作为安全基线；对本任务的对应要求是 ownership secret 缺失时不得继续放出 `AcceptedAsync` 半成品结果：https://cheatsheetseries.owasp.org/cheatsheets/Authorization_Cheat_Sheet.html

## 4. 边界与职责

### 4.1 边界

1. 本任务只收敛 Access receipt ownership token 与 secret consumer seam，不扩写 `ISecretManager` 公共 ABI，也不新增 bootstrap secret 写入口。
2. 本任务不承诺多实例 authoritative secret sync；046 只把 v1 `current/previous key` 验证窗口和 fail-closed 规则落成可执行实现。
3. 本任务不把 secret 明文提升为 shared contracts，也不把 ownership token 规则扩成新的业务授权体系。

### 4.2 职责

| 对象 | 职责 | 非职责 |
|---|---|---|
| `AsyncTaskRegistry` | 生成/验证 HMAC ownership token，维护 receipt TTL 与 current/previous key window | 不拥有 secret backend，不做 policy 决策 |
| `AccessGatewayFactory` | 在 daemon/gateway `AcceptedAsync` 分支前 materialize deployment secret，并在 secret 缺失时 fail-closed | 不负责 bootstrap secret 初始写入 |
| `ISecretManager` seam | 提供 `get_secret/materialize/release` 只读消费链 | 不被 Access 扩写成新的 create/set API |

## 5. 数据与接口说明

1. `access/src/AsyncTaskRegistry.h`
   - 新增 `OwnershipTokenKey{key_id, secret}`。
   - 新增 `enabled()` 与 `rotate_ownership_keys()`，允许在 registry 内保留 current/previous key window。
   - 旧的字符串构造仍保留，作为 tests/externally injected registry 的最小兼容入口，但不再注入默认静态 secret。
2. `access/src/AsyncTaskRegistry.cpp`
   - token 现在为 `v1.<kid>.<iat>.<exp>.<receipt>.<actor>.<request>.<sig>`，其中字符串 claims 与签名都走 base64url。
   - 签名由本地 `HMAC-SHA256` 例程生成，验证时按 token 中的 `key_id` 选择 current 或 previous key，并用 constant-time compare 比对。
3. `access/include/AccessGatewayFactory.h`
   - `DaemonAccessPipelineOptions` 与 `GatewayAccessPipelineOptions` 新增 `ownership_secret_manager`；gateway options 同步显式持有 `async_task_registry`，避免 daemon/gateway 两条 accepted-async 分支各自发明 secret 访问方式。
4. `access/src/AccessGatewayFactory.cpp`
   - 新增 deployment secret materialization helper、disabled registry fallback 与统一的 `attach_async_receipt()` helper。
   - 当 runtime 返回 `AcceptedAsync` 且 registry disabled 时，submit pipeline 返回 `InternalError/ownership_secret_unavailable`，而不是继续传播无 ownership receipt 的半成品结果。

## 6. 流程与时序

1. daemon/gateway submit pipeline 构建时，优先复用外部注入的 `async_task_registry`；否则尝试用 `ownership_token_hmac_secret_ref + ISecretManager` materialize 当前 secret。
2. 若 secret ref 缺失、manager 不可用或 materialize 失败，则 factory 生成 disabled registry；同步/非 async 请求仍可继续处理。
3. runtime 返回 `AcceptedAsync` 时，`attach_async_receipt()` 用 registry 生成 receipt token，并回填 `PublishEnvelope.receipt`、`receipt_ref` 与 observability anchor。
4. 若 registry disabled，则 `AcceptedAsync` 在 Access 主链内被 fail-closed 成 `Rejected/InternalError(ownership_secret_unavailable)`。
5. query/cancel 路径仍通过 `validate_ownership()` 进行 owner 校验；当 token 携带 previous key id 时，registry 允许在 current/previous 窗口内继续校验历史 receipt。
6. 一旦 previous key 被移除，历史 token 立即失效，但新 key 生成的 token 继续可用。

## 7. Design -> Build 映射

| 设计项 | Build 落点 | 完成判定 |
|---|---|---|
| 正式 HMAC ownership token | `access/src/AsyncTaskRegistry.cpp` | token 不再依赖 `std::hash`，claims + signature 都可稳定编码/验证 |
| current/previous key 验证窗口 | `access/src/AsyncTaskRegistry.h/.cpp`、`tests/unit/access/AsyncTaskRegistryOwnershipTest.cpp` | 旧 token 在 previous window 内继续有效，移除 previous 后失效 |
| deployment secret 消费 seam | `access/include/AccessGatewayFactory.h`、`access/src/AccessGatewayFactory.cpp` | daemon/gateway accepted-async 分支可通过 `ISecretManager` materialize ownership secret |
| missing secret fail-closed | `access/src/AccessGatewayFactory.cpp`、`tests/unit/access/AsyncTaskRegistryMissingSecretFailClosedTest.cpp` | secret 缺失时不再返回半成品 accepted async |
| query/cancel 兼容回归 | `tests/unit/access/AccessTaskQueryHandlerTest.cpp`、`tests/unit/access/DaemonTaskQueryHandlerTest.cpp` | owner query/cancel、expired 与 mismatch 行为保持通过 |

## 8. 文件范围

1. `access/include/AccessGatewayFactory.h`
2. `access/src/AsyncTaskRegistry.h`
3. `access/src/AsyncTaskRegistry.cpp`
4. `access/src/AccessGatewayFactory.cpp`
5. `tests/unit/access/AsyncTaskRegistryOwnershipTest.cpp`
6. `tests/unit/access/AsyncTaskRegistryMissingSecretFailClosedTest.cpp`
7. `tests/unit/access/CMakeLists.txt`
8. `docs/todos/access/DASALL_access子系统专项TODO.md`
9. `docs/todos/access/deliverables/ACC-TODO-046-Async-ownership-token正式HMAC与secret-rotation.md`
10. `docs/worklog/DASALL_开发执行记录.md`

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| B1 | 将 ownership token 改为 HMAC-SHA256 + base64url claims/signature | `AsyncTaskRegistryHmacOwnershipTest`、`AsyncTaskRegistryExpiryTest` | `ctest --test-dir build/vscode-linux-ninja -R "AsyncTaskRegistryHmacOwnershipTest|AsyncTaskRegistryExpiryTest" --output-on-failure` |
| B2 | 为 `AsyncTaskRegistry` 增加 current/previous key rotation window | `AsyncTaskRegistrySecretRotationTest` | `ctest --test-dir build/vscode-linux-ninja -R "AsyncTaskRegistrySecretRotationTest" --output-on-failure` |
| B3 | 让 daemon/gateway accepted-async 通过 `ISecretManager` 消费 deployment secret，并在缺 secret 时 fail-closed | `AsyncTaskRegistryMissingSecretFailClosedTest`、`AccessTaskQueryHandlerTest`、`DaemonTaskQueryHandlerTest` | `ctest --test-dir build/vscode-linux-ninja -R "AsyncTaskRegistryMissingSecretFailClosedTest|AccessTaskQueryHandlerTest|DaemonTaskQueryHandlerTest" --output-on-failure` |

## 10. 验收结果

1. `Build_CMakeTools()`
   - 结果：通过。
   - 说明：增量构建成功重编 `dasall_access`、`dasall_access_async_task_registry_ownership_unit_test`、`dasall_access_async_task_registry_missing_secret_fail_closed_unit_test` 及受影响的 daemon/gateway access 目标。
2. `RunCtest_CMakeTools(tests=["AsyncTaskRegistryTest","AsyncTaskRegistryOwnershipTest","AsyncTaskRegistrySecretRotationTest","AsyncTaskRegistryExpiryTest"])`
   - 结果：通过，4/4 passed。
3. `RunCtest_CMakeTools(tests=["AsyncTaskRegistryMissingSecretFailClosedTest","AccessTaskQueryHandlerTest","DaemonAcceptedAsyncReceiptTest","DaemonTaskQueryHandlerTest"])`
   - 结果：通过，4/4 passed。
4. 额外观察
   - factory 现在在无 ownership secret 时保留 sync/query 基线，但不再把 `AcceptedAsync` 直接透传成无 ownership receipt 的不完整结果。
   - 本轮未把 multi-instance authoritative sync 写成已闭合结论；该结论继续留在 051 / release polish 风险面。

## 11. D Gate 结果

Gate = PASS。

1. ownership token 已从弱实现收敛为正式 `HMAC-SHA256 + base64url`，并显式携带 key/version 与时间窗信息。
2. current/previous key rotation window 已具备 focused 自动化断言，secret missing 时的 accepted-async 分支也已 fail-closed。
3. `ACC-BLK-006` 对 v1 focused receipt/query/cancel 路径已解除；但多实例 authoritative sync 仍只保留为风险，不外推为规模化 ready。
4. 本轮没有扩写 `ISecretManager` 公共 ABI，也没有引入新的 secret bootstrap 通道；Access 继续只做 consumer 读链。