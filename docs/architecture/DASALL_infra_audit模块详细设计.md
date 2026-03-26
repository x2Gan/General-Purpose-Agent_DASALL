# DASALL infra/audit 模块详细设计（Detailed Design）

版本：v1.0  
日期：2026-03-25  
阶段：Detailed Design  
模块：infra/audit  

说明：本次按 4.2 上下文执行 infra/audit 设计。4.4 中“profiles 模块”表述视为模板残留，本稿不扩展到 profiles 领域。

## 1. 模块概览

### 1.1 模块定位

infra/audit 属于 Infrastructure Layer（Layer 1），负责“审计事件的可信记录、导出、保留与失败兜底”，不承担业务决策。

依据：
1. docs/architecture/DASSALL_Agent_architecture.md（3.4.7、5.10、8.8）
2. docs/architecture/DASALL_Engineering_Blueprint.md（3.12、4.2、5.1）
3. docs/architecture/DASALL_infrastructure子系统详细设计.md（6.2、6.3、6.8、6.10、7）

### 1.2 边界、职责与依赖方向

1. 上游消费者：runtime、tools、services、infra/secret、infra/ota、infra/watchdog、infra/plugin。
2. 下游依赖：platform 文件与时间抽象、可选 third_party（spdlog/openssl）。
3. 同层协同：infra/logging、infra/tracing、infra/metrics、infra/config、infra/health。
4. 依赖方向：只允许上层 -> infra，禁止 infra -> 业务实现反向依赖。

### 1.3 输入输出摘要

1. 输入：AuditEvent、InfraContext、策略快照引用、导出查询参数。
2. 输出：审计持久化记录、导出结果、失败告警指标、降级状态信号。

---

## 2. 约束清单

### 2.1 Must / Should / Must-Not 约束表

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| AUD-C001 | DASSALL_Agent_architecture.md 3.4.7/5.10/8.8 | Must | 审计必须作为独立观测链路存在，不与普通运行日志混存 | 子组件/存储/流程 |
| AUD-C002 | DASALL_Engineering_Blueprint.md 4.2 | Must-Not | infra 不得反向依赖 runtime/cognition/llm 等业务实现 | 依赖边界 |
| AUD-C003 | DASALL_Engineering_Blueprint.md 4.3 | Must | 跨模块语义通过 contracts 冻结对象传递，不引入私有实现字段到 contracts | 接口/对象 |
| AUD-C004 | ADR-005-architecture-review-baseline.md | Must | contracts 与边界冻结优先，设计不得反向改写架构结论 | 设计治理 |
| AUD-C005 | ADR-006-context-orchestrator-vs-prompt-composer.md | Must-Not | audit 不接管上下文装配与 prompt 渲染职责，仅记录事实证据 | 职责边界 |
| AUD-C006 | ADR-007-reflection-engine-vs-recovery-manager.md | Must-Not | audit 不做失败归因与恢复裁定，只记录建议与执行结果 | 异常语义 |
| AUD-C007 | ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | Must | 需保留 parent_task_id/lease_id/worker_type 等协同链路标识 | 数据字段 |
| AUD-C008 | DASALL_contracts冻结实施计划.md 6/8 | Must | 默认向后兼容；破坏性变更必须评审 | 演进策略 |
| AUD-C009 | DASALL_contracts冻结TODO总表.md M5 | Must-Not | 不把 sink、线程池、后端存储等实现细节写入 contracts 共享语义 | contracts 边界 |
| AUD-C010 | DASALL_infrastructure子系统详细设计.md 6.8/6.10 | Must | 审计失败不可静默丢失，必须有 fallback 与可观测告警 | 异常恢复/可观测 |
| AUD-C011 | DASALL_工程协作与编码规范.md 3.6 | Must | 禁止吞错，失败需映射错误码并可观测 | 错误处理 |
| AUD-C012 | DASALL_工程协作与编码规范.md 3.7 | Should | 新增公共接口需至少配 1 个 unit 或 contract 测试 | 测试门禁 |
| AUD-C013 | DASALL_Engineering_Blueprint.md 5.1 | Must | Profile 只能裁剪能力，不得绕过审计主链路 | 配置策略 |
| AUD-C014 | OTel Logs Spec + OWASP Logging Cheat Sheet | Should | 记录 TraceId/SpanId/资源属性，保持“who/what/when/where/outcome”审计字段完整 | 字段规范/关联分析 |

