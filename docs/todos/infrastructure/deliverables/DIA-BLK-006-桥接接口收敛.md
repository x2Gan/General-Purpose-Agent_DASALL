# DIA-BLK-006 桥接接口收敛

日期：2026-04-07  
任务：DIA-BLK-006  
状态：解阻 PASS

## 1. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md) 将 `DIA-TODO-021`、`DIA-TODO-022` 标记为 `Blocked`，根因明确为“metrics/audit 最小桥接接口签名未冻结”。
2. [infra/include/metrics/IMetricsProvider.h](infra/include/metrics/IMetricsProvider.h)、[infra/include/metrics/IMeter.h](infra/include/metrics/IMeter.h)、[infra/include/metrics/MetricTypes.h](infra/include/metrics/MetricTypes.h) 已冻结 `IMetricsProvider -> IMeter -> record(sample)` 接入协议，以及 `module/stage/profile/outcome/error_code` 五元标签 allowlist，这说明 diagnostics metrics bridge 不需要等待新的 metrics 公共接口。
3. [infra/include/audit/IAuditLogger.h](infra/include/audit/IAuditLogger.h) 与 [infra/include/audit/AuditTypes.h](infra/include/audit/AuditTypes.h) 已冻结 `write_audit(event, context)`、`AuditEvent`、`AuditContext` 与 `AuditWriteOutcome`，说明 diagnostics audit bridge 的缺口也不是 audit 公共接口缺失，而是 diagnostics 侧还没冻结自己的映射规则。
4. [infra/src/logging/LoggingMetricsBridge.h](infra/src/logging/LoggingMetricsBridge.h)、[infra/src/policy/PolicyMetricsBridge.h](infra/src/policy/PolicyMetricsBridge.h) 和 [infra/src/secret/SecretAuditBridge.h](infra/src/secret/SecretAuditBridge.h) 已提供仓内 bridge 样板：metrics bridge 复用固定 meter scope + 五元标签 allowlist；audit bridge 复用 `IAuditLogger` 并把内部事实收敛到 `AuditEvent/AuditContext/side_effects`。
5. [docs/todos/infrastructure/DASALL_infrastructure_tracing组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_tracing组件专项TODO.md) 已把 `TRC-BLK-001`、`TRC-BLK-002` 标记为已解阻，进一步证明 diagnostics 当前的 blocker 是台账与映射规则未同步，而不是外部组件接口仍未就绪。

## 2. 外部参考

1. OpenTelemetry Metrics API 规范要求模块经由 `MeterProvider -> Meter -> instrument -> record` 暴露指标，并强调属性应保持稳定、低基数；本轮据此把 diagnostics metrics bridge 固定到现有 `IMetricsProvider/IMeter` 接口和受控标签 allowlist，而不是自造 exporter / 动态标签。
   - https://opentelemetry.io/docs/specs/otel/metrics/api/
2. OWASP Logging Cheat Sheet 要求安全关键事件至少保留 who / what / when / outcome，并强调失败不能在没有可观测证据的情况下被静默吞掉；本轮据此把 diagnostics audit bridge 固定为 `IAuditLogger` required sink，并冻结 action/target/evidence/context/side_effects 最小映射。
   - https://cheatsheetseries.owasp.org/cheatsheets/Logging_Cheat_Sheet.html

## 3. 阻塞修复与设计结论

阻塞分类：

1. `DIA-BLK-006` 属于 context blocker：metrics/audit 公共接口已经冻结，但 diagnostics 自己还没有把“如何投影到 metrics 五元标签”和“如何映射到 audit event/action/context”写成权威设计，直接实现 021/022 会把这些约束埋进代码偶然性。

最小 blocker-fix：

1. 在 diagnostics 详细设计中新增 bridge sink contract 章节，明确 `DiagnosticsMetricsBridge` 复用 `IMetricsProvider -> IMeter -> record(sample)`，并冻结 meter scope、七指标族、stage/outcome/error_code allowlist 与 non-recursive failure semantics。
2. 在同一章节明确 `DiagnosticsAuditBridge` 复用 `IAuditLogger::write_audit`，并冻结 remote export / command extension 两类高风险动作的 action、target、evidence_ref、side_effects、context 映射，以及 required sink failure 语义。
3. 同步 diagnostics TODO / infrastructure 总 TODO / worklog，使 021/022 的状态回到 `Not Started`，不再把已解决的外部接口问题继续当作当前阻塞。

设计结论：

1. `DiagnosticsMetricsBridge` v1 的 meter scope 固定为 `infra.diagnostics@v1`，标签只允许 metrics 子域五元 allowlist。
2. diagnostics 设计 6.10 原始的 `{command}`、`{reason}`、`{target}` 维度在 v1 一律投影到 `labels.stage` 或 `labels.error_code`，不新增自定义 labels。
3. `labels.stage` v1 冻结为：`execute.health_snapshot`、`execute.queue_stats`、`execute.thread_dump`、`authorize`、`redaction`、`store`、`export.local_file`、`export.remote_upload`、`safe_mode`。
4. `labels.error_code` v1 只允许 `none`、`diag_command_denied` 与 diagnostics 错误码名 `INF_E_DIAG_*`；bridge 不接受自由文本 reason 或 target。
5. metrics bridge failure 沿用既有 non-recursive 模式：provider / identity / config / export 类失败进入 degraded，可观测但不直接反噬 diagnostics 主链结果。
6. `DiagnosticsAuditBridge` v1 的 required actions 固定为 `diagnostics.remote_export` 与 `diagnostics.command_extension`；022 只能在这两个动作范围内落桥，不得顺手扩大 audit 面。
7. audit evidence 统一使用 `AuditEvidenceKind::ToolResult`，并用 `snapshot://<snapshot_id>` / `command://<command_id>` 引用既有 diagnostics 锚点，不新增新的 contracts 类型。
8. audit sink 为 required：缺少 logger、write failure 或不一致 outcome 时必须显式失败并阻断对应高风险动作，不允许静默放行。

