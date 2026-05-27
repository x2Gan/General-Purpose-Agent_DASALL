# LoggingProductionAcceptanceMatrix

关联任务：INF-LOG-FIX-001  
最近更新时间：2026-05-27  
适用阶段：infra/logging production hardening / acceptance freeze

## 1. 目标

本文件冻结 DASALL 当前 logging production acceptance matrix，统一以下口径：

1. logging production pipeline 的默认主链、owner boundary 与 industry practice 对齐点。
2. 哪些证据属于 L1/L2/L3/L4/L5，哪些结论不能越级外推。
3. v1 production sink backend、installed artifact path 与 package artifact schema 的冻结结论。
4. `INF-LOG-GATE-001~006` 在后续 `INF-LOG-FIX-002~011` 中分别绑定哪些代码目标、测试目标、installed artifact 与 owner boundary。

本文件不是新的 logging 详细设计，而是把 `docs/architecture/DASALL_infra_logging模块详细设计.md`、ADR-006/007/008、2026-05-27 本轮评审结论与当前 install layout 统一收口为单点 SSOT。

## 2. 全局冻结规则

### 2.1 evidence level 规则

| Level | 名称 | owner 结论 | 允许证据 | 禁止外推 |
|---|---|---|---|---|
| L1 | L1 Design / SSOT | 设计、边界、gate、artifact taxonomy 已冻结 | 详细设计、SSOT matrix、TODO/closeout 回写、contract wording guard | 不代表任何代码路径或落盘行为已实现 |
| L2 | L2 build-tree focused evidence | 单组件/单 slice 的 unit、contract、focused integration 已闭合 | `tests/unit`、`tests/contract`、focused integration、build-tree 临时目录落盘 | 不代表 live composition、跨子系统或 installed package ready |
| L3 | L3 build-tree integrated evidence | live composition 或跨组件 build-tree 主链已闭合 | shared composition integration、build-tree structured/redacted sink/query artifact | 不代表 installed package、package smoke 或 release handoff |
| L4 | L4 local installed authoritative evidence | fresh installed package 在本机 authoritative smoke 下产出 redacted artifact | `pkg_smoke_install.sh --explicit-start-check`、installed daemon lifecycle、`logging-installed-proof.json`、`logging-runtime-proof.json` | 不代表 packaging / release handoff、machine isolation 或 soak 级结论 |
| L5 | L5 packaging / release handoff | release owner 归档了基于 fresh package 的 local installed / soak handoff artifact | fresh rebuilt package、release local soak、归档的 handoff summary | 不代表 qemu / kvm、autopkgtest 或 production steady-state |

冻结结论：一切 owner 当前验收以 local installed authoritative evidence 为准；不把 qemu / kvm 作为 logging owner 当前验收前置。

### 2.2 backend policy

1. v1 production primary sink backend 固定为 `spdlog-backed file / rotating sink`。
2. `DASALL platform-backed` 自定义 sink 目前不作为 production primary backend，只允许继续停留在 test double、compat seam 或 future alternative backend 评审候选。
3. 异步语义固定吸收 spdlog 风格的 queue + worker + overflow policy：`block` 与 `overrun_oldest` 必须是唯一允许的两类 backpressure 语义。
4. message ordering 默认按单 worker thread 口径冻结；多 worker 仅能在后续任务显式评审后引入，不能在本专项默认开启。
5. OTel 对齐点固定为 `TraceId / SpanId / Resource` 兼容字段与结构化 attributes/body 分层，不要求本轮直接引入 OTel SDK。

### 2.3 path 与 artifact policy

