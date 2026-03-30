# DASALL infrastructure 子系统 audit 组件专项 TODO

最近更新时间：2026-03-30  
阶段：Detailed Design -> Special TODO  
适用范围：infra/audit

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_infra_audit模块详细设计.md
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
17. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md
18. docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md
19. docs/todos/infrastructure/DASALL_infrastructure_watchdog组件专项TODO.md
20. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md
21. docs/todos/platform/DASALL_platform_linux组件专项TODO.md
22. docs/todos/profiles/DASALL_profiles子系统专项TODO.md
23. 当前代码与测试现状：infra/CMakeLists.txt、infra/include/、infra/src/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt

生成原则：

1. 不改写 ADR-005/006/007/008 已冻结结论。
2. 不越过 infrastructure/audit 组件边界扩张到无关模块。
3. 不把讨论事项或缺失设计项伪装为 Build-ready 实现任务。
4. 每项任务必须具备代码目标、测试目标、验收命令三件套。
5. 若设计证据不足，必须输出 Blocked 与补设计前置任务。

## 2. 子系统目标与范围

### 2.1 组件目标

1. 提供独立于普通运行日志的审计写入、导出、保留与失败兜底能力。
2. 提供可追责的 AuditEvent/AuditContext/AuditWriteOutcome 等核心对象，并保持 contracts 语义兼容。
3. 提供主写失败后的 fallback、告警、降级状态与健康信号。
4. 与 infra/logging、infra/metrics、infra/health、infra/config 协同，但不接管业务决策、恢复裁定或上下文装配。

### 2.2 范围边界

纳入范围：

1. audit 对外接口、核心对象、错误码、配置键、主流程与异常流程。
2. AuditServiceFacade、AuditValidator、AuditPipeline、AuditFallbackPipeline、AuditExporter、AuditRetentionManager、AuditMetricsBridge、AuditHealthProbe 的落盘任务拆分。
3. audit 的 CMake 接线、unit/contract/integration 测试注册点、质量门与阻塞项。

不纳入范围：

1. runtime 的重试、重规划、恢复准入与状态机推进。
2. logging 普通日志主链路实现与 sink 复用决策。
3. contracts 共享对象扩写、业务字段再定义、平台线程池或存储后端实现细节的 contracts 化。
4. WAL 回放、远程归档等 v2 演进项。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1：约束与边界抽取输出）

| ID | 来源 | 类型 | 约束内容 | 对 audit TODO 的影响 |
|---|---|---|---|---|
| AUD-TC001 | audit 设计 1.1/2.1；架构 3.4.7/5.10/8.8 | Must | 审计必须作为独立观测链路存在，不与普通运行日志混存 | 任务必须单独冻结审计对象、接口与存储骨架 |
| AUD-TC002 | 架构 3.7；蓝图 4.2 | Must | 依赖方向单向，audit 不反向依赖 runtime/cognition/llm 实现 | 代码目标仅限 infra/tests/docs/cmake 路径 |
| AUD-TC003 | 蓝图 4.3；infrastructure 设计 2.1 | Must | 跨模块语义通过 contracts 冻结对象传递 | AuditEvent/AuditContext 只能消费既有标识语义 |
| AUD-TC004 | ADR-005；contracts 计划 | Must | contracts 与关键边界冻结优先，breaking 变更必须评审 | 所有公共对象默认 non-breaking，breaking 风险需单列 gate |
| AUD-TC005 | ADR-006 | Must-Not | audit 不承担上下文装配与 Prompt 渲染 | 只能记录事实字段，不生成语义上下文 |
| AUD-TC006 | ADR-007 | Must-Not | audit 不拥有失败归因与恢复裁定权 | 失败任务仅输出错误码、证据与降级状态 |
| AUD-TC007 | ADR-008 | Must | 审计链路需保留 parent_task_id、lease_id、worker_type 等协同标识 | AuditContext 任务必须覆盖协同链路字段 |
| AUD-TC008 | audit 设计 2.1、6.5；contracts 冻结约束 | Must-Not | sink、线程池、具体存储后端等实现细节不得写入 contracts | 接口与对象任务只能冻结 audit 私有抽象 |
| AUD-TC009 | audit 设计 6.8/6.10；编码规范 3.6 | Must | 审计失败不可静默丢失，错误必须可观测 | 主写失败、fallback 失败、导出失败都要绑定错误码和观测出口 |
| AUD-TC010 | 编码规范 3.7 | Should | 新增公共接口至少配 1 个 unit 或 contract 测试 | 每个接口/对象任务都要绑定 AuditTypesTest、AuditInterfaceCompileTest 或 contract 测试 |
| AUD-TC011 | audit 设计 6.9；蓝图 5.1 | Must | Profile 只能裁剪能力，不得绕过审计 required 主链路 | 配置任务不得把 audit 设计成可完全关闭 |
| AUD-TC012 | 落地步骤指引 阶段 C | Must | infra 底座先行且每阶段都要可验证 | 执行顺序必须先对象/接口，再主链路，再测试与门禁 |
| AUD-TC013 | audit 设计 11.1 | Must | 导出过滤、retention、metrics/health 桥接、integration 接线存在阻塞项 | 必须显式输出 Blocked 与最小解阻动作 |
| AUD-TC014 | OTel Logs Spec + OWASP Logging Cheat Sheet；audit 设计 4.3 | Should | 审计需保留 who/what/when/where/outcome 与 trace/span 关联字段 | 对象与导出任务必须保留关联字段与脱敏边界 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/CMakeLists.txt | 仅编译 src/placeholder.cpp | audit 尚未接入构建 |
| infra/include/ | 空目录 | audit 对外接口与对象未落盘 |
| infra/src/ | 仅有 config/health/logging/metrics/ota/secret/tracing 空目录与 placeholder | audit 实现目录尚未存在 |
| tests/CMakeLists.txt | 已接入 mocks/unit/contract/integration，且提供 dasall_integration_tests 聚合入口 | audit 集成测试已可被顶层发现，但具体用例仍需随组件任务落盘 |
| tests/unit/CMakeLists.txt | 未接入 infra 子目录 | audit unit 发现性缺失 |
| tests/contract/CMakeLists.txt | 已有 centralized registration 机制 | 可承载 audit contracts 边界测试 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：可直接生成 L3/L2 混合专项 TODO；当前最小可执行粒度为接口/数据结构级，局部可细化到单错误路径或单链路骨架任务。

证据：

1. 已有明确核心接口清单：IAuditLogger、IAuditRetention、IAuditHealthProbe。
2. 已有明确核心对象字段：AuditEvent、AuditContext、ExportQuery、ExportResult、AuditWriteOutcome。
3. 已有主流程与异常流程：主写链路 6 步、异常分类 3 类、恢复动作 4 类。
4. 已有错误码域：INF_E_AUDIT_INVALID_EVENT、INF_E_AUDIT_WRITE_FAIL、INF_E_AUDIT_FALLBACK_FAIL、INF_E_AUDIT_EXPORT_DENIED、INF_E_AUDIT_EXPORT_FAIL、INF_E_AUDIT_RETENTION_FAIL。
5. 已有落盘路径与测试出口：infra/include/audit、infra/src/audit、tests/unit/infra/audit、tests/contract/infra、tests/integration/infra，以及 AuditTypesTest、AuditInterfaceCompileTest、AuditServiceFallbackTest、AuditExportFilterTest、AuditBoundaryContractTest、InfraErrorCodeMappingContractTest、InfraAuditHealthIntegrationTest。
6. 仍有证据缺口：RetentionOutcome 字段未定义、AuditHealthStatus 字段未定义、导出过滤细粒度模型未冻结、metrics/health 桥接接口未冻结、tests 顶层 integration 未接线。

