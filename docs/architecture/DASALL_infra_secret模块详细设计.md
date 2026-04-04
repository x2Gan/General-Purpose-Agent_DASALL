# DASALL infra/secret 模块详细设计（Detailed Design）

版本：v1.0
日期：2026-04-04
阶段：Detailed Design
适用模块：infra/secret

## 1. 模块概览

### 1.1 目标与定位

infra/secret 属于 Layer 1 Infrastructure Layer，负责为 runtime、apps、tools、services、llm 等上层模块提供统一的密钥与敏感配置访问能力，但不拥有业务策略、主流程调度或恢复裁定权。

本模块目标：

1. 提供统一的 SecretManager 抽象，屏蔽 file、kms、mock 等后端差异。
2. 保证凭证与敏感配置分离存储，满足明文不落盘与最小权限访问要求。
3. 提供 secret 获取、轮换、吊销、到期检查和审计留痕的最小闭环。
4. 为不同 Profile 提供可裁剪实现，不改变上层调用语义。

来源依据：

1. docs/architecture/DASSALL_Agent_architecture.md 5.10、8.8、8.10
2. docs/architecture/DASALL_Engineering_Blueprint.md 3.12、4.1、4.2、4.3、5.1
3. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.2、6.3、6.6、6.9、8.2
4. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md 中 SecretManager 阻塞记录

### 1.2 边界定义

上游消费者：apps、runtime、tools、services、llm、memory、knowledge、multi_agent。

下游依赖：

1. contracts 中已冻结的 ResultCode、ErrorInfo、request_id/session_id/task_id/lease_id 等标识语义。
2. platform 抽象的文件、时间、随机数、线程与系统调用能力。
3. third_party 中的 openssl 或后续 KMS SDK。

同层协同：

1. config：提供 backend、缓存、轮换、审计强制等配置。
2. logging 与 audit：记录 secret 访问、轮换、拒绝、过期、回退事件。
3. health：暴露 backend 可用性、轮换积压和缓存状态探针。

### 1.3 设计范围

纳入范围：

1. secret 子组件拆分、接口语义、核心对象、异常恢复、配置与可观测设计。
2. file/mock/kms 三类后端的统一抽象边界。
3. Design -> Build 映射、实施分阶段建议、测试门与阻塞管理。

不纳入范围：

1. 外部 KMS 产品选型定版。
2. 具体密码学库的最终 API 绑定细节。
3. runtime 恢复判定、业务权限决策和用户确认流程。

---

## 2. 约束清单

### 2.1 Must / Should / Must-Not 约束表

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| SEC-C001 | DASSALL_Agent_architecture.md 5.10 | Must | infra/secret 必须作为 Infrastructure 治理能力提供统一密钥管理入口 | 子组件、接口 |
| SEC-C002 | DASSALL_Agent_architecture.md 8.8 | Must | 凭证与敏感配置必须分离存储 | 对象模型、配置 |
| SEC-C003 | DASSALL_Agent_architecture.md 8.8 | Must | 审计日志必须独立保存并可导出 | 审计流程、测试 |
| SEC-C004 | DASALL_Engineering_Blueprint.md 4.1 | Must | infra 只能依赖 contracts 与下层抽象，不得反向依赖业务模块 | 依赖方向 |
| SEC-C005 | DASALL_Engineering_Blueprint.md 4.2 | Must-Not | infra/secret 不得直接依赖 runtime、tools、services 等业务实现 | 模块边界 |
| SEC-C006 | DASALL_Engineering_Blueprint.md 4.3 | Must | 对外能力必须通过冻结接口暴露，禁止跨模块直接引用实现类 | 接口设计 |
| SEC-C007 | DASALL_Engineering_Blueprint.md 3.12 | Must | secret 子模块职责是凭证与敏感配置分离存储，而不是配置中心替代品 | 职责边界 |
| SEC-C008 | DASALL_Engineering_Blueprint.md 5.1 | Must | Profile 只能裁剪 backend、缓存和轮换策略，不能绕过审计链路 | 配置策略 |
| SEC-C009 | ADR-005-architecture-review-baseline.md 1.2、5.2 | Must | 在 contracts 与关键边界冻结前，secret 设计不得反向改写主架构结论 | 设计治理 |
| SEC-C010 | ADR-006-context-orchestrator-vs-prompt-composer.md 3.2、3.3 | Must-Not | secret 不参与上下文装配和 prompt 消息渲染，只提供基础密钥访问事实 | 边界职责 |
| SEC-C011 | ADR-007-reflection-engine-vs-recovery-manager.md 3.2、3.3 | Must-Not | secret 不做失败语义判定或恢复准入裁定，只返回可判定错误与观测事件 | 异常流程 |
| SEC-C012 | ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md 3.2、3.3 | Must-Not | secret 不拥有请求主控、预算总控和最终输出权 | 运行边界 |
| SEC-C013 | DASALL_contracts冻结实施计划.md 5、6、7 | Must | secret 只能消费 contracts 的横切 ID、错误与事件语义，不向 contracts 反写 backend/file/kms 细节 | contracts 对齐 |
| SEC-C014 | DASALL_contracts冻结TODO总表.md 4、5 | Must | contracts V1 Ready 前，新增 secret 对象优先作为 infra 本地对象，不进入共享契约 | 版本策略 |
| SEC-C015 | DASALL_工程协作与编码规范.md 3.6 | Must | secret 访问失败、权限拒绝、轮换失败必须可观测，不得吞错 | 错误语义、可观测性 |
| SEC-C016 | DASALL_工程协作与编码规范.md 3.7 | Should | 新增 ISecretManager 及其公共对象时必须同步补 unit 或 contract 测试 | 测试门禁 |
| SEC-C017 | OWASP Secrets Management Cheat Sheet 2.3/2.5/2.6/2.7 | Should | 最小权限、内存最小暴露窗口、全生命周期审计、轮换/吊销/过期必须显式建模 | 对象、流程、测试 |
| SEC-C018 | Azure Key Vault Best Practices | Should | 秘密值与管理元数据分离、缓存需可刷新、轮换支持双凭据平滑切换 | 对象、缓存、轮换 |
| SEC-C019 | Twelve-Factor Config | Must-Not | secret 模块不得接管普通配置；变化型配置与密钥必须分离管理 | 职责边界 |