### 3.1 Diagnostics metrics family / label 投影

| metric family | type | stage 投影 | outcome | error_code |
|---|---|---|---|---|
| `infra_diag_command_total` | Counter | `execute.<command>` | `success` / `failure` / `rejected` | `none` 或 diagnostics 错误码 |
| `infra_diag_command_denied_total` | Counter | `authorize` | `rejected` | `diag_command_denied` |
| `infra_diag_exec_latency_ms` | Histogram | `execute.<command>` | `success` / `failure` | `none`、`INF_E_DIAG_EXEC_TIMEOUT`、`INF_E_DIAG_EXEC_FAIL` |
| `infra_diag_snapshot_store_fail_total` | Counter | `store` | `failure` | `INF_E_DIAG_SNAPSHOT_STORE_FAIL` |
| `infra_diag_export_total` | Counter | `export.local_file` / `export.remote_upload` | `success` / `failure` / `rejected` | 导出相关 diagnostics 错误码 |
| `infra_diag_redaction_fail_total` | Counter | `redaction` | `failure` | `INF_E_DIAG_REDACTION_FAIL` |
| `infra_diag_safe_mode_enter_total` | Counter | `safe_mode` | `degraded` | `none` |

### 3.2 Diagnostics audit action / payload 投影

| 高风险动作 | action | target | outcome 映射 | evidence_ref | side_effects |
|---|---|---|---|---|---|
| `RemoteUpload` 导出请求 | `diagnostics.remote_export` | `diagnostics.export:<target_ref>` | gate 拒绝=`Rejected`；成功=`Succeeded`；backend/校验失败=`Failed` | `snapshot://<snapshot_id>` | `target_ref`、`format`、`result_code`、`detail_ref` |
| 扩展命令执行请求 | `diagnostics.command_extension` | `diagnostics.command:<command_name>` | gate 拒绝=`Rejected`；成功=`Succeeded`；执行/前置失败=`Failed` | `command://<command_id>` | `request_scope`、`result_code`、`detail_ref` |

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 `IMetricsProvider -> IMeter -> record(sample)` 接入协议 | `infra/src/diagnostics/DiagnosticsMetricsBridge.cpp` 只复用既有 metrics 公共接口，不再直连 exporter 或 provider 实现 |
| 冻结 diagnostics 七指标族与 stage/error_code allowlist | `DiagnosticsMetricsAuditBridgeTest` 断言七指标 family、固定 meter scope、stage/outcome/error_code 白名单和 bridge degraded 语义 |
| 冻结 remote export / command extension 的 required audit sink | `infra/src/diagnostics/DiagnosticsAuditBridge.cpp` 对缺失 logger / write failure 返回显式 failure，并阻断高风险动作 |
| 冻结 audit payload 映射 | `DiagnosticsMetricsAuditBridgeTest` 与 `InfraDiagnosticsIntegrationTest` 验证 action/target/evidence_ref/side_effects/context 不越权且 failure 不静默 |

## 5. 对 DIA-TODO-021 / DIA-TODO-022 的直接交接

1. `DIA-TODO-021` 与 `DIA-TODO-022` 可以从 `Blocked` 转为 `Not Started`，并分别按 diagnostics 详细设计 6.10.1 直接实现 metrics bridge 与 audit bridge。
2. 021 的最小完成边界应包括：
   - 七个 diagnostics metric family 的 instrument 注册；
   - `infra.diagnostics@v1` meter scope 与五元标签 allowlist；
   - provider/meter/record failure 的 degraded / best-effort 语义；
   - 不把 bridge failure 递归放大成 diagnostics 主链新的 blocking dependency。
3. 022 的最小完成边界应包括：
   - `diagnostics.remote_export` 高风险动作能稳定写审计；
   - required sink 缺失或 write failure 显式失败；
   - payload 保持在既有 `AuditEvent/AuditContext/AuditEvidenceKind::ToolResult` 边界内；
   - 不顺手扩张 execute/get 普通只读路径的强制审计面。

## 6. Build 三件套

1. 代码目标：更新 diagnostics 详细设计、diagnostics 专项 TODO、infrastructure 总 TODO 和 worklog，并新增 blocker deliverable。
2. 测试目标：执行 process validation，确认 metrics/audit 已冻结接口 gate 仍然通过，且 `DIA-BLK-006` / `DIA-TODO-021` / `DIA-TODO-022` 的台账状态一致。
3. 验收命令：
   - `ctest --test-dir build-ci --output-on-failure -R "(MetricsProviderInterfaceTest|MetricsMeterInterfaceTest|MetricTypesTest|AuditInterfaceCompileTest|AuditBoundaryContractTest)"`
   - `rg -n "6.10.1|infra.diagnostics|diagnostics.remote_export|DIA-BLK-006|DIA-TODO-021|DIA-TODO-022" docs/architecture/DASALL_infra_diagnostics模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md`

## 7. 风险与回退

1. 若 021 后续实现重新引入 `command`、`reason`、`target` 自定义标签，会直接破坏 metrics 子域的 `MetricLabels` allowlist，应立即回退到本轮设计结论。
2. 若 022 试图绕过 `IAuditLogger` 直接写自定义 sink，或把 audit write failure 降级为“只记日志”，会直接回退本轮 blocker fix。
3. 若 diagnostics 后续要把普通只读 execute/get 也升级为 required audit，必须通过新的 design gate 单独评审，而不是在当前 bridge 任务中顺手扩大边界。