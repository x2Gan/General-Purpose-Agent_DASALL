# DASALL infrastructure 子系统 policy 组件专项 TODO

最近更新时间：2026-03-25  
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
11. docs/todos/contracts-freeze/
12. docs/todos/DASALL_infrastructure子系统专项TODO.md
13. docs/todos/DASALL_infrastructure_config组件专项TODO.md
14. docs/todos/DASALL_infrastructure_metrics组件专项TODO.md
15. docs/todos/DASALL_infrastructure_health组件专项TODO.md
16. docs/todos/DASALL_infrastructure_audit组件专项TODO.md
17. docs/todos/DASALL_profiles子系统专项TODO.md
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
| infra/CMakeLists.txt | 仅编译 src/placeholder.cpp | policy 尚未进入构建图 |
| infra/include/ | 当前为空 | policy 对外接口与对象未落盘 |
| infra/src/ | 仅存在 placeholder 与少量空子目录 | policy 实现目录尚未存在 |
| tests/CMakeLists.txt | 仅接入 mocks、unit、contract | integration 顶层尚未接线 |
| tests/unit/CMakeLists.txt | 未接入 infra 子目录 | policy unit 发现性缺失 |
| tests/contract/CMakeLists.txt | centralized registration 已存在 | 可承载 policy 语义边界与错误码映射测试 |
| contracts/include/policy/* | 当前检索不到文件 | 不能把共享 policy 对象当作现成依赖 |
| docs/todos/contracts-freeze/DASALL_contracts验收整改TODO.md T010 | PolicyDecision、PromptPolicyDecision 仍处于“新增对象或替代映射”状态 | policy contract 门禁必须承接该缺口，不得默认视为已实现 |
| docs/todos/DASALL_infrastructure子系统专项TODO.md INF-TODO-017/INF-BLK-07 | policy 已被列为 L2 可执行对象，但规则 schema 与冲突裁定顺序未冻结 | 本专项 TODO 必须显式承接 INF-BLK-07 |

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
| tests/integration/infra/policy | policy 设计 8.1/9.1；tests 现状 | L0 | 路径与用例建议存在 | 顶层 tests 未接入 integration | 先解阻测试拓扑，再拆集成任务 |

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
| 审计、指标、健康桥接 | policy 设计 6.10；audit/metrics/health TODO | 适配器 / 门禁 | POL-TODO-019、POL-TODO-020、POL-TODO-021 | 桥接依赖尚未冻结，必须显式 Blocked |
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
| POL-TODO-001 | Not Started | 定义 PolicyBundle 与 PolicyRuleDescriptor 数据结构 | policy 设计 6.5；架构 5.10；编码规范 3.7 | 6.5 核心对象表 | L3 | infra/include/policy/PolicyTypes.h | PolicyBundle、PolicyRuleDescriptor | unit：对象字段完整性；contract：effect/domain 仅做语义映射不扩写 contracts | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | 无 | 无 | 无 | PolicyTypes.h、基础对象测试 | 仅当 bundle_id/schema_version/source/checksum/rules 与 rule_id/domain/effect/priority/mode/conditions/reason_code 全部落盘，且不引入业务依赖时完成 |
| POL-TODO-002 | Not Started | 定义 PolicyPatch 与 ValidationReport 数据结构 | policy 设计 6.5/6.6 | 6.5 核心对象表；6.6 validate_patch 语义 | L3 | infra/include/policy/PolicyTypes.h | PolicyPatch、ValidationReport | unit：patch 基础字段与 report 阻断语义；contract：错误原因只映射 policy 失败域 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-001 | POL-BLK-001 | 冻结 operations 白名单与 field_path 约束 | PolicyTypes.h、对象测试 | 仅当 patch_id/base_generation/operations/actor/reason 与 blocking_errors/warnings/invalid_rule_ids/field_paths 全部落盘，且阻断语义可二值判定时完成 |
| POL-TODO-003 | Not Started | 定义 PolicySnapshot 与 PolicyOpResult 数据结构 | policy 设计 6.5/6.8 | 6.5 核心对象表；6.8 回滚兜底 | L3 | infra/include/policy/PolicyTypes.h | PolicySnapshot、PolicyOpResult | unit：generation 单调与 LKG 引用语义；contract：错误结果不扩写 contracts | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | POL-TODO-001 | 无 | 无 | PolicyTypes.h、对象测试 | 仅当 snapshot_id/generation/version/mode/effective_rules/source_chain/lkg_ref 与 applied/rolled_back/dry_run/snapshot_id/generation/error_info 全部落盘，且 generation 语义可测试时完成 |
| POL-TODO-004 | Not Started | 定义 PolicyQueryContext 与 PolicyDecisionRef 数据结构 | policy 设计 6.5/6.7；contracts-freeze 术语表 | 6.5 核心对象表；6.7 查询流程 | L3 | infra/include/policy/PolicyTypes.h | PolicyQueryContext、PolicyDecisionRef | unit：上下文字段 unknown 兜底；contract：decision 只对齐 allow/deny/require_confirmation 语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-001 | POL-BLK-002 | 完成 contracts 语义映射 catalog 或共享对象冻结结论 | PolicyTypes.h、contract 边界测试 | 仅当 module/operation/target_type/target_ref/actor_ref/request_id/session_id/trace_id/task_id/profile_id 与 decision/reason_code/matched_rule_ids/snapshot_id/generation/evidence_ref/warnings 全部落盘，且 decision 语义无越权时完成 |
| POL-TODO-005 | Not Started | 定义 PolicyErrors 错误码域 | policy 设计 6.6/6.8；contracts-freeze ResultCode 分类表 | 6.6 错误语义；6.8 异常与恢复时序 | L3 | infra/include/policy/PolicyErrors.h | INF_E_POLICY_BUNDLE_INVALID、INF_E_POLICY_SCHEMA_UNSUPPORTED、INF_E_POLICY_CONFLICT_UNRESOLVED、INF_E_POLICY_PATCH_BASE_MISMATCH、INF_E_POLICY_SNAPSHOT_NOT_FOUND、INF_E_POLICY_ROLLBACK_FAILED、INF_E_POLICY_QUERY_DENIED、INF_E_POLICY_SOURCE_UNAVAILABLE、INF_E_POLICY_STORE_COMMIT_FAILED、INF_E_POLICY_DRYRUN_REJECTED | contract：错误码映射保持 policy 失败域；unit：错误码稳定性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | 无 | POL-BLK-002 | 明确与 contracts::ResultCode 的映射 catalog 或替代映射说明 | PolicyErrors.h、映射测试 | 仅当 10 个错误码都可追溯到设计锚点，且不把 validation/tool/runtime 失败误归入 policy 域时完成 |
| POL-TODO-006 | Not Started | 定义 ISecurityPolicyManager 接口头文件 | policy 设计 6.6；infrastructure 设计 6.6 | 6.6 ISecurityPolicyManager | L3 | infra/include/policy/ISecurityPolicyManager.h | load_policy、apply_patch、dry_run_patch、snapshot、rollback、evaluate | unit：接口编译；contract：返回对象与 decision 语义边界 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-001、POL-TODO-002、POL-TODO-003、POL-TODO-004、POL-TODO-005 | 无 | 无 | ISecurityPolicyManager.h、编译与边界测试 | 仅当 6 个方法签名与设计一致，且接口不暴露规则引擎实现细节时完成 |
| POL-TODO-007 | Not Started | 定义 IPolicyLoader 接口头文件 | policy 设计 6.6/6.9；config TODO | 6.6 IPolicyLoader；6.9 配置项与默认策略 | L3 | infra/include/policy/IPolicyLoader.h | load_from_sources | unit：接口编译；unit：source_id/version/checksum 字段对接 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | POL-TODO-001、POL-TODO-006 | POL-BLK-006 | ConfigCenter 最小加载接口与 policy 配置键读取约束完成冻结 | IPolicyLoader.h、编译记录 | 仅当 loader 只暴露 PolicyBundle 输出，不泄露 ConfigCenter 内部实现时完成 |
| POL-TODO-008 | Not Started | 定义 IPolicySchemaValidator 接口头文件 | policy 设计 6.6/6.8 | 6.6 IPolicySchemaValidator；6.8 输入异常 | L2 | infra/include/policy/IPolicySchemaValidator.h | validate_bundle、validate_patch | unit：接口编译；contract：blocking_errors 只对齐错误域不扩写 contracts | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-001、POL-TODO-002 | POL-BLK-001 | 冻结规则 schema 最小字段白名单和兼容矩阵 | IPolicySchemaValidator.h、编译记录 | 仅当接口只暴露校验结果，不预埋 DSL 执行模型时完成 |
| POL-TODO-009 | Not Started | 定义 IPolicySnapshotStore 接口头文件 | policy 设计 6.6/6.8 | 6.6 IPolicySnapshotStore；6.8 存储异常 | L3 | infra/include/policy/IPolicySnapshotStore.h | commit、current、last_known_good、get_by_id | unit：接口编译；unit：LKG 与 generation 语义检查 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | POL-TODO-003 | 无 | 无 | IPolicySnapshotStore.h、编译记录 | 仅当 4 个方法齐全，且接口不泄露具体持久化后端时完成 |
| POL-TODO-010 | Not Started | 实现 PolicyLoader 配置读取骨架 | policy 设计 6.3/6.7/6.9；config TODO；profiles TODO | 6.3 PolicyLoader 输入输出；6.7 正常加载流程第 2 步；6.9 配置项表 | L2 | infra/src/policy/PolicyLoader.cpp | PolicyLoader（默认/Profile/部署层读取与 source_id/checksum 装配） | unit：strict/compat、hot_reload、default_effect 等配置读取；contract：Profile 裁剪不绕过 Audit/Runtime 主控链路 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-001、POL-TODO-007 | POL-BLK-006 | ConfigCenter 具备最小 load_layers/get_typed 能力且 profiles 侧键名冻结 | PolicyLoader.cpp、配置读取测试 | 仅当 loader 能按设计读取 enabled/mode/hot_reload/max_history/default_effect/priority_order 等策略键，且 source 链可追溯时完成 |
| POL-TODO-011 | Blocked | 实现 PolicySchemaValidator 最小校验骨架 | policy 设计 6.3/6.7/6.8 | 6.3 ValidationReport；6.7 正常加载流程第 3 步；6.8 输入异常 | L2 | infra/src/policy/PolicySchemaValidator.cpp | validate_bundle、validate_patch | unit：缺字段、未知 domain、非法 effect、base_generation 不匹配；contract：错误归类保持 policy 域 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-002、POL-TODO-005、POL-TODO-008 | POL-BLK-001 | 冻结 domain/effect/conditions 白名单、schema_version 兼容矩阵与 patch operation 集合 | PolicySchemaValidator.cpp 或阻塞记录 | 仅当四类非法输入都能返回明确 ValidationReport 且不激活快照时，状态才可从 Blocked 转为 Done |
| POL-TODO-012 | Blocked | 实现 PolicyConflictResolver 冲突裁定骨架 | policy 设计 6.3/6.7/6.8/6.9 | 6.3 EffectivePolicySet 输出；6.7 正常加载流程第 4 步；6.9 priority_order | L2 | infra/src/policy/PolicyConflictResolver.cpp | PolicyConflictResolver（deny-first 与 explicit-priority 裁定路径） | unit：deny-first 与 explicit-priority 两档裁定；failure：冲突未决拒绝激活 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | POL-TODO-001、POL-TODO-008、POL-TODO-010 | POL-BLK-001 | 冻结冲突裁定矩阵、同优先级 tie-break 与 compat 模式降级规则 | PolicyConflictResolver.cpp 或阻塞记录 | 仅当两档裁定都可被稳定验证，且冲突未决时返回显式拒绝而非静默覆盖时完成 |
| POL-TODO-013 | Not Started | 实现 PolicySnapshotStore generation/LKG 骨架 | policy 设计 6.3/6.7/6.8 | 6.3 PolicySnapshotStore；6.7 正常加载流程第 5 步；6.8 commit 失败回退 | L2 | infra/src/policy/PolicySnapshotStore.cpp | commit、current、last_known_good、get_by_id | unit：generation 单调、自增、LKG 回退；failure：commit 失败后 current 不切换 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | POL-TODO-003、POL-TODO-009 | 无 | 无 | PolicySnapshotStore.cpp、回退测试 | 仅当新快照提交成功后 generation 自增，提交失败后 current/LKG 保持旧值且错误可判定时完成 |
| POL-TODO-014 | Blocked | 实现 PolicyDecisionProjector 查询投影骨架 | policy 设计 6.3/6.5/6.7；contracts-freeze T010 | 6.3 投影输出；6.5 PolicyDecisionRef；6.7 查询流程 | L2 | infra/src/policy/PolicyDecisionProjector.cpp | PolicyDecisionProjector（domain -> target_selector -> priority -> effect 投影路径） | unit：命中、未命中、require_confirmation、deny 四类投影；contract：decision 语义与 evidence_ref 映射 catalog | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-004、POL-TODO-012、POL-TODO-013 | POL-BLK-001、POL-BLK-002 | 完成规则裁定矩阵冻结，并明确 PolicyDecision 共享对象缺失时的映射 catalog | PolicyDecisionProjector.cpp 或阻塞记录 | 仅当投影结果只输出引用和原因，不泄露规则实现细节，且 contract 门禁通过时完成 |
| POL-TODO-015 | Not Started | 实现 SecurityPolicyManager 主链骨架 | policy 设计 6.2/6.4/6.7/6.8 | 6.2 SecurityPolicyManager；6.4 依赖关系；6.7/6.8 主异常流程 | L2 | infra/src/policy/SecurityPolicyManager.cpp | load_policy、apply_patch、dry_run_patch、snapshot、rollback、evaluate、safe_mode 进入条件 | unit：正常加载、patch 失败不切 current、rollback 成功、连续失败进入 safe_mode；contract：拒绝结果保持 policy 失败域 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-006、POL-TODO-010、POL-TODO-011、POL-TODO-012、POL-TODO-013、POL-TODO-014 | POL-BLK-001、POL-BLK-006 | schema/裁定矩阵与 ConfigCenter 最小接口冻结 | SecurityPolicyManager.cpp、主链测试 | 仅当 load/dry_run/apply/query/rollback 五条路径都能被二值验证，且 safe_mode 触发条件可复现时完成 |
| POL-TODO-016 | Not Started | 注册 policy 源码到 infra CMake | policy 设计 7、8.1；代码现状 | 7 Design -> Build 映射；8.1 文件落盘建议 | L2 | infra/CMakeLists.txt | policy include/src 文件纳入 dasall_infra | build：dasall_infra 可编译；unit：policy 接口编译可进入构建图 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | POL-TODO-001 至 POL-TODO-009 | 无 | 无 | CMake 改动、构建记录 | 仅当 placeholder 不再是唯一源码入口，且 policy 文件进入 dasall_infra 构建图时完成 |
| POL-TODO-017 | Not Started | 注册 policy 的 unit 与 contract 测试入口 | policy 设计 8.1/9.1；tests 现状 | 8.1 tests/unit/infra/policy、tests/contract/infra；9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt、tests/unit/infra/policy/、tests/contract/CMakeLists.txt、tests/contract/smoke/ | unit：对象、接口、loader/store/manager 基础路径；contract：decision 语义、错误码映射、contracts 边界 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-016 | POL-BLK-002 | 共享对象缺失时采用语义映射 catalog 并冻结 contract 断言口径 | 测试源文件、注册入口、ctest 发现性证据 | 仅当新增 policy unit/contract 用例可被 ctest -N 发现并执行时完成 |
| POL-TODO-018 | Blocked | 注册 policy integration 测试入口 | policy 设计 8.1/9.1；tests 现状 | 8.1 tests/integration/infra/policy；9.1 Integration/Failure Injection | L0 | tests/CMakeLists.txt、tests/integration/infra/policy/ | integration：load -> snapshot -> evaluate -> patch -> rollback 闭环；failure：source unavailable、commit fail、safe_mode | cmake -S . -B build-ci -G Ninja && ctest --test-dir build-ci -N | POL-TODO-015、POL-TODO-017 | POL-BLK-005 | tests 顶层接入 integration 子目录并定义 integration 标签规范 | integration 注册改动或阻塞记录 | 仅当 tests 顶层完成 integration 接线且 policy 集成用例可被 ctest 发现后，状态才可从 Blocked 转为 Not Started |
| POL-TODO-019 | Blocked | 实现 PolicyAuditBridge 审计桥接骨架 | policy 设计 6.2/6.10；audit TODO | 6.2 PolicyAuditBridge；6.10 强制审计点 | L1 | infra/src/policy/PolicyAuditBridge.cpp | PolicyAuditBridge（load/apply_patch/rollback/deny 事件桥接） | unit：高风险 deny 与 patch failure 事件组装；contract：AuditEvent 引用边界不越权 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-015 | POL-BLK-003 | audit 侧最小写入接口和字段集合完成冻结 | PolicyAuditBridge.cpp 或阻塞记录 | 仅当 bridge 只输出审计事实，不重新定义审计对象，且事件覆盖四个强制审计点时完成 |
| POL-TODO-020 | Blocked | 实现 PolicyMetricsBridge 指标桥接骨架 | policy 设计 6.2/6.10；metrics TODO | 6.2 PolicyMetricsBridge；6.10 指标清单 | L1 | infra/src/policy/PolicyMetricsBridge.cpp | PolicyMetricsBridge（reload_total、invalid_total、patch_total、deny_total、rollback_total、active_generation、safe_mode_total） | unit：计数与 gauge 输出；contract：标签不过度暴露实现细节 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | POL-TODO-015 | POL-BLK-004 | metrics 侧桥接接口和标签白名单完成冻结 | PolicyMetricsBridge.cpp 或阻塞记录 | 仅当指标集合与设计一致，且 active_generation/safe_mode 语义可被测试稳定判定时完成 |
| POL-TODO-021 | Blocked | 实现 PolicyHealthProbe 健康探针骨架 | policy 设计 6.2/6.10；health TODO | 6.2 PolicyHealthProbe；6.10 ready/degraded/unavailable | L1 | infra/src/policy/PolicyHealthProbe.cpp | PolicyHealthProbe（ready/degraded/unavailable 与最近失败原因输出） | unit：状态切换；integration：commit fail 或连续 patch fail 后 health 降级 | cmake -S . -B build-ci -G Ninja && ctest --test-dir build-ci -N | POL-TODO-015 | POL-BLK-004、POL-BLK-005 | health 侧探针接口冻结且 integration 接线完成 | PolicyHealthProbe.cpp 或阻塞记录 | 仅当健康状态与最近失败原因能被稳定输出，并且不侵入 runtime 状态机时完成 |
| POL-TODO-022 | Not Started | 回写 policy 质量门与交付证据 | policy 设计 9.2/11；工程规范 6.2 | 9.2 Gate 建议；11 风险与回退 | L2 | docs/todos/DASALL_infrastructure_policy组件专项TODO.md | process test：gate 结论、阻塞变化、回退证据回写 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | POL-TODO-017 | 无 | 无 | 更新后的 TODO 文档证据段 | 仅当每个质量门都有通过/失败结论和对应命令证据时完成 |

### 6.2 当前 Blocked 任务索引

| 任务 ID | 对应阻塞项 |
|---|---|
| POL-TODO-002 | POL-BLK-001 |
| POL-TODO-004 | POL-BLK-002 |
| POL-TODO-005 | POL-BLK-002 |
| POL-TODO-007 | POL-BLK-006 |
| POL-TODO-008 | POL-BLK-001 |
| POL-TODO-010 | POL-BLK-006 |
| POL-TODO-011 | POL-BLK-001 |
| POL-TODO-012 | POL-BLK-001 |
| POL-TODO-014 | POL-BLK-001、POL-BLK-002 |
| POL-TODO-015 | POL-BLK-001、POL-BLK-006 |
| POL-TODO-017 | POL-BLK-002 |
| POL-TODO-018 | POL-BLK-005 |
| POL-TODO-019 | POL-BLK-003 |
| POL-TODO-020 | POL-BLK-004 |
| POL-TODO-021 | POL-BLK-004、POL-BLK-005 |

## 7. 执行顺序建议

### 7.1 串并行编排

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 对象冻结 | POL-TODO-001~005 | 可并行 | 先冻结 PolicyTypes 与错误码域，减少后续返工 |
| B 接口冻结 | POL-TODO-006~009 | 可并行 | 接口边界稳定后再推进实现骨架 |
| C 配置与快照底座 | POL-TODO-010、POL-TODO-013 | 串行 | 先读取配置，再形成快照存储基础 |
| D 规则治理主链 | POL-TODO-011、POL-TODO-012、POL-TODO-014、POL-TODO-015 | 串行且受阻 | 依赖 INF-BLK-07 承接的 schema 与裁定矩阵冻结 |
| E 构建与测试接线 | POL-TODO-016、POL-TODO-017 | 可并行 | 代码入图与 unit/contract 发现性可同步推进 |
| F 观测桥接与集成 | POL-TODO-018~021 | 串行且受阻 | 先桥接接口冻结，再放开 integration |
| G 证据收口 | POL-TODO-022 | 串行 | 回写质量门、阻塞变化与回退证据 |

### 7.2 必过门禁表

| Gate ID | 门禁项 | 触发时机 | 通过标准 | 不通过后动作 |
|---|---|---|---|---|
| POL-GATE-01 | 规则 schema 冻结门 | 进入 validator/resolver/projector 实现前 | domain/effect/conditions/operations 白名单与冲突裁定矩阵评审通过 | 维持 POL-BLK-001，禁止推进 POL-TODO-011/012/014/015 |
| POL-GATE-02 | 接口冻结门 | 阶段 B 结束前 | PolicyTypes、PolicyErrors、四个接口头文件全部落盘且不越界 | 退回对象/接口定义 |
| POL-GATE-03 | 快照回滚门 | 阶段 C 结束前 | generation、history、LKG 语义有自动测试，提交失败不切 current | 回退 SnapshotStore 变更 |
| POL-GATE-04 | contracts 语义边界门 | 阶段 D 与 E 前 | PolicyDecision 只做语义对齐；若共享对象仍缺失，则 mapping catalog 和 contract 测试通过 | 维持 POL-BLK-002，禁止对外暴露 shared dependency |
| POL-GATE-05 | 观测桥接门 | 进入 bridge 实现前 | audit、metrics、health 最小桥接接口冻结 | 维持 POL-BLK-003/POL-BLK-004 |
| POL-GATE-06 | 测试发现性门 | 阶段 E 前 | ctest --test-dir build-ci -N 能发现新增 policy unit/contract 用例 | 修复 tests 注册，不推进后续 |
| POL-GATE-07 | integration 拓扑门 | 进入 integration 验收前 | tests 顶层已接入 integration，且标签规范明确 | 维持 POL-BLK-005 |
| POL-GATE-08 | breaking review 门 | 任意共享语义或接口签名变化前 | 明确 breaking 风险、迁移窗口、回退策略和评审结论 | 未评审不得推进 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| POL-BLK-001 | INF-BLK-07 尚未完全关闭：规则 schema、domain/effect/conditions 白名单、patch operations、冲突裁定矩阵与 compat 降级规则未冻结 | POL-TODO-002、008、011、012、014、015 | 形成评审通过的 schema 与裁定矩阵 | 在 DASALL_infra_policy模块详细设计.md 中补齐规则表、冲突表和 patch 白名单 | 首版仅允许对象/接口冻结，不推进 validator/resolver/manager 实现 |
| POL-BLK-002 | contracts 侧缺少可直接引用的 PolicyDecision 共享对象，当前仅有术语与整改 TODO | POL-TODO-004、005、014、017 | 完成 shared object 冻结，或形成正式 mapping catalog 并通过 contract gate | 承接 contracts-freeze T010，明确语义映射和禁止越权字段 | 在共享对象未冻结前，仅输出 infra 私有 PolicyDecisionRef，不对外宣称 shared type 可用 |
| POL-BLK-003 | audit 最小写入接口与字段集合尚未冻结 | POL-TODO-019 | audit 侧 IAuditLogger/AuditEvent 边界冻结并通过评审 | 以 audit 组件专项 TODO 的对象与接口冻结结果为前置 | 暂只记录本地错误与返回码，不接审计桥 |
| POL-BLK-004 | metrics/health 桥接接口、标签白名单、探针状态对象未冻结 | POL-TODO-020、POL-TODO-021 | metrics 与 health 侧最小桥接接口完成冻结 | 承接 metrics/health 专项 TODO 的接口冻结任务 | 暂只在 manager 内部保留计数与状态，不对外暴露桥接输出 |
| POL-BLK-005 | tests 顶层尚未接入 integration 子目录，policy 集成与 failure injection 无法被 ctest 发现 | POL-TODO-018、021 | tests/CMakeLists.txt 接入 integration 并定义标签规范 | 新增 add_subdirectory(integration) 与 integration 标签约定 | integration 验收延期，首轮只跑 unit/contract |
| POL-BLK-006 | ConfigCenter 与 profiles 侧的 policy 最小配置读取接口尚未落盘 | POL-TODO-007、010、015 | config 组件完成最小 load_layers/get_typed 接口冻结，profiles 侧 policy 键名冻结 | 承接 config 与 profiles 专项 TODO 的接口/资产冻结任务 | loader 首版只保留接口，不实现真实配置读取 |

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

1. integration 相关命令当前不纳入首轮 Gate，原因见 POL-BLK-005。
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
| integration 长期未接线 | Medium | tests 顶层持续不接入 integration | ctest -N 看不到 integration 用例 | 保持 integration 任务 Blocked，不将其纳入 Done-ready |

## 11. 可行性结论

1. 结论：可直接生成并执行接口/数据结构级专项 TODO，局部可进入方法骨架级；当前不应整体进入函数级实现。
2. 原因：
   - 已具备核心接口清单、对象字段、主异常流程、错误语义、配置项和落盘建议。
   - infra/policy 的职责边界已被架构文档、蓝图和 ADR 明确约束。
   - INF-TODO-017 已在子系统级 TODO 中被识别为可执行对象。
   - 规则 schema 与冲突裁定矩阵仍是前置阻塞，直接进入实现会制造返工。
   - contracts 共享 policy 对象代码仍未落盘，只能先做语义对齐而不能直接绑定共享头文件。
3. 当前最小可执行粒度：数据结构 / 接口级，局部方法骨架级。
4. 若未达到全面函数级，还缺哪些设计信息：
   - 规则 schema 白名单、patch operations 白名单、冲突裁定矩阵、compat 降级规则。
   - contracts/shared PolicyDecision 的正式冻结结论或 mapping catalog。
   - audit、metrics、health 的最小桥接接口。
   - tests 顶层 integration 接线。
   - ConfigCenter 与 profiles 侧 policy 配置键的最终冻结。
5. 下一步建议：
   - 先执行 POL-TODO-001~009，完成对象、错误码与接口冻结。
   - 并行推动 POL-BLK-001、POL-BLK-002、POL-BLK-006 解阻，再进入 validator/resolver/manager 主链。
   - 在 audit、metrics、health 接口冻结前，不把桥接任务误写成 Build-ready。
   - 首轮验收只以 dasall_infra、unit、contract 为准，不把 integration 作为伪完成条件。