### 2.2 约束抽取结论

Must：单向依赖、配置与密钥分离、明文不落盘、审计强制、兼容优先。

Should：最小权限、短生命周期句柄、自动轮换、缓存刷新、故障可测试。

Must-Not：不改写 ADR、不把 backend 细节写入 contracts、不越权到 runtime 主控和认知决策。

---

## 3. 现状与缺口

### 3.1 现状识别

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| infra/secret 目录与源码落盘 | 部分实现 | secret public headers 已落盘，secret 源码子目录与实现骨架仍待接入 | High | P0 |
| ISecretManager 接口头文件 | 部分实现 | infra/include/secret/ISecretManager.h 已落盘，后续差距集中在 manager 实现骨架 | High | P0 |
| SecretHandle / RotationRequest / 权限模型 | 部分实现 | SecretTypes 已冻结对象与最小权限边界，执行链与生产后端约束仍待补齐 | High | P0 |
| secret backend 抽象 | 部分实现 | ISecretBackend/ISecretHealthSource 已落盘，file/kms/mock 运行时适配器仍待实现 | High | P0 |
| secret 审计与健康探针 | 缺失 | 明文不落盘与审计留痕只有文档约束，没有实现与测试出口 | High | P0 |
| secret 测试基线 | 部分实现 | unit/contract 基线已接入，integration 用例仍待补齐 | Medium | P0 |
| secret 模块详细设计文档 | 已实现 | 本模块详细设计已成文，后续需与头文件和测试持续同步 | Low | P1 |

证据：

1. infra/CMakeLists.txt 已接入 core/audit/plugin/tracing 等真实源码。
2. secret 当前不再依赖 placeholder-only 构建；缺口集中在 secret 源码子目录与实现骨架。
3. docs/architecture/DASALL_infrastructure子系统详细设计.md 对 SecretManager 仅有职责、I/O 与接口名摘要。
4. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 已完成 ISecretManager、SecretTypes、ISecretBackend 与首批 unit/contract 基线；当前阻塞集中在 secret 实现骨架、backend 适配器与 integration 用例。

### 3.2 现状-目标冲突

| 冲突类型 | 描述 | 影响 | 风险等级 |
|---|---|---|---|
| 边界冲突 | 若 secret 直接读取 runtime 会话或业务角色对象，会违反 infra 单向依赖 | 破坏分层与替换能力 | High |
| 语义重复 | 若把 SecretHandle、权限策略直接写入 contracts，会污染共享语义层 | 增加 contracts 返工与兼容风险 | High |
| 配置串扰 | 若把普通配置与密钥混放到 ConfigCenter，违背配置与敏感配置分离原则 | 提高泄漏面与治理复杂度 | High |
| 实现耦合 | 若上层直接依赖 openssl/KMS SDK 细节，Profile 难以裁剪 | 限制多后端演进 | Medium |
| 恢复越权 | 若 secret 内部自行决定重试/降级/回滚级别，会侵入 runtime 恢复职责 | 违背 ADR-007 | Medium |

---

## 4. 候选方案对比

### 4.1 候选方案概述

1. 方案 A：本地文件直读方案。
2. 方案 B：统一 SecretHandle + BackendAdapter 分层方案。
3. 方案 C：远程 KMS 原生优先方案。

### 4.2 候选方案说明

#### 方案 A：本地文件直读方案

设计思路：

1. 以本地加密文件作为主要存储。
2. ISecretManager 直接暴露 get_secret(key) -> plaintext/string 或简单字节串。
3. 轮换通过覆盖文件实现。

优点：

1. 实现最简单，适合早期 PoC。
2. 无需外部依赖，边缘设备容易落地。

风险：

1. 明文暴露窗口难控制。
2. handle、lease、权限与轮换状态无法精确建模。
3. 向 KMS 或动态凭据迁移时破坏性较大。

#### 方案 B：统一 SecretHandle + BackendAdapter 分层方案

设计思路：

1. ISecretManager 对外只暴露查询、句柄获取、受控解封、轮换与吊销接口。
2. 使用 SecretBackendAdapter 屏蔽 file/kms/mock 差异。
3. 通过 SecretHandle、SecretLease、SecretMetadata、RotationRequest 表达访问生命周期与权限边界。
4. 明文只存在于受控 SecureBuffer 生命周期内，并在 release 时零化。

优点：

1. 与 infra 子系统设计和专项 TODO 的阻塞点完全对齐。
2. 能同时满足 file/mock 最小交付与未来 KMS 演进。
3. 便于审计、缓存、轮换、过期与健康检查显式建模。

风险：

1. 对象与接口比方案 A 多，需要先冻结模型。
2. C++ 层面需要显式处理内存零化与生命周期，工程细节要求更高。

#### 方案 C：远程 KMS 原生优先方案

设计思路：

1. 以外部 KMS/Secret Store 作为唯一可信源。
2. 本地仅做短期缓存与审计，不保留 file backend。
3. 轮换、过期、事件通知完全依赖远端能力。

优点：

1. 安全治理能力强，便于集中审计与自动轮换。
2. 与云上行业实践一致。

风险：

1. 与当前 edge_minimal、factory_test 场景不匹配。
2. 对网络、身份、限流、外部 SDK 和部署环境准备要求过高。
3. 当前仓库未具备可验证的 KMS 抽象与测试夹具。

### 4.3 候选方案对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| A 本地文件直读 | 中 | 中低 | 低 | 明文暴露时间长，难支撑句柄化访问与轮换演进 | 淘汰：只能做临时 PoC |
| B 统一 Handle + Adapter | 高 | 高 | 中 | 需要先冻结对象模型和权限边界 | 保留并采纳：兼顾最小交付与长期演进 |
| C 远程 KMS 原生优先 | 中 | 高 | 高 | 部署前提与测试成本过高，edge 场景落地风险大 | 暂不采纳：列为 v2 演进方向 |