### 4.2 粒度可行性评估表（Step 2：详细设计可执行性扫描输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| AuditEvent | audit 设计 6.5 | L3 | 字段、必填约束、contracts 对齐关系明确 | side_effects 子结构细项未展开 | 直接拆数据结构冻结任务 |
| AuditContext | audit 设计 6.5 | L3 | 字段与 unknown 兜底语义明确 | 字段类型别名未成文 | 直接拆数据结构冻结任务 |
| AuditWriteOutcome | audit 设计 6.5/6.6 | L3 | accepted/persisted/fallback_used/error_code 四字段明确 | error_code 精细映射矩阵未成文 | 直接拆数据结构冻结任务 |
| ExportQuery | audit 设计 6.5 | L3 | 字段、时间窗必填、分页稳定约束明确 | target/outcome 细粒度过滤语义未冻结 | 直接拆数据结构冻结任务 |
| ExportResult | audit 设计 6.5 | L3 | records/next_page_token/truncated/checksum 字段明确 | records 元素序列化形态未成文 | 直接拆数据结构冻结任务 |
| IAuditLogger | audit 设计 6.6 | L3 | write_audit/export_audit 方法名、输入输出、后置条件明确 | 无 | 直接拆接口冻结任务 |
| IAuditRetention | audit 设计 6.6 | L1 | 方法名与职责明确 | RetentionOutcome 字段未定义 | 先补对象设计，再进接口任务 |
| IAuditHealthProbe | audit 设计 6.6 | L1 | 方法名与状态职责明确 | AuditHealthStatus 字段未定义 | 先补对象设计，再进接口任务 |
| AuditValidator | audit 设计 6.2/6.3/6.7/6.8 | L2 | 输入输出、必填校验、非法输入语义明确 | 函数签名与校验结果对象未成文 | 直接拆字段校验骨架任务 |
| AuditPipeline | audit 设计 6.2/6.3/6.7/6.8 | L2 | append-only 主写链路与失败动作明确 | 存储抽象接口未冻结 | 直接拆主写骨架任务 |
| AuditFallbackPipeline | audit 设计 6.2/6.3/6.8 | L2 | ringbuffer/file 降级职责与 degraded 语义明确 | fallback 存储抽象接口未冻结 | 直接拆降级骨架任务 |
| AuditServiceFacade | audit 设计 6.2/6.3/6.4/6.7 | L2 | 生命周期管理、统一错误映射、调用关系明确 | init/start 方法签名未成文 | 直接拆入口骨架任务 |
| AuditExporter | audit 设计 6.2/6.3/6.5；11.1 | L2 | ExportQuery/ExportResult 字段明确，导出/脱敏职责明确 | 导出 filter 细粒度字段与 contract 边界未冻结 | 先输出 Blocked，再补最小过滤模型 |
| AuditRetentionManager | audit 设计 6.2/6.3/6.6；11.1 | L0 | 保留期与归档职责明确 | RetentionOutcome、归档/清理动作对象未定义 | 先补设计 |
| AuditMetricsBridge | audit 设计 6.2/6.3/6.10；11.1 | L1 | 指标名清单与桥接职责明确 | metrics 侧桥接接口与标签白名单未冻结 | 先解阻再实现 |
| AuditHealthProbe 组件 | audit 设计 6.2/6.3/6.10；11.1 | L1 | ready/degraded/unavailable 语义明确 | 健康状态对象与 health 侧接口未冻结 | 先补对象设计再实现 |
| tests/integration/infra | audit 设计 8.1/9.1；tests 现状 | L0 | 路径与用例建议存在 | tests 顶层未 add_subdirectory(integration) | 先解阻测试拓扑 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| AuditEvent/AuditContext/AuditWriteOutcome 冻结 | audit 设计 6.5/6.6 | 数据结构 | AUD-TODO-001、AUD-TODO-002、AUD-TODO-003 | 先稳定输入输出对象和错误承载面，再进入主链路实现 |
| ExportQuery/ExportResult 冻结 | audit 设计 6.5 | 数据结构 | AUD-TODO-004、AUD-TODO-005 | 导出对象已具备字段，可先冻结最小边界 |
| IAuditLogger 接口冻结 | audit 设计 6.6 | 接口 | AUD-TODO-006 | 先稳定调用面，避免上层直接绑具体 sink |
| 审计私有错误码域 | audit 设计 6.6/6.8；编码规范 3.6 | 错误处理 | AUD-TODO-007 | 主写、fallback、导出、retention 失败都需可判定 |
| AuditValidator 字段与边界校验 | audit 设计 6.2/6.3/6.7/6.8 | 流程 | AUD-TODO-008 | 将输入合法性校验与主写链路拆分，便于单独验收 |
| AuditPipeline/AuditFallbackPipeline 主异常链路 | audit 设计 6.2/6.3/6.7/6.8 | 流程 | AUD-TODO-009、AUD-TODO-010 | 主写与降级各自单目标，避免任务过大 |
| AuditServiceFacade 统一入口 | audit 设计 6.2/6.3/6.4/6.7 | 生命周期/初始化 | AUD-TODO-011 | 用统一入口串起 validator/pipeline/fallback |
| AuditExporter 导出与脱敏 | audit 设计 6.2/6.3/6.5；11.1 | 流程/测试 | AUD-TODO-012、AUD-BLK-001 | 设计已有最小对象，但过滤模型细项仍需补冻结 |
| IAuditRetention 与 retention 管理 | audit 设计 6.2/6.6；11.1 | 接口/流程 | AUD-TODO-013、AUD-BLK-002 | 因 RetentionOutcome 缺失，先补对象设计 |
| IAuditHealthProbe 与健康状态 | audit 设计 6.2/6.6；11.1 | 接口/适配器 | AUD-TODO-014、AUD-BLK-003 | 因 AuditHealthStatus 缺失，先补对象设计 |
| AuditMetricsBridge 指标桥接 | audit 设计 6.2/6.10；11.1 | 适配器/集成 | AUD-TODO-015、AUD-BLK-004 | 依赖 metrics 侧桥接接口冻结 |
| audit 构建接线与测试发现性 | audit 设计 7、8.1、9.1；代码现状 | 测试/门禁 | AUD-TODO-016、AUD-TODO-017、AUD-TODO-018、AUD-BLK-005 | 构建、unit/contract 可先做，integration 先阻塞 |
| 质量门与交付证据回写 | audit 设计 9.2、11.1 | 文档/交付证据 | AUD-TODO-019 | 对 gate、阻塞变化与回退证据做收口 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | AUD-TODO-006、AUD-TODO-013、AUD-TODO-014 |
| 数据结构定义类任务 | 是 | AUD-TODO-001~005 |
| 生命周期与初始化类任务 | 是 | AUD-TODO-011 |
| 适配器/桥接类任务 | 是 | AUD-TODO-014、AUD-TODO-015 |
| 异常与错误处理类任务 | 是 | AUD-TODO-007、AUD-TODO-009、AUD-TODO-010 |
| 配置与 Profile 裁剪类任务 | 是 | AUD-TODO-012、AUD-TODO-013、AUD-TODO-014（均受阻塞项约束） |
| 测试与门禁类任务 | 是 | AUD-TODO-016、AUD-TODO-017、AUD-TODO-018 |
| 文档/交付证据回写类任务 | 是 | AUD-TODO-019 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| AUD-TODO-001 | Done | 定义 AuditEvent 数据结构 | audit 设计 6.5；ADR-008；编码规范 3.7 | 6.5 AuditEvent | L3 | infra/include/audit/AuditTypes.h | AuditEvent | unit：AuditTypesTest；contract：AuditBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest" --output-on-failure | 无 | 无 | 无 | AuditTypes.h、对象测试 | 仅当 event_id/action/actor/target/outcome/evidence_ref/side_effects/timestamp 与设计一致，且 contract 测试可阻止越权字段时完成 |
| AUD-TODO-002 | Done | 定义 AuditContext 数据结构 | audit 设计 6.5；ADR-008 | 6.5 AuditContext | L3 | infra/include/audit/AuditTypes.h | AuditContext | unit：AuditTypesTest；contract：AuditBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest" --output-on-failure | 无 | 无 | 无 | AuditTypes.h、对象测试 | 仅当 request_id/session_id/trace_id/task_id/parent_task_id/lease_id/worker_type 字段齐备，且缺失语义为 unknown 而非空指针时完成 |
| AUD-TODO-003 | Done | 定义 AuditWriteOutcome 数据结构 | audit 设计 6.5/6.6；编码规范 3.6 | 6.5 AuditWriteOutcome；6.6 错误语义 | L3 | infra/include/audit/AuditTypes.h | AuditWriteOutcome | unit：AuditTypesTest；contract：InfraErrorCodeMappingContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditTypesTest|InfraErrorCodeMappingContractTest" --output-on-failure | 无 | 无 | 无 | AuditTypes.h、对象测试 | 仅当 accepted/persisted/fallback_used/error_code 四字段齐备且错误码映射可测时完成 |
| AUD-TODO-004 | Done | 定义 ExportQuery 数据结构 | audit 设计 6.5；11.1 阻塞项 | 6.5 ExportQuery | L3 | infra/include/audit/AuditExporterTypes.h | ExportQuery | unit：AuditExportFilterTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R AuditExportFilterTest --output-on-failure | 无 | 无 | 无 | AuditExporterTypes.h、过滤测试 | 仅当 start_ts/end_ts/actor/action/target/outcome/page_token 字段落盘，且时间窗必填语义可由测试验证时完成 |
| AUD-TODO-005 | Done | 定义 ExportResult 数据结构 | audit 设计 6.5 | 6.5 ExportResult | L3 | infra/include/audit/AuditExporterTypes.h | ExportResult | unit：AuditExportFilterTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R AuditExportFilterTest --output-on-failure | AUD-TODO-004 | 无 | 无 | AuditExporterTypes.h、导出测试 | 仅当 records/next_page_token/truncated/checksum 字段齐备，且 truncated 显式语义可测试时完成 |
| AUD-TODO-006 | Done | 定义 IAuditLogger 接口头文件 | audit 设计 6.6；编码规范 3.7 | 6.6 IAuditLogger | L3 | infra/include/audit/IAuditLogger.h | IAuditLogger::write_audit；IAuditLogger::export_audit | unit：AuditInterfaceCompileTest；contract：AuditBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditInterfaceCompileTest|AuditBoundaryContractTest" --output-on-failure | AUD-TODO-001、AUD-TODO-002、AUD-TODO-003、AUD-TODO-004、AUD-TODO-005 | 无 | 无 | IAuditLogger.h、编译测试 | 仅当接口签名与 6.6 一致、职责只覆盖写入与导出，且不暴露 sink/线程池等实现细节时完成 |
| AUD-TODO-007 | Not Started | 定义 AuditErrors 错误码域 | audit 设计 6.6/6.8；编码规范 3.6 | 6.6 错误语义；6.8 异常恢复 | L3 | infra/include/audit/AuditErrors.h | INF_E_AUDIT_INVALID_EVENT、INF_E_AUDIT_WRITE_FAIL、INF_E_AUDIT_FALLBACK_FAIL、INF_E_AUDIT_EXPORT_DENIED、INF_E_AUDIT_EXPORT_FAIL、INF_E_AUDIT_RETENTION_FAIL | contract：InfraErrorCodeMappingContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R InfraErrorCodeMappingContractTest --output-on-failure | AUD-TODO-003 | 无 | 无 | AuditErrors.h、映射测试 | 仅当 6 个错误码均可追溯到设计条目，且 contract 测试能阻止漂移时完成 |
| AUD-TODO-008 | Not Started | 实现 AuditValidator 字段校验骨架 | audit 设计 6.2/6.3/6.7/6.8 | 6.2 AuditValidator；6.3 输入输出；6.8 输入异常 | L2 | infra/src/audit/AuditValidator.cpp | AuditValidator（字段完整性与边界校验） | unit：AuditTypesTest；contract：AuditBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest" --output-on-failure | AUD-TODO-001、AUD-TODO-002、AUD-TODO-007 | 无 | 无 | AuditValidator.cpp、校验证据 | 仅当必填字段缺失、越权字段、非法时间窗三类输入异常都能返回可判定失败时完成 |
| AUD-TODO-009 | Not Started | 实现 AuditPipeline 主写骨架 | audit 设计 6.2/6.3/6.7/6.8 | 6.2 AuditPipeline；6.7 正常路径第 3 步；6.8 主写失败 | L2 | infra/src/audit/AuditPipeline.cpp | AuditPipeline（append-only 主写链路） | unit：AuditServiceFallbackTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R AuditServiceFallbackTest --output-on-failure | AUD-TODO-008 | 无 | 无 | AuditPipeline.cpp、主写测试 | 仅当验证通过事件能进入 append-only 主写路径，且主写失败可被上层捕获时完成 |
| AUD-TODO-010 | Not Started | 实现 AuditFallbackPipeline 降级骨架 | audit 设计 6.2/6.3/6.8 | 6.2 AuditFallbackPipeline；6.8 恢复动作 1/2 | L2 | infra/src/audit/AuditFallbackPipeline.cpp | AuditFallbackPipeline（ringbuffer/file 降级链路） | unit：AuditServiceFallbackTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R AuditServiceFallbackTest --output-on-failure | AUD-TODO-007、AUD-TODO-009 | 无 | 无 | AuditFallbackPipeline.cpp、降级测试 | 仅当主写失败可触发 fallback_used=true，且 fallback 失败返回 INF_E_AUDIT_FALLBACK_FAIL 时完成 |
| AUD-TODO-011 | Not Started | 实现 AuditServiceFacade 入口骨架 | audit 设计 6.2/6.3/6.4/6.7 | 6.2 AuditServiceFacade；6.4 依赖关系；6.7 主流程 | L2 | infra/src/audit/AuditService.cpp | AuditServiceFacade（审计入口、生命周期管理、统一错误映射） | unit：AuditServiceFallbackTest；contract：InfraErrorCodeMappingContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditServiceFallbackTest|InfraErrorCodeMappingContractTest" --output-on-failure | AUD-TODO-008、AUD-TODO-009、AUD-TODO-010 | 无 | 无 | AuditService.cpp、主链路测试 | 仅当 write_audit 主链路可串起 validator/pipeline/fallback，且返回结果可二值判定时完成 |
| AUD-TODO-012 | Blocked | 实现 AuditExporter 导出与脱敏骨架 | audit 设计 6.2/6.3/6.5；11.1 | 6.2 AuditExporter；6.3 导出语义；11.1 导出 filter 阻塞 | L2 | infra/src/audit/AuditExporter.cpp | AuditExporter（过滤、分页、脱敏） | unit：AuditExportFilterTest；contract：AuditBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditExportFilterTest|AuditBoundaryContractTest" --output-on-failure | AUD-TODO-004、AUD-TODO-005、AUD-TODO-006 | AUD-BLK-001 | 冻结 ExportQuery 的最小过滤语义，并明确时间窗+actor+action 三键的 contract 边界 | AuditExporter.cpp 或阻塞记录 | 仅当最小过滤模型冻结并通过评审后，状态才可由 Blocked 转为 Not Started |
| AUD-TODO-013 | Blocked | 定义 IAuditRetention 接口与 RetentionOutcome 对象 | audit 设计 6.6；11.1 | 6.6 IAuditRetention；11.1 retention 阻塞 | L1 | infra/include/audit/IAuditRetention.h | IAuditRetention::apply_retention；RetentionOutcome | unit：AuditInterfaceCompileTest；contract：InfraErrorCodeMappingContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditInterfaceCompileTest|InfraErrorCodeMappingContractTest" --output-on-failure | AUD-TODO-007 | AUD-BLK-002 | 补齐 RetentionOutcome 的字段与自动清理/归档动作对象后再冻结接口 | IAuditRetention.h 或阻塞记录 | 仅当 retention 输出对象具备可二值判定字段且评审通过后，状态才可从 Blocked 转为 Not Started |
| AUD-TODO-014 | Blocked | 定义 IAuditHealthProbe 接口与 AuditHealthStatus 对象 | audit 设计 6.6；11.1 | 6.6 IAuditHealthProbe；6.3/6.10 健康状态语义 | L1 | infra/include/audit/IAuditHealthProbe.h | IAuditHealthProbe::evaluate；AuditHealthStatus | unit：AuditInterfaceCompileTest；integration：InfraAuditHealthIntegrationTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest" --output-on-failure | AUD-TODO-007 | AUD-BLK-003 | 补齐 ready/degraded/unavailable 状态对象与最近失败原因字段后再冻结接口 | IAuditHealthProbe.h 或阻塞记录 | 仅当 AuditHealthStatus 字段与状态机语义冻结后，状态才可从 Blocked 转为 Not Started |
| AUD-TODO-015 | Blocked | 实现 AuditMetricsBridge 指标桥接骨架 | audit 设计 6.2/6.3/6.10；11.1 | 6.2 AuditMetricsBridge；6.10 指标清单；11.1 桥接阻塞 | L1 | infra/src/audit/AuditMetricsBridge.cpp | AuditMetricsBridge（audit_write_total 等指标桥接） | integration：InfraAuditHealthIntegrationTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R InfraAuditHealthIntegrationTest --output-on-failure | AUD-TODO-011 | AUD-BLK-004 | metrics 侧桥接接口、标签白名单与上报失败语义冻结 | AuditMetricsBridge.cpp 或阻塞记录 | 仅当指标桥接接口冻结且 integration 接线具备后，状态才可从 Blocked 转为 Not Started |
| AUD-TODO-016 | Not Started | 注册 audit 源码到 infra CMake | audit 设计 7、8.1；代码现状 | 7 Design -> Build 映射；8.1 文件落盘建议 | L2 | infra/CMakeLists.txt | audit include/src 文件接线 | build：dasall_infra 可编译；unit：AuditInterfaceCompileTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R AuditInterfaceCompileTest --output-on-failure | AUD-TODO-001 至 AUD-TODO-011 | 无 | 无 | CMake 改动、构建记录 | 仅当 placeholder 不再是唯一源码入口且 audit 文件进入 dasall_infra 构建图时完成 |
| AUD-TODO-017 | Not Started | 注册 audit 的 unit 与 contract 测试入口 | audit 设计 8.1、9.1；编码规范 3.7；tests 现状 | 8.1 路径建议；9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt、tests/unit/infra/audit/、tests/contract/CMakeLists.txt、tests/contract/infra/ | unit：AuditTypesTest、AuditInterfaceCompileTest、AuditServiceFallbackTest、AuditExportFilterTest；contract：AuditBoundaryContractTest、InfraErrorCodeMappingContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | AUD-TODO-016 | 无 | 无 | 测试源文件、注册入口、ctest 发现性证据 | 仅当新增 audit unit/contract 测试可被 ctest -N 发现并执行时完成 |
| AUD-TODO-018 | Not Started | 注册 audit integration 测试入口 | audit 设计 8.1、9.1；tests 现状；11.1 | 8.1 tests/integration/infra；9.1 Integration；11.1 integration 阻塞 | L0 | tests/integration/infra/、tests/CMakeLists.txt | integration：InfraAuditHealthIntegrationTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -N && ctest --test-dir build-ci -R InfraAuditHealthIntegrationTest --output-on-failure | AUD-TODO-014、AUD-TODO-015、AUD-TODO-016 | 无（2026-03-30 已由 INF-BLK-06 integration 顶层拓扑校准解阻） | 无；待 AUD-TODO-014、015、016 完成后落盘具体 integration 用例 | integration 注册改动或阻塞记录 | 仅当 tests 顶层完成 integration 接线且用例可被 ctest 发现后，状态才可从 Not Started 转为 Done |
| AUD-TODO-019 | Not Started | 回写 audit 质量门与交付证据 | audit 设计 9.2、11.1 | 9.2 Gate；11.1 阻塞与回退 | L2 | docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md | process test：门禁结论、阻塞变化、回退证据回写 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | AUD-TODO-017 | 无 | 无 | 更新后的 TODO 文档证据段 | 仅当每个门禁都具备通过/失败结论和命令证据时完成 |

