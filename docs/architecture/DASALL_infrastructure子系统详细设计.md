# DASALL Infrastructure 子系统详细设计（Detailed Design）

版本：v1.0  
日期：2026-03-24  
阶段：Detailed Design  
适用模块：infra/

## 1. 子系统概览

### 1.1 目标与定位

Infrastructure 子系统属于 Layer 1（Infrastructure Layer），为全系统提供基础能力，不承载业务决策。其核心目标是：

1. 统一日志、追踪、指标、审计四类可观测能力，且审计链路独立于普通运行日志。
2. 提供配置、密钥、安全策略、健康检查、诊断、升级、插件治理等基础能力。
3. 为 Runtime 与各业务子系统提供稳定、可裁剪、可验证的底座接口。

来源依据：
1. docs/architecture/DASSALL_Agent_architecture.md（3.4.7、5.10、8.5、8.6、8.7、8.9）
2. docs/architecture/DASALL_Engineering_Blueprint.md（3.12、4.1、4.2）

### 1.2 边界定义

上游消费者：apps、runtime、cognition、llm、tools、memory、knowledge、services、multi_agent。  
下游依赖：platform 抽象、third_party 组件、OS 资源。  
同层协同：infra 内部子组件（logging/audit/tracing/metrics/config/secret/security_policy/diagnostics/health/watchdog/ota/plugin）。

### 1.3 设计范围

纳入范围：
1. infra 子组件拆分、接口语义、对象模型、主异常流程、配置策略、可观测方案。
2. 可映射到 Build 的实施计划与测试门禁。

不纳入范围：
1. Runtime 主状态机、Tool 执行编排、Memory 语义装配的实现细节。
2. contracts 共享语义对象重定义。

---

## 2. 约束清单

### 2.1 Must / Should / Must-Not 约束表

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| INF-C001 | DASSALL_Agent_architecture.md 3.4.7/5.10 | Must | Infrastructure 必须提供 logging/trace/metrics/audit 与配置、密钥、安全策略、诊断、升级、插件能力 | 子组件、接口 |
| INF-C002 | DASSALL_Agent_architecture.md 3.7 | Must | 上层可依赖下层抽象，Infra 不反向依赖业务模块 | 依赖关系 |
| INF-C003 | DASALL_Engineering_Blueprint.md 4.2 | Must-Not | infra/ -> 任何业务模块 为禁止依赖方向 | 模块边界 |
| INF-C004 | DASALL_Engineering_Blueprint.md 4.3 | Must | 跨模块调用必须通过 contracts 冻结接口，禁止直接依赖实现类 | 接口设计 |
| INF-C005 | ADR-005-architecture-review-baseline.md | Must | contracts 与关键边界冻结前，不能以 infra 设计反向改写主架构结论 | 设计治理 |
| INF-C006 | ADR-006-context-orchestrator-vs-prompt-composer.md | Must-Not | infra 不接管上下文装配与 Prompt 渲染职责，仅记录观测事实 | 边界职责 |
| INF-C007 | ADR-007-reflection-engine-vs-recovery-manager.md | Must-Not | infra 不做失败语义判定与恢复裁定，只提供观测与执行支撑 | 异常流程 |
| INF-C008 | ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | Must | infra 仅记录/支撑全局主控和协同链路，不拥有调度权 | 边界职责 |
| INF-C009 | DASALL_contracts冻结实施计划.md 5/6 | Must-Not | 不把 infra 实现细节（线程池、sink、存储策略）写入 contracts 共享对象 | contracts 对齐 |
| INF-C010 | DASALL_contracts冻结TODO总表.md + M5 冻结包 | Must | 以 contracts V1 Ready 作为语义输入；新增字段优先 optional 与向后兼容 | 兼容策略 |
| INF-C011 | DASALL_工程协作与编码规范.md 3.6 | Must | 错误不能吞没，失败必须可观测（日志/指标/审计） | 错误语义 |
| INF-C012 | DASALL_工程协作与编码规范.md 3.7 | Should | 新增公共接口同步新增 unit/contract/integration 测试 | 测试门禁 |
| INF-C013 | DASALL_Engineering_Blueprint.md 3.13/5.1 | Must | Profile 只能裁剪能力和替换实现，不得绕过 Audit 与 Runtime 主控链路 | 配置策略 |
| INF-C014 | OTel Logs Spec + spdlog async wiki | Should | 日志需支持 trace/span/resource 关联，异步队列溢出策略显式配置 | 可观测与性能 |

### 2.2 约束抽取结论

Must：边界单向依赖、四类观测能力、contracts 兼容优先、Profile 可裁剪。  
Should：OTel 关联字段、异步可观测、可测试门禁。  
Must-Not：不改 ADR、不污染 contracts、不越权到业务主控。

---

## 3. 现状与缺口