### 4.4 行业方案匹配结论

1. OWASP Secrets Management Cheat Sheet 建议集中化管理、最小权限、轮换、吊销、过期和全生命周期审计，支持方案 B 的对象建模方向。
2. Azure Key Vault Best Practices 建议 secret 值与元数据分离、缓存可刷新、轮换支持双凭据平滑切换，支持方案 B 的 metadata 与 dual-slot rotation 设计。
3. Twelve-Factor Config 强调配置与密钥分离，反对将普通配置混入 secret backend，支持本模块只管理敏感凭据而不替代 ConfigCenter。

---

## 5. 决策结论

### 5.1 最终选型

采纳方案 B：统一 SecretHandle + BackendAdapter 分层方案。

### 5.2 选择依据

1. 与架构一致：符合 Layer 1 治理能力定位，不反向依赖业务模块。
2. 与 ADR 一致：secret 只提供基础能力和事实观测，不拥有上下文装配、恢复裁定或主控调度权。
3. 与 contracts 一致：只消费共享 ID、错误与事件语义，不把 file/kms/mock/backend 配置写进 contracts。
4. 与工程现状一致：可以先落地 mock/file backend 打通最小链路，再扩展到 KMS。
5. 与专项 TODO 一致：直接回应 SecretHandle、RotationRequest、权限模型未冻结的阻塞点。

### 5.3 放弃其他方案理由

1. 方案 A 放弃原因：无法稳定满足明文不落盘、句柄化访问、细粒度审计和零停机轮换要求。
2. 方案 C 放弃原因：当前仓库缺少外部依赖封装、身份模型和测试环境，不适合在详细设计阶段直接绑定远端产品能力。

### 5.4 一致性说明

1. 架构一致性：secret 只属于 infra，向上提供接口，向下依赖平台与第三方能力。
2. ADR 一致性：secret 只输出可观测结果和明确失败码，不输出语义级恢复决策。
3. contracts 一致性：SecretHandle 等对象保留在 infra 本地命名空间，不扩张到共享契约。

---

## 6. 详细设计

### 6.1 职责边界

Secret 模块职责：

1. 提供统一的 secret 查询、受控读取、轮换、吊销与到期检查能力。
2. 维护 backend 抽象，隔离 file、kms、mock 实现差异。
3. 提供审计事件与健康探针，向 logging/audit/health 报告 secret 生命周期事实。
4. 提供最小缓存与 lease 生命周期管理，控制明文暴露窗口。

Secret 模块非职责：

1. 不负责通用配置管理和配置合并。
2. 不负责业务权限判定，只校验 secret 访问上下文和本模块访问策略。
3. 不决定失败后是否重试、是否降级到其他业务路径。
4. 不直接向用户输出明文 secret，也不把 secret 注入日志、trace 和普通事件。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| SecretManagerFacade | 对外统一入口，协调查询、缓存、轮换、审计与健康上报 |
| SecretPolicyEvaluator | 校验 SecretAccessContext、用途、权限域、读写方式和到期限制 |
| SecretBackendAdapter | 统一 backend 协议，屏蔽 file/kms/mock 差异 |
| FileSecretBackend | 本地加密文件后端，适用于 desktop/edge 离线场景 |
| MockSecretBackend | 测试后端，提供可重复的 fixture 与错误注入 |
| KmsSecretBackend | 远程 KMS 适配器，占位为 v2 演进实现 |
| SecretCache | 句柄级元数据缓存与短期 materialized lease 缓存 |
| SecretLeaseRegistry | 跟踪 lease、版本、到期时间、持有者与释放状态 |
| SecretRotationCoordinator | 管理轮换状态机、双槽切换、验证与回退 |
| SecretRotationValidator | 在 promote 前执行候选版本最小验证并生成 evidence_ref |
| SecretAuditBridge | 将访问、拒绝、轮换、吊销、过期事件送入独立审计链路 |
| SecretHealthProbe | 汇总 backend 连通性、缓存命中率、轮换失败数和过期积压 |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| SecretManagerFacade | SecretQuery、SecretAccessContext、RotationRequest | SecretHandle、SecretLease、RotationResult、ErrorInfo | 所有成功/失败都必须带 request_id 或 task_id 上下文 |
| SecretPolicyEvaluator | SecretDescriptor、SecretAccessContext、ProfilePolicy | Allow/Deny 决策、拒绝原因码 | 只做 access policy 校验，不做业务审批 |
| SecretBackendAdapter | backend 配置、SecretDescriptor、version hint | SecretRecord、BackendStatus | 返回密文/元数据或受控明文缓冲，不暴露实现细节 |
| SecretCache | SecretHandle、version、ttl | CacheEntry、stale 标记 | 缓存不得绕过轮换版本检查 |
| SecretLeaseRegistry | SecretHandle、consumer_ref、deadline | lease_id、expires_at、release status | lease 过期后句柄不得继续 materialize |
| SecretRotationCoordinator | RotationRequest、backend 状态、验证结果 | RotationResult、rollback evidence | 支持 create/test/promote/revoke 四阶段 |
| SecretRotationValidator | RotationValidationContext | validate_only 结论、reason_code、evidence_ref | 只冻结最小验证输入/输出，不吸收业务审批 |
| SecretAuditBridge | SecretAuditEvent | IAuditLogger | 审计失败必须上报，不允许静默丢弃 |
| SecretHealthProbe | backend 指标、队列状态、轮换失败计数 | HealthSnapshot 子域数据 | 只输出事实状态，不触发恢复裁定 |

### 6.4 子组件依赖关系

