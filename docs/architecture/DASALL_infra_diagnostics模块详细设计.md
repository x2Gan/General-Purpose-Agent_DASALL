# DASALL infra/diagnostics 模块详细设计（Detailed Design）

版本：v1.0  
日期：2026-03-25  
阶段：Detailed Design  
适用模块：infra/diagnostics

## 1. 模块概览

### 1.1 模块定位

infra/diagnostics 属于 Infrastructure Layer（Layer 1），负责诊断命令执行、故障快照生成、证据聚合与导出，不承担业务编排与恢复裁定。

模块目标：
1. 提供统一诊断入口与可审计的执行链路。
2. 产出可回放、可脱敏、可追踪的 DiagnosticsSnapshot。
3. 向 runtime/ops 提供“可观测证据”，而不是“恢复决策”。

### 1.2 层级边界与依赖方向

上游调用方：apps、runtime、infra/ota、infra/plugin、infra/health。  
同层协同：infra/logging、infra/audit、infra/security_policy、infra/config、infra/secret。  
下游依赖：platform 文件/进程/时间抽象，contracts 错误语义。  
禁止方向：infra/diagnostics 不反向依赖 runtime/cognition/tools 实现类。

### 1.3 来源依据

1. docs/architecture/DASSALL_Agent_architecture.md（3.4.7、3.7、3.8、5.10、9.5）
2. docs/architecture/DASALL_Engineering_Blueprint.md（3.12、4.2、4.3、5.1）
3. docs/adr/ADR-005-architecture-review-baseline.md
4. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
5. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
6. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
7. docs/plans/DASALL_contracts冻结实施计划.md
8. docs/todos/contracts/DASALL_contracts冻结TODO总表.md
9. docs/architecture/DASALL_infrastructure子系统详细设计.md（6.2、6.3、6.5、6.6、6.8、6.10、7、8）
10. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md（INF-TODO-018、INF-BLK-08）
11. docs/development/DASALL_工程协作与编码规范.md（3.6、3.7）
12. 代码现状：infra/CMakeLists.txt、infra/include/diagnostics/、infra/src/{InfraServiceFacade.cpp,InfraErrorCode.cpp,audit/,plugin/,tracing/}、infra/src/diagnostics（缺失）
13. 行业参考方向：Azure Health Endpoint Monitoring、OpenTelemetry Logs/Traces 关联实践、SRE 诊断快照最小化原则

---

## 2. 约束清单

### 2.1 Must / Should / Must-Not 约束表

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| DIA-C001 | DASSALL_Agent_architecture.md 3.4.7/5.10/9.5 | Must | diagnostics 必须提供故障诊断与快照导出能力 | 子组件/流程 |
| DIA-C002 | DASSALL_Agent_architecture.md 3.7 | Must | infra 不反向依赖业务实现 | 依赖关系 |
| DIA-C003 | DASALL_Engineering_Blueprint.md 4.2 | Must-Not | infra/ -> 任何业务模块为禁止依赖方向 | include/CMake |
| DIA-C004 | DASALL_Engineering_Blueprint.md 4.3 | Must | 跨模块调用通过 contracts 或抽象接口，不直连实现 | 接口语义 |
| DIA-C005 | ADR-005 | Must | 在 contracts 与边界冻结前，不得反向改写主架构 | 设计治理 |
| DIA-C006 | ADR-006 | Must-Not | diagnostics 不接管 ContextPacket 组装与 Prompt 渲染 | 职责边界 |
| DIA-C007 | ADR-007 | Must-Not | diagnostics 不做失败语义判定与恢复裁定，仅输出证据与建议引用 | 异常流程 |
| DIA-C008 | ADR-008 | Must | diagnostics 不拥有全局调度权，仅服务主控链路 | 边界职责 |
| DIA-C009 | DASALL_contracts冻结实施计划.md 5/6 | Must-Not | 不把命令执行器、采样线程、导出后端等实现细节写入 contracts | contracts 对齐 |
| DIA-C010 | DASALL_contracts冻结TODO总表.md M5 | Must | 默认向后兼容；新增字段优先 optional | 版本策略 |
| DIA-C011 | DASALL_工程协作与编码规范.md 3.6 | Must | 失败不可吞没，需日志/指标/审计可观测 | 错误处理 |
| DIA-C012 | DASALL_工程协作与编码规范.md 3.7 | Should | 新增公共接口同步 unit 或 contract 测试 | 测试门禁 |
| DIA-C013 | DASALL_Engineering_Blueprint.md 5.1 | Must | Profile 只能裁剪能力，不能绕过 Audit 与 Runtime 主控链路 | 配置策略 |
| DIA-C014 | DASALL_infrastructure子系统详细设计.md 6.5/6.8/6.10 | Must | DiagnosticsSnapshot 必须脱敏、可导出、含 evidence_ref 引用语义 | 核心对象/可观测 |
| DIA-C015 | DASALL_infrastructure子系统专项TODO.md INF-TODO-018/INF-BLK-08 | Must | 先冻结命令白名单、脱敏规则、导出格式，再进入实现 | 实施顺序 |

### 2.2 约束抽取结论

Must：边界单向依赖、诊断快照可导出且可脱敏、失败可观测、兼容优先。  
Should：接口即测试、诊断链路可注入故障并验证兜底。  
Must-Not：不改 ADR、不污染 contracts、不越权做恢复裁定。

---

## 3. 现状与缺口

### 3.1 现状识别

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| diagnostics 模块可编译接线 | 部分实现 | diagnostics 公共接口已纳入 PUBLIC_HEADER，组件实现目录仍待接入 | High | P0 |
| diagnostics 目录与源码 | 缺失 | infra/src 下没有 diagnostics 目录 | High | P0 |
| diagnostics 对外接口 | 部分实现 | infra/include/diagnostics/IDiagnosticsService.h 已落盘，service runtime 实现仍待补齐 | High | P0 |
| 核心对象 DiagnosticsSnapshot | 部分实现 | DiagnosticsTypes 已冻结快照、查询与导出对象，registry/store/export 实现仍待补齐 | High | P0 |
| 命令白名单与权限校验 | 部分实现 | 只读命令白名单与请求/返回对象已冻结，policy guard 与 registry 实现仍待补齐 | High | P0 |
| 脱敏策略 | 部分实现 | redaction profile 与导出前置守卫已冻结，字段分级矩阵与执行引擎仍待补齐 | High | P0 |
| 导出格式与保留策略 | 部分实现 | 本地导出对象、remote 默认关闭与导出结果已冻结，保留/压缩策略仍待补齐 | Medium | P1 |
| 测试基线 | 部分实现 | unit/contract/integration smoke 基线已接入，failure injection 仍待补齐 | Medium | P0 |

证据：
1. infra/CMakeLists.txt 已接入 core/audit/plugin/tracing 等真实源码。
2. diagnostics 当前已具备公共接口与对象冻结结果，缺口集中在 diagnostics 实现目录。
3. infra/src 现有目录不含 diagnostics。
4. DASALL_infrastructure子系统详细设计.md 已将 diagnostics 标为缺失与 P0（3.1、7、8）。
5. DASALL_infrastructure子系统专项TODO.md 将 diagnostics 对齐到 INF-TODO-018，并受 INF-BLK-08 约束。

### 3.2 现状-目标冲突

