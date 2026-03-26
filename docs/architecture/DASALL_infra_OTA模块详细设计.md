# DASALL infra/OTA 模块详细设计（Detailed Design）

版本：v1.0
日期：2026-03-24
阶段：Detailed Design
适用模块：infra/ota

## 1. 模块概览

### 1.1 目标与定位

infra/ota 属于 Layer 1 Infrastructure Layer，负责为 runtime、apps 与运维链路提供统一的软件升级、预检、灰度切换、启动确认与失败回滚能力，但不拥有业务策略裁定、主流程调度或恢复语义解释权。

本模块目标：

1. 提供可验证的 OTA 最小闭环：预检、验签、安装、启动确认、失败回滚。
2. 支持 A/B 或等价冗余槽位切换，并允许工具配置、模型路由、策略配置等工件独立升级。
3. 为 edge_minimal、edge_balanced、desktop_full 等 Profile 提供可裁剪实现，不改变上层调用语义。
4. 形成可观测、可审计、可回放的升级证据链，为后续 Build 与测试门禁提供直接输入。

来源依据：

1. docs/architecture/DASSALL_Agent_architecture.md 5.10、8.9、9.3、8.10
2. docs/architecture/DASALL_Engineering_Blueprint.md 3.12、4.1、4.2、4.3、5.1
3. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.2、6.3、6.5、6.6、6.8、6.9、7、8、9、11、12
4. docs/todos/DASALL_infrastructure子系统专项TODO.md 中 OTA 阻塞与任务记录
5. 行业参考：Uptane Standard 2.1.0、RAUC Basics

### 1.2 边界定义

上游消费者：apps、runtime、daemon、gateway、运维命令入口。

下游依赖：

1. contracts 中已冻结的 ResultCode、ErrorInfo、request_id、task_id、lease_id、EventEnvelope 等横切语义。
2. platform 抽象的文件系统、时间、进程、重启、启动参数与 boot control 能力。
3. infra/config、infra/health、infra/logging、infra/metrics、infra/secret。

同层协同：

1. config：提供升级策略、Profile 差异、开关与阈值配置。
2. health：提供升级前后健康检查与启动确认窗口评估。
3. logging、metrics、audit：记录升级生命周期、回滚证据和失败原因。
4. secret：提供验签公钥或信任根引用，但不承载 OTA 主流程。

### 1.3 设计范围

纳入范围：

1. OTA 子组件拆分、接口语义、核心对象、主异常流程、配置策略、可观测设计。
2. A/B 槽位或等价冗余安装策略的抽象边界。
3. Design -> Build 映射、实施分阶段建议、测试门与阻塞管理。

不纳入范围：

1. 云端仓库、发布平台、镜像生产流水线实现。
2. 第三方 OTA 方案的完整协议复刻。
3. runtime 对失败的全局 replan、abort_safe 或业务级灰度决策。

## 2. 约束清单

### 2.1 Must / Should / Must-Not 约束表

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| OTA-C001 | DASSALL_Agent_architecture.md 8.9 | Must | OTA 必须支持 A/B 或可回滚升级 | 流程、对象、回退 |
| OTA-C002 | DASSALL_Agent_architecture.md 8.9 | Must | 升级前必须执行健康检查与资源检查 | 预检、测试 |
| OTA-C003 | DASSALL_Agent_architecture.md 8.9 | Must | 工具配置、模型路由、策略配置支持独立升级 | 工件模型、目录 |
| OTA-C004 | DASSALL_Agent_architecture.md 8.9/9.3 | Must | 升级失败必须自动回滚，升级后必须自检 | 异常恢复、健康门 |
| OTA-C005 | DASALL_Engineering_Blueprint.md 3.12 | Must | infra/ota 作为 Infrastructure 子模块存在，职责是 A/B 升级、回滚与升级前健康检查 | 职责边界 |
| OTA-C006 | DASALL_Engineering_Blueprint.md 4.1/4.2 | Must | infra/ota 只能依赖 contracts 与下层抽象，不得反向依赖业务模块实现 | 依赖方向 |
| OTA-C007 | DASALL_Engineering_Blueprint.md 4.3 | Must | 对外能力必须通过冻结接口暴露，禁止跨模块直接依赖实现类 | 接口语义 |
| OTA-C008 | DASALL_Engineering_Blueprint.md 5.1/3.13 | Must | Profile 只能裁剪升级能力和替换实现，不得绕过 Audit 与 Runtime 主控链路 | 配置策略 |
| OTA-C009 | ADR-005-architecture-review-baseline.md | Must | 不得以 OTA 设计反向改写主架构与 frozen contracts 结论 | 设计治理 |
| OTA-C010 | ADR-006-context-orchestrator-vs-prompt-composer.md | Must-Not | OTA 不参与上下文装配与 Prompt 渲染，只输出升级事实和证据 | 边界职责 |
| OTA-C011 | ADR-007-reflection-engine-vs-recovery-manager.md | Must-Not | OTA 不做失败语义判定与恢复裁定，只执行本地回滚并返回结果 | 异常语义 |
| OTA-C012 | ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | Must-Not | OTA 不拥有全局请求调度权，只服务主控链路与运维命令 | 运行边界 |
| OTA-C013 | DASALL_contracts冻结实施计划.md 10 | Must | 默认向后兼容，新增字段优先 optional，不直接修改共享对象既有语义 | 兼容策略 |
| OTA-C014 | DASALL_contracts冻结TODO总表.md 5 | Must-Not | 若依赖 OTA 私有对象模型，必须留在 infra 私有域，不能倒灌到 contracts | 对象模型 |
| OTA-C015 | DASALL_infrastructure子系统详细设计.md 6.5/6.6 | Must | IOTAManager 与 UpgradeOutcome 可作为设计锚点，但在 package/signature/token 未冻结前禁止进入真实实现 | 接口冻结、阻塞管理 |
| OTA-C016 | docs/todos/DASALL_infrastructure子系统专项TODO.md INF-TODO-015 / INF-BLK-05 | Must | 必须先冻结 UpgradePlan、Package、rollback token、签名算法与存储规范，再解除接口阻塞 | 设计补齐 |
| OTA-C017 | DASALL_工程协作与编码规范.md 3.6 | Must | 验签失败、安装失败、回滚失败、启动确认失败必须可观测，不得吞错 | 错误语义、测试 |
| OTA-C018 | DASALL_工程协作与编码规范.md 3.7 | Should | 新增 IOTAManager 与公共对象时同步新增 unit、contract、integration 或 failure-injection 测试 | 测试门禁 |
| OTA-C019 | Uptane Standard 2.1.0 | Should | 镜像与元数据验证应分离，需防 rollback、freeze、mix-and-match 与 replay 风险 | 验签、对象约束 |
| OTA-C020 | RAUC Basics | Should | 槽位切换、启动确认、回退和 inactive slot 选择应显式建模 | 槽位管理、回退 |