1. SecretManagerFacade -> SecretPolicyEvaluator、SecretBackendAdapter、SecretCache、SecretLeaseRegistry、SecretRotationCoordinator、SecretAuditBridge、SecretHealthProbe。
2. SecretBackendAdapter <- FileSecretBackend / MockSecretBackend / KmsSecretBackend。
3. SecretRotationCoordinator -> SecretBackendAdapter、SecretRotationValidator、SecretPolicyEvaluator、SecretAuditBridge。
4. SecretHealthProbe 只依赖各子组件公开状态接口，不依赖内部实现细节。
5. SecretAuditBridge 只向 IAuditLogger 写入事件，不反向依赖 logging 实现类。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | contracts 对齐关系 |
|---|---|---|---|
| SecretQuery | secret_name, version_hint, purpose, access_mode | purpose 必填；不允许携带明文 fallback | 消费 request_id/session_id/task_id 作为访问上下文引用 |
| SecretAccessContext | request_id, session_id, task_id, actor, consumer_module, permission_domain | actor 与 consumer_module 必须可审计 | 只复用 contracts 横切标识与 ErrorInfo，不新增共享字段 |
| SecretDescriptor | secret_name, backend_type, classification, rotation_policy_ref, owner_ref | 仅描述元数据，不包含 secret 值 | 本地 infra 对象，不进入 contracts |
| SecretHandle | handle_id, secret_name, version, backend_ref, issued_at, expires_at, redaction_hint | 句柄不包含明文，不可序列化到普通日志 | 可引用 lease_id，但不扩写 WorkerLease 语义 |
| SecretLease | lease_id, handle_id, consumer_ref, expires_at, rotation_epoch, state | 到期或轮换代次变化后必须失效 | 复用 lease_id 语义，但不进入 contracts 共享对象 |
| SecureBuffer | byte_span, size, created_at, zeroize_on_release | 禁止隐式拷贝；release 后必须清零 | 纯 infra 私有对象 |
| RotationRequest | secret_name, requested_by, reason_code, strategy, validate_only | 不允许直接带新明文值穿越普通日志链路 | requested_by 复用 actor/request_id 语义 |
| RotationResult | secret_name, previous_version, current_version, validation_state, rollback_ready, evidence_ref | 结果必须可审计、可回放 | 通过 ResultCode/ErrorInfo 暴露成功失败，不进入 contracts |
| SecretAuditEvent | actor, action, target_secret, outcome, reason_code, version, evidence_ref | 必须脱敏，不记录 secret 明文 | 写入 infra audit；可引用 contracts 中 ErrorInfo/ResultCode |

### 6.6 核心接口语义定义

建议头文件分布：

1. infra/include/secret/ISecretManager.h
2. infra/include/secret/SecretTypes.h
3. infra/include/secret/ISecretBackend.h
4. infra/include/secret/SecretErrors.h

建议接口语义：

1. ISecretManager
   - get_secret(query, access_context): 返回 SecretHandle，不返回明文字节。
   - materialize(handle, access_context): 在权限与 lease 仍有效时返回 SecureBuffer。
   - release(handle_or_lease): 主动释放 materialized secret 并零化内存。
   - rotate(request): 发起轮换流程，返回 RotationResult。
   - revoke(secret_name, reason): 吊销指定 secret 当前版本或 lease。
   - inspect(secret_name): 返回 SecretDescriptor 与元数据，不返回密文或明文。

2. ISecretBackend
   - fetch_record(query): 获取 SecretDescriptor 与后端记录。
   - materialize_record(record, access_context): 受控解封为 SecureBuffer。
   - promote_version(rotation_stage): 切换轮换版本。
   - revoke_version(secret_name, version): 吊销版本。
   - get_backend_status(): 返回 backend 健康与限流状态。

3. ISecretHealthSource
   - sample_secret_health(): 返回 backend 可用性、缓存状态、过期/轮换积压。

前置条件：

1. query.secret_name 非空。
2. access_context 必须携带至少 request_id 或 task_id。
3. materialize 只能针对未过期且未撤销的 handle。

后置条件：

1. get_secret 成功后必须产生一条 audit 事件。
2. materialize 成功后必须绑定 lease，并在 release 或析构时零化明文内存。
3. rotate 成功后必须更新 rotation_epoch，并使旧 lease 根据策略进入过期或宽限状态。

错误语义（infra 私有码域，映射 contracts::ResultCode）：

1. INF_E_SECRET_NOT_FOUND：目标 secret 不存在。
2. INF_E_SECRET_ACCESS_DENIED：权限域、用途或 actor 不匹配。
3. INF_E_SECRET_BACKEND_UNAVAILABLE：backend 不可用或超时。
4. INF_E_SECRET_LEASE_EXPIRED：句柄或 lease 已过期。
5. INF_E_SECRET_VERSION_STALE：版本已被轮换代次替代。
6. INF_E_SECRET_MATERIALIZE_FAILED：解封或解密失败。
7. INF_E_SECRET_ROTATION_VALIDATION_FAILED：新版本验证失败。
8. INF_E_SECRET_ROTATION_ROLLBACK_FAILED：轮换失败后的回退失败。
9. INF_E_SECRET_AUDIT_WRITE_FAIL：审计写入失败。

### 6.7 主流程时序（正常）

1. 上游模块以 SecretQuery + SecretAccessContext 调用 ISecretManager.get_secret。
2. SecretManagerFacade 调用 SecretPolicyEvaluator 校验用途、权限域和 Profile 规则。
3. 通过校验后，SecretBackendAdapter 读取 SecretDescriptor，并生成不含明文的 SecretHandle。
4. SecretLeaseRegistry 创建或更新 lease，SecretAuditBridge 写入 access_granted 事件。
5. 若消费者需要使用明文，则调用 materialize(handle, access_context)。
6. SecretBackendAdapter 在受控范围内解封为 SecureBuffer，返回给调用方。
7. 调用方使用后显式 release；若未显式释放，则 SecureBuffer 析构时触发零化。
8. SecretHealthProbe 周期性采样 backend、lease、缓存与轮换状态。

### 6.8 异常与恢复时序

异常分类：

1. 权限类故障：actor、consumer_module、purpose 或 access_mode 不匹配。
2. backend 类故障：文件缺失、KMS 超时、限流、解密失败。
3. 生命周期故障：lease 过期、版本陈旧、轮换进行中。
4. 审计类故障：审计 sink 不可用、写入失败、导出失败。

恢复动作：