| 冲突类型 | 描述 | 影响 | 风险等级 |
|---|---|---|---|
| 边界冲突 | diagnostics 若直接驱动恢复动作，将违反 ADR-007 | 主控链路混乱，恢复责任不清 | High |
| 语义重复 | 若重定义 ErrorInfo/Observation 语义，会与 contracts V1 冲突 | contract 回归失败与返工 | High |
| 依赖反转 | 上层若直接调用具体诊断执行器而非接口 | Profile 裁剪与替换困难 | Medium |

---

## 4. 候选方案对比

### 4.1 候选方案概述

1. 方案 A：单入口同步诊断（execute 直跑命令，原始输出直存）。
2. 方案 B：分层诊断管线（CommandRegistry + PolicyGuard + EvidenceCollector + SnapshotAssembler + ExportManager）。
3. 方案 C：外部诊断代理优先（infra 仅透传请求到 sidecar/agent）。

### 4.2 候选方案对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| A 同步最小方案 | 中 | 中 | 低 | 无白名单与脱敏分层，快照可信度低 | 淘汰：仅适合 PoC |
| B 分层诊断管线 | 高 | 高 | 中 | 组件增多，需要接口治理与测试门禁 | 保留并采纳 |
| C 外部代理优先 | 中 | 中 | 高 | 部署耦合重，edge_minimal 成本高 | 暂不采纳，列为 v2 |

### 4.3 行业方案匹配结论

1. SRE 诊断实践强调“最小足够证据 + 脱敏先行”，适配快照模型分层。
2. OTel 关联实践强调 trace_id/span_id 贯通，适配 diagnostics 与 logging/audit 串联。
3. Azure 运维诊断模式强调命令白名单与导出受控，适配 PolicyGuard 前置校验。

---

## 5. 决策结论

### 5.1 最终选型

采纳方案 B：分层诊断管线。

### 5.2 放弃其他候选方案理由

1. 方案 A：无法满足命令准入、脱敏、导出一致性与可追责要求。
2. 方案 C：当前仓库仍处于骨架阶段，引入外部代理将放大交付复杂度。

### 5.3 与架构、ADR、contracts 一致性

1. 架构一致：diagnostics 保持 Layer 1 基础能力定位。
2. ADR 一致：
   - 不接管 Context/Prompt（ADR-006）。
   - 不做恢复裁定（ADR-007）。
   - 不拥有全局调度（ADR-008）。
3. contracts 一致：仅消费 ResultCode/ErrorInfo/ID 语义，不扩写共享对象。

---

## 6. 详细设计

### 6.1 职责边界

DiagnosticsService 职责：
1. 接收并校验诊断命令请求。
2. 执行白名单命令并采集证据。
3. 组装 DiagnosticsSnapshot 并执行脱敏。
4. 提供快照查询与导出。

DiagnosticsService 非职责：
1. 不判定恢复策略（retry/replan/rollback）。
2. 不推进 runtime 主状态机。
3. 不改写 contracts 公共语义对象。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| DiagnosticsServiceFacade | 对外统一入口与生命周期管理 |
| CommandRegistry | 维护命令白名单与参数 schema |
| CommandPolicyGuard | 基于策略快照做准入校验 |
| CommandExecutor | 执行本地诊断命令/采样任务（受限） |
| EvidenceCollector | 聚合日志片段、指标快照、健康片段、错误摘要 |
| RedactionEngine | 对输出字段执行脱敏和分级过滤 |
| SnapshotAssembler | 生成 DiagnosticsSnapshot 与 metadata |
| SnapshotStore | 快照持久化、索引、保留窗口 |
| ExportManager | 本地/远程导出（受策略控制） |
| DiagnosticsMetricsBridge | 诊断链路指标输出 |
| DiagnosticsAuditBridge | 高风险诊断动作审计写入 |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| DiagnosticsServiceFacade | execute/export/get 请求 | 结果对象 + 错误码 | 成功/失败二值可判定 |
| CommandRegistry | 静态配置 + profile 覆盖 | 命令描述、参数约束 | 非白名单命令不可执行 |
| CommandPolicyGuard | 命令请求 + PolicySnapshot | allow/deny + reason | deny 必带策略引用 |
| CommandExecutor | allow 后命令任务 | 原始输出 + 执行元数据 | 超时/异常结构化返回 |
| EvidenceCollector | 执行元数据 + infra 信号 | EvidenceBundle | 证据来源可追踪 |
| RedactionEngine | 原始输出 + 脱敏规则 | 脱敏输出 | 敏感字段不落盘 |
| SnapshotAssembler | EvidenceBundle + metadata | DiagnosticsSnapshot | snapshot_id 全局唯一 |
| SnapshotStore | Snapshot | 快照索引与内容 | 支持 retention 清理 |
| ExportManager | snapshot_id + export policy | 导出产物 | 远程导出默认禁用 |
| DiagnosticsMetricsBridge | 全链路状态 | metrics | 指标名与标签受控 |
| DiagnosticsAuditBridge | 高风险动作事件 | audit event | 审计失败不可静默 |

### 6.4 子组件依赖关系

1. DiagnosticsServiceFacade -> CommandRegistry -> CommandPolicyGuard。
2. allow 路径：CommandExecutor -> EvidenceCollector -> RedactionEngine -> SnapshotAssembler -> SnapshotStore。
3. export 路径：SnapshotStore -> ExportManager。
4. 观测桥接：CommandExecutor/EvidenceCollector/SnapshotAssembler -> DiagnosticsMetricsBridge + DiagnosticsAuditBridge。
5. PolicyGuard 仅依赖 ISecurityPolicyManager 抽象，不依赖其实现。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | contracts 对齐关系 |
|---|---|---|---|
| DiagnosticsCommand | command_id, command_name, args, request_scope, timeout_ms, actor_ref | command_name 必须白名单命中 | actor_ref 对齐 AgentRequest/WorkerTask 标识 |
| CommandDecision | allowed, reason_code, policy_ref, denied_rule_id | deny 必含 reason_code | reason_code 映射 ResultCode/ErrorInfo |
| CommandCatalog | catalog_id, profile_id, schema_version, entries, generated_at | schema_version 固定为 "1"；entries 仅返回当前 profile 生效白名单；每条 entry 至少包含 command_name、request_scope、arg_schema_ref、arg_schema_summary、read_only | infra 私有目录对象，不进入 contracts |
| ValidationResult | accepted, catalog_ref, matched_command_ref, schema_ref, normalized_command, blocking_errors, warnings, field_paths, result_code | accepted=true 时 normalized_command 必须可直接下传 PolicyGuard；accepted=false 时必须返回 blocking_errors 或 field_paths，且 result_code 仅使用 INF_E_DIAG_COMMAND_INVALID | result_code 映射 ResultCode/ErrorInfo，不新增共享校验对象 |
| EvidenceBundle | logs_ref, metrics_ref, health_ref, errors_ref, artifacts | 仅保存引用与必要摘要 | 引用 Observation 证据语义，不扩写 |
| DiagnosticsSnapshot | snapshot_id, command, collected_at, summary, evidence_refs, redaction_profile, exporter_hint | 必须脱敏、可导出、可追溯 | 与 infra 总设 6.5 对齐，evidence_ref 兼容 |
| SnapshotExportResult | export_id, target, format, size_bytes, checksum, created_at | v1 仅允许 LocalFile + ExportFormat::Json 成功；checksum 固定为 `sha256:<64hex>`；remote 需显式 allow-list | infra 私有对象，不进入 contracts |

### 6.5.1 CommandCatalog / ValidationResult 补充定义

