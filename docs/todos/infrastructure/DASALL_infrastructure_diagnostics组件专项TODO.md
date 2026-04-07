# DASALL infrastructure 子系统 diagnostics 组件专项 TODO

最近更新时间：2026-04-07  
阶段：Detailed Design -> Special TODO  
适用范围：infra/diagnostics

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_infra_diagnostics模块详细设计.md
2. docs/architecture/DASALL_infrastructure子系统详细设计.md
3. docs/architecture/DASSALL_Agent_architecture.md
4. docs/architecture/DASALL_Engineering_Blueprint.md
5. docs/adr/ADR-005-architecture-review-baseline.md
6. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
7. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
8. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
9. docs/plans/DASALL_工程落地实现步骤指引.md
10. docs/development/DASALL_工程协作与编码规范.md
11. docs/todos/contracts/DASALL_contracts冻结TODO总表.md
12. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md
13. docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md
14. docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md
15. docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md
16. docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md
17. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md
18. 当前代码与测试现状：infra/CMakeLists.txt、infra/include/、infra/src/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt

生成原则：

1. 不改写 ADR-005/006/007/008 已冻结结论。
2. 不越过 infrastructure/diagnostics 组件边界扩张到无关模块。
3. 不把讨论事项或设计缺口伪装成 Build-ready 实现任务。
4. 每项任务必须具备代码目标、测试目标、验收命令三件套。
5. 若详细设计对接口请求/响应对象、桥接接口或导出策略证据不足，必须输出 Blocked 与补设计前置任务。

## 2. 子系统目标与范围

### 2.1 组件目标

1. 提供统一诊断入口，执行白名单命令并返回可追溯的 DiagnosticsSnapshot。
2. 提供先脱敏后落盘、可查询、可导出的诊断快照链路。
3. 提供对 metrics、audit、logging、health 等基础信号的证据聚合，但不接管恢复裁定与调度。
4. 在连续失败场景下进入 diagnostics_safe_mode，仅保留只读低风险诊断能力。

### 2.2 范围边界

纳入范围：

1. diagnostics 对外接口、核心对象、错误码、配置键、主流程与异常流程。
2. DiagnosticsServiceFacade、CommandRegistry、CommandPolicyGuard、CommandExecutor、EvidenceCollector、RedactionEngine、SnapshotAssembler、SnapshotStore、ExportManager、DiagnosticsMetricsBridge、DiagnosticsAuditBridge 的落盘拆解。
3. diagnostics 的 CMake 接线、unit/contract/integration/failure-injection 测试注册点、质量门与阻塞项。

不纳入范围：

1. runtime 主状态机推进、恢复执行裁定、retry/replan/rollback 决策。
2. contracts 公共语义对象扩写或重定义。
3. metrics/audit/health/security_policy 具体实现细节，仅允许依赖其冻结接口或抽象。
4. sidecar 诊断代理、远程多目标断点续传等 v2 演进项。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1：约束与边界抽取输出）

| ID | 来源 | 类型 | 约束内容 | 对 diagnostics TODO 的影响 |
|---|---|---|---|---|
| DIA-TC001 | diagnostics 设计 1.1；架构 9.5 | Must | diagnostics 必须提供线程状态、队列积压、错误码统计、工具失败明细、模型与服务健康状态等最低诊断能力 | 任务必须覆盖命令域、快照对象、证据聚合与导出 |
| DIA-TC002 | 架构 3.7；蓝图 4.2 | Must | 依赖方向单向，diagnostics 不反向依赖 runtime/cognition/tools 实现 | 代码目标仅限 infra/tests/docs/cmake 路径 |
| DIA-TC003 | 蓝图 4.3；infrastructure 设计 2.1 | Must | 跨模块调用通过 contracts 或冻结抽象，不直连实现类 | PolicyGuard、MetricsBridge、AuditBridge 只能依赖抽象接口 |
| DIA-TC004 | ADR-005；contracts 冻结总表 M5 | Must | 共享语义与边界冻结优先，新增字段优先 optional 与向后兼容 | 对象任务必须先冻结字段，再推进实现 |
| DIA-TC005 | ADR-006 | Must-Not | diagnostics 不承担 ContextPacket 组装与 Prompt 渲染职责 | 禁止在任务中引入上下文装配逻辑 |
| DIA-TC006 | ADR-007 | Must-Not | diagnostics 不做失败语义判定与恢复裁定，仅输出证据与建议引用 | Recovery 相关任务不得出现执行动作 |
| DIA-TC007 | ADR-008 | Must | diagnostics 不拥有全局调度权，仅服务主控链路 | DiagnosticsServiceFacade 只做入口与生命周期管理 |
| DIA-TC008 | diagnostics 设计 2.1、6.5；contracts 冻结约束 | Must-Not | 命令执行器、采样线程、导出后端等实现细节不得写入 contracts | 接口与对象任务只能冻结 diagnostics 私有抽象 |
| DIA-TC009 | diagnostics 设计 6.5/6.8/6.10；工程规范 3.6 | Must | 快照必须脱敏、失败不可吞没、高风险动作必须可审计 | 红线任务必须覆盖错误码、审计、指标与日志出口 |
| DIA-TC010 | 工程规范 3.7 | Should | 新增公共接口同步增加至少一个 unit 或 contract 测试 | 每个接口/对象任务必须绑定测试 |
| DIA-TC011 | diagnostics 设计 6.9；蓝图 5.1 | Must | Profile 只能裁剪能力，不得绕过 audit 与 runtime 主控链路 | 配置与导出任务必须体现 remote 默认禁用与 safe_mode 约束 |
| DIA-TC012 | 落地步骤指引 阶段 C | Must | infra 底座先行且每阶段必须可测试 | 执行顺序需先接口对象，再主链路，再门禁 |
| DIA-TC013 | diagnostics 设计 3.1、11.1；infra 专项 TODO INF-BLK-08 | Must | 剩余阻塞项已收敛到脱敏矩阵、导出细则与桥接接口；allowed_commands 参数 schema、CommandCatalog/ValidationResult、命令白名单与 integration 顶层拓扑已完成校准 | 必须显式输出剩余 Blocked 与解阻动作 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/CMakeLists.txt | 已接入 core/audit/plugin/tracing 等真实源码 | diagnostics 公共接口已落盘，但 diagnostics 服务实现仍未接入构建 |
| infra/include/ | 已落盘 IDiagnosticsService.h 与 diagnostics/DiagnosticsTypes.h | diagnostics 对外接口与首批对象已冻结，后续主链实现仅承接剩余局部阻塞 |
| infra/src/ | 仅有 config/health/logging/metrics/ota/secret/tracing 目录与 placeholder | diagnostics 实现目录尚未存在 |
| tests/CMakeLists.txt | 已接入 mocks/unit/contract/integration，且提供 dasall_integration_tests 聚合入口 | diagnostics 集成测试已可被顶层发现，但具体用例仍需随组件任务落盘 |
| tests/unit/CMakeLists.txt | 已接入 infra 子目录，且 tests/unit/infra/CMakeLists.txt 已注册 DiagnosticsSnapshotExportTest | diagnostics unit 发现性基础已具备 |
| tests/contract/CMakeLists.txt | 已有 centralized registration 机制 | 可承载 diagnostics 边界 contract 测试 |
| tests/integration/ | 已由 tests/CMakeLists.txt 顶层纳入构建，tests/integration/infra 已注册 InfraDiagnosticsSmokeTest | diagnostics integration 顶层发现性已具备，完整闭环用例仍取决于组件实现落盘 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：可直接生成 L3/L2 混合专项 TODO；当前最小可执行粒度为数据结构/接口/单链路骨架级，局部实现任务受设计缺口约束需先补设计。

证据：

