# infra/config 模块详细设计方案

文档版本：v1.1
日期：2026-03-30
状态：Draft

## 1. 模块概览

- 模块职责：提供配置读取、分层合并、类型化访问、运行期覆盖、变更分发、配置校验与回退能力。
- 上下游边界：
  - 上游消费者：apps、runtime、cognition、llm、tools、memory、knowledge、services、multi_agent。
  - 下游依赖：platform（文件/时间/线程抽象）、infra/logging、infra/tracing、infra/metrics、infra/secret。
- 设计目标：
  1. 在不污染 contracts 的前提下，建立 infra/config 的稳定接口与可插拔实现。
  2. 形成四层配置模型（默认 -> Profile -> 部署 -> 运行时覆盖）。
  3. 保证配置变更可观测、可回退、可灰度、可验证。

来源依据：
1. docs/architecture/DASSALL_Agent_architecture.md（Infrastructure 职责、配置系统、可观测与可恢复原则）
2. docs/architecture/DASALL_Engineering_Blueprint.md（3.12 infra/config、4.1/4.2 依赖方向与禁止规则、5.1 Profile）
3. docs/architecture/DASALL_infrastructure子系统详细设计.md（Infra 子域决策与接口模式）
4. docs/adr/ADR-005-architecture-review-baseline.md（contracts 与边界先冻结）
5. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
6. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
7. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
8. docs/plans/DASALL_contracts冻结实施计划.md
9. docs/todos/contracts/DASALL_contracts冻结TODO总表.md
10. docs/development/DASALL_工程协作与编码规范.md

## 2. 约束清单

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| CFG-C001 | DASSALL_Agent_architecture.md | Must | infra/config 属于 Layer 1，仅提供基础设施能力，不承载业务决策 | 子组件职责 |
| CFG-C002 | DASALL_Engineering_Blueprint.md 4.1/4.2 | Must | 依赖方向必须单向，infra/config 不得依赖任一业务模块实现 | 依赖关系 |
| CFG-C003 | DASALL_Engineering_Blueprint.md 3.12 | Must | config 采用四层配置模型：默认、Profile、部署、运行时覆盖 | 配置策略 |
| CFG-C004 | DASALL_Engineering_Blueprint.md 5.1/3.13 | Must | Profile 只能裁剪能力与替换实现，不得绕过 Runtime 主控、PolicyGate、Audit | 策略合规 |
| CFG-C005 | ADR-005 | Must | 不改写已冻结架构边界，contracts 先行冻结原则必须遵守 | 设计治理 |
| CFG-C006 | ADR-006 | Must-Not | infra/config 不接管上下文语义装配与 Prompt 渲染职责 | 边界职责 |
| CFG-C007 | ADR-007 | Must-Not | infra/config 不裁定失败语义与恢复策略，仅提供配置与观测支撑 | 异常边界 |
| CFG-C008 | ADR-008 | Must-Not | infra/config 不拥有主控调度权，仅服务 AgentOrchestrator 的配置消费 | 控制边界 |
| CFG-C009 | contracts冻结实施计划.md | Must-Not | 不将 infra/config 实现细节反向写入 contracts 共享对象 | contracts 边界 |
| CFG-C010 | contracts冻结TODO总表.md | Must | 兼容优先：新增优先 optional，禁止破坏性变更直推 | 演进策略 |
| CFG-C011 | 工程协作与编码规范.md 3.6 | Must | 错误不可吞没，配置失败必须产生日志/指标/审计信号 | 错误处理 |
| CFG-C012 | 工程协作与编码规范.md 3.7 | Should | 新增公共配置接口必须同步 unit 或 contract 测试 | 测试门禁 |
| CFG-C013 | 12-Factor Config | Should | 部署相关配置从代码分离，优先环境与外置配置 | 配置治理 |
| CFG-C014 | Azure External Configuration Store | Should | 支持外部配置存储、缓存、版本与启动回退（last-known-good） | 可用性与回退 |
| CFG-C015 | OTel Configuration Spec | Should | 以程序化配置接口为主，环境变量与声明式配置作为上层机制 | 接口建模 |