### 6.2 当前 Blocked 任务索引

| 任务 ID | 对应阻塞项 |
|---|---|
| AUD-TODO-012 | AUD-BLK-001 |
| AUD-TODO-013 | AUD-BLK-002 |
| AUD-TODO-014 | AUD-BLK-003 |
| AUD-TODO-015 | AUD-BLK-004 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 对象冻结 | AUD-TODO-001~005 | 可并行 | 先冻结输入输出对象与导出对象，避免实现期反复改字段 |
| B 接口与错误语义冻结 | AUD-TODO-006、AUD-TODO-007 | 可并行 | 稳定 IAuditLogger 与错误码域，阻断直接绑实现 |
| C 主链路骨架 | AUD-TODO-008、AUD-TODO-009、AUD-TODO-010、AUD-TODO-011 | 串行 | validator -> pipeline -> fallback -> facade |
| D 构建与测试接线 | AUD-TODO-016、AUD-TODO-017 | 可并行 | 代码入图与测试发现性都依赖 A-C，但互不阻塞 |
| E 受阻能力补齐 | AUD-TODO-012、AUD-TODO-013、AUD-TODO-014、AUD-TODO-015、AUD-TODO-018 | 串行按阻塞项解锁 | 导出 -> retention -> health 状态 -> metrics 桥接 -> integration |
| F 证据收口 | AUD-TODO-019 | 串行 | 统一回写 gate、阻塞变化与回退证据 |