### 2.2 contracts 当前阶段允许 / 禁止动作

允许动作：

1. 复用 contracts 中已冻结的 ResultCode、ErrorInfo、ID、EventEnvelope、兼容性规则。
2. 在 infra 私有域定义 UpgradePlan、PackageDescriptor、RollbackToken、InstallEvidence 等对象。
3. 基于现有 contracts 边界增加 contract tests，验证 OTA 不越权扩写共享语义。

禁止动作：

1. 不把 slot、bootloader、签名实现、仓库布局等实现细节写入 contracts。
2. 不修改已冻结 ADR 对 runtime/cognition/agent orchestration 的职责结论。
3. 不在 package schema 未冻结前直接生成 Build-ready 的真实 IOTAManager 实现任务。

## 3. 现状与缺口

### 3.1 现状识别

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| infra/ota 目录与源码落盘 | 占位 | infra/src/ota 目录存在，但无实现文件 | High | P0 |
| IOTAManager 接口头文件 | 缺失 | infra/include 为空，尚无 IOTAManager.h | High | P0 |
| OTA 私有对象模型 | 缺失 | UpgradePlan、PackageDescriptor、RollbackToken、InstallEvidence 未成文 | High | P0 |
| 签名与包规范 | 缺失 | 签名算法、manifest 字段、包存储路径与校验顺序未冻结 | High | P0 |
| 回滚与启动确认闭环 | 缺失 | 只有总设计中的抽象描述，未形成可执行状态机 | High | P0 |
| OTA 健康与审计出口 | 缺失 | 无 OTA health probe、无升级审计事件对象 | Medium | P1 |
| OTA 测试基线 | 缺失 | tests 下无 infra/ota 的 unit、integration、failure injection 用例 | High | P0 |
| 构建入口 | 占位 | infra/CMakeLists.txt 仍只编译 src/placeholder.cpp | High | P0 |

证据：

1. infra/CMakeLists.txt 仅编译 src/placeholder.cpp。
2. infra/include 为空目录。
3. infra/src/ota 当前为空目录。
4. docs/todos/DASALL_infrastructure子系统专项TODO.md 将 INF-TODO-015 标记为 Blocked，阻塞项为 INF-BLK-05。

### 3.2 现状-目标差距表

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| 提供稳定 OTA 对外入口 | 占位 | 只有总设计中的方法名，没有可编译接口骨架 | High | P0 |
| 建立可验证预检模型 | 缺失 | 缺少资源、健康、版本、兼容性四类检查对象 | High | P0 |
| 建立验签与安装解耦管线 | 缺失 | 缺少 manifest 校验、artifact 校验、安装执行器分层 | High | P0 |
| 建立启动确认与回滚状态机 | 缺失 | 无 boot switch、confirm、rollback token 生命周期定义 | High | P0 |
| 支持独立工件升级 | 缺失 | 工具配置、模型路由、策略配置尚未抽象为 artifact class | Medium | P1 |
| 支持测试门与故障注入 | 缺失 | 无 slow download、verify fail、boot fail、rollback fail 测试点 | High | P0 |

### 3.3 风险冲突识别

| 冲突类型 | 描述 | 影响 | 风险等级 |
|---|---|---|---|
| 边界冲突 | 若 OTA 直接访问 runtime 状态机做升级决策，将越过 ADR-007/008 边界 | 破坏主控职责 | High |
| 语义重复 | 若将 UpgradePlan 或 slot 状态塞入 contracts，将污染共享语义层 | 增加 breaking 风险 | High |
| 依赖反转 | 若上层直接依赖具体 bootloader 或签名库 | Profile 裁剪与替换困难 | High |
| 回滚失真 | 若没有 rollback token 与证据链，apply 失败后无法判定回退对象 | 恢复不可验证 | High |
| 资源误判 | 若无磁盘、电量、内存阈值预检，edge 档位容易把设备带入不可启动状态 | 设备可用性下降 | High |

## 4. 候选方案对比

### 4.1 候选方案说明

#### 方案 A：原地覆盖式 OTA

设计思路：

1. 直接下载升级包到当前运行分区或活动目录。
2. 验签通过后覆盖现有文件。
3. 出现失败时依赖文件级备份恢复。

组件结构：

1. PackageFetcher
2. SignatureVerifier
3. InPlaceInstaller
4. FileBackupManager

优点：

1. 实现简单。
2. 最少依赖 boot control 抽象。

