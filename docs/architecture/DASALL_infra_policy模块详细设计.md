# DASALL infra/policy 模块详细设计（Detailed Design）

版本：v1.0  
日期：2026-03-25  
阶段：Detailed Design  
模块：infra/policy  

说明：本稿对应 infra/security_policy 组件的模块级详细设计，设计输出严格服从 infrastructure 子系统详细设计，不反向改写已冻结 ADR、contracts 边界与蓝图依赖规则。

## 1. 模块概览

### 1.1 模块定位

infra/policy 属于 Infrastructure Layer（Layer 1），负责安全策略的装载、校验、版本化、发布、快照回滚与策略证据输出，为 secret、plugin、diagnostics、ota 等基础设施能力提供统一策略底座，但不承担业务决策、调度裁定或恢复执行。

来源依据：
1. docs/architecture/DASSALL_Agent_architecture.md（3.4.7、5.10、8.8、8.10）
2. docs/architecture/DASALL_Engineering_Blueprint.md（3.12、4.1、4.2、4.3、5.1）
3. docs/architecture/DASALL_infrastructure子系统详细设计.md（6.2、6.3、6.4、6.5、6.6、6.8、6.9、6.10、6.11、7、8）
4. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md（INF-TODO-017、INF-BLK-07）

### 1.2 模块目标

1. 提供可版本化、可审计、可回滚的安全策略治理能力。
2. 将策略判断限定在基础设施约束域，不越权到 Prompt、Tool 或 Runtime 主控策略域。
3. 为上游模块输出稳定的 PolicySnapshot、PolicyDecisionRef 与拒绝原因引用。
4. 支持 Profile 裁剪策略模式、热更新开关与兼容模式切换。
5. 将设计直接映射到可实现目录、接口、测试与 Gate。

### 1.3 边界、职责与依赖方向

1. 上游消费者：infra/secret、infra/plugin、infra/diagnostics、infra/ota、InfraServiceFacade。
2. 同层协同：infra/config 提供策略层输入；infra/audit 记录高风险策略事件；infra/logging、infra/metrics、infra/tracing 输出观测；infra/health 消费策略子系统健康状态。
3. 下游依赖：platform 文件/时间抽象、third_party 可选校验库。
4. 禁止依赖：runtime、cognition、llm、tools、memory、knowledge、services、multi_agent 的实现类。
5. 输出范围：仅输出基础设施策略结果和证据引用，不直接控制执行链路。

---

## 2. 约束清单

### 2.1 Must / Should / Must-Not 约束表

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| POL-C001 | DASSALL_Agent_architecture.md 3.4.7/5.10/8.8 | Must | infra 必须提供安全策略能力，作为基础设施治理子域存在 | 子组件/接口/流程 |
| POL-C002 | DASALL_Engineering_Blueprint.md 4.1/4.2 | Must | 依赖方向单向，infra/policy 不得反向依赖业务模块实现 | 依赖边界 |
| POL-C003 | DASALL_Engineering_Blueprint.md 4.3 | Must | 跨模块调用必须通过 contracts 冻结接口或稳定抽象 | 接口/对象 |
| POL-C004 | ADR-005-architecture-review-baseline.md | Must | contracts 与关键边界冻结优先，不得以 policy 设计反改主架构结论 | 设计治理 |
| POL-C005 | ADR-006-context-orchestrator-vs-prompt-composer.md | Must-Not | policy 不接管 ContextPacket 组装、Prompt 渲染或 PromptPolicy 职责 | 职责边界 |
| POL-C006 | ADR-007-reflection-engine-vs-recovery-manager.md | Must-Not | policy 不做失败归因、恢复裁定与执行准入 | 异常语义 |
| POL-C007 | ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | Must-Not | policy 不拥有全局调度权，只提供受控的基础设施判定结果 | 控制边界 |
| POL-C008 | DASALL_infrastructure子系统详细设计.md 6.3/6.5/6.8 | Must | 策略必须支持 load/apply_patch/snapshot，且策略变更需版本化、可回滚 | 对象/流程 |
| POL-C009 | DASALL_infrastructure子系统详细设计.md 6.5 | Must-Not | 不把规则引擎实现细节、匹配算法、线程模型写入 contracts 共享对象 | contracts 边界 |
| POL-C010 | DASALL_contracts冻结实施计划.md 6/8 | Must | 默认向后兼容，新增字段优先 optional，breaking 必须走评审 | 版本演进 |
| POL-C011 | DASALL_contracts冻结TODO总表.md M5 | Must | 仅引用 contracts::policy/PolicyDecision 语义，不扩写共享字段 | 对齐策略 |
| POL-C012 | DASALL_工程协作与编码规范.md 3.6 | Must | 禁止吞错，策略加载、拒绝、回滚失败必须可观测 | 错误处理 |
| POL-C013 | DASALL_工程协作与编码规范.md 3.7 | Should | 新增公共接口应同步增加 unit/contract/integration 测试 | 测试门禁 |
| POL-C014 | DASALL_Engineering_Blueprint.md 5.1 | Must | Profile 只能裁剪能力和替换实现，不得绕过 Audit 与 Runtime 主控链路 | 配置/裁剪 |
| POL-C015 | OPA/Rego、AWS IAM policy versioning、Kubernetes admission policy 实践 | Should | 策略应具备显式版本、优先级、冲突裁定、只读 dry-run 与可回滚快照 | 规则模型/演进 |