### 3.1 现状识别

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| infra 顶层模块可编译 | 已实现（骨架） | 仅 placeholder，未承载真实能力 | Medium | P1 |
| infra 接口头文件体系 | 缺失 | infra/include 为空，缺少 IXxx 接口 | High | P0 |
| logging/tracing/metrics/config/secret/health/ota 实现 | 缺失（目录占位） | 目录存在但无实现与测试 | High | P0 |
| audit 独立组件化 | 缺失（仅接口与对象） | IAuditLogger 与 AuditEvent 已定义，但无 AuditService 独立组件与独立目录 | High | P0 |
| security policy 子域设计 | 缺失 | 架构要求存在安全策略能力，但无 SecurityPolicyManager 与规则对象模型 | High | P0 |
| plugin manager 子域设计 | 缺失 | 架构要求 OTA/Plugin Manager，但当前仅有 OTAManager | High | P0 |
| diagnostics 子域设计 | 缺失 | 仅有 execute(command) 统一入口，缺少 DiagnosticsService 命令域与输出模型 | High | P0 |
| logging 详细设计文档 | 已实现（设计） | 仅 logging 子域有详细设计，其余子域缺失 | Medium | P1 |
| contracts 对齐输入 | 已实现（可用） | 需要消费 V1 已冻结对象，不可反向扩写 | Medium | P0 |
| infra 测试基线 | 缺失 | 无 unit/integration/failure injection 覆盖 | High | P0 |

证据：
1. infra/CMakeLists.txt（仅 src/placeholder.cpp）
2. infra/src/placeholder.cpp
3. infra/include 为空目录
4. docs/architecture/DASALL_infra_logging模块详细设计.md（仅 logging 子域已有详细设计）

### 3.2 现状-目标冲突

| 冲突类型 | 描述 | 影响 | 风险等级 |
|---|---|---|---|
| 边界冲突 | 如果 infra 直接依赖 runtime/cognition 实现，将违反蓝图禁止依赖 | 破坏分层与替换能力 | High |
| 语义重复 | 若 infra 重定义 ErrorInfo/Observation 语义，将与 contracts V1 冲突 | 返工与测试失效 | High |
| 依赖反转 | 若业务模块直接绑 spdlog/openssl 细节 | 难以 profile 裁剪 | Medium |

---

## 4. 候选方案对比

### 4.1 候选方案概述

1. 方案 A：最小同步基础设施（单进程同步 I/O + 轻量接口）。
2. 方案 B：分层异步基础设施（Infra Facade + 子域管理器 + 异步队列 + 健康降级）。
3. 方案 C：OTel 原生管线优先（以 OTel SDK/Collector 作为主链路，文件落盘为兼容旁路）。

### 4.2 对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| A 同步最小方案 | 中 | 中 | 低 | 高负载阻塞、难满足长期运行和审计隔离 | 淘汰：仅适合 PoC，不满足阶段目标 |
| B 分层异步方案 | 高 | 高 | 中 | 组件较多，需要严格接口治理 | 保留并采纳：平衡实现成本与可演进性 |
| C OTel 原生优先 | 中高 | 高 | 高 | 依赖链重、边缘设备成本高、当前工程准备不足 | 暂不采纳：列为 v2 演进路径 |

### 4.3 行业方案匹配结论

1. OTel 建议统一 logs-traces-metrics 关联语义，适合作为字段与导出预留标准。
2. spdlog 异步模型支持 block/overrun_oldest 两种溢出策略，符合 Profile 差异化需求。
3. Azure Health Endpoint Monitoring 模式支持 liveness/readiness 分层健康检查，适配 infra/health 设计。
4. Azure Retry 模式强调“只在理解全上下文的控制层重试”，与 ADR-007 的 Runtime 裁定边界一致。

---

## 5. 决策结论

### 5.1 最终选型

采纳方案 B：分层异步基础设施方案。

### 5.2 选择依据

1. 与架构一致：符合 Layer 1 职责，不反向依赖业务。
2. 与 ADR 一致：不侵入 Context/Prompt、Reflection/Recovery、Orchestrator/Coordinator 边界。
3. 与 contracts 一致：只消费 V1 对象，保持兼容演进。
4. 与工程现状匹配：可在当前骨架上分阶段落地，避免一次性引入高复杂 OTel 全家桶。

### 5.3 放弃其他方案理由

1. 方案 A 无法满足长期运行和审计要求。
2. 方案 C 成本高于当前阶段收益，且 edge_minimal 档位落地风险大。

---

## 6. 详细设计

### 6.1 职责边界

Infrastructure 职责：
1. 提供统一基础能力入口与可插拔实现。
2. 提供可观测信号采集、聚合、导出。
3. 提供系统配置、密钥、安全策略、健康、诊断、升级、插件治理。