### 2.2 约束抽取结论

1. Must：审计链路独立、失败可见、字段可追责、兼容优先。
2. Should：OTel 关联字段、标准化可导出、可测试门禁。
3. Must-Not：不改 ADR、不污染 contracts、不抢 runtime/cognition 决策权。

---

## 3. 现状与缺口

### 3.1 现状识别

代码与工程现状证据：
1. infra/CMakeLists.txt 仅编译 src/placeholder.cpp。
2. infra/include 为空。
3. infra/src 仅有 config/health/logging/metrics/ota/secret/tracing 空目录与 placeholder。
4. infra 下 grep 无 IAuditLogger/AuditEvent/audit 实现命中。
5. docs/architecture/DASALL_infrastructure子系统详细设计.md 与 docs/todos/DASALL_infrastructure子系统专项TODO.md 已将 audit 标为 P0 缺口与可执行任务锚点（INF-TODO-004/006/016）。

### 3.2 现状-目标差距表

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| 审计接口冻结（IAuditLogger） | 缺失 | 无对外统一接口，调用方无法稳定接入 | High | P0 |
| 审计对象冻结（AuditEvent） | 缺失 | 审计字段与 contracts 引用边界未落盘 | High | P0 |
| 审计独立组件（AuditService） | 缺失 | 无独立生命周期与 fallback 流程 | High | P0 |
| 审计存储与导出 | 缺失 | 无导出语义、保留策略、过滤模型 | High | P0 |
| 审计失败可观测 | 缺失 | 无失败计数、告警与降级状态 | High | P0 |
| 审计与 trace/metrics 关联 | 缺失 | 无 trace_id/span_id 贯通和指标桥接 | Medium | P1 |
| 审计配置策略（Profile/部署覆盖） | 缺失 | 无开关与阈值策略可控项 | Medium | P1 |
| 审计测试门禁 | 缺失 | 无 unit/contract/integration/failure 注入基线 | High | P0 |

### 3.3 风险冲突识别

| 冲突类型 | 描述 | 影响 | 风险等级 |
|---|---|---|---|
| 边界冲突 | audit 若直接推进 retry/replan 决策，将违反 ADR-007 | 控制面失衡 | High |
| 语义重复 | audit 若重定义 contracts 错误与标识语义 | 合同漂移与返工 | High |
| 依赖反转 | 上层直接绑具体 sink 后端 | Profile 裁剪失效 | Medium |

---

## 4. 候选方案对比

### 4.1 候选方案说明

1. 方案 A：复用 infra/logging 单管线（普通日志与审计仅靠 category 区分）。
2. 方案 B：审计独立服务（AuditService + 双 sink + fallback + 导出接口）。
3. 方案 C：事件总线驱动审计（先入本地 WAL，再异步归档/导出）。

### 4.2 候选方案对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| A 复用 logging 单管线 | 中 | 中 | 低 | 审计与运行日志易混存，合规/追责弱 | 淘汰：不满足审计独立性硬约束 |
| B 审计独立服务 | 高 | 高 | 中 | 需要新增接口、导出与 fallback 设计 | 保留：本轮采纳 |
| C WAL 事件总线审计 | 高 | 高 | 高 | 引入队列/WAL/回放复杂度，超出当前最小闭环 | 暂缓：作为 v2 演进 |

### 4.3 行业实践匹配摘要