### 7.2 必过门禁表

| Gate ID | 门禁项 | 触发时机 | 通过标准 | 不通过后动作 |
|---|---|---|---|---|
| AUD-GATE-01 | 接口冻结门 | 进入主链路实现前 | AuditTypes/IAuditLogger/AuditErrors 落盘且编译通过 | 回退到对象/接口定义任务 |
| AUD-GATE-02 | 主写与降级闭环门 | 执行 AUD-TODO-011 后 | 主写失败可触发 fallback，且错误码可判定 | 回退 AUD-TODO-009/AUD-TODO-010 |
| AUD-GATE-03 | contracts 边界门 | AUD-TODO-017 前后 | AuditBoundaryContractTest 与 InfraErrorCodeMappingContractTest 通过 | 回退对象定义或错误码映射 |
| AUD-GATE-04 | 测试发现性门 | 提交前 | ctest -N 能发现新增 audit unit/contract 测试 | 修复 tests 注册 |
| AUD-GATE-05 | 导出边界门 | 推进 AUD-TODO-012 前 | ExportQuery 最小过滤模型冻结并经评审 | 未冻结则维持 Blocked |
| AUD-GATE-06 | retention 设计门 | 推进 AUD-TODO-013 前 | RetentionOutcome 与归档/清理动作对象冻结 | 未冻结则维持 Blocked |
| AUD-GATE-07 | 健康/指标桥接门 | 推进 AUD-TODO-014/AUD-TODO-015 前 | AuditHealthStatus 与 metrics/health 桥接接口冻结 | 未冻结则维持 Blocked |
| AUD-GATE-08 | integration 准入门 | 推进 AUD-TODO-018 前 | tests 顶层完成 integration 接线并定义标签规范 | 未通过前禁止 integration 验收 |
| AUD-GATE-09 | breaking 评审门 | 任意公共对象或错误映射变更前 | 已明确 breaking 风险、迁移窗口与回退方案 | 未评审不得推进 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| AUD-BLK-001 | ExportQuery 细粒度 filter 模型未冻结，当前仅有字段表，过滤语义与越权边界未固定 | AUD-TODO-012 | 明确时间窗、actor、action 三键最小过滤模型及 target/outcome 的扩展规则 | 在 audit 设计文档补充最小过滤语义表与脱敏边界 | 导出先降级为受限环境下的只读全量或时间窗导出 |
| AUD-BLK-002 | RetentionOutcome 字段、归档动作对象、自动清理证据语义未冻结 | AUD-TODO-013 | 补齐 retention 输出对象与归档/清理的二值判定字段 | 在 audit 设计文档补 retention 输出对象表与清理痕迹规则 | 暂停自动清理，仅保留 retention.days=30 的手动清理策略 |
| AUD-BLK-003 | AuditHealthStatus 字段未冻结，无法安全定义 IAuditHealthProbe 返回对象 | AUD-TODO-014 | 冻结 ready/degraded/unavailable 状态对象及最近失败原因字段 | 在 audit 设计文档补健康状态对象表 | 暂时仅在 audit 内部记录 degraded 本地状态，不对外暴露探针接口 |
| AUD-BLK-004 | metrics/health 桥接接口、标签白名单与上报失败语义未冻结 | AUD-TODO-015 | metrics 与 health 侧给出最小桥接接口和标签约束 | 在 infra/metrics 与 infra/health 详细设计补桥接接口章节 | 暂时保留本地计数和错误日志，不宣称生产级桥接 |
| AUD-BLK-005 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；audit integration 是否可执行改由组件自身落盘负责 | AUD-TODO-018 | 无；后续仅需按组件落盘 integration 用例 | 证据回链到 infra 专项 TODO 的 INF-BLK-06 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt | 若 tests 顶层 integration 接线或聚合依赖回退，则重新转为 Blocked |

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