## 3. 现状与缺口

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| infra/config 模块可用实现 | 部分实现 | config 公共接口与类型已落盘，但 config 服务实现仍待接入 | High | P0 |
| 对外配置接口 | 部分实现 | infra/include/config 已落盘 IConfigCenter/IConfigLoader/IConfigValidator/IConfigPublisher 等接口 | High | P0 |
| 四层配置合并 | 缺失 | 当前无默认/Profile/部署/运行时覆盖合并链路 | High | P0 |
| 配置热更新与分发 | 缺失 | 无变更监听、无快照版本、无订阅回调 | Medium | P1 |
| 配置校验与错误语义 | 部分实现 | IConfigValidator 与 ConfigErrors 已冻结，schema 级执行链仍待补齐 | High | P0 |
| 可观测与审计 | 缺失 | 配置读写无 trace/metric/audit 事件 | High | P0 |
| 测试基线 | 部分实现 | unit/contract 基线已接入，integration 仍待补齐 | Medium | P0 |

现状证据：
1. infra/CMakeLists.txt 已接入 core/audit/plugin/tracing 等真实源码。
2. infra 当前不再依赖 placeholder-only 构建；config 缺口集中在实现目录与服务骨架。
3. infra/include/config 已落盘 IConfigCenter、ConfigTypes、ConfigErrors 与相关 validator/loader/publisher 头文件。

## 4. 候选方案对比

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| A 文件静态配置（启动一次加载） | 中 | 高 | 低 | 无热更新、跨实例一致性差、回退链薄弱 | 淘汰：仅适合 PoC |
| B 分层配置中心（内置存储 + 外置适配 + 缓存 + 版本快照） | 高 | 高 | 中 | 组件较多，需严格边界与测试门禁 | 采纳 |
| C 强一致配置服务优先（集中式配置集群） | 中 | 高 | 高 | 运行复杂度高，edge_minimal 成本不可控 | 暂不采纳，列入 v2 预研 |

方案要点：
1. 方案 A：实现快，但难满足蓝图中的 Profile、运行时覆盖与治理要求。
2. 方案 B：平衡成本与能力，满足当前阶段最小可交付与后续演进。
3. 方案 C：适合大规模云原生集群，不适合作为当前仓库第一版落地。

## 5. 决策结论

- 最终选型：方案 B，分层配置中心。
- 选型依据：
  1. 满足 Layer 1 边界，不依赖业务实现。
  2. 与 contracts 冻结策略一致，不引入反向语义污染。
  3. 可分阶段落地，兼容 desktop_full、edge_balanced、edge_minimal。
  4. 与行业实践一致：外置配置、缓存、版本、回退、配置变更可观测。
- 放弃方案理由：
  1. 放弃 A：缺少动态治理与恢复能力。
  2. 放弃 C：超出当前工程阶段复杂度预算。

## 6. 详细设计

### 6.1 职责边界

infra/config 负责：
1. 配置源加载与优先级合并。
2. 配置值类型化读取与校验。
3. 运行期覆盖与配置版本管理。
4. 配置变更事件发布与订阅。
5. 配置操作可观测与审计。

infra/config 不负责：
1. 业务策略解释与策略裁定。
2. 主状态机控制、恢复执行或多 Agent 调度。
3. contracts 对象定义变更。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| ConfigCenterFacade | 对外统一入口，管理生命周期与命令路由 |
| ConfigLoader | 读取各层配置源（defaults/profile/deploy/runtime） |
| ConfigMerger | 执行优先级合并、冲突处理与来源追踪 |
| ConfigValidator | 结构、类型、范围、互斥规则校验 |
| ConfigSnapshotStore | 存储当前快照、历史版本、last-known-good |
| ConfigWatchService | 文件/内存/远端变更监听与去抖发布 |
| ConfigPublisher | 向订阅方发布变更事件与差异内容 |
| ConfigAuditBridge | 记录配置读写、覆盖、回退、失败审计 |
| SecretRefResolver | 解析 secret 引用（不暴露明文） |