### 2.2 约束抽取结论

1. Must：有独立策略子域、可版本化、可审计、可回滚、兼容优先。
2. Should：显式优先级、dry-run 校验、标准化拒绝原因、完整测试门禁。
3. Must-Not：不越权到 Prompt/Tool/Runtime 策略、不污染 contracts、不反向依赖业务实现。

---

## 3. 现状与缺口

### 3.1 当前实现状态识别

代码与工程现状证据：
1. infra/CMakeLists.txt 当前仅编译 src/placeholder.cpp。
2. infra/include 为空，未落盘 ISecurityPolicyManager、PolicySnapshot 等接口/对象。
3. file_search 结果显示 infra 真实代码仅有 infra/src/placeholder.cpp，未见 security_policy 目录与实现。
4. docs/architecture/DASALL_infrastructure子系统详细设计.md 已明确 SecurityPolicyManager 为 Must 独立组件，并进入 Design -> Build 映射。
5. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md 已将 INF-TODO-017 标记为可执行 L2 任务，同时指出 INF-BLK-07：规则 schema 与冲突裁定顺序未冻结。

### 3.2 现状-目标差距表

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| policy 对外接口冻结 | 缺失 | 无 ISecurityPolicyManager，调用方无法稳定接入 | High | P0 |
| 核心策略对象冻结 | 缺失 | PolicyBundle/PolicyPatch/PolicySnapshot/RuleDescriptor 未定义 | High | P0 |
| 规则 schema | 缺失 | 无规则类型、匹配条件、动作语义、优先级模型 | High | P0 |
| 策略冲突裁定顺序 | 缺失 | allow/deny/compat/fallback 缺乏稳定优先级 | High | P0 |
| 策略热更新与回滚 | 缺失 | 无版本链、无 last-known-good、无 apply_patch 流程 | High | P0 |
| 可观测性与审计 | 缺失 | 策略加载、拒绝、回滚、dry-run 无日志/指标/审计出口 | High | P0 |
| 测试基线 | 缺失 | 无 unit/contract/integration/failure injection 用例 | High | P0 |
| 与子系统设计和 TODO 对齐 | 部分存在 | 仅有 SecurityPolicyManager 名称和任务锚点，未落模块级文档 | Medium | P0 |

### 3.3 风险冲突识别

| 冲突类型 | 描述 | 影响 | 风险等级 |
|---|---|---|---|
| 边界冲突 | 将 Tool Policy Gate 或 PromptPolicy 的语义搬入 infra/policy | 破坏 ADR-006/007/008 与模块边界 | High |
| 语义重复 | 在 infra/policy 重新定义 contracts::PolicyDecision 字段 | 造成 contracts 漂移与返工 | High |
| 依赖反转 | 为了拿业务上下文直接 include runtime/tools 实现 | 破坏分层与裁剪能力 | High |
| 恢复越权 | 策略拒绝后直接触发 rollback/retry/abort | 侵入 Runtime 恢复裁定域 | High |

---

## 4. 候选方案对比

### 4.1 候选方案说明

1. 方案 A：静态策略文件直读直判。单文件加载，规则顺序即裁定顺序，不保留版本快照。
2. 方案 B：版本化策略中心。由 PolicyLoader、SchemaValidator、ConflictResolver、SnapshotStore、DecisionProjector 组成，支持 dry-run、热更新和回滚。
3. 方案 C：通用策略引擎优先。直接嵌入第三方规则引擎，以 DSL/解释执行为核心。

### 4.2 候选方案对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| A 静态直读直判 | 中 | 中 | 低 | 无版本链、无 dry-run、难回滚、难观测 | 淘汰：仅适合 PoC |
| B 版本化策略中心 | 高 | 高 | 中 | 需要补齐对象模型和裁定规则，但可分阶段落地 | 采纳 |
| C 通用规则引擎优先 | 中 | 中高 | 高 | 引擎 DSL 与执行模型易反向绑死实现，当前准备不足 | 暂不采纳，列为 v2 |

### 4.3 行业方案匹配结论