1. OTel Logs：强调 logs-traces-metrics 统一关联，建议保留 trace_id/span_id/resource。
2. OWASP Logging：建议审计与操作日志分离、字段覆盖 who/what/when/where/outcome、记录高风险动作、具备防篡改与失败验证。
3. spdlog Async：提供 block 与 overrun_oldest 队列满策略，适配 Profile 分档治理。

---

## 5. 决策结论

### 5.1 最终选型

采纳方案 B：AuditService 独立组件化（接口冻结优先，最小实现闭环优先）。

### 5.2 放弃其他候选原因

1. 放弃 A：违背 AUD-C001 与 AUD-C010，无法保证审计独立与失败不可静默。
2. 放弃 C：当前仓库处于骨架阶段，WAL/回放链路会显著增加实现和测试复杂度。

### 5.3 一致性说明（架构 / ADR / contracts）

1. 架构一致：audit 仅提供基础能力，不越权到业务决策。
2. ADR 一致：不侵入 Context/Prompt、Reflection/Recovery、Orchestrator/Coordinator 决策面。
3. contracts 一致：仅消费已冻结标识与结果语义，不扩写共享对象。

---

## 6. 详细设计

### 6.1 职责边界

AuditService 职责：
1. 接收并校验 AuditEvent。
2. 执行主审计写入与 fallback 写入。
3. 支持按时间窗/实体/动作导出审计片段。
4. 产出失败计数、降级状态、健康探针结果。

AuditService 非职责：
1. 不判定是否应该重试/重规划。
2. 不改写业务对象或 contracts 公共语义。
3. 不直接驱动用户响应或状态机推进。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| AuditServiceFacade | 审计入口、生命周期管理、统一错误映射 |
| AuditValidator | 字段完整性与边界校验（必填、枚举、引用语义） |
| AuditPipeline | 主写入链路（append-only） |
| AuditFallbackPipeline | 主写失败时降级写入（本地 ringbuffer/file） |
| AuditExporter | 审计导出（过滤、分页、脱敏） |
| AuditRetentionManager | 保留期与归档策略执行 |
| AuditMetricsBridge | 输出审计写入成功率/失败率/导出耗时等指标 |
| AuditHealthProbe | 输出 ready/degraded/unavailable |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| AuditServiceFacade | runtime/tools/infra 子域 AuditEvent | AuditPipeline/Fallback/Exporter | 返回可判定 ResultCode/ErrorInfo |
| AuditValidator | AuditEvent + PolicySnapshotRef | 验证结果 | 必填字段缺失即拒绝写入 |
| AuditPipeline | 验证通过的 AuditEvent | 主存储 | append-only，写失败必须上报 |
| AuditFallbackPipeline | 主写失败事件 | 降级存储 | 不可静默丢失，需标记 degraded |
| AuditExporter | ExportQuery | 导出结果 | 导出不得泄露敏感字段 |
| AuditRetentionManager | 保留策略 + 存储状态 | 归档/清理动作 | 清理需保留审计痕迹 |
| AuditMetricsBridge | 管线统计 | metrics 子系统 | 指标标签受白名单治理 |
| AuditHealthProbe | 主/降级链路状态 | health 子系统 | 状态与最近失败原因可查询 |

### 6.4 子组件依赖关系

1. AuditServiceFacade -> AuditValidator -> AuditPipeline。
2. AuditPipeline 失败 -> AuditFallbackPipeline（强制告警 + 计数）。
3. AuditServiceFacade -> AuditExporter / AuditRetentionManager。
4. AuditPipeline/Fallback -> AuditMetricsBridge + AuditHealthProbe。
5. 仅通过接口依赖 infra/config、infra/metrics、infra/health。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | contracts 对齐关系 |
|---|---|---|---|
| AuditEvent | event_id, action, actor, target, outcome, evidence_ref, side_effects, timestamp | 必填字段不能为空；outcome 需可判定 | 仅引用 ToolResult/RecoveryOutcome/WorkerTask 标识语义 |
| AuditContext | request_id, session_id, trace_id, task_id, parent_task_id, lease_id, worker_type | 缺失允许 unknown，不允许 null 指针语义 | 对齐 AgentRequest/WorkerLease 标识，不扩写 |
| ExportQuery | start_ts, end_ts, actor, action, target, outcome, page_token | 时间窗必填；分页稳定 | 与 contracts 无耦合，infra 私有 |
| ExportResult | records, next_page_token, truncated, checksum | truncated 必须显式 | 与 contracts 无耦合，infra 私有 |
| AuditWriteOutcome | accepted, persisted, fallback_used, error_code | 二值可判定 | 映射 contracts::ResultCode，不新增共享码域 |

