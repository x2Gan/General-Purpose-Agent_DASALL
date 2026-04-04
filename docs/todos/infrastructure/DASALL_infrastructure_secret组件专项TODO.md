# DASALL infrastructure 子系统 secret 组件专项 TODO

最近更新时间：2026-04-04  
阶段：Detailed Design -> Special TODO  
适用范围：infra/secret

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_infra_secret模块详细设计.md
2. docs/architecture/DASALL_infrastructure子系统详细设计.md
3. docs/architecture/DASSALL_Agent_architecture.md
4. docs/architecture/DASALL_Engineering_Blueprint.md
5. docs/adr/ADR-005-architecture-review-baseline.md
6. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
7. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
8. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
9. docs/plans/DASALL_工程落地实现步骤指引.md
10. docs/development/DASALL_工程协作与编码规范.md
11. docs/todos/contracts/
12. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md
13. docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md
14. docs/todos/infrastructure/DASALL_infrastructure_tracing组件专项TODO.md
15. docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md
16. docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md
17. 当前代码现状：infra/CMakeLists.txt、infra/include/、infra/src/{InfraServiceFacade.cpp,InfraErrorCode.cpp,audit/,plugin/,tracing/}、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt

生成原则：

1. 不改写已冻结 ADR 结论。
2. 不越过 infrastructure/secret 组件边界。
3. 不把讨论事项伪装为 Build-ready 实现任务。
4. 每项任务必须具备代码目标、测试目标、验收命令三件套。
5. 设计证据不足处只输出 Blocked 与补设计前置任务。

## 2. 子系统目标与范围

### 2.1 组件目标

1. 提供统一 SecretManager 抽象，屏蔽 file/kms/mock 后端差异。
2. 保证凭证与敏感配置分离存储，满足明文不落盘与最小权限访问。
3. 提供 get/materialize/release/rotate/revoke/inspect 的最小闭环。
4. 支持 Profile 裁剪 backend、缓存和轮换策略，不改变上层调用语义。

### 2.2 范围边界

纳入范围：

1. secret 对外接口、对象模型、错误码、主流程与异常流程。
2. backend 统一抽象、lease 生命周期、轮换协调、审计与健康出口。
3. secret 组件 CMake 接线、unit/contract/integration 注册建议与门禁。
4. secret 配置项（backend/cache/rotation/audit/zeroize）与 Profile 裁剪约束。

不纳入范围：

1. runtime 主控裁定、恢复策略准入与业务审批决策。
2. context 装配、prompt 渲染、认知推理语义。
3. contracts 共享对象扩写（backend/file/kms 细节不得进入 contracts）。
4. 远程 KMS 真实 SDK 绑定与外部身份体系定版（作为后续演进）。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 secret TODO 的影响 |
|---|---|---|---|---|
| SEC-TC001 | secret 设计 1.1；架构 5.10 | Must | secret 属于 Layer 1，提供统一密钥治理入口 | 任务必须聚焦基础能力，不承载业务决策 |
| SEC-TC002 | 架构 8.8 | Must | 凭证与敏感配置分离存储，审计独立保存 | 必须拆分 secret 访问与审计任务 |
| SEC-TC003 | 蓝图 4.1/4.2 | Must | infra 单向依赖，不反向依赖业务模块实现 | 代码目标限制在 infra/tests/docs/cmake |
| SEC-TC004 | 蓝图 4.3 | Must | 跨模块调用走冻结接口，不直接依赖实现类 | 接口冻结任务必须先行 |
| SEC-TC005 | 蓝图 5.1 | Must | Profile 只能裁剪 backend/cache/rotation，不得绕过审计链路 | 配置任务必须含审计强制与覆盖策略 |
| SEC-TC006 | ADR-005 | Must | 不改写已冻结边界与 contracts 先行原则 | 缺证据项必须 Blocked |
| SEC-TC007 | ADR-006 | Must-Not | secret 不参与上下文装配和 prompt 渲染 | 禁止将语义处理逻辑写入 secret |
| SEC-TC008 | ADR-007 | Must-Not | secret 不做失败语义判定与恢复准入裁定 | 仅返回可判定错误码与观测事件 |
| SEC-TC009 | ADR-008 | Must-Not | secret 不拥有主控调度权和最终输出权 | 禁止新增 orchestrator 行为 |
| SEC-TC010 | contracts 冻结计划与 TODO 总表 | Must | 只消费 contracts 横切 ID/错误语义，不反写 backend 细节 | 对象模型保持 infra 私有 |
| SEC-TC011 | 编码规范 3.6 | Must | 错误不可吞没，失败必须可观测 | 错误路径任务必须带日志/指标/审计目标 |
| SEC-TC012 | 编码规范 3.7 | Should | 新增公共接口应同步 unit 或 contract 测试 | 每个接口任务必须带测试出口 |
| SEC-TC013 | 落地步骤指引 阶段 C | Must | 先底座后能力，每阶段必须可测试 | 顺序先接口对象，再实现，再门禁 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/CMakeLists.txt | 已接入 core/audit/plugin/tracing 等真实源码 | secret 公共接口已落盘，但 secret 具体实现尚未接入构建 |
| infra/include/ | 已形成“根目录共享契约 + 组件目录公共接口”布局，secret/ 子目录已落盘接口与对象 | secret public headers 已冻结，后续差距集中在实现骨架与 backend 适配器 |
| infra/src/secret/ | 当前不存在或未落盘实现 | secret 子组件实现缺失 |
| tests/CMakeLists.txt | 已接入 mocks/unit/contract/integration 并提供 dasall_integration_tests 聚合入口 | integration 拓扑已接入顶层，后续只需补 secret 具体集成用例 |
| tests/unit/CMakeLists.txt | 已接入 infra 子目录 | secret unit 发现性已建立，后续只需补具体用例 |
| tests/contract/CMakeLists.txt | centralized registration 存在 | 可承载 secret 边界 contract |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：可直接生成并执行 L2 为主、局部 L3 的专项 TODO；接口/对象/核心流程可细化到函数骨架级，真实 KMS 绑定与 integration 全量验收需前置解阻。

支撑证据：

1. 已有明确核心接口：ISecretManager、ISecretBackend、ISecretHealthSource。
2. 已有核心对象字段：SecretQuery、SecretAccessContext、SecretDescriptor、SecretHandle、SecretLease、SecureBuffer、RotationRequest、RotationResult、SecretAuditEvent。
3. 已有主流程与异常流程：get/materialize/release/rotate/revoke/inspect 正常链路和权限/backend/生命周期/审计故障路径。
4. 已有错误语义：INF_E_SECRET_NOT_FOUND、INF_E_SECRET_ACCESS_DENIED、INF_E_SECRET_BACKEND_UNAVAILABLE、INF_E_SECRET_LEASE_EXPIRED、INF_E_SECRET_VERSION_STALE、INF_E_SECRET_MATERIALIZE_FAILED、INF_E_SECRET_ROTATION_VALIDATION_FAILED、INF_E_SECRET_ROTATION_ROLLBACK_FAILED、INF_E_SECRET_AUDIT_WRITE_FAIL。
5. 已有落盘建议：infra/include/secret、infra/src/secret、tests/unit/infra/secret、tests/contract/infra、tests/integration/infra/secret。
6. 缺失点：KMS 身份与限流策略、integration 顶层接线、审计/健康统一注册点细节。

当前最小可执行粒度：接口/数据结构级（L2），局部函数/方法骨架（L3）。

