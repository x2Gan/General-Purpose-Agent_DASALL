# DASALL infrastructure 子系统 audit 组件专项 TODO

最近更新时间：2026-04-03  
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
| infra/CMakeLists.txt | 已接入 core/audit/plugin/tracing 等真实源码 | audit 最小骨架已接入构建，后续缺口转为存储/导出策略实现 |
| infra/include/ | 已形成“根目录共享契约 + 组件目录公共接口”布局，audit/ 子目录已落盘接口与对象 | audit public headers 已冻结，后续差距集中在存储/导出策略 |
| infra/src/ | 已接入 InfraServiceFacade、InfraErrorCode、audit/、plugin/、tracing/ 等真实源码目录 | audit 最小实现骨架已存在，后续需继续补齐策略与桥接 |
| tests/CMakeLists.txt | 已接入 mocks/unit/contract/integration，且提供 dasall_integration_tests 聚合入口 | audit 集成测试已可被顶层发现，但具体用例仍需随组件任务落盘 |
| tests/unit/CMakeLists.txt | 已接入 infra 子目录 | audit unit 发现性已建立，后续只需补具体用例 |
| tests/contract/CMakeLists.txt | 已有 centralized registration 机制 | 可承载 audit contracts 边界测试 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：可直接生成 L3/L2 混合专项 TODO；当前最小可执行粒度为接口/数据结构级，局部可细化到单错误路径或单链路骨架任务。

证据：

1. 已有明确核心接口清单：IAuditLogger、IAuditRetention、IAuditHealthProbe。
2. 已有明确核心对象字段：AuditEvent、AuditContext、ExportQuery、ExportResult、AuditWriteOutcome、AuditArchiveAction、AuditCleanupEvidence、RetentionOutcome。
3. 已有主流程与异常流程：主写链路 6 步、异常分类 3 类、恢复动作 4 类。
4. 已有错误码域：INF_E_AUDIT_INVALID_EVENT、INF_E_AUDIT_WRITE_FAIL、INF_E_AUDIT_FALLBACK_FAIL、INF_E_AUDIT_EXPORT_DENIED、INF_E_AUDIT_EXPORT_FAIL、INF_E_AUDIT_RETENTION_FAIL。
5. 已有落盘路径与测试出口：infra/include/audit、infra/src/audit、tests/unit/infra/audit、tests/contract/infra、tests/integration/infra，以及 AuditTypesTest、AuditInterfaceCompileTest、AuditServiceFallbackTest、AuditExportFilterTest、AuditBoundaryContractTest、InfraErrorCodeMappingContractTest、InfraAuditHealthIntegrationTest。
6. 仍有证据缺口：`IAuditRetention` public header 与 compile tests 尚未落盘；RetentionOutcome、归档/清理动作对象与自动清理证据语义已于 2026-04-03 冻结，后续进入接口落盘轮。