1. integration 命令本轮不纳入必过基线，原因是 AUD-TODO-018 尚未落盘具体 integration 用例；顶层 integration 拓扑已于 2026-03-30 解阻。
2. 每个可执行任务至少包含 1 条构建命令与 1 条测试命令；Block 任务保留解阻后的验收命令。

### 9.2 质量门逐项回答

1. 是否给出 Design -> TODO 映射，而非仅任务标题：是。
2. 是否明确当前最细可达到粒度：是，L3/L2 混合。
3. 是否所有任务具备代码目标 + 测试目标 + 验收命令：是。
4. 是否所有 Blocked 项具备证据与解阻条件：是。
5. 是否所有任务具备可二值判定完成标准：是。
6. 是否避免跨子系统范围扩张：是。
7. 若要求函数/数据结构级，是否真正落到对象与单链路目标：是。

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 回退策略 |
|---|---|---|---|---|
| 审计与普通日志混流 | High | 复用 logging 单管线或共享 sink 而无隔离 | AuditBoundaryContractTest 失败或存储路径未分离 | 回退到独立 AuditPipeline 与独立存储目录 |
| 审计失败被吞没 | High | 主写失败未触发 fallback 或无错误码/告警 | AuditServiceFallbackTest 失败；错误计数无变化 | 立即回退到 AUD-TODO-009/AUD-TODO-010，并补错误码与观测出口 |
| contracts 语义漂移 | High | 在 AuditEvent/AuditContext 中引入实现字段或重定义共享语义 | AuditBoundaryContractTest 或 InfraErrorCodeMappingContractTest 失败 | 回退对象字段变更并走 breaking 评审门 |
| 导出越权或泄露敏感字段 | High | 未冻结 filter/redaction 语义即推进导出实现 | AuditExportFilterTest 失败；审计导出包含敏感字段 | 回退导出功能到最小时间窗导出或关闭导出实现 |
| retention 清理导致证据丢失 | High | 自动清理先于归档/清理痕迹规则冻结 | 审计样本不可回放或无清理证据 | 暂停自动清理，仅保留手动清理和归档标记 |
| 健康/指标桥接误宣称生产可用 | Medium | 在桥接接口未冻结前绑定 metrics/health | InfraAuditHealthIntegrationTest 不稳定或无法发现 | 回退到 audit 内部本地状态与计数，不暴露对外桥接 |

## 11. 可行性结论

### 11.1 结论

可直接生成并执行接口/数据结构级专项 TODO，并对主写、降级、错误路径输出组件骨架级任务；当前不宜全面进入函数实现级。

### 11.2 原因

