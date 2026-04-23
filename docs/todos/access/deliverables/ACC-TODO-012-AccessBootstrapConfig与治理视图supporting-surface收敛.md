# ACC-TODO-012 设计收敛文档

## 1. 任务定义

定义 Access 配置 supporting surface：

1. `AccessBootstrapConfig`：启动事实配置对象。
2. `SnapshotVersionFingerprint`：治理快照版本锚点。
3. `AccessAuthView`、`AccessAdmissionView`、`AccessPublishView`、`AccessRuntimeGovernanceView`：运行治理投影视图。
4. `AccessConfigAdapter.cpp`：占位实现文件，为后续 ACC-TODO-012/后续 Build 阶段提供编译入口。

---

## 2. 边界与职责

### 2.1 边界规则

| 对象 | 职责 | 边界规则 |
|---|---|---|
| `AccessBootstrapConfig` | 表达启动事实配置 | 不承载 runtime 热更新字段，不进入 shared contracts |
| `SnapshotVersionFingerprint` | 标识配置快照版本 | 仅用于 Access 内部视图失效判定 |
| `AccessAuthView` | 认证相关治理视图 | 不持有 secret 明文，仅持引用与开关 |
| `AccessAdmissionView` | 准入相关治理视图 | 用于 admission 链，不直接决定 runtime 执行策略 |
| `AccessPublishView` | 发布与重放治理视图 | 用于 publish/replay 行为边界，不扩张为 transport 实现 |
| `AccessRuntimeGovernanceView` | runtime/profile/infra 投影快照 | 仅消费白名单键，不定义上游域语义 |

### 2.2 与上游解阻文档一致性

1. 与 ACC-TODO-002 一致：`AccessBootstrapConfig` 仅表达启动事实，运行治理通过快照投影。
2. 与 ACC-TODO-002 一致：`SnapshotVersionFingerprint = bootstrap_revision + effective_profile_id + runtime_policy_generation`。
3. 与 ACC-TODO-002 一致：热更新只影响下一次请求，不影响进行中请求的 immutable view。

---

## 3. 数据结构说明

### 3.1 AccessBootstrapConfig

字段覆盖 6.11 约束中的首版最小集合：

1. 元信息：`bootstrap_revision`、`entry_type`。
2. 启动事实：`listen_ref`、`allowed_protocols`、`peer_auth_mode`、`auth_provider_ref`。
3. 准入边界：`idempotency_window_ms`、`max_inflight_requests`、`dispatch_deadline_ms`。
4. 发布边界：`result_replay_ttl_ms`、`stream_heartbeat_ms`、`slow_consumer_max_buffer`、`drain_timeout_ms`。
5. 安全边界：`max_payload_bytes`、`max_user_input_bytes`、`cors_allowed_origins`、`session_id_mode`、`ownership_token_hmac_secret_ref`。

### 3.2 SnapshotVersionFingerprint

1. `bootstrap_revision`
2. `effective_profile_id`
3. `runtime_policy_generation`

用于比较当前 view 是否过期。

### 3.3 四类治理视图

1. `AccessAuthView`：`peer_auth_mode`、`auth_provider_ref`、`trusted_local_subjects`、`strict_auth_required`。
2. `AccessAdmissionView`：`idempotency_window_ms`、`max_inflight_requests`、`dispatch_deadline_ms`、`default_deny`。
3. `AccessPublishView`：`result_replay_ttl_ms`、`stream_heartbeat_ms`、`slow_consumer_max_buffer`、`drain_timeout_ms`、`max_payload_bytes`、`max_user_input_bytes`、`cors_allowed_origins`。
4. `AccessRuntimeGovernanceView`：`runtime_budget_profile`、`timeout_policy_profile`、`ops_log_level`、`ops_trace_sample_ratio`、`remote_diagnostics_enabled`、`security_default_effect`、`diag_pull_enabled`。

---

## 4. 流程与时序

### 4.1 启动期

1. 读取 `AccessBootstrapConfig`。
2. 构建 `SnapshotVersionFingerprint`。
3. 派生 `AccessAuthView/AccessAdmissionView/AccessPublishView/AccessRuntimeGovernanceView`。
4. 若 bootstrap 缺失或非法：`init()` fail-closed。

### 4.2 运行期热更新

1. 上游 profile/runtime generation 变化。
2. 比较 fingerprint。
3. 若变化：下一次请求重建视图。
4. 当前请求继续使用旧视图（immutable）。

---

## 5. 文件范围

1. `access/include/AccessTypes.h`
2. `access/src/AccessConfigAdapter.cpp`
3. `access/CMakeLists.txt`
4. `tests/unit/access/AccessConfigProjectionTest.cpp`
5. `tests/unit/access/CMakeLists.txt`
6. `tests/unit/access/AccessInterfaceSurfaceTest.cpp`
7. `docs/todos/access/DASALL_access子系统专项TODO.md`
8. 本文档

---

## 6. 验收三件套

### 6.1 代码目标

1. 在 `AccessTypes.h` 定义 `AccessBootstrapConfig` 与治理视图 supporting surface。
2. 新增 `AccessConfigAdapter.cpp` 占位源文件并接入 `dasall_access`。

### 6.2 测试目标

1. 新增 `AccessConfigProjectionTest` 校验关键字段可见性与默认值。
2. 在 `AccessInterfaceSurfaceTest` 增加配置面可发现性断言。

### 6.3 验收命令

```bash
cmake --build build-ci --target dasall_access dasall_access_config_projection_unit_test dasall_access_interface_surface_unit_test && \
ctest --test-dir build-ci -R "AccessConfigProjectionTest|AccessInterfaceSurfaceTest" --output-on-failure
```

说明：仓库全量 `dasall_unit_tests` 已受 knowledge 既有编译问题影响，本任务采用 Access 定向验证。

---

## 7. 风险与回退

1. 若后续希望扩展配置字段，必须以新增字段方式演进，不破坏现有字段语义。
2. 若后续需要更细粒度治理键，应通过 `AccessRuntimeGovernanceView` 追加字段，不直接写入 `AccessBootstrapConfig`。
3. 本任务只定义 supporting surface，不等同于 `AccessConfigAdapter` 业务逻辑已完成；后续实现应另起原子任务推进。
