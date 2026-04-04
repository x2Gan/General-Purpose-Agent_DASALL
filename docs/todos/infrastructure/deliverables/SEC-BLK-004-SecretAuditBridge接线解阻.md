# SEC-BLK-004 SecretAuditBridge 接线解阻

日期：2026-04-04
任务：SEC-BLK-004
状态：解阻 PASS

## 1. 本地证据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 将 SEC-TODO-012 标记为 Blocked，根因是 `IAuditLogger` 接线细节和 `SecretAuditEvent` -> `AuditEvent` / `AuditContext` 的字段映射尚未冻结。
2. docs/architecture/DASALL_infra_secret模块详细设计.md 6.10 在本轮之前只给出了“secret 行为必须审计”的高层要求，但没有说明 SecretAuditBridge v1 需要哪些 sink、如何映射 consumer_module / reason_code / version，以及什么条件下算 audit success。
3. infra/include/audit/IAuditLogger.h、infra/include/audit/AuditTypes.h 已冻结 `write_audit(event, context)` 调用面与 `AuditEvent` / `AuditContext` 形状，说明 blocker 的真实缺口是 secret 侧映射规则，而不是 audit 公共接口仍未完成。
4. profiles/src/ProfileTelemetryAdapter.cpp 与 tests/integration/infra/config/ConfigObservabilityIntegrationTest.cpp 已展示当前仓库的 audit write 样板：业务/子系统事件通过 `AuditEvent` + `AuditContext` 写入 `IAuditLogger`，并以 `side_effects` 补充上下文细节。

## 2. 外部参考

1. OWASP Secrets Management Cheat Sheet 强调 secret 审计至少应覆盖谁请求了 secret、请求是否被批准/拒绝、何时使用、何时过期、是否尝试复用过期 secret，以及认证/鉴权错误；这支持 DASALL 将 access_granted / access_denied / expired_access / rotate / revoke / fallback 都收敛为显式审计动作名。
2. Azure Key Vault secrets best practices 建议记录所有 secret access operations，并为 lifecycle events、失败认证和异常访问模式建立监控与告警；这支持 DASALL 保持 `IAuditLogger` 为 SecretAuditBridge v1 的强制 sink，并在 audit write fail 时显式快返，而不是静默降级。

## 3. 阻塞修复与设计结论

阻塞结论：

1. SEC-BLK-004 已具备解阻条件。secret 详细设计 6.10.1 现已冻结 SecretAuditBridge v1 的必选 sink、`SecretAuditEvent` -> `AuditEvent` / `AuditContext` 的最小字段映射，以及 audit write success/failure 的判定规则。

最小 blocker-fix：

1. 在 secret 详细设计新增 6.10.1，冻结 SecretAuditBridge v1 只依赖 `IAuditLogger`，并明确动作名、side_effects、AuditContext 回落和 write success 语义。
2. 将 secret 专项 TODO 中的 SEC-BLK-004 改写为“已解阻（2026-04-04）”，并把 SEC-TODO-012 的 blocker 列迁移为已解阻说明。
3. 保持当前范围只做 blocker 设计收敛，不提前落盘 `SecretAuditBridge.cpp` 或 unit tests。

设计结论：

1. SecretAuditBridge v1 不依赖 logger / metrics / tracing，只依赖 `audit::IAuditLogger`。
2. `consumer_module`、`reason_code`、`version` 统一进入 `AuditEvent.side_effects`，`request_id` / `task_id` 进入 `AuditContext`，未冻结字段在 v1 使用 `kAuditContextUnknown` 默认值。
3. `AccessDenied` 必须映射 `AuditOutcome::Rejected`，`Fallback` 必须映射 `AuditOutcome::Escalated`，其余动作按布尔 outcome 映射 success/failure。
4. `AuditWriteOutcome.is_success()` 与 `is_degraded_success()` 都视为 bridge 成功；其他写入状态一律映射 `INF_E_SECRET_AUDIT_WRITE_FAIL`。
5. `infra.secret.audit.required=true` 继续保持强制语义，bridge 不能在 audit 失败时退化为“只写日志继续成功”。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 SecretAuditBridge v1 必选 sink 仅为 `IAuditLogger` | infra/src/secret/SecretAuditBridge.h；infra/src/secret/SecretAuditBridge.cpp |
| 冻结 `SecretAuditEvent` -> `AuditEvent` / `AuditContext` 映射 | infra/src/secret/SecretAuditBridge.cpp；tests/unit/infra/secret/SecretAuditBridgeTest.cpp |
| 冻结 audit write success / failure 判定 | infra/src/secret/SecretAuditBridge.cpp；tests/unit/infra/secret/SecretAuditBridgeTest.cpp |
| 把 blocker 状态回链到 TODO 与执行日志 | docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md；docs/worklog/DASALL_开发执行记录.md |

## 5. 对 SEC-TODO-012 的直接交接

1. SEC-TODO-012 可以从“受 SEC-BLK-004 阻塞”转为“可执行”，并按已冻结的 v1 sink 合同与字段映射落盘 SecretAuditBridge 最小骨架。
2. 后续实现至少需要覆盖：
   - access_granted / access_denied / rotate / revoke / fallback 动作名映射；
   - `consumer_module` / `reason_code` / `version` 的 side_effects 映射；
   - request/task 到 AuditContext 的映射与 unknown 回落；
   - audit success、degraded success 与 hard failure 三路径。
3. 后续实现不得：
   - 让 SecretAuditBridge 依赖 `ILogger` 作为必选 sink；
   - 把 `consumer_module` 或 `reason_code` 悄悄丢弃；
   - 在 `IAuditLogger` 写入失败时静默返回成功。

## 6. 风险与回退

1. 若后续 012 实现把 `IAuditLogger` 失败降级为“仅打日志”，本 blocker 需要重新打开。
2. 若后续 bridge 动作名、字段映射或 `AuditOutcome` 语义偏离 6.10.1，本 blocker 需要重新打开。
3. 本轮只解阻设计与 TODO 状态；真正的构建、单测和错误码回归留给 SEC-TODO-012。