风险：

1. 回滚窗口脆弱，容易出现半更新状态。
2. 不适合 edge 设备掉电、中断与 watchdog 失败场景。
3. 配置、模型与系统镜像混装时难做独立升级。

与 DASALL 约束匹配度：中低。

#### 方案 B：工件分层 + 槽位切换协调方案

设计思路：

1. 以 OTAPlanCoordinator 统筹预检、验签、安装、切换、启动确认与失败回滚。
2. 将升级对象抽象为 artifact class，区分 slot-bound 工件与 repo-bound 工件。
3. slot-bound 工件走 inactive slot 安装 + boot switch + confirm；repo-bound 工件走 staging repo 替换 + 原子指针切换。
4. 所有升级动作都生成 rollback token、install evidence 与 audit evidence。

组件结构：

1. OTAPlanCoordinator
2. OTAPrecheckService
3. PackageVerifier
4. ArtifactCompatibilityEvaluator
5. InstallExecutor
6. SlotSwitchCoordinator
7. BootConfirmationMonitor
8. RollbackController
9. OTAAuditBridge
10. OTAHealthProbe

优点：

1. 同时满足 A/B 回滚和配置/策略独立升级。
2. 易于按 Profile 裁剪，只需替换安装器与 boot control 适配器。
3. 与现有 infra 总设计、专项 TODO 和 contracts 冻结策略一致。

风险：

1. 对象模型较多，需要先冻结 OTA 私有对象。
2. boot switch 与启动确认钩子需要 platform 支撑。

与 DASALL 约束匹配度：高。

#### 方案 C：Uptane 全量仓库元数据方案

设计思路：

1. 在设备侧引入 director/image repository 对偶验证、时间证明、版本报告与多角色签名元数据链。
2. 设备端承担完整 metadata verification 与 repository mapping。
3. 以标准化安全元数据覆盖大部分 OTA 风险。

组件结构：

1. MetadataRepositoryClient
2. TimeAttestationClient
3. MetadataVerifier
4. VehicleVersionReporter
5. ImageInstaller

优点：

1. 安全性强，可系统化防止 rollback、freeze、mix-and-match。
2. 对未来车规或高安全设备演进友好。

风险：

1. 与当前 DASALL 工程准备度不匹配，依赖云端与仓库元数据体系。
2. 对 edge_minimal 成本过高。
3. 容易跨出 infra/ota 当前详细设计范围。

与 DASALL 约束匹配度：中。

### 4.2 候选方案对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| A 原地覆盖式 OTA | 中低 | 中 | 低 | 半更新态、掉电恢复脆弱、独立工件升级困难 | 淘汰：仅适合 PoC |
| B 工件分层 + 槽位切换协调 | 高 | 高 | 中 | 需要先冻结对象模型与 boot control 抽象 | 保留并采纳 |
| C Uptane 全量仓库元数据 | 中 | 高 | 高 | 设备侧复杂度过高，超出本阶段范围 | 暂不采纳：列为 v2 安全增强方向 |

## 5. 决策结论

### 5.1 最终选型

采纳方案 B：工件分层 + 槽位切换协调方案。

### 5.2 放弃其他候选方案的理由

1. 方案 A 无法稳定满足架构文档要求的自动回滚与升级后自检，也难支持配置、模型路由、策略配置独立升级。
2. 方案 C 虽然安全性更强，但会把当前工作范围扩展到仓库元数据体系、时间证明和版本报告协议，不符合本轮“不跨模块扩张”的硬约束。

### 5.3 与架构、ADR、contracts 的一致性说明

1. 与架构一致：OTA 仍留在 infra 层，只提供升级执行与观测能力，不接管业务编排。
2. 与 ADR 一致：
   - 不接管上下文与 Prompt 语义，只输出升级事实与证据。
   - 不裁定 recovery 语义，只执行本地回滚并报告结果。
   - 不拥有主控调度权，只作为基础设施命令和事件的执行单元。
3. 与 contracts 一致：UpgradePlan、PackageDescriptor、RollbackToken、InstallEvidence 全部保持 infra 私有域；对外仅复用 ResultCode、ErrorInfo、ID 和 EventEnvelope 语义。

## 6. 详细设计

### 6.1 职责边界

infra/ota 职责：

1. 统一接收 OTA 请求并执行预检、验签、安装、切换、确认、回滚流程。
2. 管理 slot-bound 与 repo-bound 两类工件的升级策略。
3. 输出 UpgradeOutcome、审计记录、健康探针结果和失败证据。

infra/ota 非职责：

1. 不决定是否全局重试、replan 或 abort_safe。
2. 不负责云端仓库发布、包制作与签名生产流程。
3. 不定义 contracts 共享对象的新语义。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| OTAManagerFacade | 对外统一入口，管理 apply、rollback、query_status 生命周期 |
| OTAPlanCoordinator | 协调预检、验签、安装、切换、确认与回滚 |
| OTAPrecheckService | 执行健康、资源、版本、兼容性与策略检查 |
| PackageVerifier | 校验 manifest、签名、hash、大小、版本单调性 |
| ArtifactCompatibilityEvaluator | 校验 artifact class、硬件兼容、Profile 允许集与依赖冲突 |
| InstallExecutor | 按 artifact class 执行安装或 staged materialization |
| SlotSwitchCoordinator | 选择 inactive slot、写入 boot target、生成 rollback token |
| BootConfirmationMonitor | 监听启动确认窗口、自检结果与超时事件 |
| RollbackController | 执行回滚、恢复旧 boot target、输出 rollback evidence |
| OTAAuditBridge | 写审计事件和证据引用 |
| OTAHealthProbe | 暴露 OTA backlog、上次失败、confirm 超时等健康信号 |