### 6.6 核心接口语义定义

建议头文件位置：infra/include/audit/

1. IAuditLogger
   - write_audit(const AuditEvent&, const AuditContext&) -> AuditWriteOutcome
   - export_audit(const ExportQuery&) -> ExportResult

2. IAuditRetention
   - apply_retention(now_ts) -> RetentionOutcome

3. IAuditHealthProbe
   - evaluate() -> AuditHealthStatus

前置条件：
1. AuditService 已 init/start。
2. 配置和策略快照已加载。

后置条件：
1. 写入成功或失败均有可观测证据（日志/指标/健康状态至少一项）。

错误语义（infra 私有码域）：
1. INF_E_AUDIT_INVALID_EVENT
2. INF_E_AUDIT_WRITE_FAIL
3. INF_E_AUDIT_FALLBACK_FAIL
4. INF_E_AUDIT_EXPORT_DENIED
5. INF_E_AUDIT_EXPORT_FAIL
6. INF_E_AUDIT_RETENTION_FAIL

### 6.7 主流程时序

1. 上游提交 AuditEvent + AuditContext。
2. AuditValidator 执行字段与策略校验。
3. AuditPipeline 执行主写入（append-only）。
4. AuditMetricsBridge 记录成功指标。
5. AuditHealthProbe 更新 ready 状态。
6. 对上游返回 accepted=true、persisted=true。

### 6.8 异常与恢复时序

异常分类：
1. 输入异常：字段缺失、越权字段、时间窗非法。
2. 瞬时故障：I/O 抖动、队列满、短暂权限错误。
3. 持续故障：主存储不可写、导出依赖不可用。

恢复动作：
1. 主写失败 -> fallback 写入 + 强告警 + degraded 状态。
2. fallback 失败 -> 记录 fatal 计数 + 返回 INF_E_AUDIT_FALLBACK_FAIL。
3. 导出失败 -> 保持写入链路可用，导出接口返回 INF_E_AUDIT_EXPORT_FAIL。
4. 连续失败阈值触发 -> 通知 infra/health 与上游控制面，但不越权执行恢复裁定。

兜底策略：
1. 至少保证“失败证据可观测”，禁止静默。
2. 审计不可关闭，仅允许降级模式（AUD-C013）。

### 6.9 配置项与默认策略

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| infra.audit.enabled | true | 默认/Profile/部署 | 审计能力开关（生产以 infra.audit.required=true 为准） |
| infra.audit.required | true | 默认/Profile | 合规强制开关 |
| infra.audit.storage.primary | file_append | Profile/部署 | 主存储后端 |
| infra.audit.storage.fallback | ringbuffer | Profile/部署 | 降级后端 |
| infra.audit.queue.size | 4096 | Profile/部署 | 审计队列容量 |
| infra.audit.queue.overflow_policy | block | Profile/部署 | block/overrun_oldest；选择规则遵循 docs/development/InfraConcurrencyPolicy.md |
| infra.audit.export.enabled | true | Profile/部署 | 导出能力开关 |
| infra.audit.export.max_window_sec | 86400 | Profile/部署 | 最大导出窗口 |
| infra.audit.retention.days | 30 | Profile/部署 | 保留天数 |
| infra.audit.integrity.checksum | true | Profile/部署 | 记录校验摘要 |
| infra.audit.redaction.enabled | true | 默认/Profile/部署 | 导出脱敏开关 |