### 6.3 子组件输入输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| ConfigCenterFacade | app/runtime 初始化参数、管理命令 | 配置查询结果、变更结果 | ResultCode + ErrorInfo |
| ConfigLoader | 文件路径、环境变量、远端适配器 | 原始层配置文档 | 包含 source_id、version |
| ConfigMerger | 层配置集合 | 合并后配置树 + 覆盖链 | 后层覆盖前层 |
| ConfigValidator | 合并后配置树 + 规则集 | 校验报告 | 失败必须给出路径与原因 |
| ConfigSnapshotStore | 配置快照 | 当前快照、历史快照、LKG | 支持原子切换 |
| ConfigWatchService | 配置源变化信号 | 变更事件 | 去抖与节流 |
| ConfigPublisher | 已通过校验的新快照 | 订阅事件 | 按命名空间过滤 |
| ConfigAuditBridge | 读写行为与结果 | 审计日志 | 不可静默丢失 |
| SecretRefResolver | secret:// 引用键 | 句柄或脱敏值 | 明文不落盘 |

### 6.4 子组件依赖关系

1. ConfigCenterFacade 依赖 ConfigLoader、ConfigMerger、ConfigValidator、ConfigSnapshotStore、ConfigPublisher。
2. ConfigLoader 可选依赖外置配置适配器，不直接依赖业务模块。
3. ConfigValidator 仅依赖规则库与 contracts 错误语义，不依赖业务实现。
4. ConfigPublisher 与 ConfigAuditBridge 依赖 infra/logging、infra/metrics、infra/tracing。
5. SecretRefResolver 依赖 infra/secret 抽象接口。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | 与 contracts 对齐关系 |
|---|---|---|---|
| TypedConfig | key_path, value_type, serialized_value, schema_version, source_kind, source_id | key_path 必须非空；schema_version 仅允许受支持版本 | infra/config 私有 typed 对象，不进入 contracts |
| ConfigQuery | key_path, expected_type, default_policy, fallback_serialized_value | key_path 必须非空；fallback 仅在 fallback policy 下允许 | 失败映射 ResultCode/ErrorInfo |
| ConfigPatch | patch_id, source_kind, source_id, actor, target_scope, base_version, reason_code, expires_at, patches | runtime_override 必须带 expires_at/TTL；patches 只允许白名单路径与受支持 op | 操作事件映射 EventEnvelope |
| ConfigSnapshot | version, checksum, created_at, data, source_chain | version 单调递增；source_chain 最多四层且来源唯一 | 不写入 contracts 公共对象 |
| ConfigDiff | from_version, to_version, changes | changes 必须可审计且保持 key_path 粒度 | 用于事件与审计，不污染业务语义 |
| ValidationIssue | key_path, code, severity, message | issue 可定位可修复 | code 映射统一错误域 |
| ConfigApplyResult | applied, rollback_token, warnings, result_code, error_info | applied=false 必有 error_info | 兼容 contracts 错误语义 |

说明：以上对象均为 infra/config 私有对象或模块接口对象，不新增到 contracts 共享语义层。

#### 6.5.1 TypedConfig / patch / schema / profiles 键名冻结（v1）

冻结结论：ConfigCenter v1 的 typed 配置输入固定由 `TypedConfig`、`ConfigPatch`、`ConfigPatchEntry`、`ConfigLayerRef` 共同表达；`defaults/profile` 只接受 `runtime_policy.yaml` 风格 YAML 文档，`deployment_override` 只接受受管 overlay YAML 文档，`runtime_override` 只接受结构化 patch 对象，不接受业务模块私有自由字典。

schema_version 与来源格式：

1. `schema_version` 当前冻结为字符串 `1`；未显式声明或声明为其他版本时，ConfigCenter 必须在 merge 前拒绝。
2. `defaults`、`profile` 两层的输入格式固定为 `runtime_policy.yaml`（YAML v1）。
3. `deployment_override` 输入格式固定为受管 overlay YAML v1，仅允许站点/设备/发布流水线产物提供。
4. `runtime_override` 输入格式固定为结构化 patch v1，仅允许 ConfigCenter 受鉴权入口、诊断窗口或自动化测试通道提供。

profile_id 与逻辑域命名冻结：