### 4.2 粒度可行性评估表（Step 2：详细设计可执行性扫描输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| AuditEvent | audit 设计 6.5 | L3 | 字段、必填约束、contracts 对齐关系明确 | side_effects 子结构细项未展开 | 直接拆数据结构冻结任务 |
| AuditContext | audit 设计 6.5 | L3 | 字段与 unknown 兜底语义明确 | 字段类型别名未成文 | 直接拆数据结构冻结任务 |
| AuditWriteOutcome | audit 设计 6.5/6.6 | L3 | accepted/persisted/fallback_used/error_code 四字段明确 | error_code 精细映射矩阵未成文 | 直接拆数据结构冻结任务 |
| ExportQuery | audit 设计 6.5 | L3 | 字段、时间窗必填、target/outcome 扩展规则与分页稳定约束明确 | 无 | 已完成数据结构冻结任务 |
| ExportResult | audit 设计 6.5 | L3 | records/next_page_token/truncated/checksum 字段明确 | records 元素序列化形态未成文 | 直接拆数据结构冻结任务 |
| IAuditLogger | audit 设计 6.6 | L3 | write_audit/export_audit 方法名、输入输出、后置条件明确 | 无 | 直接拆接口冻结任务 |
| IAuditRetention | audit 设计 6.6 | L2 | `apply_retention(now_ts)` 签名、RetentionOutcome/AuditArchiveAction/AuditCleanupEvidence 字段与 cleanup trace 规则已冻结 | public header 尚未落盘 | 直接拆接口冻结任务 |
| IAuditHealthProbe | audit 设计 6.6 | L3 | public header、状态对象与 `evaluate() const` 签名已落盘 | 后续仅需与 metrics bridge 场景保持状态映射一致 | 已完成接口冻结任务 |
| AuditValidator | audit 设计 6.2/6.3/6.7/6.8 | L2 | 输入输出、必填校验、非法输入语义明确 | 函数签名与校验结果对象未成文 | 直接拆字段校验骨架任务 |
| AuditPipeline | audit 设计 6.2/6.3/6.7/6.8 | L2 | append-only 主写链路与失败动作明确 | 存储抽象接口未冻结 | 直接拆主写骨架任务 |
| AuditFallbackPipeline | audit 设计 6.2/6.3/6.8 | L2 | ringbuffer/file 降级职责与 degraded 语义明确 | fallback 存储抽象接口未冻结 | 直接拆降级骨架任务 |
| AuditServiceFacade | audit 设计 6.2/6.3/6.4/6.7 | L2 | 生命周期管理、统一错误映射、调用关系明确 | init/start 方法签名未成文 | 直接拆入口骨架任务 |
| AuditExporter | audit 设计 6.2/6.3/6.5；11.1 | L3 | internal exporter、稳定 resume token、AuditEvent-only 导出边界与 unit/contract tests 已落盘 | 无 | 转入 retention 解阻轮 |
| AuditRetentionManager | audit 设计 6.2/6.3/6.6；11.1 | L1 | retention 输出对象、archive action 与 cleanup trace 规则已冻结 | 真实 manager 调度与存储动作仍未落盘 | 先落接口头文件，再进 manager 设计/实现 |
| AuditMetricsBridge | audit 设计 6.2/6.3/6.10；11.1 | L3 | internal bridge、七指标注册、真实 integration 用例与 `integration;audit` discoverability 已落盘 | 无 | 进入 quality gate 证据收口 |
| AuditHealthProbe 组件 | audit 设计 6.2/6.3/6.10；11.1 | L3 | public interface、真实 metrics bridge 协同与 `integration;audit` discoverability 已落盘 | 无 | 进入 quality gate 证据收口 |
| tests/integration/infra | audit 设计 8.1/9.1；tests 现状 | L3 | `tests/integration/infra/audit/` 子目录、顶层 target 聚合与 `integration;audit` 标签已收口 | 无 | 进入 quality gate 证据收口 |

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
| AuditExporter 导出与脱敏 | audit 设计 6.2/6.3/6.5；11.1 | 流程/测试 | AUD-TODO-012（AUD-BLK-001 已于 2026-04-03 解阻） | v1 导出过滤/分页/脱敏骨架已落盘，可直接转向 retention 解阻 |
| IAuditRetention 与 retention 管理 | audit 设计 6.2/6.6；11.1 | 接口/流程 | AUD-TODO-013（AUD-BLK-002 已于 2026-04-03 解阻） | RetentionOutcome、AuditArchiveAction 与 AuditCleanupEvidence 已冻结，可直接推进接口头文件 |
| IAuditHealthProbe 与健康状态 | audit 设计 6.2/6.6；11.1 | 接口/适配器 | AUD-TODO-014（AUD-BLK-003 已于 2026-04-03 解阻） | AuditHealthStatus 三态与最近失败原因字段已冻结，可直接推进接口落盘 |
| AuditMetricsBridge 指标桥接 | audit 设计 6.2/6.10；11.1 | 适配器/集成 | AUD-TODO-015（AUD-BLK-004 已于 2026-04-03 解阻） | meter scope、七指标对象表、五元标签白名单与 non-recursive failure 语义已冻结，可直接推进桥接骨架 |
| audit 构建接线与测试发现性 | audit 设计 7、8.1、9.1；代码现状 | 测试/门禁 | AUD-TODO-016、AUD-TODO-017、AUD-TODO-018 | 构建、unit/contract 可先做，integration 用例待组件后续落盘 |
| 质量门与交付证据回写 | audit 设计 9.2、11.1 | 文档/交付证据 | AUD-TODO-019 | 对 gate、阻塞变化与回退证据做收口 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | AUD-TODO-006、AUD-TODO-013、AUD-TODO-014 |
| 数据结构定义类任务 | 是 | AUD-TODO-001~005 |
| 生命周期与初始化类任务 | 是 | AUD-TODO-011 |
| 适配器/桥接类任务 | 是 | AUD-TODO-014、AUD-TODO-015 |
| 异常与错误处理类任务 | 是 | AUD-TODO-007、AUD-TODO-009、AUD-TODO-010 |
| 配置与 Profile 裁剪类任务 | 是 | AUD-TODO-012、AUD-TODO-013、AUD-TODO-014 |
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
| AUD-TODO-007 | Done | 定义 AuditErrors 错误码域 | audit 设计 6.6/6.8；编码规范 3.6 | 6.6 错误语义；6.8 异常恢复 | L3 | infra/include/audit/AuditErrors.h | INF_E_AUDIT_INVALID_EVENT、INF_E_AUDIT_WRITE_FAIL、INF_E_AUDIT_FALLBACK_FAIL、INF_E_AUDIT_EXPORT_DENIED、INF_E_AUDIT_EXPORT_FAIL、INF_E_AUDIT_RETENTION_FAIL | contract：InfraErrorCodeMappingContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R InfraErrorCodeMappingContractTest --output-on-failure | AUD-TODO-003 | 无 | 无 | AuditErrors.h、映射测试 | 仅当 6 个错误码均可追溯到设计条目，且 contract 测试能阻止漂移时完成 |
| AUD-TODO-008 | Done | 实现 AuditValidator 字段校验骨架 | audit 设计 6.2/6.3/6.7/6.8 | 6.2 AuditValidator；6.3 输入输出；6.8 输入异常 | L2 | infra/src/audit/AuditValidator.cpp | AuditValidator（字段完整性与边界校验） | unit：AuditTypesTest；contract：AuditBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest" --output-on-failure | AUD-TODO-001、AUD-TODO-002、AUD-TODO-007 | 无 | 无 | AuditValidator.cpp、校验证据 | 仅当必填字段缺失、越权字段、非法时间窗三类输入异常都能返回可判定失败时完成 |
| AUD-TODO-009 | Done | 实现 AuditPipeline 主写骨架 | audit 设计 6.2/6.3/6.7/6.8 | 6.2 AuditPipeline；6.7 正常路径第 3 步；6.8 主写失败 | L2 | infra/src/audit/AuditPipeline.cpp | AuditPipeline（append-only 主写链路） | unit：AuditServiceFallbackTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R AuditServiceFallbackTest --output-on-failure | AUD-TODO-008 | 无 | 无 | AuditPipeline.cpp、主写测试 | 仅当验证通过事件能进入 append-only 主写路径，且主写失败可被上层捕获时完成 |
| AUD-TODO-010 | Done | 实现 AuditFallbackPipeline 降级骨架 | audit 设计 6.2/6.3/6.8 | 6.2 AuditFallbackPipeline；6.8 恢复动作 1/2 | L2 | infra/src/audit/AuditFallbackPipeline.cpp | AuditFallbackPipeline（ringbuffer/file 降级链路） | unit：AuditServiceFallbackTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R AuditServiceFallbackTest --output-on-failure | AUD-TODO-007、AUD-TODO-009 | 无 | 无 | AuditFallbackPipeline.cpp、降级测试 | 仅当主写失败可触发 fallback_used=true，且 fallback 失败返回 INF_E_AUDIT_FALLBACK_FAIL 时完成 |
| AUD-TODO-011 | Done | 实现 AuditServiceFacade 入口骨架 | audit 设计 6.2/6.3/6.4/6.7 | 6.2 AuditServiceFacade；6.4 依赖关系；6.7 主流程 | L2 | infra/src/audit/AuditService.cpp | AuditServiceFacade（审计入口、生命周期管理、统一错误映射） | unit：AuditServiceFallbackTest；contract：InfraErrorCodeMappingContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditServiceFallbackTest|InfraErrorCodeMappingContractTest" --output-on-failure | AUD-TODO-008、AUD-TODO-009、AUD-TODO-010 | 无 | 无 | AuditService.cpp、主链路测试 | 仅当 write_audit 主链路可串起 validator/pipeline/fallback，且返回结果可二值判定时完成 |
| AUD-TODO-012 | Done | 实现 AuditExporter 导出与脱敏骨架 | audit 设计 6.2/6.3/6.5；11.1 | 6.2 AuditExporter；6.3 导出语义；6.5.1 导出过滤与边界语义 | L2 | infra/src/audit/AuditExporter.cpp、infra/src/audit/AuditExporter.h、infra/src/audit/AuditService.cpp | AuditExporter（过滤、分页、脱敏） | unit：AuditExportFilterTest；contract：AuditBoundaryContractTest；regression：AuditServiceFallbackTest | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_audit_export_filter_unit_test dasall_audit_service_fallback_unit_test dasall_contract_audit_event_boundary_test && ctest --test-dir build-ci -N -R "AuditExportFilterTest|AuditBoundaryContractTest|AuditServiceFallbackTest" && ctest --test-dir build-ci --output-on-failure -R "AuditExportFilterTest|AuditBoundaryContractTest|AuditServiceFallbackTest" && cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test && ctest --test-dir build-ci -N -L audit && ctest --test-dir build-ci --output-on-failure -L audit | AUD-TODO-004、AUD-TODO-005、AUD-TODO-006 | 无 | 无 | AuditExporter.cpp、AuditExportFilterTest、AuditBoundaryContractTest、交付件 | 仅当 exporter 从 AuditService 内联筛选中收敛为独立 internal 组件，并按已冻结的窗口+actor+action 主过滤、target/outcome 扩展规则、稳定分页与 AuditEvent-only 导出边界通过 unit/contract/audit gate 验证时完成 |
| AUD-TODO-013 | Not Started | 定义 IAuditRetention 接口与 RetentionOutcome 对象 | audit 设计 6.6；11.1 | 6.6 IAuditRetention；6.6.2 retention 输出对象与 cleanup 证据语义 | L1 | infra/include/audit/IAuditRetention.h | IAuditRetention::apply_retention；RetentionOutcome；AuditArchiveAction；AuditCleanupEvidence | unit：AuditInterfaceCompileTest；contract：InfraErrorCodeMappingContractTest | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test dasall_contract_infra_error_code_boundary_test && ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest|InfraErrorCodeMappingContractTest" && ctest --test-dir build-ci --output-on-failure -R "AuditInterfaceCompileTest|InfraErrorCodeMappingContractTest" | AUD-TODO-007 | 无（2026-04-03 已由 AUD-BLK-002 通过 RetentionOutcome、AuditArchiveAction、AuditCleanupEvidence 与 cleanup trace 规则冻结解阻） | 无 | IAuditRetention.h、接口编译测试或交付件 | 仅当 retention 接口按已冻结的 completed/error_code 二值判定、archive action 与 cleanup evidence 规则落盘，并通过 unit/contract 验证时完成 |
| AUD-TODO-014 | Done | 定义 IAuditHealthProbe 接口与 AuditHealthStatus 对象 | audit 设计 6.6；11.1 | 6.6 IAuditHealthProbe；6.3/6.10 健康状态语义 | L1 | infra/include/audit/IAuditHealthProbe.h | IAuditHealthProbe::evaluate；AuditHealthStatus | unit：AuditInterfaceCompileTest；integration：InfraAuditHealthIntegrationTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest" --output-on-failure | AUD-TODO-007 | 无（2026-04-03 已由 AUD-BLK-003 通过 AuditHealthStatus 三态与最近失败原因字段冻结解阻） | 无 | IAuditHealthProbe.h、接口编译测试、InfraAuditHealthIntegrationTest | 仅当 AuditHealthStatus 字段与状态机语义冻结后，状态才可从 Blocked 转为 Not Started |
| AUD-TODO-015 | Done | 实现 AuditMetricsBridge 指标桥接骨架 | audit 设计 6.2/6.3/6.10；11.1 | 6.2 AuditMetricsBridge；6.10 指标清单；11.1 桥接阻塞 | L1 | infra/src/audit/AuditMetricsBridge.cpp、infra/src/audit/AuditMetricsBridge.h | AuditMetricsBridge（audit_write_total 等指标桥接） | integration：InfraAuditHealthIntegrationTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R InfraAuditHealthIntegrationTest --output-on-failure | AUD-TODO-011 | 无（2026-04-03 已由 AUD-BLK-004 通过 audit meter scope、七指标对象表、五元标签白名单与 non-recursive failure 语义冻结解阻） | 无 | AuditMetricsBridge.cpp、InfraAuditHealthIntegrationTest、交付件 | 仅当 bridge 通过 `IMetricsProvider/IMeter` 注册七指标对象表、保留 degraded/no-op 语义，且 integration 用例验证 metrics bridge degraded 不反噬 audit 主结果时完成 |
| AUD-TODO-016 | Done | 注册 audit 源码到 infra CMake | audit 设计 7、8.1；代码现状 | 7 Design -> Build 映射；8.1 文件落盘建议 | L2 | infra/CMakeLists.txt | audit include/src 文件接线 | build：dasall_infra 可编译；unit：AuditInterfaceCompileTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R AuditInterfaceCompileTest --output-on-failure | AUD-TODO-001 至 AUD-TODO-011 | 无 | 无 | CMake 改动、构建记录 | 仅当 placeholder 不再是唯一源码入口且 audit 文件进入 dasall_infra 构建图时完成 |
| AUD-TODO-017 | Done | 注册 audit 的 unit 与 contract 测试入口 | audit 设计 8.1、9.1；编码规范 3.7；tests 现状 | 8.1 路径建议；9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt、tests/unit/infra/audit/、tests/contract/CMakeLists.txt、tests/contract/infra/ | unit：AuditTypesTest、AuditInterfaceCompileTest、AuditServiceFallbackTest、AuditExportFilterTest；contract：AuditBoundaryContractTest、InfraErrorCodeMappingContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | AUD-TODO-016 | 无 | 无 | 测试源文件、注册入口、ctest 发现性证据 | 仅当新增 audit unit/contract 测试可被 ctest -N 发现并执行时完成 |
| AUD-TODO-018 | Done | 注册 audit integration 测试入口 | audit 设计 8.1、9.1；tests 现状；11.1 | 8.1 tests/integration/infra；9.1 Integration；11.1 integration 阻塞 | L0 | tests/integration/infra/audit/、tests/integration/CMakeLists.txt | integration：InfraAuditHealthIntegrationTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -N && ctest --test-dir build-ci -R InfraAuditHealthIntegrationTest --output-on-failure | AUD-TODO-014、AUD-TODO-015、AUD-TODO-016 | 无（2026-03-30 已由 INF-BLK-06 integration 顶层拓扑校准解阻） | 无 | audit integration 注册改动、ctest 发现性证据 | 仅当 `InfraAuditHealthIntegrationTest` 收口到 `tests/integration/infra/audit/` 子目录、进入顶层 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS` 聚合，并可被 `ctest -N -L audit -R InfraAuditHealthIntegrationTest` 发现时完成 |
| AUD-TODO-019 | Done | 回写 audit 质量门与交付证据 | audit 设计 9.2、11.1 | 9.2 Gate；11.1 阻塞与回退 | L2 | docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md、docs/worklog/DASALL_开发执行记录.md | process test：门禁结论、阻塞变化、回退证据回写 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test && ctest --test-dir build-ci -N -L audit && ctest --test-dir build-ci --output-on-failure -L audit | AUD-TODO-018 | 无 | 无 | 更新后的 TODO 文档证据段、交付件、worklog | 仅当每个 gate 都具备 PASS/BLOCKED 结论、命令证据和阻塞变化说明时完成 |

### 6.2 当前 Blocked 任务索引

| 任务 ID | 对应阻塞项 |
|---|---|
| 无 | 无 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 对象冻结 | AUD-TODO-001~005 | 可并行 | 先冻结输入输出对象与导出对象，避免实现期反复改字段 |
| B 接口与错误语义冻结 | AUD-TODO-006、AUD-TODO-007 | 可并行 | 稳定 IAuditLogger 与错误码域，阻断直接绑实现 |
| C 主链路骨架 | AUD-TODO-008、AUD-TODO-009、AUD-TODO-010、AUD-TODO-011 | 串行 | validator -> pipeline -> fallback -> facade |
| D 构建与测试接线 | AUD-TODO-016、AUD-TODO-017 | 可并行 | 代码入图与测试发现性都依赖 A-C，但互不阻塞 |
| E 受阻能力补齐 | AUD-TODO-012、AUD-TODO-013、AUD-TODO-014、AUD-TODO-015、AUD-TODO-018 | 串行 | 导出/retention/health/metrics/integration 已按 blocker 顺序推进，当前只剩 `AUD-TODO-013` 接口落盘 |
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
| AUD-GATE-08 | integration 准入门 | 推进 AUD-TODO-018 前 | tests 顶层已完成 integration 接线并定义标签规范，且 audit 组件用例已落盘 | 未通过前补齐 audit integration 用例与注册 |
| AUD-GATE-09 | breaking 评审门 | 任意公共对象或错误映射变更前 | 已明确 breaking 风险、迁移窗口与回退方案 | 未评审不得推进 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| AUD-BLK-001 | 已解阻（2026-04-03）：audit 详细设计 6.5/6.5.1 已冻结 ExportQuery 的时间窗+actor+action 主过滤、target/outcome 扩展规则、稳定 resume token 边界与 AuditEvent-only 导出面；后续 AuditExporter 只需按该模型落盘 | AUD-TODO-012 | 无；后续仅需保持 ExportQuery/ExportResult、AuditExporter 实现与 AuditBoundaryContractTest 的过滤/边界语义一致 | 证据回链到 docs/architecture/DASALL_infra_audit模块详细设计.md 6.5/6.5.1 与 docs/todos/infrastructure/deliverables/AUD-BLK-001-ExportQuery过滤语义设计收敛.md | 若 ExportQuery 回退为 pattern/wildcard 查询、让 page_token 脱离过滤元组复用，或把 AuditContext 直接并入 ExportResult，则重新转为 Blocked |
| AUD-BLK-002 | 已解阻（2026-04-03）：audit 详细设计 6.5/6.6.2 已冻结 `RetentionOutcome` 的 completed/error_code 二值判定、`AuditArchiveAction` 的结构化 archive 引用与 checksum、`AuditCleanupEvidence` 的 Manual/Scheduled trigger 与 archive-ref 绑定；后续 `IAuditRetention` 只需按该模型落盘 | AUD-TODO-013 | 无；后续仅需保持 `IAuditRetention`、RetentionOutcome 与 `InfraErrorCodeMappingContractTest` 的错误码语义一致 | 证据回链到 docs/architecture/DASALL_infra_audit模块详细设计.md 6.5/6.6.2 与 docs/todos/infrastructure/deliverables/AUD-BLK-002-RetentionOutcome设计收敛.md | 若 retention 重新允许无证据 hard-delete、把 archive 物理路径暴露到公共对象，或让 cleanup_evidence 脱离 archive_ref，则重新转为 Blocked |
| AUD-BLK-003 | 已解阻（2026-04-03）：audit 详细设计 6.5/6.6.1 已冻结 `AuditHealthStatus` 的 Ready/Degraded/Unavailable 三态、最近失败原因字段与只读 evaluate 语义；后续 `IAuditHealthProbe` 只需按该对象边界落盘 | AUD-TODO-014 | 无；后续仅需保持 `IAuditHealthProbe`、`AuditHealthStatus` 与 integration 用例状态映射一致 | 证据回链到 docs/architecture/DASALL_infra_audit模块详细设计.md 6.5/6.6.1 与 docs/todos/infrastructure/deliverables/AUD-BLK-003-AuditHealthProbe设计收敛.md | 若 audit 重新引入自由文本 failure reason、把私有状态对象直接泄露到 health 公共接口，或回退三态定义，则重新转为 Blocked |
| AUD-BLK-004 | 已解阻（2026-04-03）：metrics 详细设计 6.6.2/6.8.2 已冻结 audit bridge 的 IMetricsProvider/IMeter 接入协议、七指标对象表、五元标签白名单与 non-recursive failure semantics；audit 详细设计 6.10.1 已补齐 bridge degraded 与 AuditHealthStatus 的对齐规则 | AUD-TODO-015 | 无；后续仅需保持 metrics/audit 设计、bridge 实现与 integration 用例的标签/失败语义同步 | 证据回链到 docs/architecture/DASALL_infra_metrics模块详细设计.md 6.6.2/6.8.2、docs/architecture/DASALL_infra_audit模块详细设计.md 6.10.1 与 docs/todos/infrastructure/deliverables/AUD-BLK-004-AuditMetricsBridge设计收敛.md | 若 IMeter、MetricLabels、MetricsErrors 或 audit bridge 的 degraded 语义回退，则重新转为 Blocked |
| AUD-BLK-005 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；audit integration 是否可执行改由组件自身落盘负责 | AUD-TODO-018 | 无；后续仅需按组件落盘 integration 用例 | 证据回链到 infra 专项 TODO 的 INF-BLK-06 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt | 若 tests 顶层 integration 接线或聚合依赖回退，则重新转为 Blocked |

## 9. 验收与质量门

### 9.1 验收命令基线

| 用途 | 命令 |
|---|---|
| 配置构建目录 | cmake -S . -B build-ci -G "Unix Makefiles" |
| 构建 infra | cmake --build build-ci --target dasall_infra |
| 执行 unit 套件 | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit |
| 执行 contract 套件 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract |
| 执行 audit 专项 gate | cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test && ctest --test-dir build-ci -N -L audit && ctest --test-dir build-ci --output-on-failure -L audit |
| 检查测试发现性 | ctest --test-dir build-ci -N |

说明：

1. 当前 `build-ci` 已锁定为 `Unix Makefiles`，本轮沿用现有生成器执行所有 gate 验证，不再切回 Ninja。
2. `AUD-TODO-018` 完成后，integration 已进入 audit 专项 gate 基线，因此 `ctest -L audit` 现同时覆盖 4 个 unit、4 个 contract 与 1 个 integration 测试。
3. 每个可执行任务至少包含 1 条构建命令与 1 条测试命令；Block 任务保留解阻后的验收命令。

### 9.2 质量门逐项回答

1. 是否给出 Design -> TODO 映射，而非仅任务标题：是。
2. 是否明确当前最细可达到粒度：是，L3/L2 混合。
3. 是否所有任务具备代码目标 + 测试目标 + 验收命令：是。
4. 是否所有 Blocked 项具备证据与解阻条件：是。
5. 是否所有任务具备可二值判定完成标准：是。
6. 是否避免跨子系统范围扩张：是。
7. 若要求函数/数据结构级，是否真正落到对象与单链路目标：是。

### 9.3 Gate 执行结论（AUD-TODO-019）

| Gate ID | 当前结论 | 证据 | 备注 |
|---|---|---|---|
| AUD-GATE-01 | PASS | `AuditTypesTest`、`AuditInterfaceCompileTest`、`InfraErrorCodeMappingContractTest` 已纳入 `ctest -L audit`，并随本轮 9/9 通过 | 接口/对象冻结闭环已完成 |
| AUD-GATE-02 | PASS | `AuditServiceFallbackTest` 已纳入 `ctest -L audit`，并随本轮 9/9 通过 | 主写失败 -> fallback 闭环已可验证 |
| AUD-GATE-03 | PASS | `AuditBoundaryContractTest` 与 `InfraErrorCodeMappingContractTest` 随本轮 9/9 通过 | contracts 边界与错误码映射稳定 |
| AUD-GATE-04 | PASS | `ctest --test-dir build-ci -N -L audit` 发现 9 个测试 | discoverability 已覆盖 unit/contract/integration |
| AUD-GATE-05 | PASS | `AUD-BLK-001` 已解阻，`AUD-TODO-012` 已完成并随当前 audit gate 9/9 通过 | 导出 filter 与边界语义已稳定 |
| AUD-GATE-06 | PASS | `AUD-BLK-002` 已解阻，audit 设计 6.5/6.6.2 与 blocker 交付件已冻结 retention 输出对象和 cleanup trace 规则 | `AUD-TODO-013` 已恢复为可执行状态 |
| AUD-GATE-07 | PASS | `AUD-TODO-014`、`AUD-TODO-015` 完成，`InfraAuditHealthIntegrationTest` 随本轮 9/9 通过 | 健康状态与 metrics bridge 协同已稳定 |
| AUD-GATE-08 | PASS | `AUD-TODO-018` 完成，`InfraAuditHealthIntegrationTest` 可被 `ctest -N -L audit` 命中 | integration 子目录、标签与聚合已收口 |
| AUD-GATE-09 | PASS | 当前轮未新增公共 breaking 变更；无新增迁移窗口需求 | 维持未触发状态 |

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 回退策略 |
|---|---|---|---|---|
| 审计与普通日志混流 | High | 复用 logging 单管线或共享 sink 而无隔离 | AuditBoundaryContractTest 失败或存储路径未分离 | 回退到独立 AuditPipeline 与独立存储目录 |
| 审计失败被吞没 | High | 主写失败未触发 fallback 或无错误码/告警 | AuditServiceFallbackTest 失败；错误计数无变化 | 立即回退到 AUD-TODO-009/AUD-TODO-010，并补错误码与观测出口 |
| contracts 语义漂移 | High | 在 AuditEvent/AuditContext 中引入实现字段或重定义共享语义 | AuditBoundaryContractTest 或 InfraErrorCodeMappingContractTest 失败 | 回退对象字段变更并走 breaking 评审门 |
| 导出越权或泄露敏感字段 | High | 未冻结 filter/redaction 语义即推进导出实现 | AuditExportFilterTest 失败；审计导出包含敏感字段 | 回退导出功能到最小时间窗导出或关闭导出实现 |
| retention 清理导致证据丢失 | High | 自动清理先于归档/清理痕迹规则冻结 | 审计样本不可回放或无清理证据 | 暂停自动清理，仅保留手动清理和归档标记 |
| 健康/指标桥接误宣称生产可用 | Medium | 在桥接实现与 integration 语义未落盘前对外暴露稳定承诺 | InfraAuditHealthIntegrationTest 不稳定或无法发现 | 回退到 audit 内部本地状态与计数，不暴露对外桥接 |

## 11. 可行性结论

### 11.1 结论

可直接生成并执行接口/数据结构级专项 TODO，并对主写、降级、错误路径输出组件骨架级任务；当前不宜全面进入函数实现级。

### 11.2 原因

1. 已有明确核心接口清单与方法语义，足以冻结 IAuditLogger 与相关对象。
2. 已有核心对象字段、主流程、异常流程和错误码域，足以拆出 AuditEvent、AuditContext、AuditWriteOutcome、ExportQuery、ExportResult 与主写/降级骨架任务。
3. 已有文件落盘建议、测试名称与验收命令基线，足以让任务具备代码目标、测试目标、验收命令三件套。
4. `RetentionOutcome`、`AuditArchiveAction`、`AuditCleanupEvidence` 与 cleanup trace 规则已冻结，`AUD-TODO-013` 进入可直接落盘接口头文件的阶段。
5. AuditExporter skeleton 已在 `AUD-TODO-012` 落盘，并经 `AuditExportFilterTest`、`AuditBoundaryContractTest` 与完整 audit gate 验证；下一步聚焦 `IAuditRetention` 头文件与 compile tests。

### 11.3 当前最小可执行粒度

接口 / 数据结构 / 单链路骨架。

### 11.4 若未达到函数级，还缺哪些设计信息

1. `IAuditRetention` public header 的具体声明与 compile tests。
2. retention manager 的真实调度与存储动作实现。

### 11.5 下一步建议

1. 下一执行入口应切换到 `AUD-TODO-013`，按已冻结的 completed/error_code、archive action 与 cleanup evidence 规则落盘 `IAuditRetention` 头文件。
2. 当前 audit 专项 gate 基线继续复用 `ctest --test-dir build-ci -N -L audit` 与 `ctest --test-dir build-ci --output-on-failure -L audit`，后续 retention 相关测试也必须保持该证据链稳定。

### 12.19 AUD-BLK-002

选中任务：

1. 任务 ID：AUD-BLK-002。
2. 可执行性依据：`AUD-TODO-012` 已完成，audit 当前唯一剩余 blocker 已明确收缩到 retention 输出对象与 cleanup trace 语义；仓库中 `INF_E_AUDIT_RETENTION_FAIL`、`IAuditRetention::apply_retention(now_ts)` 方法名和 `retention.days=30` 默认策略都已存在设计锚点，缺的是可编译对象模型。

研究学习：

1. 本地证据：当前 [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit%E6%A8%A1%E5%9D%97%E8%AF%A6%E7%BB%86%E8%AE%BE%E8%AE%A1.md) 6.2/6.3/6.6 只声明了 `IAuditRetention::apply_retention(now_ts)` 与 retention.days=30，但没有定义 `RetentionOutcome`、archive action 或 cleanup evidence 的最小字段，因此 `AUD-TODO-013` 虽然知道接口名，却无法安全冻结返回对象。
2. 本地证据：当前 [infra/include/audit/AuditErrors.h](infra/include/audit/AuditErrors.h) 已冻结 `INF_E_AUDIT_RETENTION_FAIL`，说明 blocker 不在错误码域，而在“成功/失败怎样二值判定、删除动作如何保留 trace”这一层。
3. 外部参考：OWASP Logging Cheat Sheet 明确把 data export、data deletion 与 disposal of logs 纳入 audit trail / disposal 生命周期，并要求防止未授权删除、对日志访问/修改/删除进行记录；NIST SP 800-92 将 log management 归入 Audit and Accountability 范畴，这支持 DASALL 以结构化 outcome 固定 archive/cleanup 证据，而不是返回裸布尔值。

D 结论：

1. Design -> Build 映射：在 audit 详细设计中补齐 `AuditArchiveAction`、`AuditCleanupEvidence`、`RetentionOutcome` 三个 retention 私有对象，并新增 `6.6.2 RetentionOutcome 与归档/清理证据冻结（AUD-BLK-002）`，作为 [infra/include/audit/IAuditRetention.h](infra/include/audit/IAuditRetention.h) 的直接输入契约。
2. Build 三件套：
	- 代码目标：本轮不落盘代码，仅冻结 `apply_retention(now_ts)` 的最小输入输出协议、`completed/error_code` 二值判定、`archive_ref`/`cleanup_ref`/`detail_ref` 的结构化引用边界。
	- 测试目标：通过文档 traceability 确认 architecture、TODO 与 blocker deliverable 对 `AUD-BLK-002` 的解阻状态和 013 交接约束一致。
	- 验收命令：`rg -n "6\.6\.2 RetentionOutcome 与归档/清理证据冻结|AUD-BLK-002|AUD-TODO-013" docs/architecture/DASALL_infra_audit模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md docs/todos/infrastructure/deliverables/AUD-BLK-002-RetentionOutcome设计收敛.md`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit%E6%A8%A1%E5%9D%97%E8%AF%A6%E7%BB%86%E8%AE%BE%E8%AE%A1.md)：补齐 retention 对象表，并新增 `6.6.2 RetentionOutcome 与归档/清理证据冻结（AUD-BLK-002）`。
2. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)：将 `AUD-BLK-002` 标记为已解阻，把 `AUD-TODO-013` 从 Blocked 迁移到 Not Started，并把下一步切换到接口落盘轮。
3. [docs/todos/infrastructure/deliverables/AUD-BLK-002-RetentionOutcome设计收敛.md](docs/todos/infrastructure/deliverables/AUD-BLK-002-RetentionOutcome%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)：补齐 blocker 根因、外部参考、设计结论与交接约束。

验收结果：

1. `rg -n "6\.6\.2 RetentionOutcome 与归档/清理证据冻结|AUD-BLK-002|AUD-TODO-013" docs/architecture/DASALL_infra_audit模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md docs/todos/infrastructure/deliverables/AUD-BLK-002-RetentionOutcome设计收敛.md`：通过。
2. traceability 结果显示：architecture 的 retention 冻结章节、TODO 的解阻状态与 blocker deliverable 均已可定位回链，`AUD-TODO-013` 已恢复为可执行状态。

Build 合规复核：

1. 代码注释：本轮只做 retention 设计解阻，不落盘代码实现。
2. 正负例覆盖：本轮用文档 traceability 覆盖 blocker 解除与交接条件；真正的编译/接口验证留给 `AUD-TODO-013`。
3. 测试发现性：不新增测试 target，本轮保持 audit gate 基线不变。
4. TODO 证据回写：已完成 blocker 状态、gate 结论与下一步建议回写。
5. 提交隔离：本轮只处理 retention 设计解阻，不提前落盘 `IAuditRetention.h` 或 retention manager 实现。

## 12. 本轮执行记录（2026-03-30）

### 12.1 AUD-TODO-001

选中任务：

1. 任务 ID：AUD-TODO-001。
2. 可执行性依据：无前置依赖；audit 设计 6.5 已冻结 AuditEvent 字段表；当前仓库已有 audit 骨架和测试入口，可在一轮内完成字段收敛、边界测试和兼容修复。

研究学习：

1. 本地证据：docs/architecture/DASALL_infra_audit模块详细设计.md 6.5/6.6 已明确 AuditEvent 字段为 event_id/action/actor/target/outcome/evidence_ref/side_effects/timestamp，且 contracts 边界仅允许引用 ToolResult、RecoveryOutcome、WorkerTask 标识语义。
2. 外部参考：OTel Logs Data Model 强调时间戳与 trace 关联字段应保留为结构化顶层字段；OWASP Logging Cheat Sheet 要求审计字段覆盖 who/what/when/where/outcome 且与普通日志链路隔离。

D 结论：

1. Design -> Build 映射：新增 infra/include/audit/AuditTypes.h 冻结 AuditEvent/AuditEvidenceRef/AuditOutcome/AuditEvidenceKind；AuditEvent 统一由 audit/AuditTypes.h 暴露，不再保留根层兼容 include。
2. Build 三件套：
	- 代码目标：新增 AuditTypes.h，收敛旧 AuditEvent.h，补齐受影响 AuditEvent 构造点并更新 infra/CMakeLists.txt 的 PUBLIC_HEADER。
	- 测试目标：新增 tests/unit/infra/AuditTypesTest.cpp 与 tests/contract/smoke/AuditBoundaryContractTest.cpp；同步 unit/contract 注册名为 AuditTypesTest、AuditBoundaryContractTest。
	- 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest" --output-on-failure；补充 cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "AuditTypesTest|AuditBoundaryContractTest"。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. infra/include/audit/AuditTypes.h：新增 AuditEvent 冻结字段、AuditEvidenceRef 和审计枚举；evidence kind 扩展到 WorkerTask，满足 ADR-008 协同标识引用要求。
2. 受影响消费者与测试统一直接包含 infra/include/audit/AuditTypes.h，不再保留 infra/include/AuditEvent.h 根层入口。
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

### 12.7 AUD-TODO-007

选中任务：

1. 任务 ID：AUD-TODO-007。
2. 可执行性依据：AUD-TODO-003 已冻结 `AuditWriteOutcome.error_code` 为 `optional<contracts::ResultCode>`，audit 设计 6.6/6.8 已明确 6 个私有码名称；当前不存在依赖缺口或 blocker。

研究学习：

1. 本地证据：audit 设计 6.6 给出 `INF_E_AUDIT_INVALID_EVENT / WRITE_FAIL / FALLBACK_FAIL / EXPORT_DENIED / EXPORT_FAIL / RETENTION_FAIL` 六个私有码；6.8 明确 invalid event、fallback fail、export fail 的异常恢复语义和观测要求。
2. 外部参考：OWASP Logging Cheat Sheet 要求应用日志对输入校验失败、访问拒绝、运行时错误和导出/审计事件保持一致分类与可观测性；这与将 audit 私有码压到既有 contracts result categories 的做法一致。

D 结论：

1. Design -> Build 映射：新增 `infra/include/audit/AuditErrors.h`，以 header-only 形式冻结六个 audit 私有码、名字函数和 contracts 映射函数；在现有 `InfraErrorCodeMappingContractTest` 中追加 audit 私有码断言，阻止后续实现期漂移。
2. Build 三件套：
	- 代码目标：新增 AuditErrorCode/AuditErrorMapping 及 `audit_error_code_name/map_audit_error_code`，并把 AuditService 的 write/fallback 失败路径切到新码域。
	- 测试目标：扩展 InfraErrorCodeMappingContractTest 覆盖 6 个 audit 私有码的名字前缀、映射范围和 reason；附加运行 AuditServiceFallbackTest，确认新码域接入不破坏既有 fallback 行为。
	- 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_contract_infra_error_code_boundary_test dasall_audit_service_fallback_unit_test && ctest --test-dir build-ci -R "InfraErrorCodeMappingContractTest|AuditServiceFallbackTest" --output-on-failure。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. infra/include/audit/AuditErrors.h：新增六个 audit 私有码、名字函数与映射函数。
2. infra/CMakeLists.txt：将 AuditErrors.h 接入 infra 公共头导出清单。
3. infra/src/audit/AuditService.cpp：write/fallback 失败路径切换到 `AuditErrorCode::WriteFail / InvalidEvent / FallbackFail`。
4. tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp：新增 audit 私有码映射和命名约束，并让 AuditWriteOutcome 负例直接引用新 audit error mapper。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra dasall_contract_infra_error_code_boundary_test dasall_audit_service_fallback_unit_test：通过。
3. ctest --test-dir build-ci -R "InfraErrorCodeMappingContractTest|AuditServiceFallbackTest" --output-on-failure：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：AuditErrors.h 的枚举与 mapping reason 已足够表达错误语义，无需新增注释。
2. 正负例覆盖：contract 覆盖 6 个私有码的正向映射与命名边界，并复用 AuditWriteOutcome 的 validation/runtime failure 负例；AuditServiceFallbackTest 复核 fallback fail 仍映射到 runtime contracts code。
3. 测试发现性：沿用已收敛的 InfraErrorCodeMappingContractTest 名称，无需新增测试注册即可稳定命中。
4. TODO 证据回写：已完成任务状态、交付物和验收结果回写。
5. 提交隔离：本轮只冻结 audit 私有码域及直接映射使用点，不提前推进 AuditValidator、AuditPipeline 或 retention/export 细节实现。

### 12.8 AUD-TODO-008

选中任务：

1. 任务 ID：AUD-TODO-008。
2. 可执行性依据：AUD-TODO-001、002、007 已完成，`AuditEvent` / `AuditContext` / `AuditErrors` 边界已冻结；当前仓库尚无 `AuditValidator` 实体，不存在额外 blocker。

研究学习：

1. 本地证据：audit 设计 6.2/6.3/6.7/6.8 要求 `AuditValidator` 只负责字段完整性、边界语义和输入拒绝，不承担主写、fallback、恢复裁定或导出执行。
2. 外部参考：OWASP Logging Cheat Sheet 要求输入校验失败保持显式、可观测且不能被静默吞掉；OpenTelemetry Logs Data Model 强调稳定高频字段应保持类型化顶层字段，而不是退回为模糊文本，这支持 validator 直接消费冻结的 `AuditEvent` / `AuditContext` / `ExportQuery`。

D 结论：

1. Design -> Build 映射：新增 internal `infra/src/audit/AuditValidator.h/.cpp`，用统一 `AuditValidationResult` 收敛 write/export 输入校验，并让 `AuditService` 只消费 validator 结果，不再复制字段判断。
2. Build 三件套：
	- 代码目标：落盘 `AuditValidator` internal header/source，把 `AuditService::write_audit()` / `export_audit()` 改为委托 validator，并最小接线 `infra/CMakeLists.txt`。
	- 测试目标：扩展 `AuditTypesTest` 覆盖 validator 正例、缺字段负例、边界漂移负例与非法时间窗负例；保持 `AuditBoundaryContractTest` 与 `AuditServiceFallbackTest` 回归通过。
	- 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_audit_event_unit_test dasall_contract_audit_event_boundary_test dasall_audit_service_fallback_unit_test && ctest --test-dir build-ci -N -R "AuditTypesTest|AuditBoundaryContractTest" && ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest|AuditServiceFallbackTest" --output-on-failure`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. `infra/src/audit/AuditValidator.h`、`infra/src/audit/AuditValidator.cpp`：新增 internal `AuditValidationResult` 与 `AuditValidator`，覆盖 write 输入和 export query 两条校验入口。