Infrastructure 非职责：
1. 不负责业务编排与策略决策。
2. 不修改 contracts 公共语义。
3. 不管理 runtime 主状态机。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| InfraServiceFacade | 对外统一入口，协调各子域初始化与生命周期 |
| LoggingService | 结构化运行日志写入、缓冲、导出 |
| AuditService | 审计事件写入、导出、合规保留与失败兜底 |
| TracingService | Trace span 生命周期与上下文桥接 |
| MetricsService | 指标采集、聚合、导出 |
| ConfigCenter | 四层配置合并、校验、热更新发布 |
| SecretManager | 凭证加载、轮换、权限控制 |
| SecurityPolicyManager | 安全策略装载、校验、发布与策略快照管理 |
| DiagnosticsService | 诊断命令执行、快照导出、故障证据聚合 |
| HealthMonitor | liveness/readiness 评估、依赖探测、告警事件 |
| WatchdogAgent | 关键线程心跳检测与超时上报 |
| OTAManager | 升级包校验、灰度切换、回滚控制 |
| PluginManager | 插件发现、校验、装载、启停与版本/兼容管理 |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| InfraServiceFacade | App/Runtime 初始化参数 | 各子域 init 结果 | 返回 ResultCode + ErrorInfo 引用 |
| LoggingService | LogEvent + Context IDs | 文件/控制台/远程导出 | 保留 request_id/session_id/trace_id/task_id |
| AuditService | AuditEvent + Context IDs | 审计存储/导出接口 | 审计不可静默丢失，失败需可观测 |
| TracingService | SpanStart/SpanEnd/Attributes | TraceSink/Log关联字段 | 支持 trace_id/span_id 注入 |
| MetricsService | Counter/Gauge/Histogram 采样 | Metrics Exporter | 指标名和标签受白名单治理 |
| ConfigCenter | 默认/Profile/部署/运行时覆盖 | TypedConfig + 变更事件 | 合并失败返回配置错误码 |
| SecretManager | SecretQuery/RotationRequest | SecretValueHandle/审计记录 | 明文不落盘 |
| SecurityPolicyManager | PolicyBundle/PolicyPatch | PolicySnapshot/决策依据引用 | 策略变更需版本化且可回滚 |
| DiagnosticsService | 诊断命令 + 采样窗口 | DiagnosticsSnapshot/导出文件 | 诊断输出含 evidence_ref 与脱敏规则 |
| HealthMonitor | 子组件健康探针结果 | HealthSnapshot/事件总线 | 输出 liveness/readiness/degraded |
| WatchdogAgent | 心跳与deadline | 超时事件/审计事件 | 超时动作可配置（告警或触发恢复请求） |
| OTAManager | UpgradePlan/Package | UpgradeOutcome | 支持前置检查与失败回滚 |
| PluginManager | PluginManifest/PluginPackage/Profile 策略 | PluginCatalog/LoadResult/CompatibilityReport | 插件必须通过签名与兼容检查 |

### 6.4 子组件依赖关系

1. InfraServiceFacade -> LoggingService, AuditService, TracingService, MetricsService, ConfigCenter, SecretManager, SecurityPolicyManager, DiagnosticsService, HealthMonitor, WatchdogAgent, OTAManager, PluginManager。
2. HealthMonitor 依赖各子组件公开的 IHealthProbe，不依赖其内部实现。
3. Logging/Tracing/Metrics 互相弱耦合：通过 ContextCorrelationAdapter 共享 trace_id 与 resource 标签。
4. AuditService 与 LoggingService 逻辑分离，仅通过 ContextCorrelationAdapter 共享关联字段。
5. SecurityPolicyManager 依赖 ConfigCenter 加载策略层，向 SecretManager/PluginManager/DiagnosticsService 输出策略快照。
6. PluginManager 依赖 SecurityPolicyManager 做签名和来源校验，依赖 DiagnosticsService 输出故障证据。
7. OTAManager 依赖 ConfigCenter 与 HealthMonitor 做发布前后检查。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | contracts 对齐关系 |
|---|---|---|---|
| InfraContext | request_id, session_id, trace_id, task_id, parent_task_id, lease_id | 缺失字段允许 unknown，不允许空指针传递 | 消费 AgentRequest/WorkerTask/WorkerLease 标识语义 |
| LogEvent | level, module, message, attrs, ts | attrs 需可序列化；敏感字段先脱敏 | 不新增 contracts 字段 |
| AuditEvent | action, actor, target, evidence_ref, outcome, side_effects | 审计记录不可静默丢弃 | 对齐 ToolResult/RecoveryOutcome 引用语义 |
| SecurityPolicySet | version, rules, source, checksum, updated_at | 规则变更可追溯、可回滚 | 仅引用 contracts 中的 PolicyDecision 语义，不扩写 |
| DiagnosticsSnapshot | snapshot_id, command, collected_at, summary, evidence_refs | 必须脱敏并可导出 | 通过 evidence_ref 对齐 Observation 证据引用语义 |
| PluginDescriptor | plugin_id, version, abi, trust_level, status | 需通过签名与兼容检查后才可激活 | 不反向扩写上层 Tool/Skill 契约 |
| HealthSnapshot | liveness, readiness, degraded, failed_components | 状态机只在 infra 内部 | 不反向写入 Runtime 状态 |
| UpgradeOutcome | phase, result, rollback_applied, evidence_ref | 必须可回放 | 与 AgentResult 分层，不混用 |

### 6.6 核心接口语义定义

建议头文件分布（蓝图一致）：infra/include/。

1. IInfrastructureService
	- init(config): 初始化 Infra 各子域。
	- start(): 启动后台线程与导出器。
	- stop(timeout_ms): 优雅停机与 flush。
	- execute(command): 统一基础命令入口（诊断/导出/升级触发）。

2. ILogger
	- log(event): 写普通日志。
	- flush(deadline): 刷新缓冲。

3. IAuditLogger
	- write_audit(event): 写审计日志。
	- export_audit(filter): 导出审计片段。

4. ISecurityPolicyManager
	- load_policy(bundle): 装载并校验安全策略。
	- apply_patch(patch): 热更新策略并生成新快照。
	- snapshot(): 返回当前策略快照与版本。

5. IDiagnosticsService
	- execute(command): 执行诊断命令并返回快照。
	- export_snapshot(snapshot_id): 导出故障证据。

6. IPluginManager
	- discover(): 发现可用插件清单。
	- validate(manifest): 执行签名与兼容校验。
	- load(plugin_id): 装载插件并返回 LoadResult。
	- unload(plugin_id): 卸载插件并释放资源。

