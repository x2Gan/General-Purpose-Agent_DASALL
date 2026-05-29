# LOG-BLK-005 LogQueryService 设计收敛

日期：2026-04-03  
任务：LOG-BLK-005  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 将 LOG-BLK-005 定义为“LogQueryService 查询模型与权限边界未冻结”。
2. [docs/architecture/DASALL_infra_logging模块详细设计.md](docs/architecture/DASALL_infra_logging模块详细设计.md) 已在约束与配置表中要求 logging 支持按 `trace_id`/`session_id` 的诊断拉取，并冻结了 `infra.logging.export.enable_diag_pull`，但尚未把 query schema、授权证明与导出限制写成正式设计。
3. [docs/architecture/DASALL_架构设计文档.md](docs/architecture/DASALL_架构设计文档.md) 13.3 明确运维侧必须具备“结构化日志抓取和按 trace/session 检索”能力；单纯回退到“只保留文件级导出、不开放 trace/session”与该架构要求冲突。
4. [docs/architecture/DASALL_infra_diagnostics模块详细设计.md](docs/architecture/DASALL_infra_diagnostics模块详细设计.md) 已冻结 `IDiagnosticsPolicyGuard`、本地/远程导出分离、remote 默认关闭与本地 artifact 优先策略，说明 logging 不应再发明第二套导出/鉴权体系。
5. [infra/include/policy/PolicyTypes.h](infra/include/policy/PolicyTypes.h) 与 [infra/include/diagnostics/DiagnosticsTypes.h](infra/include/diagnostics/DiagnosticsTypes.h) 已提供 `PolicyDecisionRef`、本地导出结果与 allow/deny 决策模型，可直接作为 logging query 的边界参考。

## 2. 解阻结论

1. LOG-BLK-005 的真实缺口不是“是否允许按 trace/session 拉取”，而是缺少受控的 query 对象、allow 决策证明和导出限制；真正需要冻结的是边界，而不是回退需求本身。
2. `LogQueryService` 的唯一对外边界冻结为：`query(const LogQueryRequest&, const LogQueryAccessContext&) -> LogQueryResult`。
3. `LogQueryRequest` 最小字段冻结为：
   - `query_id`
   - `selector_kind`
   - `selector_value`
   - `start_ts_ms`
   - `end_ts_ms`
   - `max_records`
4. `selector_kind` 首版基础面仅允许 `TraceId` 或 `SessionId`；2026-05-29 memory production logging 补强后，受控精确 selector 扩展为 `TraceId` / `SessionId` / `RequestId` 三选一。仍禁止 regex、全文搜索、任意 attr filter、sort expression、cursor DSL 等自由查询语法。
5. `LogQueryAccessContext` 必须携带 `actor_ref`、`consumer_module`、`policy_decision_ref` 与 `InfraContext`。logging 不自行做身份判定或二次授权，只验证 allow 证明是否完整且可审计。
6. `infra.logging.export.enable_diag_pull` 继续作为配置 gate；该键只接受默认/Profile/部署，runtime override 不允许开启此能力。
7. `LogQueryResult` 只返回本地 artifact 摘要：`artifact_ref`、`match_count`、`truncated`、`checksum`、`created_at`。首版只允许 `diag://infra/logging/query/<query_id>` 或等价本地文件引用，不直接返回原始记录容器。
8. remote export、目标白名单、格式策略继续由 diagnostics 子域持有；若未来要做远程导出，必须消费 `LogQueryResult.artifact_ref`，而不是让 logging 直接持有上传能力。
9. 失败边界继续只暴露 contracts 已冻结错误语义：
   - query 非法：`contracts::ResultCode::ValidationFieldMissing`
   - policy/config gate 拒绝：`contracts::ResultCode::PolicyDenied`
   - 本地 artifact 生成失败：`contracts::ResultCode::ToolExecutionFailed`

## 3. Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| 冻结 `LogQueryRequest` / `LogQueryAccessContext` / `LogQueryResult` | 后续任务只需在 `infra/src/logging/LogQueryService.cpp/.h` 落最小骨架，不再补第二套 query/export 对象 |
| 查询面只允许 trace/session/request 精确 selector | 单测直接覆盖空 selector、乱序时间窗、非法 `max_records` 负例，以及 trace/request 正例 |
| logging 只接受上游 allow 决策证明 | 单测与集成测试只验证 allow/deny/config gate，不把二次确认逻辑塞进 logging |
| 首版只产出本地 artifact_ref | 集成测试只覆盖本地 artifact 生成与 diagnostics 消费前置，不实现 remote upload |

## 4. 验证结果

1. `grep -n "结构化日志抓取和按 trace/session 检索" docs/architecture/DASALL_架构设计文档.md`：可定位运维侧对 trace/session 诊断拉取的架构要求。
2. `grep -n "IDiagnosticsPolicyGuard\|remote 默认关闭\|导出" docs/architecture/DASALL_infra_diagnostics模块详细设计.md`：可定位 diagnostics 子域已冻结的 policy/export 边界。
3. `grep -n "LogQueryService\|LogQueryRequest\|LogQueryAccessContext\|diag://infra/logging/query" docs/architecture/DASALL_infra_logging模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md docs/todos/infrastructure/deliverables/LOG-BLK-005-LogQueryService设计收敛.md`：可定位 logging 侧 query schema、allow proof、本地 artifact 限制与 blocker 回写。

## 5. 后续边界

1. 本轮只完成 `LogQueryService` 的设计解阻，不实现代码；实现继续由后续 `LOG-TODO-019` 承接。
2. `LogQueryService` 不承担 remote export、通用检索 DSL、audit 主存储 join 或任何业务权限裁定；若后续实现试图越过这些边界，应立即回退并重新审查。