CommandCatalog.entries 的最小公开字段：
1. command_name：与 DiagnosticsCommand.command_name 一致；v1 仅允许 `health.snapshot`、`queue.stats`、`thread.dump` 三个只读命令。
2. request_scope：与 DiagnosticsCommand.request_scope 对齐，用于声明命令适用的 subsystem/instance 范围。
3. arg_schema_ref：指向 profile/config 中的参数 schema 资产或内建 schema 锚点。
4. arg_schema_summary：仅公开 `type`、`required`、`enum`、`default` 等摘要性注解，不内联完整 schema 内容。
5. read_only：v1 固定为 `true`；若出现 `false`，视为未通过 diagnostics design gate。

ValidationResult 的返回语义：
1. `validate()` 只负责白名单命中与参数 schema 静态校验，不承担 PolicyGuard、AuditBridge 或执行器副作用。
2. `accepted=true` 时必须返回 `catalog_ref`、`matched_command_ref`、`schema_ref` 与 `normalized_command`；其中 `normalized_command` 只允许做 `args`、`request_scope`、`timeout_ms` 的规范化，不引入执行结果元数据。
3. `accepted=false` 时必须返回 `blocking_errors` 和/或 `field_paths`；`field_paths` 使用稳定定位符，如 `command_name`、`request_scope`、`timeout_ms`、`args[0]`。
4. `warnings` 只作为非阻断注解，不得单独把 `accepted` 置为 `false`。
5. `result_code` 固定落在 diagnostics 私有错误域，并映射到 contracts::ResultCode::ValidationFieldMissing，不新增共享校验语义对象。

### 6.5.2 allowed_commands 参数 schema 冻结

冻结原则：
1. `CommandRegistry::validate()` 以 `DiagnosticsCommand` 为权威输入模型；v1 参数 schema 只约束 `command_name`、`request_scope`、`args` 与 `timeout_ms`，不反向扩张公共对象。
2. `infra.diagnostics.allowed_commands` 在 v1 只承担 capability gate，Profile/部署层只能裁剪已冻结的内建只读命令集合，不允许内联对象、不允许覆盖 `schema_ref`，也不允许按 profile 改写参数结构。
3. 每条只读命令使用固定 schema 锚点：`schema://diagnostics/<command_name>/v1`；`CommandCatalog.arg_schema_ref` 返回该锚点，`arg_schema_summary` 继续只暴露 discoverability 摘要而不内联完整 schema。
4. `DiagnosticsCommand.args` 采用受限 CLI token 形式：空数组表示“使用默认参数”；布尔 flag 写成 `--flag`；带值参数写成 `--key=value`；不允许 positional args、短参数、重复 token、未知 key 或非 ASCII token。
5. `ValidationResult.field_paths` 继续使用已冻结的稳定定位符 `command_name`、`request_scope`、`timeout_ms`、`args[0]`；若未来需要指向 schema 内部子字段，可在 `arg_schema_ref` 中追加 JSON Pointer fragment，但不得改变公开 `field_paths` 语义。

共用 envelope 约束：
1. 三条 v1 只读命令的 `request_scope` 固定为 `runtime`；scope 不匹配时直接返回 `accepted=false`，`field_paths` 命中 `request_scope`。
2. `timeout_ms` 必须满足 `1 <= timeout_ms <= infra.diagnostics.command.timeout_ms`；默认 cap 为 `3000`，超过 cap 的请求按参数非法处理。
3. `args` 的静态校验只负责 token 语法、数量与数值范围，不承担 queue 是否存在、线程是否可枚举等运行期可用性判断；这类失败仍归 `CommandExecutor`。

| command_name | schema_ref | request_scope | args schema | normalized default | validate 负例锚点 |
|---|---|---|---|---|---|
| `health.snapshot` | `schema://diagnostics/health.snapshot/v1` | `runtime` | `args` 允许为空，或仅允许 1 个 token `--summary`；`minItems=0`、`maxItems=1`、`uniqueItems=true` | 空 `args` 规范化为 `["--summary"]` | `request_scope!=runtime`、`args[0]=--verbose`、`args[1]` 存在 |
| `queue.stats` | `schema://diagnostics/queue.stats/v1` | `runtime` | `args` 允许为空，或仅允许 1 个 token `--queue=<queue_id>`；`queue_id` 语法固定为 `[a-z0-9._-]{1,32}`；`minItems=0`、`maxItems=1`、`uniqueItems=true` | 空 `args` 规范化为 `["--queue=main"]` | `args[0]=main`、`args[0]=--queue=`、`args[1]` 存在 |
| `thread.dump` | `schema://diagnostics/thread.dump/v1` | `runtime` | `args` 允许为空，或仅允许 1 个 token `--limit=<n>`；`n` 必须是十进制整数且满足 `1 <= n <= 32`；`minItems=0`、`maxItems=1`、`uniqueItems=true` | 空 `args` 规范化为 `["--limit=5"]` | `args[0]=5`、`args[0]=--limit=0`、`args[0]=--limit=99` |

补充说明：
1. `health.snapshot` 在 v1 不允许 `--full`、`--component=<id>` 等扩展参数，以保证 `diagnostics_safe_mode` 的最低风险回退面只包含稳定摘要快照。
2. `queue.stats` 的 schema 只冻结 `queue_id` 的语法，不在 registry 层校验队列实例是否存在；若执行期找不到队列，错误归 `INF_E_DIAG_EXEC_FAIL`。
3. `thread.dump` 的默认 `--limit=5` 是输出体积治理的一部分；任何需要更大上限的扩展都必须通过新的 schema version，而不是原位修改 v1。
4. `CommandCatalog.arg_schema_summary` 的 v1 摘要应与上表保持一一对应，例如：
   - `health.snapshot`: `type=array;minItems=0;maxItems=1;token=--summary;default=--summary`
   - `queue.stats`: `type=array;minItems=0;maxItems=1;token=--queue=<queue_id>;pattern=[a-z0-9._-]{1,32};default=--queue=main`
   - `thread.dump`: `type=array;minItems=0;maxItems=1;token=--limit=<1..32>;default=--limit=5`

### 6.5.3 Redaction profile / deny-list 冻结

冻结原则：
1. `RedactionEngine` 的首版职责限定为“对可落盘/可导出的 diagnostics snapshot 执行字段过滤与字符串脱敏”，不引入新的 contracts 对象，也不让 store/export 组件各自复制脱敏逻辑。
2. `infra.diagnostics.redaction.profile` 在 v1 仅允许 `strict` 与 `compat` 两档；未命中这两档的 profile 视为配置错误，必须返回 `INF_E_DIAG_REDACTION_FAIL`。
3. v1 deny-list 以不区分大小写的 ASCII token 匹配为准，最小集合冻结为：`secret`、`password`、`token`、`authorization`、`cookie`、`apikey`、`credential`。
4. deny-list 只作用于 string/string-like 字段：`command.actor_ref`、`command.args[*]`、`summary`、`exporter_hint` 与 `evidence_refs[*]` 的尾部片段；scheme 本身如 `logs://`、`metrics://`、`health://`、`errors://`、`command://`、`snapshot://`、`export://`、`error://` 不参与敏感词判定。
5. 若 `evidence_refs` 中出现 `raw://`、`inline://`、`data:` 这类携带原始内容的引用，或发现未知 scheme，v1 一律按 redaction failure 处理，而不是尝试“猜测性脱敏”。