7. IConfigCenter
	- load_layers(): 加载四层配置。
	- get_typed(path): 获取强类型配置。
	- apply_override(patch): 运行时覆盖。
	- 语义补充：`load_layers()` 只接受 defaults/profile/deployment 三类受管来源；`apply_override(patch)` 只接受带来源元数据、作用域与 TTL 的 runtime override patch，不接受业务模块私有自由字典。

8. IHealthMonitor
	- register_probe(name, probe): 注册探针。
	- evaluate(): 生成 HealthSnapshot。

9. ISecretManager
	- get_secret(key): 返回 SecretHandle。
	- rotate(key): 触发轮换并审计。

10. IOTAManager
	- precheck(plan): 发布前检查。
	- apply(plan): 执行升级。
	- rollback(token): 回退版本。

错误语义（infra 私有错误码域，映射 contracts::ResultCode）：
1. INF_E_CONFIG_INVALID
2. INF_E_SECRET_UNAVAILABLE
3. INF_E_LOG_QUEUE_FULL
4. INF_E_AUDIT_WRITE_FAIL
5. INF_E_HEALTH_PROBE_TIMEOUT
6. INF_E_OTA_VERIFY_FAIL
7. INF_E_OTA_ROLLBACK_FAIL
8. INF_E_PLUGIN_VALIDATE_FAIL
9. INF_E_PLUGIN_LOAD_FAIL
10. INF_E_POLICY_INVALID
11. INF_E_DIAG_COMMAND_DENIED
12. INF_E_DIAG_EXPORT_FAIL

### 6.7 主流程时序（正常）

1. App 启动调用 InfraServiceFacade.init。
2. ConfigCenter 完成四层配置合并并下发子组件配置。
3. SecurityPolicyManager 装载策略并发布策略快照。
4. Logging/Audit/Tracing/Metrics 初始化并注册导出器。
5. SecretManager 加载必要密钥句柄并绑定策略快照。
6. PluginManager 扫描与校验插件，按 Profile 激活可用插件。
7. DiagnosticsService 完成诊断命令域注册与快照存储初始化。
8. HealthMonitor 注册探针并首次评估。
9. WatchdogAgent 启动心跳监测。
10. InfraServiceFacade.start 返回 Ready。

### 6.8 异常与恢复时序

异常分类：
1. 可重试瞬时故障：导出端短暂不可用、瞬时 I/O 失败。
2. 降级可运行故障：审计主 sink 不可用、指标推送失败。
3. 致命故障：配置非法、密钥读取失败、OTA 校验失败。

恢复动作：
1. Logging queue 满：按 Profile 策略 block 或 overrun_oldest，并上报 drop 指标。
2. Audit sink 故障：切换 fallback sink + 强告警，不允许静默丢审计。
3. Plugin 校验失败：拒绝激活并记录兼容报告 + 审计事件。
4. Policy 装载失败：回退到上一版本策略快照并上报告警。
5. Diagnostics 命令被拒绝：返回明确错误码与策略引用，不执行命令。
6. Health 探针超时：标记 degraded，触发恢复建议事件。
7. OTA apply 失败：执行 rollback，记录 UpgradeOutcome 与证据。

兜底策略：
1. 连续 N 次关键故障触发 infra_safe_mode（仅保留核心日志+健康检查）。
2. 对上游返回明确失败码，禁止隐藏失败。

### 6.9 配置项与默认策略

#### ConfigCenter 四层来源与 override 契约（v1）

冻结结论：ConfigCenter 的四层来源固定为 `defaults`、`profile`、`deployment_override`、`runtime_override`。其中前两层属于静态基线；后两层属于受管覆盖层。ConfigCenter 必须在读取或接收 patch 阶段完成来源鉴别、作用域检查和 typed 结构化校验，再把合格输入交给合并器；不允许把任意 JSON、YAML 或 HTTP 参数直接下沉到合并层。

| 层级 | 载入入口 | 来源类型 | 允许写入者 | 生命周期 | 说明 |
|---|---|---|---|---|---|
| `defaults` | `load_default()` | 仓库内默认配置 | 开发与构建阶段 | 随版本发布 | 最低默认值，不携带环境差异 |
| `profile` | `load_profile()` | profiles 基线资产 | profile 设计与发布流程 | 随 profile 版本发布 | 表达 Build/Profile 固定差异 |
| `deployment_override` | `load_deploy()` | 站点/设备/环境受管配置快照 | 发布流水线、站点运维 | 随部署版本或站点包更新 | 允许环境适配，不得改 profile 身份 |
| `runtime_override` | `apply_override()` / `load_runtime_overlay()` | 受鉴权运行时 patch | 受控运维、诊断窗口、自动化测试 | 临时，需 TTL 或 rollback | 只允许白名单键，必须可审计可回滚 |

override 输入对象最小契约：
1. `override_id`、`source_kind`、`source_id`、`issued_by`、`target_scope`、`base_version`、`reason_code`、`patches` 为必填字段。
2. `runtime_override` 必须额外带 `expires_at` 或等价 TTL；超时后由 ConfigCenter 自动失效或要求显式 rollback。
3. `patches` 中每个条目至少包含 `path`、`op`、`value`；`op` 首版仅允许 `replace` 与显式 `remove` 白名单子集，禁止脚本化表达式。