2. `infra/src/audit/AuditService.cpp`：移除内联字段判断，改为委托 validator，保持 `AuditService` 只消费统一校验结果。
3. `infra/CMakeLists.txt`：最小新增 `AuditValidator.cpp` 到 `dasall_infra` 构建图，支撑本轮真实编译验证。
4. `tests/unit/infra/AuditTypesTest.cpp`、`tests/unit/infra/CMakeLists.txt`：为既有 `AuditTypesTest` 增补 validator 正负例，并给该 target 添加 `infra/src` include path。
5. `docs/todos/infrastructure/deliverables/AUD-TODO-008-AuditValidator骨架收敛.md`：补齐 D/B 收敛、外部参考、Design -> Build 映射与验收结果。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。由于当前 `build-ci` 已锁定为 Unix Makefiles，本轮沿用现有生成器执行验证。
2. `cmake --build build-ci --target dasall_infra dasall_audit_event_unit_test dasall_contract_audit_event_boundary_test dasall_audit_service_fallback_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R "AuditTypesTest|AuditBoundaryContractTest"`：通过，发现 2 个定向测试。
4. `ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest|AuditServiceFallbackTest" --output-on-failure`：通过，3/3 tests passed。

Build 合规复核：

1. 代码注释：本轮内部类型与方法命名已能直接表达校验职责，无需新增注释噪音。
2. 正负例覆盖：`AuditTypesTest` 新增 validator 正例、缺字段负例、边界漂移负例和非法时间窗负例；`AuditServiceFallbackTest` 作为 service 回归补充。
3. 测试发现性：`AuditTypesTest` 与 `AuditBoundaryContractTest` 已通过 `ctest -N -R` 命中，未引入新的 discoverability 缺口。
4. TODO 证据回写：已完成 008 状态、交付物、验收结果与执行记录回写。
5. 提交隔离：本轮只推进 validator 校验骨架及其最小测试/CMake 支撑，不提前实现 `AuditPipeline`、`AuditFallbackPipeline` 或 `AuditServiceFacade`。