1. 当前支持的 `profile_id` 固定为 `desktop_full`、`cloud_full`、`edge_balanced`、`edge_minimal`、`factory_test` 五档，不得在 v1 中重命名。
2. `schema_version: 1` 的 Profile 顶层逻辑域固定为 `profile_meta`、`enabled_modules`、`runtime_budget`、`model_profile`、`token_budget_policy`、`prompt_policy`、`capability_cache_policy`、`degrade_policy`、`timeout_policy`、`execution_policy`、`ops_policy`。
3. `profile_meta` 必填键固定为 `profile_id`、`schema_version`、`target_platform`、`support_level`；缺失任一键即拒绝。
4. `enabled_modules` 继续作为唯一稳定能力真源，命名冻结沿用 profiles 详细设计 6.9，不允许 ConfigCenter 发明别名。

patch 结构冻结：

1. `ConfigPatch` 必填元数据固定为 `patch_id`、`source_kind`、`source_id`、`actor`、`target_scope`、`base_version`、`reason_code`、`patches`。
2. `runtime_override` 必须额外提供 `expires_at` 或等价 TTL；`deployment_override` 可以省略该字段。
3. `patches` 中每个条目至少包含 `op`、`key_path`、`value`；`op` 首版仅允许 `replace` 与 `remove`。
4. `replace` 要求 `value.key_path` 与 patch `key_path` 一致；`remove` 不允许携带 value。

受保护路径与拒绝规则：

1. `runtime_override` 禁止修改 `schema_version`、`profile_meta.*`、`enabled_modules.*` 与 `enabled_adapters.*`。
2. 任意 patch 的 `base_version` 与当前快照不匹配时必须拒绝，以避免 stale write。
3. 对高风险键只允许收紧不允许放宽；实现细则继续由 validator 与 profiles validator 共同收敛。

评审依据：

1. 本地证据：infra 详细设计 6.6/6.9 已冻结四层来源与 override 契约；profiles 详细设计 6.9 已冻结 `schema_version: 1` 的顶层逻辑域、`profile_meta` 必填键与 `enabled_modules` 命名表。
2. 外部参考：Azure External Configuration Store 模式要求配置接口暴露 typed/structured 数据、版本与作用域控制，并为启动失败保留 last-known-good fallback；12-Factor Config 要求把随部署变化的配置与代码分离，并避免无边界的环境分组爆炸。

### 6.6 核心接口语义定义

建议头文件：infra/include/config/

1. IConfigCenter
- load_layers(startup_context)
- get_typed(query)
- apply_override(config_patch)
- rollback(rollback_token)
- subscribe(subscription_request)

说明：`startup_context` 首版最小字段固定为 `requested_profile_id`、`deployment_source_ref`、`runtime_overlay_source_ref`、`actor_ref`；`query` 复用 6.5 的 `ConfigQuery`；`subscription_request` 固定为 `namespace_filter`、`subscriber_id` 与 `callback` 三元组，不提前冻结跨进程事件总线细节。

1. IConfigLoader
- load_default()
- load_profile(profile_id)
- load_deploy(source_ref)
- load_runtime_overlay()

1. IConfigValidator
- validate(snapshot)
- validate_patch(current_snapshot, patch)

1. IConfigSnapshotStore
- commit(snapshot)
- get_current()
- get_by_version(version)
- get_last_known_good()

1. IConfigPublisher
- publish_config_changed(diff)

#### 6.6.1 ConfigPublisher v1 事件抽象冻结

冻结结论：为解开 CFG-BLK-001，ConfigPublisher v1 不再等待独立跨进程事件总线设计；首版直接把 `ConfigPublisher` 冻结为 config 组件内部最小事件抽象，负责 `ConfigChanged` 事件发布、订阅登记和命名空间过滤投递。跨进程 broker、持久化 topic、dead-letter 与重放能力不纳入 v1。

最小接口边界：

1. 发布入口继续沿用 `IConfigPublisher::publish_config_changed(diff)`，输入保持 `ConfigDiff`，避免把快照对象直接暴露给订阅回调。
2. 订阅入口复用 `IConfigCenter` 中已冻结的 `ConfigSubscriptionRequest` 与 `ConfigSubscriptionHandle`，不新增第二套订阅对象。
3. v1 不引入独立 `unsubscribe()` 契约；取消订阅仍由句柄生命周期和后续 Facade/Broker 演进任务补齐。