### 6.3 子组件输入 / 输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| OTAManagerFacade | 运维命令、runtime 触发请求、配置开关 | UpgradeOutcome、OTAStatusSnapshot | 所有失败必须返回可判定 ResultCode |
| OTAPlanCoordinator | UpgradePlan、当前状态、配置策略 | 分阶段执行结果、rollback token | 过程可中断、可恢复、可追踪 |
| OTAPrecheckService | HealthSnapshot、资源信息、版本信息、Profile 策略 | PrecheckReport | 任一 hard-fail 必须阻断 apply |
| PackageVerifier | PackageDescriptor、信任根、artifact manifest | VerifiedPackageManifest | 签名或 hash 失败必须短路 |
| ArtifactCompatibilityEvaluator | VerifiedPackageManifest、设备能力、Profile | CompatibilityReport | 兼容性失败不得进入安装 |
| InstallExecutor | Verified artifacts、安装目标、slot plan | InstallResult、InstallEvidence | 安装结果必须与 artifact 一一对应 |
| SlotSwitchCoordinator | SlotPlan、boot control adapter | BootSwitchResult、RollbackToken | 仅允许切到 inactive target |
| BootConfirmationMonitor | 启动确认信号、self-check 结果、超时配置 | BootConfirmationResult | 超时必须视为失败路径 |
| RollbackController | RollbackToken、InstallEvidence、当前 boot 状态 | RollbackResult、RollbackEvidence | 回滚失败必须单独可观测 |
| OTAAuditBridge | 阶段结果、actor、evidence_ref | audit event | 高风险动作强制审计 |
| OTAHealthProbe | 历史结果、当前 backlog、confirm state | ProbeResult | 只提供事实，不裁定恢复策略 |

### 6.4 子组件依赖关系

1. OTAManagerFacade -> OTAPlanCoordinator、RollbackController、OTAHealthProbe。
2. OTAPlanCoordinator -> OTAPrecheckService、PackageVerifier、ArtifactCompatibilityEvaluator、InstallExecutor、SlotSwitchCoordinator、BootConfirmationMonitor、OTAAuditBridge。
3. OTAPrecheckService -> infra/health、infra/config、platform resource adapter。
4. PackageVerifier -> infra/secret 提供的 trust anchor 引用、platform crypto/file adapter。
5. InstallExecutor -> platform file/block/device adapter。
6. SlotSwitchCoordinator 与 BootConfirmationMonitor -> platform boot control adapter、watchdog/health 信号。
7. RollbackController -> SlotSwitchCoordinator、InstallExecutor、OTAAuditBridge。

依赖约束：

1. 不直接依赖 runtime/cognition/llm 的实现类。
2. 不直接依赖具体 bootloader 或签名库头文件，由 platform/adapter 层封装。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | contracts 对齐关系 |
|---|---|---|---|
| UpgradePlan | plan_id, requested_by, target_scope, artifact_refs, strategy, validate_only | plan_id 单调可追踪；validate_only 不产生副作用 | requested_by 复用 actor/request_id 语义；对象本身不进入 contracts |
| PackageDescriptor | package_id, package_uri, manifest_version, package_kind, signed_metadata_ref, size_bytes | 描述包来源，不包含已解包明文内容 | infra 私有对象，不扩写共享契约 |
| ArtifactDescriptor | artifact_id, artifact_class, target_slot_group, version, hardware_selector, dependency_refs | artifact_class 仅允许 slot_bound 或 repo_bound 受控集合 | 仅引用既有 ID 和 ErrorInfo 语义 |
| VerifiedPackageManifest | package_id, signature_ok, hash_set, release_counter, compatible_profiles, artifact_list | release_counter 必须单调不回退 | 可映射到 ResultCode，但对象保持私有 |
| PrecheckReport | health_ok, resource_ok, compatibility_ok, policy_ok, blocking_reasons | 任一 false 都需带 blocking_reasons | reason 可通过 ErrorInfo 暴露，不新增 contracts 字段 |
| SlotPlan | active_slot, target_slot, slot_group, switch_policy, confirm_deadline | target_slot 必须为 inactive | infra 私有对象 |
| RollbackToken | rollback_id, previous_boot_target, staged_artifacts, created_at, expires_at | apply 成功切换前必须生成；过期后只能人工恢复 | rollback_id 复用 ID 语义，不进入 contracts |
| InstallEvidence | artifact_id, written_target, checksum, install_ts, installer_version | 必须可回放、可审计 | 仅 evidence_ref 向外暴露 |
| UpgradeOutcome | phase, result_code, rollback_applied, final_version_set, evidence_ref | 结果必须二值可判定；rollback_applied 显式建模 | 可作为 IOTAManager 输出锚点，不引入额外共享语义 |
| OTAStatusSnapshot | last_plan_id, state, active_slot, pending_confirm, last_failure_code, backlog_count | 用于运维查询，不承载业务决策 | 通过 ResultCode/ErrorInfo 暴露失败摘要 |

### 6.6 OTA 包与对象冻结建议

#### 6.6.1 包规范最小冻结面

本轮仅冻结设备侧消费所需的最小字段，不定义云端发布协议：

1. PackageDescriptor 必须能定位 package_uri、manifest_version、package_kind、size_bytes。
2. 包 manifest 必须包含 package_id、artifact 列表、每个 artifact 的版本、长度、hash、兼容硬件选择器、release_counter。
3. 签名校验最小要求为单签名链路占位，签名算法作为配置项冻结，不写入 contracts。
4. 若后续升级到更强元数据链，可在 manifest_version 上增量演进，不破坏本轮对象语义。