字段分级矩阵：

| 字段 | 字段等级 | strict | compat | 失败条件 |
|---|---|---|---|---|
| `snapshot_id` | S0 结构锚点 | 保留原值 | 保留原值 | 空值不属于 redaction failure，由对象校验处理 |
| `command.command_name` | S0 结构锚点 | 保留原值 | 保留原值 | 非白名单命令不进入 redaction success 路径 |
| `command.request_scope` / `command.timeout_ms` / `command.command_id` | S0 结构锚点 | 保留原值 | 保留原值 | 无 |
| `command.actor_ref` | S2 身份敏感 | 固定写成 `actor://redacted` | 固定写成 `actor://redacted` | 若字段缺失则直接写固定占位，不单独失败 |
| `command.args` | S1 操作上下文 | 仅保留 schema 默认值或安全 flag：`health.snapshot -> ["--summary"]`、`queue.stats -> ["--queue=redacted"]`、`thread.dump -> ["--limit=5"]` | 保留已通过 schema 校验的 token；若 token 命中 deny-list，则把 value 部分替换为 `redacted` | 出现 positional arg、未知 key 或 deny-list 命中后无法稳定重写时失败 |
| `summary` | S1 可分享摘要 | 固定重写为命令级 canonical summary：`health.snapshot -> diagnostics redacted health snapshot`、`queue.stats -> diagnostics redacted queue stats`、`thread.dump -> diagnostics redacted thread dump` | 保留 executor summary；若命中 deny-list，命中片段替换为 `[REDACTED]` | summary 为空或替换后仍残留 deny-list token 时失败 |
| `evidence_refs` | S0/S1 引用锚点 | 仅允许保留受控 scheme 的 ref；不改 scheme，只允许对尾部敏感 token 做 `[REDACTED]` 替换 | 与 strict 相同 | 出现 `raw://`、`inline://`、`data:` 或未知 scheme 时失败 |
| `redaction_profile` | S0 策略锚点 | 固定写 `strict` | 固定写 `compat` | profile 非 strict/compat 时失败 |
| `exporter_hint` | S1 导出提示 | 仅允许 `local_file` | 仅允许 `local_file` | 任意 remote/export target hint 直接失败 |

补充约束：
1. `strict` 的目标是把可共享输出收敛到命令级摘要与受控 ref，因此不允许把 queue 名、actor 身份或原始 summary 片段原样落盘。
2. `compat` 的目标是保持调试可用性，但只能在已通过白名单 schema 的参数面内保留 token；它不是“跳过脱敏”。
3. `RedactionEngine` 失败时不得降级为“未脱敏继续落盘”；主链必须返回 `INF_E_DIAG_REDACTION_FAIL`，并阻断 store/export。
4. `DiagnosticsSnapshot.redaction_profile` 继续作为最终 snapshot 的权威落盘值；若 redaction 成功，该值必须与实际应用的 profile 一致。

### 6.5.4 Export format / checksum / allowed_targets 冻结

冻结原则：
1. 不新增新的公开导出枚举；v1 继续使用已冻结的 `ExportTarget` / `ExportFormat`。其中 `ExportFormat::Json` 在 diagnostics 导出语义上固定映射为 UTF-8 JSON Lines（`.jsonl`）输出，而不是任意 JSON blob；`ExportFormat::TextArchive` 保留枚举值，但在 v1 必须返回 `INF_E_DIAG_EXPORT_FAIL`。
2. 本地导出是 v1 唯一允许成功的路径：`request.target == LocalFile`、`request.format == Json`、`request.target_ref` 命中受控本地目标规则后，`ExportManager` 才能生成导出产物。
3. JSON Lines 约束冻结为：UTF-8 编码、每一行都是合法 JSON value、以 `\n` 作为行终止符；对 diagnostics v1，本地导出产物固定为单 snapshot 对象一行，因此推荐并要求 `.jsonl` 后缀。
4. `SnapshotExportResult.checksum` 的 v1 语义固定为对“最终写出的确切字节串”计算 `sha256`，并以 `sha256:<64 lowercase hex>` 返回；不允许 `md5`、`sha1` 或无算法前缀摘要。
5. 远程导出仍保留在公开请求模型中，但主链必须先过 gate：当 `infra.diagnostics.remote.enabled=false` 时，任何 `RemoteUpload` 请求都必须在发起网络 IO 前返回 `INF_E_DIAG_REMOTE_EXPORT_DISABLED`；当显式启用远程导出时，`request.target_ref` 还必须与 `infra.diagnostics.remote.allowed_targets` 中的某个条目做 exact match。
6. `infra.diagnostics.remote.allowed_targets` 的 v1 条目固定为完全限定的 `https://` endpoint ref；不允许 wildcard、prefix match、查询串、fragment、内嵌凭据或运行时拼接 host/path。Gatekeeper/ExportManager 只做“是否精确命中 allow-list”的判定，不做宽松匹配。

target / format / gate 矩阵：

| target | format | v1 结果 | 约束 |
|---|---|---|---|
| `LocalFile` | `Json` | 允许成功 | `target_ref` 必须匹配 `local://diagnostics/<artifact_name>.jsonl`；`artifact_name` 仅允许 `[a-z0-9._-]{1,128}` |
| `LocalFile` | `TextArchive` | 拒绝 | 返回 `INF_E_DIAG_EXPORT_FAIL`；v1 不支持 tar/zip/text bundle |
| `RemoteUpload` | `Json` | 默认拒绝 | `remote.enabled=false` 时返回 `INF_E_DIAG_REMOTE_EXPORT_DISABLED`；启用后仍需 exact-match `allowed_targets` |
| `RemoteUpload` | `TextArchive` | 拒绝 | v1 不支持 |

补充约束：
1. 本地 `target_ref` 只接受 `local://diagnostics/` scheme；不允许 `..`、重复斜杠、query、fragment 或隐式路径跳转。
2. `ExportManager` 在写出本地产物前必须先从 `SnapshotStore` 读取已脱敏 snapshot，再按固定 JSON Lines 编码序列化；不得直接从 executor/evidence 原始输出拼装导出文件。
3. 远程导出即便在未来启用，也必须沿用“先 exact-match allow-list，再上传”的 gatekeeper 语义；`allowed_targets` 是发布时冻结的 endpoint ref 列表，不是用户可自由拼接的 URL 模板。
4. diagnostics v1 的 `ExportFormat::Json` 与 `.jsonl` 映射只对 diagnostics 模块生效；若未来需要普通 `.json` 单对象文件或压缩包格式，应通过新的 design gate 扩展，而不是改写现有 `Json` 语义。

### 6.6 核心接口语义定义

建议头文件分布：infra/include/diagnostics/

1. IDiagnosticsService
   - execute(const DiagnosticsCommand&) -> DiagnosticsSnapshotResult
   - get_snapshot(const SnapshotQuery&) -> DiagnosticsSnapshotResult
   - export_snapshot(const SnapshotExportRequest&) -> SnapshotExportResult

2. IDiagnosticsCommandRegistry
   - list_commands() -> CommandCatalog
   - validate(const DiagnosticsCommand&) -> ValidationResult

3. IDiagnosticsPolicyGuard
   - authorize(const DiagnosticsCommand&, const InfraContext&) -> CommandDecision

前置条件：
1. 服务 init/start 完成。
2. 策略快照可用（不可用则进入受限模式）。
3. 命令命中白名单且参数通过校验。