1. 已有明确核心接口清单与方法语义，足以冻结 IAuditLogger 与相关对象。
2. 已有核心对象字段、主流程、异常流程和错误码域，足以拆出 AuditEvent、AuditContext、AuditWriteOutcome、ExportQuery、ExportResult 与主写/降级骨架任务。
3. 已有文件落盘建议、测试名称与验收命令基线，足以让任务具备代码目标、测试目标、验收命令三件套。
4. 但 RetentionOutcome、AuditHealthStatus、metrics/health 桥接接口、integration 顶层接线仍缺失，不能安全伪造函数级实现任务。
5. 导出 filter 细粒度模型与 retention 证据规则仍需补设计，必须先 Blocked 再推进。

### 11.3 当前最小可执行粒度

接口 / 数据结构 / 单链路骨架。

### 11.4 若未达到函数级，还缺哪些设计信息

1. AuditValidator、AuditPipeline、AuditFallbackPipeline、AuditServiceFacade 的精确方法签名与返回对象。
2. RetentionOutcome 与 AuditHealthStatus 的字段定义。
3. AuditExporter 的最小过滤语义与脱敏边界。
4. metrics/health 桥接接口及 integration 顶层接线规则。

### 11.5 下一步建议

1. 先执行 AUD-TODO-001 至 AUD-TODO-011、AUD-TODO-016、AUD-TODO-017，建立 audit 的对象、接口、主写/降级最小闭环与测试发现性。
2. 并行补齐 AUD-BLK-001 至 AUD-BLK-005 对应设计缺口，再推进导出、retention、健康/指标桥接与 integration。

## 12. 本轮执行记录（2026-03-30）

### 12.1 AUD-TODO-001

选中任务：

1. 任务 ID：AUD-TODO-001。
2. 可执行性依据：无前置依赖；audit 设计 6.5 已冻结 AuditEvent 字段表；当前仓库已有 audit 骨架和测试入口，可在一轮内完成字段收敛、边界测试和兼容修复。

研究学习：

1. 本地证据：docs/architecture/DASALL_infra_audit模块详细设计.md 6.5/6.6 已明确 AuditEvent 字段为 event_id/action/actor/target/outcome/evidence_ref/side_effects/timestamp，且 contracts 边界仅允许引用 ToolResult、RecoveryOutcome、WorkerTask 标识语义。
2. 外部参考：OTel Logs Data Model 强调时间戳与 trace 关联字段应保留为结构化顶层字段；OWASP Logging Cheat Sheet 要求审计字段覆盖 who/what/when/where/outcome 且与普通日志链路隔离。

D 结论：

1. Design -> Build 映射：新增 infra/include/audit/AuditTypes.h 冻结 AuditEvent/AuditEvidenceRef/AuditOutcome/AuditEvidenceKind；保留 infra/include/AuditEvent.h 作为兼容 include，避免后续实现阶段重复分叉字段。
2. Build 三件套：
	- 代码目标：新增 AuditTypes.h，收敛旧 AuditEvent.h，补齐受影响 AuditEvent 构造点并更新 infra/CMakeLists.txt 的 PUBLIC_HEADER。
	- 测试目标：新增 tests/unit/infra/AuditTypesTest.cpp 与 tests/contract/smoke/AuditBoundaryContractTest.cpp；同步 unit/contract 注册名为 AuditTypesTest、AuditBoundaryContractTest。
	- 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest" --output-on-failure；补充 cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "AuditTypesTest|AuditBoundaryContractTest"。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. infra/include/audit/AuditTypes.h：新增 AuditEvent 冻结字段、AuditEvidenceRef 和审计枚举；evidence kind 扩展到 WorkerTask，满足 ADR-008 协同标识引用要求。
2. infra/include/AuditEvent.h：改为兼容 include，避免旧引用路径继续分叉定义。
3. tests/unit/infra/AuditTypesTest.cpp、tests/contract/smoke/AuditBoundaryContractTest.cpp：分别覆盖 AuditEvent 必填字段、timestamp/side_effects 守卫和 contracts 引用边界。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成新头文件和测试命名收敛。
5. tests/unit/infra/AuditLoggerInterfaceTest.cpp、tests/unit/infra/AuditServiceFallbackTest.cpp、tests/contract/smoke/AuditServiceBoundaryContractTest.cpp、profiles/src/ProfileTelemetryAdapter.cpp：完成 AuditEvent 新字段引入后的直接兼容修复，保证现有调用面继续可编译。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest" --output-on-failure：通过，目标测试 2/2 通过。
3. cmake --build build-ci --target dasall_unit_tests dasall_contract_tests：通过；unit 40/40、contract 95/95 全部通过。
4. ctest --test-dir build-ci -N -R "AuditTypesTest|AuditBoundaryContractTest"：通过，发现 2 个测试，分别为 AuditTypesTest 与 AuditBoundaryContractTest。
5. ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest" --output-on-failure：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：本轮新增对象与守卫语义可由命名直接表达，无需额外注释。
2. 正负例覆盖：unit 覆盖合法 AuditEvent、缺失 event_id、非法 timestamp、重复或空 side_effects；contract 覆盖 ToolResult/RecoveryOutcome/WorkerTask 三条正例和 unspecified evidence 负例。
3. 测试发现性：已补充 ctest -N -R 证据，确认新注册名称可被发现。
4. TODO 证据回写：已完成任务状态、交付物、发现性和验收结果回写。
5. 提交隔离：本轮提交范围限定为 AuditEvent 类型冻结、测试收敛、CMake 命名调整及直接兼容修复，不扩张到 AuditContext/ExportQuery 等后续任务。

### 12.2 AUD-TODO-002

选中任务：

1. 任务 ID：AUD-TODO-002。
2. 可执行性依据：AUD-TODO-001 已完成，AuditTypes.h 和 AuditBoundaryContractTest 已落盘；本轮只需在同一类型头中补充 AuditContext，并扩展现有测试覆盖 unknown 兜底语义。

研究学习：

1. 本地证据：audit 设计 6.5 明确 AuditContext 字段为 request_id/session_id/trace_id/task_id/parent_task_id/lease_id/worker_type，且缺失语义必须是 unknown 而不是 null 指针。
2. 外部参考：OTel Logs Data Model 建议保留 trace 关联字段为结构化字段；OWASP Logging 对高风险审计要求保留 who/what/when/where/outcome 之外的追踪锚点，支撑后续追责与关联分析。

D 结论：

1. Design -> Build 映射：在 infra/include/audit/AuditTypes.h 中新增 AuditContext，并冻结 unknown 常量占位语义，避免把内部相关标识做成 optional/null 语义。
2. Build 三件套：
	- 代码目标：新增 AuditContext 与 non-empty/unknown helper，保持其为 audit 私有对象，不扩写 goal_id/checkpoint_ref/global_fsm_state 等越界字段。
	- 测试目标：扩展 AuditTypesTest 覆盖 unknown 默认值、显式相关标识保留和空字符串负例；扩展 AuditBoundaryContractTest 覆盖字段类型、越界字段缺失和 empty-string 负例。
	- 验收命令：cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "AuditTypesTest|AuditBoundaryContractTest" && ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest" --output-on-failure。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. infra/include/audit/AuditTypes.h：新增 AuditContext 和 kAuditContextUnknown，占位语义固定为 unknown。