### 12.9 AUD-TODO-009

选中任务：

1. 任务 ID：AUD-TODO-009。
2. 可执行性依据：`AUD-TODO-008` 已完成 validator 骨架，主写 append 仍堆在 `AuditService` 中，且 009 不依赖任何 Blocked 项。

研究学习：

1. 本地证据：audit 设计 6.2/6.3/6.7/6.8 要求 `AuditPipeline` 只接收 validator 已通过的 `AuditEvent`，执行 append-only 主写，并把失败交还上层处理。
2. 外部参考：OWASP Logging Cheat Sheet 明确要求测试日志写入失败、容量耗尽和存储异常等场景，支持把主写 append 路径拆成独立 pipeline，并把容量耗尽保留为结构化失败而不是 service 内隐式分支。

D 结论：

1. Design -> Build 映射：新增 internal `infra/src/audit/AuditPipeline.h/.cpp`，定义 append-only `AuditPipeline` 与 `AuditPipelineWriteResult`；`AuditService` 在 validator 通过后改为委托 pipeline 执行主写 append。
2. Build 三件套：
	- 代码目标：落盘 `AuditPipeline` internal header/source，并最小更新 `AuditService.cpp` 与 `infra/CMakeLists.txt`。
	- 测试目标：扩展 `AuditServiceFallbackTest`，新增主写 append-only 顺序正例，并继续复用 fallback exhaustion 负例。
	- 验收命令：`cmake --build build-ci --target dasall_infra dasall_audit_service_fallback_unit_test && ctest --test-dir build-ci -N -R "AuditServiceFallbackTest" && ctest --test-dir build-ci -R "AuditServiceFallbackTest" --output-on-failure`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. `infra/src/audit/AuditPipeline.h`、`infra/src/audit/AuditPipeline.cpp`：新增 internal `AuditPipelineWriteResult` 与 `AuditPipeline`，收敛 append-only 主写骨架。