1. 已有明确核心接口名与方法语义：IDiagnosticsService、IDiagnosticsCommandRegistry、IDiagnosticsPolicyGuard 及其方法集合在 diagnostics 设计 6.6 明确。
2. 已有明确核心对象字段：DiagnosticsCommand、CommandDecision、EvidenceBundle、DiagnosticsSnapshot、SnapshotExportResult 在 diagnostics 设计 6.5 明确。
3. 已有主流程与异常流程：6.7 正常 9 步、6.8 异常分类/恢复动作/兜底策略完整。
4. 已有错误码域、配置项、文件落点与测试出口：6.6、6.9、7、8.1、9.1 已给出。
5. 剩余证据缺口已收敛为脱敏矩阵、metrics/audit 桥接接口签名，以及导出格式与远程目标白名单；allowed_commands 参数 schema 已由 DIA-BLK-003 在本轮补齐，SnapshotQuery、SnapshotExportRequest、DiagnosticsSnapshotResult、CommandCatalog、ValidationResult 已分别由 INF-BLK-08 与 DIA-TODO-008 补齐。

### 4.2 粒度可行性评估表（Step 2：详细设计可执行性扫描输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| DiagnosticsCommand | diagnostics 设计 6.5 | L3 | 字段、白名单约束、actor_ref 语义明确，且只读命令的 args schema 已在 6.5.2 冻结 | 无 | 直接拆数据结构冻结任务；实现期只需遵守已冻结 schema |
| CommandDecision | diagnostics 设计 6.5/6.6 | L3 | allowed/reason_code/policy_ref/denied_rule_id 完整 | reason_code 到 contracts 映射矩阵未成文 | 直接拆对象任务，并补错误映射测试 |
| EvidenceBundle | diagnostics 设计 6.5 | L3 | logs_ref/metrics_ref/health_ref/errors_ref/artifacts 字段明确 | artifacts 子元素结构未展开 | 直接拆对象任务，内部聚合细节后置 |
| DiagnosticsSnapshot | diagnostics 设计 6.5/6.7/6.8 | L3 | 字段、脱敏、可导出、evidence_refs 约束明确 | get_snapshot 查询对象未定义 | 直接拆对象任务；查询对象另列补设计 |
| SnapshotExportResult | diagnostics 设计 6.5 | L3 | 字段、失败语义、导出约束明确 | format 枚举与 checksum 算法未展开 | 直接拆对象任务 |
| IDiagnosticsPolicyGuard | diagnostics 设计 6.6 | L3 | authorize 输入输出完整，接口头文件与 unit/contract 证据已落盘 | PolicySnapshot 结构未冻结，但不影响接口最小签名 | 后续直接进入实现骨架任务 |
| IDiagnosticsService | diagnostics 设计 6.6 | L3 | 方法名、请求/返回对象与远程导出默认门禁已冻结 | 无（2026-03-30 已由 INF-BLK-08 校准） | 维持 Done 证据并避免接口漂移 |
| IDiagnosticsCommandRegistry | diagnostics 设计 6.6 | L3 | 方法名、CommandCatalog/ValidationResult 字段、代码级最小对象定义与 validate 成功/失败语义已明确 | 无（2026-04-07 已由 DIA-BLK-003 补齐 v1 参数 schema） | 后续直接进入实现前阻塞校验 |
| CommandRegistry | diagnostics 设计 6.2/6.3/6.7 | L2 | 白名单职责、输入输出路径明确，且 health.snapshot / queue.stats / thread.dump 的 v1 参数 schema 已冻结 | 无 | 直接进入骨架任务 |
| CommandPolicyGuard | diagnostics 设计 6.2/6.3/6.4 | L2 | 准入职责、deny 必带策略引用明确 | security policy snapshot 最小字段未回链 | 直接拆骨架任务，前置依赖 security policy 抽象 |
| CommandExecutor | diagnostics 设计 6.2/6.3/6.7/6.8 | L2 | 执行职责、超时/异常结构化返回明确 | 执行结果内部对象未命名 | 直接拆类级骨架任务，备注无法细化到函数级 |
| EvidenceCollector | diagnostics 设计 6.2/6.3/6.7 | L2 | 聚合日志/指标/健康/错误摘要职责明确 | artifacts 聚合对象与来源接口未冻结 | 直接拆类级骨架任务，依赖相邻组件接口 |
| RedactionEngine | diagnostics 设计 6.2/6.3/6.8/6.9 | L2 | 脱敏职责、失败策略、配置键明确 | 脱敏规则矩阵未冻结 | 先解阻 D-BLK-02，再做骨架任务 |
| SnapshotAssembler | diagnostics 设计 6.2/6.7 | L2 | 输入输出、snapshot_id 唯一性约束明确 | snapshot_id 生成策略未展开 | 直接拆骨架任务 |
| SnapshotStore | diagnostics 设计 6.2/6.3/6.7/6.9 | L2 | 持久化、索引、保留窗口职责明确 | 存储后端首版形态未冻结 | 直接拆骨架任务，先按最小本地存储实现 |
| ExportManager | diagnostics 设计 6.2/6.3/6.8/6.9；11.1 | L2 | 本地/远程导出职责、远程默认禁用明确 | 导出格式、target 白名单、远程目标模型未冻结 | 先解阻 D-BLK-03，再做骨架任务 |
| DiagnosticsMetricsBridge | diagnostics 设计 6.2/6.10；11.1 | L1 | 指标名清单、metrics sink contract 与标签投影规则已在 6.10.1 冻结 | bridge runtime wiring 与 discoverability 证据仍待落盘 | 可直接拆 bridge 骨架任务 |
| DiagnosticsAuditBridge | diagnostics 设计 6.2/6.10；11.1 | L1 | 高风险动作审计字段、`IAuditLogger` sink contract 与 failure semantics 已在 6.10.1 冻结 | remote export / command extension 的 runtime wiring 证据仍待落盘 | 可直接拆 bridge 骨架任务 |
| tests/integration/infra/diagnostics | diagnostics 设计 8.1/9.1；tests 现状 | L0 | 路径与用例建议存在，且 tests 顶层 integration 拓扑已接入 | diagnostics integration 用例尚未完整落盘 | 直接拆 integration 注册任务 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| DiagnosticsCommand / CommandDecision / EvidenceBundle / DiagnosticsSnapshot / SnapshotExportResult 冻结 | diagnostics 设计 6.5 | 数据结构 | DIA-TODO-001、DIA-TODO-002、DIA-TODO-003、DIA-TODO-004、DIA-TODO-005 | 先稳定字段与兼容边界，再进入实现 |
| diagnostics 私有错误码域 | diagnostics 设计 6.6/6.8；工程规范 3.6 | 错误处理 | DIA-TODO-006 | 拒绝、超时、脱敏、存储、导出失败都需可判定 |
| SnapshotQuery / SnapshotExportRequest / DiagnosticsSnapshotResult | diagnostics 设计 6.6；INF-TODO-018 | 接口前置补设计（已完成） | DIA-TODO-007 | 已由 DiagnosticsTypes.h 与 IDiagnosticsService.h 落盘完成，作为 INF-BLK-08 校准证据保留 |
| CommandCatalog / ValidationResult | diagnostics 设计 6.6 | 接口前置补设计（已完成） | DIA-TODO-008 | 已在详细设计中补齐目录对象、校验返回对象与 schema return semantics，可直接支撑 IDiagnosticsCommandRegistry 接口冻结 |
| IDiagnosticsPolicyGuard 接口冻结 | diagnostics 设计 6.6 | 接口 | DIA-TODO-009 | 已完成；后续实现可直接依赖冻结签名 |
| IDiagnosticsService / IDiagnosticsCommandRegistry 接口冻结 | diagnostics 设计 6.6 | 接口 | DIA-TODO-010、DIA-TODO-011 | IDiagnosticsService 与 IDiagnosticsCommandRegistry 均已完成头文件冻结；registry 实现已具备遵循 6.5.2 schema 的前提 |
| DiagnosticsServiceFacade 生命周期与 safe_mode | diagnostics 设计 6.2/6.7/6.8/6.9 | 生命周期/初始化 | DIA-TODO-012 | 主入口与 safe_mode 单独拆出，避免与执行器耦合 |
| CommandRegistry / CommandPolicyGuard 准入链路 | diagnostics 设计 6.2/6.3/6.4/6.7 | 流程 | DIA-TODO-013、DIA-TODO-014 | 白名单校验与策略准入拆分单目标；registry 的 schema blocker 已由 DIA-BLK-003 解阻 |
| CommandExecutor / EvidenceCollector / SnapshotAssembler | diagnostics 设计 6.2/6.3/6.7/6.8 | 流程 | DIA-TODO-015、DIA-TODO-016、DIA-TODO-017 | 执行、聚合、组装拆分后更易独立验收 |
| RedactionEngine / SnapshotStore / ExportManager | diagnostics 设计 6.2/6.8/6.9/11.1 | 流程/配置 | DIA-TODO-018、DIA-TODO-019、DIA-TODO-020 | 脱敏、落盘、导出各自单独 gate |
| Metrics/Audit Bridge | diagnostics 设计 6.10/11.1 | 适配器/桥接 | DIA-TODO-021、DIA-TODO-022 | bridge sink contract 已冻结，可直接进入实现 |
| CMake 与测试门禁接线 | diagnostics 设计 7、8.1、9.1；代码现状 | 测试/门禁 | DIA-TODO-023、DIA-TODO-024、DIA-TODO-025 | 构建、unit/contract 可先做，integration 先阻塞 |
| 文档与交付证据回写 | diagnostics 设计 8.3 DIA-T010；9.2；11.1 | 文档/交付证据 | DIA-TODO-026 | 对 INF-TODO-018、INF-BLK-08 的执行证据做收口 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | DIA-TODO-009、010、011 |
| 数据结构定义类任务 | 是 | DIA-TODO-001~005 |
| 生命周期与初始化类任务 | 是 | DIA-TODO-012 |
| 适配器/桥接类任务 | 是 | DIA-TODO-021、022 |
| 异常与错误处理类任务 | 是 | DIA-TODO-006、018、020 |
| 配置与 Profile 裁剪类任务 | 是 | DIA-TODO-013、018、019、020 |
| 测试与门禁类任务 | 是 | DIA-TODO-023、024、025 |
| 文档/交付证据回写类任务 | 是 | DIA-TODO-026 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DIA-TODO-001 | Done | 定义 DiagnosticsCommand 数据结构 | diagnostics 设计 6.5；架构 9.5 | 6.5 DiagnosticsCommand | L3 | infra/include/diagnostics/DiagnosticsTypes.h | DiagnosticsCommand | unit：DiagnosticsTypesTest；contract：DiagnosticsBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "DiagnosticsTypesTest|DiagnosticsBoundaryContractTest" --output-on-failure | 无 | 无 | 无 | DiagnosticsTypes.h、对象测试；2026-03-31 已落盘 tests/unit/infra/DiagnosticsTypesTest.cpp、tests/contract/smoke/DiagnosticsBoundaryContractTest.cpp，并完成最小 CMake 注册 | 仅当 command_id、command_name、args、request_scope、timeout_ms、actor_ref 字段齐备且 command_name 白名单约束可由测试断言时完成 |
| DIA-TODO-002 | Done | 定义 CommandDecision 数据结构 | diagnostics 设计 6.5/6.6 | 6.5 CommandDecision；6.6 错误语义 | L3 | infra/include/diagnostics/DiagnosticsTypes.h | CommandDecision | unit：DiagnosticsTypesTest；contract：DiagnosticsErrorMappingContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "DiagnosticsTypesTest|DiagnosticsErrorMappingContractTest" --output-on-failure | 无 | 无 | 无 | DiagnosticsTypes.h、对象测试；2026-03-31 已补强 allow/deny 守卫与 reason_code 映射，并新增 tests/contract/smoke/DiagnosticsErrorMappingContractTest.cpp | 仅当 allowed、reason_code、policy_ref、denied_rule_id 字段齐备，且 deny 路径映射 contracts 语义可测时完成 |
| DIA-TODO-003 | Done | 定义 EvidenceBundle 数据结构 | diagnostics 设计 6.5；infrastructure 设计 6.5 | 6.5 EvidenceBundle | L3 | infra/include/diagnostics/DiagnosticsTypes.h | EvidenceBundle | unit：DiagnosticsTypesTest；contract：DiagnosticsBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "DiagnosticsTypesTest|DiagnosticsBoundaryContractTest" --output-on-failure | 无 | 无 | 无 | DiagnosticsTypes.h、对象测试；2026-03-31 已新增 EvidenceBundle 字段守卫，并扩展 DiagnosticsTypesTest/DiagnosticsBoundaryContractTest 校验引用边界 | 仅当 logs_ref、metrics_ref、health_ref、errors_ref、artifacts 字段齐备，且对象只保存引用与必要摘要时完成 |
| DIA-TODO-004 | Done | 定义 DiagnosticsSnapshot 数据结构 | diagnostics 设计 6.5/6.7/6.8；infra 专项 TODO INF-TODO-018 | 6.5 DiagnosticsSnapshot | L3 | infra/include/diagnostics/DiagnosticsTypes.h | DiagnosticsSnapshot | unit：DiagnosticsSnapshotExportTest；contract：DiagnosticsBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "DiagnosticsSnapshotExportTest|DiagnosticsBoundaryContractTest" --output-on-failure | DIA-TODO-001、DIA-TODO-003 | 无 | 无 | DiagnosticsTypes.h、对象测试；2026-03-31 已显式冻结 is_redaction_ready 守卫，并扩展 unit/contract 测试覆盖脱敏前置语义 | 仅当 snapshot_id、command、collected_at、summary、evidence_refs、redaction_profile、exporter_hint 字段齐备，且脱敏前置语义可测试时完成 |
| DIA-TODO-005 | Done | 定义 SnapshotExportResult 数据结构 | diagnostics 设计 6.5/6.8 | 6.5 SnapshotExportResult | L3 | infra/include/diagnostics/DiagnosticsTypes.h | SnapshotExportResult | unit：DiagnosticsSnapshotExportTest；contract：DiagnosticsErrorMappingContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "DiagnosticsSnapshotExportTest|DiagnosticsErrorMappingContractTest" --output-on-failure | DIA-TODO-004 | 无 | 无 | DiagnosticsTypes.h、对象测试；2026-03-31 已新增 SnapshotExportResult 守卫，并扩展 unit/contract 测试覆盖导出元数据与失败错误码绑定 | 仅当 export_id、target、format、size_bytes、checksum、created_at 字段齐备，且失败路径绑定错误码时完成 |
| DIA-TODO-006 | Done | 定义 DiagnosticsErrors 错误码域 | diagnostics 设计 6.6/6.8；工程规范 3.6 | 6.6 错误语义；6.8 异常分类 | L3 | infra/include/diagnostics/DiagnosticsErrors.h | INF_E_DIAG_COMMAND_DENIED、INF_E_DIAG_COMMAND_INVALID、INF_E_DIAG_EXEC_TIMEOUT、INF_E_DIAG_EXEC_FAIL、INF_E_DIAG_REDACTION_FAIL、INF_E_DIAG_SNAPSHOT_STORE_FAIL、INF_E_DIAG_EXPORT_FAIL、INF_E_DIAG_REMOTE_EXPORT_DISABLED | contract：DiagnosticsErrorMappingContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R DiagnosticsErrorMappingContractTest --output-on-failure | DIA-TODO-002、DIA-TODO-005 | 无 | 无 | DiagnosticsErrors.h、映射测试；2026-03-31 已新增 diagnostics 私有错误码头，并扩展 DiagnosticsErrorMappingContractTest 冻结 8 个错误码到 contracts ResultCode 的映射矩阵 | 仅当 8 个错误码全部可追溯到 6.6/6.8 条目且映射测试可阻止漂移时完成 |
| DIA-TODO-007 | Done | 补齐 IDiagnosticsService 请求与返回对象设计 | diagnostics 设计 6.6；硬约束 5；INF-TODO-018 | 6.6 IDiagnosticsService | L0 | infra/include/diagnostics/DiagnosticsTypes.h、infra/include/diagnostics/IDiagnosticsService.h | SnapshotQuery、SnapshotExportRequest、DiagnosticsSnapshotResult | unit：DiagnosticsSnapshotExportTest；integration：InfraDiagnosticsSmokeTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest" && ctest --test-dir build-ci --output-on-failure -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest" | 无 | 无（2026-03-30 已由 INF-BLK-08 校准确认） | 无 | DiagnosticsTypes.h、IDiagnosticsService.h、校准记录；2026-03-27 已随 INF-TODO-018 落盘 | 仅当三类对象具备字段、输入输出语义与错误约束，且对应测试可发现并通过时完成 |
| DIA-TODO-008 | Done | 补齐 CommandRegistry 目录与校验返回对象设计 | diagnostics 设计 6.6；硬约束 5 | 6.6 IDiagnosticsCommandRegistry | L0 | docs/architecture/DASALL_infra_diagnostics模块详细设计.md、docs/todos/infrastructure/deliverables/DIA-TODO-008-CommandRegistry目录与校验语义收敛.md | CommandCatalog、ValidationResult | process：对象边界补齐后可进入 DiagnosticsCommandRegistryTest | rg -n "CommandCatalog|ValidationResult|arg_schema_ref|field_paths" docs/architecture/DASALL_infra_diagnostics模块详细设计.md | 无 | 无（2026-04-07 已完成） | 无 | diagnostics 详细设计、设计收敛记录；2026-04-07 已补齐 registry 目录对象、validate 成功/失败语义与 schema ref/summary 返回边界 | 仅当目录对象与校验结果对象字段冻结、`field_paths`/`blocking_errors` 语义稳定，且 `list_commands`/`validate` 不内联完整 schema 时完成 |
| DIA-TODO-009 | Done | 定义 IDiagnosticsPolicyGuard 接口头文件 | diagnostics 设计 6.6；infrastructure 设计 6.6 | 6.6 IDiagnosticsPolicyGuard | L3 | infra/include/diagnostics/IDiagnosticsPolicyGuard.h | authorize(const DiagnosticsCommand&, const InfraContext&) -> CommandDecision | unit：DiagnosticsServiceInterfaceTest；contract：DiagnosticsBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "DiagnosticsServiceInterfaceTest|DiagnosticsBoundaryContractTest" --output-on-failure | DIA-TODO-001、DIA-TODO-002 | 无 | 无 | IDiagnosticsPolicyGuard.h、DiagnosticsServiceInterfaceTest.cpp、DiagnosticsBoundaryContractTest.cpp；2026-04-07 已落盘接口头文件并通过 interface/unit + boundary contract 验收 | 仅当接口签名与 6.6 一致、只依赖抽象类型且不暴露策略实现细节时完成 |
| DIA-TODO-010 | Done | 定义 IDiagnosticsService 接口头文件 | diagnostics 设计 6.6；infra 专项 TODO INF-TODO-018 | 6.6 IDiagnosticsService | L2 | infra/include/diagnostics/IDiagnosticsService.h | execute、get_snapshot、export_snapshot | unit：DiagnosticsSnapshotExportTest；integration：InfraDiagnosticsSmokeTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest" && ctest --test-dir build-ci --output-on-failure -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest" | DIA-TODO-001、DIA-TODO-004、DIA-TODO-005、DIA-TODO-007 | 无（2026-03-30 已由 INF-BLK-08 校准确认） | 无 | IDiagnosticsService.h、校准记录；2026-03-27 已随 INF-TODO-018 落盘 | 仅当三类请求/返回对象冻结后接口可无占位别名落盘，且 smoke/unit 证据通过时完成 |
| DIA-TODO-011 | Done | 定义 IDiagnosticsCommandRegistry 接口头文件 | diagnostics 设计 6.6 | 6.6 IDiagnosticsCommandRegistry | L2 | infra/include/diagnostics/IDiagnosticsCommandRegistry.h | list_commands、validate | unit：DiagnosticsServiceInterfaceTest；unit：DiagnosticsCommandRegistryTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "DiagnosticsServiceInterfaceTest|DiagnosticsCommandRegistryTest" --output-on-failure | DIA-TODO-001、DIA-TODO-008 | 无（2026-04-07 已由 DIA-TODO-008 解阻） | 无 | DiagnosticsTypes.h、IDiagnosticsCommandRegistry.h、DiagnosticsCommandRegistryTest.cpp；2026-04-07 已落盘 registry 接口头文件与最小目录/校验对象定义，并通过 interface/unit 验收 | 仅当目录与校验对象字段冻结后接口可落盘时完成 |
| DIA-TODO-012 | Done | 实现 DiagnosticsServiceFacade 生命周期与 safe_mode 骨架 | diagnostics 设计 6.2/6.7/6.8/6.9 | 6.2 DiagnosticsServiceFacade；6.8 兜底策略；6.9 safe_mode.failure_threshold | L2 | infra/src/diagnostics/DiagnosticsServiceFacade.cpp | DiagnosticsServiceFacade | unit：DiagnosticsServiceInterfaceTest；failure：InfraDiagnosticsSmokeTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "DiagnosticsServiceInterfaceTest|InfraDiagnosticsSmokeTest" --output-on-failure | DIA-TODO-010 | 无（2026-03-30 已由 INF-BLK-08 校准解阻） | 无；可直接基于已落盘的 IDiagnosticsService 请求/返回对象推进 | DiagnosticsServiceFacade.cpp、DiagnosticsServiceFacade.h、DiagnosticsServiceInterfaceTest.cpp、InfraDiagnosticsSmokeTest.cpp；2026-04-07 已落盘 facade 生命周期/safe_mode 骨架并通过 unit + smoke integration 验收 | 仅当 execute/get/export 生命周期、safe_mode 进入条件与失败可观测路径可二值判定时完成 |
| DIA-TODO-013 | Done | 实现 CommandRegistry 白名单治理骨架 | diagnostics 设计 6.2/6.3/6.7；11.1 | 6.2 CommandRegistry；6.5.2 allowed_commands 参数 schema；7 Design->Build | L2 | infra/src/diagnostics/CommandRegistry.cpp | CommandRegistry | unit：DiagnosticsCommandRegistryTest；unit：DiagnosticsCommandPolicyTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_diagnostics_command_registry_unit_test && cmake --build build-ci --target dasall_diagnostics_command_policy_unit_test && ctest --test-dir build-ci -R "DiagnosticsCommandRegistryTest|DiagnosticsCommandPolicyTest" --output-on-failure | DIA-TODO-011 | 无（2026-04-07 已由 DIA-BLK-003 解阻） | 无 | CommandRegistry.cpp、CommandRegistry.h、DiagnosticsCommandRegistryTest.cpp、DiagnosticsCommandPolicyTest.cpp、tests/unit/infra/CMakeLists.txt、infra/CMakeLists.txt；2026-04-07 已落盘真实 registry validate/capability gate 骨架并通过 2 条 unit 测试 | 仅当非白名单命令拒绝、参数非法路径可判定、空 args 可按 schema 规范化，且 registry 输出可稳定交给 policy handoff 测试时完成 |
| DIA-TODO-014 | Done | 实现 CommandPolicyGuard 准入骨架 | diagnostics 设计 6.2/6.3/6.4/6.7 | 6.2 CommandPolicyGuard；6.4 依赖关系 | L2 | infra/src/diagnostics/CommandPolicyGuard.cpp | CommandPolicyGuard | unit：DiagnosticsCommandPolicyTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_diagnostics_command_policy_unit_test && ctest --test-dir build-ci -R "DiagnosticsCommandRegistryTest|DiagnosticsCommandPolicyTest" --output-on-failure | DIA-TODO-002、DIA-TODO-009 | 无 | 无 | CommandPolicyGuard.cpp、CommandPolicyGuard.h、DiagnosticsCommandPolicyTest.cpp、infra/CMakeLists.txt；2026-04-07 已落盘真实 policy query translation 与 allow/deny skeleton，并通过 registry+policy 2 条 unit 测试 | 仅当 allow/deny 双路径都能返回 policy_ref，且实现只依赖 ISecurityPolicyManager 抽象时完成 |
| DIA-TODO-015 | Done | 实现 CommandExecutor 执行骨架 | diagnostics 设计 6.2/6.3/6.7/6.8 | 6.2 CommandExecutor；6.8 执行失败 | L2 | infra/src/diagnostics/CommandExecutor.cpp | CommandExecutor | unit：DiagnosticsCommandPolicyTest；failure：InfraDiagnosticsSmokeTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_diagnostics_command_policy_unit_test && cmake --build build-ci --target dasall_infra_diagnostics_smoke_integration_test && ctest --test-dir build-ci -R "DiagnosticsCommandPolicyTest|InfraDiagnosticsSmokeTest" --output-on-failure | DIA-TODO-014 | 无 | 无 | CommandExecutor.cpp、CommandExecutor.h、DiagnosticsCommandPolicyTest.cpp、DiagnosticsServiceFacade.cpp、InfraDiagnosticsSmokeTest.cpp、infra/CMakeLists.txt；2026-04-07 已落盘真实 executor success/failure skeleton 并通过 unit+smoke 2 条测试 | 仅当超时、执行失败、资源不可用三类错误可结构化返回且不越权执行变更型命令时完成 |
| DIA-TODO-016 | Done | 实现 EvidenceCollector 证据聚合骨架 | diagnostics 设计 6.2/6.3/6.7；架构 9.5 | 6.2 EvidenceCollector；6.7 步骤 5 | L2 | infra/src/diagnostics/EvidenceCollector.cpp | EvidenceCollector | unit：DiagnosticsTypesTest；integration：InfraDiagnosticsIntegrationTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra_diagnostics_integration_test && ctest --test-dir build-ci -R "DiagnosticsTypesTest|InfraDiagnosticsIntegrationTest|InfraDiagnosticsSmokeTest" --output-on-failure | DIA-TODO-003、DIA-TODO-015 | 无 | 无 | EvidenceCollector.cpp、EvidenceCollector.h、DiagnosticsServiceFacade.cpp、tests/integration/infra/InfraDiagnosticsIntegrationTest.cpp、tests/integration/infra/CMakeLists.txt、infra/CMakeLists.txt；2026-04-07 已落盘真实 EvidenceBundle 聚合 skeleton 并通过 3 条定向测试 | 仅当日志、指标、健康、错误摘要四类引用可进入 EvidenceBundle，且不引入相邻组件实现依赖时完成 |
| DIA-TODO-017 | Done | 实现 SnapshotAssembler 快照组装骨架 | diagnostics 设计 6.2/6.7 | 6.2 SnapshotAssembler；6.7 步骤 7 | L2 | infra/src/diagnostics/SnapshotAssembler.cpp | SnapshotAssembler | unit：DiagnosticsSnapshotExportTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_diagnostics_snapshot_export_unit_test && cmake --build build-ci --target dasall_infra_diagnostics_smoke_integration_test && cmake --build build-ci --target dasall_infra_diagnostics_integration_test && ctest --test-dir build-ci -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest|InfraDiagnosticsIntegrationTest" --output-on-failure | DIA-TODO-004、DIA-TODO-016 | 无 | 无 | SnapshotAssembler.cpp、SnapshotAssembler.h、DiagnosticsServiceFacade.cpp、DiagnosticsServiceFacade.h、DiagnosticsSnapshotExportTest.cpp、tests/unit/infra/CMakeLists.txt、infra/CMakeLists.txt；2026-04-07 已落盘真实 SnapshotAssembler 并通过 unit+smoke+integration 3 条定向测试 | 仅当 snapshot_id 生成、summary 组装、evidence_refs 引用绑定都可重复验证时完成 |
| DIA-TODO-018 | Done | 实现 RedactionEngine 脱敏骨架 | diagnostics 设计 6.2/6.3/6.8/6.9；11.1 | 6.2 RedactionEngine；6.8 脱敏失败；11.1 D-BLK-02 | L2 | infra/src/diagnostics/RedactionEngine.cpp | RedactionEngine | unit：DiagnosticsRedactionTest；failure：DiagnosticsRedactionFailureTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_diagnostics_redaction_unit_test && cmake --build build-ci --target dasall_diagnostics_redaction_failure_unit_test && cmake --build build-ci --target dasall_infra_diagnostics_smoke_integration_test && cmake --build build-ci --target dasall_infra_diagnostics_integration_test && ctest --test-dir build-ci -R "DiagnosticsRedactionTest|DiagnosticsRedactionFailureTest|InfraDiagnosticsSmokeTest|InfraDiagnosticsIntegrationTest" --output-on-failure | DIA-TODO-017 | 无（2026-04-07 已由 DIA-BLK-004 解阻） | 无 | RedactionEngine.cpp、RedactionEngine.h、DiagnosticsRedactionTest.cpp、DiagnosticsRedactionFailureTest.cpp、DiagnosticsServiceFacade.cpp、InfraDiagnosticsSmokeTest.cpp、tests/unit/infra/CMakeLists.txt、infra/CMakeLists.txt；2026-04-07 已落盘 strict/compat 脱敏骨架并通过 4 条定向测试 | 仅当敏感字段不落盘、脱敏失败阻断导出且测试可验证时完成 |
| DIA-TODO-019 | Done | 实现 SnapshotStore 持久化骨架 | diagnostics 设计 6.2/6.3/6.7/6.9 | 6.2 SnapshotStore；6.7 步骤 8；6.9 retention 配置 | L2 | infra/src/diagnostics/SnapshotStore.cpp | SnapshotStore | unit：DiagnosticsSnapshotStoreTest | cmake -S . -B build-ci && cmake --build build-ci --target dasall_diagnostics_snapshot_store_unit_test && cmake --build build-ci --target dasall_infra_diagnostics_smoke_integration_test && cmake --build build-ci --target dasall_infra_diagnostics_integration_test && ctest --test-dir build-ci -R "DiagnosticsSnapshotStoreTest|InfraDiagnosticsSmokeTest|InfraDiagnosticsIntegrationTest" --output-on-failure | DIA-TODO-004、DIA-TODO-017 | 无 | 无 | SnapshotStore.cpp、SnapshotStore.h、DiagnosticsSnapshotStoreTest.cpp、DiagnosticsServiceFacade.cpp、DiagnosticsServiceFacade.h、tests/unit/infra/CMakeLists.txt、infra/CMakeLists.txt；2026-04-07 已落盘 retention_days/max_count 内存持久化骨架，并把 facade retained map 收口到 SnapshotStore 后通过 3 条定向测试 | 仅当快照可持久化、可按 retention_days/max_count 清理且失败返回 INF_E_DIAG_SNAPSHOT_STORE_FAIL 时完成 |
| DIA-TODO-020 | Done | 实现 ExportManager 导出骨架 | diagnostics 设计 6.2/6.3/6.8/6.9；11.1 | 6.2 ExportManager；6.8 导出失败；11.1 D-BLK-03 | L2 | infra/src/diagnostics/ExportManager.cpp | ExportManager | unit：DiagnosticsExportTest；integration：InfraDiagnosticsIntegrationTest | cmake -S . -B build-ci && cmake --build build-ci --target dasall_diagnostics_service_interface_unit_test && cmake --build build-ci --target dasall_diagnostics_snapshot_export_unit_test && cmake --build build-ci --target dasall_diagnostics_export_unit_test && cmake --build build-ci --target dasall_infra_diagnostics_smoke_integration_test && cmake --build build-ci --target dasall_infra_diagnostics_integration_test && ctest --test-dir build-ci -R "DiagnosticsServiceInterfaceTest|DiagnosticsSnapshotExportTest|DiagnosticsExportTest|InfraDiagnosticsSmokeTest|InfraDiagnosticsIntegrationTest" --output-on-failure | DIA-TODO-005、DIA-TODO-018、DIA-TODO-019 | 无（2026-04-07 已由 DIA-BLK-005 解阻） | 无 | ExportManager.cpp、ExportManager.h、DiagnosticsExportTest.cpp、DiagnosticsServiceFacade.cpp、tests/unit/infra/CMakeLists.txt、InfraDiagnosticsSmokeTest.cpp、DiagnosticsSnapshotExportTest.cpp、DiagnosticsServiceInterfaceTest.cpp、infra/CMakeLists.txt；2026-04-07 已落盘本地 jsonl 导出、sha256 checksum 与 remote disabled gate 并通过 5 条定向测试 | 仅当本地导出可成功、远程未启用时强制拒绝、失败返回可判定错误码时完成 |
| DIA-TODO-021 | Done | 实现 DiagnosticsMetricsBridge 指标桥接骨架 | diagnostics 设计 6.2/6.10；11.1 | 6.2 DiagnosticsMetricsBridge；6.10 指标清单；6.10.1 metrics sink contract | L1 | infra/src/diagnostics/DiagnosticsMetricsBridge.cpp | DiagnosticsMetricsBridge | unit：DiagnosticsMetricsAuditBridgeTest | cmake -S . -B build-ci && cmake --build build-ci --target dasall_diagnostics_metrics_audit_bridge_unit_test && cmake --build build-ci --target dasall_diagnostics_service_interface_unit_test && cmake --build build-ci --target dasall_diagnostics_export_unit_test && cmake --build build-ci --target dasall_infra_diagnostics_smoke_integration_test && cmake --build build-ci --target dasall_infra_diagnostics_integration_test && ctest --test-dir build-ci -R "DiagnosticsMetricsAuditBridgeTest|DiagnosticsServiceInterfaceTest|DiagnosticsExportTest|InfraDiagnosticsSmokeTest|InfraDiagnosticsIntegrationTest" --output-on-failure | DIA-TODO-014、DIA-TODO-015、DIA-TODO-020 | 无（2026-04-07 已由 DIA-BLK-006 解阻） | 无 | DiagnosticsMetricsBridge.cpp、DiagnosticsMetricsBridge.h、DiagnosticsServiceFacade.cpp、DiagnosticsServiceFacade.h、CommandExecutor.cpp、CommandExecutor.h、DiagnosticsMetricsAuditBridgeTest.cpp、tests/unit/infra/CMakeLists.txt、infra/CMakeLists.txt；2026-04-07 已落盘 diagnostics 七指标桥接骨架并通过 5 条定向测试 | 仅当 infra_diag_command_total 等指标可按设计上报且桥接失败可观测时完成 |
| DIA-TODO-022 | Done | 实现 DiagnosticsAuditBridge 审计桥接骨架 | diagnostics 设计 6.2/6.10；11.1 | 6.2 DiagnosticsAuditBridge；6.10 审计字段；6.10.1 audit sink contract | L1 | infra/src/diagnostics/DiagnosticsAuditBridge.cpp | DiagnosticsAuditBridge | unit：DiagnosticsMetricsAuditBridgeTest；integration：InfraDiagnosticsIntegrationTest | cmake -S . -B build-ci && cmake --build build-ci --target dasall_diagnostics_metrics_audit_bridge_unit_test dasall_diagnostics_service_interface_unit_test dasall_infra_diagnostics_smoke_integration_test dasall_infra_diagnostics_integration_test && ctest --test-dir build-ci -R "DiagnosticsMetricsAuditBridgeTest|DiagnosticsServiceInterfaceTest|InfraDiagnosticsSmokeTest|InfraDiagnosticsIntegrationTest" --output-on-failure | DIA-TODO-014、DIA-TODO-020 | 无（2026-04-07 已由 DIA-BLK-006 解阻） | 无 | DiagnosticsAuditBridge.cpp、DiagnosticsAuditBridge.h、DiagnosticsServiceFacade.cpp、DiagnosticsServiceFacade.h、DiagnosticsMetricsAuditBridgeTest.cpp、InfraDiagnosticsIntegrationTest.cpp、InfraDiagnosticsSmokeTest.cpp、DiagnosticsServiceInterfaceTest.cpp、infra/CMakeLists.txt；2026-04-07 已落盘 required sink 审计桥接骨架，并通过 4 条定向测试 | 仅当远程导出、扩展命令执行等高风险动作可写审计，且审计失败不可静默时完成 |
| DIA-TODO-023 | Not Started | 注册 diagnostics 源码到 infra CMake | diagnostics 设计 7、8.1；代码现状 | 8.1 文件落盘建议；7 Design->Build 映射 | L2 | infra/CMakeLists.txt、infra/src/diagnostics/ | diagnostics include/src 文件接线 | build：dasall_infra 可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | DIA-TODO-001~DIA-TODO-022 | 无 | 无 | CMake 改动、构建记录 | 仅当 placeholder 不再是唯一源码入口且 diagnostics 文件进入 dasall_infra 构建图时完成 |
| DIA-TODO-024 | Not Started | 注册 diagnostics 的 unit 与 contract 测试入口 | diagnostics 设计 7、8.1、9.1；工程规范 3.7 | 8.1 tests 路径；9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt、tests/unit/infra/diagnostics/、tests/contract/CMakeLists.txt、tests/contract/infra/diagnostics/ | unit：DiagnosticsTypesTest、DiagnosticsServiceInterfaceTest、DiagnosticsCommandRegistryTest、DiagnosticsCommandPolicyTest、DiagnosticsRedactionTest、DiagnosticsSnapshotStoreTest、DiagnosticsExportTest；contract：DiagnosticsBoundaryContractTest、DiagnosticsErrorMappingContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | DIA-TODO-023 | 无 | 无 | 测试源文件、注册入口、ctest 发现性证据 | 仅当新增 diagnostics unit/contract 测试可被 ctest -N 发现并执行时完成 |
| DIA-TODO-025 | Not Started | 注册 diagnostics integration 测试入口 | diagnostics 设计 8.1、9.1；tests 现状；11.1 | 8.1 tests/integration/infra/diagnostics；9.1 Integration；11.1 D-BLK-05 | L0 | tests/integration/infra/diagnostics/、tests/CMakeLists.txt | integration：InfraDiagnosticsIntegrationTest、InfraDiagnosticsSmokeTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -N && ctest --test-dir build-ci -R "InfraDiagnosticsIntegrationTest|InfraDiagnosticsSmokeTest" --output-on-failure | DIA-TODO-020、DIA-TODO-021、DIA-TODO-022、DIA-TODO-023 | 无（2026-03-30 已由 INF-BLK-06 integration 顶层拓扑校准解阻） | 无；待 DIA-TODO-020、021、022、023 完成后落盘具体 integration 用例 | integration 注册改动或阻塞记录 | 仅当 tests 顶层完成 integration 接线且 diagnostics 集成用例可被 ctest 发现后，状态才可由 Not Started 转为 Done |
| DIA-TODO-026 | Not Started | 回写 diagnostics 质量门与交付证据 | diagnostics 设计 8.3 DIA-T010；9.2；11.1；infra 专项 TODO INF-TODO-018 | 8.3 DIA-T010；9.2 Gate；11.1 阻塞 | L2 | docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md、docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md | process test：门禁结论、INF-TODO-018 与 INF-BLK-08 状态变化、回退证据回写 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | DIA-TODO-024 | 无 | 无 | 更新后的 TODO 文档证据段 | 仅当每个门禁具备通过/失败结论及命令证据，并回链 INF-TODO-018/INF-BLK-08 时完成 |