### 6.10 可观测性（日志/指标/追踪/审计）

1. 日志点：write_start/write_success/write_fail/fallback_start/fallback_fail/export_start/export_fail/retention_run。
2. 指标：audit_write_total、audit_write_fail_total、audit_fallback_total、audit_fallback_fail_total、audit_export_total、audit_export_fail_total、audit_queue_depth。
3. 追踪：write/export 操作带 trace_id/span_id 关联字段。
4. 审计：高风险动作（secret.rotate、ota.apply/rollback、plugin.load/unload、policy.patch、diagnostics.export）强制入审计。

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 审计独立接口冻结 | 新增 IAuditLogger/类型定义 | 先稳边界后实现 | infra/include/audit/IAuditLogger.h, infra/include/audit/AuditTypes.h | unit: AuditTypesTest, AuditInterfaceCompileTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditTypesTest|AuditInterfaceCompileTest" --output-on-failure | 依赖 INF-TODO-004/006 语义锚点 |
| 主写入+fallback 闭环 | 新增 AuditService 组件骨架 | 满足失败不可静默 | infra/src/audit/AuditService.cpp, infra/src/audit/AuditPipeline.cpp, infra/src/audit/AuditFallbackPipeline.cpp | unit: AuditServiceFallbackTest | ctest --test-dir build-ci -R AuditServiceFallbackTest --output-on-failure | 阻塞：导出 filter 模型未冻结 |
| 导出与脱敏 | 新增 AuditExporter | 支撑诊断与合规审查 | infra/src/audit/AuditExporter.cpp | unit: AuditExportFilterTest; contract: AuditBoundaryContractTest | ctest --test-dir build-ci -R "AuditExportFilterTest|AuditBoundaryContractTest" --output-on-failure | 阻塞：ExportQuery 细粒度字段需冻结 |
| 错误码映射 | 新增审计错误码与映射器 | 保证失败二值可判定 | infra/include/audit/AuditErrors.h, infra/src/audit/AuditErrorMapper.cpp | contract: InfraErrorCodeMappingContractTest | ctest --test-dir build-ci -R InfraErrorCodeMappingContractTest --output-on-failure | 依赖 contracts::ResultCode 稳定映射 |
| 可观测桥接 | 新增指标与健康探针桥接 | 审计链路自身可观测 | infra/src/audit/AuditMetricsBridge.cpp, infra/src/audit/AuditHealthProbe.cpp | integration: InfraAuditHealthIntegrationTest | ctest --test-dir build-ci -R InfraAuditHealthIntegrationTest --output-on-failure | 阻塞：tests/integration 顶层接线 |
| 无法映射项：WAL 回放 | 标注 v2 预研项 | 当前阶段复杂度过高 | N/A | N/A | N/A | 后续需 ADR 或补设计支持 |

---

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

```text
infra/
  include/
    audit/
      IAuditLogger.h
      AuditTypes.h
      AuditErrors.h
      AuditExporterTypes.h
  src/
    audit/
      AuditService.cpp
      AuditPipeline.cpp
      AuditFallbackPipeline.cpp
      AuditExporter.cpp
      AuditRetentionManager.cpp
      AuditMetricsBridge.cpp
      AuditHealthProbe.cpp
tests/
  unit/
    infra/
      audit/
        AuditTypesTest.cpp
        AuditInterfaceCompileTest.cpp
        AuditServiceFallbackTest.cpp
        AuditExportFilterTest.cpp
  contract/
    infra/
      AuditBoundaryContractTest.cpp
      InfraErrorCodeMappingContractTest.cpp
  integration/
    infra/
      InfraAuditHealthIntegrationTest.cpp
```

### 8.2 分阶段实施计划（最小可交付）