命名空间过滤语义：

1. `namespace_filter` 必须非空，且采用 key_path 前缀匹配，判断条件为“任一 `ConfigDiffEntry.key_path` 以 `namespace_filter` 开头”。
2. v1 不支持通配符、正则或多命名空间并集；需要多个前缀时，调用方应注册多个订阅。
3. `ConfigChanged` 事件只投递给命中的订阅者；未命中订阅不应收到空事件或 no-op 回调。

发布与交付语义：

1. `ConfigPublishResult.event_id` 冻结为 `config-event://diff/<to_version>` 格式，便于追踪与集成测试断言。
2. `delivered_subscriber_count` 仅统计成功执行回调的订阅者数量；未命中订阅和回调失败订阅都不计入。
3. 订阅回调失败不得反向使 publisher 拒绝已验证的 diff；v1 采用进程内 at-most-once 分发，发布成功与订阅者自身处理成功解耦。
4. 订阅回调失败必须保留可观测钩子：至少能在 v1 skeleton 中通过交付计数差异与后续日志/指标接入点暴露，不允许静默吞没为“全部成功”。

事件类型边界：

1. CFG-TODO-013 的实现范围只覆盖运行时覆盖路径的 `ConfigChanged` 事件。
2. `ConfigLoaded` 仍保留为设计中的后续扩展事件名，但不纳入本轮 blocker fix 与 013 的验收面，避免把当前任务扩张到新的启动期事件工作包。

评审依据：

1. 本地证据：config 详细设计 6.3/6.7 已要求 ConfigPublisher 面向已通过校验的新快照或差异内容发出订阅事件，且 TODO 中 013 的唯一 blocker 为“事件总线最小抽象未冻结”。
2. 外部参考：Azure Publisher-Subscriber pattern 强调 publisher 与 subscriber 应通过 broker/事件总线解耦，并支持按主题或内容过滤投递；v1 先冻结为进程内最小抽象，同时保留未来升级到独立 broker 的演进空间。

错误语义（建议）：
- INF_CFG_E_NOT_FOUND
- INF_CFG_E_TYPE_MISMATCH
- INF_CFG_E_INVALID_SCHEMA
- INF_CFG_E_CONFLICT
- INF_CFG_E_SOURCE_UNAVAILABLE
- INF_CFG_E_SECRET_RESOLVE_FAIL
- INF_CFG_E_APPLY_REJECTED
- INF_CFG_E_ROLLBACK_FAILED

### 6.7 主流程时序

1. 启动阶段
- ConfigCenterFacade.load_layers 启动。
- ConfigLoader 依次加载 defaults、profile、deploy、runtime overlay。
- ConfigMerger 按优先级合并，并记录 source_chain。
- ConfigValidator 执行规则校验。
- 校验通过后 commit 到 ConfigSnapshotStore，标记为 current 与 LKG。
- ConfigPublisher 发布 ConfigLoaded 事件。

2. 运行时覆盖阶段
- runtime 发起 apply_override。
- ConfigValidator.validate_patch 先验校验。
- 通过后生成新 snapshot 并 commit。
- 发布 ConfigChanged 事件。
- 关键变更写入审计日志。

### 6.8 异常与恢复时序

异常分类：
1. 读取故障：配置源不可达、文件损坏。
2. 语义故障：类型不匹配、越界、互斥冲突。
3. 安全故障：secret 引用解析失败或权限不足。
4. 应用故障：运行时补丁违反白名单或影响安全边界。

恢复动作：
1. 读取故障：回退到 LKG，打 degraded 标记并告警。
2. 语义故障：拒绝新版本，保持 current，不中断服务。
3. 安全故障：拒绝读取并输出审计事件。
4. 应用故障：回滚到 rollback_token 对应版本。

兜底策略：
1. 连续 N 次更新失败，冻结运行时覆盖通道，仅允许只读查询。
2. 对上游返回可判定错误码，不做静默容错。