2. `infra/src/audit/AuditService.cpp`：主写 append 由 service 直接 vector/capacity 判断切为委托 pipeline。
3. `infra/CMakeLists.txt`：最小新增 `AuditPipeline.cpp` 到 `dasall_infra` 构建图。
4. `tests/unit/infra/AuditServiceFallbackTest.cpp`：新增主写 append-only 顺序正例，继续保留 fallback 回归断言。
5. `docs/todos/infrastructure/deliverables/AUD-TODO-009-AuditPipeline骨架收敛.md`：补齐 D/B 收敛、外部参考、Design -> Build 映射与验收结果。

验收结果：

1. `cmake --build build-ci --target dasall_infra dasall_audit_service_fallback_unit_test`：通过。
2. `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest"`：通过，发现 1 个定向测试。
3. `ctest --test-dir build-ci -R "AuditServiceFallbackTest" --output-on-failure`：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：pipeline/result 命名已能直接表达主写 append-only 语义，无需新增注释。
2. 正负例覆盖：`AuditServiceFallbackTest` 新增主写 append-only 顺序正例，继续复用 fallback exhaustion 负例。
3. 测试发现性：`AuditServiceFallbackTest` 已通过 `ctest -N -R` 命中，未引入新的 discoverability 缺口。
4. TODO 证据回写：已完成 009 状态、交付物、验收结果与执行记录回写。
5. 提交隔离：本轮只拆主写路径，不提前实现 `AuditFallbackPipeline` 或 facade 统一入口。

### 12.10 AUD-TODO-010

选中任务：

1. 任务 ID：AUD-TODO-010。
2. 可执行性依据：`AUD-TODO-009` 已完成主写 `AuditPipeline` 骨架，fallback 仍堆在 `AuditService` 中，且 010 不依赖任何 Blocked 项。

研究学习：

1. 本地证据：audit 设计 6.2/6.3/6.8 要求 `AuditFallbackPipeline` 只承接主写失败后的降级写入，不可静默丢失，失败要维持 `INF_E_AUDIT_FALLBACK_FAIL` 可观测。
2. 外部参考：OWASP Logging Cheat Sheet 要求显式测试日志写入故障、容量耗尽和存储异常场景，这支持把 fallback 容量耗尽单独建模成结构化失败，而不是 service 内的隐式分支。

D 结论：

1. Design -> Build 映射：新增 internal `infra/src/audit/AuditFallbackPipeline.h/.cpp`，定义降级 append-only `AuditFallbackPipeline` 与 `AuditFallbackWriteResult`；`AuditService` 在主写失败后改为委托 fallback pipeline。
2. Build 三件套：
	- 代码目标：落盘 `AuditFallbackPipeline` internal header/source，并最小更新 `AuditService.cpp` 与 `infra/CMakeLists.txt`。
	- 测试目标：扩展 `AuditServiceFallbackTest`，新增 fallback append 顺序正例，并继续复用 fallback exhaustion 负例。
	- 验收命令：`cmake --build build-ci --target dasall_infra dasall_audit_service_fallback_unit_test && ctest --test-dir build-ci -N -R "AuditServiceFallbackTest" && ctest --test-dir build-ci -R "AuditServiceFallbackTest" --output-on-failure`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. `infra/src/audit/AuditFallbackPipeline.h`、`infra/src/audit/AuditFallbackPipeline.cpp`：新增 internal `AuditFallbackWriteResult` 与 `AuditFallbackPipeline`，收敛降级写入骨架。
2. `infra/src/audit/AuditService.cpp`：主写失败后的降级 append 由 service 直接 vector/capacity 判断切为委托 fallback pipeline。
3. `infra/CMakeLists.txt`：最小新增 `AuditFallbackPipeline.cpp` 到 `dasall_infra` 构建图。
4. `tests/unit/infra/AuditServiceFallbackTest.cpp`：新增 fallback append 顺序正例，继续保留 fallback exhaustion 负例。
5. `docs/todos/infrastructure/deliverables/AUD-TODO-010-AuditFallbackPipeline骨架收敛.md`：补齐 D/B 收敛、外部参考、Design -> Build 映射与验收结果。

验收结果：

1. `cmake --build build-ci --target dasall_infra dasall_audit_service_fallback_unit_test`：通过。
2. `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest"`：通过，发现 1 个定向测试。
3. `ctest --test-dir build-ci -R "AuditServiceFallbackTest" --output-on-failure`：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：fallback pipeline/result 命名已能直接表达降级 append 语义，无需新增注释。
2. 正负例覆盖：`AuditServiceFallbackTest` 新增 fallback append 顺序正例，继续复用 fallback exhaustion 负例。
3. 测试发现性：`AuditServiceFallbackTest` 已通过 `ctest -N -R` 命中，未引入新的 discoverability 缺口。
4. TODO 证据回写：已完成 010 状态、交付物、验收结果与执行记录回写。
5. 提交隔离：本轮只拆降级写入路径，不提前实现 facade 统一入口。

### 12.11 AUD-TODO-011

选中任务：

1. 任务 ID：AUD-TODO-011。
2. 可执行性依据：`AUD-TODO-008`、`009`、`010` 已完成，validator/pipeline/fallback 三段骨架均已落盘；当前唯一缺口是统一入口与生命周期管理尚未收敛为 facade。

研究学习：

1. 本地证据：audit 设计 6.2/6.3/6.4/6.7 要求 `AuditServiceFacade` 统一负责生命周期管理、错误映射和对子组件的串接，但不越权进入 exporter/retention/metrics/health。
2. 外部参考：OWASP Logging Cheat Sheet 建议实现应用范围内可复用、可测试的统一日志处理模块，并保持日志故障显式、不可静默吞掉；这支持把 audit 的 orchestrate 逻辑集中到 internal facade 中。

D 结论：

1. Design -> Build 映射：在 `infra/src/audit/AuditService.cpp` 内新增 internal `AuditServiceFacade`，统一持有 config、lifecycle、record store、degraded 状态和 validator，并让 public `AuditService` 变成 thin wrapper。
2. Build 三件套：
	- 代码目标：更新 `infra/include/audit/AuditService.h` 与 `infra/src/audit/AuditService.cpp`，让 `AuditServiceFacade` 串起 `AuditValidator -> AuditPipeline -> AuditFallbackPipeline`，同时补回 public `AuditService` 的深拷贝语义。
	- 测试目标：扩展 `AuditServiceFallbackTest` 覆盖 lifecycle state 与 pre-start write gate；补跑 `InfraErrorCodeMappingContractTest` 和 `AuditServiceBoundaryContractTest` 回归。
	- 验收命令：`cmake --build build-ci --target dasall_infra dasall_audit_service_fallback_unit_test dasall_contract_infra_error_code_boundary_test dasall_contract_audit_service_boundary_test && ctest --test-dir build-ci -N -R "AuditServiceFallbackTest|InfraErrorCodeMappingContractTest|AuditServiceBoundaryContractTest" && ctest --test-dir build-ci -R "AuditServiceFallbackTest|InfraErrorCodeMappingContractTest|AuditServiceBoundaryContractTest" --output-on-failure`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. `infra/include/audit/AuditService.h`：`AuditService` 收敛为 thin wrapper，持有 internal facade 指针，并显式提供构造、析构、拷贝、移动语义。
