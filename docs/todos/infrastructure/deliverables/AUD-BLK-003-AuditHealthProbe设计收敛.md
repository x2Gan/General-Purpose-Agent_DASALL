# AUD-BLK-003 AuditHealthProbe 设计收敛

日期：2026-04-03  
任务：AUD-BLK-003  
状态：解阻 PASS

## 1. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 将 `AUD-TODO-014` 标记为 Blocked，根因明确为“`AuditHealthStatus` 字段未冻结，无法安全定义 `IAuditHealthProbe` 返回对象”。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.2/6.3/6.6 已经固定 `AuditHealthProbe` 子组件职责和 `evaluate() -> AuditHealthStatus` 签名，但在本轮前仍缺少状态对象字段、状态机语义与 failure reason 约束。
3. [docs/architecture/DASALL_infra_health模块详细设计.md](docs/architecture/DASALL_infra_health模块详细设计.md) 6.5/6.6 已冻结通用 `IHealthProbe`、`ProbeDescriptor`、`ProbeResult` 与结构化 timeout failure 语义，说明 audit 不应把私有状态对象直接外泄到 health 公共接口，而应先冻结自身对象再经适配器桥接。
4. [infra/include/audit](infra/include/audit) 当前尚无 `IAuditHealthProbe.h`，进一步说明 blocker 的本质是对象边界未成文，而不是实现代码缺失。

## 2. 外部参考

1. Kubernetes 的 liveness/readiness probe 语义强调“readiness 在整个生命周期中持续评估，并在临时故障或过载恢复期间反映是否可继续接流量”，这支持 audit v1 把 `Ready/Degraded/Unavailable` 冻结为长期可查询的运行态，而不是一次性初始化标志。

## 3. 阻塞修复与设计结论

阻塞结论：

1. `AUD-BLK-003` 的真实缺口不是缺少通用 health 基础设施，而是 audit 自身没有把“主链可写 / fallback 降级 / 完全不可用”收敛成可二值判定的私有状态对象，导致 `IAuditHealthProbe` 的返回边界无法冻结。

最小 blocker-fix：

1. 在 audit 详细设计中补齐 `AuditHealthStatus` 对象表，冻结 `Ready/Degraded/Unavailable` 三态。
2. 把最近失败原因固定为受控 reason code，而不是自由文本。
3. 明确 `IAuditHealthProbe::evaluate()` 只提供只读快照，不吸收 `infra/health` 的调度与恢复职责。

设计结论：

1. `AuditHealthStatus` 的最小字段冻结为：
   - `state`
   - `last_failure_reason`
   - `detail_ref`
   - `error_code`
   - `sampled_at_unix_ms`
   - `fallback_active`
   - `metrics_bridge_degraded`
2. `state` 只允许 `Ready`、`Degraded`、`Unavailable`：
   - `Ready`：主链可写且无活跃故障；`last_failure_reason` 为空。
   - `Degraded`：fallback 仍可承接，或 metrics bridge 进入 best-effort；必须带 `last_failure_reason` 与 `detail_ref`。
   - `Unavailable`：audit 当前无法保证主链与降级链继续承接；必须带 `last_failure_reason`、`detail_ref` 与 `error_code`。
3. `last_failure_reason` v1 允许集冻结为：
   - `primary_capacity_exhausted`
   - `fallback_active`
   - `metrics_bridge_degraded`
   - `fallback_unavailable`
   - `service_not_started`
   - `service_stopped`
4. `detail_ref` 只允许承接 audit 本地可追溯证据，例如 `diag://infra/audit/health/degraded/fallback_active`，不透传 request/session/file path 等高基数字段。
5. `IAuditHealthProbe::evaluate()` 保持只读快照语义；后续若需要接入通用 `IHealthProbe`，只能通过适配器做状态映射，不能把 `AuditHealthStatus` 直接变成 health 公共对象。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 `AuditHealthStatus` 对象字段与三态 | infra/include/audit/IAuditHealthProbe.h |
| 冻结 `IAuditHealthProbe::evaluate()` 只读边界 | infra/include/audit/IAuditHealthProbe.h |
| 固化 failure reason allowlist 与 detail_ref 约束 | tests/unit/infra/AuditLoggerInterfaceTest.cpp |
| 验证三态可被集成场景承接 | tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp |

## 5. 对 AUD-TODO-014 的直接交接

1. `AUD-TODO-014` 可以从 Blocked 转为 Not Started，并按已冻结的对象边界直接实现 `IAuditHealthProbe.h`。
2. 后续实现不得把 `AuditHealthStatus` 改写成自由文本状态对象，也不得直接复用 `infra/health` 的 `ProbeResult` 替代 audit 私有状态。

## 6. 风险与回退

1. 若后续 audit 需要更细粒度 failure reason，只能增量扩展 allowlist，不得替换既有三态或移除 `last_failure_reason`。
2. 若后续 health 子系统要求 audit 直接注册通用探针，也必须通过 adapter 适配，而不是回退当前 `AuditHealthStatus` 私有边界。
3. 本轮只解阻设计，不提前落盘 `IAuditHealthProbe` 实现或 integration wiring；若实现轮次试图顺手吸收 metrics/exporter 细节，应视为越界。