### 4.2 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| ISecretManager | secret 设计 6.6/6.7 | L3 | 方法名、前后置条件、错误语义明确 | inspect 返回对象过滤细节需约束 | 直接拆接口冻结与编译验证 |
| ISecretBackend | secret 设计 6.6 | L2 | fetch/materialize/promote/revoke/status 语义明确 | KMS 特定协议未冻结 | 先落统一抽象与 file/mock 骨架 |
| SecretTypes 对象族 | secret 设计 6.5 | L3 | 字段与不变量明确 | 少数字段类型别名未成文 | 直接拆数据结构任务 |
| SecureBuffer | secret 设计 6.5/6.7/6.8 | L3 | zeroize_on_release、禁隐式拷贝语义明确 | 平台零化实现策略细节未定 | 直接拆 release/zeroize 原子任务 |
| SecretLeaseRegistry | secret 设计 6.2/6.3/6.7 | L3 | lease 生命周期与过期语义明确 | 与轮换宽限窗口交互细节需验证 | 直接拆生命周期任务 |
| SecretRotationCoordinator | secret 设计 6.2/6.8/6.9 | L2 | create/test/promote/revoke 与回退语义明确，且 6.8.1 已冻结最小验证器接口、candidate_version 推导与 grace period 规则 | 无 | 直接拆轮换状态机骨架 |
| SecretAuditBridge | secret 设计 6.2/6.10 | L2 | 审计事件集合明确 | IAuditLogger 注册点细节未冻结 | 先落桥接骨架，注册点为前置依赖 |
| SecretHealthProbe | secret 设计 6.2/6.10 | L2 | 健康指标与 degraded 信号明确 | IHealthMonitor 接口对接细节未冻结 | 先落 secret 私有健康快照出口 |
| KmsSecretBackend | secret 设计 6.2/8.2 | L1 | 仅有占位职责和演进方向 | 身份、限流、超时、测试夹具缺失 | 标记 Blocked，先补设计 |
| tests/integration 注册点 | secret 设计 8.1/9；tests 现状 | L0 | 设计建议存在，且 tests 顶层 integration 拓扑已接入 | secret integration 用例尚未落盘 | 直接拆 integration 注册任务 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| SecretManager 对外稳定入口 | secret 设计 6.6/7 | 接口 | SEC-TODO-001、SEC-TODO-002 | 先冻结调用面，解除对象模型阻塞 |
| Secret 对象模型冻结 | secret 设计 6.5 | 数据结构 | SEC-TODO-003、SEC-TODO-004 | 先固化字段与不变量，防止实现漂移 |
| backend 统一抽象 | secret 设计 6.2/6.6/7 | 适配器 | SEC-TODO-005、SEC-TODO-006、SEC-TODO-007（SEC-BLK-001 已于 2026-04-03 解阻） | 抽象与 file/mock 分拆，保持单目标 |
| get/materialize/release 生命周期 | secret 设计 6.7 | 生命周期/流程 | SEC-TODO-008、SEC-TODO-009 | 将访问链与零化链拆分验收 |
| 轮换闭环与回退 | secret 设计 6.8/6.9/7 | 流程/异常处理 | SEC-TODO-010（SEC-BLK-002 已于 2026-04-04 解阻） | 轮换状态机单独任务 |
| 错误语义映射 | secret 设计 6.6 | 错误处理 | SEC-TODO-011 | 错误码域与 contracts 映射单列 |
| 审计与健康出口 | secret 设计 6.10/7 | 适配器/门禁 | SEC-TODO-012、SEC-TODO-013 | 审计与健康拆分，避免任务过大 |
| CMake 与测试注册 | secret 设计 8.1/9.1 | 测试/门禁 | SEC-TODO-014、SEC-TODO-015、SEC-TODO-016 | 构建接线、unit/contract、integration 解阻 |
| 文档与交付证据回写 | secret 设计 9.2/11 | 文档/门禁 | SEC-TODO-017 | gate 证据和阻塞状态回写 |
| KMS 真实接入 | secret 设计 4/8.2/11 | 配置/流程 | SEC-BLK-003 | 缺测试夹具和身份策略，先阻塞 |