| 阶段 | 里程碑 | 原子任务（代码目标/测试目标/验收命令） | 完成判定 |
|---|---|---|---|
| M1 | 接口与对象冻结 | 新增 AuditTypes/IAuditLogger；新增 AuditTypesTest + 接口编译测试；命令：cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "AuditTypesTest|AuditInterfaceCompileTest" | 字段与接口通过评审，编译与单测通过 |
| M2 | 主链路可写 | 新增 AuditService/Pipeline/Fallback 最小实现；新增 AuditServiceFallbackTest；命令：ctest --test-dir build-ci -R AuditServiceFallbackTest | 主写失败可触发 fallback，且失败可观测 |
| M3 | 导出与边界门禁 | 新增 Exporter 与 contract 测试；命令：ctest --test-dir build-ci -R "AuditExportFilterTest|AuditBoundaryContractTest|InfraErrorCodeMappingContractTest" | 导出可过滤、边界不越权、错误码稳定 |
| M4 | 集成与质量门 | 新增健康与指标桥接 + integration 测试；命令：ctest --test-dir build-ci -R InfraAuditHealthIntegrationTest | 审计链路 ready/degraded 可判定，质量门通过 |

---

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试层级 | 覆盖范围 | 关键用例 | 通过标准 |
|---|---|---|---|
| Unit | AuditTypes/Validator/Pipeline/Fallback/Exporter | 必填字段校验、主写失败 fallback、导出时间窗过滤、脱敏验证 | 所有断言通过，失败路径返回明确错误码 |
| Contract | contracts 语义对齐 | AuditEvent 引用边界不越权、错误码映射不漂移 | contract 测试可阻止越权字段 |
| Integration | audit 与 health/metrics/config 协同 | degraded 状态传播、指标上报、配置覆盖生效 | 集成链路可复现且二值可判定 |
| Failure Injection | I/O 失败、队列满、fallback 失败 | 写失败计数增长、健康状态降级、错误码准确 | 注入后观测信号完整 |
| Compatibility | 升级前后字段兼容 | 新增 optional 字段向后兼容，旧数据可读 | 兼容用例通过，无 breaking 回归 |

### 9.2 质量门（Gate）建议

| Gate ID | 门禁项 | 通过条件 | 失败动作 |
|---|---|---|---|
| AUD-GATE-01 | 接口冻结门 | IAuditLogger/AuditTypes 落盘并评审通过 | 回退到接口层修订 |
| AUD-GATE-02 | 边界一致性门 | contract 测试阻止语义越权 | 回退对象定义并补映射测试 |
| AUD-GATE-03 | 失败可观测门 | 写失败、fallback 失败均有日志/指标/健康信号 | 禁止进入集成阶段 |
| AUD-GATE-04 | 配置一致性门 | Profile 覆盖不绕过审计 required 链路 | 回退配置变更 |
| AUD-GATE-05 | 回归门 | unit+contract+integration 全通过 | 阻断合入 |

---

## 10. 兼容性与演进评估（建议级）

### 10.1 兼容与演进评估表

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | runtime/tools/infra 子域调用方 | 先引入新接口并保留旧占位路径；调用方逐步迁移至 IAuditLogger | 先在 desktop_full/edge_balanced 开启，edge_minimal 保持最小审计 | 预留 WAL 后端、远程归档 exporter |
| Medium（若改字段） | contract 测试与导出消费者 | 新增字段只用 optional；禁止删除或改名既有字段 | 双写窗口（旧字段+新字段），通过后再清理 | 预留 schema_version 字段 |
| High（若改错误码语义） | 全链路错误处理 | 保持旧码映射，新增细分码走一对多映射 | 分阶段启用新码并保持兼容映射表 | 预留 error_domain 子码段 |

### 10.2 迁移原则

1. 默认 non-breaking。
2. 任何 breaking 必须先评审并附迁移脚本/映射说明。
3. 先 contract gate，再功能扩展。