1. build-tree 默认 schema 继续允许 `infra.logging.file.path=logs/runtime.log`，用于 focused/build-tree slice，不直接冒充 installed 路径 owner。
2. installed authoritative path 固定为 `state_root/logging/runtime.log`；packaged `state_root` 来自 `InstallLayout.state_root=/var/lib/dasall`，因此 canonical installed log path 为 `/var/lib/dasall/logging/runtime.log`。
3. rotation family 固定与 primary file sink 同目录：`/var/lib/dasall/logging/runtime.log.<n>`。
4. diagnostics query artifact 继续复用 `diag://infra/logging/query/<query_id>` 命名空间；本地导出文件属于 diagnostics artifact，不等于 primary runtime log owner。
5. installed package proof artifact 名称冻结为 `logging-installed-proof.json` 与 `logging-runtime-proof.json`；两者都必须是 redacted artifact，不允许回写明文 secret/token/password/auth value。

## 3. production chain 与 owner boundary

| pipeline stage | canonical owner | v1 冻结结论 | 不可越权 |
|---|---|---|---|
| `LoggingFacade -> LogContextEnricher` | infra/logging | 只补 owner-safe `request_id/session_id/trace_id/task_id/parent_task_id/lease_id`，不生成 Memory 语义上下文 | 不接管 ADR-006 的 ContextOrchestrator 职责 |
| `RedactionFilter -> StructuredFormatter` | infra/logging | redaction/filter/formatter 必须默认不可绕过，先脱敏再格式化 | 不把 provider secret、auth value、用户原始敏感输入直接落盘 |
| `SinkDispatcher -> AsyncQueueController` | infra/logging | spdlog-backed file / rotating sink 为 primary，stderr + ringbuffer 为 fallback | 不把 queue/backpressure 语义扩张成 runtime recovery execute owner |
| `LoggingRecovery` | infra/logging | 只发 advisory degraded/fallback/recovered signal | 不越过 ADR-007 去执行 Runtime recovery |
| `LoggingMetricsBridge / LoggingHealthProbe` | infra/logging + infra/metrics + infra/health | 只投影 accepted/dropped/flush/failure/queue depth 与 degraded/unhealthy | 不成为全局主控或第二套 health owner |
| `AuditLinkAdapter / LogQueryService` | infra/logging 协同 audit/diagnostics | ordinary log 与 audit persistence/query artifact 分层，默认脱敏 | 不把 audit payload 混入普通日志，不开放任意全文搜索 |

## 4. artifact taxonomy

| artifact | level | owner | 最小字段/语义 | 非外推说明 |
|---|---|---|---|---|
| `build-tree logging pipeline output` | L2/L3 | infra/logging focused or live composition tests | structured + redacted line、route/fallback/flush result | 不代表 installed package 路径、权限或 daemon lifecycle flush |
| `diag://infra/logging/query/<query_id>` | L2/L3 | LogQueryService / diagnostics local artifact | trace/session selector、checksum、match_count、truncated | 不代表 audit 主存储、远程导出或 admin override 已闭合 |
| `logging-installed-proof.json` | L4 | installed package smoke authoritative owner | installed log path、rotation presence、redaction proof、daemon stop flush result | 不代表 release handoff 或 qemu/autopkgtest |
| `logging-runtime-proof.json` | L4 | installed package smoke summary owner | pipeline ready/degraded、query/export summary、audit split summary、subsystem summary | 不代表 machine-isolated soak 或 release archive |
| `infra-release-soak-summary.json.logging` | L5 | packaging / release local soak owner | local soak iterations、drop/failure slices、installed artifact refs | 不代表 qemu / kvm 或 production steady-state |

## 5. gate matrix

