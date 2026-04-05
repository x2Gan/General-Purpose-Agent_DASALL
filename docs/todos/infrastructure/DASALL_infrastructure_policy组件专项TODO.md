# DASALL infrastructure 子系统 policy 组件专项 TODO

最近更新时间：2026-04-05  
阶段：Detailed Design -> Special TODO  
适用范围：infra/policy

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_infra_policy模块详细设计.md
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
13. docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md
14. docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md
15. docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md
16. docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md
17. docs/todos/profiles/DASALL_profiles子系统专项TODO.md
18. 当前代码与测试现状：infra/CMakeLists.txt、infra/include/、infra/src/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt

生成原则：

1. 不改写已冻结 ADR-005/006/007/008 结论。
2. 不越过 infra/policy 组件边界扩张到 runtime、prompt、tool 或业务模块。
3. 每项任务必须具备代码目标、测试目标、验收命令三件套。
4. 设计证据不足处只输出 Blocked 与补设计前置阻塞，不伪造实现任务。
5. contracts 侧当前未见可直接 include 的 policy 共享头文件，因此本专项 TODO 只允许写“语义对齐”和“映射门禁”，不允许假定已有 contracts/policy 代码可用。

## 2. 子系统目标与范围

### 2.1 组件目标

1. 提供可版本化、可审计、可回滚的基础设施安全策略治理能力。
2. 向 secret、plugin、diagnostics、ota 等上游消费者输出稳定的 PolicySnapshot、PolicyDecisionRef 与拒绝原因引用。
3. 支持 load、dry_run_patch、apply_patch、snapshot、rollback、evaluate 主链路。
4. 支持 strict 与 compat 两档模式、Profile 裁剪、热更新开关与 safe_mode 兜底。
5. 保持 Layer 1 边界，只输出基础设施约束结果和证据引用，不接管主控裁定。

### 2.2 范围边界

纳入范围：

1. policy 核心对象、错误码、接口、配置模型、主流程与异常流程。
2. PolicyLoader、PolicySchemaValidator、PolicyConflictResolver、PolicySnapshotStore、PolicyDecisionProjector、SecurityPolicyManager 的专项拆解。
3. policy 的 CMake 接线、unit/contract 注册点、集成测试门禁与交付证据。
4. 与 audit、metrics、health、config、profiles 的最小桥接依赖和阻塞项。

不纳入范围：

1. PromptPolicy、PolicyGate、Runtime 恢复准入、全局调度和用户确认流的实现。
2. 规则 DSL 解释器、第三方通用规则引擎接入、跨进程策略分发等 v2 项。
3. contracts 共享语义对象扩写或反向定义。
4. secret、plugin、ota、diagnostics 各自业务侧授权语义定义。

## 3. 输入依据与约束清单

### 3.1 约束清单

| ID | 来源 | 类型 | 约束内容 | 对 policy TODO 的影响 |
|---|---|---|---|---|
| POL-TC001 | policy 设计 1.1/2.1；架构 3.4.7/5.10/8.8 | Must | infra/policy 属于 Layer 1，只提供基础设施安全策略治理 | 任务不得写入 runtime/prompt/tool 主控职责 |
| POL-TC002 | 架构 3.7；蓝图 4.1/4.2 | Must | 依赖方向单向，policy 不反向依赖业务模块实现 | 代码目标仅限 infra/tests/docs/cmake |
| POL-TC003 | 蓝图 4.3；infrastructure 设计 2.1 | Must | 跨模块调用必须通过 contracts 冻结接口或稳定抽象 | contract 任务只能做语义映射门禁，不得假定 contracts/policy 头文件已存在 |
| POL-TC004 | ADR-005 | Must | contracts 与关键边界冻结优先 | schema、冲突裁定、共享语义缺口必须先列为阻塞 |
| POL-TC005 | ADR-006 | Must-Not | policy 不接管 ContextPacket 装配、Prompt 渲染或 PromptPolicy | QueryContext 只承载基础设施查询条件，不承载 prompt 消息语义 |
| POL-TC006 | ADR-007 | Must-Not | policy 不做失败归因、恢复裁定与补偿执行 | rollback 与 safe_mode 只描述 policy 内部状态，不触发 runtime 恢复链 |
| POL-TC007 | ADR-008 | Must-Not | policy 不拥有全局调度权 | evaluate 只能返回决策引用，不直接推进执行状态 |
| POL-TC008 | policy 设计 2.1、6.7、6.8 | Must | 必须支持 load、apply_patch、dry_run_patch、snapshot、rollback、evaluate，且变更版本化可回滚 | 主链路任务必须覆盖版本快照和 LKG 语义 |
| POL-TC009 | policy 设计 2.1、6.5；contracts-freeze 差异矩阵 | Must-Not | 不把规则引擎实现细节、匹配算法、线程模型写入 contracts | 任务只能冻结 infra 私有对象与映射规则 |
| POL-TC010 | contracts-freeze T010、术语表、ResultCode 分类表 | Must | 当前 contracts 只冻结了 policy 失败域和术语，不存在可直接引用的 PolicyDecision 代码对象 | contract 验证必须以语义边界和映射 catalog 为主 |
| POL-TC011 | 编码规范 3.6 | Must | 策略加载、拒绝、提交失败、回滚失败必须可观测 | 错误码、审计、指标、健康输出必须单列任务或阻塞 |
| POL-TC012 | 编码规范 3.7 | Should | 新增公共接口应同步增加 unit 或 contract 测试 | 每个接口/对象任务必须绑定 unit 或 contract 目标 |
| POL-TC013 | 蓝图 5.1；policy 设计 6.9 | Must | Profile 只能裁剪能力和替换实现，不得绕过 Audit 与 Runtime 主控链路 | 配置读取与模式切换任务必须保留 Audit gate 和 strict 默认值 |
| POL-TC014 | 计划文档 阶段 C | Must | 先底座后能力，每阶段都要可验证 | 顺序必须先对象/接口，再主链路，再桥接与门禁 |
| POL-TC015 | policy 设计 4.3；行业参考 OPA/Rego、AWS IAM、Kubernetes Admission Policy | Should | 规则需具备显式版本、优先级、拒绝优先、dry-run 与可回滚快照 | 冲突裁定、版本快照、safe_mode 任务必须显式拆出 |