1. OPA/Rego 的经验说明：策略定义、校验、评估、dry-run 应显式分层，而不是把所有逻辑塞进单次 evaluate。
2. AWS IAM/Kubernetes Admission Policy 的经验说明：显式 deny 优先级、版本化与审计轨迹是长期演进前提。
3. 对当前 DASALL 阶段而言，最优路径不是先引入通用 DSL，而是先冻结对象、冲突规则、快照与接口，再保留后续替换引擎的空间。

---

## 5. 决策结论

### 5.1 最终选型

采纳方案 B：版本化策略中心。

### 5.2 选择依据

1. 能与 infrastructure 子系统详细设计中的 SecurityPolicyManager、PolicySnapshot、回滚语义直接对齐。
2. 能保持 Layer 1 边界，只输出基础设施约束结果与证据引用，不侵入上层主控。
3. 能满足 INF-BLK-07 对“规则 schema 与冲突裁定顺序”的解阻要求。
4. 能分阶段落地：先对象与接口，再主流程，再热更新/回滚，再测试与 Gate。

### 5.3 放弃其他方案理由

1. 放弃 A：无法满足版本化、回滚、dry-run 和审计要求。
2. 放弃 C：当前仓库为空骨架，先引入通用策略引擎会把实现细节提前固化，反而削弱演进性。

### 5.4 与架构、ADR、contracts 的一致性说明

1. 架构一致：infra/policy 仅提供基础设施策略，不做业务审批、主流程裁定或认知语义判断。
2. ADR 一致：不碰 Context/Prompt，不碰 Reflection/Recovery，不碰全局调度权。
3. contracts 一致：只引用 PolicyDecision 的 allow/deny/require_confirmation 语义，不把规则 DSL、优先级算法、快照存储等实现细节写入 contracts。

---

## 6. 详细设计

### 6.1 职责边界

infra/policy 负责：
1. 装载 PolicyBundle 并进行结构校验、语义校验、来源校验。
2. 基于明确的优先级与冲突裁定规则生成 PolicySnapshot。
3. 提供 apply_patch、dry_run、snapshot、rollback 能力。
4. 为 secret/plugin/diagnostics/ota 等子域提供策略查询与决策投影。
5. 输出策略版本、拒绝原因、证据引用、可观测事件和审计事件。

infra/policy 不负责：
1. 不负责 Tool 执行治理链路中的最终 Policy Gate 裁定。
2. 不负责 Prompt 可见范围与内容裁剪。
3. 不负责 Runtime 恢复准入、调度推进或用户确认流。
4. 不负责业务对象授权语义定义。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| SecurityPolicyManager | 对外统一入口，管理生命周期、加载、热更新、快照与回滚 |
| PolicyLoader | 从 config/profile/deploy/runtime patch 读取策略包 |
| PolicySchemaValidator | 校验规则类型、字段完整性、版本兼容、来源合法性 |
| PolicyConflictResolver | 根据优先级、作用域、模式生成最终有效规则集 |
| PolicySnapshotStore | 保存 current、history、last-known-good 快照 |
| PolicyDecisionProjector | 将 infra 私有规则结果投影为 PolicyDecisionRef/DecisionTrace |
| PolicyAuditBridge | 记录 load/apply_patch/rollback/deny/dry_run 高风险事件 |
| PolicyMetricsBridge | 输出 reload_total、invalid_total、deny_total、rollback_total 等指标 |
| PolicyHealthProbe | 输出 ready/degraded/unavailable、最近失败原因与快照代次 |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| SecurityPolicyManager | InfraServiceFacade、ConfigCenter、管理命令 | SnapshotStore、Projector、Audit/Metrics/Health | 返回 ResultCode + ErrorInfo 或 PolicyOpResult |
| PolicyLoader | 默认/Profile/部署/运行时 patch | PolicyBundle | source_id、version、checksum 可追溯 |
| PolicySchemaValidator | PolicyBundle 或 PolicyPatch | ValidationReport | 失败必须可定位到 rule_id/field_path |
| PolicyConflictResolver | 通过校验的规则集合 | EffectivePolicySet | deny 优先、优先级显式、冲突可解释 |
| PolicySnapshotStore | EffectivePolicySet + metadata | current/history/LKG snapshot | generation 单调递增、可回滚 |
| PolicyDecisionProjector | QueryContext + EffectivePolicySet | PolicyDecisionRef/DecisionTrace | 只投影结果与证据，不泄露实现细节 |
| PolicyAuditBridge | 关键操作与拒绝结果 | AuditService | 高风险操作必须审计 |
| PolicyMetricsBridge | 管线统计 | MetricsService | 标签白名单治理 |
| PolicyHealthProbe | current snapshot + recent failures | HealthMonitor | 只输出健康事实与原因 |