### 5.2 覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | SEC-TODO-001、SEC-TODO-002、SEC-TODO-005 |
| 数据结构定义类任务 | 是 | SEC-TODO-003、SEC-TODO-004 |
| 生命周期与初始化类任务 | 是 | SEC-TODO-008、SEC-TODO-009、SEC-TODO-010 |
| 适配器/桥接类任务 | 是 | SEC-TODO-006、SEC-TODO-007、SEC-TODO-012、SEC-TODO-013 |
| 异常与错误处理类任务 | 是 | SEC-TODO-010、SEC-TODO-011 |
| 配置与 Profile 裁剪类任务 | 是 | SEC-TODO-007、SEC-TODO-010、SEC-BLK-003 |
| 测试与门禁类任务 | 是 | SEC-TODO-014、SEC-TODO-015、SEC-TODO-016 |
| 文档/交付证据回写类任务 | 是 | SEC-TODO-017 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SEC-TODO-001 | Completed | 定义 ISecretManager 接口头文件 | secret 设计 6.6；编码规范 3.7 | 6.6 ISecretManager | L3 | infra/include/secret/ISecretManager.h | get_secret, materialize, release, rotate, revoke, inspect | unit：接口可编译；contract：边界语义不越权 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | 无 | 无 | 接口头文件、编译记录 | 仅当 6 个方法与锚点一致且编译通过时完成 |
| SEC-TODO-002 | Completed | 定义 ISecretHealthSource 接口头文件 | secret 设计 6.6 | 6.6 ISecretHealthSource | L2 | infra/include/secret/ISecretHealthSource.h | sample_secret_health | unit：接口可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | 无 | 无 | 接口头文件、编译记录 | 仅当健康采样接口落盘且可编译时完成 |
| SEC-TODO-003 | Completed | 定义 SecretTypes 对象模型 | secret 设计 6.5 | 6.5 核心对象表 | L3 | infra/include/secret/SecretTypes.h | SecretQuery, SecretAccessContext, SecretDescriptor, SecretHandle, SecretLease, RotationRequest, RotationResult, SecretAuditEvent | unit：字段完整性；contract：不写入 contracts 共享对象 | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -L unit | 无 | 无 | 无 | 对象头文件、单测 | 仅当对象字段与 6.5 对齐且无明文字段时完成 |
| SEC-TODO-004 | Completed | 定义 SecureBuffer 语义与约束 | secret 设计 6.5/6.7 | 6.5 SecureBuffer；6.7 release 语义 | L3 | infra/include/secret/SecureBuffer.h | zeroize_on_release, 禁止隐式拷贝约束 | unit：零化与访问失效断言 | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -L unit | SEC-TODO-003 | 平台零化实现策略未定 | 先冻结接口和语义，不绑定平台实现 | 头文件、单测 | 仅当 release 后不可访问且零化断言通过时完成 |
| SEC-TODO-005 | Completed | 定义 ISecretBackend 统一协议 | secret 设计 6.6/7 | 6.6 ISecretBackend | L2 | infra/include/secret/ISecretBackend.h | fetch_record, materialize_record, promote_version, revoke_version, get_backend_status | unit：接口可编译；contract：协议稳定性检查 | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -L contract | SEC-TODO-003 | KMS 协议细节未冻结 | 先冻结公共协议，不落 KMS 细节 | 接口头文件、编译/合同检查 | 仅当 file/mock 可共享同协议且编译通过时完成 |
| SEC-TODO-006 | Completed | 实现 MockSecretBackend 骨架 | secret 设计 6.2/8.3 | 6.2 MockSecretBackend | L3 | infra/src/secret/backends/MockSecretBackend.cpp | fetch_record/materialize_record/revoke_version 最小实现 | unit：成功/未命中/拒绝/backend down 四路径 | ctest --test-dir build-ci -L unit | SEC-TODO-005 | 无 | 无 | Mock 后端实现、单测、交付件 | 仅当四类路径可二值判定时完成 |
| SEC-TODO-007 | Completed | 实现 FileSecretBackend 最小骨架 | secret 设计 6.2/6.9 | 6.2 FileSecretBackend；6.9 file 配置项 | L2 | infra/src/secret/backends/FileSecretBackend.cpp | fetch_record/materialize_record 最小实现 | unit：本地路径读取与错误路径；failure：backend unavailable | ctest --test-dir build-ci -L unit | SEC-TODO-005 | 无（2026-04-03 已由 SEC-BLK-001 通过 secret 设计 6.9 的 file.root_dir/encrypt_at_rest 策略冻结解阻） | 无 | File 后端骨架、测试、交付件 | 仅当不写明文临时文件且错误路径可判定时完成 |
| SEC-TODO-008 | Completed | 实现 SecretManagerFacade 访问骨架 | secret 设计 6.2/6.7 | 6.7 正常流程 1-4 步 | L3 | infra/src/secret/SecretManagerFacade.cpp | get_secret, materialize, release, inspect | unit：访问链路可走通；contract：上下文字段复用边界 | ctest --test-dir build-ci -L unit && ctest --test-dir build-ci -L contract | SEC-TODO-001、SEC-TODO-003、SEC-TODO-005、SEC-TODO-006 | 无 | 无 | Facade 骨架、测试、交付件 | 仅当 get->materialize->release 路径可稳定验证时完成 |
| SEC-TODO-009 | Completed | 实现 SecretLeaseRegistry 生命周期管理 | secret 设计 6.2/6.3/6.7 | 6.7 lease 创建/过期 | L3 | infra/src/secret/SecretLeaseRegistry.cpp | create_lease, validate_lease, expire_lease, release_lease | unit：创建/过期/释放/陈旧句柄 | ctest --test-dir build-ci -L unit | SEC-TODO-003、SEC-TODO-008 | 无 | 无 | Lease 注册实现、单测、交付件 | 仅当过期后 materialize 被拒绝并返回明确错误码时完成 |
| SEC-TODO-010 | Not Started | 实现 SecretRotationCoordinator 轮换骨架 | secret 设计 6.2/6.8/6.9 | 6.8 轮换与回退 | L2 | infra/src/secret/SecretRotationCoordinator.cpp | rotate(request), promote_version, revoke_version, rollback | unit：验证失败回退；failure injection：rollback fail 路径 | ctest --test-dir build-ci -L unit | SEC-TODO-003、SEC-TODO-005、SEC-TODO-009 | 无（2026-04-04 已由 SEC-BLK-002 通过 secret 设计 6.8.1 / 6.9 的 dual-slot validator / grace period 语义冻结解阻） | 无 | 轮换骨架、测试 | 仅当 create/test/promote/revoke 路径与回退路径可判定时完成 |
| SEC-TODO-011 | Completed | 定义 SecretErrors 错误码域与映射 | secret 设计 6.6；编码规范 3.6 | 6.6 错误语义 | L3 | infra/include/secret/SecretErrors.h | INF_E_SECRET_NOT_FOUND, INF_E_SECRET_ACCESS_DENIED, INF_E_SECRET_BACKEND_UNAVAILABLE, INF_E_SECRET_LEASE_EXPIRED, INF_E_SECRET_VERSION_STALE, INF_E_SECRET_MATERIALIZE_FAILED, INF_E_SECRET_ROTATION_VALIDATION_FAILED, INF_E_SECRET_ROTATION_ROLLBACK_FAILED, INF_E_SECRET_AUDIT_WRITE_FAIL | contract：映射 contracts::ResultCode；unit：枚举稳定性 | ctest --test-dir build-ci -L contract | SEC-TODO-001 | 映射矩阵未成文 | 在 contract 测试中固化映射矩阵 | 错误码头文件、映射测试 | 仅当 9 个错误码都可追溯且映射测试通过时完成 |
| SEC-TODO-012 | Not Started | 实现 SecretAuditBridge 审计桥骨架 | secret 设计 6.2/6.10 | 6.10 审计事件清单 | L2 | infra/src/secret/SecretAuditBridge.cpp | emit_access_granted, emit_access_denied, emit_rotate, emit_revoke, emit_fallback | unit：事件完整性；failure：audit write fail 路径 | ctest --test-dir build-ci -L unit | SEC-TODO-003、SEC-TODO-011 | SEC-BLK-004 | 冻结 IAuditLogger 注册点与事件字段映射 | 审计桥骨架、测试 | 仅当关键事件不丢失且失败路径返回明确错误码时完成 |
| SEC-TODO-013 | Not Started | 实现 SecretHealthProbe 健康出口骨架 | secret 设计 6.2/6.10 | 6.10 健康指标与 degraded | L2 | infra/src/secret/SecretHealthProbe.cpp | sample_secret_health | unit：backend down、rotation backlog、cache stale 三路径 | ctest --test-dir build-ci -L unit | SEC-TODO-002、SEC-TODO-009、SEC-TODO-010 | 无 | 无 | 健康探针骨架、单测 | 仅当三类风险均可映射到健康状态并可重复验证时完成 |
| SEC-TODO-014 | Not Started | 接线 infra/secret 到 CMake | secret 设计 8.1；代码现状 | 8.1 落盘建议 | L2 | infra/CMakeLists.txt、infra/include/secret/、infra/src/secret/ | 注册 secret 源文件与头文件入口 | build：dasall_infra 编译通过 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | SEC-TODO-001~SEC-TODO-013 | 无 | 无 | CMake 改动、构建记录 | 仅当 placeholder 不再是唯一功能入口且 secret 文件入图时完成 |
| SEC-TODO-015 | Not Started | 注册 secret unit 与 contract 测试入口 | secret 设计 8.1/9.1；编码规范 3.7 | 9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt、tests/unit/infra/secret/、tests/contract/CMakeLists.txt | unit：类型、接口、访问、lease、轮换、审计、健康；contract：边界与错误映射 | cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | SEC-TODO-014 | 无 | 无 | 测试代码、注册入口、执行记录 | 仅当新增测试在 ctest -N 可见并执行通过时完成 |
| SEC-TODO-016 | Not Started | 注册 secret integration 与故障注入入口 | secret 设计 8.1/9.1；tests 现状 | integration 建议目录与用例 | L0 | tests/CMakeLists.txt、tests/integration/infra/secret/ | integration：SecretRotationWorkflowTest、SecretFailureInjectionTest | ctest --test-dir build-ci -N && ctest --test-dir build-ci -L integration | SEC-TODO-015 | 无（2026-03-30 已由 INF-BLK-06 integration 顶层拓扑校准解阻） | 无；待 SEC-TODO-015 完成后落盘具体 integration 用例 | CMake 改动或阻塞记录 | 仅当 integration 用例可发现并执行时完成 |
| SEC-TODO-017 | Not Started | 回写 secret 质量门与交付证据 | secret 设计 9.2/11 | Gate 与风险回退章节 | L2 | docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md | process test：门禁结论、阻塞变化、回退证据回写 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | SEC-TODO-015 | 无 | 无 | 更新后的 TODO 文档证据段 | 仅当每个门禁项都有通过/失败结论和命令证据时完成 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 接口与对象冻结 | SEC-TODO-001~005、SEC-TODO-011 | 可并行（接口组/对象组/错误码组） | 先冻结边界，避免实现返工 |
| B backend 与访问链骨架 | SEC-TODO-006~009 | 串行 | backend -> facade -> lease |
| C 轮换、审计、健康 | SEC-TODO-010、SEC-TODO-012、SEC-TODO-013 | 串行为主，12/13 可局部并行 | 先轮换再桥接更稳妥 |
| D 构建与测试接线 | SEC-TODO-014、SEC-TODO-015 | 可并行 | CMake 接线与测试注册同步推进 |
| E 集成与证据收口 | SEC-TODO-016、SEC-TODO-017 | 串行（016 当前 Blocked） | 先解阻 integration，再做最终证据回写 |