### 6.2 当前 Blocked 任务索引

| 任务 ID | 对应阻塞项 |
|---|---|
| 无 | 无 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 对象与错误码冻结 | DIA-TODO-001~006 | 可并行 | 先冻结核心对象与错误语义，避免实现期字段漂移 |
| B 接口前置补设计 | DIA-TODO-008 | 已完成 | 2026-04-07 已把剩余设计缺口收敛到 registry/catalog 对象与 validate 返回语义 |
| C 接口冻结 | DIA-TODO-009、DIA-TODO-011 | 已完成 | 2026-04-07 已完成 IDiagnosticsPolicyGuard 与 IDiagnosticsCommandRegistry 公开接口冻结 |
| D 主链路骨架 | DIA-TODO-012、DIA-TODO-013、DIA-TODO-014、DIA-TODO-015、DIA-TODO-016、DIA-TODO-017 | 已完成 | 2026-04-07 已完成 Facade -> Registry -> PolicyGuard -> Executor -> EvidenceCollector -> SnapshotAssembler 主链骨架；下一步转入 E 阶段的脱敏/存储/导出 |
| E 脱敏/落盘/导出 | 已完成 | 2026-04-07 已完成 RedactionEngine、SnapshotStore、ExportManager 最小骨架；当前可转入 F 阶段桥接接口冻结 |
| F 桥接与门禁 | DIA-TODO-021、DIA-TODO-022、DIA-TODO-023、DIA-TODO-024、DIA-TODO-025 | 串行推进更稳妥：022 已完成，继续 023/024/025 | 2026-04-07 已由 DIA-BLK-006 冻结 bridge sink contract，且 DIA-TODO-021、DIA-TODO-022 已完成 diagnostics metrics/audit bridge；完整门禁与 integration 发现性收口仍以后置依赖为前提 |
| G 证据收口 | DIA-TODO-026 | 串行 | 回写质量门、阻塞变化与 INF-TODO-018 状态 |