后置条件：
1. 允许执行时必须产出 snapshot_id 或明确错误码。
2. 拒绝执行时必须产出 deny reason 与 policy_ref。
3. 高风险诊断动作必须写审计。

错误语义（infra 私有错误码域，映射 contracts::ResultCode）：
1. INF_E_DIAG_COMMAND_DENIED
2. INF_E_DIAG_COMMAND_INVALID
3. INF_E_DIAG_EXEC_TIMEOUT
4. INF_E_DIAG_EXEC_FAIL
5. INF_E_DIAG_REDACTION_FAIL
6. INF_E_DIAG_SNAPSHOT_STORE_FAIL
7. INF_E_DIAG_EXPORT_FAIL
8. INF_E_DIAG_REMOTE_EXPORT_DISABLED

### 6.7 主流程时序（正常）

1. 调用方提交 DiagnosticsCommand。
2. CommandRegistry 校验命令与参数。
3. PolicyGuard 做准入判定。
4. CommandExecutor 在受限沙箱/受限执行器中执行。
5. EvidenceCollector 聚合日志、指标、健康与错误摘要。
6. RedactionEngine 执行脱敏。
7. SnapshotAssembler 组装 DiagnosticsSnapshot。
8. SnapshotStore 持久化并返回 snapshot_id。
9. 调用方按需执行 export_snapshot。

### 6.8 异常与恢复时序

异常分类：
1. 准入拒绝：命令不在白名单或策略拒绝。
2. 执行失败：命令超时、执行器异常、资源不可用。
3. 脱敏失败：规则加载失败或字段处理失败。
4. 存储/导出失败：快照持久化失败、导出目标不可用。

恢复动作：
1. 准入拒绝：返回 INF_E_DIAG_COMMAND_DENIED，记录审计与计数。
2. 执行失败：返回结构化错误，保留失败证据最小集。
3. 脱敏失败：禁止导出原始内容，返回 INF_E_DIAG_REDACTION_FAIL。
4. 导出失败：本地保留快照并返回 INF_E_DIAG_EXPORT_FAIL。

兜底策略：
1. 连续失败阈值触发 diagnostics_safe_mode，仅允许只读低风险命令。
2. 远程导出默认关闭；未显式启用时一律拒绝。

### 6.9 配置项与默认策略

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| infra.diagnostics.enabled | true | 默认/Profile/部署 | 诊断模块开关 |
| infra.diagnostics.allowed_commands | ["health.snapshot","queue.stats","thread.dump"] | Profile/部署 | 只允许从 6.5.2 已冻结的内建 schema 集合中裁剪命令；v1 不允许 profile 改写参数 schema |
| infra.diagnostics.command.timeout_ms | 3000 | Profile/部署 | 单命令超时上限；registry validate 拒绝超过该上限的请求 |
| infra.diagnostics.max_artifact_bytes | 1048576 | Profile/部署 | 单快照最大证据大小 |
| infra.diagnostics.redaction.profile | strict | Profile/部署 | strict/compat |
| infra.diagnostics.snapshot.retention_days | 7 | Profile/部署 | 快照保留 |
| infra.diagnostics.snapshot.max_count | 500 | Profile/部署 | 快照数量上限 |
| infra.diagnostics.export.local.enabled | true | 默认/Profile | 本地导出开关；v1 仅允许 `ExportFormat::Json`，并固定写为 UTF-8 JSON Lines |
| infra.diagnostics.remote.enabled | false | Profile/部署 | 远程导出开关 |
| infra.diagnostics.remote.allowed_targets | [] | Profile/部署 | 远程导出目标白名单；v1 条目必须为 exact-match `https://` endpoint ref，不允许 wildcard/query/fragment/内嵌凭据 |
| infra.diagnostics.safe_mode.failure_threshold | 5 | Profile/部署 | 连续失败阈值 |

### 6.10 可观测性设计

日志点：
1. execute/get/export 生命周期。
2. 命令拒绝原因、策略引用。
3. 执行超时、脱敏失败、导出失败。
4. safe_mode 进入/退出。

指标：
1. infra_diag_command_total{command,outcome}
2. infra_diag_command_denied_total{reason}
3. infra_diag_exec_latency_ms{command}
4. infra_diag_snapshot_store_fail_total
5. infra_diag_export_total{target,outcome}
6. infra_diag_redaction_fail_total
7. infra_diag_safe_mode_enter_total

追踪：
1. 每次 execute 创建 span，属性包含 command_name、snapshot_id、denied_reason。
2. 与日志/audit 使用 trace_id/span_id 关联。

审计：
1. 高风险动作（远程导出、扩展命令执行）强制审计。
2. 审计字段至少包含 actor、action、target、outcome、evidence_ref。

### 6.10.1 Metrics / Audit bridge sink contract 冻结

Metrics bridge 冻结原则：
1. `DiagnosticsMetricsBridge` v1 只允许依赖 `std::shared_ptr<metrics::IMetricsProvider>`，并通过 `IMetricsProvider -> IMeter -> record(MetricSample)` 发射指标；diagnostics 不得直连 `IMetricExporter`、provider 具体实现或再造第二套私有 exporter sink。
2. meter scope 固定为 `infra.diagnostics` / `v1`；`MetricLabels` 继续严格受 metrics 子域五元标签白名单约束，只允许 `module/stage/profile/outcome/error_code`。
3. diagnostics 设计 6.10 中的 `{command}`、`{reason}`、`{target}` 维度在 v1 统一投影到现有 allowlist：命令与导出目标投影到 `labels.stage`，拒绝原因与 diagnostics 错误码投影到 `labels.error_code`；不新增 `command`、`reason`、`target` 自定义标签。
4. `labels.module` 固定为 `diagnostics`；`labels.profile` 复用当前 diagnostics profile id；`labels.outcome` v1 只允许 `success`、`failure`、`rejected`、`degraded`。
5. `labels.stage` v1 只允许以下固定值：`execute.health_snapshot`、`execute.queue_stats`、`execute.thread_dump`、`authorize`、`redaction`、`store`、`export.local_file`、`export.remote_upload`、`safe_mode`。
6. `labels.error_code` v1 只允许 `none`、`diag_command_denied`，以及 diagnostics 错误码名 `INF_E_DIAG_COMMAND_DENIED`、`INF_E_DIAG_COMMAND_INVALID`、`INF_E_DIAG_EXEC_TIMEOUT`、`INF_E_DIAG_EXEC_FAIL`、`INF_E_DIAG_REDACTION_FAIL`、`INF_E_DIAG_SNAPSHOT_STORE_FAIL`、`INF_E_DIAG_EXPORT_FAIL`、`INF_E_DIAG_REMOTE_EXPORT_DISABLED`。
7. metrics bridge failure 继续遵循现有 infra bridge 模式：provider not ready、identity invalid、config invalid、export failure / timeout 进入 `bridge_degraded`；label/cardinality/queue 类失败只做 best-effort 可观测，不递归反噬 diagnostics 主链结果。

### 6.10.1.1 Diagnostics metrics family 映射