### 6.9 配置项与默认策略

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| infra.config.watch.enabled | true | defaults/profile/deploy | 是否启用变更监听 |
| infra.config.watch.debounce_ms | 500 | profile/deploy | 变更去抖 |
| infra.config.cache.ttl_ms | 30000 | defaults/profile | 快照缓存有效期 |
| infra.config.cache.stale_read_allowed | true | profile/deploy | 外部源故障时允许读取过期快照 |
| infra.config.validation.strict | true | defaults/profile | 是否严格校验 |
| infra.config.runtime_patch.enabled | true | profile/deploy | 是否允许运行时覆盖 |
| infra.config.runtime_patch.allowlist | 空 | profile/deploy | 允许运行时覆盖的键前缀 |
| infra.config.rollback.enabled | true | defaults/profile | 是否允许回滚 |
| infra.config.source.external.enabled | false | profile/deploy | 是否启用外置配置源 |
| infra.config.source.external.timeout_ms | 1000 | profile/deploy | 外置源读取超时 |

### 6.10 可观测性设计

日志点：
1. 启动加载开始/结束。
2. 各层加载耗时与命中来源。
3. 校验失败详情（脱敏）。
4. 变更发布、订阅回调失败、回滚结果。

指标：
1. infra_config_load_duration_ms（Histogram）
2. infra_config_apply_total（Counter）
3. infra_config_apply_fail_total（Counter）
4. infra_config_validation_fail_total（Counter）
5. infra_config_rollback_total（Counter）
6. infra_config_snapshot_version（Gauge）

追踪：
1. load_layers、apply_override、rollback 三条核心 span。
2. 每条 span 关联 request_id、trace_id、actor。

审计：
1. 高风险键变更必须审计（执行策略、模型路由、权限门槛）。
2. 审计字段包含 actor、action、key_path、before_version、after_version、outcome、evidence_ref。

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 建立 config 统一入口 | 新增 IConfigCenter + ConfigCenterFacade | 收敛配置入口与生命周期 | infra/include/config/IConfigCenter.h; infra/src/config/ConfigCenterFacade.cpp | unit: ConfigCenterFacadeTest | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R ConfigCenterFacadeTest | 依赖 contracts ResultCode/ErrorInfo |
| 建立四层加载与合并 | 新增 ConfigLoader + ConfigMerger | 落地蓝图四层模型 | infra/src/config/ConfigLoader.cpp; infra/src/config/ConfigMerger.cpp | unit: ConfigLoaderTest, ConfigMergerTest | ctest --test-dir build-ci -R "ConfigLoaderTest|ConfigMergerTest" | 阻塞：profiles 键空间规范待确认 |
| 建立配置校验与错误语义 | 新增 IConfigValidator + RuleSet | 保证配置失败可判定 | infra/src/config/ConfigValidator.cpp | unit: ConfigValidatorTest; contract: ConfigErrorMappingContractTest | ctest --test-dir build-ci -R "ConfigValidatorTest|ConfigErrorMappingContractTest" | 依赖错误码映射表 |
| 建立快照与回滚 | 新增 ConfigSnapshotStore | 实现 LKG 与回退机制 | infra/src/config/ConfigSnapshotStore.cpp | unit: ConfigSnapshotStoreTest | ctest --test-dir build-ci -R ConfigSnapshotStoreTest | 阻塞：快照持久化后端未定 |
| 建立运行时覆盖与发布 | 新增 ConfigPublisher + subscribe API | 支持动态更新与事件分发；v1 冻结为进程内 publish + namespace-filtered subscribe 抽象 | infra/src/config/ConfigPublisher.cpp | integration: ConfigRuntimePatchIntegrationTest | ctest --test-dir build-ci -R ConfigRuntimePatchIntegrationTest | 无（2026-04-02 已完成 CFG-BLK-001 解阻） |
| 建立配置观测与审计 | 接入 logging/metrics/tracing/audit | 满足可观测与治理要求 | infra/src/config/ConfigAuditBridge.cpp | integration: ConfigObservabilityIntegrationTest | ctest --test-dir build-ci -R ConfigObservabilityIntegrationTest | 阻塞：审计字段规范冻结 |

