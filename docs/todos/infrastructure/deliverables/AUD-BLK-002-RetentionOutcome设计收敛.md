# AUD-BLK-002 RetentionOutcome 设计收敛

日期：2026-04-03
任务：AUD-BLK-002
状态：解阻 PASS

## 1. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 将 `AUD-TODO-013` 标记为 Blocked，根因明确为“RetentionOutcome 字段、归档动作对象、自动清理证据语义未冻结”。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.2/6.3/6.6 已冻结 `AuditRetentionManager` 职责、`IAuditRetention::apply_retention(now_ts)` 方法名与 retention.days=30 默认策略，但在本轮前仍缺少 retention 输出对象、archive action 和 cleanup trace 的最小协议。
3. 当前仓库里 [infra/include/audit](infra/include/audit) 仍不存在 `IAuditRetention.h`，说明 blocker 的本质不是“实现写不写得出来”，而是“公共接口该返回什么、删除动作如何保留证据、自动清理何时算合规”还未成文。
4. 现有 [infra/include/audit/AuditErrors.h](infra/include/audit/AuditErrors.h) 已冻结 `INF_E_AUDIT_RETENTION_FAIL`，因此 blocker 的缺口不在错误码域，而在 retention 输出对象与 cleanup trace 的二值判定规则。

## 2. 外部参考

1. OWASP Logging Cheat Sheet 强调 data exports、data deletion 都属于 audit trail 范畴，日志处置必须遵守 retention/disposal 时限，同时要求防止未授权删除并记录对日志的访问、修改与删除；这支持 audit retention v1 把 cleanup 结果显式建模为带 `cleanup_ref` 和 `archive_ref` 的证据对象，而不是静默删除。
2. NIST SP 800-92《Guide to Computer Security Log Management》将 log management 归入 Audit and Accountability 主题域，强调日志管理需要覆盖保护、监控与处置生命周期；这支持 DASALL 把 retention 输出对象设计成“是否完成、删了多少、证据在哪里、失败如何映射”的结构化结果，而不是只返回布尔值。

## 3. 阻塞修复与设计结论

阻塞结论：

1. `AUD-BLK-002` 的真实缺口不是缺少 `apply_retention()` 方法名，而是 audit 没有把“归档动作对象长什么样、cleanup 证据何时必填、自动清理是否允许在没有 archive 证据时直接删除、RetentionOutcome 如何二值判定成功/失败”冻结成可编译协议，导致 `AUD-TODO-013` 无法安全落盘公共接口。

最小 blocker-fix：

1. 在 audit 详细设计中补齐 `AuditArchiveAction`、`AuditCleanupEvidence`、`RetentionOutcome` 三个 retention v1 私有对象的字段表与一致性约束。
2. 新增 `6.6.2 RetentionOutcome 与归档/清理证据冻结（AUD-BLK-002）`，固定 `IAuditRetention::apply_retention(now_ts)` 的输入输出边界，以及 `completed/error_code`、`archive_action`、`cleanup_evidence` 的对齐规则。
3. 明确 v1 不支持无证据 hard-delete、返回原始 archive 物理路径、按 actor/target/outcome 细分 retention policy 或多级 archive compaction，对超出范围的需求统一降级到后续设计轮。

设计结论：

1. `AuditArchiveAction` 的最小字段固定为 `archive_ref/archived_records/archived_through_ts/checksum`：
   - `archive_ref` 只允许承接结构化引用，例如 `diag://infra/audit/retention/archive/batch-001`。
   - 不得返回原始文件路径、bucket URL、挂载点或带凭据的外部地址。
2. `AuditCleanupEvidence` 的最小字段固定为 `trigger/cleanup_ref/archive_ref/deleted_records/deleted_through_ts`：
   - `trigger` 只允许 `Manual`、`Scheduled` 两态。
   - 每次删除都必须产生 `cleanup_ref` 并绑定非空 `archive_ref`。
3. `RetentionOutcome` 的最小字段固定为 `completed/cutoff_ts/scanned_records/archived_records/deleted_records/detail_ref/error_code/archive_action/cleanup_evidence`：
   - `completed=true` 时 `error_code` 必须为空。
   - `completed=false` 时 `error_code` 必须存在且仍映射到既有 `contracts::ResultCode`。
   - `detail_ref` 必须落在 `diag://infra/audit/retention/...` 命名空间内。
4. cleanup 证据语义冻结为：
   - `deleted_records>0` 时必须携带合法 `cleanup_evidence`。
   - 若同轮也产生 `archive_action`，则 `cleanup_evidence.archive_ref` 必须与 `archive_action.archive_ref` 一致。
   - 若当前没有可追溯的 `archive_ref`，v1 retention 只能返回 archive-only 或 failure/no-op，不能执行静默 hard-delete。
5. v1 明确不支持：
   - direct hard-delete without `cleanup_evidence`
   - 返回 archive 物理路径或第三方存储凭据
   - 按 actor/target/outcome 的差异化 retention policy
   - 多阶段 archive compaction / tiering 结果对象

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 `IAuditRetention::apply_retention(now_ts)` 的最小输入输出边界 | infra/include/audit/IAuditRetention.h |
| 冻结 `RetentionOutcome` 与 `AuditArchiveAction`/`AuditCleanupEvidence` 的字段与一致性规则 | infra/include/audit/IAuditRetention.h |
| 固化 retention 接口仍只映射既有 `contracts::ResultCode` | tests/unit/infra/AuditLoggerInterfaceTest.cpp, tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp |

## 5. 对 AUD-TODO-013 的直接交接

1. `AUD-TODO-013` 可以从 Blocked 转为 Not Started，并按已冻结的 retention 输出对象与 cleanup trace 规则落盘 [infra/include/audit/IAuditRetention.h](infra/include/audit/IAuditRetention.h)。
2. 后续实现只允许支持：
   - `apply_retention(now_ts)` 单一入口
   - `RetentionOutcome` 的 completed/error_code 二值判定
   - `AuditArchiveAction` 的结构化 archive 引用与 checksum
   - `AuditCleanupEvidence` 的 Manual/Scheduled trigger 与 archive-ref 绑定
3. 后续实现不得：
   - 在公共接口里暴露文件路径、bucket URL、secret-bearing URI
   - 提供无 `cleanup_evidence` 的删除成功结果
   - 提前扩张到多策略 retention manager、archive compaction 或按 actor 差异化保留策略

## 6. 风险与回退

1. 若后续 retention 实现允许 `deleted_records>0` 但不返回 `cleanup_evidence`，或 `cleanup_ref` 不再绑定 `archive_ref`，本 blocker 需要重新转为 Blocked。
2. 若后续接口把 archive 物理路径、第三方存储地址或高敏感标识直接暴露到公共返回对象，本轮设计边界应立即回退并重新评审。
3. 本轮只解阻设计，不提前落盘 `IAuditRetention.h` 或 retention manager 实现；若接口轮顺手引入真实自动清理调度、archive 后端或存储回收算法，应视为越界。