### 7.2 必过门禁表

| Gate ID | 门禁项 | 通过标准 | 失败处置 |
|---|---|---|---|
| SEC-GATE-01 | 接口冻结门 | ISecretManager/ISecretBackend/SecretTypes/SecretErrors 落盘并编译通过 | 回退到接口定义阶段 |
| SEC-GATE-02 | 明文防护门 | SecureBuffer release 后零化断言通过，日志/审计不含明文 | 回退相关实现并补测试 |
| SEC-GATE-03 | 访问链门 | get/materialize/release/lease 生命周期路径可二值判定 | 回退 Facade 与 LeaseRegistry |
| SEC-GATE-04 | 轮换回退门 | 轮换失败可回退，回退失败有明确错误码 | 回退轮换实现并保留只读路径 |
| SEC-GATE-05 | 审计完整性门 | access/deny/rotate/revoke/fallback 事件均有证据 | 回退审计桥并补事件断言 |
| SEC-GATE-06 | 测试发现性门 | ctest -N 可见 secret 新增 unit/contract 测试 | 修复测试注册 |
| SEC-GATE-07 | breaking 评审门 | 接口签名/contracts 映射变化有评审结论 | 未评审不得推进 |
| SEC-GATE-08 | integration 准入门 | tests 顶层 integration 接线完成且标签规范落地，且 secret 组件用例已落盘 | 未通过前补齐 secret integration 用例与注册 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| SEC-BLK-001 | 已解阻（2026-04-03）：secret 详细设计 6.9 已冻结 `infra.secret.file.root_dir = secrets/`（部署层）与 `infra.secret.file.encrypt_at_rest = true`（默认/Profile）；后续 FileSecretBackend 只需按该最小策略落盘 | SEC-TODO-007 | 无；后续仅需保持 FileSecretBackend 实现、TODO 与单测对 root_dir/encrypt_at_rest 语义一致 | 证据回链到 docs/architecture/DASALL_infra_secret模块详细设计.md 6.9 与 docs/todos/infrastructure/deliverables/SEC-BLK-001-FileSecretBackend配置解阻.md | 若 file backend 回退为明文存储、允许越过 root_dir 访问任意路径，或把 encrypt_at_rest 默认值改为 false，则重新转为 Blocked |
| SEC-BLK-002 | 已解阻（2026-04-04）：secret 详细设计 6.8.1 / 6.9 已冻结 internal SecretRotationValidator 最小接口、candidate_version 推导、`validation_required` 与 `grace_period_sec` 语义；后续 SecretRotationCoordinator 只需按该最小策略落盘 | SEC-TODO-010 | 无；后续仅需保持 coordinator 实现、TODO 与单测对 validator / grace window 语义一致 | 证据回链到 docs/architecture/DASALL_infra_secret模块详细设计.md 6.8.1 / 6.9 与 docs/todos/infrastructure/deliverables/SEC-BLK-002-SecretRotationValidator最小接口解阻.md | 若 rotation coordinator 回退为静默跳过验证、把 dual-slot 请求降级为 inplace promote，或忽略 grace period / rollback 语义，则重新转为 Blocked |
| SEC-BLK-003 | KMS 身份、限流、超时和测试夹具未冻结 | 后续 KMS 真实接入任务 | 冻结 identity/retry/timeout/quota 策略并补夹具 | 先仅保留 KmsSecretBackend 接口占位 | 禁止真实 KMS SDK 接入 |
| SEC-BLK-004 | 审计注册点细节未统一 | SEC-TODO-012 | 冻结 IAuditLogger 接线和事件字段映射 | 先以 mock audit logger 打通断言 | 审计桥保留缓存，不宣称生产可用 |
| SEC-BLK-005 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；secret integration 用例是否可执行改由组件自身落盘负责 | SEC-TODO-016 | 无；后续仅需按组件落盘 integration 用例 | 证据回链到 infra 专项 TODO 的 INF-BLK-06 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt | 若 tests 顶层 integration 接线或聚合依赖回退，则重新转为 Blocked |

## 9. 验收与质量门

### 9.1 验收命令基线

| 用途 | 命令 |
|---|---|
| 配置构建目录 | cmake -S . -B build-ci -G Ninja |
| 构建 infra | cmake --build build-ci --target dasall_infra |
| 执行 unit 套件 | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit |
| 执行 contract 套件 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract |
| 检查测试发现性 | ctest --test-dir build-ci -N |
| 点测 secret（若已注册） | ctest --test-dir build-ci -R "SecretInterfaceCompileTest|SecretTypeBoundaryTest|SecretBackendAdapterTest|SecretLeaseLifecycleTest|SecretRotationCoordinatorTest|SecretAuditBridgeTest|SecretHealthProbeTest" |

说明：

1. integration 命令当前不纳入首轮 Gate，原因是 SEC-TODO-016 尚未落盘具体 integration 用例；顶层 integration 拓扑已于 2026-03-30 解阻。
2. 每项任务至少需要 1 条构建命令与 1 条测试命令。

### 9.2 质量门逐项回答

1. 是否给出 Design -> TODO 映射，而不是只列标题：是。
2. 是否明确当前最细可达到粒度等级：是（L2 为主，局部 L3）。
3. 是否所有任务都满足代码目标 + 测试目标 + 验收命令：是。
4. 是否所有 Blocked 项都带有证据和解阻条件：是。
5. 是否所有任务都具备可二值判定的完成标准：是。
6. 是否避免跨子系统范围扩张：是。
7. 是否真正落到接口/数据结构级对象：是。

### 9.3 已完成任务证据