### 6.4 子组件依赖关系

1. SecurityPolicyManager -> PolicyLoader -> PolicySchemaValidator -> PolicyConflictResolver -> PolicySnapshotStore。
2. SecurityPolicyManager -> PolicyDecisionProjector，用于向 secret/plugin/diagnostics/ota 暴露查询结果。
3. SecurityPolicyManager、PolicySnapshotStore -> PolicyAuditBridge、PolicyMetricsBridge、PolicyHealthProbe。
4. PolicyLoader 仅依赖 ConfigCenter 抽象，不直接读取上层模块内部配置结构。
5. secret/plugin/diagnostics/ota 仅通过 ISecurityPolicyManager 访问策略快照与查询，不感知内部规则引擎。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | contracts 对齐关系 |
|---|---|---|---|
| PolicyBundle | bundle_id, schema_version, source, checksum, rules, generated_at | source/checksum 必填；rules 不可为空列表 | infra 私有对象，不进入 contracts |
| PolicyRuleDescriptor | rule_id, domain, subject, action, target_selector, effect, priority, mode, conditions, reason_code | rule_id 唯一；priority 越小优先级越高；effect 仅 allow/deny/require_confirmation/observe | effect 投影到 contracts::PolicyDecision 语义 |
| PolicyPatch | patch_id, base_generation, operations, actor, reason | 仅允许白名单操作；base_generation 必须匹配当前快照 | infra 私有 |
| PolicySnapshot | snapshot_id, generation, version, mode, effective_rules, created_at, source_chain, lkg_ref | generation 单调递增；可回滚到 lkg_ref | 输出 DecisionRef 时只引用 snapshot_id/generation |
| PolicyQueryContext | module, operation, target_type, target_ref, actor_ref, request_id, session_id, trace_id, task_id, profile_id | 缺失字段允许 unknown，不允许空语义漂移 | 仅复用 contracts 横切 ID 语义 |
| PolicyDecisionRef | decision, reason_code, matched_rule_ids, snapshot_id, generation, evidence_ref, warnings | decision 仅允许 allow/deny/require_confirmation | 对齐 contracts::PolicyDecision |
| ValidationReport | blocking_errors, warnings, invalid_rule_ids, field_paths | blocking_errors 非空时禁止激活 | infra 私有 |
| PolicyOpResult | applied, rolled_back, dry_run, snapshot_id, generation, error_info | applied=false 必有 error_info | 错误映射到 ResultCode/ErrorInfo |

规则域建议：
1. secret_access
2. plugin_load
3. diagnostics_command
4. ota_apply
5. ota_rollback
6. policy_admin

规则 effect 建议：
1. allow
2. deny
3. require_confirmation
4. observe

### 6.6 核心接口语义定义

建议头文件位置：infra/include/policy/

1. ISecurityPolicyManager
   - load_policy(const PolicyBundle&) -> PolicyOpResult
   - apply_patch(const PolicyPatch&) -> PolicyOpResult
   - dry_run_patch(const PolicyPatch&) -> ValidationReport
   - snapshot() -> PolicySnapshot
   - rollback(snapshot_id) -> PolicyOpResult
   - evaluate(const PolicyQueryContext&) -> PolicyDecisionRef

2. IPolicyLoader
   - load_from_sources() -> PolicyBundle

3. IPolicySchemaValidator
   - validate_bundle(const PolicyBundle&) -> ValidationReport
   - validate_patch(const PolicySnapshot&, const PolicyPatch&) -> ValidationReport

4. IPolicySnapshotStore
   - commit(const PolicySnapshot&) -> PolicyOpResult
   - current() -> PolicySnapshot
   - last_known_good() -> PolicySnapshot
   - get_by_id(snapshot_id) -> PolicySnapshot

前置条件：
1. ConfigCenter 已完成基础配置加载。
2. contracts 横切标识语义已可用。
3. 策略包 schema_version 与当前 manager 支持版本兼容。

后置条件：
1. load/apply_patch 成功后必生成新 generation 的 PolicySnapshot。
2. 任一失败路径必产生日志、指标或审计至少一种可观测输出。
3. evaluate 只返回决策引用，不直接执行动作。

错误语义（建议）:
1. INF_E_POLICY_BUNDLE_INVALID
2. INF_E_POLICY_SCHEMA_UNSUPPORTED
3. INF_E_POLICY_CONFLICT_UNRESOLVED
4. INF_E_POLICY_PATCH_BASE_MISMATCH
5. INF_E_POLICY_SNAPSHOT_NOT_FOUND
6. INF_E_POLICY_ROLLBACK_FAILED
7. INF_E_POLICY_QUERY_DENIED
8. INF_E_POLICY_SOURCE_UNAVAILABLE
9. INF_E_POLICY_STORE_COMMIT_FAILED
10. INF_E_POLICY_DRYRUN_REJECTED