来源与权限规则：
1. `deployment_override` 只能来自受管部署产物、本地站点包、设备包或外置配置存储的已版本化快照；不得来自终端用户请求或业务模块自行写文件。
2. `runtime_override` 只能来自 ConfigCenter 受鉴权 API、诊断入口或自动化测试通道；不得来自普通业务流量、未签名参数、cookie、query string 或未经授权的环境变量热改。
3. ConfigCenter 需区分读权限与写权限；写入 runtime override 的主体必须可审计，且能映射到 actor/session/ticket。

白名单与拒绝规则：
1. 允许动态覆盖的键必须显式白名单化；默认拒绝所有未声明路径。
2. 禁止覆盖 `schema_version`、`profile_meta.*`、`enabled_modules.*` 以及会改变安全或审计硬门槛的关键键。
3. 对高风险键只允许收紧不允许放宽；例如确认门槛、安全模式、审计等级只能维持或增强。
4. patch 的 `base_version` 与当前快照版本不匹配时必须拒绝，以避免 stale write。

与 profiles 的边界约定：
1. ConfigCenter 负责校验来源、权限、TTL 与 typed patch 结构；ProfileOverlayComposer 负责消费已校验的 deployment/runtime override 对象。
2. ConfigCenter 不理解具体 profile 业务语义，只保证 override 对象满足来源与结构契约；最终语义接受由 profiles validator 和下游模块校验共同完成。
3. 合并失败或 patch 被拒绝时，ConfigCenter 返回明确配置错误并保留当前快照，不得写入半成品状态。

评审依据：
1. 本地证据：infra 详细设计已冻结四层模型与 `IConfigCenter.apply_override(patch)` 入口；profiles 详细设计要求 OverlayComposer 只处理 Profile 层与 deployment/runtime override 的受管合并。
2. 外部参考：Azure External Configuration Store 模式要求配置接口暴露 typed/structured 数据、版本与作用域控制，并为启动失败保留 fallback；Martin Fowler 将 runtime override 视为高风险但必要的运维能力，建议与静态配置分开治理，并限制在受控路径使用。

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| infra.logging.level | INFO | 默认/Profile/部署/运行时 | 最低日志级别 |
| infra.logging.async.enabled | true | 默认/Profile | 是否异步写入 |
| infra.logging.async.queue_size | 8192 | Profile/部署 | 队列大小 |
| infra.logging.async.overflow_policy | block | Profile/部署 | block/overrun_oldest |
| infra.audit.required | true | 默认/Profile | 审计不可关闭（可降级） |
| infra.tracing.enabled | true | Profile | 是否启用 tracing |
| infra.metrics.export.interval_ms | 5000 | 默认/Profile | 指标导出周期 |
| infra.health.liveness.interval_ms | 2000 | Profile/部署 | 存活探测间隔 |
| infra.health.readiness.interval_ms | 5000 | Profile/部署 | 就绪探测间隔 |
| infra.watchdog.timeout_ms | 15000 | Profile/部署 | 心跳超时阈值 |
| infra.ota.enabled | false | Profile/部署 | edge/desktop 差异化 |
| infra.secret.backend | file | Profile/部署 | file/kms/mock |
| infra.security.policy.mode | strict | Profile/部署 | strict/compat 两档策略模式 |
| infra.security.policy.hot_reload | true | Profile/部署 | 是否允许策略热更新 |
| infra.plugin.enabled | true | Profile/部署 | 是否启用插件机制 |
| infra.plugin.allowlist | [] | Profile/部署 | 插件白名单 |
| infra.plugin.load_timeout_ms | 3000 | Profile/部署 | 插件装载超时 |
| infra.diagnostics.remote.enabled | false | Profile/部署 | 是否启用远程诊断导出 |
| infra.diagnostics.snapshot.retention_days | 7 | Profile/部署 | 诊断快照保留时间 |

### 6.10 可观测性设计

日志点：
1. infra init/start/stop 生命周期。
2. 配置加载与配置变更。
3. 队列溢出、fallback、恢复事件。
4. 策略装载/回滚、插件装载/卸载、诊断命令执行/拒绝。
5. 升级开始/完成/回滚。

指标：
1. infra_log_write_total、infra_log_drop_total、infra_log_queue_depth。
2. infra_audit_write_fail_total。
3. infra_policy_reload_total、infra_policy_invalid_total。
4. infra_plugin_load_total、infra_plugin_load_fail_total、infra_plugin_active_count。
5. infra_diag_command_total、infra_diag_command_denied_total、infra_diag_export_fail_total。
6. infra_health_degraded_total、infra_probe_latency_ms。
7. infra_ota_apply_duration_ms、infra_ota_rollback_total。

追踪：
1. 为 infra 命令执行创建 span（init/start/execute/rollback）。
2. 日志与审计记录中附 trace_id/span_id。

审计：
1. 高风险命令（secret rotate、ota apply、ota rollback、plugin load/unload、policy patch）强制审计。
2. 审计字段必须包含 actor、action、target、outcome、evidence_ref。

### 6.11 独立组件化建议

组件化建议分级：
1. Must（本轮应独立组件化）：AuditService、PluginManager、SecurityPolicyManager、DiagnosticsService。
2. Should（后续演进可独立）：TracingExporterAdapter、MetricsExporterAdapter（当前可先留在各子域内部）。

建议依据：
1. AuditService：架构要求审计独立保存与导出，且当前跨子域已出现审计接口冻结阻塞。
2. PluginManager：架构明确 OTA/Plugin Manager，同域治理可避免升级、签名、兼容检查分散。
3. SecurityPolicyManager：架构要求安全策略能力，且该能力跨 secret/plugin/diagnostics 复用。
4. DiagnosticsService：架构要求诊断与运维支持，需独立命令域和证据导出模型。