| 任务 ID | 完成时间 | 代码交付物 | 验证证据 |
|---|---|---|---|
| SEC-TODO-001 | 2026-04-01 | infra/include/secret/ISecretManager.h；tests/unit/infra/SecretManagerInterfaceTest.cpp；tests/contract/smoke/SecretManagerInterfaceBoundaryContractTest.cpp；infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt；tests/contract/CMakeLists.txt | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_secret_manager_interface_unit_test dasall_contract_secret_manager_interface_boundary_test` 通过；`ctest --test-dir build-ci -R "SecretManagerInterfaceTest|SecretManagerInterfaceBoundaryContractTest" --output-on-failure` 通过（2/2） |
| SEC-TODO-002 | 2026-04-01 | infra/include/secret/ISecretHealthSource.h；tests/unit/infra/SecretHealthSourceInterfaceTest.cpp；infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt | `cmake --build build-ci --target dasall_infra dasall_secret_health_source_interface_unit_test` 通过；`ctest --test-dir build-ci -N | grep SecretHealthSourceInterfaceTest` 命中；`ctest --test-dir build-ci -R SecretHealthSourceInterfaceTest --output-on-failure` 通过（1/1） |
| SEC-TODO-003 | 2026-04-01 | infra/include/secret/SecretTypes.h；infra/include/secret/ISecretManager.h；tests/unit/infra/SecretTypesTest.cpp；tests/contract/smoke/SecretTypeBoundaryContractTest.cpp；infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt；tests/contract/CMakeLists.txt | `cmake --build build-ci --target dasall_infra dasall_secret_types_unit_test dasall_contract_secret_type_boundary_test` 通过；`ctest --test-dir build-ci -R "SecretTypesTest|SecretTypeBoundaryContractTest" --output-on-failure` 通过（2/2） |
| SEC-TODO-004 | 2026-04-01 | infra/include/secret/SecureBuffer.h；infra/include/secret/SecretTypes.h；tests/unit/infra/SecureBufferTest.cpp；infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt | `cmake --build build-ci --target dasall_infra dasall_secret_secure_buffer_unit_test` 通过；`ctest --test-dir build-ci -R SecureBufferTest --output-on-failure` 通过（1/1） |
| SEC-TODO-005 | 2026-04-01 | infra/include/secret/ISecretBackend.h；tests/unit/infra/SecretBackendInterfaceTest.cpp；tests/contract/smoke/SecretBackendContractSmokeTest.cpp；infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt；tests/contract/CMakeLists.txt | `cmake --build build-ci --target dasall_infra dasall_secret_backend_interface_unit_test dasall_contract_secret_backend_contract_smoke_test` 通过；`ctest --test-dir build-ci -R "SecretBackendInterfaceTest|SecretBackendContractSmokeTest" --output-on-failure` 通过（2/2） |
| SEC-TODO-006 | 2026-04-03 | infra/src/secret/backends/MockSecretBackend.h；infra/src/secret/backends/MockSecretBackend.cpp；tests/unit/infra/secret/MockSecretBackendTest.cpp；infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt；docs/todos/infrastructure/deliverables/SEC-TODO-006-MockSecretBackend骨架收敛.md | `cmake -S . -B build-ci -G "Unix Makefiles"` 通过；`cmake --build build-ci --target dasall_infra dasall_mock_secret_backend_unit_test` 通过；`ctest --test-dir build-ci -N -R MockSecretBackendTest` 发现 1 个测试；`ctest --test-dir build-ci --output-on-failure -R MockSecretBackendTest` 通过（1/1） |
| SEC-TODO-007 | 2026-04-03 | infra/src/secret/backends/FileSecretBackend.h；infra/src/secret/backends/FileSecretBackend.cpp；tests/unit/infra/secret/FileSecretBackendTest.cpp；infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt；docs/todos/infrastructure/deliverables/SEC-TODO-007-FileSecretBackend骨架收敛.md | `cmake -S . -B build-ci -G "Unix Makefiles"` 通过；`cmake --build build-ci --target dasall_infra dasall_file_secret_backend_unit_test` 通过；`ctest --test-dir build-ci -N -R FileSecretBackendTest` 发现 1 个测试；`ctest --test-dir build-ci --output-on-failure -R FileSecretBackendTest` 通过（1/1） |
| SEC-TODO-008 | 2026-04-03 | infra/src/secret/SecretManagerFacade.h；infra/src/secret/SecretManagerFacade.cpp；tests/unit/infra/secret/SecretManagerFacadeTest.cpp；tests/contract/smoke/SecretManagerFacadeBoundaryContractTest.cpp；infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt；tests/contract/CMakeLists.txt；docs/todos/infrastructure/deliverables/SEC-TODO-008-SecretManagerFacade访问骨架收敛.md | `cmake -S . -B build-ci -G "Unix Makefiles"` 通过；`cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_contract_secret_manager_facade_boundary_test` 通过；`ctest --test-dir build-ci -N -R "SecretManagerFacade(Test|BoundaryContractTest)"` 发现 2 个测试；`ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacade(Test|BoundaryContractTest)"` 通过（2/2） |
| SEC-TODO-009 | 2026-04-03 | infra/src/secret/SecretLeaseRegistry.h；infra/src/secret/SecretLeaseRegistry.cpp；infra/src/secret/SecretManagerFacade.h；infra/src/secret/SecretManagerFacade.cpp；tests/unit/infra/secret/SecretLeaseRegistryTest.cpp；tests/unit/infra/secret/SecretManagerFacadeTest.cpp；infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt；docs/todos/infrastructure/deliverables/SEC-TODO-009-SecretLeaseRegistry生命周期收敛.md | `cmake -S . -B build-ci -G "Unix Makefiles"` 通过；`cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_secret_lease_registry_unit_test dasall_contract_secret_manager_facade_boundary_test` 通过；`ctest --test-dir build-ci -N -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest"` 发现 3 个测试；`ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest"` 通过（3/3） |
| SEC-TODO-011 | 2026-04-01 | infra/include/secret/SecretErrors.h；tests/unit/infra/SecretErrorsTest.cpp；tests/contract/smoke/SecretErrorMappingContractTest.cpp；infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt；tests/contract/CMakeLists.txt | `cmake --build build-ci --target dasall_infra dasall_secret_errors_unit_test dasall_contract_secret_error_mapping_test` 通过；`ctest --test-dir build-ci -R "SecretErrorsTest|SecretErrorMappingContractTest" --output-on-failure` 通过（2/2） |

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 回退策略 |
|---|---|---|---|---|
| 明文泄漏到日志/对象序列化 | High | SecureBuffer 被转储或日志误打印 | 明文不落盘测试失败 | 立即回退并强制启用零化与脱敏检查 |
| 权限校验过粗导致越权 | High | 未校验 purpose/permission_domain | access_denied 与访问成功比例异常 | 回退到最小 allowlist 策略并补拒绝断言 |
| 轮换破坏在线句柄 | High | promote 后旧 lease 非预期失效 | stale/version 错误激增 | 回退到旧版本并开启 grace period |
| backend 抖动导致可用性下降 | Medium | file/kms backend 连续不可用 | backend_error_total、degraded 状态持续上升 | 启用只读降级（受 profile 约束）或暂时切回 mock/file |
| 审计桥故障被静默忽略 | High | audit 写入失败未上报 | audit_fail 计数缺失 | 回退并强制 INF_E_SECRET_AUDIT_WRITE_FAIL 快返 |
| contracts 污染 | High | backend_type/实现细节进入共享契约 | contract 测试失败 | 回退对象定义，保留 infra 私有模型 |

## 11. 可行性结论

1. 结论：可直接生成并执行接口/数据结构级专项 TODO，并可局部进入函数骨架级；当前不建议推进 KMS 真实接入与 integration 全量验收。
2. 原因：
   - 已具备核心接口清单、对象字段和错误语义。
   - 已具备主流程/异常流程与配置项策略。
   - 已具备落盘目录、测试矩阵与 Design -> Build 映射。
   - KMS 身份策略与 secret integration 用例落盘仍存在关键缺口。
3. 当前最小可执行粒度：接口 / 数据结构（L2），局部函数骨架（L3）。
4. 未达到全量函数级的缺口：KMS 真实接入策略、integration 顶层注册。
5. 下一步建议：
   - 先执行 SEC-TODO-001~015 完成接口/对象/主链/门禁骨架。
   - 继续按顺序处理 SEC-TODO-010；其中访问链与 lease 生命周期骨架已完成，dual-slot 验证器与 grace period 语义也已解阻，轮换链路可直接进入实现。
   - KMS 真实接入保持 Blocked，待策略与测试夹具冻结后单独建 v2 专项 TODO。