### 7.2 必过门禁表

| Gate ID | 门禁项 | 触发时机 | 通过标准 | 不通过后动作 |
|---|---|---|---|---|
| DIA-GATE-01 | 对象冻结门 | 进入接口任务前 | DiagnosticsTypes 与 DiagnosticsErrors 落盘且 contract 测试可承载边界断言 | 回退到对象定义任务 |
| DIA-GATE-02 | 接口补设计门 | 推进 IDiagnosticsCommandRegistry 前 | SnapshotQuery、SnapshotExportRequest、DiagnosticsSnapshotResult 已由 INF-BLK-08 校准确认；CommandCatalog、ValidationResult 与 schema return semantics 已由 DIA-TODO-008 冻结 | 若对象或返回语义回退则恢复 DIA-BLK-002 |
| DIA-GATE-03 | 准入链路门 | 推进执行器前 | 白名单校验与策略准入双路径测试通过 | 回退 CommandRegistry / PolicyGuard |
| DIA-GATE-04 | 脱敏安全门 | 推进 ExportManager 前 | 脱敏规则冻结，DiagnosticsRedactionTest 与 DiagnosticsRedactionFailureTest 通过 | 回退 RedactionEngine |
| DIA-GATE-05 | 导出安全门 | 推进远程导出路径前 | remote.enabled 默认 false，target 白名单与格式约束冻结 | 未通过则禁止远程导出 |
| DIA-GATE-06 | 桥接接口门 | 推进 Metrics/Audit Bridge 前 | metrics 与 audit 最小桥接接口签名冻结 | 未通过则维持 Blocked |
| DIA-GATE-07 | 构建接线门 | 推进测试注册前 | dasall_infra 构建通过且 diagnostics 文件入图 | 修复 CMake 接线 |
| DIA-GATE-08 | 测试发现性门 | 提交前 | ctest -N 能发现新增 diagnostics unit/contract 测试 | 修复 tests 注册 |
| DIA-GATE-09 | integration 准入门 | 推进 DIA-TODO-025 前 | tests 顶层 integration 接线已校准，且 diagnostics 具体 integration 用例与桥接前置齐备 | 未通过前禁止 diagnostics integration 闭环验收 |
| DIA-GATE-10 | breaking 评审门 | 任意公共对象或错误映射变更前 | 已明确 breaking 风险、迁移窗口与回退方案 | 未评审不得推进 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| DIA-BLK-001 | 已解阻（2026-03-30）：SnapshotQuery、SnapshotExportRequest、DiagnosticsSnapshotResult 已在 DiagnosticsTypes.h 中落盘，IDiagnosticsService.h 已直接消费这些对象 | DIA-TODO-007、010、012 | 无；后续仅需保持 DiagnosticsTypes/IDiagnosticsService 与 smoke/unit 测试口径同步 | 证据回链到 infra 专项 TODO 的 INF-BLK-08 校准记录，以及 infra/include/diagnostics/DiagnosticsTypes.h、infra/include/diagnostics/IDiagnosticsService.h、tests/unit/infra/DiagnosticsSnapshotExportTest.cpp、tests/integration/infra/InfraDiagnosticsSmokeTest.cpp | 若请求/返回对象或接口签名回退，则重新转为 Blocked |
| DIA-BLK-002 | 已解阻（2026-04-07）：docs/architecture/DASALL_infra_diagnostics模块详细设计.md 6.5/6.5.1/6.6 已补齐 CommandCatalog、ValidationResult 与 arg_schema_ref/arg_schema_summary/field_paths 返回语义；docs/todos/infrastructure/deliverables/DIA-TODO-008-CommandRegistry目录与校验语义收敛.md 已记录设计结论 | DIA-TODO-011 | 无；后续仅需保持 diagnostics 详细设计、专项 TODO 与后续接口任务口径同步 | 证据回链到 diagnostics 详细设计、设计收敛记录与 DIA-TODO-008 完成记录 | 若目录对象或 validate 返回语义回退，则重新转为 Blocked |
| DIA-BLK-003 | 已解阻（2026-04-07）：docs/architecture/DASALL_infra_diagnostics模块详细设计.md 6.5.2、6.9 已冻结 `health.snapshot`、`queue.stats`、`thread.dump` 的 v1 schema_ref、`request_scope=runtime`、args token grammar 与 normalized default；docs/todos/infrastructure/deliverables/DIA-BLK-003-allowed_commands参数schema收敛.md 已记录解阻结论 | DIA-TODO-013 | 无；后续仅需按 6.5.2 实现 validate，并保持 TODO/worklog/设计口径同步 | 证据回链到 diagnostics 详细设计、设计收敛记录与本轮 worklog | 若后续实现允许 profile 改写参数 schema、重新放开变更型命令，或改变 v1 内建 schema 结构，则重新转为 Blocked |
| DIA-BLK-004 | 已解阻（2026-04-07）：diagnostics 详细设计 6.5.3 已冻结 strict/compat、字段分级矩阵、deny-list 与 redaction failure 兜底；docs/todos/infrastructure/deliverables/DIA-BLK-004-Redaction规则矩阵收敛.md 已记录解阻结论 | DIA-TODO-018 | 无；后续仅需按 6.5.3 实现 RedactionEngine 并保持 TODO/worklog/设计口径同步 | 证据回链到 diagnostics 详细设计、deliverable 与本轮 worklog | 若后续实现允许未脱敏 snapshot 落盘、放开未知 evidence scheme，或把 compat 扩成原样透传，则重新转为 Blocked |
| DIA-BLK-005 | 已解阻（2026-04-07）：diagnostics 详细设计 6.5.4 已冻结 `ExportFormat::Json -> UTF-8 JSON Lines`、`checksum=sha256:<64hex>`、本地 `target_ref` 规则与 remote exact-match `allowed_targets`；docs/todos/infrastructure/deliverables/DIA-BLK-005-导出格式与目标策略冻结.md 已记录解阻结论 | DIA-TODO-020 | 无；后续仅需按 6.5.4 实现 ExportManager 并保持 TODO/worklog/设计口径同步 | 证据回链到 diagnostics 详细设计、deliverable 与本轮 worklog | 若后续实现放开 `TextArchive`、接受非 `sha256` checksum，或把 remote allow-list 做成 prefix/wildcard，则重新转为 Blocked |
| DIA-BLK-006 | 已解阻（2026-04-07）：diagnostics 详细设计 6.10.1 已冻结 `DiagnosticsMetricsBridge` 对 `IMetricsProvider -> IMeter -> record(sample)` 的接入协议、meter scope、七指标族、`module/stage/profile/outcome/error_code` allowlist 与 non-recursive failure semantics；同章节已冻结 `DiagnosticsAuditBridge` 对 `IAuditLogger::write_audit` 的 action/target/evidence/context 映射与 required sink failure semantics；docs/todos/infrastructure/deliverables/DIA-BLK-006-桥接接口收敛.md 已记录解阻结论 | DIA-TODO-021、DIA-TODO-022 | 无；后续仅需按 6.10.1 实现 bridge 并保持 TODO/worklog/设计口径同步 | 证据回链到 diagnostics 详细设计、deliverable，以及 metrics/audit 已完成的接口/bridge 样板 | 若后续 diagnostics bridge 重新直连 exporter、自增非白名单标签，或把 audit failure 降级为静默放行，则重新转为 Blocked |
| DIA-BLK-007 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；diagnostics integration 是否可执行改由组件自身落盘负责 | DIA-TODO-025 | 无；后续仅需按组件落盘 integration 用例 | 证据回链到 infra 专项 TODO 的 INF-BLK-06 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt | 若 tests 顶层 integration 接线或聚合依赖回退，则重新转为 Blocked |