1. 权限拒绝：立即返回 INF_E_SECRET_ACCESS_DENIED，写拒绝审计，不重试。
2. backend 瞬时失败：在本模块内仅做有限次技术重试，超过阈值后返回明确错误码；是否继续业务流程由 runtime 裁定。
3. stale handle：返回 INF_E_SECRET_VERSION_STALE，要求调用方重新获取 handle。
4. 轮换验证失败：保留旧版本为 current，记录失败证据，进入 rollback_ready 或 rollback_failed 状态。
5. 审计写入失败：返回 INF_E_SECRET_AUDIT_WRITE_FAIL，并上报告警指标，不允许静默成功。

兜底策略：

1. 当 KMS/backend 不可用时，若 Profile 允许且存在有效本地只读缓存，可进入只读降级模式，但必须记录 degraded 审计事件。
2. 对高风险 secret 禁止使用降级缓存，直接失败快返。
3. 连续 N 次 backend 不可用后，HealthProbe 输出 degraded，交由 runtime/ops 处理，不在 secret 内自行切换业务路径。

### 6.8.1 dual-slot 验证与宽限窗口最小冻结

为解除 SecretRotationCoordinator 的实现阻塞，轮换链路在 v1 明确冻结以下最小 internal 语义：

1. `RotationRequest` 保持现有 public 形状不变，不追加 `candidate_version` 字段；`candidate_version` 由 coordinator 内部根据当前版本推导：若当前版本匹配 `v<整数>`，则递增数值；否则追加 `.candidate` 后缀。
2. coordinator 在进入 promote 前必须构造 internal `RotationValidationContext`，最小字段包含 `secret_name`、`previous_version`、`candidate_version`、`strategy`、`requested_by`、`reason_code`、`validation_required`、`dual_slot_enabled`、`grace_period_sec`。
3. internal `ISecretRotationValidator` 仅冻结一个最小入口：`validate_candidate(context)`；返回值至少包含 `accepted`、`reason_code`、`evidence_ref` 和 `rollback_ready`，用于支持 `validate_only`、失败快返和回退证据。
4. 当 `infra.secret.rotation.validation_required = true` 时，coordinator 必须先调用 validator，再决定是否 promote；当该配置为 `false` 时，允许跳过 validator，但仍必须生成显式 `validation_skipped` evidence_ref，不得静默跳过验证阶段。
5. 当 `request.strategy = DualSlot` 且 `infra.secret.rotation.dual_slot_enabled = false` 时，coordinator 必须返回 `INF_E_SECRET_ROTATION_VALIDATION_FAILED`，不得降级为 inplace promote。
6. 当 `request.strategy = DualSlot` 且 `infra.secret.rotation.grace_period_sec > 0` 时，promote 成功后旧版本进入 rollback-ready 宽限窗口，立即 revoke 被延后到宽限期后；当宽限窗口为 `0` 或策略不是 `DualSlot` 时，promote 后必须立即执行旧版本 revoke。
7. 若旧版本 revoke 或其后的收口步骤失败，coordinator 必须尝试 rollback，把 candidate 重新切回 previous；若 rollback 自身失败，则返回 `INF_E_SECRET_ROTATION_ROLLBACK_FAILED`，并把失败证据写入 `RotationResult.evidence_ref`。

### 6.9 配置项与默认策略

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| infra.secret.backend | file | 默认/Profile/部署 | file/kms/mock |
| infra.secret.cache.enabled | true | 默认/Profile | 是否启用句柄元数据缓存 |
| infra.secret.cache.ttl_sec | 300 | Profile/部署 | 仅缓存元数据和短期 materialized lease |
| infra.secret.cache.allow_readonly_fallback | false | Profile/部署 | backend 不可用时是否允许只读缓存 |
| infra.secret.audit.required | true | 默认/Profile | 审计不可关闭 |
| infra.secret.zeroize_on_release | true | 默认/Profile | 释放后零化内存 |
| infra.secret.rotation.enabled | true | 默认/Profile | 是否允许轮换 |
| infra.secret.rotation.dual_slot_enabled | true | Profile/部署 | 是否启用双槽平滑切换 |
| infra.secret.rotation.validation_required | true | 默认/Profile | promote 前必须验证新版本 |
| infra.secret.rotation.grace_period_sec | 600 | Profile/部署 | 旧 lease 宽限窗口 |
| infra.secret.file.root_dir | secrets/ | 部署 | 本地加密文件根目录 |
| infra.secret.file.encrypt_at_rest | true | 默认/Profile | file backend 是否启用静态加密 |
| infra.secret.kms.request_timeout_ms | 2000 | Profile/部署 | KMS 请求超时 |
| infra.secret.kms.max_retries | 2 | Profile/部署 | 技术重试次数 |
| infra.secret.policy.require_purpose | true | 默认/Profile | 访问必须显式声明用途 |

配置策略：

1. backend 类型和缓存策略由 Profile 与部署层裁剪。
2. 普通配置项仍由 ConfigCenter 管理，secret 仅存储敏感值和必要元数据引用。
3. 不允许通过运行时 patch 直接下发 secret 明文；运行时 patch 仅允许修改 backend 行为与策略参数。
4. `infra.secret.rotation.validation_required` 仅控制是否必须经过 `ISecretRotationValidator`；关闭验证时不能改变 `DualSlot` / rollback 语义。
5. `infra.secret.rotation.grace_period_sec` 只在 `DualSlot` promote 成功后生效，用于界定旧版本 revoke 的延后窗口；不得被 reinterpret 为普通 cache ttl。

### 6.10 可观测性设计

日志点：

1. secret 查询命中、未命中、权限拒绝、版本陈旧。
2. backend 超时、限流、解密失败、缓存 fallback 触发。
3. 轮换开始、验证成功、promote 成功、回退成功/失败。
4. release 超时未调用导致的析构零化与泄漏防护事件。

指标：

1. infra_secret_get_total。
2. infra_secret_access_denied_total。
3. infra_secret_backend_error_total。
4. infra_secret_cache_hit_ratio。
5. infra_secret_materialize_duration_ms。
6. infra_secret_rotation_total。
7. infra_secret_rotation_fail_total。
8. infra_secret_active_lease_count。
9. infra_secret_zeroize_fail_total。