## 12. 本轮执行记录（2026-04-03 ~ 2026-04-04）

### 12.1 SEC-BLK-001

选中任务：

1. 任务 ID：SEC-BLK-001。
2. 可执行性依据：SEC-TODO-007 当前唯一阻塞项是 file backend 的 root_dir/encrypt_at_rest 最小策略；仓库内 secret 详细设计 6.9 已经给出这两个配置项的默认值与覆盖层级，因此本轮可以作为 blocker recovery 独立收口。

研究学习：

1. 本地证据：docs/architecture/DASALL_infra_secret模块详细设计.md 6.9 已冻结 `infra.secret.file.root_dir = secrets/` 与 `infra.secret.file.encrypt_at_rest = true`，并明确覆盖层级分别为部署与默认/Profile。
2. 本地证据：docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 仍把 SEC-BLK-001 记为“策略未冻结”，与现有设计文档状态不一致，导致 SEC-TODO-007 在 TODO 视图里被错误阻塞。
3. 外部参考：OWASP Secrets Management Cheat Sheet 强调 secrets at rest 应受加密保护，并尽量缩短 plaintext in memory 的暴露窗口；Azure Key Vault best practices 强调 secret 存储不应退化为普通配置存储，这支持 DASALL 将 root_dir/encrypt_at_rest 保持为 secret 专属最小策略。

D 结论：

1. Design -> Build 映射：不新增代码实现，仅把 secret 详细设计 6.9 的 file backend 配置冻结结果回链到 TODO 与 blocker deliverable，作为 SEC-TODO-007 的直接输入约束。
2. Build 三件套：
	- 代码目标：新增 docs/todos/infrastructure/deliverables/SEC-BLK-001-FileSecretBackend配置解阻.md，并更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 的 blocker 状态与 SEC-TODO-007 前置说明。
	- 测试目标：通过文档 traceability 确认 architecture、TODO 与 blocker deliverable 对 SEC-BLK-001 的解阻状态和 007 交接约束一致。
	- 验收命令：`rg -n "SEC-BLK-001|infra\.secret\.file\.root_dir|infra\.secret\.file\.encrypt_at_rest|SEC-TODO-007" docs/architecture/DASALL_infra_secret模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md docs/todos/infrastructure/deliverables/SEC-BLK-001-FileSecretBackend配置解阻.md`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. docs/todos/infrastructure/deliverables/SEC-BLK-001-FileSecretBackend配置解阻.md：补齐 blocker 根因、外部参考、设计结论与对 SEC-TODO-007 的交接约束。
2. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md：将 SEC-BLK-001 标记为已解阻，把 SEC-TODO-007 的 blocker 列迁移为已解阻说明，并把下一步建议切换到 006->007->008->009 的串行入口。

验收结果：

1. `rg -n "SEC-BLK-001|infra\.secret\.file\.root_dir|infra\.secret\.file\.encrypt_at_rest|SEC-TODO-007" docs/architecture/DASALL_infra_secret模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md docs/todos/infrastructure/deliverables/SEC-BLK-001-FileSecretBackend配置解阻.md`：通过；命中 secret 设计 6.9 的 file 配置项、TODO 中的解阻状态与执行记录，以及 blocker deliverable 的交接约束，三处证据已可回链。

Build 合规复核：

1. 代码注释：本轮不落盘实现代码，只做 blocker 设计/状态收口。
2. 正负例覆盖：本轮以文档 traceability 覆盖“已解阻/若回退则重新 Blocked”的二值状态；真正的编译与功能正负例留给 SEC-TODO-007。
3. 测试发现性：不新增测试目标，不改变现有 ctest 发现性。
4. TODO 证据回写：已完成 blocker 状态、交付物与下一步入口回写。
5. 提交隔离：本轮只处理 SEC-BLK-001 文档解阻，不提前落盘 Mock/File backend 代码。

### 12.2 SEC-TODO-006

选中任务：

1. 任务 ID：SEC-TODO-006。
2. 可执行性依据：ISecretBackend、SecretTypes 和 SecretErrors 已全部冻结；当前仓库缺的是 secret backend 的第一个可运行实现与对应 unit 验收出口，因此 006 是 backend 与访问链骨架阶段最早、最小且可提交的原子任务。

研究学习：

1. 本地证据：infra/include/secret/ISecretBackend.h 已固定 fetch/materialize/promote/revoke/status 五个入口，说明 mock backend 首轮需要把完整协议落盘，而不能只做半个 fetch stub。
2. 本地证据：fetch_record 不接收 access_context，因此“拒绝”路径应放在 materialize_record 结合 permission_domain 判定，而不是把权限逻辑硬塞进 fetch。
3. 外部参考：OWASP Secrets Management Cheat Sheet 强调最小权限和受控 materialize；Azure Key Vault best practices 强调 secret 数据面不应退化为普通配置面，这支持 MockSecretBackend 继续只暴露 metadata record + SecureBuffer，不向公共对象泄露实现细节。

D 结论：

1. Design -> Build 映射：新增 internal MockSecretBackend 头/源，提供 seeded secret fixture、backend availability 状态和 permission-domain 守卫；新增 MockSecretBackendTest 与最小 CMake 接线。
2. Build 三件套：
   - 代码目标：新增 infra/src/secret/backends/MockSecretBackend.h、infra/src/secret/backends/MockSecretBackend.cpp，并更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt。
   - 测试目标：新增 tests/unit/infra/secret/MockSecretBackendTest.cpp，覆盖成功、未命中、拒绝和 backend down 四路径，并验证 ctest discoverability。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_mock_secret_backend_unit_test && ctest --test-dir build-ci -N -R MockSecretBackendTest && ctest --test-dir build-ci --output-on-failure -R MockSecretBackendTest`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. infra/src/secret/backends/MockSecretBackend.h、infra/src/secret/backends/MockSecretBackend.cpp：新增 internal mock backend 实现，支持 seeded record、materialize lease、状态查询和最小 rotation/revoke 语义。
2. tests/unit/infra/secret/MockSecretBackendTest.cpp：新增四路径单测。
3. infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt：接入 mock backend 源码与 unit test target。
4. docs/todos/infrastructure/deliverables/SEC-TODO-006-MockSecretBackend骨架收敛.md：补齐本轮 D/B 收敛、研究结论和验收结果。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_mock_secret_backend_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R MockSecretBackendTest`：通过，发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R MockSecretBackendTest`：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：本轮新增类和测试命名已足够自解释，无需额外注释扩张。
2. 正负例覆盖：已覆盖成功、未命中、权限拒绝和 backend down 四条任务要求路径。
3. 测试发现性：已补 `ctest -N -R MockSecretBackendTest` 证据，确认新 target 可发现。
4. TODO 证据回写：已完成状态、交付物和验收结果回写。
5. 提交隔离：本轮只处理 mock backend、单测和最小接线，不提前进入 file backend、manager 或 lease registry。

### 12.3 SEC-TODO-007

选中任务：

1. 任务 ID：SEC-TODO-007。
2. 可执行性依据：SEC-BLK-001 已解阻，root_dir/encrypt_at_rest 的最小策略已经冻结；当前仓库只缺 file backend 内部实现和单测，因此 007 可以独立完成并提交。

研究学习：

1. 本地证据：secret 详细设计 6.9 已把 `infra.secret.file.root_dir` 标记为部署层配置、`infra.secret.file.encrypt_at_rest` 标记为默认/Profile，说明 file backend 首轮必须内化这两个约束，而不是留到后续 facade 再处理。
2. 本地证据：contracts 边界不允许新增 `file_path` 字段，因此 file backend 只能通过 opaque `cipher_ref` 表达文件来源，不能把物理路径细节上抬。
3. 外部参考：OWASP Secrets Management Cheat Sheet 强调 secrets at rest 受保护和 materialize 后不落明文中间介质；Azure Key Vault best practices 强调 secret store 不应退化为普通配置中心，这支持本轮用受控 fixture + SecureBuffer 打通最小链路。

D 结论：

1. Design -> Build 映射：新增 internal FileSecretBackend 头/源，负责 root_dir 安全解析、key=value fixture 读取、`ciphertext_hex` 解码和 backend status；新增 FileSecretBackendTest 与最小 CMake 接线。
2. Build 三件套：
   - 代码目标：新增 infra/src/secret/backends/FileSecretBackend.h、infra/src/secret/backends/FileSecretBackend.cpp，并更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt。
   - 测试目标：新增 tests/unit/infra/secret/FileSecretBackendTest.cpp，覆盖成功路径、缺失路径、backend unavailable，并断言不生成额外明文文件。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_file_secret_backend_unit_test && ctest --test-dir build-ci -N -R FileSecretBackendTest && ctest --test-dir build-ci --output-on-failure -R FileSecretBackendTest`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. infra/src/secret/backends/FileSecretBackend.h、infra/src/secret/backends/FileSecretBackend.cpp：新增 file backend internal skeleton，实现 root_dir 安全解析、fixture 读取、`ciphertext_hex` 解码和 backend unavailable/status 语义。