无法立即映射项：
1. 外置配置中心高可用集群能力：当前阶段先保留 IExternalConfigAdapter，不纳入首批交付。
2. 强一致分布式配置事务：超出阶段目标，留作 v2。

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

1. infra/include/config/
2. infra/src/config/
3. tests/unit/infra/config/
4. tests/integration/infra/config/
5. tests/contract/infra/（可选，若复用 contract gate）

### 8.2 分阶段实施计划

| 阶段 | 目标 | 关键动作 | 完成判定 | 风险 |
|---|---|---|---|---|
| CFG-M1 | 接口冻结 | 新增 IConfigCenter/IConfigLoader/IConfigValidator/IConfigSnapshotStore/IConfigPublisher | 头文件可编译且无循环依赖 | 低 |
| CFG-M2 | 核心链路可用 | 实现 load-merge-validate-commit 主流程 | 启动时可生成有效快照 | 中 |
| CFG-M3 | 运行时覆盖可用 | 实现 apply_override、订阅发布、回滚 | 运行时补丁可成功应用并可回滚 | 中 |
| CFG-M4 | 观测与审计闭环 | 接入日志、指标、追踪、审计桥接 | 关键操作都有可观测证据 | 中 |
| CFG-M5 | 测试与Gate收口 | 补齐 unit/integration/failure 注入并接 CI | 配置专项 Gate 全绿 | 高 |

### 8.3 原子实施任务（最小原子化 + 三件套）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| CFG-T001 | Not Started | 新增 IConfigCenter 接口并替换 placeholder 入口 | Blueprint 3.12/4.2 | infra/include/config/IConfigCenter.h; infra/src/config/ConfigCenterFacade.cpp | ConfigCenterFacadeTest | cmake --build build-ci --target dasall_infra | 编译通过且不再依赖 placeholder 作为唯一功能 |
| CFG-T002 | Not Started | 新增 ConfigLoader 实现四层读取 | Blueprint 3.12/5.1 | infra/src/config/ConfigLoader.cpp | ConfigLoaderTest | ctest --test-dir build-ci -R ConfigLoaderTest | defaults/profile/deploy/runtime 四层均可加载 |
| CFG-T003 | Not Started | 新增 ConfigMerger 实现优先级覆盖 | Infrastructure 详细设计 6.9 | infra/src/config/ConfigMerger.cpp | ConfigMergerTest | ctest --test-dir build-ci -R ConfigMergerTest | 覆盖顺序与预期一致 |
| CFG-T004 | Not Started | 新增 ConfigValidator 与规则集 | 编码规范 3.6 | infra/src/config/ConfigValidator.cpp | ConfigValidatorTest | ctest --test-dir build-ci -R ConfigValidatorTest | 非法配置被拒绝且错误可定位 |
| CFG-T005 | Not Started | 新增 ConfigSnapshotStore 与 LKG 回退 | Azure External Config Pattern | infra/src/config/ConfigSnapshotStore.cpp | ConfigSnapshotStoreTest | ctest --test-dir build-ci -R ConfigSnapshotStoreTest | 失败时可回退到 last-known-good |
| CFG-T006 | Not Started | 新增运行时覆盖 apply_override | Blueprint Profile 约束 | infra/src/config/ConfigRuntimePatch.cpp | ConfigRuntimePatchIntegrationTest | ctest --test-dir build-ci -R ConfigRuntimePatchIntegrationTest | 白名单键可覆盖，非白名单拒绝 |
| CFG-T007 | Not Started | 新增 ConfigPublisher 订阅发布机制 | OTel 配置接口分层 | infra/src/config/ConfigPublisher.cpp | ConfigPublishSubscribeTest | ctest --test-dir build-ci -R ConfigPublishSubscribeTest | 订阅者可接收变更且含版本号 |
| CFG-T008 | Not Started | 新增 ConfigAuditBridge 可观测桥接 | 架构可观测原则 | infra/src/config/ConfigAuditBridge.cpp | ConfigObservabilityIntegrationTest | ctest --test-dir build-ci -R ConfigObservabilityIntegrationTest | 变更事件具备日志+指标+审计 |
| CFG-T009 | Not Started | 补齐故障注入测试 | 工程规范 3.7 | tests/integration/infra/config/ConfigFailureInjectionTest.cpp | ConfigFailureInjectionTest | ctest --test-dir build-ci -R ConfigFailureInjectionTest | 至少 4 类故障路径可验证 |