## 9. 验收与质量门

### 9.1 验收命令基线

| 用途 | 命令 |
|---|---|
| 配置构建目录 | cmake -S . -B build-ci -G Ninja |
| 构建 infra | cmake --build build-ci --target dasall_infra |
| 执行 unit 套件 | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit |
| 执行 contract 套件 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract |
| 检查测试发现性 | ctest --test-dir build-ci -N |

说明：

1. integration 命令本轮不纳入必过基线，原因是 DIA-TODO-025 尚未落盘具体 integration 用例；顶层 integration 拓扑已于 2026-03-30 解阻。
2. 每个可执行任务至少包含 1 条构建命令与 1 条测试命令；Block 任务保留解阻后的验收命令。
3. 对于来自详细设计的命名测试项，应优先使用 DiagnosticsTypesTest、DiagnosticsBoundaryContractTest、DiagnosticsCommandPolicyTest、DiagnosticsRedactionTest、DiagnosticsRedactionFailureTest、DiagnosticsSnapshotStoreTest、DiagnosticsExportTest、DiagnosticsMetricsAuditBridgeTest、InfraDiagnosticsIntegrationTest、InfraDiagnosticsSmokeTest。

### 9.2 质量门逐项回答

1. 是否给出 Design -> TODO 映射，而非仅任务标题：是。
2. 是否明确当前最细可达到粒度：是，L3/L2 混合，受阻项为 L0/L1。
3. 是否所有任务具备代码目标 + 测试目标 + 验收命令：是。
4. 是否所有 Blocked 项具备证据与解阻条件：是。
5. 是否所有任务具备可二值判定完成标准：是。
6. 是否避免跨子系统范围扩张：是。
7. 若要求函数/数据结构级，是否真正落到对象：是；无法细化处已标注证据缺口。

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 回退策略 |
|---|---|---|---|---|
| 命令越权执行风险 | High | 未冻结白名单就实现执行链路 | 出现非只读命令进入 CommandExecutor | 立即回退到 D-BLK-003，禁止所有非查询命令 |
| 脱敏失效风险 | High | RedactionEngine 规则缺失或绕过 | 导出内容含敏感字段、DiagnosticsRedactionFailureTest 失败 | 立即禁用导出，仅保留摘要快照 |
| 远程导出误开风险 | High | remote.enabled 默认门禁失效 | 未授权 target 出现导出尝试 | 回退为本地导出 only，并强制审计告警 |
| 边界越权风险 | High | diagnostics 输出恢复执行动作或依赖 runtime 实现 | 出现 retry/replan/rollback 调用或 runtime 具体 include | 回退到证据输出模式，仅保留错误码与 evidence_ref |
| 桥接接口漂移风险 | Medium | metrics/audit 抽象或 6.10.1 映射合同回退 | DiagnosticsMetricsAuditBridgeTest 无法稳定复现，或 bridge 开始依赖 exporter / 自定义标签 | 降级为日志 + 错误码观测 |
| 集成门禁过早推进风险 | Medium | diagnostics 具体 integration 用例未落盘前就提前写闭环验收 | ctest -N 仅能发现 smoke 或缺少目标用例 | 暂停 diagnostics integration 扩围，保留 unit/contract/smoke 作为执行基线 |