| metric family | type | stage 投影 | outcome 语义 | error_code 投影 |
|---|---|---|---|---|
| `infra_diag_command_total` | Counter | `execute.<command>` | `success` / `failure` / `rejected` | 成功为 `none`；拒绝/失败时投影 diagnostics 错误码 |
| `infra_diag_command_denied_total` | Counter | `authorize` | `rejected` | 投影 `diag_command_denied` |
| `infra_diag_exec_latency_ms` | Histogram | `execute.<command>` | `success` / `failure` | 成功为 `none`；失败时投影 `INF_E_DIAG_EXEC_TIMEOUT` 或 `INF_E_DIAG_EXEC_FAIL` |
| `infra_diag_snapshot_store_fail_total` | Counter | `store` | `failure` | 固定 `INF_E_DIAG_SNAPSHOT_STORE_FAIL` |
| `infra_diag_export_total` | Counter | `export.local_file` / `export.remote_upload` | `success` / `failure` / `rejected` | 成功为 `none`；拒绝/失败时投影导出相关 diagnostics 错误码 |
| `infra_diag_redaction_fail_total` | Counter | `redaction` | `failure` | 固定 `INF_E_DIAG_REDACTION_FAIL` |
| `infra_diag_safe_mode_enter_total` | Counter | `safe_mode` | `degraded` | `none` |

Audit bridge 冻结原则：
1. `DiagnosticsAuditBridge` v1 只允许依赖 `std::shared_ptr<audit::IAuditLogger>`，并复用既有 `AuditEvent`、`AuditContext`、`AuditWriteOutcome`；diagnostics 不新增 bridge 专属公共审计对象。
2. v1 强制审计动作固定为 `diagnostics.remote_export` 与 `diagnostics.command_extension`；前者对应 `RemoteUpload` 请求，后者为未来非白名单扩展命令预留，不允许在 022 中扩张为新的执行器能力。
3. `AuditEvent.action` 固定使用上述 action 名；`AuditEvent.target` 分别映射为 `diagnostics.export:<target_ref>` 与 `diagnostics.command:<command_name>`；`AuditEvent.actor` 默认使用 `actor://redacted` 或调用方显式传入的受控 actor ref，不恢复未脱敏身份。
4. `AuditEvent.evidence_ref.kind` 固定为 `AuditEvidenceKind::ToolResult`；`ref` 对 remote export 使用 `snapshot://<snapshot_id>`，对扩展命令使用 `command://<command_id>`。
5. `AuditEvent.side_effects` v1 只允许可序列化稳定事实：`target_ref:<...>`、`format:json`、`result_code:<...>`、`detail_ref:<...>`、`request_scope:<...>`；不得写入原始 payload、secret、query string 或 diagnostics 内部对象 dump。
6. `AuditContext` 只复用现有 `request_id/session_id/trace_id/task_id/parent_task_id/lease_id/worker_type`；未知值继续填 `unknown`，不要求 diagnostics 自己发明第二套上下文容器。
7. audit failure semantics 固定为 required sink：缺少 `IAuditLogger`、`write_audit()` 返回失败或不一致状态时，`DiagnosticsAuditBridge` 必须返回显式 failure，并阻断对应高风险动作；不得降级为“继续执行但只写日志”。

### 6.10.1.2 Diagnostics audit action / outcome 映射

| 高风险动作 | action | target | outcome 映射 | evidence_ref |
|---|---|---|---|---|
| `RemoteUpload` 导出请求 | `diagnostics.remote_export` | `diagnostics.export:<target_ref>` | 远程 gate 拒绝 -> `Rejected`；远程写出成功 -> `Succeeded`；导出 backend / checksum / target 校验失败 -> `Failed` | `snapshot://<snapshot_id>` |
| 非白名单扩展命令执行请求 | `diagnostics.command_extension` | `diagnostics.command:<command_name>` | policy / capability gate 拒绝 -> `Rejected`；执行成功 -> `Succeeded`；执行器或审计前置失败 -> `Failed` | `command://<command_id>` |

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 冻结诊断统一入口 | 新增 IDiagnosticsService 接口 | 先锁定边界，避免实现漂移 | infra/include/diagnostics/IDiagnosticsService.h | unit: DiagnosticsServiceInterfaceTest | cmake --build build-ci --target dasall_infra | 依赖 INF-M1 接口冻结 |
| 冻结快照对象 | 新增 DiagnosticsSnapshot 与命令对象 | 先对象后实现，满足 contracts 边界 | infra/include/diagnostics/DiagnosticsTypes.h | unit: DiagnosticsTypesTest; contract: DiagnosticsBoundaryContractTest | ctest --test-dir build-ci -R "DiagnosticsTypesTest|DiagnosticsBoundaryContractTest" | 依赖 contracts V1 Ready |
| 冻结 registry 目录与校验返回边界 | 补齐 CommandCatalog 与 ValidationResult 设计 | 先锁定 list_commands/validate 的结果语义，再推进 IDiagnosticsCommandRegistry 头文件 | docs/architecture/DASALL_infra_diagnostics模块详细设计.md；后续 infra/include/diagnostics/IDiagnosticsCommandRegistry.h | process gate: DiagnosticsCommandRegistryTest | rg -n "CommandCatalog|ValidationResult|arg_schema_ref|field_paths" docs/architecture/DASALL_infra_diagnostics模块详细设计.md | 依赖只读命令白名单已完成；完整 allowed_commands 参数 schema 已由 D-BLK-01 解阻 |
| 冻结 allowed_commands v1 参数 schema | 补齐 `health.snapshot`、`queue.stats`、`thread.dump` 的 schema ref、args grammar 与 normalized defaults | 直接解除 CommandRegistry 的 validate 语义阻塞，避免实现期靠猜测扩张 schema | docs/architecture/DASALL_infra_diagnostics模块详细设计.md；docs/todos/infrastructure/deliverables/DIA-BLK-003-allowed_commands参数schema收敛.md | process gate: DiagnosticsCommandRegistryTest；TODO/worklog sync | rg -n "schema://diagnostics/health.snapshot/v1|schema://diagnostics/queue.stats/v1|schema://diagnostics/thread.dump/v1" docs/architecture/DASALL_infra_diagnostics模块详细设计.md | 无（2026-04-07 已完成 D-BLK-01 / DIA-BLK-003 解阻） |
| 建立命令准入链路 | 新增 CommandRegistry + PolicyGuard 骨架 | 解决 INF-BLK-08 的白名单与准入问题 | infra/src/diagnostics/CommandRegistry.cpp; infra/src/diagnostics/CommandPolicyGuard.cpp | unit: DiagnosticsCommandPolicyTest | ctest --test-dir build-ci -R DiagnosticsCommandPolicyTest | 阻塞：策略 schema 冻结 |
| 建立证据与脱敏链路 | 新增 EvidenceCollector + RedactionEngine | 满足“先脱敏再存储导出”硬约束 | infra/src/diagnostics/EvidenceCollector.cpp; infra/src/diagnostics/RedactionEngine.cpp | unit: DiagnosticsRedactionTest; failure: DiagnosticsRedactionFailureTest | ctest --test-dir build-ci -R "DiagnosticsRedactionTest|DiagnosticsRedactionFailureTest" | 阻塞：脱敏规则冻结 |
| 建立快照存储与导出 | 新增 SnapshotStore + ExportManager | 形成可验证导出闭环 | infra/src/diagnostics/SnapshotStore.cpp; infra/src/diagnostics/ExportManager.cpp | unit: DiagnosticsSnapshotStoreTest; integration: InfraDiagnosticsIntegrationTest | ctest --test-dir build-ci -R "DiagnosticsSnapshotStoreTest|InfraDiagnosticsIntegrationTest" | 阻塞：导出格式与目标白名单 |
| 建立可观测桥接 | 新增 Metrics/Audit Bridge | 保障失败可观测与可审计 | infra/src/diagnostics/DiagnosticsMetricsBridge.cpp; infra/src/diagnostics/DiagnosticsAuditBridge.cpp | unit: DiagnosticsMetricsAuditBridgeTest | ctest --test-dir build-ci -R DiagnosticsMetricsAuditBridgeTest | 依赖 metrics/audit 接口可用 |
| 接入 infra 构建与测试门禁 | 增加 diagnostics CMake 与测试注册 | 保证模块可持续集成 | infra/CMakeLists.txt; tests/unit/infra/diagnostics/; tests/contract/ | build + unit + contract + integration | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -L "unit|contract" | 依赖 tests 顶层标签治理 |