2. tests/unit/infra/AuditTypesTest.cpp：新增 AuditContext 默认 unknown、显式标识保留和空字符串负例覆盖。
3. tests/contract/smoke/AuditBoundaryContractTest.cpp：新增 AuditContext 字段类型和越界字段缺失断言，确保不把 goal/checkpoint/global_fsm 状态带入 audit context。

验收结果：

1. cmake --build build-ci --target dasall_unit_tests dasall_contract_tests：通过。
2. ctest --test-dir build-ci -N -R "AuditTypesTest|AuditBoundaryContractTest"：通过，发现 2 个测试，分别为 AuditTypesTest 与 AuditBoundaryContractTest。
3. ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest" --output-on-failure：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：AuditContext 字段和 helper 命名已足够表达 unknown 兜底语义，无需额外注释。
2. 正负例覆盖：unit 覆盖 unknown 默认值、显式相关标识保留和空 request_id 负例；contract 覆盖字段非 optional 字符串、禁止越界字段和空 task_id 负例。
3. 测试发现性：沿用 001 已建立的 AuditTypesTest/AuditBoundaryContractTest 注册名，并再次用 ctest -N -R 回填发现性证据。
4. TODO 证据回写：已完成任务状态、交付物和验收结果回写。
5. 提交隔离：本轮只改 AuditContext 与同名测试，不提前引入 AuditWriteOutcome/ExportQuery 语义。

### 12.3 AUD-TODO-003

选中任务：

1. 任务 ID：AUD-TODO-003。
2. 可执行性依据：AUD-TODO-001、002 已冻结输入与上下文对象；当前仓库已有 infra 错误码映射 contract 测试，可直接承载 AuditWriteOutcome 的 error_code 约束。

研究学习：

1. 本地证据：audit 设计 6.5/6.6 要求 AuditWriteOutcome 仅包含 accepted/persisted/fallback_used/error_code 四字段，且 error_code 必须映射到既有 contracts::ResultCode，不新增共享码域。
2. 外部参考：OWASP Logging 对高风险链路要求失败必须可观测；OTel 语义建议把错误状态做成结构化字段，而不是依赖模糊文本消息。

D 结论：

1. Design -> Build 映射：在 infra/include/audit/AuditTypes.h 中新增 AuditWriteOutcome，以 optional contracts::ResultCode 承载失败错误码，避免使用伪成功码或新增 success sentinel。
2. Build 三件套：
	- 代码目标：新增 AuditWriteOutcome 与一致性 helper，明确 primary success、fallback degraded success 和 failure 三种可二值判定状态。
	- 测试目标：扩展 AuditTypesTest 覆盖 success/degraded success/failure 与不一致组合；扩展 infra 错误码 contract 测试覆盖 AuditWriteOutcome::error_code 类型与映射约束，并将 ctest 名称收敛为 InfraErrorCodeMappingContractTest。
	- 验收命令：cmake --build build-ci --target dasall_infra dasall_contract_tests && ctest --test-dir build-ci -R "AuditTypesTest|InfraErrorCodeMappingContractTest" --output-on-failure。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. infra/include/audit/AuditTypes.h：新增 AuditWriteOutcome、状态一致性校验和 success/failure helper。
2. tests/unit/infra/AuditTypesTest.cpp：新增 primary success、fallback degraded success、mapped failure 和不一致组合负例覆盖。
3. tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp、tests/contract/CMakeLists.txt：新增 AuditWriteOutcome error_code contract 约束，并把 ctest 名称更新为 InfraErrorCodeMappingContractTest。

验收结果：

1. cmake --build build-ci --target dasall_infra dasall_contract_tests：通过。
2. ctest --test-dir build-ci -R "AuditTypesTest|InfraErrorCodeMappingContractTest" --output-on-failure：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：AuditWriteOutcome helper 命名已直接表达状态机语义，无需补注释。
2. 正负例覆盖：unit 覆盖 success、degraded success、failure 和 persisted-with-error 等负例；contract 覆盖 error_code 只能落在既有 contracts::ResultCode 范围。
3. 测试发现性：ctest 名称已收敛为 InfraErrorCodeMappingContractTest，并在目标命令中被显式命中。
4. TODO 证据回写：已完成任务状态、交付物和验收结果回写。
5. 提交隔离：本轮只冻结输出对象与映射测试，不提前引入 ExportQuery/ExportResult 对象。

### 12.4 AUD-TODO-004

选中任务：

1. 任务 ID：AUD-TODO-004。
2. 可执行性依据：audit 设计 6.5 已明确 ExportQuery 字段集合；当前仓库尚无 AuditExporterTypes.h 和 AuditExportFilterTest，可直接冻结最小查询对象而不碰 006 之后的导出接口切换。

研究学习：

1. 本地证据：audit 设计 6.5 规定 ExportQuery 仅包含 start_ts/end_ts/actor/action/target/outcome/page_token，且时间窗必填、分页必须稳定。
2. 约束边界：AUD-BLK-001 仍指向更细粒度 filter 语义未冻结，因此本轮只固化字段和时间窗规则，不提前定义 target/outcome 的扩展过滤协议。

D 结论：

1. Design -> Build 映射：新增 infra/include/audit/AuditExporterTypes.h，定义 ExportQuery，并通过 helper 固化时间窗必填、窗口顺序、分页 token 与 outcome 过滤是否启用的最小语义。
2. Build 三件套：
	- 代码目标：落盘 ExportQuery 七个字段与最小 helper，不改旧 AuditExportFilter/AuditExportResult 接口。
	- 测试目标：新增 AuditExportFilterTest，覆盖字段类型冻结、有效时间窗、分页恢复 token 和缺失/倒序时间窗负例。
	- 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_audit_export_filter_unit_test && ctest --test-dir build-ci -R AuditExportFilterTest --output-on-failure。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. infra/include/audit/AuditExporterTypes.h：新增 ExportQuery 结构与时间窗/分页 helper。
2. tests/unit/infra/AuditExportFilterTest.cpp：新增 ExportQuery 冻结与负例测试。
3. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt：新增头文件导出与 AuditExportFilterTest 注册。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_audit_export_filter_unit_test：通过。
3. ctest --test-dir build-ci -R AuditExportFilterTest --output-on-failure：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：ExportQuery helper 命名已直接表达时间窗和分页语义，无需额外注释。
2. 正负例覆盖：unit 覆盖七字段类型冻结、有效时间窗、page_token 恢复以及缺失/倒序时间窗负例。
3. 测试发现性：AuditExportFilterTest 已加入 tests/unit/infra/CMakeLists.txt，并能被 ctest -R 命中执行。
4. TODO 证据回写：已完成任务状态、交付物和验收结果回写。
5. 提交隔离：本轮只冻结 ExportQuery 和测试入口，不提前定义 ExportResult 或切换旧导出接口签名。

### 12.5 AUD-TODO-005

选中任务：

1. 任务 ID：AUD-TODO-005。
2. 可执行性依据：AUD-TODO-004 已落盘 AuditExporterTypes.h 和 AuditExportFilterTest，本轮只需在同一头文件中补齐 ExportResult，并复用现有测试目标扩展输出边界覆盖。