#### 6.6.2 artifact class 约束

1. slot_bound：需要 inactive slot 安装与 boot 切换，典型对象为 rootfs、daemon bundle、gateway runtime bundle。
2. repo_bound：安装到 staging repo，再通过原子指针切换生效，典型对象为 tool config、model route、ops policy、prompt registry snapshot。
3. 同一 UpgradePlan 可同时包含两类工件，但执行顺序必须是 repo_bound 先验证后 staged，slot_bound 在最后切换。

### 6.7 核心接口语义定义

建议头文件分布：infra/include/ota/

1. IOTAManager
   - precheck(plan): 返回 PrecheckReport。
   - apply(plan): 返回 UpgradeOutcome。
   - rollback(token): 返回 UpgradeOutcome。
   - query_status(): 返回 OTAStatusSnapshot。

2. IOTAPackageVerifier
   - verify_package(package_descriptor): 返回 VerifiedPackageManifest。
   - verify_artifact(artifact_descriptor): 返回单工件校验结果。

3. IInstallExecutor
   - stage_artifact(artifact, target): 返回 InstallEvidence。
   - activate_plan(slot_plan): 返回 BootSwitchResult。
   - revert_install(rollback_token): 返回 RollbackResult。

4. IBootControlAdapter
   - get_active_target(): 返回当前 boot target。
   - set_next_boot(target): 设置下一次启动目标。
   - mark_boot_success(target): 标记当前启动成功。
   - mark_boot_failed(target): 标记当前启动失败。

5. IOTAHealthProbe
   - probe(): 返回 ProbeResult。

前置条件：

1. apply 前必须先通过 precheck，除 validate_only 场景外不得跳过。
2. rollback 只能接受同一设备上、未过期、与当前状态一致的 rollback token。
3. query_status 不得触发任何副作用。

错误语义建议：

1. INF_E_OTA_PRECHECK_FAILED
2. INF_E_OTA_PACKAGE_INVALID
3. INF_E_OTA_SIGNATURE_INVALID
4. INF_E_OTA_COMPATIBILITY_MISMATCH
5. INF_E_OTA_INSTALL_FAIL
6. INF_E_OTA_BOOT_CONFIRM_TIMEOUT
7. INF_E_OTA_ROLLBACK_FAIL

说明：

1. 当前总设计仅冻结了 INF_E_OTA_VERIFY_FAIL 与 INF_E_OTA_ROLLBACK_FAIL；其余细粒度错误码作为 OTA 子域私有码建议项，落 Build 前需先与 infra 私有码域总表收敛，不直接写入 contracts。

### 6.8 主流程时序

#### 6.8.1 正常升级时序

1. 调用方提交 UpgradePlan 到 OTAManagerFacade.apply。
2. OTAPlanCoordinator 调用 OTAPrecheckService，执行健康、资源、兼容性与策略检查。
3. PackageVerifier 校验包 manifest、签名、hash、release_counter 与 package_kind。
4. ArtifactCompatibilityEvaluator 根据 hardware_selector、Profile 允许集、当前版本集合生成 CompatibilityReport。
5. InstallExecutor 对 repo_bound 工件先写入 staging 区，不立即切主指针。
6. SlotSwitchCoordinator 选择 inactive slot group，生成 SlotPlan 与 RollbackToken。
7. InstallExecutor 将 slot_bound 工件写入 target slot，记录 InstallEvidence。
8. SlotSwitchCoordinator 写入 next boot target。
9. 系统进入重启或切换窗口。
10. BootConfirmationMonitor 在 confirm_deadline 内等待 self-check 与 health gate 通过。
11. 成功后 IBootControlAdapter.mark_boot_success，repo_bound 工件切换主指针，OTAAuditBridge 写完成事件。
12. 返回 UpgradeOutcome，状态为 success，rollback_applied 为 false。

#### 6.8.2 validate_only 时序

1. precheck 通过后只执行 verify 与 compatibility，不写 slot、不切换 boot target。
2. 产出 UpgradeOutcome，phase 为 validated。

### 6.9 异常与恢复时序

#### 6.9.1 异常分类

1. 预检失败：磁盘不足、健康不达标、策略禁用、版本回退风险。
2. 验签失败：签名不合法、hash 不匹配、manifest 缺失字段、release_counter 回退。
3. 安装失败：写入错误、slot 不可用、staging materialization 失败。
4. 启动确认失败：自检失败、confirm 超时、watchdog reset、健康门未通过。
5. 回滚失败：旧 boot target 不可恢复、旧工件缺失、rollback token 无效或过期。

#### 6.9.2 恢复动作

1. 预检失败：立即返回，不产生副作用，记录 audit 和指标。
2. 验签失败：清理临时 staging 区，保留失败证据，不进入安装。
3. 安装失败：若未发生 boot switch，则删除新写入目标；若已生成 rollback token，则执行 rollback 前置恢复。
4. 启动确认失败：RollbackController 调用 set_next_boot(previous_boot_target)，恢复 repo_bound 主指针并标记 boot failed。
5. 回滚失败：上报 critical 级审计与健康事件，系统进入 ota_degraded 状态，禁止继续 apply。

#### 6.9.3 失败兜底

1. 连续 N 次 apply 失败后自动冻结 ota.apply，只允许 precheck 与 query_status。
2. 任何 rollback_fail 都必须写入独立证据 ref，并将 OTAHealthProbe 标记为 degraded。
3. 若 boot confirm 未收到明确成功标记，则默认视为失败路径，不能乐观放行。

