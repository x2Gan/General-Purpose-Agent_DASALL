# HealthCadenceAndEventBoundary (Single Source of Truth)

关联任务：INT-TODO-026  
关联来源：HLT-TODO-009/012/014；RT-OQ-06；COG-OQ05；INT-BLK-07

## 1. 目标

本文件冻结 health cadence / config / event publish boundary 的系统口径，收敛以下问题：

1. `ProbeScheduler` 应以什么 timer seam 运转。
2. `HealthConfigPolicy` 如何决定 default cadence 与覆盖优先级。
3. event publish 最小接口未冻结前，health 子系统允许的 fallback 行为是什么。
4. runtime background maintenance、cognition health probe、profile 键域、`diag_enabled` / 诊断开关之间的关系是什么。

## 2. 总结论

1. `platform::ITimer` 是 post-unary health cadence 的唯一 timer seam；`ProbeScheduler` 不允许绕开 `ITimer` 自建 sleep loop、裸线程轮询或第二套计时抽象。
2. `HealthConfigPolicy` 是 health cadence 与阈值配置的唯一合成入口；其合成顺序固定为“模块默认值 < Profile 基线 < deployment override”，`infra.health.*` 不开放 `runtime_override`。
3. runtime 只消费 health cadence 的投影视图，不拥有 cadence 语义主控权；不得在 `AgentOrchestrator` 中硬编码第二套默认周期。
4. cognition health probe 首版仍是 metrics 聚合视图，不引入独立 probe endpoint，不引入独立 scheduler，不反向定义 system cadence。
5. event publish 最小接口未冻结前，health 子系统只允许走“本地状态提交 + logging/metrics + 受控 audit/diagnostics + 局部缓存”的 fallback 路径，不宣称 bus-ready。

## 3. Owner / Consumer Matrix

| 主题 | semantic owner | consumer / execute owner | 冻结结论 |
|---|---|---|---|
| default cadence 基线 | infra health 详设 + `HealthConfigPolicy` 默认表 | `ProbeScheduler` | 默认 cadence 由 health 6.9 配置表定义，不由 runtime/cognition 私自改写 |
| effective cadence 覆盖顺序 | profiles (`infra.health.*`) | `HealthConfigPolicy::merge(default, profile, deploy)` | `infra.health.*` 只允许默认/Profile/部署三层；不进入 `runtime_override` |
| timer seam | platform | `ProbeScheduler` | 统一使用 `ITimer` / `TimerSpec` / `TimerHandle`；不再把 `HLT-BLK-001` 当作抽象前置 blocker |
| runtime background health probe consume | runtime | `AgentOrchestrator` / background maintenance hooks | runtime 只在启动时和空闲窗口触发 health probe；具体 cadence 读取 health 配置投影 |
| cognition health probe | cognition telemetry local view | infra health / runtime consume path | 首版只使用 metrics 汇聚（`last_latency` / `error_count` 等）构造 health probe 输入，不独立调度 |
| event publish fallback | infra health | `HealthEventPublisher` / logging / metrics / audit | event publish 接口未冻结或 sink 缺失时，只允许 fallback，不允许把 bus 缺位当成系统不可评估 |
| diagnostics / audit rich sink gate | profiles / app bootstrap | runtime / apps | `ops_policy.remote_diagnostics_enabled` 与 app/bootstrap `diag_enabled` 只控制 richer diagnostics sink，不改写 cadence 本身 |

## 4. Default Cadence And Config Rules

`HealthConfigPolicy` 必须以 health 详设 6.9 的默认表作为模块基线，至少冻结以下键：

| 配置键 | 默认值 | 覆盖层级 | 规则 |
|---|---|---|---|
| `infra.health.enabled` | `true` | 默认/Profile/部署 | 关闭后不再注册周期 health probe，但同步 `evaluate_now` 仍可保留为显式调用面 |
| `infra.health.liveness.interval_ms` | `2000` | Profile/部署 | liveness cadence；由 `ProbeScheduler` 通过 `ITimer` 执行 |
| `infra.health.readiness.interval_ms` | `5000` | Profile/部署 | readiness cadence；不得在 runtime 中硬编码第二套默认值 |
| `infra.health.probe.timeout_ms` | `1000` | Profile/部署 | 单 probe timeout；属于 `HealthConfigPolicy` 的合成结果 |
| `infra.health.event_on_transition_only` | `true` | 默认/Profile | event publish 只在状态变化时尝试进入 publish pipeline |
| `infra.health.recovery_hint.enabled` | `true` | Profile/部署 | 只控制建议输出，不控制 recovery 执行权 |

补充规则：

1. `HealthConfigPolicy` 只负责合成 default/profile/deploy 三层，不为 `runtime_override` 提供热改入口。
2. runtime 消费的是合成后的 cadence / timeout 投影，而不是 profile YAML 文本。
3. 若部署方未显式覆盖 cadence，则沿用默认值；不得把“缺省即无 cadence”解释为停用 `ProbeScheduler`。

## 5. ProbeScheduler And ITimer Boundary