追踪：

1. secret.get、secret.materialize、secret.rotate、secret.revoke 建立独立 span。
2. span 仅记录 secret_name hash、backend_type、result_code、lease_id，不记录 secret 明文。

审计：

1. get_secret、materialize、rotate、revoke、fallback、expired_access、access_denied 必须审计。
2. 审计字段必须包含 actor、action、target_secret、consumer_module、outcome、reason_code、request_id、evidence_ref。
3. 审计记录与普通日志分通道保存，满足架构文档“独立保存并可导出”的要求。

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 建立 SecretManager 对外稳定入口 | 新增 ISecretManager 与 SecretTypes | 先冻结接口与对象，解除专项 TODO 的模型阻塞 | infra/include/secret/ISecretManager.h; infra/include/secret/SecretTypes.h | unit: SecretInterfaceCompileTest; contract: SecretTypeBoundaryTest | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "SecretInterfaceCompileTest|SecretTypeBoundaryTest" | 阻塞：SecretHandle、RotationRequest、权限模型需先冻结 |
| 建立多 backend 统一抽象 | 新增 ISecretBackend 与 file/mock 适配器骨架 | 先打通 file/mock，KMS 保留接口位 | infra/include/secret/ISecretBackend.h; infra/src/secret/backends/* | unit: SecretBackendAdapterTest | ctest --test-dir build-ci -R SecretBackendAdapterTest | 阻塞：platform 文件/时间接口接线策略 |
| 建立句柄化访问与 lease 生命周期 | 新增 SecretLeaseRegistry 与 SecureBuffer | 解决明文暴露窗口与 release/zeroize 可验证性 | infra/src/secret/SecretLeaseRegistry.cpp; infra/src/secret/SecureBuffer.cpp | unit: SecretLeaseLifecycleTest; failure: SecretZeroizeTest | ctest --test-dir build-ci -R "SecretLeaseLifecycleTest|SecretZeroizeTest" | 依赖：对象模型冻结 |
| 建立轮换最小闭环 | 新增 SecretRotationCoordinator | 支撑 create/test/promote/revoke 四阶段流程 | infra/src/secret/SecretRotationCoordinator.cpp | unit: SecretRotationCoordinatorTest; integration: SecretRotationWorkflowTest | ctest --test-dir build-ci -R "SecretRotationCoordinatorTest|SecretRotationWorkflowTest" | 阻塞：双槽策略与验证规则需冻结 |
| 建立审计与健康出口 | 新增 SecretAuditBridge 与 SecretHealthProbe | 把明文不落盘、失败可观测落为自动化门禁 | infra/src/secret/SecretAuditBridge.cpp; infra/src/secret/SecretHealthProbe.cpp | unit: SecretAuditBridgeTest; integration: SecretHealthProbeTest | ctest --test-dir build-ci -R "SecretAuditBridgeTest|SecretHealthProbeTest" | 依赖：IAuditLogger/IHealthMonitor 注册点 |
| 建立 infra/secret 测试基线 | 新增 tests/unit/infra/secret + tests/integration/infra/secret | 把设计约束转为可重复执行的门禁 | tests/unit/infra/secret/*; tests/integration/infra/secret/*; tests/contract/infra/* | unit/contract/integration/failure injection | ctest --test-dir build-ci -L "unit|contract|integration" | 阻塞：tests/integration 顶层注册策略 |

不可立即映射项：

1. 远程 KMS 真实 SDK 接入：当前外部身份、限流与测试夹具未冻结，列为 v2 演进项。
2. 硬件安全模块或机密计算：与当前工程阶段不匹配，不纳入本轮最小交付。

---

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

建议目录：

1. infra/include/secret/ISecretManager.h
2. infra/include/secret/SecretTypes.h
3. infra/include/secret/ISecretBackend.h
4. infra/include/secret/SecretErrors.h
5. infra/src/secret/SecretManagerFacade.cpp
6. infra/src/secret/SecretPolicyEvaluator.cpp
7. infra/src/secret/SecretCache.cpp
8. infra/src/secret/SecretLeaseRegistry.cpp
9. infra/src/secret/SecretRotationCoordinator.cpp
10. infra/src/secret/SecretAuditBridge.cpp
11. infra/src/secret/SecretHealthProbe.cpp
12. infra/src/secret/backends/FileSecretBackend.cpp
13. infra/src/secret/backends/MockSecretBackend.cpp
14. tests/unit/infra/secret/
15. tests/contract/infra/
16. tests/integration/infra/secret/

### 8.2 分阶段实施与完成判定

| 阶段 | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| SEC-M1 对象与接口冻结 | Not Started | 冻结 SecretQuery、SecretHandle、SecretLease、RotationRequest、ISecretManager、ISecretBackend | 本文 6.5、6.6；专项 TODO INF-BLK-02 | infra/include/secret/* | unit: SecretInterfaceCompileTest; contract: SecretTypeBoundaryTest | cmake --build build-ci --target dasall_infra | 对象字段、接口签名与错误码域固定，能编译 |
| SEC-M2 file/mock 最小闭环 | Not Started | 新增 FileSecretBackend/MockSecretBackend 与 SecretManagerFacade 最小实现 | 本文 6.2、6.3、6.7 | infra/src/secret/*; infra/src/secret/backends/* | unit: SecretBackendAdapterTest; SecretLeaseLifecycleTest | ctest --test-dir build-ci -R "SecretBackendAdapterTest|SecretLeaseLifecycleTest" | get/materialize/release 能跑通，明文不落盘 |
| SEC-M3 轮换与审计闭环 | Not Started | 新增 SecretRotationCoordinator 与 SecretAuditBridge | 本文 6.8、6.10 | infra/src/secret/SecretRotationCoordinator.cpp; SecretAuditBridge.cpp | unit: SecretRotationCoordinatorTest; SecretAuditBridgeTest | ctest --test-dir build-ci -R "SecretRotationCoordinatorTest|SecretAuditBridgeTest" | 轮换失败可回退，审计失败可观测 |
| SEC-M4 健康与集成门禁 | Not Started | 新增 SecretHealthProbe 与集成测试接线 | 本文 6.10；测试矩阵 | infra/src/secret/SecretHealthProbe.cpp; tests/integration/infra/secret/* | integration: SecretHealthProbeTest; SecretRotationWorkflowTest | ctest --test-dir build-ci -R "SecretHealthProbeTest|SecretRotationWorkflowTest" | degraded 状态与轮换路径可重复验证 |
| SEC-M5 KMS 适配预留 | Not Started | 冻结 KmsSecretBackend 接口占位，不接入真实 SDK | 本文 4、6.2、9 | infra/include/secret/ISecretBackend.h | unit: KmsBackendContractSmokeTest | ctest --test-dir build-ci -R KmsBackendContractSmokeTest | 保持接口兼容，不引入 breaking 变更 |

### 8.3 原子实施任务（建议级）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| SEC-T001 | Not Started | 新增 SecretTypes 头文件并冻结 SecretQuery/SecretHandle/SecretLease | 本文 6.5 | infra/include/secret/SecretTypes.h | SecretTypeBoundaryTest | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R SecretTypeBoundaryTest | 对象字段与约束完整、无明文字段 |
| SEC-T002 | Not Started | 新增 ISecretManager 接口骨架 | 本文 6.6 | infra/include/secret/ISecretManager.h | SecretInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 方法签名、错误语义与对象依赖一致 |
| SEC-T003 | Not Started | 新增 ISecretBackend 统一后端接口 | 本文 6.2、6.6 | infra/include/secret/ISecretBackend.h | SecretBackendContractSmokeTest | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R SecretBackendContractSmokeTest | file/mock/kms 可共享同一协议 |
| SEC-T004 | Not Started | 新增 SecretManagerFacade 最小骨架并替换 placeholder-only 接线 | 本文 6.2、6.7 | infra/src/secret/SecretManagerFacade.cpp; infra/CMakeLists.txt | SecretInterfaceCompileTest | cmake --build build-ci --target dasall_infra | infra 不再只有 placeholder 作为唯一入口 |
| SEC-T005 | Not Started | 新增 MockSecretBackend 与固定夹具 | 本文 6.2、6.3 | infra/src/secret/backends/MockSecretBackend.cpp | SecretBackendAdapterTest | ctest --test-dir build-ci -R SecretBackendAdapterTest | 支持成功、未命中、拒绝、backend down 四类路径 |
| SEC-T006 | Not Started | 新增 FileSecretBackend 最小实现 | 本文 6.2、6.9 | infra/src/secret/backends/FileSecretBackend.cpp | FileSecretBackendTest | ctest --test-dir build-ci -R FileSecretBackendTest | 仅输出受控缓冲，不写明文临时文件 |
| SEC-T007 | Not Started | 新增 SecureBuffer 与 release/zeroize 逻辑 | 本文 6.5、6.8 | infra/src/secret/SecureBuffer.cpp | SecretZeroizeTest | ctest --test-dir build-ci -R SecretZeroizeTest | release 后缓冲被零化且不可继续访问 |
| SEC-T008 | Not Started | 新增 SecretLeaseRegistry 生命周期管理 | 本文 6.3、6.7 | infra/src/secret/SecretLeaseRegistry.cpp | SecretLeaseLifecycleTest | ctest --test-dir build-ci -R SecretLeaseLifecycleTest | lease 创建、过期、回收路径可判定 |
| SEC-T009 | Not Started | 新增 SecretRotationCoordinator 轮换状态机 | 本文 6.8 | infra/src/secret/SecretRotationCoordinator.cpp | SecretRotationCoordinatorTest | ctest --test-dir build-ci -R SecretRotationCoordinatorTest | create/test/promote/revoke 四阶段断言全绿 |
| SEC-T010 | Not Started | 新增 SecretAuditBridge 并接入审计事件 | 本文 6.10 | infra/src/secret/SecretAuditBridge.cpp | SecretAuditBridgeTest | ctest --test-dir build-ci -R SecretAuditBridgeTest | access/deny/rotate/revoke 事件均被记录 |
| SEC-T011 | Not Started | 新增 SecretHealthProbe 与健康指标出口 | 本文 6.10 | infra/src/secret/SecretHealthProbe.cpp | SecretHealthProbeTest | ctest --test-dir build-ci -R SecretHealthProbeTest | backend down、rotation backlog、cache stale 可转为健康状态 |
| SEC-T012 | Not Started | 新增 infra/secret 单元与契约测试注册点 | 本文 7、9 | tests/unit/infra/secret/*; tests/contract/infra/* | unit + contract | ctest --test-dir build-ci -L "unit|contract" | 新增测试能被 ctest 发现并执行 |
| SEC-T013 | Not Started | 新增 infra/secret 集成与故障注入测试 | 本文 6.8、9 | tests/integration/infra/secret/* | integration + failure injection | ctest --test-dir build-ci -R "SecretRotationWorkflowTest|SecretFailureInjectionTest" | 至少覆盖 backend down、stale handle、audit fail 三类故障 |

---

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试层 | 覆盖对象 | 关键用例 | 通过标准 |
|---|---|---|---|
| Unit | SecretTypes、ISecretManager、ISecretBackend、SecureBuffer、LeaseRegistry、RotationCoordinator | 句柄不含明文、release 零化、lease 过期、轮换四阶段、拒绝访问 | 断言全部通过，错误码与状态二值可判定 |
| Contract | secret 与 contracts 边界 | ErrorInfo/ResultCode 映射稳定；request_id/task_id/lease_id 仅复用已冻结语义；不引入 backend 字段到 contracts | 无越权字段、无 breaking 语义漂移 |
| Integration | SecretManagerFacade + file/mock backend + audit/health | get/materialize/release 链路、轮换后 handle 失效、degraded 健康上报 | 关键路径可重复执行 |
| Failure Injection | backend down、decrypt fail、audit fail、stale handle、rotation validation fail | 快返错误码、fallback 控制、回退与告警 | 每类故障都有证据、指标和兜底动作 |
| Compatibility | file/mock/kms 协议一致性、Profile 差异 | desktop_full/edge_balanced/edge_minimal 的 backend/caching 行为一致性 | 不出现接口 breaking |

### 9.2 质量 Gate 建议

| Gate ID | 检查项 | 失败判定 |
|---|---|---|
| SEC-G1 | secret 单元测试全绿 | 任一 unit 失败即阻断 |
| SEC-G2 | contracts 边界检查通过 | 出现 backend 细节泄漏到 contracts 即阻断 |
| SEC-G3 | 明文不落盘检查通过 | 任一测试发现 secret 明文写入日志、trace、临时文件或普通对象序列化即阻断 |
| SEC-G4 | 轮换回退测试全绿 | promote 或 rollback 任一路径不可判定即阻断 |
| SEC-G5 | 审计完整性检查通过 | access/deny/rotate/revoke 任一事件缺失即阻断 |
| SEC-G6 | Profile 兼容检查通过 | file/mock/kms 协议或配置行为出现 breaking 即阻断 |

---

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | runtime、tools、services 等通过 ISecretManager 访问 secret 的消费者 | 先引入 ISecretManager + SecretHandle，不要求立即替换所有配置读取路径；旧路径逐步迁移到 facade | 先在 mock/file backend 验证，再按 Profile 引入真实 backend | 预留 kms backend、dynamic secret、双槽轮换、事件通知 |
| Medium（若调整对象字段） | 所有包含 SecretHandle/RotationRequest 的调用点 | 采用可选字段追加和新接口重载，不删除旧字段 | 先 desktop_full，再 edge_balanced，最后 edge_minimal | 预留 descriptor tags、owner metadata、policy version |

演进原则：

1. 默认向后兼容，新增字段优先 optional。
2. ISecretBackend 的新能力通过 capability flags 扩展，避免修改核心查询语义。
3. 真实 KMS 接入优先通过新 backend 实现落地，而不是修改上层 ISecretManager 语义。

---

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| B-SEC-01 SecretHandle/SecretLease/RotationRequest 未冻结 | SEC-T001、SEC-T002、SEC-T008、SEC-T009 | 本文 6.5 对象模型评审通过 | 先冻结字段与不变量，不进入复杂实现 | 保留接口草案，禁止进入 backend 实现 |
| B-SEC-02 权限控制对象未冻结 | SEC-T002、SEC-T004 | 明确 SecretAccessContext 与 purpose/permission_domain 规则 | 先以最小字段模型落盘 | 仅支持 allowlist 形式的最小校验，不开放高级策略 |
| B-SEC-03 file backend 加密与根目录规范未统一 | SEC-T006 | 明确部署层目录与静态加密策略 | 先用 mock backend 完成大部分测试，file 只做受控最小实现 | 暂停 file backend 上线，仅保留 mock |
| B-SEC-04 审计通道接入策略未统一 | SEC-T010 | IAuditLogger 与 audit sink 注册点可用 | 先使用 mock audit logger 打通断言 | 审计桥保留事件缓存，但不宣称生产就绪 |
| B-SEC-05 tests/integration 顶层注册未接好 | SEC-T013、SEC-M4 | tests/CMakeLists.txt 明确 integration 接入方式 | 先完成 unit + contract gate | 集成 gate 延后到下一迭代 |
| B-SEC-06 远程 KMS 身份与限流策略未冻结 | SEC-M5 | 明确 service identity、timeout、retry、quota 策略 | 仅冻结 KMS backend 接口占位 | 禁止真实 KMS SDK 接入 |

### 11.2 风险清单

| 风险 | 等级 | 触发条件 | 缓解动作 |
|---|---|---|---|
| 明文泄漏到日志或崩溃转储 | High | 开发者直接把 SecureBuffer 转成 string 或记录原始值 | 类型层禁止隐式拷贝，增加明文不落盘测试与代码评审清单 |
| 权限模型过粗导致横向越权 | High | 仅按 secret_name 授权，不校验用途和 consumer_module | 引入 SecretAccessContext 与 purpose 校验，审计 actor/consumer |
| 轮换打断在线消费者 | Medium | promote 后旧 lease 立即失效 | 采用 dual-slot + grace period，先验证后 promote |
| backend 限流或抖动导致频繁失败 | Medium | KMS/file backend 不稳定 | 增加元数据缓存、短期 lease 缓存、显式限流与 health degraded |
| contracts 污染 | High | 将 SecretHandle/backend_type 写入共享契约 | 通过 contract 测试和 review checklist 防止越权 |

### 11.3 回退策略

1. 若 M2 未完成：保留 mock backend 作为唯一可测试实现，不开放 file backend。
2. 若轮换闭环不稳定：禁用 rotate/apply，仅保留 inspect/get/materialize/release 路径。
3. 若审计通道未就绪：禁止宣称生产可用，所有 secret 访问默认失败快返或仅限测试 Profile。
4. 若 KMS 适配阻塞：保持 ISecretBackend 协议稳定，延后真实远程实现，不修改上层 facade。

---

## 12. 未决问题与后续任务

### 12.1 未决问题

1. 生产后端优先采用本地加密文件、外部 KMS 还是两者混合部署。
2. SecretAccessContext 中 permission_domain 的最小字段集合是否需要与平台身份系统对齐。
3. file backend 的静态加密密钥由本地保护、TPM、还是外部引导链提供。
4. dual-slot rotation 的验证动作由 secret 模块提供通用 hook，还是由上层消费者注册验证器。
5. KMS 场景的缓存 TTL 与限流策略是否需要按 secret classification 分级。

### 12.2 后续任务建议

1. 在 docs/todos 下新增 infra/secret 专项 TODO，将 SEC-T001 至 SEC-T013 作为独立任务串接到 infrastructure 专项 TODO。
2. 优先推进 SEC-M1，解除当前 ISecretManager Blocked 状态。
3. 在 tests/contract 中增加 secret 对 contracts 边界守卫，防止 backend 字段误入共享对象。
4. 在 edge_balanced 与 edge_minimal Profile 下做一轮 file/mock backend 资源预算压测，验证缓存和轮换参数。
