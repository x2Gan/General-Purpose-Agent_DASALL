# ACC-TODO-002 AccessBootstrapConfig 与治理投影源收敛

日期：2026-04-23  
任务：ACC-TODO-002  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md) 将 ACC-TODO-002 定义为补齐 `AccessBootstrapConfig`、`AccessAuthView`、`AccessAdmissionView`、`AccessPublishView`、`AccessRuntimeGovernanceView` 的 source-of-truth、热更新边界与 immutable view 规则，用于解阻 ACC-BLK-002。
2. [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 的 6.11 已给出启动事实字段表和运行治理投影表，但尚未把“deployment bundle / ConfigCenter typed query / entry 启动参数”统一为单一来源语义，也没有冻结 snapshot fingerprint。
3. 同一详设的 AccessConfigAdapter 段落已经要求所有 view 都是 immutable snapshot，并建议提供 `refresh_runtime_governance()` 与 `is_snapshot_current()`，说明当前真正缺口是 version/fingerprint 和热更新适用面的唯一口径。
4. [docs/architecture/DASALL_profiles模块详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_profiles模块详细设计.md) 已冻结四层覆盖顺序、typed `deployment_override` / `runtime_override` 输入契约、不可变 `RuntimePolicySnapshot` 与 LastKnownGoodStore，Access 不应自造第二套热更新体系。

## 2. 外部参考

1. Azure External Configuration Store 模式强调，运行时配置应由外部 typed/structured configuration interface 提供，支持版本化、缓存、启动期 fallback 与多实例一致性；应用内应缓存一致快照，而不是允许每个实例用各自本地参数拼出并行配置面。这与 DASALL access 侧“typed bundle/query 作为唯一 schema carrier、invoke-scoped immutable view、启动期 last-known-good fallback”的要求一致。
   - 参考：<https://learn.microsoft.com/en-us/azure/architecture/patterns/external-configuration-store>

## 3. 设计结论

1. `AccessBootstrapConfig` 的唯一 schema 继续归 access 所有，但其合法 carrier 固定为两种同形态 typed source：
   - deployment bundle 中落盘的 typed bootstrap asset；
   - ConfigCenter typed query 返回的同 schema 快照。
2. entry 启动参数不再被视为第二配置源；它们只允许传入 `bootstrap_ref` / `config_snapshot_ref` / `entry_instance_ref` 这类定位信息，不能直接承载 `listen_ref`、`peer_auth_mode`、`dispatch_deadline_ms` 等业务字段。
3. `AccessBootstrapConfig` 只表达启动事实，不允许 runtime hot-update；运行期可变治理只来自已冻结的 `RuntimePolicySnapshot` 与白名单 `runtime_override`，并通过 `AccessConfigAdapter` 派生为模块内视图。
4. `AccessConfigAdapter` 的 snapshot fingerprint 固定为 `bootstrap_revision + effective_profile_id + runtime_policy_generation` 的组合；同一请求在入口绑定 fingerprint 后，整个请求生命周期内只消费这一版 immutable view，热更新只影响下一次请求。
5. 如果 ConfigCenter 在启动时不可用，允许回退到 deployment 随包分发的 last-known-good bootstrap asset；若二者都缺失或 schema 不合法，Access `init()` 必须 fail-closed。

## 4. 边界 / 职责

| 对象 | 边界与职责 | 不允许事项 |
|---|---|---|
| `AccessBootstrapConfig` | access 启动事实 schema；描述 listen/auth/replay/drain/payload/CORS/ownership-secret 等 entry-local 静态配置 | 不承载 `runtime_budget.*`、`timeout_policy.*`、`ops_policy.*` 的副本；不接受普通启动参数逐字段拼装 |
| `AccessAuthView` | 从 bootstrap static auth 字段和治理快照中派生认证相关轻量视图 | 不直接持有 secret 明文；不成为新 policy brain |
| `AccessAdmissionView` | 从 bootstrap quotas 与 runtime governance 投影并发/超时/默认拒绝等准入视图 | 不直接解析 profile YAML；不突破 profile 已冻结域 |
| `AccessPublishView` | 从 bootstrap replay/stream/drain/payload 限制和治理快照派生发布/重放视图 | 不决定 transport 线程模型；不引入第二套 streaming policy |
| `AccessRuntimeGovernanceView` | 只消费允许的 `RuntimePolicySnapshot` / infra 子域键，形成 access 内部治理快照 | 不反向定义 profiles 键语义；不暴露为 shared contract |
| `AccessConfigAdapter` | 唯一投影 owner：加载 bootstrap、构建 views、维护 fingerprint 与缓存失效 | 不直读任意 YAML/环境变量；不处理授权裁定 |

## 5. 数据 / 接口说明

### 5.1 `AccessBootstrapConfig`

1. canonical 载体是 typed bootstrap asset，建议随 deployment bundle 落盘，并允许由 ConfigCenter 以同 schema 查询返回。
2. schema 最小字段除了 6.11 既有表外，新增两个冻结元数据：
   - `bootstrap_revision`：必填，供缓存、审计与 LKG 回退引用；
   - `entry_type`：必填，明确该 bootstrap 只适用于 `cli` / `daemon` / `gateway` / `simulator` 中之一。
3. 启动参数只允许提供 `bootstrap_ref` 或本地 bootstrap asset 路径，不允许把字段级覆盖当成第二来源。

### 5.2 治理视图派生

1. `AccessAuthView`：消费 `peer_auth_mode`、`auth_provider_ref`、`trusted_local_subjects` 与 runtime/infra 中与认证相关的收紧开关。
2. `AccessAdmissionView`：消费 `idempotency_window_ms`、`max_inflight_requests`、`dispatch_deadline_ms`，并叠加 `runtime_budget.*`、`timeout_policy.*`、`infra.security_policy.default_effect` 的收紧投影。
3. `AccessPublishView`：消费 `result_replay_ttl_ms`、`stream_heartbeat_ms`、`slow_consumer_max_buffer`、`drain_timeout_ms`、`max_payload_bytes`、`cors_allowed_origins`，并叠加 `timeout_policy.*`、`ops_policy.*`、`infra.logging.*` 中与发布可观测相关的允许键。
4. `AccessRuntimeGovernanceView`：只包含 access 允许消费的现有键：`runtime_budget.*`、`timeout_policy.*`、`ops_policy.log_level`、`ops_policy.trace_sample_ratio`、`ops_policy.remote_diagnostics_enabled`、`infra.security_policy.default_effect`、`infra.logging.export.enable_diag_pull`。

### 5.3 `SnapshotVersionFingerprint`

1. fingerprint 最小组成：`bootstrap_revision`、`effective_profile_id`、`runtime_policy_generation`。
2. 当 bootstrap asset 更新或 profile snapshot generation 变化时，`is_snapshot_current()` 必须返回 false，触发下一次请求重建 views。
3. 进行中请求继续持有入口时刻绑定的 fingerprint，不因热更新 mid-request 漂移到半新半旧视图。

## 6. 流程 / 时序

1. 启动期：entry 壳层解析 `bootstrap_ref` -> 读取 deployment bundle 或 ConfigCenter typed snapshot -> 校验 schema 与 entry_type -> 构建 `AccessBootstrapConfig` -> 首次派生 `Access*View`。
2. 运行期：`AccessConfigAdapter` 从 active `RuntimePolicySnapshot` 提取白名单键并生成 `AccessRuntimeGovernanceView`；若 generation 未变，则复用已有 immutable view。
3. 热更新：profiles / infra 触发新的 snapshot generation 时，不回溯影响进行中请求；下一次请求进入时发现 fingerprint 失配，再重建 views。
4. 回退：若 live query 失败但 deployment 随包包含 last-known-good bootstrap asset，则按该版本启动；若 bootstrap 缺失、schema 不合法或 entry_type 不匹配，则 `init()` fail-closed。
5. override 边界：`runtime_override` 只允许修改治理投影白名单键，不允许修改 `listen_ref`、`allowed_protocols`、`peer_auth_mode`、`auth_provider_ref`、`ownership_token_hmac_secret_ref` 等 bootstrap 字段。

## 7. 文件范围

1. 设计真值源更新在 [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 的 6.11、6.20、AccessConfigAdapter 段与 12.1/12.2。
2. 上游约束回链到 [docs/architecture/DASALL_profiles模块详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_profiles模块详细设计.md) 的 overlay 与 RuntimePolicySnapshot 规则。
3. 本任务交付物落于 [docs/todos/access/deliverables/ACC-TODO-002-AccessBootstrapConfig与治理投影源收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-002-AccessBootstrapConfig与治理投影源收敛.md)。
4. TODO / blocker / 证据回写落于 [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md) 与 [docs/worklog/DASALL_开发执行记录.md](/home/gangan/DASALL/docs/worklog/DASALL_开发执行记录.md)。

## 8. Design -> Build 映射

| Design 项 | 后续 Build 落点 |
|---|---|
| `AccessBootstrapConfig` metadata 与字段面 | `access/include/AccessTypes.h` |
| `AccessAuthView` / `AccessAdmissionView` / `AccessPublishView` / `AccessRuntimeGovernanceView` | `access/include/AccessTypes.h`、`access/src/AccessConfigAdapter.cpp` |
| `SnapshotVersionFingerprint` 与 `is_snapshot_current()` 规则 | `access/src/AccessConfigAdapter.cpp`、`tests/unit/access/AccessConfigProjectionTest.cpp`、`tests/unit/access/AccessConfigAdapterHotUpdateTest.cpp` |

## 9. Build 三件套

1. 代码目标：无；本任务只完成 `AccessBootstrapConfig` source-of-truth 与治理投影规则冻结，不修改 access 生产代码。
2. 测试目标：通过文档检索确认 `AccessBootstrapConfig`、治理视图、白名单治理键、snapshot fingerprint 与 runtime_override 边界在 architecture / TODO / deliverable 中形成唯一口径。
3. 验收命令：
   - `rg -n "AccessBootstrapConfig|AccessAuthView|AccessAdmissionView|AccessPublishView|AccessRuntimeGovernanceView|SnapshotVersionFingerprint|runtime_budget|timeout_policy|ops_policy|infra.security_policy" docs/architecture/DASALL_access子系统详细设计.md docs/todos/access/DASALL_access子系统专项TODO.md docs/todos/access/deliverables/ACC-TODO-002-AccessBootstrapConfig与治理投影源收敛.md`

## 10. 风险与回退

1. 如果后续 apps 继续允许通过命令行逐字段覆盖 `AccessBootstrapConfig`，会重新引入第二来源；需要回退到“启动参数只传 locator”的规则。
2. 如果后续 `AccessConfigAdapter` 直接解析 profile YAML 或自行扩写 `runtime_policy.yaml` 顶层键，会破坏 profiles 作为唯一域语义 owner 的约束，应回退到 projection-only 组件定位。
3. 本任务只冻结 source-of-truth 和视图边界，不等价于 `AccessConfigAdapter` 代码已实现；ACC-TODO-012 仍需用 unit test 证明 profile diff、hot-update 与 deny-oriented fallback 行为。