### 6.10 配置项与默认策略

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| infra.ota.enabled | false | Profile/部署 | 默认关闭，按设备能力启用 |
| infra.ota.mode | dry_run | 默认/Profile/部署 | dry_run 或 apply_enabled |
| infra.ota.package.verify_required | true | 默认/Profile | 是否强制验签与 hash 校验 |
| infra.ota.package.signature_algorithm | ed25519_placeholder | 默认/部署 | 本轮仅冻结配置键，不冻结实现库 |
| infra.ota.precheck.min_free_space_mb | 256 | Profile/部署 | 最低可用空间阈值 |
| infra.ota.precheck.max_cpu_load_pct | 80 | Profile/部署 | 升级前 CPU 阈值 |
| infra.ota.precheck.require_health_ready | true | 默认/Profile | 是否要求 readiness 为 true |
| infra.ota.install.max_parallel_artifacts | 1 | 默认/Profile | 默认串行安装，避免边缘设备资源抖动 |
| infra.ota.slot.confirm_timeout_sec | 180 | Profile/部署 | 启动确认窗口 |
| infra.ota.rollback.auto_on_confirm_fail | true | 默认/Profile | confirm 失败是否自动回滚 |
| infra.ota.repo_switch.atomic_required | true | 默认/Profile | repo_bound 工件是否要求原子指针切换 |
| infra.ota.allow_downgrade | false | 默认/部署 | 是否允许人工显式降级 |
| infra.ota.audit.required | true | 默认/Profile | 高风险操作审计不可关闭 |

默认策略：

1. edge_minimal 默认仅支持 validate_only 与 repo_bound 工件升级，slot_bound apply 需显式打开。
2. edge_balanced 支持 slot_bound apply，但强制单工件串行安装。
3. desktop_full 可支持更大的 staging 空间与更长 confirm 窗口。

### 6.11 可观测性设计

#### 6.11.1 日志点

1. ota.precheck.start / success / fail
2. ota.package.verify.start / fail
3. ota.install.stage.start / fail / complete
4. ota.slot.switch.request / applied
5. ota.boot.confirm.success / timeout / fail
6. ota.rollback.start / success / fail

#### 6.11.2 指标

1. infra_ota_apply_total
2. infra_ota_apply_fail_total
3. infra_ota_precheck_fail_total
4. infra_ota_verify_fail_total
5. infra_ota_install_duration_ms
6. infra_ota_boot_confirm_timeout_total
7. infra_ota_rollback_total
8. infra_ota_rollback_fail_total
9. infra_ota_pending_confirm_gauge
10. infra_ota_staging_bytes_gauge

#### 6.11.3 追踪

1. apply、precheck、verify、install、switch、confirm、rollback 分别建立独立 span。
2. 跨重启场景通过 plan_id、rollback_id、trace_id 串联证据链。

#### 6.11.4 审计

1. ota.precheck
2. ota.apply
3. ota.switch_boot_target
4. ota.mark_boot_success
5. ota.rollback
6. ota.freeze_apply_channel