### 6.7 主流程时序

正常加载流程：
1. InfraServiceFacade.init 调用 SecurityPolicyManager.load_policy。
2. PolicyLoader 从默认/Profile/部署层拼装 PolicyBundle。
3. PolicySchemaValidator 校验 bundle 结构、规则域、effect、priority、条件字段。
4. PolicyConflictResolver 生成 EffectivePolicySet，并输出冲突说明。
5. PolicySnapshotStore 写入新快照，generation 自增，更新 current 与 LKG。
6. PolicyAuditBridge 记录 load success 审计事件。
7. PolicyMetricsBridge 增加 reload_total、active_generation 指标。
8. SecurityPolicyManager 对外返回 PolicyOpResult{applied=true}。

策略查询流程：
1. secret/plugin/diagnostics/ota 构造 PolicyQueryContext。
2. SecurityPolicyManager.evaluate 读取 current snapshot。
3. PolicyDecisionProjector 按 domain -> target_selector -> priority -> effect 投影结果。
4. 返回 PolicyDecisionRef，附 matched_rule_ids、reason_code、snapshot_id、evidence_ref。
5. 调用方据此决定是否继续自身基础设施动作。

热更新流程：
1. 管理入口提交 PolicyPatch。
2. SecurityPolicyManager.dry_run_patch 先执行 validate。
3. validate 通过后 apply_patch，生成候选新快照。
4. 新快照 commit 成功后替换 current，旧快照保留 history。
5. Audit/Metrics/Health 同步更新。

### 6.8 异常与恢复时序

异常分类：
1. 输入异常：bundle 缺字段、未知 rule domain、priority 冲突无法裁定。
2. 来源异常：配置层策略源不可读、checksum 不匹配。
3. 存储异常：快照提交失败、history/LKG 读写失败。
4. 查询异常：当前快照缺失、rule effect 非法、query_context 不完整。

恢复动作：
1. load_policy 失败：保持 current/LKG 不变，返回 INF_E_POLICY_BUNDLE_INVALID 或 INF_E_POLICY_SOURCE_UNAVAILABLE。
2. apply_patch 失败：不替换 current，保留旧快照，并记录 dry-run/patch failure 审计事件。
3. commit 失败：回退到 LKG，标记 degraded，并增加 rollback_total/commit_fail_total。
4. evaluate 异常：返回显式 deny 或 query denied，附 reason_code，不做执行补偿。

兜底策略：
1. current snapshot 不可用时，仅允许读取 LKG，禁止继续热更新。
2. 连续 N 次 patch 失败进入 policy_safe_mode：只允许静态只读查询，禁止 apply_patch。
3. 所有拒绝必须给出 reason_code 和 snapshot_id，禁止“静默拒绝”。

### 6.9 配置项与默认策略

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| infra.security_policy.enabled | true | 默认/Profile/部署 | 是否启用策略治理 |
| infra.security_policy.mode | strict | 默认/Profile/部署 | strict/compat 两档 |
| infra.security_policy.hot_reload | true | Profile/部署 | 是否允许运行时 patch |
| infra.security_policy.max_history_snapshots | 16 | Profile/部署 | 历史快照保留数 |
| infra.security_policy.default_effect | deny | 默认/Profile | 未命中规则时的默认 effect |
| infra.security_policy.priority_order | deny-first | 默认/Profile | deny-first 或 explicit-priority |
| infra.security_policy.require_checksum | true | 默认/Profile/部署 | 是否强制校验 checksum |
| infra.security_policy.dry_run_required | true | 默认/Profile/部署 | patch 前是否必须 dry-run |
| infra.security_policy.patch_actor_allowlist | [] | 部署/运行时 | 允许提交 patch 的 actor |
| infra.security_policy.safe_mode_threshold | 3 | Profile/部署 | 连续失败后进入 safe mode 的阈值 |
| infra.security_policy.snapshot.persist_lkg | true | 默认/Profile | 是否持久化 LKG |

模式建议：
1. strict：未知规则域、冲突未决、缺失 checksum 一律拒绝激活。
2. compat：允许部分未知字段保留但不生效，仍要求核心规则合法。

### 6.10 可观测性设计

日志点：
1. 策略加载开始/成功/失败。
2. patch dry-run 通过/拒绝。
3. snapshot commit/rollback。
4. query deny/require_confirmation 命中。
5. safe_mode 进入/退出。