2. tests/unit/infra/secret/FileSecretBackendTest.cpp：新增成功、缺失路径、backend unavailable 三路径单测，并验证 materialize 不创建额外文件。
3. infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt：接入 file backend 源码和 unit test target。
4. docs/todos/infrastructure/deliverables/SEC-TODO-007-FileSecretBackend骨架收敛.md：补齐本轮 D/B 收敛、研究结论和验收结果。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_file_secret_backend_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R FileSecretBackendTest`：通过，发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R FileSecretBackendTest`：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：类名、fixture 字段和测试断言已足够自解释，本轮未增加额外注释噪音。
2. 正负例覆盖：已覆盖成功路径、缺失路径、backend unavailable，并额外验证不生成额外明文文件。
3. 测试发现性：已补 `ctest -N -R FileSecretBackendTest` 证据，确认新 target 可发现。
4. TODO 证据回写：已完成状态、交付物和验收结果回写。
5. 提交隔离：本轮只处理 file backend、单测和最小接线，不提前进入 SecretManagerFacade 或 LeaseRegistry。

### 12.4 SEC-TODO-008

选中任务：

1. 任务 ID：SEC-TODO-008。
2. 可执行性依据：Mock/File backend、ISecretManager、SecretTypes 和 SecretErrors 已全部落盘，当前缺口只剩 manager 主链实现与访问边界验收，因此 008 是 backend 与访问链骨架阶段下一最小、可提交的原子任务。

研究学习：

1. 本地证据：现有 SecretManagerInterfaceBoundaryContractTest 只冻结接口签名和“不吸收 backend/health 协议”，并不覆盖 access_context 在访问链中的复用边界，因此 008 需要新增独立 contract test，而不是复用旧测试。
2. 本地证据：MockSecretBackend 已具备 fetch/materialize/status 和 permission-domain 守卫，足以作为 manager facade 的正负例夹具，无需本轮再引入额外 backend 变体。
3. 外部参考：OWASP Secrets Management Cheat Sheet 强调 secrets materialization 的最小暴露窗口和审计锚点；Azure Key Vault best practices 强调访问令牌、lease 与 materialized value 边界分离，这支持 DASALL 继续保持 metadata-only handle/lease，不复制 request/task/session 字段。

D 结论：

1. Design -> Build 映射：新增 internal SecretManagerFacade 头/源，实现 get_secret、materialize、release、inspect 主链；rotate 保持 deferred failure，revoke 做最小 backend 委托；新增 unit/contract tests 与最小 CMake 接线。
2. Build 三件套：
   - 代码目标：新增 infra/src/secret/SecretManagerFacade.h、infra/src/secret/SecretManagerFacade.cpp，并更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt。
   - 测试目标：新增 tests/unit/infra/secret/SecretManagerFacadeTest.cpp，覆盖访问链正向和 expired handle 负向；新增 tests/contract/smoke/SecretManagerFacadeBoundaryContractTest.cpp，覆盖 access_context 字段边界与错误载荷边界。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_contract_secret_manager_facade_boundary_test && ctest --test-dir build-ci -N -R "SecretManagerFacade(Test|BoundaryContractTest)" && ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacade(Test|BoundaryContractTest)"`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. infra/src/secret/SecretManagerFacade.h、infra/src/secret/SecretManagerFacade.cpp：新增 manager access-chain skeleton，收敛 get/materialize/release/inspect 主链，以及最小 revoke/rotate 边界。
2. tests/unit/infra/secret/SecretManagerFacadeTest.cpp：新增访问链正向与 expired handle 负向单测。
3. tests/contract/smoke/SecretManagerFacadeBoundaryContractTest.cpp：新增上下文字段复用边界 contract 守卫。
4. infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：接入 manager facade 源码与 unit/contract test targets。
5. docs/todos/infrastructure/deliverables/SEC-TODO-008-SecretManagerFacade访问骨架收敛.md：补齐本轮 D/B 收敛、研究结论和验收结果。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_contract_secret_manager_facade_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "SecretManagerFacade(Test|BoundaryContractTest)"`：通过，发现 2 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacade(Test|BoundaryContractTest)"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：类名、方法名和测试断言已足够表达 manager skeleton 边界，本轮未增加额外注释噪音。
2. 正负例覆盖：已覆盖 get->materialize->release->inspect 正向链路、expired handle 负向路径，以及 access_context 字段边界 contract 守卫。
3. 测试发现性：已补 `ctest -N -R "SecretManagerFacade(Test|BoundaryContractTest)"` 证据，确认 unit/contract 新 target 可发现。
4. TODO 证据回写：已完成状态、交付物和验收结果回写。
5. 提交隔离：本轮只处理 SecretManagerFacade skeleton、对应 unit/contract 与最小 CMake 接线，不提前抽取 SecretLeaseRegistry 或推进 rotation coordinator。

### 12.5 SEC-TODO-009

选中任务：

1. 任务 ID：SEC-TODO-009。
2. 可执行性依据：SEC-TODO-008 已完成 manager 访问骨架，当前唯一缺口是把 facade 内部临时 lease 状态抽离成独立 registry，并补齐 expired/stale 生命周期回归，因此 009 可作为下一原子任务独立完成并提交。

研究学习：

1. 本地证据：secret 详细设计 6.3 明确 SecretLeaseRegistry 的语义契约是“输入 handle/consumer/deadline，输出 lease_id/expires_at/release status”，说明 registry 只应管理生命周期，不应接管权限、审计或 backend 协议。
2. 本地证据：secret 详细设计 6.8 明确 stale handle 必须返回 `INF_E_SECRET_VERSION_STALE`，因此 materialize 不能继续把版本漂移退化为 backend 未命中。
3. 外部参考：OWASP Secrets Management Cheat Sheet 强调短期凭据、显式过期/吊销与全生命周期可判定；Azure Key Vault best practices 强调版本切换后旧引用快速失效，这支持本轮以 `rotation_epoch` 漂移建模 stale handle。

D 结论：

1. Design -> Build 映射：新增 internal SecretLeaseRegistry 头/源，实现 create/validate/expire/release 最小生命周期；更新 SecretManagerFacade，移除临时 active lease map，materialize/release/revoke 改为委托 registry，并把 stale handle 显式映射到 VersionStale。
2. Build 三件套：
   - 代码目标：新增 infra/src/secret/SecretLeaseRegistry.h、infra/src/secret/SecretLeaseRegistry.cpp，并更新 infra/src/secret/SecretManagerFacade.h、infra/src/secret/SecretManagerFacade.cpp、infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt。
   - 测试目标：新增 tests/unit/infra/secret/SecretLeaseRegistryTest.cpp，覆盖创建/过期/释放/陈旧句柄；更新 tests/unit/infra/secret/SecretManagerFacadeTest.cpp，增加 stale handle materialize 拒绝回归。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_secret_lease_registry_unit_test dasall_contract_secret_manager_facade_boundary_test && ctest --test-dir build-ci -N -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest"`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. infra/src/secret/SecretLeaseRegistry.h、infra/src/secret/SecretLeaseRegistry.cpp：新增 lease lifecycle registry，实现 create/validate/expire/release 与按 secret 批量失效。