不可立即映射项：
1. 远程诊断多目标上传与断点续传：当前非最小交付，保留为 v2。
2. 外部 sidecar 诊断代理：受部署与安全基线约束，后置。

---

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

建议目录：
1. infra/include/diagnostics/
2. infra/src/diagnostics/
3. tests/unit/infra/diagnostics/
4. tests/contract/infra/diagnostics/
5. tests/integration/infra/diagnostics/

建议文件：
1. infra/include/diagnostics/IDiagnosticsService.h
2. infra/include/diagnostics/DiagnosticsTypes.h
3. infra/include/diagnostics/DiagnosticsErrors.h
4. infra/src/diagnostics/DiagnosticsServiceFacade.cpp
5. infra/src/diagnostics/CommandRegistry.cpp
6. infra/src/diagnostics/CommandPolicyGuard.cpp
7. infra/src/diagnostics/EvidenceCollector.cpp
8. infra/src/diagnostics/RedactionEngine.cpp
9. infra/src/diagnostics/SnapshotStore.cpp
10. infra/src/diagnostics/ExportManager.cpp

### 8.2 分阶段实施与完成判定

| 阶段 | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| DIA-M1 接口与对象冻结 | Not Started | 冻结 IDiagnosticsService、DiagnosticsTypes、错误码域 | 详细设计 6.5/6.6 + contracts 计划 | infra/include/diagnostics/*.h | unit + contract | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -L "unit|contract" | 头文件齐备且边界测试通过 |
| DIA-M2 准入与执行骨架 | Not Started | 落盘 CommandRegistry/PolicyGuard/Executor 骨架 | 详细设计 6.2/6.3/6.7 | infra/src/diagnostics/Command*.cpp | unit | ctest --test-dir build-ci -R DiagnosticsCommandPolicyTest | 白名单拒绝与允许路径均可判定 |
| DIA-M3 证据与快照闭环 | Not Started | 落盘 EvidenceCollector/Redaction/SnapshotStore | 详细设计 6.8/6.9 | infra/src/diagnostics/Evidence*.cpp infra/src/diagnostics/Snapshot*.cpp | unit + failure injection | ctest --test-dir build-ci -R "DiagnosticsRedaction|DiagnosticsSnapshot" | 脱敏失败被阻断且快照可保存 |
| DIA-M4 导出与桥接 | Not Started | 落盘 ExportManager + Metrics/Audit Bridge | 详细设计 6.10 | infra/src/diagnostics/ExportManager.cpp + Bridges | unit + integration | ctest --test-dir build-ci -R "InfraDiagnosticsIntegrationTest|DiagnosticsMetricsAuditBridgeTest" | 导出失败可观测且审计完整 |
| DIA-M5 Gate 收口 | Not Started | 注册 tests 标签并接入 CI 门禁 | 工程规范 3.7 + infra 专项 TODO | tests/* + scripts/ci/* | unit/contract/integration/failure | ctest --test-dir build-ci -L "infra" | Gate 可重复执行并通过 |

### 8.3 原子实施任务（建议级）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| DIA-T001 | Not Started | 新增 IDiagnosticsService 接口头文件 | infra 总设 6.6 | infra/include/diagnostics/IDiagnosticsService.h | InterfaceCompileTest | cmake --build build-ci --target dasall_infra | 接口方法与语义一致且可编译 |
| DIA-T002 | Not Started | 新增 DiagnosticsTypes 核心对象 | infra 总设 6.5 | infra/include/diagnostics/DiagnosticsTypes.h | DiagnosticsTypesTest | ctest --test-dir build-ci -R DiagnosticsTypesTest | 字段完整且含 evidence_refs |
| DIA-T003 | Not Started | 新增 DiagnosticsErrors 错误码域 | 工程规范 3.6 | infra/include/diagnostics/DiagnosticsErrors.h | DiagnosticsErrorMappingContractTest | ctest --test-dir build-ci -R DiagnosticsErrorMappingContractTest | 错误码映射稳定且不越权 |
| DIA-T004 | Not Started | 新增 CommandRegistry 骨架 | 本设计 6.2/6.3 | infra/src/diagnostics/CommandRegistry.cpp | DiagnosticsCommandRegistryTest | ctest --test-dir build-ci -R DiagnosticsCommandRegistryTest | 非白名单命令拒绝 |
| DIA-T005 | Not Started | 新增 CommandPolicyGuard 骨架 | 本设计 6.3/6.4 | infra/src/diagnostics/CommandPolicyGuard.cpp | DiagnosticsCommandPolicyTest | ctest --test-dir build-ci -R DiagnosticsCommandPolicyTest | deny 返回策略引用 |
| DIA-T006 | Not Started | 新增 RedactionEngine 骨架 | 本设计 6.8/6.9 | infra/src/diagnostics/RedactionEngine.cpp | DiagnosticsRedactionTest | ctest --test-dir build-ci -R DiagnosticsRedactionTest | 敏感字段不落盘 |
| DIA-T007 | Not Started | 新增 SnapshotStore 与 ExportManager 骨架 | 本设计 6.7/6.8 | infra/src/diagnostics/SnapshotStore.cpp; infra/src/diagnostics/ExportManager.cpp | DiagnosticsSnapshotStoreTest; DiagnosticsExportTest | ctest --test-dir build-ci -R "DiagnosticsSnapshotStoreTest|DiagnosticsExportTest" | 本地导出可成功且失败可回报 |
| DIA-T008 | Not Started | 接线 diagnostics 到 infra CMake | 代码现状 + 本设计 8.1 | infra/CMakeLists.txt | build smoke | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | diagnostics 文件入图且可编译 |
| DIA-T009 | Not Started | 注册 diagnostics unit/contract/integration 测试入口 | 工程规范 3.7 | tests/unit/infra/diagnostics/*; tests/contract/*; tests/integration/* | ctest 标签覆盖 | ctest --test-dir build-ci -N && ctest --test-dir build-ci -L "unit|contract|integration" | 测试可发现并执行 |
| DIA-T010 | Not Started | 回写 infra 专项 TODO 对齐证据 | infra 专项 TODO 6/7/8/9 章 | docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md | process gate | rg -n "INF-TODO-018|INF-BLK-08|Diagnostics" docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md | 至少回写状态、命令证据、阻塞变化 |

### 8.4 与 infra 总设和专项 TODO 对齐检查

| 对齐项 | 来源 | 本文落点 | 结论 |
|---|---|---|---|
| DiagnosticsService 必须独立组件化 | DASALL_infrastructure子系统详细设计.md 6.11 | 6.2 子组件清单、8.2 M2-M4 | 已对齐 |
| DiagnosticsSnapshot 字段与 evidence_ref 语义 | DASALL_infrastructure子系统详细设计.md 6.5 | 6.5 核心对象表 | 已对齐 |
| IDiagnosticsService execute/export 语义 | DASALL_infrastructure子系统详细设计.md 6.6 | 6.6 接口定义 | 已对齐 |
| Design->Build 对应“诊断命令与快照能力” | DASALL_infrastructure子系统详细设计.md 7 | 7 映射表 | 已对齐 |
| 里程碑 INF-M3（配置/策略/诊断闭环） | DASALL_infrastructure子系统详细设计.md 8.2 | 8.2 DIA-M2~M3 | 已对齐 |
| 原子任务 INF-T010 诊断接口与导出 | DASALL_infrastructure子系统详细设计.md 8.3 | 8.3 DIA-T001/T002/T007 | 已对齐 |
| 专项 TODO INF-TODO-018 与 INF-BLK-08 | DASALL_infrastructure子系统专项TODO.md | 2.1 DIA-C015、8.3 DIA-T010 | 已对齐 |

---

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试层 | 覆盖对象 | 关键用例 | 通过标准 |
|---|---|---|---|
| Unit | Registry/PolicyGuard/Redaction/SnapshotStore/ExportManager | 白名单拒绝、参数非法、脱敏成功/失败、存储失败、导出失败 | 所有断言通过且错误码明确 |
| Contract | DiagnosticsTypes 与 contracts 边界 | ResultCode/ErrorInfo 映射稳定；evidence_ref 不越权 | 无越权字段与语义漂移 |
| Integration | Diagnostics 与 logging/audit/health 协同 | execute->snapshot->export 全链路；审计桥接 | 关键链路可重复执行 |
| Failure Injection | timeout、redaction_fail、store_fail、export_fail | 兜底动作与可观测证据齐备 | 每类故障有日志+指标+错误码 |
| Compatibility | Profile 差异 | desktop_full/edge_balanced/edge_minimal 诊断行为一致性 | 不出现 breaking 行为 |

### 9.2 质量 Gate 建议

| Gate ID | 检查项 | 失败判定 |
|---|---|---|
| DIA-G1 | diagnostics 单元测试全绿 | 任一 unit 失败即阻断 |
| DIA-G2 | diagnostics 契约边界测试通过 | 出现越权字段或错误码漂移即阻断 |
| DIA-G3 | 故障注入关键用例通过 | 任一故障无兜底动作即阻断 |
| DIA-G4 | 导出安全门通过 | 远程导出禁用策略失效或脱敏失败未阻断即阻断 |
| DIA-G5 | Profile 兼容检查通过 | 任一 profile 行为偏离策略基线即阻断 |

---

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | runtime/apps/ops 调用方 | 新增接口 + 默认实现，旧调用通过 Facade 兼容 | 先 desktop_full，再 edge_balanced，最后 edge_minimal | 预留远程导出、多后端存储、sidecar 代理 |
| Medium（若改命令对象字段） | 所有 diagnostics 调用方 | v1/v2 命令对象并存 + 适配器 | 双写 snapshot 元数据后切换 | 预留 command schema_version |

演进原则：
1. 新增字段优先 optional，保持后向兼容。
2. breaking 变更必须经 ADR/评审并给出迁移窗口。
3. profile 先灰度后全量。

---

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| D-BLK-01 已解阻（2026-04-07）：6.5.2 已冻结 `health.snapshot`、`queue.stats`、`thread.dump` 的 schema_ref、`request_scope=runtime`、args token grammar 与 normalized default；Profile 只允许裁剪命令集合，不允许改写参数 schema | DIA-T004/T005 | 无；后续仅需保持 diagnostics 详细设计、专项 TODO 与 deliverable 口径同步 | 证据回链到 6.5.2 与 docs/todos/infrastructure/deliverables/DIA-BLK-003-allowed_commands参数schema收敛.md | 若后续实现重新允许 profile 内联 schema、放开变更型命令或改变 v1 参数结构，则重新转为 Blocked |
| D-BLK-02 已解阻（2026-04-07）：6.5.3 已冻结 strict/compat、字段分级矩阵、deny-list 与 redaction failure 兜底；RedactionEngine 可直接按该矩阵实现最小骨架 | DIA-T006/T007 | 无；后续仅需保持详细设计、deliverable 与 TODO/worklog 口径同步 | 证据回链到 6.5.3 与 docs/todos/infrastructure/deliverables/DIA-BLK-004-Redaction规则矩阵收敛.md | 若后续实现允许未脱敏 snapshot 落盘、放开未知 evidence scheme，或把 compat 扩成“原样透传”，则重新转为 Blocked |
| D-BLK-03 已解阻（2026-04-07）：6.5.4 已冻结 `ExportFormat::Json -> UTF-8 JSON Lines`、`checksum=sha256:<64hex>`、本地 `target_ref` 规则与 remote exact-match `allowed_targets`；`ExportManager` 可直接按该矩阵实现最小骨架 | DIA-T007 | 无；后续仅需保持详细设计、deliverable 与 TODO/worklog 口径同步 | 证据回链到 6.5.4 与 docs/todos/infrastructure/deliverables/DIA-BLK-005-导出格式与目标策略冻结.md | 若后续实现放开 `TextArchive`、接受非 `sha256` checksum，或把 remote allow-list 做成 prefix/wildcard，则重新转为 Blocked |
| D-BLK-04 metrics/audit 最小接口未冻结 | DIA-T007/T009 | metrics/audit 桥接接口签名冻结 | 先使用最小适配器接口 | 降级为日志+错误码观测 |
| D-BLK-05 tests/integration 拓扑未稳 | DIA-T009 | tests 顶层集成注册策略明确 | 先保证 unit/contract gate | 延后 integration gate 到下一迭代 |

### 11.2 风险清单

| 风险 | 等级 | 触发条件 | 缓解动作 |
|---|---|---|---|
| 诊断命令越权执行 | High | 未做白名单或策略校验 | 强制 PolicyGuard 前置并做 contract 测试 |
| 脱敏失效导致敏感信息泄露 | High | 规则缺失或绕过 RedactionEngine | 导出前强制 redaction gate |
| 快照膨胀导致资源超限 | Medium | 单快照无大小限制 | max_artifact_bytes + retention 限制 |
| 远程导出误开 | Medium | profile 配置漂移 | remote.enabled 默认 false + 审计告警 |

---

## 12. 未决问题与后续任务

### 12.1 未决问题

1. 诊断命令域是否分层为只读诊断与受控运维变更双通道。
2. RedactionEngine 的规则来源与热更新边界如何与 SecurityPolicyManager 协同。
3. 远程导出协议最小集合（HTTP push、对象存储、仅本地）选择。
4. SnapshotStore 采用内存窗口、文件索引还是 sqlite 作为首版后端。
5. diagnostics 与 plugin/ota 故障证据共享字段是否需要统一 artifact schema。

### 12.2 后续任务建议

1. 继续按 diagnostics 组件专项 TODO 推进 DIA-TODO-009 与 DIA-TODO-011，先完成公开接口收口。
2. `DIA-BLK-003` 已在本轮完成；下一轮可直接以 `DIA-TODO-013` 落盘 CommandRegistry，并按 6.5.2 的 v1 schema 实现 validate 负例与规范化逻辑。
3. 在 edge_balanced/edge_minimal 做一次资源预算压测，确认快照大小与保留窗口默认值。
4. 补齐 tests/integration/infra/diagnostics 最小链路用例后，再开启远程导出能力评审。