审计字段必须包含：actor、plan_id、package_id、target_scope、outcome、evidence_ref、rollback_id。

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 冻结 OTA 私有对象模型 | 新增 ota/OTATypes 头文件组 | 先冻结对象，解除 IOTAManager 设计阻塞 | infra/include/ota/OTATypes.h | unit: OTATypesCompileTest; contract: OTATypeBoundaryTest | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "OTATypesCompileTest|OTATypeBoundaryTest" | 依赖 INF-TODO-009 私有码域收敛 |
| 建立 IOTAManager 对外稳定入口 | 新增 IOTAManager.h | 先固定 precheck/apply/rollback/query_status 语义 | infra/include/IOTAManager.h | unit: OTAInterfaceCompileTest | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R OTAInterfaceCompileTest | 阻塞：package/signature/token 冻结 |
| 建立预检最小闭环 | 新增 OTAPrecheckService 骨架 | 把 health/resource/policy 校验前置显式化 | infra/src/ota/OTAPrecheckService.cpp | unit: OTAPrecheckServiceTest | ctest --test-dir build-ci -R OTAPrecheckServiceTest | 依赖 health/config 查询接口 |
| 建立验签与 manifest 校验闭环 | 新增 PackageVerifier 骨架 | 将签名校验与安装动作解耦 | infra/src/ota/PackageVerifier.cpp | unit: OTAPackageVerifierTest; failure: OTAPackageVerifyFailTest | ctest --test-dir build-ci -R "OTAPackageVerifierTest|OTAPackageVerifyFailTest" | 阻塞：签名算法与 trust anchor 接口 |
| 建立 slot 切换与 confirm 闭环 | 新增 SlotSwitchCoordinator + BootConfirmationMonitor | 形成 A/B 切换和启动确认可验证路径 | infra/src/ota/SlotSwitchCoordinator.cpp; BootConfirmationMonitor.cpp | unit: OTASlotPlanTest; integration: OTABootConfirmIntegrationTest | ctest --test-dir build-ci -R "OTASlotPlanTest|OTABootConfirmIntegrationTest" | 依赖 platform boot control adapter |
| 建立自动回滚闭环 | 新增 RollbackController | 满足架构 8.9 的失败自动回滚要求 | infra/src/ota/RollbackController.cpp | unit: OTARollbackControllerTest; integration: OTAWorkflowTest | ctest --test-dir build-ci -R "OTARollbackControllerTest|OTAWorkflowTest" | 依赖 rollback token 生命周期冻结 |
| 建立 OTA 审计与健康出口 | 新增 OTAAuditBridge + OTAHealthProbe | 保证高风险动作可审计、可观测、可 gate | infra/src/ota/OTAAuditBridge.cpp; OTAHealthProbe.cpp | unit: OTAAuditBridgeTest; OTAHealthProbeTest | ctest --test-dir build-ci -R "OTAAuditBridgeTest|OTAHealthProbeTest" | 依赖 logging/metrics/health 最小接口 |
| 建立 OTA 集成与故障注入测试基线 | 新增 tests/integration/infra/ota 和 tests/unit/infra/ota | 把设计约束转成自动化门禁 | tests/unit/infra/ota/*; tests/integration/infra/ota/* | integration + failure injection | ctest --test-dir build-ci -R "OTAWorkflowTest|OTAFailureInjectionTest" | 阻塞：tests integration 顶层注册点 |

无法立即映射项：

1. Uptane 式双仓库元数据一致性校验：超出本轮范围，列为后续安全增强项。
2. 远程 streaming install：依赖 HTTP range、NBD 或等价平台能力，先不进入 Build 最小交付。

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

建议目录：

1. infra/include/IOTAManager.h
2. infra/include/ota/OTATypes.h
3. infra/include/ota/IOTAPackageVerifier.h
4. infra/include/ota/IInstallExecutor.h
5. infra/include/ota/IBootControlAdapter.h
6. infra/src/ota/OTAManagerFacade.cpp
7. infra/src/ota/OTAPlanCoordinator.cpp
8. infra/src/ota/OTAPrecheckService.cpp
9. infra/src/ota/PackageVerifier.cpp
10. infra/src/ota/ArtifactCompatibilityEvaluator.cpp
11. infra/src/ota/InstallExecutor.cpp
12. infra/src/ota/SlotSwitchCoordinator.cpp
13. infra/src/ota/BootConfirmationMonitor.cpp
14. infra/src/ota/RollbackController.cpp
15. infra/src/ota/OTAAuditBridge.cpp
16. infra/src/ota/OTAHealthProbe.cpp
17. tests/unit/infra/ota/
18. tests/integration/infra/ota/

### 8.2 分阶段实施计划

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| OTA-M1 对象与接口冻结 | Not Started | 新增并冻结 OTATypes 与 IOTAManager 最小骨架 | 本文 6.5、6.6、专项 TODO INF-TODO-015 | infra/include/IOTAManager.h; infra/include/ota/* | unit: OTATypesCompileTest; OTAInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 对象字段、接口签名、错误码映射入口稳定且可编译 |
| OTA-M2 预检与验签骨架 | Not Started | 新增 OTAPrecheckService 与 PackageVerifier 骨架 | 本文 6.3、6.8、6.9 | infra/src/ota/OTAPrecheckService.cpp; PackageVerifier.cpp | unit: OTAPrecheckServiceTest; OTAPackageVerifierTest | ctest --test-dir build-ci -R "OTAPrecheckServiceTest|OTAPackageVerifierTest" | 预检 hard-fail 可阻断，验签失败可快返 |
| OTA-M3 安装与切换闭环 | Not Started | 新增 InstallExecutor、SlotSwitchCoordinator、BootConfirmationMonitor | 本文 6.3、6.8 | infra/src/ota/InstallExecutor.cpp; SlotSwitchCoordinator.cpp; BootConfirmationMonitor.cpp | unit: OTASlotPlanTest; integration: OTABootConfirmIntegrationTest | ctest --test-dir build-ci -R "OTASlotPlanTest|OTABootConfirmIntegrationTest" | inactive slot 选择、切换、confirm 路径可重复验证 |
| OTA-M4 回滚与审计闭环 | Not Started | 新增 RollbackController、OTAAuditBridge、OTAHealthProbe | 本文 6.9、6.11 | infra/src/ota/RollbackController.cpp; OTAAuditBridge.cpp; OTAHealthProbe.cpp | unit: OTARollbackControllerTest; OTAAuditBridgeTest; OTAHealthProbeTest | ctest --test-dir build-ci -R "OTARollbackControllerTest|OTAAuditBridgeTest|OTAHealthProbeTest" | 回滚成功或失败都有证据与健康出口 |
| OTA-M5 集成门与故障注入 | Not Started | 补齐 OTAWorkflow 与失败注入测试 | 本文 9 | tests/integration/infra/ota/* | integration: OTAWorkflowTest; failure: OTAFailureInjectionTest | ctest --test-dir build-ci -R "OTAWorkflowTest|OTAFailureInjectionTest" | 至少覆盖 verify_fail、confirm_timeout、rollback_fail 三类故障 |

### 8.3 阶段完成判定

1. OTA-M1：对象字段、接口签名、错误码域与 contracts 边界测试全部稳定。
2. OTA-M2：precheck 和 verify 任一失败都能返回二值可判定结果且无副作用泄漏。
3. OTA-M3：slot plan 只选择 inactive target，boot confirm 成功与超时路径都可重复验证。
4. OTA-M4：rollback success 与 rollback fail 均有独立审计与指标证据。
5. OTA-M5：集成与 failure injection 测试进入 gate，可阻断回归。

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试层 | 覆盖对象 | 关键用例 | 通过标准 |
|---|---|---|---|
| Unit | OTATypes、IOTAManager、PrecheckService、PackageVerifier、SlotPlan、RollbackController | 版本回退拒绝、签名失败快返、inactive slot 选择、rollback token 过期、confirm 超时 | 断言全部通过，错误码与状态二值可判定 |
| Contract | OTA 与 contracts 交互边界 | ResultCode/ErrorInfo 映射稳定、ID/事件字段不越权、UpgradeOutcome 不扩写共享语义 | 无越权字段，兼容测试通过 |
| Integration | apply -> switch -> confirm -> success；apply -> switch -> fail -> rollback | 完整闭环、跨重启状态恢复、repo_bound 指针切换 | 关键链路可重复执行 |
| Failure Injection | verify_fail、disk_full、slot_busy、boot_confirm_timeout、rollback_fail | 快返错误码、清理 staging、自动回滚、冻结 apply 通道 | 每类故障都有证据、指标与兜底动作 |
| Compatibility | desktop_full / edge_balanced / edge_minimal | dry_run / apply_enabled 差异、资源阈值与工件允许集 | 不出现未声明 breaking 行为 |

### 9.2 质量 Gate 建议清单

| Gate ID | 检查项 | 失败判定 |
|---|---|---|
| OTA-G1 | OTA 单元测试全绿 | 任一 unit 失败即阻断 |
| OTA-G2 | OTA contract 边界测试全绿 | 任一共享语义越权即阻断 |
| OTA-G3 | OTA 集成闭环测试通过 | apply-success 或 rollback 路径任一失败即阻断 |
| OTA-G4 | 失败注入关键用例通过 | verify_fail、confirm_timeout、rollback_fail 任一缺失即阻断 |
| OTA-G5 | Profile 兼容检查通过 | 任一 Profile 产生未声明行为漂移即阻断 |
| OTA-G6 | 审计完整性检查通过 | ota.apply、ota.switch_boot_target、ota.rollback 任一事件缺失即阻断 |

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | 通过 IOTAManager 调用 OTA 的上层模块 | 采用新增可选字段和新接口方法，不删除旧签名 | 先 desktop_full，再 edge_balanced，最后 edge_minimal | 预留 manifest_version、artifact class 扩展、streaming install |
| Medium（若调整 UpgradeOutcome 或错误码域） | 调用方与测试门 | 通过适配层与 contract tests 先双写旧新语义，再逐步切换 | 先 dry_run 双跑，再打开 apply_enabled | 预留更细粒度失败码、双仓库元数据一致性校验 |

演进原则：

1. 默认向后兼容，新增字段优先 optional。
2. 允许先冻结设备侧最小包语义，后续用 manifest_version 增量扩展。
3. 若引入 Uptane 式多元数据链或 streaming 安装，只能作为新能力增补，不能破坏现有 apply/rollback 语义。

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| OTA 包 manifest 字段未冻结 | OTA-M1、OTA-M2 | 明确 package_id、artifact 列表、hash、release_counter、hardware_selector 最小字段 | 先冻结 OTATypes 与 manifest 最小字段表 | 未解阻前只允许 dry_run verify，不允许真实 apply |
| 签名算法与 trust anchor 接口未冻结 | OTA-M2 | 明确签名算法配置键与 secret 提供的 trust anchor 读取接口 | 先用 SignatureVerifierAdapter 抽象占位 | 保留 verify 接口占位，禁用生产验签实现 |
| platform boot control adapter 未具备 | OTA-M3、OTA-M4 | 明确 get_active_target、set_next_boot、mark_boot_success/failed 四个动作 | 先用 mock boot control 走单测和集成夹具 | 未解阻前只支持 repo_bound 工件升级与 dry_run |
| tests integration 顶层注册未稳定 | OTA-M5 | tests/CMakeLists.txt 接入 integration 子目录并建立发现规则 | 先完成 unit/contract/failure-injection 局部测试 | 未解阻前不得宣告 OTA 全链路 gate 完成 |
| rollback token 生命周期与持久化位置未冻结 | OTA-M3、OTA-M4 | 明确 token 存储位置、过期策略与重启恢复规则 | 先冻结 token 字段和内存态流程 | 无法解阻时仅支持同进程内模拟回滚验证 |

### 11.2 风险清单

| 风险 | 等级 | 触发条件 | 缓解动作 |
|---|---|---|---|
| 回滚攻击或版本回退被错误接受 | High | 未校验 release_counter 或版本单调性 | 在 PackageVerifier 中强制 monotonic check，并做 contract/failure 测试 |
| mix-and-match 工件集合不兼容 | High | 多 artifact 升级未做依赖与冲突校验 | 引入 ArtifactCompatibilityEvaluator 和 dependency_refs 校验 |
| 升级后无法确认成功启动 | High | 无 boot confirm 窗口或自检钩子 | 强制 confirm_timeout 与 mark_boot_success 钩子 |
| edge 设备资源不足导致升级失败 | High | staging 空间不足或写入期资源抖动 | precheck 强制资源阈值与单工件串行策略 |
| 审计证据不完整导致问题不可回放 | Medium | apply/rollback 事件未统一携带 evidence_ref | 通过 OTAAuditBridge 统一审计字段与 gate 检查 |

## 12. 未决问题与后续任务

### 12.1 未决问题

1. UpgradePlan 的 target_scope 是否允许跨进程组件批量升级，还是仅支持单设备局部范围。
2. repo_bound 工件的原子指针切换由 config 模块提供通用发布能力，还是由 OTA 内部实现私有切换器。
3. rollback token 需要持久化到本地文件、sqlite，还是先保留 platform 抽象存储。
4. 签名算法首版是 Ed25519、ECDSA 还是仅通过 adapter 抽象占位。
5. boot confirm 成功条件是否只依赖 health ready，还是还要包含指定进程心跳与版本报告。

### 12.2 后续任务建议

1. 先基于本文冻结 OTA 私有对象与包最小字段表，解除 INF-BLK-05 的设计阻塞。
2. 在 docs/todos/DASALL_infrastructure子系统专项TODO.md 中为 OTA 拆出对象冻结、验签骨架、slot 切换、回滚测试四类原子任务。
3. 在 tests/integration/infra/ota 中先实现 mock boot control 场景，再接真实 platform adapter。
4. 待平台与安全要求稳定后，再评估是否引入 Uptane 式双仓库元数据一致性校验或 streaming install。