指标：
1. infra_policy_reload_total
2. infra_policy_invalid_total
3. infra_policy_patch_total
4. infra_policy_patch_reject_total
5. infra_policy_deny_total
6. infra_policy_rollback_total
7. infra_policy_active_generation
8. infra_policy_safe_mode_total

追踪：
1. load_policy、apply_patch、rollback、evaluate 各自建立 span。
2. span 附 snapshot_id、generation、rule_count、decision。

审计：
1. policy load/apply_patch/rollback 为强制审计事件。
2. 高风险 deny（例如拒绝 ota_apply、plugin_load）写审计证据。
3. 审计字段至少包含 actor、action、target、snapshot_id、generation、reason_code、outcome。

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 冻结 policy 对外接口 | 新增 ISecurityPolicyManager 与 policy 头文件 | 先稳定边界，再推进实现 | infra/include/policy/ISecurityPolicyManager.h | unit: PolicyInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 依赖 infra 目标接线 |
| 冻结核心对象与规则模型 | 新增 PolicyTypes | 把 schema、snapshot、query、decision 拆成稳定对象 | infra/include/policy/PolicyTypes.h | unit: PolicyTypesTest; contract: PolicyDecisionBoundaryTest | ctest --test-dir build-ci -R "PolicyTypesTest|PolicyDecisionBoundaryTest" | 依赖 contracts::PolicyDecision 边界 |
| 建立 schema 校验与冲突裁定骨架 | 新增 PolicySchemaValidator 与 PolicyConflictResolver | 解 INF-BLK-07 的核心步骤 | infra/src/policy/PolicySchemaValidator.cpp; infra/src/policy/PolicyConflictResolver.cpp | unit: PolicySchemaValidatorTest; unit: PolicyConflictResolverTest | ctest --test-dir build-ci -R "PolicySchemaValidatorTest|PolicyConflictResolverTest" | 阻塞：规则 domain 枚举与优先级表需冻结 |
| 建立 snapshot 与回滚闭环 | 新增 PolicySnapshotStore | 支撑 generation/LKG/history | infra/src/policy/PolicySnapshotStore.cpp | unit: PolicySnapshotStoreTest; failure: PolicyRollbackFailureTest | ctest --test-dir build-ci -R "PolicySnapshotStoreTest|PolicyRollbackFailureTest" | 依赖 LKG 存储策略 |
| 建立 manager 主链 | 新增 SecurityPolicyManager | 打通 load/apply_patch/dry_run/evaluate | infra/src/policy/SecurityPolicyManager.cpp | unit: SecurityPolicyManagerTest; integration: InfraPolicyLifecycleTest | ctest --test-dir build-ci -R "SecurityPolicyManagerTest|InfraPolicyLifecycleTest" | 依赖测试 integration 拓扑 |
| 建立可观测与审计桥接 | 新增 PolicyAuditBridge/PolicyMetricsBridge/PolicyHealthProbe | 将失败与高风险操作转成可观测信号 | infra/src/policy/PolicyAuditBridge.cpp; infra/src/policy/PolicyMetricsBridge.cpp | unit: PolicyAuditBridgeTest; unit: PolicyMetricsBridgeTest | ctest --test-dir build-ci -R "PolicyAuditBridgeTest|PolicyMetricsBridgeTest" | 依赖 audit/metrics/health 最小接口 |
| 建立 policy 测试与 Gate | 新增 tests/unit/infra/policy 与 contract/integration 用例 | 把模块级约束转成自动门禁 | tests/unit/infra/policy/*; tests/contract/infra/* | unit/contract/integration/failure injection | ctest --test-dir build-ci -L "unit|contract" | 阻塞：tests 顶层 integration 尚未接线 |

不可立即映射项：
1. 通用 DSL/解释执行引擎接入：当前不是最小交付，列为 v2。
2. 跨进程策略分发：当前 Layer 1 范围内不纳入。

---

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

建议目录：
1. infra/include/policy/
2. infra/src/policy/
3. tests/unit/infra/policy/
4. tests/contract/infra/
5. tests/integration/infra/policy/

建议文件：
1. infra/include/policy/ISecurityPolicyManager.h
2. infra/include/policy/PolicyTypes.h
3. infra/include/policy/PolicyErrors.h
4. infra/src/policy/SecurityPolicyManager.cpp
5. infra/src/policy/PolicySchemaValidator.cpp
6. infra/src/policy/PolicyConflictResolver.cpp
7. infra/src/policy/PolicySnapshotStore.cpp
8. infra/src/policy/PolicyAuditBridge.cpp

### 8.2 分阶段实施与完成判定

| 阶段 | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| POL-M1 接口与对象冻结 | Not Started | 冻结 PolicyTypes、PolicyErrors、ISecurityPolicyManager | 本文档 6.5/6.6 | include/policy/* | unit + contract | cmake --build build-ci --target dasall_infra | 头文件齐备且可编译 |
| POL-M2 规则校验与冲突裁定 | Not Started | 落地 schema validator 与 conflict resolver 骨架 | 本文档 6.2/6.8 | src/policy/PolicySchemaValidator.cpp; PolicyConflictResolver.cpp | unit | ctest --test-dir build-ci -R "PolicySchemaValidatorTest|PolicyConflictResolverTest" | 规则非法与冲突路径可判定 |
| POL-M3 快照与回滚闭环 | Not Started | 落地 snapshot store、LKG、rollback | 本文档 6.5/6.8 | src/policy/PolicySnapshotStore.cpp | unit + failure injection | ctest --test-dir build-ci -R "PolicySnapshotStoreTest|PolicyRollbackFailureTest" | generation/LKG/rollback 可验证 |
| POL-M4 Manager 主链与查询投影 | Not Started | 打通 load/apply_patch/dry_run/evaluate | 本文档 6.7 | src/policy/SecurityPolicyManager.cpp | unit + integration | ctest --test-dir build-ci -R "SecurityPolicyManagerTest|InfraPolicyLifecycleTest" | 生命周期与查询闭环可运行 |
| POL-M5 可观测与 Gate | Not Started | 接线 audit/metrics/health 与测试门禁 | 本文档 6.10/9 | tests/* + bridge files | unit/contract/integration | ctest --test-dir build-ci -L "unit|contract" | Gate 可重复执行 |

### 8.3 原子实施任务（建议级）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| POL-T001 | Not Started | 新增 PolicyTypes 对象头文件 | 本文档 6.5 | infra/include/policy/PolicyTypes.h | PolicyTypesTest | ctest --test-dir build-ci -R PolicyTypesTest | 对象字段与约束齐备 |
| POL-T002 | Not Started | 新增 PolicyErrors 错误码头文件 | 本文档 6.6 | infra/include/policy/PolicyErrors.h | PolicyErrorMappingContractTest | ctest --test-dir build-ci -R PolicyErrorMappingContractTest | 错误码稳定且映射清晰 |
| POL-T003 | Not Started | 新增 ISecurityPolicyManager 接口 | 本文档 6.6 | infra/include/policy/ISecurityPolicyManager.h | PolicyInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 接口可编译且不越界 |
| POL-T004 | Not Started | 实现 PolicySchemaValidator 骨架 | 本文档 6.2/6.8 | infra/src/policy/PolicySchemaValidator.cpp | PolicySchemaValidatorTest | ctest --test-dir build-ci -R PolicySchemaValidatorTest | 非法规则可拒绝 |
| POL-T005 | Not Started | 实现 PolicyConflictResolver 骨架 | 本文档 6.2/6.8 | infra/src/policy/PolicyConflictResolver.cpp | PolicyConflictResolverTest | ctest --test-dir build-ci -R PolicyConflictResolverTest | 冲突裁定顺序可验证 |
| POL-T006 | Not Started | 实现 PolicySnapshotStore 骨架 | 本文档 6.5/6.8 | infra/src/policy/PolicySnapshotStore.cpp | PolicySnapshotStoreTest | ctest --test-dir build-ci -R PolicySnapshotStoreTest | generation/LKG 可验证 |
| POL-T007 | Not Started | 实现 SecurityPolicyManager 主链骨架 | 本文档 6.7 | infra/src/policy/SecurityPolicyManager.cpp | SecurityPolicyManagerTest | ctest --test-dir build-ci -R SecurityPolicyManagerTest | load/apply/query 闭环可验证 |
| POL-T008 | Not Started | 新增 policy contract 测试 | 本文档 5.4/6.5 | tests/contract/infra/PolicyDecisionBoundaryTest.cpp | PolicyDecisionBoundaryTest | ctest --test-dir build-ci -R PolicyDecisionBoundaryTest | 不污染 contracts 语义 |
| POL-T009 | Not Started | 新增 policy integration smoke | 本文档 6.7/9.1 | tests/integration/infra/InfraPolicyLifecycleTest.cpp | InfraPolicyLifecycleTest | ctest --test-dir build-ci -R InfraPolicyLifecycleTest | 与 infra 入口链路对接成功 |

---

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试层 | 覆盖对象 | 关键用例 | 通过标准 |
|---|---|---|---|
| Unit | PolicyTypes、SchemaValidator、ConflictResolver、SnapshotStore、Manager | 规则缺字段、优先级冲突、LKG 回滚、默认 deny | 断言通过，错误码明确 |
| Contract | PolicyDecision 边界、ID 语义、错误码映射 | 不扩写 contracts::PolicyDecision；snapshot_id/generation 引用稳定 | 无越权字段 |
| Integration | SecurityPolicyManager 与 ConfigCenter/Audit/Health 最小装配 | load -> snapshot -> evaluate -> patch -> rollback | 主链可重复执行 |
| Failure Injection | source unavailable、commit fail、patch base mismatch、rollback fail | degraded、拒绝、回退、safe_mode | 每条故障有可观测证据 |
| Compatibility | strict/compat 模式与不同 profile | strict 拒绝、compat 降级接受 | 行为符合模式定义 |

### 9.2 Gate 建议清单

| Gate ID | 检查项 | 失败判定 |
|---|---|---|
| POL-G1 | policy unit 全绿 | 任一 unit 失败即阻断 |
| POL-G2 | contracts 边界校验通过 | 出现 contracts 语义越权即阻断 |
| POL-G3 | patch/rollback failure injection 全绿 | 任一失败路径无兜底即阻断 |
| POL-G4 | strict/compat 模式行为校验通过 | 模式行为与定义不一致即阻断 |
| POL-G5 | breaking review 结论存在 | 公共接口或对象变更无评审即阻断 |

---

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | secret/plugin/diagnostics/ota | 通过新增 optional 字段和 snapshot generation 演进 | 先 desktop_full，再 edge_balanced | 预留 DSL/规则引擎替换层 |
| Medium（规则 schema 变化时） | policy 资产维护方、配置输入方 | schema_version 升级 + validator 双读窗口 | strict/compat 双模式灰度 | 预留跨进程策略分发 |

演进原则：
1. 优先新增字段，不修改旧字段含义。
2. effect 与 domain 枚举新增必须保留向后兼容路径。
3. 如果后续引入规则 DSL，引擎必须隐藏在 PolicySchemaValidator/ConflictResolver 之后，不改变 ISecurityPolicyManager 公共接口。

---

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| B-POL-01 规则 domain 与 effect 枚举未冻结 | POL-T001、POL-T004、POL-T005 | 冻结 domain/effect 名单与语义 | 在本模块设计评审中确认 secret_access/plugin_load/diagnostics_command/ota_* | 首版仅支持冻结子集 |
| B-POL-02 冲突裁定顺序未评审通过 | POL-T005、POL-T007 | 明确 priority_order 与 deny-first 规则 | 在 detailed design 评审中冻结裁定矩阵 | 默认 deny-first，禁止热更新 |
| B-POL-03 tests 顶层 integration 未接线 | POL-M4、POL-T009 | tests/CMakeLists 接入 integration | 新增 integration 注册规则 | integration 延后，仅保留 unit/contract |
| B-POL-04 audit/metrics/health 最小接口未冻结 | POL-M5 | 桥接接口签名冻结 | 先以 stub/logger 占位 | 仅保留本地日志和内部计数 |
| B-POL-05 LKG 持久化介质未冻结 | POL-M3 | 冻结 file/memory 方案 | 首版以内存 LKG 落地 | 重启后不承诺 LKG 持久化 |

### 11.2 风险清单

| 风险 | 等级 | 触发条件 | 缓解动作 |
|---|---|---|---|
| 规则域越界 | High | 把 Prompt/Tool 运行治理规则写入 infra/policy | 评审门禁 + domain 白名单 |
| 默认 allow 造成安全旁路 | High | 未命中规则默认 allow | 默认 effect 固定 deny |
| 冲突裁定不可解释 | High | 多条规则命中但无 matched_rule_ids/reason_code | 强制输出 DecisionTrace |
| patch 破坏当前运行态 | Medium | apply_patch 直接替换 current 且不 dry-run | 强制 dry-run_required |
| safe_mode 设计缺失 | Medium | 连续失败后仍允许 patch | 连续失败阈值触发 safe_mode |

---

## 12. 未决问题与后续任务

### 12.1 未决问题

1. strict 与 compat 模式下，对未知 effect/未知 domain 的兼容边界是否完全一致。
2. LKG 首版是否只做内存态，还是直接引入本地持久化文件。
3. apply_patch 的白名单操作是否允许删除规则，还是仅允许 add/update/disable。
4. PolicyDecisionRef 中 matched_rule_ids 是否需要分页或裁剪上限。
5. 与 plugin/ota 的签名来源策略是否需要单独 rule domain 子分类。

### 12.2 后续任务建议

1. 在 docs/todos 下新增 policy 组件专项 TODO，把 POL-T001~POL-T009 映射为最小原子任务。
2. 先执行接口/对象冻结和 schema/冲突裁定任务，优先解掉 INF-BLK-07。
3. 与 infra/config、infra/audit、infra/health 对齐最小桥接接口，减少后续实现期返工。
4. 在 strict/compat 两档上各补一组 failure injection 用例，确保模式差异可回归。