| Gate ID | target level | code goal | test goal | installed artifact | owner boundary | fail-closed / non-extrapolation |
|---|---|---|---|---|---|---|
| INF-LOG-GATE-001 | L1 | 冻结 `LoggingProductionAcceptanceMatrix`、backend policy、artifact taxonomy、Design -> Build 映射 | `LoggingProductionAcceptanceContractTest` 或等价 docs contract | N/A | 只声明 design/SSOT freeze | 未过 gate 前禁止进入 production code 实现 |
| INF-LOG-GATE-002 | L2 | `StructuredFormatter` / `RedactionFilter` 成为默认主链 | formatter/filter/facade focused tests 与 golden fixtures | N/A | 只声明 build-tree focused redaction ready | 未有 redacted golden 前禁止 sink 落盘 |
| INF-LOG-GATE-003 | L2/L3 | `spdlog-backed file / rotating sink`、async worker、flush deadline、backpressure 闭合 | file sink / route / queue / deadline focused tests | N/A | build-tree 可落盘，不等于 installed | 路径不可写时必须 fail-closed + degraded signal |
| INF-LOG-GATE-004 | L3 | config/recovery/metrics/health 全部接入 live composition | live composition config + health cadence + failure signal tests | N/A | live composition 仍只是 build-tree integrated evidence | 不得把 degraded/live composition 结论外推成 installed ready |
| INF-LOG-GATE-005 | L3/L4 | audit route、query artifact、retention/cleanup 默认脱敏并遵守 admin boundary | audit correlation、query artifact、retention focused/integration tests | `logging-runtime-proof.json` 摘要可复用 | audit/diagnostics owner 继续分层 | query/export 未闭合前禁止宣称 production diagnostics ready |
| INF-LOG-GATE-006 | L4/L5 | cross-subsystem logging e2e + installed package proof 同时闭合 | key subsystem e2e + package smoke + local soak slice | `logging-installed-proof.json`、`logging-runtime-proof.json`、`infra-release-soak-summary.json.logging` | local installed authoritative evidence 是当前 owner 验收标准；L5 仅为 handoff | 未有 installed artifact 前禁止宣称 production / installed ready |

## 6. Design -> Build 映射

| Task | 设计冻结输出 | code goal | test goal | installed artifact / level |
|---|---|---|---|---|
| INF-LOG-FIX-001 | acceptance matrix、backend/path policy、gate 表、non-extrapolation | 设计冻结，不改 production code | `LoggingProductionAcceptanceContractTest` | L1，无 installed artifact |
| INF-LOG-FIX-002 | redaction schema、structured field schema | `StructuredFormatter`、`RedactionFilter`、`LoggingFacade` 主链 | formatter/filter/facade redaction tests | L2 focused evidence |
| INF-LOG-FIX-003 | primary sink/rotation/install path policy | `FileLogSink`、`SinkDispatcher`、CMake target | file sink / route / failure injection tests | L2/L3 build-tree log file |
| INF-LOG-FIX-004 | async queue/backpressure/flush deadline policy | `AsyncQueueController`、`LoggingFacade::flush()` | worker/deadline/backpressure tests | L2/L3 build-tree queue/flush evidence |
| INF-LOG-FIX-005 | config key SSOT 与 live projection | `LoggingConfigAdapter`、live composition wiring | strict parse + live composition config tests | L3 live composition evidence |
| INF-LOG-FIX-006 | recovery advisory/fallback schema | `LoggingRecovery`、sink/queue failure wiring | recovery/fallback/failure signal tests | L3 degraded/fallback evidence |
| INF-LOG-FIX-007 | metric family 与 health threshold | `LoggingMetricsBridge`、`LoggingHealthProbe` | metrics/health/live composition tests | L3 health/metric evidence |
| INF-LOG-FIX-008 | audit split 与 audit ref schema | `AuditLinkAdapter`、audit route wiring | audit route/correlation tests | L3 audit split evidence |
| INF-LOG-FIX-009 | query artifact、retention、cleanup policy | `LogQueryService`、persisted reader/index | query artifact/retention tests | L3 query artifact evidence |
| INF-LOG-FIX-010 | key subsystem field/correlation matrix | cross-subsystem live composition logging pipeline | runtime/llm/memory/profiles/tools production logging tests | L3 shared sink/query evidence |
| INF-LOG-FIX-011 | installed package proof schema | package smoke / release local soak logging slice | installed package logging smoke、local soak logging slice | L4 `logging-installed-proof.json`、`logging-runtime-proof.json`；L5 handoff summary |