1. `ProbeScheduler` 的 post-unary baseline 直接建立在 `platform::ITimer` 之上，使用 `TimerSpec.mode`、`interval_ms`、`initial_delay_ms` 与 `TimerHandle` 表达调度语义。
2. 首版允许 `HealthMonitorFacade::evaluate_now()` 继续作为同步入口；这不是对 `ProbeScheduler` 的替代，而是 event publish/interface 未完全冻结时的最小可执行基线。
3. `ProbeScheduler` 的 start/stop/tick_once 语义由 `INT-TODO-029` 落代码，但 026 之后不再允许把“timer seam 不存在”作为 stale blocker。
4. 若 `ITimer` provider 不可用，允许降级到“禁用周期调度 + 保留同步 evaluate_now + 记录 fallback 证据”的路径；不允许悄悄切换到私有轮询实现。

## 6. Event Publish Fallback Rule

在 event publish 最小接口与 EventEnvelope 约束未冻结前，系统只允许以下 fallback 路径：

1. `HealthStateStore` 先提交本地快照与 transition version。
2. `HealthMetricsBridge` 必须继续输出 health 指标与 event publish fail 计数。
3. logging / audit 允许 best-effort 写入结构化事件，作为 event publish 的最小替代证据。
4. 若 richer diagnostics sink 受 `ops_policy.remote_diagnostics_enabled` 或 app/bootstrap `diag_enabled` 关闭，则仍必须保留本地 logging/metrics fallback；这些开关不能让 health 评估静默失踪。
5. 缺失 event sink 或 publish 失败，不得让 `evaluate_now()`、快照提交或 readiness 判定失败闭锁；只能记录 fallback、局部缓存 transition，并保持“not bus-ready”的状态说明。

明确禁止：

1. 在 event bus 未冻结时宣称 `HealthEventPublisher` 已具备 external bus delivery guarantee。
2. 把 event publish 失败直接升级为 runtime 主状态机失败。
3. 因 `diag_enabled=false` 或 `remote_diagnostics_enabled=false` 而关闭基础 logging/metrics health signal。

## 7. Runtime / Cognition Relationship

1. runtime background maintenance 的 health probe 只消费 `HealthConfigPolicy` 合成后的 cadence 结果，并以 non-blocking 方式挂在空闲窗口或启动阶段。
2. runtime 可以依据 health 结果触发 safe mode / degraded 路径，但不重新定义 probe group、cadence 默认值或 publish 语义。
3. cognition health probe 首版继续采纳 `COG-OQ05` 的收口：使用 metrics 聚合（如 `last_latency`、`error_count`）作为被动 health probe 输入，不增加独立 endpoint、独立 cadence 或独立 event publish 通道。
4. 因此，cognition 是 health probe 的输入来源之一，不是 system cadence owner，也不是 event publish owner。

## 8. Executable Baseline Vs Deferred Bus Scope

026 之后，health 残项必须重新分账：

### 8.1 可执行基线

1. `HealthConfigPolicy::merge(default, profile, deploy)` 与阈值校验。
2. `ProbeScheduler` 沿 `ITimer` seam 的最小 start/stop/tick_once 骨架。
3. runtime 对合成后 cadence 的 consume points。
4. event sink 缺失时的 logging/metrics/local cache fallback。

### 8.2 仍待冻结或实现的 bus 扩展

1. event bus 最小 publish_transition API。
2. EventEnvelope 约束、ack/failure surface 与 external delivery guarantee。
3. richer diagnostics sink 的扩展协议。

这意味着：`HLT-TODO-009` 与 `HLT-TODO-014` 属于可执行基线；`HLT-TODO-012` 仍受 event publish 最小接口冻结影响，但必须遵守本文件的 fallback rule。

## 9. Design -> Build 映射

1. `INT-TODO-029` 必须基于本文件把 `HealthConfigPolicy`、`ProbeScheduler`、runtime cadence consume 点与 `InfraHealthCadenceIntegrationTest` 对齐。
2. `HLT-TODO-009` 不能再以“缺 timer seam”为理由停留在 Blocked；其剩余工作是沿 `ITimer` 实现 scheduler baseline。
3. `HLT-TODO-012` 在 event bus 接口未冻结前，只能实现 fallback-compatible publisher surface，不得越权声称 external bus ready。
4. `COG-TODO-022` 若继续推进 cognition health probe，只能复用 metrics 聚合输入或 health 子系统既有 probe 面，不得反向定义新 cadence。

## 10. 完成判定

当且仅当以下条件同时成立时，才允许把 health cadence / config / event publish boundary 视为系统级 SSOT：

1. `ProbeScheduler`、`HealthConfigPolicy`、`ITimer` 与 default cadence 的归属一致。
2. `infra.health.*` 的覆盖顺序与不开放 `runtime_override` 的结论一致。
3. event publish fallback 已被明确为 logging/metrics/audit/local cache 路径，且不再把 stale blocker 误写成“timer seam 缺失”。
4. runtime、infra、profiles、相关 TODO 对 cadence / config / event publish / health probe 的表述一致。