---

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| 导出 filter 模型未冻结 | M3 导出与 contract 门禁 | 冻结 ExportQuery 最小字段集合 | 先支持时间窗+actor+action 三键过滤 | 导出功能降级为只读全量（受限环境） |
| tests/integration 顶层接线不完整 | M4 集成测试 | 顶层 CMake 注册 integration 子目录 | 先执行 unit+contract，integration 标记 Blocked | 回退到 unit/contract gate-only |
| 审计存储保留策略未冻结 | M3/M4 retention 验证 | 明确保留天数、归档周期、清理策略 | 先固定 retention.days=30 与手动清理 | 暂停自动清理，只做归档标记 |
| metrics/health 桥接接口细节待定 | M4 协同测试 | 冻结桥接接口签名与标签白名单 | 先用 mock bridge 完成接口测试 | 回退桥接为本地计数，不宣称生产可观测 |

### 11.2 风险清单

| 风险 | 等级 | 触发条件 | 预防措施 |
|---|---|---|---|
| 审计与普通日志混流 | High | 复用同一 sink 且无隔离标识 | 强制 AuditPipeline 独立目录与存储 |
| 审计失败被吞没 | High | 未实现 fallback 或无计数告警 | AUD-GATE-03 强制门禁 |
| contracts 语义漂移 | High | 在 audit 对象中引入实现字段 | contract 边界测试 + 评审清单 |
| 高负载阻塞主流程 | Medium | overflow policy 不当 | Profile 分档 + block/overrun 策略可配置 |

---

## 12. 未决问题与后续任务

### 12.1 未决问题

1. ExportQuery 是否需要 target_pattern 与 outcome_reason 细粒度过滤。
2. 审计记录完整性校验采用 checksum 还是链式 hash（v2 可选）。
3. edge_minimal 档位的最小保留策略是否允许仅 ringbuffer + 周期上送。
4. integration 顶层接线何时并入默认 CI。

### 12.2 后续任务建议（原子化）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| AUD-T001 | Not Started | 新增 AuditTypes 头文件并冻结字段 | 6.5 + AUD-C007 | infra/include/audit/AuditTypes.h | AuditTypesTest | ctest --test-dir build-ci -R AuditTypesTest --output-on-failure | 字段齐备且不越权 |
| AUD-T002 | Not Started | 新增 IAuditLogger 接口骨架 | 6.6 + AUD-C003 | infra/include/audit/IAuditLogger.h | AuditInterfaceCompileTest | ctest --test-dir build-ci -R AuditInterfaceCompileTest --output-on-failure | 接口可编译且职责分离 |
| AUD-T003 | Not Started | 新增 AuditService 主写与 fallback 骨架 | 6.2/6.8 + AUD-C010 | infra/src/audit/AuditService.cpp | AuditServiceFallbackTest | ctest --test-dir build-ci -R AuditServiceFallbackTest --output-on-failure | 主写失败可触发 fallback |
| AUD-T004 | Blocked | 冻结 ExportQuery 并实现导出过滤 | 6.3 + 11.1 阻塞项 | infra/include/audit/AuditExporterTypes.h, infra/src/audit/AuditExporter.cpp | AuditExportFilterTest | ctest --test-dir build-ci -R AuditExportFilterTest --output-on-failure | 过滤语义评审通过并测试通过 |
| AUD-T005 | Not Started | 补齐审计 contract 边界测试 | 2.1 + 9.1 | tests/contract/infra/AuditBoundaryContractTest.cpp | AuditBoundaryContractTest | ctest --test-dir build-ci -R AuditBoundaryContractTest --output-on-failure | 可阻止 contracts 越权 |
| AUD-T006 | Blocked | 接入 integration 健康桥接测试 | 9.1 + 11.1 阻塞项 | tests/integration/infra/InfraAuditHealthIntegrationTest.cpp | InfraAuditHealthIntegrationTest | ctest --test-dir build-ci -R InfraAuditHealthIntegrationTest --output-on-failure | integration 接线完成且用例通过 |