2. `infra/src/audit/AuditService.cpp`：新增 internal `AuditServiceFacade`，统一处理 `init/start/stop/write_audit/export_audit`，并收拢 validator/pipeline/fallback 串接与错误映射。
3. `tests/unit/infra/AuditServiceFallbackTest.cpp`：新增 lifecycle state 与 pre-start write gate 回归测试。
4. `docs/todos/infrastructure/deliverables/AUD-TODO-011-AuditServiceFacade骨架收敛.md`：补齐 D/B 收敛、外部参考、Design -> Build 映射与验收结果。

验收结果：

1. `cmake --build build-ci --target dasall_infra dasall_audit_service_fallback_unit_test dasall_contract_infra_error_code_boundary_test dasall_contract_audit_service_boundary_test`：通过。
2. `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest|InfraErrorCodeMappingContractTest|AuditServiceBoundaryContractTest"`：通过，发现 3 个定向测试。
3. `ctest --test-dir build-ci -R "AuditServiceFallbackTest|InfraErrorCodeMappingContractTest|AuditServiceBoundaryContractTest" --output-on-failure`：通过，3/3 tests passed。

Build 合规复核：

1. 代码注释：facade、wrapper 与 clone 语义均可由命名直接表达，无需新增注释。
2. 正负例覆盖：`AuditServiceFallbackTest` 新增 lifecycle/pre-start 负例与状态推进正例；contract 继续覆盖错误码映射与 service 边界。
3. 测试发现性：`AuditServiceFallbackTest`、`InfraErrorCodeMappingContractTest`、`AuditServiceBoundaryContractTest` 已通过 `ctest -N -R` 命中，未引入 discoverability 缺口。
4. TODO 证据回写：已完成 011 状态、交付物、验收结果与执行记录回写。
5. 提交隔离：本轮只收敛统一入口与生命周期管理，不提前推进 `AUD-TODO-016`、`017` 或 exporter/retention 能力。

### 12.12 AUD-TODO-016

选中任务：

1. 任务 ID：AUD-TODO-016。
2. 可执行性依据：`AUD-TODO-001` 至 `AUD-TODO-011` 已完成，audit 源码已实际进入构建图但尚未形成独立 CMake 入口，不存在额外 blocker。

研究学习：

1. 本地证据：当前 [infra/CMakeLists.txt](infra/CMakeLists.txt) 已能编译 audit 骨架，但 audit source/header 仍混在通用 core/public 列表中，缺少专项 TODO 所要求的显式 build 入口。
2. 外部参考：OpenTelemetry 项目布局指南强调稳定组件边界应在构建层也保持显式分组，而不是长期埋在笼统的 core/source 聚合列表中。

D 结论：

1. Design -> Build 映射：在 [infra/CMakeLists.txt](infra/CMakeLists.txt) 中新增 `DASALL_INFRA_AUDIT_SOURCES` 与 `DASALL_INFRA_AUDIT_PUBLIC_HEADERS`，把 audit 的 source/header 从通用列表抽为独立构建入口。
2. Build 三件套：
	- 代码目标：更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，将 audit 源码和 public header 收口为独立变量并接入 `dasall_infra`。
	- 测试目标：复用 `AuditInterfaceCompileTest`，确认 audit public 头与 `dasall_infra` 构建图接线稳定。
	- 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test && ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest" && ctest --test-dir build-ci -R "AuditInterfaceCompileTest" --output-on-failure`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. [infra/CMakeLists.txt](infra/CMakeLists.txt)：新增 `DASALL_INFRA_AUDIT_SOURCES`，收口 `AuditValidator.cpp`、`AuditPipeline.cpp`、`AuditFallbackPipeline.cpp`、`AuditService.cpp`。
2. [infra/CMakeLists.txt](infra/CMakeLists.txt)：新增 `DASALL_INFRA_AUDIT_PUBLIC_HEADERS`，收口 `AuditTypes.h`、`AuditExporterTypes.h`、`AuditErrors.h`、`AuditService.h`、`IAuditLogger.h`。
3. [docs/todos/infrastructure/deliverables/AUD-TODO-016-Audit构建接线收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-016-Audit%E6%9E%84%E5%BB%BA%E6%8E%A5%E7%BA%BF%E6%94%B6%E6%95%9B.md)：补齐 D/B 收敛、外部参考、Design -> Build 映射与验收结果。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。由于当前 `build-ci` 已锁定为 Unix Makefiles，本轮沿用现有生成器执行验证。
2. `cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest"`：通过，发现 1 个定向测试。
4. `ctest --test-dir build-ci -R "AuditInterfaceCompileTest" --output-on-failure`：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：本轮为纯 CMake 收敛，无新增代码逻辑注释需求。
2. 正负例覆盖：本轮不新增实现逻辑，复用 `AuditInterfaceCompileTest` 作为 build surface 正例；测试 discoverability 通过 `ctest -N -R` 补证。
3. 测试发现性：`AuditInterfaceCompileTest` 已通过 `ctest -N -R` 命中。
4. TODO 证据回写：已完成 016 状态、交付物与验收结果回写。
5. 提交隔离：本轮只处理 audit 构建入口收敛，不提前处理测试标签/注册问题，该部分留给 `AUD-TODO-017`。

### 12.13 AUD-TODO-017

选中任务：

1. 任务 ID：AUD-TODO-017。
2. 可执行性依据：`AUD-TODO-016` 已完成，audit 源码构建图稳定，当前剩余问题只在 tests 注册与标签 discoverability，未见前置 blocker。

研究学习：

1. 本地证据：[tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt) 中 `AuditTypesTest`、`AuditInterfaceCompileTest` 仍挂在历史 `logging` 标签上，而 `AuditServiceFallbackTest`、`AuditExportFilterTest` 只带通用 `unit` 标签；[tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt) 也把 audit targets 分散在 logging 与通用列表中。
2. 本地证据：[tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 中 `AuditBoundaryContractTest`、`AuditLoggerInterfaceBoundaryContractTest` 仍通过 logging helper 注册，`AuditServiceBoundaryContractTest` 与 `InfraErrorCodeMappingContractTest` 缺少 audit discoverability 标签。
3. 外部参考：CTest label 机制适合在不重排整体 target 拓扑的前提下，为模块建立稳定 discoverability 入口。

D 结论：

1. Design -> Build 映射：在 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt) 中新增 `dasall_register_audit_unit_test`，统一给 4 个 audit unit 测试设置 `unit;audit`；在 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt) 中新增 `DASALL_AUDIT_UNIT_TEST_EXECUTABLE_TARGETS`；在 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 中新增 `dasall_register_audit_contract_test`，统一给 4 个 audit contract 测试设置 `contract;smoke;audit`。
2. Build 三件套：
	- 代码目标：收敛 audit unit/contract 的注册 helper、顶层聚合分组和标签边界。
	- 测试目标：确保 4 个 audit unit 与 4 个 audit contract 测试既能被 `ctest -L unit/contract` 覆盖，也能被 `ctest -L audit` 定向发现。
	- 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract && ctest --test-dir build-ci -N -L audit && ctest --test-dir build-ci --output-on-failure -L audit`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)：新增 `dasall_register_audit_unit_test`，统一注册 `AuditTypesTest`、`AuditInterfaceCompileTest`、`AuditServiceFallbackTest`、`AuditExportFilterTest`。
2. [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)：新增 `DASALL_AUDIT_UNIT_TEST_EXECUTABLE_TARGETS`，把 audit unit targets 从 logging 与通用列表中抽出。
3. [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)：新增 `dasall_register_audit_contract_test`，收敛 `AuditBoundaryContractTest`、`AuditLoggerInterfaceBoundaryContractTest`、`AuditServiceBoundaryContractTest`、`InfraErrorCodeMappingContractTest` 的 audit discoverability。
4. [docs/todos/infrastructure/deliverables/AUD-TODO-017-Audit测试接线收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-017-Audit%E6%B5%8B%E8%AF%95%E6%8E%A5%E7%BA%BF%E6%94%B6%E6%95%9B.md)：补齐 D/B 收敛、设计结论与验收结果。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过。
3. `ctest --test-dir build-ci -N`：通过，发现总计 254 个测试。
4. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，112/112 tests passed，标签摘要中 `audit = 4 tests`。
5. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，132/132 tests passed，标签摘要中 `audit = 4 tests`。
6. `ctest --test-dir build-ci -N -L audit`：通过，发现 8 个测试。
7. `ctest --test-dir build-ci --output-on-failure -L audit`：通过，8/8 tests passed。

Build 合规复核：

1. 代码注释：本轮为 CMake 注册与标签收敛，无新增逻辑注释需求。
2. 正负例覆盖：本轮不新增测试逻辑，通过 existing audit unit/contract 测试矩阵验证 discoverability 与执行闭环。
3. 测试发现性：`ctest -N`、`ctest -L unit`、`ctest -L contract` 与 `ctest -L audit` 均已补齐证据。
4. TODO 证据回写：已完成 017 状态、交付物与验收结果回写。
5. 提交隔离：本轮只处理 audit 测试注册/标签收敛，不提前推进 `AUD-TODO-018` integration 接线。

### 12.14 AUD-TODO-014

选中任务：

1. 任务 ID：AUD-TODO-014。
2. 可执行性依据：`AUD-BLK-003` 已完成解阻，`AuditHealthStatus` 三态、最近失败原因字段与只读 `evaluate()` 语义已经冻结；当前仓库只缺 public header、接口编译测试与最小 integration ground truth。

研究学习：

1. 本地证据：audit 设计 [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.5/6.6.1 已冻结 `AuditHealthStatus` 的字段、三态与 allowed reason set，但 [infra/include/audit](infra/include/audit) 仍缺少 `IAuditHealthProbe.h`。
2. 本地证据：现有 [tests/unit/infra/AuditLoggerInterfaceTest.cpp](tests/unit/infra/AuditLoggerInterfaceTest.cpp) 已承担 `AuditInterfaceCompileTest`，适合继续冻结 `IAuditHealthProbe::evaluate()` 签名与 `AuditHealthStatus` 一致性守卫。
3. 本地证据：现有 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt) 允许像 `ConfigRuntimePatchIntegrationTest` 一样先在 infra 根级落一个最小 integration 用例，再由后续拓扑任务做目录与标签收口，因此本轮可补最小 `InfraAuditHealthIntegrationTest` 而不提前关闭 `AUD-TODO-018`。
4. 外部参考：Kubernetes readiness/liveness probe 指南强调 readiness 应保持低成本、持续评估，并在临时故障或组件停机时暴露可判定状态，而不是在探针内部吸收恢复动作；这支持 audit health 首版保持只读快照边界。

D 结论：

1. Design -> Build 映射：新增 [infra/include/audit/IAuditHealthProbe.h](infra/include/audit/IAuditHealthProbe.h)，在 `dasall::infra` 中冻结 `AuditHealthState` / `AuditHealthStatus` 与 reason allowlist，在 `dasall::infra::audit` 中定义只读 `IAuditHealthProbe::evaluate() const`。
2. Build 三件套：
	- 代码目标：新增 `IAuditHealthProbe.h`，并更新 [infra/CMakeLists.txt](infra/CMakeLists.txt) 把它纳入 audit public headers；扩展 [tests/unit/infra/AuditLoggerInterfaceTest.cpp](tests/unit/infra/AuditLoggerInterfaceTest.cpp)；新增 [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp) 与最小 root-level 注册。
	- 测试目标：`AuditInterfaceCompileTest` 覆盖 `Ready/Degraded/Unavailable` 正例与 invalid reason/Ready 携带 failure bits 负例；`InfraAuditHealthIntegrationTest` 覆盖 Ready、fallback_active Degraded、metrics bridge degraded 与 stopped -> Unavailable 四类场景。
	- 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test dasall_infra_audit_health_integration_test && ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest" && ctest --test-dir build-ci --output-on-failure -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest"`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. [infra/include/audit/IAuditHealthProbe.h](infra/include/audit/IAuditHealthProbe.h)：新增 `AuditHealthState`、`AuditHealthStatus`、reason allowlist 与 `IAuditHealthProbe::evaluate() const`。
