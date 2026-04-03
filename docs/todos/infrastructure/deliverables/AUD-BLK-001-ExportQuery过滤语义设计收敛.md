# AUD-BLK-001 ExportQuery 过滤语义设计收敛

日期：2026-04-03
任务：AUD-BLK-001
状态：解阻 PASS

## 1. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 将 `AUD-TODO-012` 标记为 Blocked，根因明确为“ExportQuery 细粒度 filter 模型未冻结，过滤语义与越权边界未固定”。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.2/6.3/6.5 已冻结 `AuditExporter` 职责、`ExportQuery` / `ExportResult` 字段表和导出返回面，但在本轮前仍缺少 actor/action/target/outcome 的组合语义、稳定排序与 page token 边界。
3. 当前实现中，[infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp) 仍在 service 内联执行导出筛选，只具备最粗粒度的时间窗/actor/action/target/outcome 判断，还没有独立 `AuditExporter` 组件或稳定分页语义。
4. 当前导出面已经由 [infra/include/audit/AuditExporterTypes.h](infra/include/audit/AuditExporterTypes.h) 冻结为 `ExportResult.records = std::vector<AuditEvent>`，说明 blocker 的本质不是“是否需要导出上下文”，而是“ExportQuery 的最小过滤模型、resume token 与导出边界还未成文”。

## 2. 外部参考

1. OWASP Logging Cheat Sheet 强调审计/安全日志需要覆盖 when/where/who/what/outcome，同时要求导出或查看日志时排除 access token、session id、password、原始文件路径等高敏感数据，并在日志处置阶段保持 retention/disposal 可追溯；这支持 audit v1 把导出面限制在结构化 `AuditEvent`，而不是把 `AuditContext` 与原始 payload 一并外泄。
2. OpenTelemetry Logs Data Model 强调稳定且高频出现的字段应保持命名明确、类型固定，并用结构化字段承载事件语义；这支持 audit v1 继续沿用 `start_ts/end_ts/actor/action/target/outcome/page_token` 的固定查询形状，而不是在 blocker 未解前引入模糊 pattern 查询。

## 3. 阻塞修复与设计结论

阻塞结论：

1. `AUD-BLK-001` 的真实缺口不是缺少导出对象，而是 audit 没有把“哪些过滤轴是 v1 必须支持、哪些轴只是缩窄结果集、page token 如何与过滤元组绑定、导出面究竟允许暴露什么字段”冻结成可实现协议，导致 `AUD-TODO-012` 无法安全从 service 内联筛选收敛到独立 `AuditExporter`。

最小 blocker-fix：

1. 在 audit 详细设计中新增 `6.5.1 ExportQuery 最小过滤与导出边界冻结（AUD-BLK-001）`，固定时间窗+actor+action 主过滤、target/outcome 扩展规则与稳定排序/分页边界。
2. 明确 `ExportResult.records` 只允许承载 `AuditEvent`，并把 `AuditContext`、session/request/lease 等关联锚点排除在 export payload 之外。
3. 把 `target_pattern`、`outcome_reason`、fuzzy matching、full-text body export 明确降级到 v2，而不是继续作为 `AUD-TODO-012` 的隐性前置条件。

设计结论：

1. `ExportQuery` 的基准过滤轴固定为 `start_ts/end_ts/actor/action`：
   - `start_ts` / `end_ts` 必填，时间窗按闭区间 `[start_ts, end_ts]` 解释。
   - `actor`、`action` 为空表示该轴未启用；非空时只允许 exact-match，并与时间窗采用 AND 组合。
2. `target` 与 `outcome` 固定为 v1 扩展过滤轴：
   - 只能在时间窗+actor+action 基础上继续缩窄结果集，不能放宽主过滤。
   - `target` 只允许 exact-match，不引入 `target_pattern`、wildcard 或 regex。
   - `outcome` 只允许复用 `AuditOutcome` 既有枚举值，不新增 `outcome_reason` 自由文本过滤。
3. 稳定排序与分页语义冻结为：
   - 记录按 `timestamp ASC, event_id ASC` 排序。
   - `page_token` 是 opaque resume token，必须绑定标准化过滤元组和最后一条已返回记录的位置。
   - 过滤元组变化时，旧 token 不得在新查询上复用。
4. 导出边界与脱敏语义冻结为：
   - `ExportResult.records` 只允许承载 `AuditEvent`；`AuditContext` 及其 request/session/trace/task/lease/worker 锚点不得进入导出载荷。
   - exporter 不新增 request body、response body、prompt/context、secret payload 等扩展字段。
   - `target`、`evidence_ref.ref`、`side_effects` 在 v1 只允许承载结构化标识或 effect 名称，不得承载 access token、password、session id、原始文件路径或其他高敏感原文。
5. v1 明确不支持：
   - `target_pattern`
   - `outcome_reason`
   - fuzzy/substring matching
   - full-text body export
   - `AuditContext` 作为导出载荷

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结时间窗+actor+action 主过滤与 target/outcome exact-match 扩展 | infra/src/audit/AuditExporter.cpp |
| 冻结稳定排序与 opaque resume token 语义 | infra/src/audit/AuditExporter.cpp |
| 冻结 AuditEvent-only 导出边界，不让 AuditContext 外泄 | tests/unit/infra/AuditExportFilterTest.cpp |
| 固化导出边界不越权到 request/session/lease 等上下文锚点 | tests/contract/smoke/AuditBoundaryContractTest.cpp |

## 5. 对 AUD-TODO-012 的直接交接

1. `AUD-TODO-012` 可以从 Blocked 转为 Not Started，并按已冻结的过滤/分页/边界协议落盘 internal `AuditExporter`。
2. 后续实现只允许支持：
   - 时间窗必填
   - actor/action 精确匹配主过滤
   - target/outcome 精确匹配扩展过滤
   - 稳定排序与 opaque resume token
   - `ExportResult.records = AuditEvent` 的导出边界
3. 后续实现不得：
   - 引入 `target_pattern` / regex / fuzzy matching
   - 把 `AuditContext` 合并进 `ExportResult`
   - 暗中导出 request/session/lease/worker 或原始 payload

## 6. 风险与回退

1. 若后续导出实现把 `target`/`evidence_ref.ref`/`side_effects` 扩张为原始 payload 文本，本 blocker 需要重新转为 Blocked。
2. 若后续分页 token 不再绑定过滤元组，而允许跨查询复用，导出结果的一致性与可追溯性将失效，应回退到当前设计。
3. 本轮只解阻设计，不提前落盘 `AuditExporter.cpp` 或修改 service/export 测试逻辑；若实现轮顺手引入 retention、archive 或全文检索需求，应视为越界。