研究学习：

1. 本地证据：audit 设计 6.5 明确 ExportResult 字段为 records/next_page_token/truncated/checksum，其中 truncated 必须显式表达。
2. 约束边界：records 的最终序列化形态仍未成文，因此本轮仅冻结 records 容器边界为 AuditEvent 列表，不提前引入额外 DTO 或导出格式枚举。

D 结论：

1. Design -> Build 映射：在 infra/include/audit/AuditExporterTypes.h 中新增 ExportResult，并通过 helper 固化 checksum 存在性、分页 token 与 truncated 的一致性，以及 final page 的显式语义。
2. Build 三件套：
	- 代码目标：新增 ExportResult 四字段与最小分页 helper，保持新旧导出结果对象并存。
	- 测试目标：扩展 AuditExportFilterTest 覆盖 records 类型冻结、checksum 字段、truncated=true 时必须存在 next_page_token，以及 final page 不得携带恢复 token 的负例。
	- 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_audit_export_filter_unit_test && ctest --test-dir build-ci -R AuditExportFilterTest --output-on-failure。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. infra/include/audit/AuditExporterTypes.h：新增 ExportResult、checksum helper 和分页一致性 helper。
2. tests/unit/infra/AuditExportFilterTest.cpp：新增 ExportResult 正例与 truncated 负例覆盖。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_audit_export_filter_unit_test：通过。
3. ctest --test-dir build-ci -R AuditExportFilterTest --output-on-failure：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：ExportResult helper 命名已直接表达分页与完整性语义，无需额外注释。
2. 正负例覆盖：unit 覆盖 records/next_page_token/truncated/checksum 四字段冻结、partial page/final page 正例，以及 truncated/token 失配负例。
3. 测试发现性：沿用 AUD-TODO-004 已注册的 AuditExportFilterTest，无需新增测试目标即可稳定命中。
4. TODO 证据回写：已完成任务状态、交付物和验收结果回写。
5. 提交隔离：本轮只冻结 ExportResult 和同一测试文件，不提前切换 IAuditLogger/AuditService 的旧导出结果类型。

### 12.6 AUD-TODO-006

选中任务：

1. 任务 ID：AUD-TODO-006。
2. 可执行性依据：AUD-TODO-001 至 005 已冻结 AuditEvent、AuditContext、AuditWriteOutcome、ExportQuery、ExportResult；6.6 已明确 IAuditLogger 仅暴露 write_audit/export_audit 两个方法，当前不存在依赖未满足的 blocker。

研究学习：

1. 本地证据：audit 设计 6.6 明确 `write_audit(const AuditEvent&, const AuditContext&) -> AuditWriteOutcome` 与 `export_audit(const ExportQuery&) -> ExportResult`；后置条件要求成功或失败都具备可观测证据，但接口层不得暴露 sink/线程池等实现细节。
2. 外部参考：OWASP Logging Cheat Sheet 强调安全事件日志必须保留 when/where/who/what 及结果状态，并且审计与普通日志通常应分离；OpenTelemetry Logs Data Model 强调频繁出现且语义稳定的字段应提升为类型化顶层字段，而不是继续保留模糊占位属性。

D 结论：

1. Design -> Build 映射：将 `infra/include/audit/IAuditLogger.h` 从旧的 `AuditExportFilter/AuditWriteResult/AuditExportResult` 占位接口，收敛为直接消费 `AuditEvent/AuditContext/ExportQuery`、返回 `AuditWriteOutcome/ExportResult` 的 6.6 签名；保留 `audit` 命名空间，仅冻结接口，不引入 retention/health/fallback 控制方法。
2. Build 三件套：
	- 代码目标：切换 IAuditLogger、AuditService 和直接调用方到新签名，并把 ExportQuery/ExportResult 的最小语义接到现有 AuditService 骨架上。
	- 测试目标：将 unit 名称收敛为 AuditInterfaceCompileTest，覆盖接口精确签名、正例写入/导出和负例写入失败；扩展 AuditBoundaryContractTest 验证 IAuditLogger 仅依赖冻结对象，不再接受 `opaque_selector` 旧占位输入；补跑 AuditService/ProfileTelemetry 兼容回归。
	- 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_profile_observability_integration_test && ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest|AuditBoundaryContractTest" && ctest --test-dir build-ci -R "AuditInterfaceCompileTest|AuditServiceFallbackTest|AuditBoundaryContractTest|AuditLoggerInterfaceBoundaryContractTest|AuditServiceBoundaryContractTest|ProfileObservabilityIntegrationTest" --output-on-failure。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. infra/include/audit/IAuditLogger.h：冻结为 6.6 新签名，只暴露 write_audit/export_audit 两个接口方法。
2. infra/include/audit/AuditService.h、infra/src/audit/AuditService.cpp：同步到新接口，最小接入 AuditContext 与 ExportQuery/ExportResult，不提前引入 exporter/fallback 控制接口扩张。
3. tests/unit/infra/AuditLoggerInterfaceTest.cpp、tests/unit/infra/CMakeLists.txt：将 unit 名称收敛为 AuditInterfaceCompileTest，并覆盖新签名的正负例。
4. tests/contract/smoke/AuditBoundaryContractTest.cpp、tests/contract/smoke/AuditLoggerInterfaceBoundaryContractTest.cpp、tests/contract/smoke/AuditServiceBoundaryContractTest.cpp：补齐 IAuditLogger 的边界静态断言和 AuditService 新边界回归。
5. profiles/include/ProfileTelemetryAdapter.h、profiles/src/ProfileTelemetryAdapter.cpp、tests/integration/profiles/ProfileObservabilityIntegrationTest.cpp：完成直接调用方兼容切换，避免 profiles 继续绑定旧占位接口语义。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_profile_observability_integration_test：通过。
3. ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest|AuditBoundaryContractTest"：通过，发现 2 个测试。
4. ctest --test-dir build-ci -R "AuditInterfaceCompileTest|AuditServiceFallbackTest|AuditBoundaryContractTest|AuditLoggerInterfaceBoundaryContractTest|AuditServiceBoundaryContractTest|ProfileObservabilityIntegrationTest" --output-on-failure：通过，6/6 tests passed。

Build 合规复核：

1. 代码注释：本轮以类型命名和 helper 语义表达接口约束，无需新增注释；未引入实现细节注释噪音。
2. 正负例覆盖：AuditInterfaceCompileTest 覆盖新签名正例和 event/context 负例；AuditBoundaryContractTest/AuditLoggerInterfaceBoundaryContractTest 覆盖边界静态断言；AuditServiceFallbackTest 与 ProfileObservabilityIntegrationTest 作为直接兼容回归补充。
3. 测试发现性：新增 ctest 名称 AuditInterfaceCompileTest 已通过 `ctest -N -R` 命中；contract 目标 AuditBoundaryContractTest 保持可发现。
4. TODO 证据回写：已完成任务状态、交付物和验收结果回写。
5. 提交隔离：本轮只做 IAuditLogger 接口冻结及直接兼容修复，不提前实现 AuditErrors、AuditValidator 或 AuditExporter 细粒度逻辑。