2. [infra/CMakeLists.txt](infra/CMakeLists.txt)：将 `IAuditHealthProbe.h` 纳入 `DASALL_INFRA_AUDIT_PUBLIC_HEADERS`。
3. [tests/unit/infra/AuditLoggerInterfaceTest.cpp](tests/unit/infra/AuditLoggerInterfaceTest.cpp)：新增 `IAuditHealthProbe` 签名冻结、`AuditHealthStatus` 正负例一致性断言。
4. [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt)、[tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp)：新增根级 `InfraAuditHealthIntegrationTest`，用 test-local `AuditServiceBackedHealthProbe` 验证 Ready/Degraded/Unavailable 与 metrics bridge degraded 不升级为 `Unavailable` 的协同语义。
5. [docs/todos/infrastructure/deliverables/AUD-TODO-014-AuditHealthProbe接口收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-014-AuditHealthProbe%E6%8E%A5%E5%8F%A3%E6%94%B6%E6%95%9B.md)：补齐 D/B 收敛、研究结论与验收结果。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test dasall_infra_audit_health_integration_test`：通过。
3. `ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest"`：通过，发现 2 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：本轮新增接口与测试逻辑可由命名和状态对象直接表达，无需额外实现注释。
2. 正负例覆盖：unit 覆盖 Ready/Degraded/Unavailable 正例与 invalid reason、Ready 携带 failure bits 两类负例；integration 覆盖 primary healthy、fallback degraded、metrics degraded 与 stopped unavailable 四类协同路径。
3. 测试发现性：`ctest -N -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest"` 已证明当前根级注册可被定向发现；audit 专项目录与标签 discoverability 仍留给 `AUD-TODO-018` 收口。
4. TODO 证据回写：已完成 014 状态、交付物与验收结果回写，并同步标注 018 的剩余目录/标签拓扑工作。
5. 提交隔离：本轮只处理 audit health public interface、最小 integration ground truth 与对应证据，不提前推进 `AUD-TODO-015` 的 metrics bridge 实现。

### 12.15 AUD-TODO-015

选中任务：

1. 任务 ID：AUD-TODO-015。
2. 可执行性依据：`AUD-BLK-004` 已完成解阻，audit/metrics 设计已经冻结 `infra.audit@v1` meter scope、七指标对象表、五元标签白名单与 non-recursive failure 语义；现有根级 `InfraAuditHealthIntegrationTest` 已可作为当前轮最小 integration 验收出口。

研究学习：

1. 本地证据：现有 [infra/src/logging/LoggingMetricsBridge.h](infra/src/logging/LoggingMetricsBridge.h) / [infra/src/logging/LoggingMetricsBridge.cpp](infra/src/logging/LoggingMetricsBridge.cpp) 已经证明仓内 metrics bridge 的推荐分层是 `IMetricsProvider -> IMeter -> Instrument -> record(sample)`，而不是为每个模块再造私有 sink。
2. 本地证据：现有 [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp) 已覆盖 fallback degraded 与 stopped unavailable 语义，只缺“真实 metrics bridge degraded 不反噬 audit 主结果”的可执行断言。
3. 外部参考：OpenTelemetry Metrics API 强调 meter scope 与 instrument identity 必须稳定冻结，bridge 只应消费 provider/meter 分层，不应把 exporter、队列或运行时配置对象暴露给上层模块；这与 audit v1 协议冻结结论一致。

D 结论：

1. Design -> Build 映射：新增 internal [infra/src/audit/AuditMetricsBridge.h](infra/src/audit/AuditMetricsBridge.h) / [infra/src/audit/AuditMetricsBridge.cpp](infra/src/audit/AuditMetricsBridge.cpp)，冻结 `AuditMetricKind`、`AuditMetricSignal`、`AuditMetricsEmitResult`、七指标 family、五元标签白名单与 degraded/no-op 回退语义。
2. Build 三件套：
	- 代码目标：新增 internal bridge 头/源，并更新 [infra/CMakeLists.txt](infra/CMakeLists.txt) 把 `AuditMetricsBridge.cpp` 接入 `dasall_infra`；更新 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt) 为现有用例补 `infra/src` include path。
	- 测试目标：扩展 [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp)，用 fake `RecordingMetricsProvider` / `RecordingMeter` 驱动真实 bridge，覆盖 `audit_write_total` 成功发射、fallback 路径下 bridge 仍可健康发射、provider timeout 导致 bridge degraded 但 audit 主写仍成功，以及 stopped -> Unavailable 四类场景。
	- 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_infra_audit_health_integration_test && ctest --test-dir build-ci -N -R "InfraAuditHealthIntegrationTest" && ctest --test-dir build-ci --output-on-failure -R "InfraAuditHealthIntegrationTest"`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. [infra/src/audit/AuditMetricsBridge.h](infra/src/audit/AuditMetricsBridge.h)、[infra/src/audit/AuditMetricsBridge.cpp](infra/src/audit/AuditMetricsBridge.cpp)：新增 internal bridge，冻结 `infra.audit@v1`、七指标 family、`module/stage/profile/outcome/error_code` 标签白名单、provider/exporter degraded 与 `IdentityInvalid`/`ConfigInvalid` no-op 回退语义。
2. [infra/CMakeLists.txt](infra/CMakeLists.txt)：将 `AuditMetricsBridge.cpp` 纳入 `DASALL_INFRA_AUDIT_SOURCES`。
3. [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt)：为现有根级 `InfraAuditHealthIntegrationTest` 增加 `infra/src` include path，使其可直接消费 internal bridge。
4. [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp)：将 test-local health probe 改为读取真实 `AuditMetricsBridge::is_degraded()` 状态，并新增 `RecordingMetricsProvider` / `RecordingMeter` 断言 meter scope、七指标注册与 provider timeout -> degraded 语义。
5. [docs/todos/infrastructure/deliverables/AUD-TODO-015-AuditMetricsBridge骨架收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-015-AuditMetricsBridge%E9%AA%A8%E6%9E%B6%E6%94%B6%E6%95%9B.md)：补齐 D/B 收敛、研究结论与验收结果。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_infra_audit_health_integration_test`：通过。
3. `ctest --test-dir build-ci -N -R "InfraAuditHealthIntegrationTest"`：通过，发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "InfraAuditHealthIntegrationTest"`：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：bridge 的状态与标签约束由类型和 helper 命名直接表达，无需新增实现注释。
2. 正负例覆盖：integration 覆盖 write success、fallback degraded、metrics bridge degraded 不反噬主写结果、stopped unavailable 四类协同路径，并额外断言 `infra.audit@v1` meter scope 与七指标注册数。
3. 测试发现性：继续复用现有根级 `InfraAuditHealthIntegrationTest` 完成当前轮验收；audit 专项目录、顶层聚合 target 与 `integration;audit` 标签仍留给 `AUD-TODO-018` 收口。
4. TODO 证据回写：已完成 015 状态、交付物与验收结果回写，并将下一步切换到 018 的 integration 注册收口。
5. 提交隔离：本轮只处理 audit metrics bridge internal 骨架、现有 integration 用例扩展与对应证据，不提前推进 018 的目录/标签拓扑改造。

### 12.16 AUD-TODO-018

选中任务：

1. 任务 ID：AUD-TODO-018。
2. 可执行性依据：`AUD-TODO-014` 与 `AUD-TODO-015` 已完成，`InfraAuditHealthIntegrationTest` 已具备真实 health/metrics 协同断言；当前剩余缺口仅是 audit integration 目录、顶层 target 聚合与 `integration;audit` discoverability 收口。

研究学习：

1. 本地证据：现有 [tests/integration/infra/logging/CMakeLists.txt](tests/integration/infra/logging/CMakeLists.txt) 已通过模块级 helper 将 logging integration 用例收口到子目录与 `integration;logging` 标签，audit 可以沿同一模式完成 discoverability 收敛。
2. 本地证据：现有 [tests/integration/CMakeLists.txt](tests/integration/CMakeLists.txt) 的 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS` 尚未纳入 audit integration target，因此 `dasall_integration_tests` 顶层聚合边界仍不完整。
3. 外部参考：CTest label 过滤与分目录组织可以同时满足模块 discoverability 与顶层 gate 聚合，不需要为接线任务重写测试逻辑；018 的重点是 topology/registration，而不是用例语义改写。

D 结论：