## 11. 可行性结论

### 11.1 结论

可直接生成接口/数据结构级专项 TODO，并可对部分主链路生成类级骨架任务；不能无补设计地直接生成完整函数级专项 TODO。

### 11.2 原因

1. DiagnosticsCommand、CommandDecision、EvidenceBundle、DiagnosticsSnapshot、SnapshotExportResult 已具备字段级证据，可安全拆到 L3。
2. IDiagnosticsPolicyGuard 已完成接口冻结，且 `authorize(const DiagnosticsCommand&, const InfraContext&) -> CommandDecision` 已通过 interface/unit 与 boundary contract 验收。
3. IDiagnosticsService 已完成首版对象/接口冻结；IDiagnosticsCommandRegistry 已完成公开头文件冻结，并以 DiagnosticsTypes.h 中的最小 `CommandCatalog` / `ValidationResult` 代码定义支撑可编译接口；CommandRegistry、CommandPolicyGuard、CommandExecutor、EvidenceCollector 与 SnapshotAssembler 已完成 validate/capability gate、allow/deny、执行 success/failure、EvidenceBundle 聚合与 snapshot 组装 skeleton，主链 D 阶段已完成。
4. RedactionEngine、ExportManager、DiagnosticsMetricsBridge 与 DiagnosticsAuditBridge 已完成最小骨架；remote export 的 required audit 与预留 command extension 审计合同已进入代码路径。
5. tests/integration 顶层已接线并可发现 InfraDiagnosticsSmokeTest，但完整 diagnostics 门禁与 integration 发现性收口仍需随 023/024/025 落盘。

### 11.3 当前最小可执行粒度

1. 数据结构：函数/字段级可执行。
2. 接口：IDiagnosticsPolicyGuard、IDiagnosticsService 与 IDiagnosticsCommandRegistry 均已完成方法级冻结。
3. 实现：DiagnosticsMetricsBridge、DiagnosticsAuditBridge 与 ExportManager 均已落盘最小骨架，可直接转入构建接线、测试注册与 integration 发现性门禁收口。

### 11.4 若未达到函数级，还缺哪些设计信息

1. diagnostics CMake / unit-contract / integration 发现性门禁的最终收口证据。

### 11.5 下一步建议

1. 直接进入 DIA-TODO-023，正式收口 diagnostics 源码在 infra 构建图中的接线证据。
2. 023 完成后推进 024/025，收口 diagnostics 的 unit/contract/integration 发现性。
3. 桥接与门禁任务完成后再执行 DIA-TODO-026，统一回写 diagnostics 质量门证据。