最小落地边界：
1. 先冻结接口与对象模型（L2），再进入实现。
2. 不引入新的平级子系统，保持在 Infrastructure 内部组件化。
3. 所有组件均通过 InfraServiceFacade 生命周期统一编排。

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 建立 infra 统一入口与生命周期管理 | 新增 InfraServiceFacade 与 IInfrastructureService | 将设计主控点收敛为单入口，避免模块分散初始化 | infra/include/IInfrastructureService.h; infra/src/InfraServiceFacade.cpp | unit: InfraServiceFacadeTest | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R InfraServiceFacadeTest | 依赖 contracts ResultCode/ErrorInfo |
| 建立 logging 通道 | 新增 LoggingService | 满足普通运行日志可观测要求 | infra/include/ILogger.h; infra/src/logging/* | unit: LoggingServiceTest | ctest --test-dir build-ci -R LoggingServiceTest | 阻塞：third_party/spdlog 接入策略 |
| 建立 audit 独立通道 | 新增 AuditService 与 IAuditLogger | 满足审计独立存储、导出与失败兜底 | infra/include/audit/IAuditLogger.h; infra/src/audit/* | unit: AuditServiceTest; integration: InfraAuditIntegrationTest | ctest --test-dir build-ci -R "AuditServiceTest|InfraAuditIntegrationTest" | 阻塞：审计导出过滤模型与存储保留策略待冻结 |
| 建立配置四层合并能力 | 新增 ConfigCenter | 支撑 Profile 与运行时覆盖 | infra/include/IConfigCenter.h; infra/src/config/* | unit: ConfigMergePolicyTest | ctest --test-dir build-ci -R ConfigMergePolicyTest | 依赖 profiles/* 配置格式统一 |
| 建立安全策略治理能力 | 新增 SecurityPolicyManager | 支撑策略装载、校验、热更新与回滚 | infra/include/ISecurityPolicyManager.h; infra/src/security_policy/* | unit: SecurityPolicyManagerTest; contract: SecurityPolicyContractTest | ctest --test-dir build-ci -R "SecurityPolicyManagerTest|SecurityPolicyContractTest" | 阻塞：策略规则 schema 与冲突裁定顺序待冻结 |
| 建立诊断命令与快照能力 | 新增 DiagnosticsService | 满足诊断命令执行与证据导出 | infra/include/IDiagnosticsService.h; infra/src/diagnostics/* | unit: DiagnosticsServiceTest; integration: InfraDiagnosticsIntegrationTest | ctest --test-dir build-ci -R "DiagnosticsServiceTest|InfraDiagnosticsIntegrationTest" | 阻塞：诊断命令域与脱敏规则待冻结 |
| 建立健康检查与 watchdog | 新增 HealthMonitor/WatchdogAgent | 形成 liveness/readiness 与超时事件 | infra/include/IHealthMonitor.h; infra/src/health/* | unit: HealthProbeTest; integration: InfraHealthLoopTest | ctest --test-dir build-ci -R "HealthProbeTest|InfraHealthLoopTest" | 阻塞：线程模型参数待 runtime 对齐 |
| 建立 OTA 最小闭环 | 新增 IOTAManager + 回滚流程 | 满足升级与回退可验证要求 | infra/include/IOTAManager.h; infra/src/ota/* | unit: OTARollbackTest; integration: OTAWorkflowTest | ctest --test-dir build-ci -R "OTARollbackTest|OTAWorkflowTest" | 阻塞：包签名与存储后端未确定 |
| 建立插件治理能力 | 新增 PluginManager | 满足插件发现、校验、装载与兼容治理 | infra/include/IPluginManager.h; infra/src/plugin/* | unit: PluginManagerTest; integration: InfraPluginLifecycleTest | ctest --test-dir build-ci -R "PluginManagerTest|InfraPluginLifecycleTest" | 阻塞：插件 ABI/签名规范与兼容矩阵待冻结 |
| 建立 Infra 测试基线与 Gate | 新增 tests/unit/infra + tests/integration/infra | 将设计约束转化为自动化门禁 | tests/unit/infra/*; tests/integration/infra/*; tests/CMakeLists.txt | unit/contract/integration/failure injection | ctest --test-dir build-ci -L "unit|integration|contract" | 依赖测试标签标准化 |

不可立即映射项：
1. OTel 原生直连 Collector：当前作为 v2 演进项，不纳入本轮最小交付。
2. 远程 KMS 深度集成：受部署环境和安全基线约束，先保留 ISecretBackend 抽象。

---

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

建议目录：
1. infra/include/
2. infra/src/logging/
3. infra/src/audit/
4. infra/src/tracing/
5. infra/src/metrics/
6. infra/src/config/
7. infra/src/secret/
8. infra/src/security_policy/
9. infra/src/diagnostics/
10. infra/src/health/
11. infra/src/plugin/
12. infra/src/ota/
13. tests/unit/infra/
14. tests/integration/infra/

### 8.2 分阶段实施与完成判定

| 阶段 | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| INF-M1 接口冻结 | Not Started | 新增并冻结 infra 对外 IXxx 接口 | Blueprint 3.12/6 + 工程规范 | infra/include/*.h | unit: InterfaceCompileTest | cmake --build build-ci --target dasall_infra | 头文件齐备且可编译 |
| INF-M2 日志与审计闭环 | Not Started | 新增 LoggingService/AuditService 并替换 placeholder | 架构 5.10 + logging 详细设计 | infra/src/logging/*; infra/src/audit/* | unit + integration | ctest --test-dir build-ci -R "Logging|Audit" | 审计路径独立可写且失败可观测 |
| INF-M3 配置/安全策略/诊断闭环 | Not Started | 补齐 ConfigCenter/SecurityPolicyManager/DiagnosticsService | 架构 5.10、8.6、8.8、9.5 | infra/src/config/*; infra/src/security_policy/*; infra/src/diagnostics/* | unit + contract + failure injection | ctest --test-dir build-ci -R "Config|Policy|Diagnostics" | 策略回滚与诊断导出可验证 |
| INF-M4 密钥/健康/守护/插件闭环 | Not Started | 新增 SecretManager/HealthMonitor/WatchdogAgent/PluginManager | 架构 5.10、8.7、8.8 | infra/src/secret/*; infra/src/health/*; infra/src/plugin/* | unit + integration | ctest --test-dir build-ci -R "Secret|Health|Watchdog|Plugin" | 插件校验失败可拒绝激活且可审计 |
| INF-M5 OTA 最小闭环 | Not Started | 新增 OTAManager 最小实现与回滚 | 架构 8.9 | infra/src/ota/* | unit + integration | ctest --test-dir build-ci -R "OTA" | 升级失败可回滚 |
| INF-M6 全链路 Gate | Not Started | 接入 infra 相关测试标签与 CI 门禁脚本 | 工程规范 3.7 + contracts gate 实践 | tests/* + scripts/ci/* | unit/contract/integration/failure | ctest --test-dir build-ci -L "infra" | Gate 通过且可重复执行 |

### 8.3 原子实施任务（建议级）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| INF-T001 | Not Started | 新增 IInfrastructureService 接口并替换占位入口 | Blueprint 6 | infra/include/IInfrastructureService.h | InterfaceCompileTest | cmake --build build-ci --target dasall_infra | 编译通过且无 placeholder 依赖 |
| INF-T002 | Not Started | 新增 ILogger 与基础实现 | 架构 5.10 + logging 详细设计 | infra/include/ILogger.h; infra/src/logging/* | LoggingServiceTest | ctest --test-dir build-ci -R LoggingServiceTest | 支持结构化运行日志 |
| INF-T003 | Not Started | 新增 IAuditLogger 与 AuditService 最小实现 | 架构 5.10 + 8.8 | infra/include/audit/IAuditLogger.h; infra/src/audit/* | AuditServiceTest | ctest --test-dir build-ci -R AuditServiceTest | 审计链路独立且失败可观测 |
| INF-T004 | Not Started | 新增 IConfigCenter 四层合并策略 | 架构 8.6 | infra/src/config/* | ConfigMergePolicyTest | ctest --test-dir build-ci -R ConfigMergePolicyTest | 四层覆盖结果可重复验证 |
| INF-T005 | Not Started | 新增 IHealthMonitor 与 WatchdogAgent | 架构 8.7 | infra/src/health/* | HealthProbeTest | ctest --test-dir build-ci -R HealthProbeTest | 支持 liveness/readiness + 超时上报 |
| INF-T006 | Not Started | 新增 ISecretManager 最小后端（file/mock） | 架构 8.8 | infra/src/secret/* | SecretManagerTest | ctest --test-dir build-ci -R SecretManagerTest | 明文不落盘且审计可查 |
| INF-T007 | Not Started | 新增 IOTAManager 预检与回滚流程 | 架构 8.9 | infra/src/ota/* | OTARollbackTest | ctest --test-dir build-ci -R OTARollbackTest | apply 失败后 rollback 成功 |
| INF-T008 | Not Started | 新增 infra 集成测试与故障注入用例 | 工程规范 3.7 | tests/integration/infra/* | InfraFailureInjectionTest | ctest --test-dir build-ci -R InfraFailureInjectionTest | 至少覆盖3类故障注入 |
| INF-T009 | Not Started | 新增 ISecurityPolicyManager 与策略快照对象 | 架构 5.10 + 8.8 | infra/include/ISecurityPolicyManager.h; infra/src/security_policy/* | SecurityPolicyManagerTest | ctest --test-dir build-ci -R SecurityPolicyManagerTest | 策略加载/回滚可验证 |
| INF-T010 | Not Started | 新增 IDiagnosticsService 与诊断快照导出 | 架构 5.10 + 9.5 | infra/include/IDiagnosticsService.h; infra/src/diagnostics/* | DiagnosticsServiceTest | ctest --test-dir build-ci -R DiagnosticsServiceTest | 诊断命令与导出链路可验证 |
| INF-T011 | Not Started | 新增 IPluginManager 与插件校验/装载最小闭环 | 架构 5.10 + 7.5 | infra/include/IPluginManager.h; infra/src/plugin/* | PluginManagerTest | ctest --test-dir build-ci -R PluginManagerTest | 插件签名或兼容失败时拒绝激活 |

---

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试层 | 覆盖对象 | 关键用例 | 通过标准 |
|---|---|---|---|
| Unit | Logging/Audit/Tracing/Metrics/Config/Secret/SecurityPolicy/Diagnostics/Health/Plugin/OTA | 配置合并、日志分流、审计失败、策略回滚、诊断导出、插件校验、探针超时、升级回滚 | 全部断言通过，失败路径有明确错误码 |
| Contract | Infra 与 contracts 交互边界 | ResultCode/ErrorInfo 映射稳定；事件字段不越权 | 无越权字段、兼容性校验通过 |
| Integration | Infra 与 runtime/apps 的装配链路 | 启停流程、健康退化、回滚流程 | 关键链路可重复执行 |
| Failure Injection | 队列满、sink down、secret 缺失、probe timeout、ota verify fail | 降级、告警、回滚、错误码上报 | 每个故障有可观测证据与兜底动作 |
| Compatibility | Profile 差异与版本演进 | desktop_full/edge_balanced/edge_minimal 配置行为一致性 | 不出现 breaking 行为 |

### 9.2 质量 Gate 建议

| Gate ID | 检查项 | 失败判定 |
|---|---|---|
| INF-G1 | infra 单元测试全绿 | 任一 unit 失败即阻断 |
| INF-G2 | infra 集成测试全绿 | 任一 integration 失败即阻断 |
| INF-G3 | failure injection 关键用例全绿 | 任一故障路径无兜底动作即阻断 |
| INF-G4 | contracts 边界检查通过 | 出现越权字段或语义漂移即阻断 |
| INF-G5 | Profile 兼容检查通过 | 任一 profile 行为不一致即阻断 |

---

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | runtime/apps/tools 等调用 infra 接口方 | 通过新增接口 + 默认实现保持兼容，旧调用逐步迁移到 Facade | 先 desktop_full 灰度，再 edge_balanced，最后 edge_minimal | 预留 OTel exporter、KMS backend、远程诊断通道 |
| Medium（仅当修改接口签名） | 所有 infra 消费者 | 采用 v1/v2 并存接口 + 适配器过渡 | 双写日志指标，稳定后切换 | 预留 IInfraCommand 扩展命令域 |

演进原则：
1. 默认向后兼容，新增字段优先 optional。
2. breaking 变更必须先经 ADR/评审并提供迁移窗口。
3. Profile 维度先灰度后全量。

---

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| B-01 spdlog 接入策略未统一（静态/FetchContent） | INF-T002/INF-M2 | CMake 依赖策略评审通过 | 先用最小 mock sink 打通接口与测试 | 保留同步 fallback sink，延后异步能力 |
| B-02 Profile 配置结构未统一 | INF-T004/INF-M3 | profiles 下 infra 配置键命名冻结 | 先支持默认+部署两层 | 暂停运行时覆盖，仅保留静态配置 |
| B-03 OTA 包签名与存储规范未冻结 | INF-T007/INF-M5 | 明确签名算法、包格式、存储路径 | 先实现 dry-run precheck 与回滚框架 | 禁用 ota.apply，仅保留校验接口 |
| B-04 集成测试环境不稳定 | INF-T008/INF-M6 | 提供稳定 mock 依赖与测试夹具 | 先建立 unit+contract gate | 延后全链路集成 gate 到下一迭代 |
| B-05 安全策略规则 schema 未冻结 | INF-T009/INF-M3 | 明确规则对象、冲突裁定顺序、热更新边界 | 先冻结 PolicySnapshot 与最小规则子集 | 不启用策略热更新，仅加载静态策略 |
| B-06 诊断命令域与脱敏规则未冻结 | INF-T010/INF-M3 | 明确命令白名单、输出脱敏策略、导出格式 | 先支持只读诊断命令与本地快照导出 | 禁用远程诊断导出 |
| B-07 插件 ABI/签名规范未冻结 | INF-T011/INF-M4 | 明确 plugin manifest、ABI 兼容矩阵、签名链路 | 先支持白名单 + 本地签名校验 | 禁用动态远程插件加载 |

### 11.2 风险清单

| 风险 | 等级 | 触发条件 | 缓解动作 |
|---|---|---|---|
| 依赖边界被破坏 | High | infra 直接 include 业务实现 | 启用 include 审计脚本 + code review checklist |
| 审计链路丢失 | High | 审计 sink 故障且无 fallback | 强制 fallback + 告警 + gate 用例 |
| 资源超限 | Medium | edge_minimal 默认参数过高 | 按 profile 预置轻量参数并做压力测试 |
| 配置漂移 | Medium | 多层配置冲突且无校验 | ConfigCenter 合并校验 + 启动失败快返 |

---

## 12. 未决问题与后续任务

### 12.1 未决问题

1. SecretManager 最终生产后端采用本地加密文件还是外部 KMS。
2. OTA 包格式与签名校验链路的统一标准。
3. tracing exporter 的最小依赖集（仅本地 trace 还是含远程导出）。
4. diagnostics 命令域是否按只读诊断/运维变更双通道分层。
5. plugin manifest 与 ABI 兼容矩阵版本策略。
6. security policy 规则冲突时的裁定顺序与回滚窗口。

### 12.2 后续任务建议

1. 在 docs/todos 下新增 Infrastructure 专项 TODO，总结 INF-T001~INF-T007 的负责人与时间窗。
2. 基于本设计文档先推进 INF-M1 与 INF-M2，尽快替换 infra placeholder。
3. 对接 tests/contract 的边界守卫机制，新增 infra 边界回归测试。
4. 在 edge_balanced/edge_minimal 做一轮资源预算压测，确认异步队列默认值。