### 6.1 INF-LOG-FIX-002 structured/redaction schema freeze

1. `StructuredFormatter` 的 frozen schema version 固定为 `dasall.logging.event.v1`；首版仍复用 `LogEvent` 外形，不新增 top-level contracts 字段，只把结构化 JSON envelope 写入 `LogEvent.message`。
2. formatter 必须在 attrs 中补齐 `schema_version`、`correlation_id`、`idempotency_key` 三个 canonical 字段。
3. `correlation_id` 的选择优先级固定为 `trace_id -> request_id -> session_id -> task_id -> unknown`。
4. `idempotency_key` 的首版 frozen tuple 固定为 `correlation_id|task_id|module|ts_ms`；后续若要引入 hash/extra dimensions，必须新开 task，不得静默改变当前语义。
5. `RedactionFilter` 的 deny-by-default sensitive key fragments 固定为 `token`、`secret`、`password`、`authorization`、`api_key`、`apikey`。
6. message/exception 文本模式的最低必拦截集合固定为 `bearer `、`token=`、`token:`、`secret=`、`secret:`、`password=`、`password:`、`authorization=`、`authorization:`、`api_key=`、`apikey=`；凡命中上述模式的 payload，不允许以明文继续进入 formatter/sink。
7. owner-safe allowlist 当前固定为 `request_id`、`session_id`、`trace_id`、`task_id`、`parent_task_id`、`lease_id`、`event_name`、`event_kind`、`evidence_ref`、`audit_ref_pending`、`schema_version`、`correlation_id`、`idempotency_key`；这些字段允许保留，但其 value 一旦命中文本模式，仍必须被 redaction。
8. `INF-LOG-GATE-002` 的 golden 证据固定落在 `LoggingStructuredFormatterTest`、`LoggingRedactionFilterTest` 与 `LoggingFacadeRedactionIntegrationTest`；它们共同证明默认 `LoggingFacade` 主链无法绕过 redaction/formatter，且 golden 输出不含 secret/token/password/auth value。
9. 本轮结论仍只到 L2 focused evidence：已冻结 redaction schema 与 structured field schema，但尚未宣称真实 sink 落盘、installed artifact 或 package handoff ready。

## 7. industry practice alignment

1. OpenTelemetry Logs：把 `TraceId / SpanId / Resource` 作为 top-level correlation 对齐点，把 request-scoped 附加信息保留在 structured attributes；DASALL 只吸收字段契约，不在本轮引入 OTel SDK 直连。
2. spdlog asynchronous logging：冻结 queue size、单 worker message ordering、`block`/`overrun_oldest` 两类 overflow policy，与 `spdlog-backed file / rotating sink` 一起作为 v1 primary backend policy。
3. Boost.Log 风格分层：只吸收 source/filter/formatter/sink layering 思想，用于约束 `LoggingFacade -> RedactionFilter -> StructuredFormatter -> SinkDispatcher -> AsyncQueueController` 的职责边界，不新增第二套 owner 模型。

## 8. 完成判定

当且仅当以下条件成立时，才允许将 `INF-LOG-FIX-001` 视为完成：

1. 本文件已明确 evidence level、backend policy、path policy、artifact taxonomy 与 `INF-LOG-GATE-001~006`。
2. `docs/architecture/DASALL_infra_logging模块详细设计.md` 已回链本文件，并明确 `spdlog-backed file / rotating sink`、`state_root/logging/runtime.log` 与 OTel correlation 对齐点。
3. `docs/todos/DASALL_子系统查漏补缺专项记录.md` 已把 `INF-LOG-FIX-001` 标为 Done，并将 `BLK-INF-LOG-001` 转为 Closed。
4. 本轮结论明确停留在 L1，不把 qemu / kvm 作为 logging owner 当前验收前置。