## 9. 测试与质量门

| 测试层级 | 覆盖范围 | 核心用例 | 验收方式 |
|---|---|---|---|
| Unit | loader/merger/validator/snapshot/publisher | 合并顺序、类型校验、版本递增、回滚正确性 | gtest 全绿 |
| Contract | 错误码映射与事件字段稳定性 | ConfigErrorMappingContractTest | 不出现 breaking 字段变更 |
| Integration | runtime 与 infra/config 装配 | 启动加载、运行时覆盖、订阅通知 | 集成测试全绿 |
| Failure Injection | 外置源不可用、配置损坏、补丁非法、回滚失败 | ConfigFailureInjectionTest | 每条路径都产生可观测证据 |
| Compatibility | profile 差异行为 | desktop_full 与 edge_balanced 行为一致性检查 | Profile 回归通过 |

Gate 建议：
1. CFG-G1：unit 全绿，否则阻断。
2. CFG-G2：integration 全绿，否则阻断。
3. CFG-G3：failure injection 关键路径全绿，否则阻断。
4. CFG-G4：contracts 边界检查通过，否则阻断。
5. CFG-G5：Profile 兼容检查通过，否则阻断。

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | 所有读取配置的模块 | 先新增接口与默认实现，再逐模块切换到 IConfigCenter | 先 desktop_full，再 edge_balanced，最后 edge_minimal | 预留 IExternalConfigAdapter、FeatureFlagProvider |
| Medium（仅当接口签名调整） | runtime/apps/tools/memory 等 | 提供 v1/v2 适配层并行 1 个迭代周期 | 双读对比，指标稳定后切换 | 预留声明式 schema 与版本迁移器 |

演进原则：
1. 默认向后兼容，新增优先 optional。
2. 破坏性变更必须先评审再落地。
3. 先灰度后全量，回滚路径必须先验可用。

## 11. 风险、阻塞与回退（建议级）

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| B-CFG-01 Profile 键空间未冻结 | CFG-T002/T003/T006 | profiles 的 config 键命名评审通过 | 先用临时命名映射层 | 暂停运行时覆盖，仅启用 defaults+profile |
| B-CFG-02 外置配置源协议未定 | CFG-T002/T005 | IExternalConfigAdapter 最小接口冻结 | 先实现 file/env 两类本地源 | 关闭 external.enabled，使用本地配置 |
| B-CFG-03 审计字段规范未定 | CFG-T008 | 审计字段最小集合冻结 | 先记录核心 6 字段 | 缺省写入降级审计 sink |
| B-CFG-04 快照持久化后端未定 | CFG-T005 | 持久化策略评审通过 | 先内存快照 + 启动导入 | 保留仅进程内回滚能力 |
| B-CFG-05 运行时覆盖安全边界未定 | CFG-T006 | 白名单前缀与审批策略冻结 | 先禁用高风险键覆盖 | 关闭 runtime_patch.enabled |

## 12. 未决问题与后续任务

未决问题：
1. 外置配置源首版是否直接接入统一远端服务，还是先保留本地 file/env。
2. 快照持久化是否采用 sqlite3 还是纯文件序列化。
3. 高风险配置键的审批机制由谁触发与如何审计。
4. 配置变更与 runtime 热重载的最小一致性语义（立即生效或窗口生效）。

后续任务建议：
1. 在 docs/todos 下新增 infra-config 专项 TODO，落地 CFG-T001 到 CFG-T009。
2. 优先推进 CFG-M1 与 CFG-M2，先替换占位实现并形成可运行主链路。
3. 将配置故障注入纳入 tests/integration 与 CI Gate，确保回退路径可持续验证。
4. 在 edge_balanced 与 edge_minimal 做一轮配置热更新压测，验证去抖与缓存策略。