1. Design -> Build 映射：将现有 `InfraAuditHealthIntegrationTest` 收口到 [tests/integration/infra/audit](tests/integration/infra/audit) 子目录，新增 audit integration helper，统一 `integration;audit` 标签。
2. Build 三件套：
	- 代码目标：新增 [tests/integration/infra/audit/CMakeLists.txt](tests/integration/infra/audit/CMakeLists.txt)，将用例迁移到 [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp)，并更新 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt) 与 [tests/integration/CMakeLists.txt](tests/integration/CMakeLists.txt) 完成子目录与顶层 target 聚合接线。
	- 测试目标：验证 `InfraAuditHealthIntegrationTest` 可被名字与 `audit` 标签同时发现，并继续稳定执行通过。
	- 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra_audit_health_integration_test && ctest --test-dir build-ci -N -R "InfraAuditHealthIntegrationTest" && ctest --test-dir build-ci -N -L audit -R "InfraAuditHealthIntegrationTest" && ctest --test-dir build-ci --output-on-failure -R "InfraAuditHealthIntegrationTest"`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. [tests/integration/infra/audit/CMakeLists.txt](tests/integration/infra/audit/CMakeLists.txt)：新增 `dasall_register_audit_integration_test`，统一 `integration;audit` 标签与 `infra/src` include path。
2. [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp)：将现有 audit integration 用例迁入 audit 子目录，保持 015 已落盘的 health/metrics 协同断言不变。
3. [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt)：移除 root-level audit test 直接注册，改为 `add_subdirectory(audit)`。
4. [tests/integration/CMakeLists.txt](tests/integration/CMakeLists.txt)：将 `dasall_infra_audit_health_integration_test` 纳入 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS`，补齐顶层 integration target 聚合。
5. [docs/todos/infrastructure/deliverables/AUD-TODO-018-AuditIntegration测试接线收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-018-AuditIntegration%E6%B5%8B%E8%AF%95%E6%8E%A5%E7%BA%BF%E6%94%B6%E6%95%9B.md)：补齐 D/B 收敛、discoverability 证据与验收结果。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra_audit_health_integration_test`：通过。
3. `ctest --test-dir build-ci -N -R "InfraAuditHealthIntegrationTest"`：通过，发现 1 个定向测试。
4. `ctest --test-dir build-ci -N -L audit -R "InfraAuditHealthIntegrationTest"`：通过，证明该用例已具备 `integration;audit` discoverability。
5. `ctest --test-dir build-ci --output-on-failure -R "InfraAuditHealthIntegrationTest"`：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：本轮只做接线与目录收口，无需新增实现注释。
2. 正负例覆盖：未改测试逻辑本体，只保持 015 已验证的四类协同场景；本轮新增的是目录/标签 discoverability 证据。
3. 测试发现性：`InfraAuditHealthIntegrationTest` 现已同时满足按名字发现、按 `audit` 标签发现、按 `integration` 标签执行的三类入口。
4. TODO 证据回写：已完成 018 状态、交付物与发现性证据回写，并将下一步切换到 019 的质量门收口。
5. 提交隔离：本轮只处理 audit integration 目录、顶层 target 聚合与标签 discoverability，不再混入 bridge 语义改动。

### 12.17 AUD-TODO-019

选中任务：

1. 任务 ID：AUD-TODO-019。
2. 可执行性依据：`AUD-TODO-017`、`AUD-TODO-018` 已完成，audit 当前可执行测试入口已覆盖 unit/contract/integration 三层；当前剩余工作是把 gate 结论、阻塞变化与回退证据统一回写到 TODO / deliverable / worklog。

研究学习：

1. 本地证据：当前 audit 标签下已经可发现 9 个测试，覆盖 4 个 unit、4 个 contract 与 1 个 integration，用同一条 `ctest -L audit` 证据链即可完成质量门收口。
2. 本地证据：`AUD-BLK-001` 与 `AUD-BLK-002` 仍然真实存在，因此 019 不能伪造“全绿完成”，必须把 PASS gate 与 BLOCKED gate 明确拆开回写。
3. 外部参考：工程 gate 文档的价值在于把“可执行证据”和“仍未具备执行条件的 blocker”同时写清楚，而不是只保留通过项；这与本仓库一任务一证据链的提交规范一致。

D 结论：

1. Design -> Build 映射：本轮不新增代码实现，只回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md)、[docs/todos/infrastructure/deliverables/AUD-TODO-019-Audit质量门与证据收口.md](docs/todos/infrastructure/deliverables/AUD-TODO-019-Audit%E8%B4%A8%E9%87%8F%E9%97%A8%E4%B8%8E%E8%AF%81%E6%8D%AE%E6%94%B6%E5%8F%A3.md) 与 [docs/worklog/DASALL_开发执行记录.md](docs/worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md)，形成当前 audit gate SSOT。
2. Build 三件套：
	- 代码目标：更新 TODO 的 019 状态、9.1 验收命令基线、9.3 gate 结论表、11.5 下一步建议，并同步 deliverable / worklog。
	- 测试目标：复用 audit 标签全量 gate，证明当前 audit 可执行范围已经覆盖 unit/contract/integration 三层。
	- 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test && ctest --test-dir build-ci -N -L audit && ctest --test-dir build-ci --output-on-failure -L audit`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md)：回写 019 状态、专项 gate 基线、PASS/BLOCKED gate 结论表与后续入口建议。
2. [docs/todos/infrastructure/deliverables/AUD-TODO-019-Audit质量门与证据收口.md](docs/todos/infrastructure/deliverables/AUD-TODO-019-Audit%E8%B4%A8%E9%87%8F%E9%97%A8%E4%B8%8E%E8%AF%81%E6%8D%AE%E6%94%B6%E5%8F%A3.md)：补齐本轮输入依据、gate 结论、阻塞变化与验收结果。
3. [docs/worklog/DASALL_开发执行记录.md](docs/worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md)：新增当前轮 gate 收口记录，并将下一步指向导出/retention blocker 解阻。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test`：通过。
3. `ctest --test-dir build-ci -N -L audit`：通过，发现 9 个测试。
4. `ctest --test-dir build-ci --output-on-failure -L audit`：通过，9/9 tests passed；标签摘要为 `unit=4`、`contract=4`、`integration=1`。

Build 合规复核：

1. 代码注释：本轮仅回写文档与 gate 证据，无代码注释改动。
2. 正负例覆盖：9 个 audit 标签测试已覆盖对象、接口、主写/降级、错误码映射与 health/metrics integration；blocked gate 也已以文档方式给出清晰解阻条件。
3. 测试发现性：`ctest -N -L audit` 已成为当前 audit 子域的统一 discoverability 入口。
4. TODO 证据回写：已完成 019 状态、gate 结论、阻塞变化与后续建议回写。
5. 提交隔离：本轮只处理 audit gate 证据与文档一致性回写，不混入新的实现改动。

### 12.18 AUD-TODO-012

选中任务：

1. 任务 ID：AUD-TODO-012。
2. 可执行性依据：`AUD-BLK-001` 已完成解阻，ExportQuery 的窗口+actor+action 主过滤、target/outcome 扩展规则、稳定 resume token 边界与 AuditEvent-only 导出面已经冻结；当前仓库只缺 internal exporter 落盘与对应 unit/contract 证据。

研究学习：

1. 本地证据：当前 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp) 仍在 service 内联执行导出筛选，说明 012 的根任务是把 export 责任收口到独立 internal `AuditExporter`，而不是继续在 facade 里堆逻辑。
2. 本地证据：现有 [tests/unit/infra/AuditExportFilterTest.cpp](tests/unit/infra/AuditExportFilterTest.cpp) 已冻结 ExportQuery/ExportResult 对象边界，适合作为 exporter 过滤/分页语义的 unit 验收出口；现有 [tests/contract/smoke/AuditBoundaryContractTest.cpp](tests/contract/smoke/AuditBoundaryContractTest.cpp) 已能承载“不引入 pattern/free-text/context 泄露”的 contract 边界断言。
3. 外部参考：OWASP Logging Cheat Sheet 强调日志/审计导出必须避免暴露 access token、session id、password、原始文件路径等高敏感数据；OTel Logs Data Model 强调稳定高频字段应保持结构化和类型固定，这支持 audit v1 把导出面继续锁定在 `AuditEvent`，而不是外带 `AuditContext`。

D 结论：

1. Design -> Build 映射：新增 internal [infra/src/audit/AuditExporter.h](infra/src/audit/AuditExporter.h) / [infra/src/audit/AuditExporter.cpp](infra/src/audit/AuditExporter.cpp)，把窗口+actor+action 主过滤、target/outcome exact-match 扩展、稳定排序与 opaque resume token 从 `AuditService` 内联逻辑收口成独立 exporter。
2. Build 三件套：
	- 代码目标：新增 internal exporter 头/源，更新 [infra/CMakeLists.txt](infra/CMakeLists.txt) 把 `AuditExporter.cpp` 接入 `dasall_infra`，并更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp) 让 service 委托 exporter 导出。
	- 测试目标：扩展 [tests/unit/infra/AuditExportFilterTest.cpp](tests/unit/infra/AuditExportFilterTest.cpp) 验证主过滤、分页 resume token 与跨过滤元组 token 失效负例；扩展 [tests/contract/smoke/AuditBoundaryContractTest.cpp](tests/contract/smoke/AuditBoundaryContractTest.cpp) 固定“不引入 `target_pattern`/`outcome_reason`，不把 `AuditContext` 并入导出载荷”的 contract 边界；补跑 `AuditServiceFallbackTest` 与完整 `ctest -L audit` 回归。
	- 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_audit_export_filter_unit_test dasall_audit_service_fallback_unit_test dasall_contract_audit_event_boundary_test && ctest --test-dir build-ci -N -R "AuditExportFilterTest|AuditBoundaryContractTest|AuditServiceFallbackTest" && ctest --test-dir build-ci --output-on-failure -R "AuditExportFilterTest|AuditBoundaryContractTest|AuditServiceFallbackTest" && cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test && ctest --test-dir build-ci -N -L audit && ctest --test-dir build-ci --output-on-failure -L audit`。
3. D Gate：PASS。

Build 交付与证据：

交付物：

1. [infra/src/audit/AuditExporter.h](infra/src/audit/AuditExporter.h)、[infra/src/audit/AuditExporter.cpp](infra/src/audit/AuditExporter.cpp)：新增 internal exporter，冻结窗口+actor+action 主过滤、target/outcome exact-match 扩展、稳定排序与 opaque resume token 语义。
2. [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)：将导出逻辑从 service 内联筛选收口为委托 `AuditExporter::export_records()`。
3. [infra/CMakeLists.txt](infra/CMakeLists.txt)：将 `AuditExporter.cpp` 纳入 `DASALL_INFRA_AUDIT_SOURCES`。
4. [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)、[tests/unit/infra/AuditExportFilterTest.cpp](tests/unit/infra/AuditExportFilterTest.cpp)：为 exporter unit 测试补 `infra/src` include path，并新增主过滤、分页 token 与 token 失配负例覆盖。
5. [tests/contract/smoke/AuditBoundaryContractTest.cpp](tests/contract/smoke/AuditBoundaryContractTest.cpp)：新增 exact-match 过滤边界与 AuditEvent-only 导出载荷约束。
6. [docs/todos/infrastructure/deliverables/AUD-TODO-012-AuditExporter骨架收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-012-AuditExporter%E9%AA%A8%E6%9E%B6%E6%94%B6%E6%95%9B.md)：补齐本轮 D/B 收敛、研究结论与验收结果。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_audit_export_filter_unit_test dasall_audit_service_fallback_unit_test dasall_contract_audit_event_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "AuditExportFilterTest|AuditBoundaryContractTest|AuditServiceFallbackTest"`：通过，发现 3 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "AuditExportFilterTest|AuditBoundaryContractTest|AuditServiceFallbackTest"`：通过，3/3 tests passed。
5. `ctest --test-dir build-ci -N -L audit`：通过，发现 9 个测试。
6. `ctest --test-dir build-ci --output-on-failure -L audit`：通过，9/9 tests passed。

Build 合规复核：

1. 代码注释：本轮新增 internal exporter 的类型与 helper 命名足以直接表达过滤/分页职责，无需额外实现注释。
2. 正负例覆盖：`AuditExportFilterTest` 新增主过滤正例、分页 resume token 正例与跨过滤元组 token 失配负例；`AuditBoundaryContractTest` 新增 exact-match/filter-shape 边界断言；`AuditServiceFallbackTest` 作为 service 委托 exporter 的回归补充。
3. 测试发现性：定向 `ctest -N -R` 和完整 `ctest -N -L audit` 均已补齐，未引入 discoverability 缺口。
4. TODO 证据回写：已完成 012 状态、交付物、验收结果与下一步建议回写。
5. 提交隔离：本轮只处理 exporter internal 骨架、service 导出委托、相关 unit/contract 测试与证据回写，不提前推进 retention 设计。