### 3.2 代码与交付现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/CMakeLists.txt | 已接入 core/audit/plugin/tracing 等真实源码 | policy 接口与对象已落盘，但 policy 具体实现仍未进入构建图 |
| infra/include/ | 已落盘 policy/ISecurityPolicyManager.h、PolicyBundle.h、PolicyPatch.h、PolicySnapshot.h、PolicyDecisionRef.h | policy 对外接口与基础对象已冻结，后续主要承接 contracts/config 侧剩余阻塞 |
| infra/src/ | 仅存在 placeholder 与少量空子目录 | policy 实现目录尚未存在 |
| tests/CMakeLists.txt | 已接入 mocks、unit、contract、integration，且提供 dasall_integration_tests 聚合入口 | policy integration 拓扑已进入顶层构建，具体用例是否可执行取决于组件任务落盘 |
| tests/unit/CMakeLists.txt | 已接入 infra 子目录，且 tests/unit/infra/CMakeLists.txt 已注册 PolicySnapshotCompatibilityTest | policy unit 发现性基础已具备 |
| tests/contract/CMakeLists.txt | centralized registration 已纳入 PolicyDecisionBoundaryTest | policy 语义边界测试已有首批准入证据 |
| contracts/include/policy/* | 当前检索不到文件 | 不能把共享 policy 对象当作现成依赖 |
| docs/todos/contracts/DASALL_contracts验收整改TODO.md T010 | PolicyDecision、PromptPolicyDecision 仍处于“新增对象或替代映射”状态 | policy contract 门禁必须承接该缺口，不得默认视为已实现 |
| docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md INF-TODO-017/INF-BLK-07 | INF-TODO-017 已完成，且 INF-BLK-07 已于 2026-03-30 完成台账校准 | 本专项 TODO 仅继续承接 contracts/config 等剩余局部阻塞 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：当前可直接生成 L2 为主、局部 L3 的专项 TODO；当前最小可执行粒度为数据结构 / 接口级，局部可下钻到单个方法骨架，但不能整体进入函数级实现。

支撑证据：

1. 已有明确核心接口清单：ISecurityPolicyManager、IPolicyLoader、IPolicySchemaValidator、IPolicySnapshotStore。
2. 已有明确核心对象与字段：PolicyBundle、PolicyRuleDescriptor、PolicyPatch、PolicySnapshot、PolicyQueryContext、PolicyDecisionRef、ValidationReport、PolicyOpResult。
3. 已有主流程与异常流程：正常加载、策略查询、热更新、回滚、safe_mode 兜底。
4. 已有错误语义、配置项、文件落盘建议、测试出口建议。
5. 仍缺规则 schema 白名单、冲突裁定矩阵、Audit/Metrics/Health 桥接接口、integration 顶层接线、contracts policy 共享对象代码落点。

当前最小可执行粒度：数据结构 / 接口级（L2），局部方法骨架级（L3）仅限设计已明确签名的方法。

### 4.2 粒度可行性评估表

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| PolicyBundle + PolicyRuleDescriptor | policy 设计 6.5 | L3 | 字段、effect/domain/priority 约束、contracts 对齐说明明确 | conditions 白名单与 target_selector 规范未冻结 | 直接拆数据结构任务 |
| PolicyPatch + ValidationReport | policy 设计 6.5/6.6 | L3 | patch_id、base_generation、operations、blocking_errors 等字段明确 | operations 允许集合未冻结 | 直接拆数据结构任务 |
| PolicySnapshot + PolicyOpResult | policy 设计 6.5/6.8 | L3 | generation、lkg_ref、error_info、回滚语义明确 | persist_lkg 的介质策略未冻结 | 直接拆数据结构与 store 任务 |
| PolicyQueryContext + PolicyDecisionRef | policy 设计 6.5/6.7 | L3 | query 字段、decision 语义、matched_rule_ids、evidence_ref 明确 | contracts 共享 PolicyDecision 对象代码未落盘 | 直接拆数据结构任务，并附 contracts 映射门禁 |
| PolicyErrors | policy 设计 6.6/6.8 | L3 | 10 个建议错误码已给出 | 与 contracts::ResultCode 的精确映射矩阵未成文 | 直接拆错误码任务 |
| ISecurityPolicyManager | policy 设计 6.6 | L3 | 6 个方法名、输入输出、前后置条件明确 | 无 | 直接拆接口冻结任务 |
| IPolicyLoader | policy 设计 6.6/6.9 | L3 | load_from_sources 语义、source_id/version/checksum 约束明确 | ConfigCenter 最小接口尚未落盘 | 先冻结接口，再推进实现骨架 |
| IPolicySchemaValidator | policy 设计 6.6/6.8 | L2 | validate_bundle、validate_patch 签名明确 | 规则 schema 白名单与兼容矩阵未冻结 | 先冻结接口，校验实现保持 Blocked |
| IPolicySnapshotStore | policy 设计 6.6/6.8 | L3 | commit/current/last_known_good/get_by_id 签名明确 | 持久化介质细节未冻结 | 直接拆接口与内存版骨架 |
| PolicyConflictResolver | policy 设计 6.2/6.3/6.7/6.8 | L2 | 输入输出和 deny-first、explicit-priority 两种顺序已明确 | 冲突裁定矩阵、同优先级 tie-break 规则未冻结 | 先标 Blocked，补设计后再实现 |
| PolicyDecisionProjector | policy 设计 6.2/6.3/6.7 | L2 | domain -> target_selector -> priority -> effect 投影顺序明确 | contracts 共享对象缺失，evidence_ref 边界需映射 catalog | 先冻结输出对象，再标实现阻塞 |
| PolicyAuditBridge | policy 设计 6.2/6.10 | L1 | 强制审计事件范围明确 | audit 最小写入接口与导出字段尚未冻结 | 先阻塞，等待 audit 侧解阻 |
| PolicyMetricsBridge | policy 设计 6.2/6.10 | L1 | 指标名清单明确 | metrics 桥接接口与标签白名单未冻结 | 先阻塞，等待 metrics 侧解阻 |
| PolicyHealthProbe | policy 设计 6.2/6.10 | L1 | ready/degraded/unavailable 与最近失败原因语义明确 | health 侧探针接口与状态对象未冻结 | 先阻塞，等待 health 侧解阻 |
| tests/integration/infra/policy | policy 设计 8.1/9.1；tests 现状 | L0 | 路径与用例建议存在，且顶层 tests 已接入 integration | policy integration/failure 用例尚未落盘 | 直接拆组件集成任务 |

## 5. Design -> TODO 映射表

### 5.1 映射总表

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| 冻结规则与快照对象 | policy 设计 6.5 | 数据结构 | POL-TODO-001、POL-TODO-002、POL-TODO-003、POL-TODO-004 | 先稳定 PolicyTypes，避免实现期字段漂移 |
| 冻结 policy 错误语义 | policy 设计 6.6/6.8 | 错误处理 | POL-TODO-005 | 把加载、校验、提交、回滚、查询拒绝转成稳定错误码域 |
| 冻结对外入口与依赖接口 | policy 设计 6.6 | 接口定义 | POL-TODO-006、POL-TODO-007、POL-TODO-008、POL-TODO-009 | 先冻结入口和依赖接口，阻止直接绑实现 |
| 配置读取与 Profile 裁剪 | policy 设计 6.7/6.9；蓝图 5.1 | 配置 / 流程 | POL-TODO-010 | 将 strict/compat、hot_reload、priority_order 等配置读取单独拆任务 |
| 规则校验与冲突裁定 | policy 设计 6.7/6.8 | 流程 / 错误处理 | POL-TODO-011、POL-TODO-012 | 承接 INF-BLK-07，把 schema 与 conflict resolver 分开 |
| 快照提交、LKG 与回滚 | policy 设计 6.7/6.8 | 生命周期 / 初始化 | POL-TODO-013 | generation、history、LKG 是独立主目标 |
| 查询投影与 contracts 语义边界 | policy 设计 6.5/6.7；contracts-freeze T010 | 适配器 / contract | POL-TODO-014 | 只做 decision 引用与语义映射，不直接扩写 contracts |
| Manager 主链与 safe_mode | policy 设计 6.7/6.8 | 生命周期 / 流程 | POL-TODO-015 | 把 load/apply/dry_run/evaluate/rollback 收束到统一入口 |
| 构建与测试注册 | policy 设计 7、8.1、9.1；代码现状 | 测试 / 门禁 | POL-TODO-016、POL-TODO-017、POL-TODO-018 | CMake、unit/contract、integration 分拆，避免任务过大 |
| 审计、指标、健康桥接 | policy 设计 6.10；audit/metrics/health TODO | 适配器 / 门禁 | POL-TODO-019、POL-TODO-020、POL-TODO-021 | audit 桥接已完成；metrics/health 依赖已解阻可执行 |
| 交付证据回写 | policy 设计 9.2/11；工程规范 6.2 | 文档 / 质量门 | POL-TODO-022 | 对 gate、阻塞变化、回退证据做收口 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | POL-TODO-006~009 |
| 数据结构定义类任务 | 是 | POL-TODO-001~004 |
| 生命周期与初始化类任务 | 是 | POL-TODO-013、POL-TODO-015 |
| 适配器/桥接类任务 | 是 | POL-TODO-014、POL-TODO-019~021 |
| 异常与错误处理类任务 | 是 | POL-TODO-005、POL-TODO-011、POL-TODO-012 |
| 配置与 Profile 裁剪类任务 | 是 | POL-TODO-010 |
| 测试与门禁类任务 | 是 | POL-TODO-016~018 |
| 文档/交付证据回写类任务 | 是 | POL-TODO-022 |

## 6. 原子任务清单

### 6.1 原子任务表

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| POL-TODO-001 | Done (2026-04-01) | 定义 PolicyBundle 与 PolicyRuleDescriptor 数据结构 | policy 设计 6.5；架构 5.10；编码规范 3.7 | 6.5 核心对象表 | L3 | infra/include/policy/PolicyTypes.h | PolicyBundle、PolicyRuleDescriptor | unit：对象字段完整性；contract：effect/domain 仅做语义映射不扩写 contracts | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | 无 | 无 | 无 | PolicyTypes.h、PolicyBundle.h 兼容转发、PolicyTypesRuleBundleTest、PolicyTypesBoundaryContractTest；2026-04-01 已落盘并完成 CTest 注册与验收 | 仅当 bundle_id/schema_version/source/checksum/rules 与 rule_id/domain/effect/priority/mode/conditions/reason_code 全部落盘，且不引入业务依赖时完成 |
| POL-TODO-002 | Done (2026-04-01) | 定义 PolicyPatch 与 ValidationReport 数据结构 | policy 设计 6.5/6.6 | 6.5 核心对象表；6.6 validate_patch 语义 | L3 | infra/include/policy/PolicyTypes.h | PolicyPatch、ValidationReport | unit：patch 基础字段与 report 阻断语义；contract：错误原因只映射 policy 失败域 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-001 | 无（2026-03-30 已由 INF-BLK-07 校准解阻） | 无；可直接按已冻结的 operations 白名单与 field_path 约束推进 | PolicyTypes.h、PolicyPatch.h 兼容转发、ISecurityPolicyManager.h 收敛、PolicyPatchValidationTypesTest、PolicyPatchValidationBoundaryContractTest；2026-04-01 已落盘并完成 CTest 注册与验收 | 仅当 patch_id/base_generation/operations/actor/reason 与 blocking_errors/warnings/invalid_rule_ids/field_paths 全部落盘，且阻断语义可二值判定时完成 |
| POL-TODO-003 | Done (2026-04-01) | 定义 PolicySnapshot 与 PolicyOpResult 数据结构 | policy 设计 6.5/6.8 | 6.5 核心对象表；6.8 回滚兜底 | L3 | infra/include/policy/PolicyTypes.h | PolicySnapshot、PolicyOpResult | unit：generation 单调与 LKG 引用语义；contract：错误结果不扩写 contracts | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | POL-TODO-001 | 无 | 无 | PolicyTypes.h、对象测试；2026-04-01 已完成 PolicySnapshot.h 兼容转发、PolicySnapshotOpResultTypesTest 与 contract 边界校准 | 仅当 snapshot_id/generation/version/mode/effective_rules/source_chain/lkg_ref 与 applied/rolled_back/dry_run/snapshot_id/generation/error_info 全部落盘，且 generation 语义可测试时完成 |
| POL-TODO-004 | Done (2026-04-01) | 定义 PolicyQueryContext 与 PolicyDecisionRef 数据结构 | policy 设计 6.5/6.7；contracts-freeze 术语表 | 6.5 核心对象表；6.7 查询流程 | L3 | infra/include/policy/PolicyTypes.h | PolicyQueryContext、PolicyDecisionRef | unit：上下文字段 unknown 兜底；contract：decision 只对齐 allow/deny/require_confirmation 语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-001 | 无（2026-04-01 已由 PolicyDecisionMappingCatalog 解阻） | 无；contracts 语义映射 catalog 已冻结 | PolicyTypes.h、PolicyDecisionRef.h 兼容转发、PolicyQueryDecisionRefTypesTest、PolicyDecisionBoundaryTest；2026-04-01 已完成统一入口收敛与 CTest 验收 | 仅当 module/operation/target_type/target_ref/actor_ref/request_id/session_id/trace_id/task_id/profile_id 与 decision/reason_code/matched_rule_ids/snapshot_id/generation/evidence_ref/warnings 全部落盘，且 decision 语义无越权时完成 |
| POL-TODO-005 | Done (2026-04-01) | 定义 PolicyErrors 错误码域 | policy 设计 6.6/6.8；contracts-freeze ResultCode 分类表 | 6.6 错误语义；6.8 异常与恢复时序 | L3 | infra/include/policy/PolicyErrors.h | INF_E_POLICY_BUNDLE_INVALID、INF_E_POLICY_SCHEMA_UNSUPPORTED、INF_E_POLICY_CONFLICT_UNRESOLVED、INF_E_POLICY_PATCH_BASE_MISMATCH、INF_E_POLICY_SNAPSHOT_NOT_FOUND、INF_E_POLICY_ROLLBACK_FAILED、INF_E_POLICY_QUERY_DENIED、INF_E_POLICY_SOURCE_UNAVAILABLE、INF_E_POLICY_STORE_COMMIT_FAILED、INF_E_POLICY_DRYRUN_REJECTED | contract：错误码映射保持 policy 失败域；unit：错误码稳定性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | 无 | 无（2026-04-01 已明确采用 mapping catalog 作为替代路径） | 无；可直接冻结 policy 失败域与 contracts 映射说明 | PolicyErrors.h、PolicyErrorsTest、PolicyErrorMappingContractTest；2026-04-01 已完成错误码域冻结、CMake 入图与 CTest 验收 | 仅当 10 个错误码都可追溯到设计锚点，且不把 validation/tool/runtime 失败误归入 policy 域时完成 |
| POL-TODO-006 | Done (2026-04-01) | 定义 ISecurityPolicyManager 接口头文件 | policy 设计 6.6；infrastructure 设计 6.6 | 6.6 ISecurityPolicyManager | L3 | infra/include/policy/ISecurityPolicyManager.h | load_policy、apply_patch、dry_run_patch、snapshot、rollback、evaluate | unit：接口编译；contract：返回对象与 decision 语义边界 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-001、POL-TODO-002、POL-TODO-003、POL-TODO-004、POL-TODO-005 | 无 | 无 | ISecurityPolicyManager.h、PolicyManagerInterfaceTest、PolicyDecisionBoundaryTest；2026-04-01 已完成 nodiscard 收敛、unit 接口冻结测试与 contract 边界校验 | 仅当 6 个方法签名与设计一致，且接口不暴露规则引擎实现细节时完成 |
| POL-TODO-007 | Done (2026-04-01) | 定义 IPolicyLoader 接口头文件 | policy 设计 6.6/6.9；config TODO | 6.6 IPolicyLoader；6.9 配置项与默认策略 | L3 | infra/include/policy/IPolicyLoader.h | load_from_sources | unit：接口编译；unit：source_id/version/checksum 字段对接 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | POL-TODO-001、POL-TODO-006 | 无（2026-04-01 已由 config/profiles 接口与 schema 冻结解阻） | 无；ConfigCenter 最小 load_layers/get_typed 接口与 profiles 侧 policy 键名已冻结 | IPolicyLoader.h、PolicyLoaderInterfaceTest、build-ci unit 验证记录；2026-04-01 已完成 | 仅当 loader 只暴露 PolicyBundle 输出，不泄露 ConfigCenter 内部实现时完成 |
| POL-TODO-008 | Done (2026-04-01) | 定义 IPolicySchemaValidator 接口头文件 | policy 设计 6.6/6.8 | 6.6 IPolicySchemaValidator；6.8 输入异常 | L2 | infra/include/policy/IPolicySchemaValidator.h | validate_bundle、validate_patch | unit：接口编译；contract：blocking_errors 只对齐错误域不扩写 contracts | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-001、POL-TODO-002 | 无（2026-03-30 已由 INF-BLK-07 校准解阻） | 无；可直接按已冻结 schema 字段与兼容矩阵推进 | IPolicySchemaValidator.h、PolicySchemaValidatorInterfaceTest、PolicySchemaValidatorInterfaceBoundaryContractTest、build-ci unit/contract 验证记录；2026-04-01 已完成 | 仅当接口只暴露校验结果，不预埋 DSL 执行模型时完成 |
| POL-TODO-009 | Done (2026-04-01) | 定义 IPolicySnapshotStore 接口头文件 | policy 设计 6.6/6.8 | 6.6 IPolicySnapshotStore；6.8 存储异常 | L3 | infra/include/policy/IPolicySnapshotStore.h | commit、current、last_known_good、get_by_id | unit：接口编译；unit：LKG 与 generation 语义检查 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | POL-TODO-003 | 无 | 无 | IPolicySnapshotStore.h、PolicySnapshotStoreInterfaceTest、build-ci unit 验证记录；2026-04-01 已完成 | 仅当 4 个方法齐全，且接口不泄露具体持久化后端时完成 |
| POL-TODO-010 | Done (2026-04-05) | 实现 PolicyLoader 配置读取骨架 | policy 设计 6.3/6.7/6.9；config TODO；profiles TODO | 6.3 PolicyLoader 输入输出；6.7 正常加载流程第 2 步；6.9 配置项表 | L2 | infra/src/policy/PolicyLoader.cpp | PolicyLoader（默认/Profile/部署层读取与 source_id/checksum 装配） | unit：strict/compat、hot_reload、default_effect 等配置读取；contract：Profile 裁剪不绕过 Audit/Runtime 主控链路 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-001、POL-TODO-007 | 无（2026-04-01 已由 config/profiles 接口与 schema 冻结解阻） | 无；ConfigCenter 具备最小 load_layers/get_typed 能力且 profiles 侧键名冻结 | PolicyLoader.h/.cpp、PolicyLoaderConfigReadTest、PolicyLoaderBoundaryContractTest、policy CMake/test 接线；2026-04-05 已落盘并完成 build-ci unit/contract 验收 | 仅当 loader 能按设计读取 enabled/mode/hot_reload/max_history/default_effect/priority_order 等策略键，且 source 链可追溯时完成 |
| POL-TODO-011 | Done (2026-04-05) | 实现 PolicySchemaValidator 最小校验骨架 | policy 设计 6.3/6.7/6.8 | 6.3 ValidationReport；6.7 正常加载流程第 3 步；6.8 输入异常 | L2 | infra/src/policy/PolicySchemaValidator.cpp | validate_bundle、validate_patch | unit：缺字段、未知 domain、非法 effect、base_generation 不匹配；contract：错误归类保持 policy 域 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-002、POL-TODO-005、POL-TODO-008 | 无（2026-03-30 已由 INF-BLK-07 校准解阻） | 无；可直接按已冻结 domain/effect/conditions 白名单、schema_version 兼容矩阵与 patch operation 集合推进 | PolicySchemaValidator.h/.cpp、PolicySchemaValidatorTest、PolicySchemaValidatorBoundaryContractTest、policy CMake/test 接线；2026-04-05 已落盘并完成 build-ci unit/contract 验收 | 仅当四类非法输入都能返回明确 ValidationReport 且不激活快照时，状态才可从 Not Started 转为 Done |
| POL-TODO-012 | Done (2026-04-05) | 实现 PolicyConflictResolver 冲突裁定骨架 | policy 设计 6.3/6.7/6.8/6.9 | 6.3 EffectivePolicySet 输出；6.7 正常加载流程第 4 步；6.9 priority_order | L2 | infra/src/policy/PolicyConflictResolver.cpp | PolicyConflictResolver（deny-first 与 explicit-priority 裁定路径） | unit：deny-first 与 explicit-priority 两档裁定；failure：冲突未决拒绝激活 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | POL-TODO-001、POL-TODO-008、POL-TODO-010 | 无（2026-03-30 已由 INF-BLK-07 校准解阻） | 无；可直接按已冻结冲突裁定矩阵、同优先级 tie-break 与 compat 模式降级规则推进 | PolicyConflictResolver.h/.cpp、PolicyConflictResolverTest、policy CMake/unit 接线；2026-04-05 已落盘并完成 build-ci unit 验收 | 仅当两档裁定都可被稳定验证，且冲突未决时返回显式拒绝而非静默覆盖时完成 |
| POL-TODO-013 | Done (2026-04-05) | 实现 PolicySnapshotStore generation/LKG 骨架 | policy 设计 6.3/6.7/6.8 | 6.3 PolicySnapshotStore；6.7 正常加载流程第 5 步；6.8 commit 失败回退 | L2 | infra/src/policy/PolicySnapshotStore.cpp | commit、current、last_known_good、get_by_id | unit：generation 单调、自增、LKG 回退；failure：commit 失败后 current 不切换 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | POL-TODO-003、POL-TODO-009 | 无 | 无 | PolicySnapshotStore.h/.cpp、PolicySnapshotStoreTest、policy CMake/unit 接线；2026-04-05 已落盘并完成 build-ci unit 验收 | 仅当新快照提交成功后 generation 自增，提交失败后 current/LKG 保持旧值且错误可判定时完成 |
| POL-TODO-014 | Done (2026-04-05) | 实现 PolicyDecisionProjector 查询投影骨架 | policy 设计 6.3/6.5/6.7；contracts-freeze T010 | 6.3 投影输出；6.5 PolicyDecisionRef；6.7 查询流程 | L2 | infra/src/policy/PolicyDecisionProjector.cpp | PolicyDecisionProjector（domain -> target_selector -> priority -> effect 投影路径） | unit：命中、未命中、require_confirmation、deny 四类投影；contract：decision 语义与 evidence_ref 映射 catalog | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-004、POL-TODO-012、POL-TODO-013 | 无（2026-04-01 已由 PolicyDecisionMappingCatalog 解阻） | 无；共享对象缺失时的映射 catalog 已固定 | PolicyDecisionProjector.h/.cpp、PolicyDecisionProjectorTest、PolicyDecisionProjectorBoundaryContractTest、policy CMake/test 接线；2026-04-05 已落盘并完成 build-ci unit/contract 验收 | 仅当投影结果只输出引用和原因，不泄露规则实现细节，且 contract 门禁通过时完成 |
| POL-TODO-015 | Done (2026-04-05) | 实现 SecurityPolicyManager 主链骨架 | policy 设计 6.2/6.4/6.7/6.8 | 6.2 SecurityPolicyManager；6.4 依赖关系；6.7/6.8 主异常流程 | L2 | infra/src/policy/SecurityPolicyManager.cpp | load_policy、apply_patch、dry_run_patch、snapshot、rollback、evaluate、safe_mode 进入条件 | unit：正常加载、patch 失败不切 current、rollback 成功、连续失败进入 safe_mode；contract：拒绝结果保持 policy 失败域 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-006、POL-TODO-010、POL-TODO-011、POL-TODO-012、POL-TODO-013、POL-TODO-014 | 无（2026-04-01 已由 config/profiles 接口与 schema 冻结解阻） | 无；ConfigCenter 最小接口冻结 | SecurityPolicyManager.h/.cpp、SecurityPolicyManagerTest、SecurityPolicyManagerFailureContractTest、policy CMake/test 接线；2026-04-05 已落盘并完成 build-ci unit/contract 验收 | 仅当 load/dry_run/apply/query/rollback 五条路径都能被二值验证，且 safe_mode 触发条件可复现时完成 |
| POL-TODO-016 | Done (2026-04-05) | 注册 policy 源码到 infra CMake | policy 设计 7、8.1；代码现状 | 7 Design -> Build 映射；8.1 文件落盘建议 | L2 | infra/CMakeLists.txt | policy include/src 文件纳入 dasall_infra | build：dasall_infra 可编译；unit：policy 接口编译可进入构建图 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | POL-TODO-001 至 POL-TODO-009 | 无 | 无 | infra/CMakeLists.txt 中 DASALL_INFRA_POLICY_SOURCES/PRIVATE_HEADERS 已纳入 dasall_infra；2026-04-05 已完成 build-ci 构建验收 | 仅当 placeholder 不再是唯一源码入口，且 policy 文件进入 dasall_infra 构建图时完成 |
| POL-TODO-017 | Done (2026-04-05) | 注册 policy 的 unit 与 contract 测试入口 | policy 设计 8.1/9.1；tests 现状 | 8.1 tests/unit/infra/policy、tests/contract/infra；9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt、tests/unit/infra/policy/、tests/contract/CMakeLists.txt、tests/contract/smoke/ | unit：对象、接口、loader/store/manager 基础路径；contract：decision 语义、错误码映射、contracts 边界 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-016 | 无（2026-04-01 已由 PolicyDecisionMappingCatalog 解阻） | 无；可直接按已冻结 mapping catalog 与 contract 口径推进 | tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt 已完成注册；2026-04-05 ctest -N 发现 26 个 policy 核心用例，unit 125/125、contract 137/137 通过 | 仅当新增 policy unit/contract 用例可被 ctest -N 发现并执行时完成 |
| POL-TODO-018 | Done (2026-04-05) | 注册 policy integration 测试入口 | policy 设计 8.1/9.1；tests 现状 | 8.1 tests/integration/infra/policy；9.1 Integration/Failure Injection | L0 | tests/CMakeLists.txt、tests/integration/infra/policy/ | integration：load -> snapshot -> evaluate -> patch -> rollback 闭环；failure：source unavailable、commit fail、safe_mode | cmake -S . -B build-ci -G Ninja && ctest --test-dir build-ci -N | POL-TODO-015、POL-TODO-017 | 无（2026-03-30 已由 INF-BLK-06 integration 顶层拓扑校准解阻） | 无；已落盘 policy integration 子目录、CTest 注册与 lifecycle/failure 注入用例 | tests/integration/infra/policy/CMakeLists.txt、PolicyLifecycleIntegrationTest.cpp、integration 聚合接线与 ctest 发现性证据；2026-04-05 已完成 build-ci integration 验收 | 仅当 tests 顶层完成 integration 接线且 policy 集成用例可被 ctest 发现后，状态才可从 Not Started 转为 Done |
| POL-TODO-019 | Done (2026-04-05) | 实现 PolicyAuditBridge 审计桥接骨架 | policy 设计 6.2/6.10；audit TODO | 6.2 PolicyAuditBridge；6.10 强制审计点 | L1 | infra/src/policy/PolicyAuditBridge.cpp | PolicyAuditBridge（load/apply_patch/rollback/deny 事件桥接） | unit：高风险 deny 与 patch failure 事件组装；contract：AuditEvent 引用边界不越权 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-015 | 无（2026-04-05 已由 AUD-TODO-006、AUD-TODO-014、AUD-TODO-015 与 audit gate 9/9 解阻） | 无 | PolicyAuditBridge.h/.cpp、PolicyAuditBridgeTest、PolicyAuditBridgeBoundaryContractTest、policy CMake/test 接线；2026-04-05 已落盘并完成 build-ci 定向/unit/contract 验收 | 仅当 bridge 只输出审计事实，不重新定义审计对象，且事件覆盖四个强制审计点时完成 |
| POL-TODO-020 | Not Started | 实现 PolicyMetricsBridge 指标桥接骨架 | policy 设计 6.2/6.10；metrics TODO | 6.2 PolicyMetricsBridge；6.10 指标清单 | L1 | infra/src/policy/PolicyMetricsBridge.cpp | PolicyMetricsBridge（reload_total、invalid_total、patch_total、deny_total、rollback_total、active_generation、safe_mode_total） | unit：计数与 gauge 输出；contract：标签不过度暴露实现细节 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | POL-TODO-015 | 无（2026-04-05 已由 metrics/health 接口冻结、标签白名单与状态对象边界测试解阻） | 无 | PolicyMetricsBridge.cpp 或阻塞记录 | 仅当指标集合与设计一致，且 active_generation/safe_mode 语义可被测试稳定判定时完成 |
| POL-TODO-021 | Not Started | 实现 PolicyHealthProbe 健康探针骨架 | policy 设计 6.2/6.10；health TODO | 6.2 PolicyHealthProbe；6.10 ready/degraded/unavailable | L1 | infra/src/policy/PolicyHealthProbe.cpp | PolicyHealthProbe（ready/degraded/unavailable 与最近失败原因输出） | unit：状态切换；integration：commit fail 或连续 patch fail 后 health 降级 | cmake -S . -B build-ci -G Ninja && ctest --test-dir build-ci -N | POL-TODO-015 | 无（2026-04-05 已由 metrics/health 接口冻结、标签白名单与状态对象边界测试解阻） | 无 | PolicyHealthProbe.cpp 或阻塞记录 | 仅当健康状态与最近失败原因能被稳定输出，并且不侵入 runtime 状态机时完成 |
| POL-TODO-022 | Not Started | 回写 policy 质量门与交付证据 | policy 设计 9.2/11；工程规范 6.2 | 9.2 Gate 建议；11 风险与回退 | L2 | docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md | process test：gate 结论、阻塞变化、回退证据回写 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-017 | 无 | 无 | 更新后的 TODO 文档证据段 | 仅当每个质量门都有通过/失败结论和对应命令证据时完成 |

### 6.2 当前 Blocked 任务索引

| 任务 ID | 对应阻塞项 |
|---|---|
| 无 | 无 |

## 7. 执行顺序建议

### 7.1 串并行编排

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 对象冻结 | POL-TODO-001~005 | 可并行 | 先冻结 PolicyTypes 与错误码域，减少后续返工 |
| B 接口冻结 | POL-TODO-006~009 | 可并行 | 接口边界稳定后再推进实现骨架 |
| C 配置与快照底座 | POL-TODO-010、POL-TODO-013 | 串行 | 先读取配置，再形成快照存储基础 |
| D 规则治理主链 | POL-TODO-011、POL-TODO-012、POL-TODO-014、POL-TODO-015 | 串行 | INF-BLK-07 已完成校准；POL-BLK-006 已由 config/profiles 接口与 schema 冻结解阻，decision 语义缺口已由 mapping catalog 解阻 |
| E 构建与测试接线 | POL-TODO-016、POL-TODO-017 | 可并行 | 代码入图与 unit/contract 发现性可同步推进；contracts 语义映射缺口已由 mapping catalog 补齐 |
| F 观测桥接与集成 | POL-TODO-018~021 | 串行 | 018、019 已完成；020/021 已由 metrics/health 接口冻结解阻，可继续串行推进 |
| G 证据收口 | POL-TODO-022 | 串行 | 回写质量门、阻塞变化与回退证据 |

### 7.2 必过门禁表

| Gate ID | 门禁项 | 触发时机 | 通过标准 | 不通过后动作 |
|---|---|---|---|---|
| POL-GATE-01 | 规则 schema 冻结门 | 进入 validator/resolver/projector 实现前 | domain/effect/conditions/operations 白名单与冲突裁定矩阵已由 INF-BLK-07 校准固化 | 若 schema、priority_order 或 patch 白名单回退，则重新挂起 POL-BLK-001 并禁止推进 POL-TODO-011/012/014/015 |
| POL-GATE-02 | 接口冻结门 | 阶段 B 结束前 | PolicyTypes、PolicyErrors、四个接口头文件全部落盘且不越界 | 退回对象/接口定义 |
| POL-GATE-03 | 快照回滚门 | 阶段 C 结束前 | generation、history、LKG 语义有自动测试，提交失败不切 current | 回退 SnapshotStore 变更 |
| POL-GATE-04 | contracts 语义边界门 | 阶段 D 与 E 前 | PolicyDecision 只做语义对齐；若共享对象仍缺失，则 mapping catalog 和 contract 测试通过 | 维持 POL-BLK-002，禁止对外暴露 shared dependency |
| POL-GATE-05 | 观测桥接门 | 进入 bridge 实现前 | audit、metrics、health 最小桥接接口冻结 | 维持 POL-BLK-003/POL-BLK-004 |
| POL-GATE-06 | 测试发现性门 | 阶段 E 前 | ctest --test-dir build-ci -N 能发现新增 policy unit/contract 用例 | 修复 tests 注册，不推进后续 |
| POL-GATE-07 | integration 拓扑门 | 进入 integration 验收前 | tests 顶层已接入 integration，且 policy 组件用例与标签规范明确 | 补齐 policy integration/failure 用例与注册，不再回退到仓库级阻塞 |
| POL-GATE-08 | breaking review 门 | 任意共享语义或接口签名变化前 | 明确 breaking 风险、迁移窗口、回退策略和评审结论 | 未评审不得推进 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| POL-BLK-001 | 已解阻（2026-03-30）：INF-BLK-07 所需的规则 schema、domain/effect 白名单、patch operations 与冲突裁定顺序已由 policy 详细设计、头文件与边界测试共同固化 | POL-TODO-002、008、011、012、014、015 | 无；后续仅需保持设计、头文件与测试口径同步 | 证据回链到 infra 专项 TODO 的 INF-BLK-07 校准记录，以及 infra/include/policy/*、tests/unit/infra/PolicySnapshotCompatibilityTest.cpp、tests/contract/smoke/PolicyDecisionBoundaryTest.cpp | 若 schema、priority_order 或 patch operation 白名单回退，则重新转为 Blocked |
| POL-BLK-002 | 已解阻（2026-04-01）：contracts 侧仍缺少可直接引用的 PolicyDecision 共享对象，但已通过 contracts/include/boundary/PolicyDecisionMappingCatalog.h 与 PolicyDecisionMappingCatalogContractTest 形成正式 mapping catalog，并冻结允许语义与 infra 私有 trace 字段边界 | POL-TODO-004、005、014、017 | 无；后续仅需保持 mapping catalog、PolicyDecisionRef 与 contract 测试口径同步 | 承接 contracts-freeze T010，使用替代映射 catalog 固化 allow/deny/require_confirmation 与禁止越权字段 | 若后续直接扩写 shared PolicyDecision 或映射 catalog 回退，则重新转为 Blocked |
| POL-BLK-003 | 已解阻（2026-04-05）：audit 组件专项 TODO 已完成 `AUD-TODO-006` 的 `IAuditLogger` 冻结、`AUD-TODO-014` 的 `IAuditHealthProbe/AuditHealthStatus` 冻结、`AUD-TODO-015` 的 AuditMetricsBridge 落盘，且 `ctest --test-dir build-ci -L audit` 当前 9/9 通过，说明 policy 所需的最小审计写入接口和字段集合已经稳定 | POL-TODO-019 | 无；后续仅需保持 `IAuditLogger`、`AuditEvent/AuditContext/AuditWriteOutcome`、`AuditHealthStatus` 与 audit gate 的语义一致 | 证据回链到 audit 组件专项 TODO 的 AUD-TODO-006、AUD-TODO-014、AUD-TODO-015、AUD-BLK-003/AUD-BLK-004 记录，以及 infra/include/audit/IAuditLogger.h、infra/include/audit/AuditTypes.h | 若 audit 侧最小写入接口、核心字段或 health/metrics 协同语义回退，则重新转为 Blocked |
| POL-BLK-004 | 已解阻（2026-04-05）：metrics 组件专项 TODO 已完成 `MET-TODO-001`~`MET-TODO-008`，冻结 `IMetricsProvider`、`IMeter`、`IMetricConfigPolicy`、`IMetricsHealthProbe`、`MetricTypes`、`MetricsSnapshots`、`MetricsErrors`；health 组件专项 TODO 已完成 `HLT-TODO-001`~`HLT-TODO-006`，冻结 `IHealthProbe`、`IHealthMonitor`、`IHealthPolicy`、`HealthStateTypes`、`RecoveryHint`。当前 `ctest --test-dir build-ci -R "(MetricsProviderInterfaceTest|MetricsMeterInterfaceTest|MetricsConfigPolicyInterfaceTest|MetricsHealthProbeInterfaceTest|MetricTypesTest|MetricsProviderInterfaceBoundaryContractTest|MetricsMeterInterfaceBoundaryContractTest|MetricsConfigPolicyInterfaceBoundaryContractTest|HealthSnapshotUnitTest|HealthSnapshotBoundaryContractTest|HealthMonitorInterfaceBoundaryContractTest)"` 已 11/11 通过，说明 policy 所需的 metrics bridge 接口、标签白名单与 health 状态对象都已稳定 | POL-TODO-020、POL-TODO-021 | 无；后续仅需保持 `IMetricsProvider/IMeter/IMetricConfigPolicy/IMetricsHealthProbe`、`MetricLabels` allowlist 与 `HealthSnapshot/HealthTransition` 语义一致 | 证据回链到 metrics 组件专项 TODO 的 MET-TODO-001~008、health 组件专项 TODO 的 HLT-TODO-001~006，以及 tests/unit/infra/MetricTypesTest.cpp、tests/contract/smoke/MetricsConfigPolicyInterfaceBoundaryContractTest.cpp、tests/unit/infra/HealthSnapshotTest.cpp、tests/contract/smoke/HealthSnapshotBoundaryContractTest.cpp | 若 metrics 标签白名单、provider/meter 接口或 health 状态对象边界未来回退，则重新转为 Blocked |
| POL-BLK-005 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；policy integration/failure 是否可执行改由组件自身落盘负责 | POL-TODO-018、021 | 无；后续仅需按组件落盘 integration/failure 用例 | 证据回链到 infra 专项 TODO 的 INF-BLK-06 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt | 若 tests 顶层 integration 接线或聚合依赖回退，则重新转为 Blocked |
| POL-BLK-006 | 已解阻（2026-04-01）：config 组件已落盘 IConfigCenter 最小 load_layers/get_typed 接口与对应 unit/contract 边界，profiles 侧已冻结 runtime_policy.yaml policy 键域并落盘 RuntimePolicyProvider 最小加载流程 | POL-TODO-007、010、015 | 无；后续仅需保持 ConfigCenter 接口、profiles schema 与 policy loader 读取键口径同步 | 证据回链到 config TODO 的 CFG-TODO-001，以及 profiles TODO 的 PRF-TODO-008、PRF-TODO-013 与相关 unit/contract 测试 | 若 IConfigCenter 接口回退、runtime_policy.yaml 键域漂移或 schema 校验失效，则重新转为 Blocked |

## 33. 本轮执行记录（2026-04-05 / POL-BLK-004 解阻校准）

### 33.1 选中任务

1. 本轮任务：POL-BLK-004。
2. 可执行性依据：`POL-TODO-020` 与 `POL-TODO-021` 仍被文档标记为 Blocked，但 metrics/health 的接口冻结与对象冻结任务已在各自专项 TODO 内完成；当前需要的是 policy 侧 blocker 台账校准，而不是新增外部设计工作。

### 33.2 研究与 Design 结论

本地证据：

1. docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md 已将 `MET-TODO-001`~`MET-TODO-008` 标记为 Done，覆盖 `IMetricsProvider`、`IMeter`、`IMetricConfigPolicy`、`IMetricsHealthProbe`、`MetricTypes`、`MetricsSnapshots`、`MetricsErrors`，说明 policy metrics bridge 所需的最小 bridge 接口、标签对象与错误语义已经冻结。
2. tests/unit/infra/MetricTypesTest.cpp 与 tests/contract/smoke/MetricsConfigPolicyInterfaceBoundaryContractTest.cpp 已把 `module/stage/profile/outcome` 必填标签和 `error_code` 白名单约束固化为可执行门禁；当前 build-ci 定向执行证明这些 allowlist 语义仍然有效。
3. docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md 已将 `HLT-TODO-001`~`HLT-TODO-006` 标记为 Done，覆盖 `IHealthProbe`、`IHealthMonitor`、`IHealthPolicy`、`ProbeTypes`、`HealthStateTypes` 与 `RecoveryHint`，说明 policy health probe 所需的最小探针接口和状态对象已经冻结。
4. tests/unit/infra/HealthSnapshotTest.cpp、tests/contract/smoke/HealthSnapshotBoundaryContractTest.cpp、tests/contract/smoke/HealthMonitorInterfaceBoundaryContractTest.cpp 当前通过，证明 `HealthSnapshot/HealthTransition` 的 ready/degraded/unhealthy 边界、版本语义和 monitor 输出边界在当前仓库状态下可复用。

D 结论：

1. `POL-BLK-004` 的根因已经被 metrics/health 专项的接口冻结、标签白名单门禁与状态对象门禁消除；继续把 020/021 保持为 Blocked 会制造伪阻塞。
2. 本轮不修改 metrics 或 health 代码，只在 policy 专项 TODO 和 worklog 中回写解阻证据，并将 `POL-TODO-020`、`POL-TODO-021` 校准为可执行的 Not Started。
3. D Gate：PASS。

### 33.3 Build 交付与证据

交付物：

1. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md：将 `POL-BLK-004` 标记为已解阻，更新 `POL-TODO-020/021` 的状态与阻塞说明，并同步刷新“当前 Blocked 任务索引”“阶段 F 编排”与本轮执行记录。

验收结果：

1. ctest --test-dir build-ci -N -R "(MetricsProviderInterfaceTest|MetricsMeterInterfaceTest|MetricsConfigPolicyInterfaceTest|MetricsHealthProbeInterfaceTest|MetricTypesTest|MetricsProviderInterfaceBoundaryContractTest|MetricsMeterInterfaceBoundaryContractTest|MetricsConfigPolicyInterfaceBoundaryContractTest|HealthSnapshotUnitTest|HealthSnapshotBoundaryContractTest|HealthMonitorInterfaceBoundaryContractTest)"：通过，发现 11 个相关 gate 测试。
2. ctest --test-dir build-ci --output-on-failure -R "(MetricsProviderInterfaceTest|MetricsMeterInterfaceTest|MetricsConfigPolicyInterfaceTest|MetricsHealthProbeInterfaceTest|MetricTypesTest|MetricsProviderInterfaceBoundaryContractTest|MetricsMeterInterfaceBoundaryContractTest|MetricsConfigPolicyInterfaceBoundaryContractTest|HealthSnapshotUnitTest|HealthSnapshotBoundaryContractTest|HealthMonitorInterfaceBoundaryContractTest)"：通过，11/11 tests passed。

Build 合规复核：

1. 根因闭环：本轮通过 metrics/health 已冻结接口与测试门禁证据消除 policy 侧历史阻塞，而不是跳过前置依赖直接开做 020/021。
2. 边界保持：本轮只校准 policy 台账，不越界修改 metrics/health 组件实现。
3. TODO 证据回写：已完成 `POL-BLK-004` 状态、影响任务、解阻依据与执行顺序回写。
4. 提交隔离：本轮提交范围限定为 `POL-BLK-004` 的 policy TODO/worklog 解阻证据同步，不混入 `POL-TODO-020`、`POL-TODO-021` 的实现代码。

## 32. 本轮执行记录（2026-04-05 / POL-TODO-019）

### 32.1 选中任务

1. 本轮任务：POL-TODO-019。
2. 可执行性依据：POL-BLK-003 已在上一轮完成解阻校准；audit 组件专项已冻结 `IAuditLogger`、`AuditEvent`、`AuditContext`、`AuditWriteOutcome`，因此 policy 侧可以在不扩写公共审计对象的前提下落盘最小审计桥接骨架。

### 32.2 研究与 Design 结论

本地证据：

1. infra/include/audit/IAuditLogger.h 与 infra/include/audit/AuditTypes.h 已冻结最小审计写入接口与事件边界，当前 policy bridge 只能复用既有 `AuditEvent/AuditContext/AuditWriteOutcome`，不能另起审计对象。
2. infra/src/secret/SecretAuditBridge.h/.cpp 已提供仓库内桥接实现样式：私有 bridge 只依赖 `std::shared_ptr<audit::IAuditLogger>`，并通过 side_effects 携带稳定事实，不把内部实现细节外泄到公共 payload。
3. tests/contract/smoke/AuditBoundaryContractTest.cpp 与新增 tests/contract/smoke/PolicyAuditBridgeBoundaryContractTest.cpp 共同约束 policy bridge 的事件必须停留在既有 `AuditEvidenceKind::ToolResult` 边界内，且不得泄露 `matched_rule_ids`、`effective_rules` 等 policy 内部结构。
4. 新增 tests/unit/infra/PolicyAuditBridgeTest.cpp 以高风险 deny 与 patch failure 两条路径覆盖 `reason_code`、`snapshot_id`、`generation`、`detail_ref` 等稳定审计事实组装，保证四个强制审计点可以沿同一 event 装配模型扩展。

D 结论：

1. `PolicyAuditBridge` 作为 infra/policy 私有实现落盘在 infra/src/policy/，只承接 `load/apply_patch/rollback/high_risk_deny` 四类审计发射，不接管 manager 主链或 audit 子系统职责。
2. 审计证据统一保持在 `AuditEvidenceKind::ToolResult` 范围内，并把 `reason_code/snapshot_id/generation/detail_ref` 收敛到可序列化的 side_effects，避免新增 public payload 成员。
3. `PolicyAuditBridgeStatus` 只暴露发射计数、降级态、最后错误码和 detail_ref，满足最小可观测性而不引入 metrics/health 的外部桥接语义。
4. D Gate：PASS。

### 32.3 Build 交付与证据

交付物：

1. infra/src/policy/PolicyAuditBridge.h、infra/src/policy/PolicyAuditBridge.cpp：新增 policy 审计桥接私有实现，覆盖 load、apply_patch、rollback 与 deny 事件组装与发射。
2. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成 019 所需源码与 unit/contract 目标接线。
3. tests/unit/infra/PolicyAuditBridgeTest.cpp、tests/contract/smoke/PolicyAuditBridgeBoundaryContractTest.cpp：新增 unit/contract 门禁，验证事件事实组装和 audit 边界不越权。

验收结果：

1. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
2. cmake --build build-ci --target dasall_infra dasall_policy_audit_bridge_unit_test dasall_contract_policy_audit_bridge_boundary_test：通过。
3. ctest --test-dir build-ci -N -R "PolicyAuditBridge(Test|BoundaryContractTest)"：通过，发现 2 个目标测试。
4. ctest --test-dir build-ci --output-on-failure -R "PolicyAuditBridge(Test|BoundaryContractTest)"：通过，2/2 tests passed。
5. ctest --test-dir build-ci --output-on-failure -L unit：通过，126/126 tests passed。
6. ctest --test-dir build-ci --output-on-failure -L contract：通过，138/138 tests passed。

Build 合规复核：

1. 根因闭环：本轮直接补齐 policy 对 audit 的私有桥接缺口，而不是在 manager 或公共接口层做旁路埋点。
2. 边界保持：bridge 只复用 audit 冻结对象和 logger 接口，不新增跨子系统公共头，也不引入 metrics/health 语义越权。
3. 测试闭环：定向、unit、contract 三层 gate 都已覆盖，既验证事件装配正确，也验证不泄露 policy 内部结构。
4. 提交隔离：本轮提交范围限定为 `POL-TODO-019` 的 policy bridge 实现、CMake/test 接线和 TODO/worklog 证据，不混入 `POL-BLK-004`、`POL-TODO-020`、`POL-TODO-021`。

## 31. 本轮执行记录（2026-04-05 / POL-BLK-003 解阻校准）

### 31.1 选中任务

1. 本轮任务：POL-BLK-003。
2. 可执行性依据：POL-TODO-019 仍被标记为 Blocked，但 audit 组件专项 TODO 与当前 build-ci gate 已显示 policy 所需的最小审计写入接口、字段集合和 health/metrics 协同语义都已冻结；当前差异只剩 policy 台账未同步。

### 31.2 研究与 Design 结论

本地证据：

1. docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md 已将 `AUD-TODO-006` 标记为 Done，明确 `IAuditLogger::write_audit/export_audit` 和 `AuditBoundaryContractTest` 已冻结最小审计写入接口。
2. 同一文档已将 `AUD-TODO-014`、`AUD-BLK-003` 标记为已完成/已解阻，明确 `AuditHealthStatus` 的 Ready/Degraded/Unavailable 三态、最近失败原因字段与只读 `evaluate()` 语义已经冻结。
3. 同一文档已将 `AUD-TODO-015`、`AUD-BLK-004` 标记为已完成/已解阻，明确 audit 所依赖的 `IMetricsProvider/IMeter` 接入协议、七指标对象表、五元标签白名单与 non-recursive failure semantics 已冻结。
4. infra/include/audit/IAuditLogger.h 与 infra/include/audit/AuditTypes.h 已落盘 `IAuditLogger`、`AuditEvent`、`AuditContext`、`AuditWriteOutcome`；`ctest --test-dir build-ci -L audit` 当前发现并通过 9 个 audit gate 测试，说明上述边界在当前仓库状态下仍有效。

D 结论：

1. POL-BLK-003 的根因已经被 audit 组件专项的对象冻结、接口冻结与 audit gate 验证闭环消除；policy 文档继续保留 Blocked 状态会制造伪阻塞。
2. 本轮不修改 audit 代码，只做 policy 台账解阻校准，并把 `POL-TODO-019` 从 Blocked 校准为可执行的 Not Started。
3. D Gate：PASS。

### 31.3 Build 交付与证据

交付物：

1. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md：将 `POL-BLK-003` 标记为已解阻，更新 `POL-TODO-019` 的状态与阻塞说明，并同步刷新“当前 Blocked 任务索引”“阶段 F 编排”与本轮执行记录。

验收结果：

1. ctest --test-dir build-ci -N -L audit：通过，发现 9 个 audit gate 测试。
2. ctest --test-dir build-ci --output-on-failure -L audit：通过，9/9 tests passed。

Build 合规复核：

1. 根因闭环：本轮不是新增 audit 实现，而是根据 audit 专项已完成的接口冻结和当前 gate 结果消除 policy 侧伪阻塞。
2. 边界保持：本轮只校准 policy 台账，不越界修改 audit 组件的实现或测试。
3. TODO 证据回写：已完成 `POL-BLK-003` 状态、影响任务与解阻依据回写。
4. 提交隔离：本轮提交范围限定为 `POL-BLK-003` 的 policy TODO/worklog 解阻证据同步，不混入 `POL-TODO-019` 的桥接实现。

## 9. 验收与质量门

### 9.1 验收命令基线

| 用途 | 命令 |
|---|---|
| 刷新构建目录 | cmake -S . -B build-ci -G Ninja |
| 构建 infra | cmake --build build-ci --target dasall_infra |
| 构建 unit 聚合目标 | cmake --build build-ci --target dasall_unit_tests |
| 构建 contract 聚合目标 | cmake --build build-ci --target dasall_contract_tests |
| 查看测试发现性 | ctest --test-dir build-ci -N |
| 执行 unit 标签 | ctest --test-dir build-ci --output-on-failure -L unit |
| 执行 contract 标签 | ctest --test-dir build-ci --output-on-failure -L contract |

说明：

1. integration 相关命令当前不纳入首轮 Gate，原因是 POL-TODO-018 尚未落盘具体 integration/failure 用例；顶层 integration 拓扑已于 2026-03-30 解阻。
2. 若 contracts 共享 policy 对象仍未冻结，contract 验收以“语义映射 catalog + contract 测试”作为准入条件。
3. 每个任务至少需要一条构建命令与一条测试命令，禁止只以文档结论判定完成。

### 9.2 质量门逐项回答

1. 是否给出 Design -> TODO 映射，而不是只列任务标题：是。
2. 是否明确当前最细可达到的粒度等级：是，当前整体为 L2，局部对象/接口可达 L3。
3. 是否所有任务都具备代码目标、测试目标、验收命令：是。
4. 是否所有 Blocked 项都带有明确证据和解阻条件：是。
5. 是否所有任务都具备可二值判定完成标准：是。
6. 是否避免跨子系统范围扩张：是。
7. 若要求函数/数据结构级原子任务，输出是否真正落到这些对象：是，已落到 PolicyTypes、PolicyErrors、接口与单链路骨架。

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 回退策略 |
|---|---|---|---|---|
| 规则 schema 漂移 | High | domain/effect/conditions 白名单未冻结就进入实现 | validator/resolver 测试口径反复变化 | 停止推进实现，回退到对象/接口冻结阶段，先关闭 POL-BLK-001 |
| contracts 语义越权 | High | 在共享对象缺失时直接扩写 decision 字段或 include 不存在头文件 | contract 门禁失败、映射 catalog 缺失 | 回退到 PolicyDecisionRef 语义对齐方案，不暴露 shared dependency |
| 默认 allow 形成安全旁路 | High | 未命中规则时未保持 default_effect=deny | unit/contract 用例出现未命中放行 | 固定默认 effect 为 deny，回退投影逻辑 |
| commit 失败导致 current 污染 | High | 提交失败后 current 已切换 | generation/LKG 测试失败 | 回退 SnapshotStore 变更，只允许旧快照继续服务 |
| hot_reload 绕过 dry-run 或审计 | High | apply_patch 未强制 dry_run 或未记录审计 | patch 路径无审计事件或无拒绝原因 | 关闭 hot_reload，回退到只读静态加载 |
| safe_mode 不可复现 | Medium | 连续失败阈值、退出条件和健康状态不稳定 | unit 用例无法稳定复现 safe_mode 进入/退出 | 回退为“只进不出”的只读模式，待阈值模型冻结后再恢复 |
| integration 长期未落盘 | Medium | policy 组件 integration/failure 用例长期未注册 | ctest -N 看不到 policy integration 用例 | 保持 integration 任务为 Not Started，并优先补组件用例 |

## 11. 可行性结论

1. 结论：可直接生成并执行接口/数据结构级专项 TODO，局部可进入方法骨架级；当前不应整体进入函数级实现。
2. 原因：
   - 已具备核心接口清单、对象字段、主异常流程、错误语义、配置项和落盘建议。
   - infra/policy 的职责边界已被架构文档、蓝图和 ADR 明确约束。
   - INF-TODO-017 已完成，INF-BLK-07 也已完成台账校准。
   - 当前前置阻塞主要收敛到 contracts 共享对象缺口与 config/profile 配置读取接口。
   - contracts 共享 policy 对象代码仍未落盘，只能先做语义对齐而不能直接绑定共享头文件。
3. 当前最小可执行粒度：数据结构 / 接口级，局部方法骨架级。
4. 若未达到全面函数级，还缺哪些设计信息：
   - contracts/shared PolicyDecision 的正式冻结结论或 mapping catalog。
   - audit、metrics、health 的最小桥接接口。
   - ConfigCenter 与 profiles 侧 policy 配置键的最终冻结。
5. 下一步建议：
   - 先执行 POL-TODO-001~009，完成对象、错误码与接口冻结。
   - 并行推动 POL-BLK-002、POL-BLK-006 解阻，再进入 validator/resolver/manager 主链。
   - 在 audit、metrics、health 接口冻结前，不把桥接任务误写成 Build-ready。
   - 首轮验收只以 dasall_infra、unit、contract 为准，不把 integration 作为伪完成条件。

## 12. 本轮执行记录（2026-04-01 / POL-TODO-001）

### 12.1 选中任务

1. 本轮任务：POL-TODO-001。
2. 可执行性依据：该任务无前置依赖和阻塞项；当前仓库虽已存在 PolicyBundle.h，但仍缺少本专项 TODO 要求的 PolicyTypes.h 统一冻结入口，适合在不触及 patch/snapshot/query/error 的前提下完成最小收敛。

### 12.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.5 已冻结 policy 规则域、effect、mode、PolicyRuleDescriptor 与 PolicyBundle 的字段边界。
2. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md 5.1、6.1、7.1 明确要求阶段 A 先收敛 PolicyTypes，再推进 patch/snapshot/error 与接口冻结。
3. 当前仓库仅有 infra/include/policy/PolicyBundle.h、PolicyPatch.h、PolicySnapshot.h、PolicyDecisionRef.h、ISecurityPolicyManager.h 分散落盘，尚无 PolicyTypes.h 统一入口，且 tests 侧也缺少直接面向 PolicyTypes 的对象冻结测试。

外部参考：

1. OPA FAQ 的 conflict resolution 与 secure policy authoring 建议强调，策略系统需要显式定义决策语义与冲突优先级，并优先采用 default deny 或 deny override 风格；本轮据此保留 deny-first precedence helper，并把 domain/effect 的字符串映射显式冻结到 PolicyTypes.h，而不把共享 contracts 对象提前引入 infra/policy。

D 结论：

1. Design -> Build 映射：新增 infra/include/policy/PolicyTypes.h 作为 PolicyDomain、PolicyEffect、PolicyMode、PolicyRuleDescriptor、PolicyBundle 的统一冻结入口；既有 infra/include/policy/PolicyBundle.h 退化为兼容转发头，避免破坏现有 include 面。
2. Build 三件套：
   - 代码目标：新增 PolicyTypes.h，并把 PolicyBundle.h 改为兼容转发，同时更新 infra/CMakeLists.txt 公开头文件注册。
   - 测试目标：新增 tests/unit/infra/PolicyTypesRuleBundleTest.cpp 覆盖对象字段完整性正负例；新增 tests/contract/smoke/PolicyTypesBoundaryContractTest.cpp 覆盖 domain/effect 语义映射与 deny-first precedence。
   - 验收命令：优先尝试 Build_CMakeTools；若工作区无法配置，则回退到 cmake -S . -B build-ci -G Ninja、cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci -N -R "PolicyTypesRuleBundleTest|PolicySnapshotCompatibilityTest|PolicyTypesBoundaryContractTest|PolicyDecisionBoundaryTest"、ctest --test-dir build-ci --output-on-failure -L contract。
3. D Gate：PASS。

### 12.3 Build 交付与证据

交付物：

1. infra/include/policy/PolicyTypes.h：新增 policy 统一类型入口，冻结 domain/effect/mode、rule descriptor、bundle 与 effect precedence helper。
2. infra/include/policy/PolicyBundle.h：改为兼容转发，保持既有 include 路径稳定。
3. infra/CMakeLists.txt：把 PolicyTypes.h 纳入 dasall_infra 公共头文件清单。
4. tests/unit/infra/PolicyTypesRuleBundleTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 测试与聚合目标。
5. tests/contract/smoke/PolicyTypesBoundaryContractTest.cpp、tests/contract/CMakeLists.txt：新增并注册 contract 边界测试。

验收结果：

1. Build_CMakeTools：失败，返回“无法配置项目”；按仓库既定回退策略改用 build-ci 命令链，不视为任务阻塞。
2. cmake -S . -B build-ci -G Ninja：通过。
3. cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests：通过。
4. ctest --test-dir build-ci -N -R "PolicyTypesRuleBundleTest|PolicySnapshotCompatibilityTest|PolicyTypesBoundaryContractTest|PolicyDecisionBoundaryTest"：通过，发现 4 个测试。
5. ctest --test-dir build-ci --output-on-failure -R "PolicyTypesRuleBundleTest|PolicySnapshotCompatibilityTest|PolicyTypesBoundaryContractTest|PolicyDecisionBoundaryTest"：通过，4/4 tests passed。
6. ctest --test-dir build-ci --output-on-failure -L contract：通过，116/116 tests passed。
7. ctest --test-dir build-ci --output-on-failure -R "PolicyTypesRuleBundleTest|PolicySnapshotCompatibilityTest"：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：本轮新增类型头与兼容转发结构命名已足够自解释，无需额外冗余注释。
2. 正负例覆盖：unit 覆盖合法 rule/bundle 正例与缺失 reason_code/checksum 负例；contract 覆盖 domain/effect 语义映射正例与 deny-first precedence 负例守卫。
3. 测试发现性：已通过 ctest -N 验证新增 unit/contract 测试进入 CTest 图。
4. TODO 证据回写：已完成主任务状态、交付物和验收结果回写。
5. 提交隔离：本轮提交范围限定为 PolicyTypes 头文件、兼容转发、测试注册与本专项 TODO 文档。

## 13. 本轮执行记录（2026-04-01 / POL-TODO-002）

### 13.1 选中任务

1. 本轮任务：POL-TODO-002。
2. 可执行性依据：前置依赖 POL-TODO-001 已完成，且 POL-BLK-001 已在 2026-03-30 校准解阻；当前只需把分散在 PolicyPatch.h 与 ISecurityPolicyManager.h 的 patch/report 类型收敛回 PolicyTypes.h，并补足直接面向该冻结面的测试。

### 13.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.5、6.6 已冻结 PolicyPatch 的 patch_id、base_generation、operations、actor、reason，以及 ValidationReport 的 blocking_errors、warnings、invalid_rule_ids、field_paths。
2. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md 6.1 明确 POL-TODO-002 的代码目标是把 PolicyPatch 与 ValidationReport 统一落到 PolicyTypes.h，而不是继续分散在独立头文件或接口头中。
3. 当前仓库中 PolicyPatch 仍独立定义在 infra/include/policy/PolicyPatch.h，ValidationReport 仍位于 infra/include/policy/ISecurityPolicyManager.h，尚未满足本专项 TODO 的类型收敛要求。

外部参考：

1. RFC 6902 指出 patch 文档是按顺序执行的一组显式 operation，operation 类型必须来自受控白名单，且一旦任一 operation 失败，整份 patch 不应被视为成功；本轮据此保持 add_rule/replace_rule/remove_rule/update_mode 的显式白名单，并要求 operation 形状可二值判定。
2. Kubernetes API dry-run 语义强调修改请求在持久化前仍应完整经过校验与 merge 冲突检查，但不得产生存储副作用；本轮据此把 ValidationReport 保持为本地字符串报告结构，而不是提前扩展为共享 contracts 对象。

D 结论：

1. Design -> Build 映射：把 ValidationReport、PolicyPatchOperationType、PolicyPatchOperation、PolicyPatch 并入 infra/include/policy/PolicyTypes.h；infra/include/policy/PolicyPatch.h 退化为兼容转发头；infra/include/policy/ISecurityPolicyManager.h 仅保留接口与 PolicyOpResult，不再内嵌 ValidationReport。
2. Build 三件套：
   - 代码目标：更新 PolicyTypes.h、PolicyPatch.h、ISecurityPolicyManager.h，完成 patch/report 类型收敛。
   - 测试目标：新增 tests/unit/infra/PolicyPatchValidationTypesTest.cpp 覆盖 patch 元数据、operation 白名单和 report 阻断语义；新增 tests/contract/smoke/PolicyPatchValidationBoundaryContractTest.cpp 覆盖 patch operation 语义映射和本地报告边界。
   - 验收命令：沿用 build-ci 回退链路执行 cmake -S . -B build-ci -G Ninja、cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci -N -R "PolicyPatchValidationTypesTest|PolicySnapshotCompatibilityTest|PolicyPatchValidationBoundaryContractTest|PolicyDecisionBoundaryTest"、ctest --test-dir build-ci --output-on-failure -R "PolicyPatchValidationTypesTest|PolicySnapshotCompatibilityTest|PolicyPatchValidationBoundaryContractTest|PolicyDecisionBoundaryTest"、ctest --test-dir build-ci --output-on-failure -L contract。
3. D Gate：PASS。

### 13.3 Build 交付与证据

交付物：

1. infra/include/policy/PolicyTypes.h：新增 ValidationReport、PolicyPatchOperationType、PolicyPatchOperation、PolicyPatch，并补充 operation 名称 helper。
2. infra/include/policy/PolicyPatch.h：改为兼容转发，保持既有 include 路径稳定。
3. infra/include/policy/ISecurityPolicyManager.h：删除内嵌 ValidationReport 定义，使接口头只引用统一类型入口。
4. tests/unit/infra/PolicyPatchValidationTypesTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 patch/report unit 测试与聚合目标。
5. tests/contract/smoke/PolicyPatchValidationBoundaryContractTest.cpp、tests/contract/CMakeLists.txt：新增并注册 patch/report contract 边界测试。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests：通过。
3. ctest --test-dir build-ci -N -R "PolicyPatchValidationTypesTest|PolicySnapshotCompatibilityTest|PolicyPatchValidationBoundaryContractTest|PolicyDecisionBoundaryTest"：通过，发现 4 个测试，分别为 PolicySnapshotCompatibilityTest、PolicyPatchValidationTypesTest、PolicyPatchValidationBoundaryContractTest、PolicyDecisionBoundaryTest。
4. ctest --test-dir build-ci --output-on-failure -R "PolicyPatchValidationTypesTest|PolicySnapshotCompatibilityTest|PolicyPatchValidationBoundaryContractTest|PolicyDecisionBoundaryTest"：通过，4/4 tests passed。
5. ctest --test-dir build-ci --output-on-failure -L contract：通过，117/117 tests passed。
6. ctest --test-dir build-ci --output-on-failure -R "PolicyPatchValidationTypesTest|PolicySnapshotCompatibilityTest"：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：本轮新增类型和兼容转发命名直接对应 patch/report 冻结语义，无需额外冗余注释。
2. 正负例覆盖：unit 覆盖合法 add_rule patch 正例、非法 update_mode 负例，以及 ValidationReport 的 warning/blocking 分离；contract 覆盖 patch operation 名称映射正例与非法 remove_rule 负例。
3. 测试发现性：已通过 ctest -N 验证新增 patch/report unit 与 contract 用例进入 CTest 图。
4. TODO 证据回写：已完成 POL-TODO-002 状态、交付物和验收结果回写。
5. 提交隔离：本轮提交范围限定为 patch/report 类型头文件、兼容转发、测试注册与本专项 TODO 文档。

## 14. 本轮执行记录（2026-04-01 / POL-TODO-003）

### 14.1 选中任务

1. 本轮任务：POL-TODO-003。
2. 可执行性依据：前置依赖 POL-TODO-001 已完成，且 004/005 仍受 POL-BLK-002 约束；当前可独立推进的最小闭环是把 PolicySnapshot 与 PolicyOpResult 收敛进 PolicyTypes.h，并补齐 generation/LKG 与 contracts 错误边界测试。

### 14.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.5、6.8 已冻结 PolicySnapshot 的 snapshot_id、generation、version、mode、effective_rules、created_at、source_chain、LKG 引用，以及 PolicyOpResult 的 applied、rolled_back、dry_run、snapshot_id、generation、error_info 语义。
2. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md 6.1 明确 POL-TODO-003 的代码目标是把 PolicySnapshot 与 PolicyOpResult 统一落到 PolicyTypes.h，而不是继续分散在 PolicySnapshot.h 与 ISecurityPolicyManager.h。
3. 当前仓库中 PolicySnapshot 仍独立定义在 infra/include/policy/PolicySnapshot.h，PolicyOpResult 仍内嵌在 infra/include/policy/ISecurityPolicyManager.h，尚未满足本专项 TODO 的统一类型入口要求。

外部参考：

1. Kubernetes API Concepts 对 resourceVersion 的说明强调版本标识应支持单调递增并作为一致性快照引用的稳定锚点；本轮据此保持 policy generation 为显式单调字段，并在测试中把“新快照 generation 严格大于旧快照”作为可验证约束，而不是隐含约定。

D 结论：

1. Design -> Build 映射：把 PolicySnapshot 与 PolicyOpResult 并入 infra/include/policy/PolicyTypes.h；infra/include/policy/PolicySnapshot.h 退化为兼容转发头；infra/include/policy/ISecurityPolicyManager.h 仅保留接口定义并引用统一类型入口。
2. Build 三件套：
   - 代码目标：更新 PolicyTypes.h、PolicySnapshot.h、ISecurityPolicyManager.h，完成 snapshot/op result 类型收敛，并把 PolicyOpResult 字段口径对齐为 applied/error_info。
   - 测试目标：新增 tests/unit/infra/PolicySnapshotOpResultTypesTest.cpp 覆盖 generation 单调、LKG 引用以及 applied/rolled_back/dry_run/error_info 结构语义；更新 tests/contract/smoke/PolicyDecisionBoundaryTest.cpp，确保失败结果仍只落在 contracts::ResultCode/ErrorInfo 边界内。
   - 验收命令：沿用 build-ci 回退链路执行 cmake -S . -B build-ci -G Ninja、cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci -N -R "PolicySnapshotCompatibilityTest|PolicySnapshotOpResultTypesTest|PolicyDecisionBoundaryTest"、ctest --test-dir build-ci --output-on-failure -R "PolicySnapshotCompatibilityTest|PolicySnapshotOpResultTypesTest|PolicyDecisionBoundaryTest"、ctest --test-dir build-ci --output-on-failure -L unit。
3. D Gate：PASS。

### 14.3 Build 交付与证据

交付物：

1. infra/include/policy/PolicyTypes.h：新增 PolicySnapshot、PolicyOpResult，并把 PolicyOpResult 字段口径收敛为 applied/error_info，同时保留 contracts 对齐的 result_code 与 failure helper。
2. infra/include/policy/PolicySnapshot.h：改为兼容转发，保持既有 include 路径稳定。
3. infra/include/policy/ISecurityPolicyManager.h：删除内嵌 PolicyOpResult 定义，使接口头只引用统一类型入口与 PolicyDecisionRef。
4. tests/unit/infra/PolicySnapshotOpResultTypesTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 snapshot/op result unit 测试与聚合目标。
5. tests/contract/smoke/PolicyDecisionBoundaryTest.cpp：更新 contract 边界断言，使错误结果继续只依赖 contracts::ResultCode/ErrorInfo，不暴露额外 shared payload。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests：通过；构建过程中 contract 聚合目标附带执行，117/117 contract tests passed。
3. ctest --test-dir build-ci -N -R "PolicySnapshotCompatibilityTest|PolicySnapshotOpResultTypesTest|PolicyDecisionBoundaryTest"：通过，发现 3 个测试，分别为 PolicySnapshotCompatibilityTest、PolicySnapshotOpResultTypesTest、PolicyDecisionBoundaryTest。
4. ctest --test-dir build-ci --output-on-failure -R "PolicySnapshotCompatibilityTest|PolicySnapshotOpResultTypesTest|PolicyDecisionBoundaryTest"：通过，3/3 tests passed。
5. ctest --test-dir build-ci --output-on-failure -L unit：通过，72/72 tests passed。

Build 合规复核：

1. 代码注释：本轮新增类型与兼容转发命名已直接对应 snapshot/op result 冻结语义，无需额外冗余注释。
2. 正负例覆盖：unit 将覆盖 generation 单调、LKG 引用、success/dry-run/rollback/failure 四类 PolicyOpResult 形态；contract 将继续覆盖 policy operation failure 不扩写 contracts 边界。
3. 测试发现性：已通过 ctest -N 验证 PolicySnapshotCompatibilityTest、PolicySnapshotOpResultTypesTest、PolicyDecisionBoundaryTest 全部进入 CTest 图。
4. TODO 证据回写：已完成 POL-TODO-003 状态、交付物和验收结果回写。
5. 提交隔离：本轮提交范围限定为 snapshot/op result 类型头文件、兼容转发、测试注册、contract 断言与本专项 TODO 文档。

## 15. 本轮执行记录（2026-04-01 / POL-BLK-002）

### 15.1 选中任务

1. 本轮任务：POL-BLK-002。
2. 可执行性依据：POL-TODO-003 完成后，POL-TODO-004、POL-TODO-005、POL-TODO-014、POL-TODO-017 仍共同受阻于“contracts 侧缺少可直接引用的 PolicyDecision 共享对象”；当前最小闭环不是直接扩写 shared object，而是先形成正式 mapping catalog 与 contract gate，把允许共享的 decision 语义和 infra 私有 trace 字段边界冻结下来。

### 15.2 研究与 Design 结论

本地证据：

1. docs/todos/contracts/DASALL_contracts验收整改TODO.md 的 T010 明确指出 PolicyDecision 仍处于“新增对象或替代映射”状态，说明当前不能假定 contracts/include/policy/ 已有可直接 include 的共享头。
2. contracts/include/boundary/ObjectBoundaryCatalog.h、InterfaceCatalog.h 与 ADRFieldMappingGuards.h 已提供仓库认可的替代映射 catalog 模式，适合用于“共享对象暂缺但语义必须先冻结”的场景。
3. infra/include/policy/PolicyDecisionRef.h 已包含 allow/deny/require_confirmation 所需的最小语义字段，但 reason_code、matched_rule_ids、snapshot_id、generation、evidence_ref、warnings 仍属于 infra 私有 trace 信息，不能被误判为共享 contracts 对象表面。
4. docs/todos/contracts/deliverables/DASALL_blueprint对当前contracts差异矩阵-2026-03-23.md 当前已把 PolicyDecision 从 Missing 调整为 Replaced / Partial，说明仓库证据链允许用正式替代映射承接 blueprint 缺口。

D 结论：

1. Design -> Build 映射：新增 contracts/include/boundary/PolicyDecisionMappingCatalog.h，显式冻结 allow/deny/require_confirmation 三个允许共享的 PolicyDecision 语义，并列出 PolicyDecisionRef 中必须保持 infra-private 的 trace 字段 catalog；同时把 PolicyDecisionRef.h 补齐 cstdint 依赖，避免 catalog/contract test 间接 include 失稳。
2. Build 三件套：
   - 代码目标：新增 PolicyDecisionMappingCatalog.h，更新 PolicyDecisionRef.h，并把专项 TODO 与 contracts 差异矩阵同步回写为“替代映射已成立”。
   - 测试目标：新增 tests/contract/smoke/PolicyDecisionMappingCatalogContractTest.cpp，验证三类共享语义覆盖、语义名称稳定性，以及 reason_code/matched_rule_ids/snapshot_id/generation/evidence_ref/warnings 六个 infra 私有字段守卫；同步更新 tests/contract/CMakeLists.txt 完成 CTest 注册。
   - 验收命令：执行 cmake -S . -B build-ci -G Ninja、cmake --build build-ci --target dasall_infra dasall_contract_tests、ctest --test-dir build-ci -N -R "PolicyDecision(BoundaryTest|MappingCatalogContractTest)"、ctest --test-dir build-ci --output-on-failure -R "PolicyDecision(BoundaryTest|MappingCatalogContractTest)"，并复核 contract 全量门禁结果。
3. D Gate：PASS。POL-BLK-002 的最小正式解法是“mapping catalog + contract gate”，而不是在 contracts 侧仓促新增 shared PolicyDecision 对象。

### 15.3 Build 交付与证据

交付物：

1. contracts/include/boundary/PolicyDecisionMappingCatalog.h：新增正式 mapping catalog，冻结 shareable semantics 与 infra-private trace field catalog，并提供 compile-time validation helper。
2. tests/contract/smoke/PolicyDecisionMappingCatalogContractTest.cpp、tests/contract/CMakeLists.txt：新增并注册 contract smoke test，验证 catalog 完整性与字段边界。
3. infra/include/policy/PolicyDecisionRef.h：补齐 cstdint 依赖，保证 PolicyDecisionRef 在独立 include 下保持稳定。
4. docs/todos/contracts/deliverables/DASALL_blueprint对当前contracts差异矩阵-2026-03-23.md：把 PolicyDecision 从 Missing 调整为 Replaced / Partial，并同步汇总计数。
5. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md：解除 POL-TODO-004、POL-TODO-005、POL-TODO-014、POL-TODO-017 的 POL-BLK-002 阻塞，并记录本轮解阻证据。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra dasall_contract_tests：通过；构建过程中 contract 聚合目标附带执行，118/118 contract tests passed。
3. ctest --test-dir build-ci -N -R "PolicyDecision(BoundaryTest|MappingCatalogContractTest)"：通过，发现 2 个测试，分别为 PolicyDecisionMappingCatalogContractTest、PolicyDecisionBoundaryTest。
4. ctest --test-dir build-ci --output-on-failure -R "PolicyDecision(BoundaryTest|MappingCatalogContractTest)"：通过，2/2 tests passed。
5. contract 全量门禁复核：通过，118/118 tests passed。

Build 合规复核：

1. 正式边界：本轮只冻结替代映射 catalog，不把共享 PolicyDecision 对象伪装成已实现 contracts 依赖。
2. 正负例覆盖：contract 覆盖 shareable semantics 正例与 unspecified 不可共享负例，以及 decision 字段不可误标为 infra-private 的边界守卫。
3. 测试发现性：已通过 ctest -N 验证 PolicyDecisionMappingCatalogContractTest 进入 CTest 图，并与既有 PolicyDecisionBoundaryTest 共同构成解阻 gate。
4. TODO 证据回写：已完成 POL-BLK-002 状态、受影响任务解阻、交付物与验收结果回写。
5. 提交隔离：本轮提交范围限定为 mapping catalog、contract 注册与测试、相关文档回写，以及 PolicyDecisionRef 的最小依赖修正。

## 16. 本轮执行记录（2026-04-01 / POL-TODO-004）

### 16.1 选中任务

1. 本轮任务：POL-TODO-004。
2. 可执行性依据：POL-BLK-002 已在上一轮通过 PolicyDecisionMappingCatalog 解阻；当前最小闭环是把仍独立定义在 PolicyDecisionRef.h 的 PolicyDecision、PolicyQueryContext、PolicyDecisionRef 收敛回 PolicyTypes.h，并补一组直接面向统一入口的 unit/contract 证据。

### 16.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.5 已冻结 PolicyQueryContext 的 module、operation、target_type、target_ref、actor_ref、request_id、session_id、trace_id、task_id、profile_id，以及 PolicyDecisionRef 的 decision、reason_code、matched_rule_ids、snapshot_id、generation、evidence_ref、warnings 字段集合。
2. docs/architecture/DASALL_infra_policy模块详细设计.md 6.7 的策略查询流程明确要求 evaluate 读取 current snapshot 后返回 PolicyDecisionRef，并附 matched_rule_ids、reason_code、snapshot_id、evidence_ref。
3. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md 6.1 明确 POL-TODO-004 的代码目标是把这两类对象统一落到 PolicyTypes.h，而不是继续停留在独立头文件。
4. 当前仓库中 PolicyDecision、PolicyQueryContext、PolicyDecisionRef 仍独立定义在 infra/include/policy/PolicyDecisionRef.h，ISecurityPolicyManager.h 与 PolicyDecisionBoundaryTest 仍通过拆分头间接消费，不符合本专项 TODO 的统一入口要求。

D 结论：

1. Design -> Build 映射：把 PolicyDecision、PolicyQueryContext、PolicyDecisionRef 并入 infra/include/policy/PolicyTypes.h；infra/include/policy/PolicyDecisionRef.h 退化为兼容转发头；infra/include/policy/ISecurityPolicyManager.h 与 PolicyDecisionBoundaryTest 直接转向统一入口。
2. Build 三件套：
   - 代码目标：更新 PolicyTypes.h、PolicyDecisionRef.h、ISecurityPolicyManager.h，完成 query/decision ref 类型收敛。
   - 测试目标：新增 tests/unit/infra/PolicyQueryDecisionRefTypesTest.cpp，覆盖 request/session/trace/task/profile 的 unknown 兜底、required field 非空约束，以及 PolicyDecisionRef 的有效负载边界；更新 tests/contract/smoke/PolicyDecisionBoundaryTest.cpp，直接验证统一入口仍只暴露 allow/deny/require_confirmation 语义。
   - 验收命令：执行 cmake -S . -B build-ci -G Ninja、cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci -N -R "Policy(QueryDecisionRefTypesTest|DecisionBoundaryTest|DecisionMappingCatalogContractTest)"、ctest --test-dir build-ci --output-on-failure -R "Policy(QueryDecisionRefTypesTest|DecisionBoundaryTest|DecisionMappingCatalogContractTest)"，并复核 unit/contract 全量门禁。
3. D Gate：PASS。统一入口落在 PolicyTypes.h，兼容 include 面继续由 PolicyDecisionRef.h 承接，不新增 shared contracts 依赖。

### 16.3 Build 交付与证据

交付物：

1. infra/include/policy/PolicyTypes.h：新增 PolicyDecision、PolicyQueryContext、PolicyDecisionRef，完成 policy query/decision types 的统一入口收敛。
2. infra/include/policy/PolicyDecisionRef.h：改为兼容转发，保持既有 include 路径稳定。
3. infra/include/policy/ISecurityPolicyManager.h：删除对拆分头的直接依赖，使 evaluate 通过统一类型入口暴露 PolicyQueryContext/PolicyDecisionRef。
4. tests/unit/infra/PolicyQueryDecisionRefTypesTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 测试与聚合目标，覆盖 unknown 兜底、empty drift 负例与 decision payload 有效性。
5. tests/contract/smoke/PolicyDecisionBoundaryTest.cpp：直接 include policy/PolicyTypes.h，验证统一入口仍只暴露 contracts 对齐的 decision/error 边界。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests：通过；构建过程中 unit 聚合目标附带执行，73/73 unit tests passed；contract 聚合目标附带执行，118/118 contract tests passed。
3. ctest --test-dir build-ci -N -R "Policy(QueryDecisionRefTypesTest|DecisionBoundaryTest|DecisionMappingCatalogContractTest)"：通过，发现 3 个测试，分别为 PolicyQueryDecisionRefTypesTest、PolicyDecisionMappingCatalogContractTest、PolicyDecisionBoundaryTest。
4. ctest --test-dir build-ci --output-on-failure -R "Policy(QueryDecisionRefTypesTest|DecisionBoundaryTest|DecisionMappingCatalogContractTest)"：通过，3/3 tests passed。
5. 警告回归复核：已补齐 unit 测试中的 warnings 初始化，定向重建后未再出现新的本轮警告。

Build 合规复核：

1. 统一入口：PolicyQueryContext/PolicyDecisionRef 已进入 PolicyTypes.h，后续接口冻结与 projector 实现不再依赖拆分定义。
2. 正负例覆盖：unit 覆盖 unknown fallback 正例、empty required field 负例、unspecified decision 负例与 missing evidence 负例；contract 继续覆盖 decision 语义不越权和错误结果只落在 contracts::ResultCode/ErrorInfo。
3. 测试发现性：已通过 ctest -N 验证新增 PolicyQueryDecisionRefTypesTest 与既有 PolicyDecision* contract tests 全部进入 CTest 图。
4. TODO 证据回写：已完成 POL-TODO-004 状态、交付物和验收结果回写。
5. 提交隔离：本轮提交范围限定为 query/decision ref 类型头文件、兼容转发、接口 include 收敛、unit/contract 测试与本专项 TODO 文档。

## 17. 本轮执行记录（2026-04-01 / POL-TODO-005）

### 17.1 选中任务

1. 本轮任务：POL-TODO-005。
2. 可执行性依据：POL-BLK-002 已解阻，且 PolicyTypes、PolicyQueryContext、PolicyDecisionRef 已完成冻结；当前最小闭环是补齐独立的 PolicyErrors.h，把 6.6/6.8 设计列出的 10 个 policy 私有码和 contracts::ResultCode 映射一并固化为 header-only 边界。

### 17.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.6 已明确 10 个建议错误语义：INF_E_POLICY_BUNDLE_INVALID、INF_E_POLICY_SCHEMA_UNSUPPORTED、INF_E_POLICY_CONFLICT_UNRESOLVED、INF_E_POLICY_PATCH_BASE_MISMATCH、INF_E_POLICY_SNAPSHOT_NOT_FOUND、INF_E_POLICY_ROLLBACK_FAILED、INF_E_POLICY_QUERY_DENIED、INF_E_POLICY_SOURCE_UNAVAILABLE、INF_E_POLICY_STORE_COMMIT_FAILED、INF_E_POLICY_DRYRUN_REJECTED。
2. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md 6.1 明确 POL-TODO-005 的代码目标是新增 infra/include/policy/PolicyErrors.h，并以 unit/contract 测试固定错误码稳定性与映射边界。
3. contracts/include/error/ResultCode.h 当前只提供 ValidationFieldMissing、PolicyDenied、ProviderTimeout、RuntimeRetryExhausted 等最小 contracts 失败域，因此 policy 私有码必须映射到现有 contracts 类别，而不能自行扩写 contracts 错误集合。
4. 仓库中已有 infra/include/plugin/PluginErrorCode.h、infra/include/audit/AuditErrors.h 以及对应 unit/contract 测试，说明本轮可沿用“私有码 + 名称函数 + 映射函数 + CTest gate”的既有实现模式。

D 结论：

1. Design -> Build 映射：新增 infra/include/policy/PolicyErrors.h，采用 header-only 形式冻结 10 个 policy 私有码、policy_error_code_name 和 map_policy_error_code；同时把该头文件纳入 infra/CMakeLists.txt 公共头清单。
2. Build 三件套：
   - 代码目标：新增 PolicyErrors.h 与公共头注册，固定 Validation/Policy/Provider/Runtime 四类 contracts 映射落点。
   - 测试目标：新增 tests/unit/infra/PolicyErrorsTest.cpp 覆盖 10 个名字稳定性与关键映射；新增 tests/contract/smoke/PolicyErrorMappingContractTest.cpp 覆盖只映射到现有 contracts::ResultCode、名称仍停留在 INF_E_POLICY_* 本地命名空间；同步更新 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt。
   - 验收命令：执行 cmake -S . -B build-ci -G Ninja、cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci -N -R "Policy(ErrorsTest|ErrorMappingContractTest|DecisionBoundaryTest)"、ctest --test-dir build-ci --output-on-failure -R "Policy(ErrorsTest|ErrorMappingContractTest|DecisionBoundaryTest)"，并复核 unit/contract 全量门禁。
3. D Gate：PASS。PolicyErrors 只冻结 infra 私有码与 contracts 映射，不扩写新的 contracts 错误对象或分类。

### 17.3 Build 交付与证据

交付物：

1. infra/include/policy/PolicyErrors.h：新增 PolicyErrorCode、PolicyErrorMapping、policy_error_code_name 和 map_policy_error_code，冻结 10 个 policy 私有码与 contracts 映射。
2. infra/CMakeLists.txt：把 include/policy/PolicyErrors.h 纳入 dasall_infra 公共头清单。
3. tests/unit/infra/PolicyErrorsTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 测试与聚合目标，覆盖名字稳定性、关键映射与全量 frozen code 覆盖。
4. tests/contract/smoke/PolicyErrorMappingContractTest.cpp、tests/contract/CMakeLists.txt：新增并注册 contract smoke test，验证 policy 私有码只映射到现有 contracts::ResultCode，且名称保持 INF_E_POLICY_* 本地边界。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests：通过；构建过程中 unit 聚合目标附带执行，74/74 unit tests passed；contract 聚合目标附带执行，119/119 contract tests passed。
3. ctest --test-dir build-ci -N -R "Policy(ErrorsTest|ErrorMappingContractTest|DecisionBoundaryTest)"：通过，发现 3 个测试，分别为 PolicyErrorsTest、PolicyDecisionBoundaryTest、PolicyErrorMappingContractTest。
4. ctest --test-dir build-ci --output-on-failure -R "Policy(ErrorsTest|ErrorMappingContractTest|DecisionBoundaryTest)"：通过，3/3 tests passed。

Build 合规复核：

1. 错误域边界：10 个 policy 私有码均留在 infra/policy，本轮未扩写 contracts 错误对象或 categories。
2. 正负例覆盖：unit 覆盖名字稳定性与关键映射；contract 覆盖只使用现有 contracts result code、名称前缀不越界与 mapping reason 非空。
3. 测试发现性：已通过 ctest -N 验证新增 PolicyErrorsTest 与 PolicyErrorMappingContractTest 全部进入 CTest 图。
4. TODO 证据回写：已完成 POL-TODO-005 状态、交付物和验收结果回写。
5. 提交隔离：本轮提交范围限定为 PolicyErrors 头文件、CMake 注册、unit/contract 测试与本专项 TODO 文档。

## 18. 本轮执行记录（2026-04-01 / POL-TODO-006）

### 18.1 选中任务

1. 本轮任务：POL-TODO-006。
2. 可执行性依据：POL-TODO-001~005 已完成，且 006 无阻塞项；当前最小闭环不是提前进入 manager 实现骨架，而是把现有 ISecurityPolicyManager 头文件补齐“接口冻结证据”，包括签名一致性、返回值不可忽略意图，以及 unit/contract 双侧门禁。

### 18.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.6 已明确 ISecurityPolicyManager 只暴露 load_policy、apply_patch、dry_run_patch、snapshot、rollback、evaluate 六个方法。
2. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md 6.1 明确 POL-TODO-006 的完成判定是“6 个方法签名与设计一致，且接口不暴露规则引擎实现细节”。
3. 当前仓库中的 infra/include/policy/ISecurityPolicyManager.h 已具备六个方法雏形，但缺少直接证明“接口未吸收 loader/validator/store 职责”的 unit 编译证据，也缺少直接面向接口头的 contract 返回边界校验。

外部参考：

1. C++ Core Guidelines C.121 / I.25 指出，作为接口使用的基类应保持 pure abstract、无状态，并保留 virtual destructor，以提升边界稳定性。
2. cppreference 对 nodiscard 的说明指出，对不应被静默丢弃的返回值使用该属性，可以促使编译器在调用方忽略结果时给出诊断；这适用于 policy load/apply/dry-run/rollback 这类显式结果边界。

D 结论：

1. Design -> Build 映射：保持 ISecurityPolicyManager 为纯抽象无状态接口，不新增实现细节；对 load_policy、apply_patch、dry_run_patch、rollback 补齐 nodiscard，使六个入口全部明确表达“返回值属于稳定边界的一部分”。
2. Build 三件套：
   - 代码目标：更新 infra/include/policy/ISecurityPolicyManager.h，补齐 nodiscard；不扩张到 SecurityPolicyManager.cpp 或依赖接口实现。
   - 测试目标：新增 tests/unit/infra/PolicyManagerInterfaceTest.cpp，冻结六个方法签名、pure abstract 属性、virtual destructor 以及“不吸收 loader/validator/store 方法”的边界；更新 tests/contract/smoke/PolicyDecisionBoundaryTest.cpp，直接验证 ISecurityPolicyManager 仍只返回 PolicyOpResult / ValidationReport / PolicySnapshot / PolicyDecisionRef 四类已冻结对象。
   - 验收命令：执行 cmake -S . -B build-ci -G Ninja、cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci -N -R "Policy(ManagerInterfaceTest|DecisionBoundaryTest)"、ctest --test-dir build-ci --output-on-failure -R "Policy(ManagerInterfaceTest|DecisionBoundaryTest)"，并复核 unit/contract 全量门禁。
3. D Gate：PASS。006 只冻结入口边界与测试证据，不提前进入 manager 实现骨架。

### 18.3 Build 交付与证据

交付物：

1. infra/include/policy/ISecurityPolicyManager.h：为 load_policy、apply_patch、dry_run_patch、rollback 补齐 nodiscard，统一六个 frozen entrypoints 的返回值使用意图。
2. tests/unit/infra/PolicyManagerInterfaceTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 接口冻结测试与聚合目标，覆盖签名、抽象性、virtual destructor 与 boundary leakage 守卫。
3. tests/contract/smoke/PolicyDecisionBoundaryTest.cpp：新增对 ISecurityPolicyManager 六个方法返回类型的 contract 边界断言，确保返回面仍只依赖已冻结 policy 对象与 contracts::ResultCode/ErrorInfo。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests：通过；构建过程中 unit 聚合目标附带执行，75/75 unit tests passed；contract 聚合目标附带执行，119/119 contract tests passed。
3. ctest --test-dir build-ci -N -R "Policy(ManagerInterfaceTest|DecisionBoundaryTest)"：通过，发现 2 个测试，分别为 PolicyManagerInterfaceTest、PolicyDecisionBoundaryTest。
4. ctest --test-dir build-ci --output-on-failure -R "Policy(ManagerInterfaceTest|DecisionBoundaryTest)"：通过，2/2 tests passed。
5. ctest --test-dir build-ci --output-on-failure -L unit：通过，75/75 tests passed。
6. ctest --test-dir build-ci --output-on-failure -L contract：通过，119/119 tests passed。

Build 合规复核：

1. 代码注释：本轮新增测试和 nodiscard 语义已足够自解释，无需额外冗余注释。
2. 正负例覆盖：unit 覆盖六个签名正例与“不吸收 loader/validator/store 方法”负例；contract 继续覆盖 decision/error 边界，并新增 manager 返回类型边界断言。
3. 测试发现性：已通过 ctest -N 验证新增 PolicyManagerInterfaceTest 进入 CTest 图，且与既有 PolicyDecisionBoundaryTest 共同构成 006 的接口冻结 gate。
4. TODO 证据回写：已完成 POL-TODO-006 状态、交付物和验收结果回写。
5. 提交隔离：本轮提交范围限定为 ISecurityPolicyManager 头文件、unit/contract 测试与本专项 TODO 文档。

## 19. 本轮执行记录（2026-04-01 / POL-TODO-007）

### 19.1 选中任务

1. 本轮任务：POL-TODO-007。
2. 可执行性依据：经核查，POL-BLK-006 的解阻条件已由 config 与 profiles 专项任务事实满足；当前最小闭环不是提前实现 PolicyLoader.cpp，而是先把 IPolicyLoader 作为纯抽象依赖接口落盘，并冻结 source_id/version/checksum 的输出边界承载方式。

### 19.2 研究与 Design 结论

本地证据：

1. docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md 的 CFG-TODO-001 已完成，明确 IConfigCenter 已冻结 load_layers(startup_context) 与 get_typed(query) 最小读取接口。
2. infra/include/config/IConfigCenter.h 与 tests/unit/infra/ConfigCenterInterfaceTest.cpp 已落盘 ConfigStartupContext/ConfigQuery 边界，并证明 profile_id 只允许五档、runtime override 不得越过 profile 保护键。
3. docs/todos/profiles/DASALL_profiles子系统专项TODO.md 的 PRF-TODO-008、PRF-TODO-013 已完成；profiles/include/RuntimePolicyProvider.h 与 tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp 已冻结 runtime_policy.yaml 最小 schema 与 policy 键域。
4. docs/architecture/DASALL_infra_policy模块详细设计.md 6.3 要求 PolicyLoader 输出的 PolicyBundle 保持 source_id、version、checksum 可追溯；6.6 明确 IPolicyLoader 只暴露 load_from_sources() -> PolicyBundle。

D 结论：

1. Design -> Build 映射：POL-BLK-006 可在本轮视为事实解阻。007 只落盘 pure abstract IPolicyLoader，不提前实现真实配置读取；source_id/version/checksum 的可追溯性先冻结在 PolicyBundle 既有 source、schema_version、checksum 边界上，不把 ConfigCenter 或 profiles 细节塞进接口签名。
2. Build 三件套：
   - 代码目标：新增 infra/include/policy/IPolicyLoader.h，并纳入 infra 公共头导出。
   - 测试目标：新增 tests/unit/infra/PolicyLoaderInterfaceTest.cpp，冻结 load_from_sources 签名、pure abstract/virtual destructor 属性，以及“不吸收 load_layers/get_typed/load_snapshot/activate_snapshot 方法”的边界；用最小 stub 证明 source_id/version/checksum 通过 PolicyBundle 出口可追溯。
   - 验收命令：执行 cmake -S . -B build-ci -G Ninja、cmake --build build-ci --target dasall_infra、cmake --build build-ci --target dasall_unit_tests、ctest --test-dir build-ci -N -R PolicyLoaderInterfaceTest、ctest --test-dir build-ci --output-on-failure -R PolicyLoaderInterfaceTest，并复核 unit 全量门禁。
3. D Gate：PASS。007 仍处于接口冻结阶段，不进入 PolicyLoader.cpp 骨架。

### 19.3 Build 交付与证据

交付物：

1. infra/include/policy/IPolicyLoader.h：新增纯抽象 loader 边界，固定 load_from_sources() -> PolicyBundle，并用 nodiscard 强化返回值不可静默忽略的意图。
2. tests/unit/infra/PolicyLoaderInterfaceTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 接口冻结测试与聚合目标，覆盖签名、抽象性、virtual destructor、source_id/version/checksum 对接，以及 boundary leakage 守卫。
3. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md：完成 POL-TODO-007 状态回写，并同步将 POL-BLK-006 标记为已解阻，解除 POL-TODO-010、POL-TODO-015 的阻塞显示。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra：通过。
3. cmake --build build-ci --target dasall_unit_tests：通过；构建过程中 unit 聚合目标附带执行，76/76 unit tests passed。
4. ctest --test-dir build-ci -N -R PolicyLoaderInterfaceTest：通过，发现 1 个测试，即 PolicyLoaderInterfaceTest。
5. ctest --test-dir build-ci --output-on-failure -R PolicyLoaderInterfaceTest：通过，1/1 tests passed。
6. unit 全量门禁复核：通过，76/76 tests passed。

Build 合规复核：

1. 代码注释：本轮接口与测试命名已足够自解释，无需额外注释。
2. 正负例覆盖：unit 既覆盖 load_from_sources 单入口正例，也覆盖“不吸收 ConfigCenter / RuntimePolicyProvider 方法”的负例。
3. 测试发现性：已通过 ctest -N 验证 PolicyLoaderInterfaceTest 进入 CTest 图，并可单独执行。
4. TODO 证据回写：已完成 POL-TODO-007 状态、POL-BLK-006 解阻状态与受影响任务阻塞显示的同步回写。
5. 提交隔离：本轮提交范围限定为 IPolicyLoader 头文件、对应 unit 测试与本专项 TODO 文档。

## 20. 本轮执行记录（2026-04-01 / POL-TODO-008）

### 20.1 选中任务

1. 本轮任务：POL-TODO-008。
2. 可执行性依据：POL-TODO-001、POL-TODO-002 已完成，且 POL-TODO-008 无阻塞项；当前最小闭环不是提前实现 PolicySchemaValidator.cpp，而是先冻结 validate_bundle、validate_patch 两个校验入口及其 ValidationReport 边界，确保后续实现不会把错误结果扩写成 contracts 共享结构或执行模型。

### 20.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.6 已明确 IPolicySchemaValidator 只暴露 validate_bundle(const PolicyBundle&) 与 validate_patch(const PolicySnapshot&, const PolicyPatch&) 两个方法。
2. docs/architecture/DASALL_infra_policy模块详细设计.md 6.8 把 validator 的职责限制为输入异常与 patch/base_generation 之类的结构性校验，而非执行、投影或提交。
3. infra/include/config/IConfigValidator.h 已在仓库内提供现成先例：纯校验接口使用 const 方法、返回本地 validation report，并把 contracts::ResultCode/ErrorInfo 留在更外层 orchestrator 边界。
4. tests/unit/infra/PolicyPatchValidationTypesTest.cpp 与 tests/contract/smoke/PolicyPatchValidationBoundaryContractTest.cpp 已冻结 ValidationReport 的 blocking_errors/warnings/invalid_rule_ids/field_paths 四个字段与局部字符串报告语义。

D 结论：

1. Design -> Build 映射：新增 pure abstract 的 IPolicySchemaValidator，使用 const 方法表达“校验不产生状态副作用”；接口只返回 ValidationReport，不引入 ResultCode/ErrorInfo，也不预埋 DSL 执行或快照提交入口。
2. Build 三件套：
   - 代码目标：新增 infra/include/policy/IPolicySchemaValidator.h，并纳入 infra 公共头导出。
   - 测试目标：新增 tests/unit/infra/PolicySchemaValidatorInterfaceTest.cpp，冻结两个方法签名、抽象性、virtual destructor、bundle/patch 正负例与“不吸收 loader/store/manager 方法”的边界；新增 tests/contract/smoke/PolicySchemaValidatorInterfaceBoundaryContractTest.cpp，直接验证 ValidationReport 仍只保留本地字符串向量字段，不扩写 ResultCode/ErrorInfo。
   - 验收命令：执行 cmake -S . -B build-ci -G Ninja、cmake --build build-ci --target dasall_infra、cmake --build build-ci --target dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci -N -R "PolicySchemaValidatorInterface(Test|BoundaryContractTest)"、ctest --test-dir build-ci --output-on-failure -R "PolicySchemaValidatorInterface(Test|BoundaryContractTest)"，并复核 unit/contract 全量门禁。
3. D Gate：PASS。008 仅冻结校验入口与报告边界，不进入 PolicySchemaValidator.cpp 骨架。

### 20.3 Build 交付与证据

交付物：

1. infra/include/policy/IPolicySchemaValidator.h：新增纯抽象 validator 边界，固定 validate_bundle、validate_patch 两个 const 入口与 ValidationReport 返回面。
2. tests/unit/infra/PolicySchemaValidatorInterfaceTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 接口冻结测试，覆盖签名、抽象性、正负例与 boundary leakage 守卫。
3. tests/contract/smoke/PolicySchemaValidatorInterfaceBoundaryContractTest.cpp、tests/contract/CMakeLists.txt：新增并注册 contract 边界测试，验证 blocking_errors 仍停留在 policy 私有 ValidationReport 中。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra：通过。
3. cmake --build build-ci --target dasall_unit_tests dasall_contract_tests：通过；构建过程中 unit 聚合目标附带执行，77/77 unit tests passed；contract 聚合目标附带执行，120/120 contract tests passed。
4. ctest --test-dir build-ci -N -R "PolicySchemaValidatorInterface(Test|BoundaryContractTest)"：通过，发现 2 个测试，分别为 PolicySchemaValidatorInterfaceTest、PolicySchemaValidatorInterfaceBoundaryContractTest。
5. ctest --test-dir build-ci --output-on-failure -R "PolicySchemaValidatorInterface(Test|BoundaryContractTest)"：通过，2/2 tests passed。
6. unit 全量门禁复核：通过，77/77 tests passed。
7. contract 全量门禁复核：通过，120/120 tests passed。

Build 合规复核：

1. 代码注释：本轮接口与测试命名已足够自解释，无需额外注释。
2. 正负例覆盖：unit 覆盖 bundle/patch 的通过与阻断路径，contract 覆盖 ValidationReport 仅保留本地字符串报告的边界。
3. 测试发现性：已通过 ctest -N 验证 PolicySchemaValidatorInterfaceTest 与 PolicySchemaValidatorInterfaceBoundaryContractTest 同时进入 CTest 图，并可定向执行。
4. TODO 证据回写：已完成 POL-TODO-008 状态、交付物与验收结果回写。
5. 提交隔离：本轮提交范围限定为 IPolicySchemaValidator 头文件、对应 unit/contract 测试与本专项 TODO 文档。

## 21. 本轮执行记录（2026-04-01 / POL-TODO-009）

### 21.1 选中任务

1. 本轮任务：POL-TODO-009。
2. 可执行性依据：POL-TODO-003 已完成，且 POL-TODO-009 无阻塞项；当前最小闭环不是提前实现 PolicySnapshotStore.cpp 或绑定具体持久化介质，而是先冻结 commit/current/last_known_good/get_by_id 四个存储边界，并用 unit 证据固定 generation 与 LKG 语义。

### 21.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.6 已明确 IPolicySnapshotStore 只暴露 commit(const PolicySnapshot&)、current()、last_known_good()、get_by_id(snapshot_id) 四个方法。
2. docs/architecture/DASALL_infra_policy模块详细设计.md 6.8 把 snapshot store 的异常限定为 commit 失败、history/LKG 读写失败和回滚路径，因此接口边界应继续复用已冻结的 PolicyOpResult 与 PolicySnapshot，而不暴露具体后端句柄。
3. infra/include/policy/PolicyTypes.h 与 tests/unit/infra/PolicySnapshotOpResultTypesTest.cpp 已冻结 PolicySnapshot 的 generation/source_chain/last_known_good_ref 语义，以及 PolicyOpResult 的 success/failure/error_info 边界。
4. tests/unit/infra/ConfigSnapshotStoreInterfaceTest.cpp 已在仓库内提供对应先例：commit 入口返回 operation result，读取路径只返回快照对象本身，generation/LKG 单调性通过 unit 断言守卫。

D 结论：

1. Design -> Build 映射：新增 pure abstract 的 IPolicySnapshotStore，commit 返回 PolicyOpResult，读取路径只返回 PolicySnapshot；get_by_id 仅按 snapshot_id 查询，不引入文件路径、数据库句柄或其他后端细节。
2. Build 三件套：
   - 代码目标：新增 infra/include/policy/IPolicySnapshotStore.h，并纳入 infra 公共头导出。
   - 测试目标：新增 tests/unit/infra/PolicySnapshotStoreInterfaceTest.cpp，冻结四个方法签名、抽象性、virtual destructor、generation 单调与 last_known_good 更新语义，以及“不吸收 loader/validator/manager 方法”的边界。
   - 验收命令：执行 cmake -S . -B build-ci -G Ninja、cmake --build build-ci --target dasall_infra、cmake --build build-ci --target dasall_unit_tests、ctest --test-dir build-ci -N -R PolicySnapshotStoreInterfaceTest、ctest --test-dir build-ci --output-on-failure -R PolicySnapshotStoreInterfaceTest，并复核 unit 全量门禁。
3. D Gate：PASS。009 仅冻结 snapshot store 依赖边界，不进入具体持久化或内存版实现骨架。

### 21.3 Build 交付与证据

交付物：

1. infra/include/policy/IPolicySnapshotStore.h：新增纯抽象 snapshot store 边界，固定 commit/current/last_known_good/get_by_id 四个入口与返回对象。
2. tests/unit/infra/PolicySnapshotStoreInterfaceTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 接口冻结测试，覆盖签名、抽象性、generation/LKG 正负例与 boundary leakage 守卫。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra：通过。
3. cmake --build build-ci --target dasall_unit_tests：通过；构建过程中 unit 聚合目标附带执行，78/78 unit tests passed。
4. ctest --test-dir build-ci -N -R PolicySnapshotStoreInterfaceTest：通过，发现 1 个测试，即 PolicySnapshotStoreInterfaceTest。
5. ctest --test-dir build-ci --output-on-failure -R PolicySnapshotStoreInterfaceTest：通过，1/1 tests passed。
6. unit 全量门禁复核：通过，78/78 tests passed。

Build 合规复核：

1. 代码注释：本轮接口与测试命名已足够自解释，无需额外注释。
2. 正负例覆盖：unit 覆盖 commit 成功、generation 单调、LKG 更新、commit 失败不切 current 和缺失快照查询五类路径。
3. 阶段门禁：完成本轮后，POL-TODO-006~009 四个接口头文件将全部落盘，可作为 POL-GATE-02 的事实通过证据。
4. 测试发现性：已通过 ctest -N 验证 PolicySnapshotStoreInterfaceTest 进入 CTest 图，并可单独执行。
5. TODO 证据回写：已完成 POL-TODO-009 状态、交付物与验收结果回写。
6. 提交隔离：本轮提交范围限定为 IPolicySnapshotStore 头文件、对应 unit 测试与本专项 TODO 文档。

## 22. 本轮执行记录（2026-04-05 / POL-TODO-010）

### 22.1 选中任务

1. 本轮任务：POL-TODO-010。
2. 可执行性依据：POL-TODO-001、POL-TODO-007 已完成，且 POL-BLK-006 已于 2026-04-01 解阻；7.1 已明确阶段 C 必须先完成配置读取，再进入 POL-TODO-013 的快照底座，因此本轮应聚焦 loader 的配置读取骨架与 source trace。

### 22.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.3、6.7、6.9 已冻结 PolicyLoader 的输入输出、正常加载流程第 2 步以及 enabled/mode/hot_reload/max_history/default_effect/priority_order 等配置键域。
2. infra/include/config/IConfigCenter.h、infra/src/config/ConfigCenterFacade.cpp 与 tests/unit/infra/ConfigCenterFacadeTest.cpp 已证明 policy 侧当前可依赖的最小读取边界是 load_layers/startup_context 与 get_typed(query)，不应把 ConfigCenter 内部实现泄漏到 policy 接口。
3. profiles/include/RuntimePolicyProvider.h、profiles/src/RuntimePolicyProvider.cpp 与 tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp 已冻结 runtime_policy 的最小 schema，说明 policy loader 只应消费已校准的 profile/deployment 叠层输出。
4. docs/architecture/DASALL_infrastructure子系统详细设计.md 中仍存在 infra.security.policy.mode 与 infra.security.policy.hot_reload 的历史写法漂移；若实现期只接受 infra.security_policy.* 单一路径，将导致 loader 对既有设计证据不兼容，因此需要在私有实现内做最小 alias 兼容。

外部参考：

1. OPA Policy Language 文档指出，策略系统应通过 default 语义避免 undefined，并以 strict mode 强化输入检查；本轮据此把缺失或非法配置回退到 frozen defaults，同时保持 admin patch gate fail-closed，而不是在配置缺省时静默放宽治理。

D 结论：

1. Design -> Build 映射：新增 infra/src/policy/PolicyLoader.h/.cpp 私有实现，基于 IConfigCenter 读取默认/Profile/部署层策略键，构造可追溯的 PolicyBundle source/checksum；同时兼容 infra.security_policy.* 与历史 alias infra.security.policy.*，并在值缺失或非法时回退到 frozen defaults。
2. Build 三件套：
   - 代码目标：落盘 PolicyLoader 私有实现，完成 mode/default_effect/priority_order 归一化、source trace/checksum 装配，以及 admin patch gate/default decision 两条 skeleton rule 的 fail-closed 输出。
   - 测试目标：新增 tests/unit/infra/PolicyLoaderConfigReadTest.cpp，覆盖 strict/compat、alias key、hot_reload 与 frozen default fallback；新增 tests/contract/smoke/PolicyLoaderBoundaryContractTest.cpp，验证 Profile 裁剪不越出 PolicyAdmin 域，且治理输入关闭时 patch gate 仍保持 fail-closed。
   - 验收命令：优先尝试 CMake Tools；若工作区仍无法配置，则按仓库既定回退链路执行 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci -N -R "PolicyLoader(ConfigReadTest|BoundaryContractTest)"、ctest --test-dir build-ci --output-on-failure -R "PolicyLoader(ConfigReadTest|BoundaryContractTest)"、ctest --test-dir build-ci --output-on-failure -L unit、ctest --test-dir build-ci --output-on-failure -L contract。
3. D Gate：PASS。

### 22.3 Build 交付与证据

交付物：

1. infra/src/policy/PolicyLoader.h、infra/src/policy/PolicyLoader.cpp：新增 PolicyLoader 私有实现，读取 enabled/mode/hot_reload/max_history/default_effect/priority_order/require_checksum/dry_run_required/safe_mode_threshold/persist_lkg，并生成带 source/checksum 的 skeleton PolicyBundle。
2. infra/CMakeLists.txt：把 PolicyLoader.cpp 与对应私有头纳入 dasall_infra 构建图，确保 policy 不再只有接口头而无实现入口。
3. tests/unit/infra/PolicyLoaderConfigReadTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 测试，覆盖 strict/compat、alias key、hot_reload 关闭和 frozen default fallback。
4. tests/contract/smoke/PolicyLoaderBoundaryContractTest.cpp、tests/contract/CMakeLists.txt：新增并注册 contract smoke test，验证 Profile 裁剪不会绕出 PolicyAdmin 域，且 governance inputs 关闭时 patch gate 仍返回 fail-closed 决策。

验收结果：

1. CMake Tools：失败，返回“无法配置项目”且未列出可用 targets/tests；按仓库既定回退策略改用 build-ci 命令链，不视为任务阻塞。
2. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
3. cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests：通过；首次回退构建暴露私有头 include 路径错误，修正为 #include "PolicyLoader.h" 后复跑成功。
4. ctest --test-dir build-ci -N -R "PolicyLoader(ConfigReadTest|BoundaryContractTest)"：通过，发现 2 个测试，分别为 PolicyLoaderConfigReadTest 与 PolicyLoaderBoundaryContractTest。
5. ctest --test-dir build-ci --output-on-failure -R "PolicyLoader(ConfigReadTest|BoundaryContractTest)"：通过，2/2 tests passed。
6. ctest --test-dir build-ci --output-on-failure -L unit：通过，120/120 tests passed。
7. ctest --test-dir build-ci --output-on-failure -L contract：通过，134/134 tests passed。

Build 合规复核：

1. 配置边界：PolicyLoader 仅依赖 IConfigCenter 最小读取接口，不反向吸收 runtime、prompt、tool 或业务模块职责。
2. 正负例覆盖：unit 覆盖 strict/compat、alias key、缺失值 fallback 与 hot_reload 关闭路径；contract 覆盖 PolicyAdmin 域边界与 disabled governance fail-closed 守卫。
3. 测试发现性：已通过 ctest -N 和定向执行验证新增 loader unit/contract 用例进入 CTest 图，并补充 unit/contract 标签级全量门禁复核。
4. 回退链路：CMake Tools 的“无法配置项目”属于仓库已知工具态问题；本轮已按 build-ci 回退链路保留完整构建与测试证据。
5. TODO 证据回写：已完成 POL-TODO-010 状态、交付物与验收结果回写。
6. 提交隔离：本轮提交范围限定为 PolicyLoader 私有实现、CMake/test 接线与本专项 TODO 文档。

## 23. 本轮执行记录（2026-04-05 / POL-TODO-013）

### 23.1 选中任务

1. 本轮任务：POL-TODO-013。
2. 可执行性依据：POL-TODO-003、POL-TODO-009 已完成，且 POL-TODO-010 已在上一轮完成并推送；根据 7.1 的阶段 C 串行要求，当前应在“先读取配置”之后补齐快照存储 generation/LKG 底座，再进入 validator/resolver/manager 主链。

### 23.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.3、6.7、6.8 已冻结 PolicySnapshotStore 的 commit/current/last_known_good/get_by_id 四个入口，并要求 load/apply_patch 成功后生成新 generation 快照，commit 失败时 current/LKG 保持旧值。
2. infra/include/policy/IPolicySnapshotStore.h 与 tests/unit/infra/PolicySnapshotStoreInterfaceTest.cpp 已冻结纯抽象接口、generation 单调、history 按 snapshot_id 可读和 commit failure 不切 current 的边界。
3. infra/src/config/ConfigSnapshotStore.cpp 与 tests/unit/infra/ConfigSnapshotStoreTest.cpp 已给出仓库认可的最小 store 骨架模式：内存态 history + current/LKG + 单调版本约束 + 失败不污染已有状态；policy snapshot store 可沿用该收敛方式，而不提前引入持久化后端。
4. POL-TODO-010 已落盘 PolicyLoader skeleton，并输出可追溯 source/checksum 的 PolicyBundle；本轮只需承接 snapshot lifecycle，不应越界进入 manager、audit、metrics 或 health 桥接职责。

外部参考：

1. Kubernetes API Concepts 对 resourceVersion 的说明强调，快照版本应作为单调递增的一致性锚点，失败写入不能破坏当前可读状态；本轮据此把 generation 单调、history 可读和 commit failure 不切 current/LKG 固化到 PolicySnapshotStore 骨架与 unit 测试中。

D 结论：

1. Design -> Build 映射：新增 infra/src/policy/PolicySnapshotStore.h/.cpp 私有实现，提供内存版 current/history/LKG 存储、generation 单调校验、缺省 last_known_good_ref 回填，以及可控的 commit failure 注入；不引入外部持久化介质或 rollback 主链实现。
2. Build 三件套：
   - 代码目标：落盘 PolicySnapshotStore 私有实现，完成 commit/current/last_known_good/get_by_id、bounded history trim 和 fail-next-commit test seam。
   - 测试目标：新增 tests/unit/infra/PolicySnapshotStoreTest.cpp，覆盖成功提交、generation 自增、history trim、last_known_good linkage 和 injected commit failure 不切 current/LKG；同步更新 infra/tests unit CMake 接线。
   - 验收命令：优先尝试 CMake Tools / RunCtest；若工作区仍无法配置，则按仓库既定回退链路执行 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_infra dasall_unit_tests、ctest --test-dir build-ci -N -R "PolicySnapshotStore(InterfaceTest|Test)"、ctest --test-dir build-ci --output-on-failure -R "PolicySnapshotStore(InterfaceTest|Test)"、ctest --test-dir build-ci --output-on-failure -L unit。
3. D Gate：PASS。

### 23.3 Build 交付与证据

交付物：

1. infra/src/policy/PolicySnapshotStore.h、infra/src/policy/PolicySnapshotStore.cpp：新增 PolicySnapshotStore 私有实现，提供内存版 current/history/LKG 存储、generation 单调校验、bounded history trim 与 injected commit failure seam。
2. infra/CMakeLists.txt：把 PolicySnapshotStore.cpp 与对应私有头纳入 dasall_infra 构建图。
3. tests/unit/infra/PolicySnapshotStoreTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 测试与聚合目标，覆盖 generation 自增、history trim、LKG linkage 与 commit failure 保持旧状态。

验收结果：

1. CMake Tools / RunCtest：失败，返回“无法配置项目”且未列出可用 targets/tests；按仓库既定回退策略改用 build-ci 命令链，不视为任务阻塞。
2. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
3. cmake --build build-ci --target dasall_infra dasall_unit_tests：通过；构建过程中 unit 聚合目标附带执行，121/121 unit tests passed。
4. ctest --test-dir build-ci -N -R "PolicySnapshotStore(InterfaceTest|Test)"：通过，发现 2 个测试，分别为 PolicySnapshotStoreInterfaceTest 与 PolicySnapshotStoreTest。
5. ctest --test-dir build-ci --output-on-failure -R "PolicySnapshotStore(InterfaceTest|Test)"：通过，2/2 tests passed。
6. ctest --test-dir build-ci --output-on-failure -L unit：通过，121/121 tests passed。

Build 合规复核：

1. 生命周期边界：PolicySnapshotStore 仍只承接 snapshot persistence 语义，不吸收 loader、validator、manager、audit 或 rollback orchestrator 职责。
2. 正负例覆盖：unit 覆盖成功提交、generation 单调、history trim、缺省 LKG linkage、invalid snapshot、non-monotonic commit 与 injected commit failure 七类路径。
3. 测试发现性：已通过 ctest -N 与定向执行验证 PolicySnapshotStoreInterfaceTest、PolicySnapshotStoreTest 均进入 CTest 图，并补充 unit 标签级全量门禁复核。
4. 回退链路：CMake Tools / RunCtest 的“无法配置项目”属于仓库已知工具态问题；本轮已按 build-ci 回退链路保留完整构建与测试证据。
5. TODO 证据回写：已完成 POL-TODO-013 状态、交付物与验收结果回写。
6. 提交隔离：本轮提交范围限定为 PolicySnapshotStore 私有实现、unit/CMake 接线与本专项 TODO 文档。

## 24. 本轮执行记录（2026-04-05 / POL-TODO-011）

### 24.1 选中任务

1. 本轮任务：POL-TODO-011。
2. 可执行性依据：POL-TODO-002、POL-TODO-005、POL-TODO-008 已完成，且 POL-BLK-001 已于 2026-03-30 解阻；根据 7.1 阶段 D 的串行要求，当前应先落地 schema validator，再进入 POL-TODO-012 的 conflict resolver。

### 24.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.3、6.7、6.8 已冻结 PolicySchemaValidator 的职责边界：只负责 bundle/patch 的结构、字段完整性、版本兼容和 patch base 校验，不负责投影、提交或执行。
2. infra/include/policy/IPolicySchemaValidator.h 与 tests/unit/infra/PolicySchemaValidatorInterfaceTest.cpp 已冻结 validate_bundle(const PolicyBundle&) 和 validate_patch(const PolicySnapshot&, const PolicyPatch&) 两个 const 入口，并要求失败信息必须可定位到 invalid_rule_ids/field_paths。
3. tests/contract/smoke/PolicySchemaValidatorInterfaceBoundaryContractTest.cpp 与 tests/contract/smoke/PolicyPatchValidationBoundaryContractTest.cpp 已冻结 ValidationReport 仍是 policy 私有字符串向量边界，不得引入 contracts::ResultCode/ErrorInfo 字段。
4. POL-TODO-010 与 POL-TODO-013 已分别落盘 loader 和 snapshot store 底座；本轮只承接非法 bundle、非法 patch 和 base_generation mismatch 的最小验证闭环，不扩张到 conflict resolution 或 manager 主链。

外部参考：

1. JSON Schema object/reference 文档指出，`required` 用于把缺失字段显式判为 invalid，并且额外属性与字段约束需要通过明确的 property/path 规则定位；本轮据此把缺字段、schema_version 不兼容和 patch operation 形状错误全部收敛到可定位的 field_paths，而不是返回模糊的通用失败。

D 结论：

1. Design -> Build 映射：新增 infra/src/policy/PolicySchemaValidator.h/.cpp 私有实现，按 bundle/policy rule/patch operation 三层校验缺字段、未知 domain、非法 effect、unsupported schema_version 和 base_generation mismatch，并输出稳定的 ValidationReport。
2. Build 三件套：
   - 代码目标：落盘 PolicySchemaValidator 私有实现，完成 validate_bundle/validate_patch 最小骨架与 rule/operation 级 field_paths 生成。
   - 测试目标：新增 tests/unit/infra/PolicySchemaValidatorTest.cpp，覆盖 valid bundle/patch、缺字段、未知 domain、非法 effect、schema_version 不兼容和 base_generation mismatch；新增 tests/contract/smoke/PolicySchemaValidatorBoundaryContractTest.cpp，验证实现仍只输出 policy 本地 ValidationReport 字段而不扩写 contracts 错误对象。
   - 验收命令：优先尝试 CMake Tools / RunCtest；若工作区仍无法配置，则按仓库既定回退链路执行 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci -N -R "PolicySchemaValidator(Test|BoundaryContractTest)"、ctest --test-dir build-ci --output-on-failure -R "PolicySchemaValidator(Test|BoundaryContractTest)"、ctest --test-dir build-ci --output-on-failure -L unit、ctest --test-dir build-ci --output-on-failure -L contract。
3. D Gate：PASS。

### 24.3 Build 交付与证据

交付物：

1. infra/src/policy/PolicySchemaValidator.h、infra/src/policy/PolicySchemaValidator.cpp：新增 PolicySchemaValidator 私有实现，覆盖 bundle 字段缺失、schema_version 不兼容、未知 domain、非法 effect、patch operation 形状错误和 base_generation mismatch。
2. infra/CMakeLists.txt：把 PolicySchemaValidator.cpp 与对应私有头纳入 dasall_infra 构建图。
3. tests/unit/infra/PolicySchemaValidatorTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 测试与聚合目标，覆盖 valid/invalid bundle 与 valid/invalid patch 两组正负例。
4. tests/contract/smoke/PolicySchemaValidatorBoundaryContractTest.cpp、tests/contract/CMakeLists.txt：新增并注册 contract smoke test，验证实现保持本地 ValidationReport 边界。

验收结果：

1. CMake Tools / RunCtest：失败，返回“无法配置项目”且未列出可用 targets/tests；按仓库既定回退策略改用 build-ci 命令链，不视为任务阻塞。
2. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
3. cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests：通过；构建过程中 unit 聚合目标附带执行，122/122 unit tests passed；contract 聚合目标附带执行，135/135 contract tests passed。
4. ctest --test-dir build-ci -N -R "PolicySchemaValidator(Test|BoundaryContractTest)"：通过，发现 2 个测试，分别为 PolicySchemaValidatorTest 与 PolicySchemaValidatorBoundaryContractTest。
5. ctest --test-dir build-ci --output-on-failure -R "PolicySchemaValidator(Test|BoundaryContractTest)"：通过，2/2 tests passed。
6. ctest --test-dir build-ci --output-on-failure -L unit：通过，122/122 tests passed。
7. ctest --test-dir build-ci --output-on-failure -L contract：通过，135/135 tests passed。

Build 合规复核：

1. 代码注释：validator 逻辑以 field-level helper 和稳定命名拆分，当前实现已可直接从函数名与字段路径读出意图，无需额外冗余注释。
2. 正负例覆盖：unit 覆盖 valid bundle/patch 正例以及缺字段、未知 domain、非法 effect、unsupported schema 和 base_generation mismatch 负例；contract 覆盖实现边界仍只使用本地 ValidationReport 字符串字段。
3. 测试发现性：已通过 ctest -N 与定向执行验证新增 validator unit/contract 用例进入 CTest 图，并补充 unit/contract 标签级全量门禁复核。
4. 回退链路：CMake Tools / RunCtest 的“无法配置项目”属于仓库已知工具态问题；本轮已按 build-ci 回退链路保留完整构建与测试证据。
5. TODO 证据回写：已完成 POL-TODO-011 状态、交付物与验收结果回写。
6. 提交隔离：本轮提交范围限定为 PolicySchemaValidator 私有实现、unit/contract/CMake 接线与本专项 TODO 文档。

## 25. 本轮执行记录（2026-04-05 / POL-TODO-012）

### 25.1 选中任务

1. 本轮任务：POL-TODO-012。
2. 可执行性依据：POL-TODO-001、POL-TODO-008、POL-TODO-010 已完成，且 POL-BLK-001 已于 2026-03-30 解阻；根据 7.1 阶段 D 的串行要求，当前应先把 deny-first / explicit-priority 冲突裁定最小闭环落地，再进入 014 的查询投影骨架。

### 25.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.3、6.7、6.8、6.9 已冻结 PolicyConflictResolver 的职责边界：输入是通过校验的规则集合，输出是 EffectivePolicySet，必须满足 deny 优先、优先级显式、冲突可解释。
2. POL-TODO-010 已在 loader skeleton 中把 `priority_order` 固化进规则 conditions，且区分了 `deny-first` 与 `explicit-priority` 两档，这为 resolver 提供了当前最小可读的顺序输入。
3. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md 6.1 已把 012 的完成判定锁定为“两档裁定可稳定验证，且冲突未决时返回显式拒绝而非静默覆盖”。
4. 当前仓库仍无 public EffectivePolicySet 类型，因此本轮应保持私有返回面，不新增公共接口或 shared contracts 依赖，只为后续 manager/projector 提供最小私有骨架。

外部参考：

1. Apache Casbin Priority Model 文档明确说明：显式优先级模式下数值越小优先级越高，而 `priority(...) || deny` 的效果模型允许在多条规则命中时以优先级优先并保留 deny 兜底；本轮据此把 explicit-priority 落成“先比 priority，再比 effect precedence”，并保留 deny-first 模式作为单独分支。

D 结论：

1. Design -> Build 映射：新增 infra/src/policy/PolicyConflictResolver.h/.cpp 私有实现，输入 `PolicyBundle`，输出私有 `ConflictResolutionResult`，提供 deny-first、explicit-priority、compat tie downgrade 和 unresolved tie reject 四类最小结果。
2. Build 三件套：
   - 代码目标：落盘 PolicyConflictResolver 私有实现，按 scope key 分组规则，并基于 `priority_order` 计算排序与 unresolved tie 检测。
   - 测试目标：新增 tests/unit/infra/PolicyConflictResolverTest.cpp，覆盖 deny-first 优先 deny、explicit-priority 优先小数值 priority、enforced same-rank unresolved reject，以及 compatibility-only same-rank downgrade warning。
   - 验收命令：优先尝试 CMake Tools；若工作区仍无法配置，则按仓库既定回退链路执行 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_infra dasall_unit_tests、ctest --test-dir build-ci -N -R PolicyConflictResolverTest、ctest --test-dir build-ci --output-on-failure -R PolicyConflictResolverTest、ctest --test-dir build-ci --output-on-failure -L unit。
3. D Gate：PASS。

### 25.3 Build 交付与证据

交付物：

1. infra/src/policy/PolicyConflictResolver.h、infra/src/policy/PolicyConflictResolver.cpp：新增 PolicyConflictResolver 私有实现与私有 `ConflictResolutionResult`，支持按 scope key 分组的 deny-first / explicit-priority 冲突裁定、compat downgrade warning 和 unresolved tie reject。
2. infra/CMakeLists.txt：把 PolicyConflictResolver.cpp 与对应私有头纳入 dasall_infra 构建图。
3. tests/unit/infra/PolicyConflictResolverTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 测试与聚合目标，覆盖两档裁定路径与 unresolved/compat 两类边界。

验收结果：

1. CMake Tools / RunCtest：失败，返回“无法配置项目”且未列出可用 targets/tests；按仓库既定回退策略改用 build-ci 命令链，不视为任务阻塞。
2. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
3. cmake --build build-ci --target dasall_infra dasall_unit_tests：通过；修正一次同-rank 测试夹具后，unit 聚合目标最终通过，123/123 unit tests passed。
4. ctest --test-dir build-ci -N -R PolicyConflictResolverTest：通过，发现 1 个测试，即 PolicyConflictResolverTest。
5. ctest --test-dir build-ci --output-on-failure -R PolicyConflictResolverTest：通过，1/1 tests passed。
6. ctest --test-dir build-ci --output-on-failure -L unit：通过，123/123 tests passed。

Build 合规复核：

1. 私有边界：resolver 仍保持私有返回面，没有引入新的 public EffectivePolicySet 类型或 shared contracts 依赖。
2. 正负例覆盖：unit 覆盖 deny-first、explicit-priority 两条正例，以及 enforced unresolved reject 与 compat downgrade warning 两条负/降级路径。
3. 测试发现性：已通过 ctest -N 验证 PolicyConflictResolverTest 进入 CTest 图，并补充定向执行与 unit 标签级全量门禁复核。
4. 回退链路：CMake Tools / RunCtest 的“无法配置项目”属于仓库已知工具态问题；本轮已按 build-ci 回退链路保留完整构建与测试证据。
5. TODO 证据回写：已完成 POL-TODO-012 状态、交付物与验收结果回写。
6. 提交隔离：本轮提交范围限定为 PolicyConflictResolver 私有实现、unit/CMake 接线与本专项 TODO 文档。

## 26. 本轮执行记录（2026-04-05 / POL-TODO-014）

### 26.1 选中任务

1. 本轮任务：POL-TODO-014。
2. 可执行性依据：POL-TODO-004、POL-TODO-012、POL-TODO-013 已完成，且 POL-BLK-002 已于 2026-04-01 通过 PolicyDecisionMappingCatalog 解阻；根据 7.1 阶段 D 的串行要求，当前应先把查询投影骨架落地，再进入 POL-TODO-015 的 manager 主链。

### 26.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.3、6.5、6.7 已冻结 PolicyDecisionProjector 的职责边界：输入是 QueryContext + EffectivePolicySet，输出是 PolicyDecisionRef/DecisionTrace，且只能投影结果与证据，不泄露内部规则实现。
2. contracts/include/boundary/PolicyDecisionMappingCatalog.h 与 tests/contract/smoke/PolicyDecisionMappingCatalogContractTest.cpp 已冻结 allow/deny/require_confirmation 三类可共享 decision 语义，以及 reason_code/matched_rule_ids/snapshot_id/generation/evidence_ref/warnings 六个 infra-private trace 字段边界。
3. POL-TODO-010 已在 loader skeleton 中生成 `evaluate_default` / `policy:default_effect` 默认规则；这为 projector 提供了未命中查询时的最小 fail-closed 回退锚点。
4. POL-TODO-012 与 POL-TODO-013 已分别落盘 conflict resolver 与 snapshot store 底座；本轮应直接从 `PolicySnapshot::effective_rules` 做最小投影，不新增 public EffectivePolicySet 或 shared contracts 对象。

外部参考：

1. Open Policy Agent 的 `default` 关键字文档明确指出：当同名规则全部为 undefined 时，default 值用于提供显式回退结果；本轮据此把 query miss 收敛到显式 default_effect 路径，而不是返回 undefined 或隐式放行。

D 结论：

1. Design -> Build 映射：新增 infra/src/policy/PolicyDecisionProjector.h/.cpp 私有实现，按 query module 映射 domain，结合 target_selector specificity、priority、effect 生成 PolicyDecisionRef，并在未命中时回退到 default_effect 规则。
2. Build 三件套：
   - 代码目标：落盘 PolicyDecisionProjector 私有实现，完成 domain 映射、target_selector specificity 优先、default_effect fallback、observe -> allow 投影告警，以及 evidence_ref 锚点生成。
   - 测试目标：新增 tests/unit/infra/PolicyDecisionProjectorTest.cpp，覆盖 direct hit、default miss、require_confirmation、deny 四类投影；新增 tests/contract/smoke/PolicyDecisionProjectorBoundaryContractTest.cpp，验证 projector 输出仍受 decision semantic catalog 与 evidence_ref private-field catalog 约束。
   - 验收命令：优先尝试 CMake Tools；若工作区仍无法配置，则按仓库既定回退链路执行 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci -N -R "PolicyDecisionProjector(Test|BoundaryContractTest)"、ctest --test-dir build-ci --output-on-failure -R "PolicyDecisionProjector(Test|BoundaryContractTest)"、ctest --test-dir build-ci --output-on-failure -L unit、ctest --test-dir build-ci --output-on-failure -L contract。
3. D Gate：PASS。

### 26.3 Build 交付与证据

交付物：

1. infra/src/policy/PolicyDecisionProjector.h、infra/src/policy/PolicyDecisionProjector.cpp：新增 PolicyDecisionProjector 私有实现，支持 query module -> domain 映射、target_selector specificity 优先、default_effect fallback、observe 投影告警和 evidence_ref 生成。
2. infra/CMakeLists.txt：把 PolicyDecisionProjector.cpp 与对应私有头纳入 dasall_infra 构建图。
3. tests/unit/infra/PolicyDecisionProjectorTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 测试与聚合目标，覆盖命中、未命中、require_confirmation、deny 四类投影路径。
4. tests/contract/smoke/PolicyDecisionProjectorBoundaryContractTest.cpp、tests/contract/CMakeLists.txt：新增并注册 contract smoke test，验证 projector 输出仍停留在 PolicyDecisionRef 与 mapping catalog 的冻结边界内。

验收结果：

1. ListBuildTargets_CMakeTools / ListTests_CMakeTools：返回空 targets/tests；工作区 IDE 工具态仍未恢复。
2. Build_CMakeTools / RunCtest_CMakeTools：失败，返回“生成失败: 无法配置项目”；按仓库既定回退策略改用 build-ci 命令链，不视为任务阻塞。
3. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
4. cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests：通过；构建过程中 unit 聚合目标附带执行，124/124 unit tests passed；contract 聚合目标附带执行，136/136 contract tests passed。
5. ctest --test-dir build-ci -N -R "PolicyDecisionProjector(Test|BoundaryContractTest)"：通过，发现 2 个测试，分别为 PolicyDecisionProjectorTest 与 PolicyDecisionProjectorBoundaryContractTest。
6. ctest --test-dir build-ci --output-on-failure -R "PolicyDecisionProjector(Test|BoundaryContractTest)"：通过，2/2 tests passed。
7. ctest --test-dir build-ci --output-on-failure -L unit：通过，124/124 tests passed。
8. ctest --test-dir build-ci --output-on-failure -L contract：通过，136/136 tests passed。

Build 合规复核：

1. 私有边界：projector 仍保持私有实现面，没有引入 public EffectivePolicySet、DecisionTrace 公共对象或 shared PolicyDecision 共享对象扩张。
2. 正负例覆盖：unit 覆盖 direct allow hit、default deny miss、require_confirmation 与 specificity-first deny 四类路径；contract 覆盖 decision semantic catalog 与 evidence_ref private audit anchor 两类边界。
3. 测试发现性：已通过 ctest -N 与定向执行验证新增 projector unit/contract 用例进入 CTest 图，并补充 unit/contract 标签级全量门禁复核。
4. 回退链路：CMake Tools / RunCtest 的“无法配置项目”属于仓库已知工具态问题；本轮已按 build-ci 回退链路保留完整构建与测试证据。
5. TODO 证据回写：已完成 POL-TODO-014 状态、交付物与验收结果回写。
6. 提交隔离：本轮提交范围限定为 PolicyDecisionProjector 私有实现、unit/contract/CMake 接线与本专项 TODO 文档。

## 27. 本轮执行记录（2026-04-05 / POL-TODO-015）

### 27.1 选中任务

1. 本轮任务：POL-TODO-015。
2. 可执行性依据：POL-TODO-006、POL-TODO-010、POL-TODO-011、POL-TODO-012、POL-TODO-013、POL-TODO-014 已完成，且 7.1 阶段 D 的串行主链已推进至 manager 主控节点；当前可以在不扩张公共接口的前提下闭合 load/dry_run/apply/query/rollback/safe_mode 最小主链。

### 27.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_policy模块详细设计.md 6.2、6.4、6.7、6.8 已冻结 SecurityPolicyManager 的职责边界：它是对外统一入口，负责串接 validator、resolver、snapshot store 与 projector，并在 load/apply_patch/rollback 失败时保持 current/LKG 稳定。
2. infra/include/policy/ISecurityPolicyManager.h 与 tests/unit/infra/PolicyManagerInterfaceTest.cpp 已冻结 manager 只暴露六个入口：load_policy、apply_patch、dry_run_patch、snapshot、rollback、evaluate；不允许吸收 loader、commit 或内部规则集公共接口。
3. POL-TODO-010 的 loader skeleton 已把 `dry_run_required` 和 `safe_mode_threshold` 固化进 admin patch gate rule.conditions；本轮可直接从激活后的 effective rules 解析这两个治理开关，而不引入新的配置入口。
4. POL-TODO-011 至 POL-TODO-014 已分别落盘 validator、resolver、snapshot store、projector；本轮只负责 orchestrate 它们，不提前吸收 audit/metrics/health 桥接职责。

外部参考：

1. Martin Fowler 的 Circuit Breaker 说明强调：当失败次数达到阈值后，应立即打开断路并让后续调用 fail-fast，直到显式恢复或重置；本轮据此把连续 patch 失败后的 safe_mode 收敛为“拒绝后续 apply_patch、保留只读 query 路径”的最小 fail-fast 语义。

D 结论：

1. Design -> Build 映射：新增 infra/src/policy/SecurityPolicyManager.h/.cpp 私有实现，围绕 validator、resolver、snapshot store、projector 构建 load_policy、dry_run_patch、apply_patch、rollback、evaluate 主链，并维护连续 patch 失败计数与 safe_mode 状态。
2. Build 三件套：
   - 代码目标：落盘 SecurityPolicyManager 私有实现，完成 bundle validate/resolve/commit、patch dry-run gate、apply fail-closed、rollback clone-commit、query projector 转发，以及从 patch gate rule 解析 dry_run_required/safe_mode_threshold。
   - 测试目标：新增 tests/unit/infra/SecurityPolicyManagerTest.cpp，覆盖正常 load+evaluate、patch 失败不切 current、dry_run+apply 后 rollback 成功、连续失败进入 safe_mode；新增 tests/contract/smoke/SecurityPolicyManagerFailureContractTest.cpp，验证 dry-run reject 与 safe_mode reject 仍映射到冻结的 policy failure domain。
   - 验收命令：优先尝试 CMake Tools；若工作区仍无法配置，则按仓库既定回退链路执行 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci -N -R "SecurityPolicyManager(Test|FailureContractTest)"、ctest --test-dir build-ci --output-on-failure -R "SecurityPolicyManager(Test|FailureContractTest)"、ctest --test-dir build-ci --output-on-failure -L unit、ctest --test-dir build-ci --output-on-failure -L contract。
3. D Gate：PASS。

### 27.3 Build 交付与证据

交付物：

1. infra/src/policy/SecurityPolicyManager.h、infra/src/policy/SecurityPolicyManager.cpp：新增 SecurityPolicyManager 私有实现，完成 load_policy、dry_run_patch、apply_patch、snapshot、rollback、evaluate 六条主链和连续 patch 失败进入 safe_mode 的最小状态机。
2. infra/CMakeLists.txt：把 SecurityPolicyManager.cpp 与对应私有头纳入 dasall_infra 构建图。
3. tests/unit/infra/SecurityPolicyManagerTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：新增并注册 unit 测试与聚合目标，覆盖 load+evaluate、patch reject 不切 current、rollback 成功、连续失败进入 safe_mode 四类路径。
4. tests/contract/smoke/SecurityPolicyManagerFailureContractTest.cpp、tests/contract/CMakeLists.txt：新增并注册 contract smoke test，验证 apply_patch 拒绝结果仍停留在 contracts 的 policy failure domain。

验收结果：

1. ListBuildTargets_CMakeTools / ListTests_CMakeTools：返回空 targets/tests；工作区 IDE 工具态仍未恢复。
2. Build_CMakeTools / RunCtest_CMakeTools：失败，返回“生成失败: 无法配置项目”；按仓库既定回退策略改用 build-ci 命令链，不视为任务阻塞。
3. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
4. cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests：通过；构建过程中 unit 聚合目标附带执行，125/125 unit tests passed；contract 聚合目标附带执行，137/137 contract tests passed。
5. ctest --test-dir build-ci -N -R "SecurityPolicyManager(Test|FailureContractTest)"：通过，发现 2 个测试，分别为 SecurityPolicyManagerTest 与 SecurityPolicyManagerFailureContractTest。
6. ctest --test-dir build-ci --output-on-failure -R "SecurityPolicyManager(Test|FailureContractTest)"：通过，2/2 tests passed。
7. ctest --test-dir build-ci --output-on-failure -L unit：通过，125/125 tests passed。
8. ctest --test-dir build-ci --output-on-failure -L contract：通过，137/137 tests passed。

Build 合规复核：

1. 主控边界：manager 仍保持对外六入口和私有 orchestration 角色，没有新增 load_from_sources、commit 或 EffectivePolicySet 等公共泄露面。
2. 正负例覆盖：unit 覆盖 load+evaluate、dry-run gate 导致 patch reject 且 current 稳定、apply 后 rollback 恢复历史语义、连续失败触发 safe_mode；contract 覆盖 dry-run reject 与 safe_mode reject 继续停留在 policy failure domain。
3. 测试发现性：已通过 ctest -N 与定向执行验证新增 manager unit/contract 用例进入 CTest 图，并补充 unit/contract 标签级全量门禁复核。
4. 回退链路：CMake Tools / RunCtest 的“无法配置项目”属于仓库已知工具态问题；本轮已按 build-ci 回退链路保留完整构建与测试证据。
5. TODO 证据回写：已完成 POL-TODO-015 状态、交付物与验收结果回写。
6. 提交隔离：本轮提交范围限定为 SecurityPolicyManager 私有实现、unit/contract/CMake 接线与本专项 TODO 文档。

## 28. 本轮执行记录（2026-04-05 / POL-TODO-016）

### 28.1 选中任务

1. 本轮任务：POL-TODO-016。
2. 可执行性依据：POL-TODO-001 至 POL-TODO-015 已把 policy 对象、接口、loader、validator、resolver、snapshot store、projector、manager 骨架逐步落盘；当前只需核验 infra/CMake 是否已把这些既有实现纳入 dasall_infra 构建图，并将专项 TODO 状态校准到实际交付。

### 28.2 研究与 Design 结论

本地证据：

1. infra/CMakeLists.txt 已存在 DASALL_INFRA_POLICY_SOURCES 与 DASALL_INFRA_POLICY_PRIVATE_HEADERS，且明确收录 PolicyLoader、PolicySchemaValidator、PolicyConflictResolver、PolicySnapshotStore、PolicyDecisionProjector、SecurityPolicyManager 六个 policy 私有实现及对应私有头。
2. dasall_infra 的 target_sources 已直接消费上述两个集合，说明 016 的技术目标“policy 源码进入 infra 构建图”已在 011-015 的多轮实现中落地，当前差异主要是专项 TODO 仍停留在 Not Started。
3. TODO 3.2 中“policy 具体实现仍未进入构建图”的旧结论已经被后续实现覆盖；本轮不再新增占位式 CMake 代码，而是校准台账、保留构建证据并完成独立提交。

D 结论：

1. Design -> Build 映射：不新增新的 CMake 入口，直接以 infra/CMakeLists.txt 中既有 DASALL_INFRA_POLICY_SOURCES / DASALL_INFRA_POLICY_PRIVATE_HEADERS 作为 016 的交付落点，并通过 dasall_infra 增量构建验证接线有效。
2. Build 三件套：
   - 代码目标：核验 policy 私有实现源码与私有头已纳入 dasall_infra target_sources，不再由 placeholder 代表 policy 实现。
   - 测试目标：执行 dasall_infra 定向构建，确认 policy 接口与实现可以进入 infra 构建图并完成编译链接。
   - 验收命令：先观察 ListBuildTargets_CMakeTools / ListTests_CMakeTools 当前工具态；若仍为空，则按仓库回退链路执行 cmake -S . -B build-ci -G "Unix Makefiles" 与 cmake --build build-ci --target dasall_infra。
3. D Gate：PASS。

### 28.3 Build 交付与证据

交付物：

1. infra/CMakeLists.txt：已存在 DASALL_INFRA_POLICY_SOURCES / DASALL_INFRA_POLICY_PRIVATE_HEADERS，并通过 target_sources 纳入 dasall_infra；本轮核验确认 016 的构建接线已实质完成。
2. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md：将 POL-TODO-016 标记为 Done，并补齐本轮执行记录、工具态说明与构建验收结果。

验收结果：

1. ListBuildTargets_CMakeTools / ListTests_CMakeTools：返回空 targets/tests；工作区 IDE 工具态仍未恢复，不能作为 016 完成证据。
2. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
3. cmake --build build-ci --target dasall_infra：通过；增量构建命中 SecurityPolicyManager.cpp 并成功链接 libdasall_infra.a，证明 policy 源码已在 dasall_infra 构建图内。

Build 合规复核：

1. 根因闭环：016 的完成判据是“policy 源码进入 dasall_infra 构建图”，本轮通过 target_sources 静态核验与定向增量编译直接闭环，不重复制造无效 CMake 改动。
2. 边界保持：本轮不扩张公共接口、不提前吸收测试接线或 integration 职责，保持 016 只收敛 build wiring。
3. 回退链路：CMake Tools targets/tests 仍为空；本轮按仓库既定 build-ci 回退路径保留构建证据，不将 IDE 工具态问题误判为任务阻塞。
4. TODO 证据回写：已完成 POL-TODO-016 状态、交付物与验收结果回写。
5. 提交隔离：本轮提交范围限定为 POL-TODO-016 的 TODO/worklog 证据同步，不混入 017 的测试接线回写。

## 29. 本轮执行记录（2026-04-05 / POL-TODO-017）

### 29.1 选中任务

1. 本轮任务：POL-TODO-017。
2. 可执行性依据：POL-TODO-016 已完成并确认 policy 源码进入 dasall_infra 构建图；tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt 中也已存在 policy 相关注册项，本轮只需核验这些入口已进入 CTest 图并可稳定执行。

### 29.2 研究与 Design 结论

本地证据：

1. tests/unit/infra/CMakeLists.txt 已注册 PolicySnapshotCompatibilityTest、PolicyManagerInterfaceTest、PolicyLoaderInterfaceTest、PolicyLoaderConfigReadTest、PolicyConflictResolverTest、PolicyDecisionProjectorTest、SecurityPolicyManagerTest、PolicySchemaValidatorInterfaceTest、PolicySchemaValidatorTest、PolicySnapshotStoreInterfaceTest、PolicySnapshotStoreTest、PolicyTypesRuleBundleTest、PolicyPatchValidationTypesTest、PolicySnapshotOpResultTypesTest、PolicyQueryDecisionRefTypesTest、PolicyErrorsTest。
2. tests/unit/CMakeLists.txt 已将上述 policy unit executable targets 纳入 DASALL_UNIT_TEST_EXECUTABLE_TARGETS 聚合。
3. tests/contract/CMakeLists.txt 已注册 PolicyTypesBoundaryContractTest、PolicyPatchValidationBoundaryContractTest、PolicyDecisionMappingCatalogContractTest、PolicyLoaderBoundaryContractTest、PolicySchemaValidatorBoundaryContractTest、PolicyDecisionProjectorBoundaryContractTest、SecurityPolicyManagerFailureContractTest、PolicyDecisionBoundaryTest、PolicySchemaValidatorInterfaceBoundaryContractTest、PolicyErrorMappingContractTest 等 policy contract 入口。
4. ctest --test-dir build-ci -N -R "^(Policy|SecurityPolicyManager)" 发现 26 个 policy 核心用例，其中 unit 16 个、contract 10 个；说明 017 的“注册测试入口并进入 CTest 图”技术目标已经落地，当前差异仍是专项 TODO 台账未同步。

D 结论：

1. Design -> Build 映射：不新增新的测试源码或注册函数，直接以现有 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt 的 policy 注册项作为 017 交付落点，并通过 build-ci 的测试目标构建与 CTest 发现性验收闭环。
2. Build 三件套：
   - 代码目标：核验 policy 对象、接口、loader/store/manager 基础路径与 contracts 边界测试已经纳入 unit/contract 聚合入口。
   - 测试目标：执行 dasall_unit_tests 与 dasall_contract_tests 构建，随后使用 ctest -N、ctest -L unit、ctest -L contract 验证发现性与可执行性。
   - 验收命令：先尝试 RunCtest_CMakeTools；若仍报“无法配置项目”，则按仓库回退链路执行 cmake --build build-ci --target dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci -N -R "^(Policy|SecurityPolicyManager)"、ctest --test-dir build-ci --output-on-failure -L unit、ctest --test-dir build-ci --output-on-failure -L contract。
3. D Gate：PASS。

### 29.3 Build 交付与证据

交付物：

1. tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：已存在并聚合 16 个 policy 核心 unit 入口，覆盖对象、接口、loader、resolver、projector、snapshot store、manager 与错误语义基础路径。
2. tests/contract/CMakeLists.txt：已存在并聚合 10 个 policy 核心 contract 入口，覆盖 decision 语义、错误码映射、schema/interface 边界、loader/projector/manager 契约与 catalog 门禁。
3. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md：将 POL-TODO-017 标记为 Done，并补齐本轮执行记录、工具态说明与 ctest 发现性证据。

验收结果：

1. RunCtest_CMakeTools：失败，返回“生成失败: 无法配置项目”；工作区 IDE 测试工具态仍未恢复，按仓库既定回退链路切换到 build-ci，不视为任务阻塞。
2. cmake --build build-ci --target dasall_unit_tests dasall_contract_tests：通过；构建输出完成 policy 相关 unit executables 与聚合测试图的链接。
3. ctest --test-dir build-ci -N -R "^(Policy|SecurityPolicyManager)"：通过，发现 26 个 policy 核心测试，其中 unit 16 个、contract 10 个。
4. ctest --test-dir build-ci --output-on-failure -L unit：通过，125/125 tests passed。
5. ctest --test-dir build-ci --output-on-failure -L contract：通过，137/137 tests passed。

Build 合规复核：

1. 根因闭环：017 的完成判据是“policy unit/contract 用例进入 CTest 图并可执行”，本轮通过聚合目标构建、精确的 policy regex 发现性和标签级全量执行直接闭环，不重复制造测试壳文件。
2. 覆盖保持：unit 已覆盖对象/接口/loader/store/manager 基础路径，contract 已覆盖 decision 语义、错误码映射与 contracts 边界，满足 9.1 测试矩阵的当前阶段要求。
3. 回退链路：RunCtest_CMakeTools 仍报“无法配置项目”；本轮按仓库既定 build-ci 回退路径保留完整发现性与执行证据。
4. TODO 证据回写：已完成 POL-TODO-017 状态、交付物与验收结果回写。
5. 提交隔离：本轮提交范围限定为 POL-TODO-017 的 TODO/worklog 证据同步，不混入 018 integration 接线或其他 bridge 任务。

## 30. 本轮执行记录（2026-04-05 / POL-TODO-018）

### 30.1 选中任务

1. 本轮任务：POL-TODO-018。
2. 可执行性依据：POL-TODO-015 已完成 manager 主链，POL-TODO-017 已完成 unit/contract 发现性；POL-BLK-005 也已于 2026-03-30 解阻，tests 顶层 integration 拓扑已接入，可直接补 policy integration 子目录与 CTest 注册。

### 30.2 研究与 Design 结论

本地证据：

1. tests/CMakeLists.txt 已提供 `dasall_integration_tests` 聚合目标，并依赖 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS`；tests/integration/CMakeLists.txt 与 tests/integration/infra/CMakeLists.txt 已采用“顶层聚合列表 + 组件子目录注册”的统一 integration 接线模式。
2. 当前 tests/integration/infra/ 下已有 audit/config/logging/secret 子目录，但尚无 tests/integration/infra/policy/；因此 018 的根因不是 manager 不可测试，而是 policy integration 子目录、target 与 discoverability 尚未落盘。
3. SecurityPolicyManager 当前公开入口从 `PolicyBundle` / `PolicyPatch` 起步，PolicyLoader 对缺失配置键采取 frozen defaults 回退，因此 `source unavailable` 在现有 loader-manager 边界下不适合作为稳定 integration 注入点；本轮将 integration 范围收敛为真实可验证的 lifecycle 闭环，以及 commit fail / safe_mode 两类 failure injection。

D 结论：

1. Design -> Build 映射：新增 tests/integration/infra/policy/CMakeLists.txt 与 PolicyLifecycleIntegrationTest.cpp，并把 policy 子目录接入 tests/integration/infra/CMakeLists.txt 与 tests/integration/CMakeLists.txt 的 integration 聚合图。
2. Build 三件套：
   - 代码目标：新增 policy integration 子目录与 `dasall_policy_lifecycle_integration_test`，覆盖 load -> snapshot -> evaluate -> patch -> rollback 闭环，以及 store commit fail / safe_mode failure injection。
   - 测试目标：验证新增 policy integration 用例可被 CTest 发现、定向执行，并与现有 integration 标签套件兼容。
   - 验收命令：沿用仓库回退链路执行 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_policy_lifecycle_integration_test、ctest --test-dir build-ci -N -R "PolicyLifecycleIntegrationTest|infra_integration_topology_smoke"、ctest --test-dir build-ci --output-on-failure -R PolicyLifecycleIntegrationTest、ctest --test-dir build-ci --output-on-failure -L integration；同时记录 ListTests_CMakeTools / RunCtest_CMakeTools 的当前坏工具态。
3. D Gate：PASS。

### 30.3 Build 交付与证据

交付物：

1. tests/integration/infra/policy/CMakeLists.txt：新增 policy integration 注册函数与 `PolicyLifecycleIntegrationTest` 的 CTest 入口，标签为 `integration;policy`。
2. tests/integration/infra/policy/PolicyLifecycleIntegrationTest.cpp：新增 policy 生命周期集成测试，覆盖 load -> snapshot -> evaluate -> patch -> rollback 闭环，以及 snapshot store commit fail 和 safe_mode failure injection。
3. tests/integration/infra/CMakeLists.txt、tests/integration/CMakeLists.txt：把 policy 子目录与 `dasall_policy_lifecycle_integration_test` 纳入顶层 integration 聚合图。
4. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md：将 POL-TODO-018 标记为 Done，并补齐本轮执行记录、工具态说明与 integration 发现性证据。

验收结果：

1. ListTests_CMakeTools：返回空 tests；工作区 IDE 测试工具态仍未恢复。
2. RunCtest_CMakeTools：失败，返回“生成失败: 无法配置项目”；按仓库既定回退链路切换到 build-ci，不视为任务阻塞。
3. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
4. cmake --build build-ci --target dasall_policy_lifecycle_integration_test：通过，新增 policy integration executable 成功编译链接。
5. ctest --test-dir build-ci -N -R "PolicyLifecycleIntegrationTest|infra_integration_topology_smoke"：通过，发现 2 个测试，分别为顶层 integration topology smoke 与 PolicyLifecycleIntegrationTest。
6. ctest --test-dir build-ci --output-on-failure -R PolicyLifecycleIntegrationTest：通过，1/1 tests passed。
7. ctest --test-dir build-ci --output-on-failure -L integration：通过，14/14 tests passed。

Build 合规复核：

1. 根因闭环：018 的完成判据是“tests 顶层完成 integration 接线且 policy 集成用例可被 ctest 发现”；本轮通过新增 policy 子目录、聚合 target 注册与 discoverability/执行证据直接闭环。
2. 覆盖保持：integration 用例已覆盖 lifecycle 主闭环、commit fail 与 safe_mode 两类 failure injection；`source unavailable` 由于当前 loader 对缺失输入默认回退、manager 入口直接接受 PolicyBundle，暂不伪造不稳定注入点，不影响 018 的 discoverability 完成判据。
3. 回退链路：ListTests_CMakeTools 仍为空、RunCtest_CMakeTools 仍报“无法配置项目”；本轮按仓库既定 build-ci 回退路径保留完整 integration 发现性与回归证据。
4. TODO 证据回写：已完成 POL-TODO-018 状态、交付物与验收结果回写。
5. 提交隔离：本轮提交范围限定为 POL-TODO-018 的 integration 接线、测试落盘与 TODO/worklog 证据同步，不混入 019~021 的 bridge 代码或 blocker 校准。