2. infra/src/secret/SecretManagerFacade.h、infra/src/secret/SecretManagerFacade.cpp：移除临时 active lease map，把 materialize/release/revoke 改为委托 registry，并显式返回 stale handle 错误码。
3. tests/unit/infra/secret/SecretLeaseRegistryTest.cpp：新增创建/过期/释放/陈旧句柄四路径单测。
4. tests/unit/infra/secret/SecretManagerFacadeTest.cpp：新增 backend 版本轮换后的 stale handle materialize 拒绝回归。
5. infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt：接入 registry 源码与 unit test target。
6. docs/todos/infrastructure/deliverables/SEC-TODO-009-SecretLeaseRegistry生命周期收敛.md：补齐本轮 D/B 收敛、研究结论和验收结果。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_secret_lease_registry_unit_test dasall_contract_secret_manager_facade_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest"`：通过，发现 3 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest"`：通过，3/3 tests passed。

Build 合规复核：

1. 代码注释：registry 和 facade 的类型命名、状态枚举与测试断言已足够表达生命周期边界，本轮未额外扩张注释面。
2. 正负例覆盖：已覆盖 lease 创建、过期、释放、rotation epoch 漂移导致的 stale 句柄，以及 stale handle materialize 拒绝回归。
3. 测试发现性：已补 `ctest -N -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest"` 证据，确认 unit/contract 相关 target 可发现。
4. TODO 证据回写：已完成状态、交付物和验收结果回写。
5. 提交隔离：本轮只处理 SecretLeaseRegistry、Facade 生命周期委托和对应回归测试，不提前进入 rotation coordinator、审计桥或健康探针。

### 12.6 SEC-BLK-002

选中任务：

1. 任务 ID：SEC-BLK-002。
2. 可执行性依据：SEC-TODO-010 当前唯一阻塞项是 dual-slot 验证器最小接口与 `validation_required` / `grace_period_sec` 的执行语义未冻结；现有 secret 详细设计与代码基线已经具备 backend promote/revoke 能力和 lease 生命周期语义，因此本轮可以作为 blocker recovery 独立收口。

研究学习：

1. 本地证据：docs/architecture/DASALL_infra_secret模块详细设计.md 6.9 已经列出 `infra.secret.rotation.dual_slot_enabled`、`infra.secret.rotation.validation_required` 与 `infra.secret.rotation.grace_period_sec` 三个配置项，但 6.8 之前没有把这些配置映射成 coordinator 可直接编码的最小时序规则。
2. 本地证据：infra/src/secret/SecretManagerFacade.cpp 仍将 `rotate` 保持为 deferred failure，而 infra/src/secret/backends/MockSecretBackend.cpp 已具备最小 `promote_version` / `revoke_version` 语义，说明 010 的真实缺口是轮换协调器与验证器契约，而不是 backend 协议本身。
3. 外部参考：OWASP Secrets Management Cheat Sheet 强调自动化轮换应采用显式 create / set / test / finish 多阶段流程，并支持“新版本写、旧版本读”的渐进切换；Azure Key Vault secrets best practices 明确建议零停机轮换使用 dual credentials，并在轮换后刷新缓存与监控生命周期事件，这支持 DASALL 把 dual-slot validator、宽限窗口与 rollback 证据显式冻结。

D 结论：

1. Design -> Build 映射：在 secret 详细设计新增 6.8.1，冻结 internal `RotationValidationContext` / `ISecretRotationValidator` 最小接口、candidate version 推导、dual-slot 启用条件与 grace window 时序；同步新增 blocker deliverable，并更新 secret TODO 的 blocker 状态与 SEC-TODO-010 前置说明。
2. Build 三件套：
   - 代码目标：更新 docs/architecture/DASALL_infra_secret模块详细设计.md，并新增 docs/todos/infrastructure/deliverables/SEC-BLK-002-SecretRotationValidator最小接口解阻.md；同步更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 的 blocker 状态与后续执行入口。
   - 测试目标：通过文档 traceability 确认 architecture、TODO 与 blocker deliverable 对 SEC-BLK-002 的解阻状态以及对 SEC-TODO-010 的交接约束一致。
   - 验收命令：`rg -n "SecretRotationValidator|validation_required|grace_period_sec|SEC-BLK-002|SEC-TODO-010" docs/architecture/DASALL_infra_secret模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md docs/todos/infrastructure/deliverables/SEC-BLK-002-SecretRotationValidator最小接口解阻.md`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. docs/architecture/DASALL_infra_secret模块详细设计.md：新增 6.8.1，冻结 internal rotation validator、candidate version 推导、dual-slot / grace period / rollback 的最小语义，并补充 6.9 的配置解释。
2. docs/todos/infrastructure/deliverables/SEC-BLK-002-SecretRotationValidator最小接口解阻.md：补齐 blocker 根因、外部参考、设计结论与对 SEC-TODO-010 的交接约束。
3. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md：将 SEC-BLK-002 标记为已解阻，把 SEC-TODO-010 的 blocker 列迁移为已解阻说明，并将下一步建议切换到直接推进 SEC-TODO-010。

验收结果：

1. `rg -n "SecretRotationValidator|validation_required|grace_period_sec|SEC-BLK-002|SEC-TODO-010" docs/architecture/DASALL_infra_secret模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md docs/todos/infrastructure/deliverables/SEC-BLK-002-SecretRotationValidator最小接口解阻.md`：通过；命中 secret 设计 6.8.1 / 6.9 的 validator 与 grace period 语义、TODO 中的解阻状态与执行记录，以及 blocker deliverable 的交接约束，三处证据已可回链。

Build 合规复核：

1. 代码注释：本轮不落盘实现代码，只做 blocker 设计/状态收口。
2. 正负例覆盖：本轮以文档 traceability 覆盖“已解阻/若回退则重新 Blocked”的二值状态；真正的 create/test/promote/revoke/rollback 正负例留给 SEC-TODO-010。
3. 测试发现性：不新增测试目标，不改变现有 ctest 发现性。
4. TODO 证据回写：已完成 blocker 状态、交付物与下一步入口回写。
5. 提交隔离：本轮只处理 SEC-BLK-002 设计解阻，不提前落盘 SecretRotationCoordinator 或相关测试实现。
