# LOG-BLK-003 LoggingHealthProbe 设计收敛

日期：2026-04-03  
任务：LOG-BLK-003  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 将 LOG-BLK-003 定义为“health 探针接口未冻结：LoggingHealthProbe 输出对象和超时语义不明确”。
2. [docs/architecture/DASALL_infra_health模块详细设计.md](docs/architecture/DASALL_infra_health模块详细设计.md) 已在 6.5/6.6 冻结 `IHealthProbe`、`ProbeDescriptor`、`ProbeResult` 与超时/失败返回语义。
3. [docs/architecture/DASALL_infra_logging模块详细设计.md](docs/architecture/DASALL_infra_logging模块详细设计.md) 已明确 logging 侧存在 `LoggingHealthProbe`，但尚未把它如何映射到 health 通用接口写成正式设计证据。

## 2. 解阻结论

1. `LoggingHealthProbe` 不新增 logging 私有 probe result 或第二套 health interface，而是直接实现通用 [infra/include/health/IHealthProbe.h](infra/include/health/IHealthProbe.h)。
2. `LoggingHealthProbe` 的固定 descriptor 冻结为：
   - `probe_name = "infra.logging.pipeline"`
   - `group = "readiness"`
   - `criticality = Critical`
   - `interval_ms = 5000`
   - `timeout_ms = 100`
3. `probe()` 只读取 logging 本地可观测信号，不依赖 runtime 主状态机：`queue_depth`、`dropped_total_delta`、`recovery_degraded`、`fallback_active`、`unrecoverable_failure_total`、`metrics_bridge_degraded`。
4. 状态映射冻结为：
   - Healthy：无降级、无未恢复失败、队列未越过高水位、无新增 drop。
   - Degraded：fallback 已启用，或 recovery/metrics bridge 处于 degraded，或队列高水位/新增 drop 被观测到。
   - Unhealthy：主/降级写入链都不可用，或存在未恢复的不可恢复失败。
5. 超时语义冻结为：`probe()` 必须是只读、无副作用、本地状态采样；若状态采样在 descriptor `timeout_ms` 内无法完成，则返回结构化失败 `ProbeResult`，而不是阻塞 logging 主链。

## 3. Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| LoggingHealthProbe 直接实现通用 `IHealthProbe` | 后续任务只落 `infra/src/logging/LoggingHealthProbe.cpp/.h`，不新增私有 health 接口 |
| descriptor 与状态映射已冻结 | 后续单测只验证 descriptor 合法性、Healthy/Degraded/Unhealthy 三态映射和 timeout failure |
| 输入信号来自 logging 本地状态 | 后续实现通过最小 `LoggingHealthSignals`/provider 注入读取 `queue_depth`、`drop_total`、`recovery`、`metrics bridge` 状态 |

## 4. 验证结果

1. `grep -n "IHealthProbe\|ProbeDescriptor\|ProbeResult\|timeout_ms" docs/architecture/DASALL_infra_health模块详细设计.md infra/include/health/IHealthProbe.h infra/include/health/ProbeTypes.h`：可定位到通用 health probe 契约已冻结。
2. `grep -n "infra.logging.pipeline\|LoggingHealthProbe\|readiness\|unrecoverable_failure_total" docs/architecture/DASALL_infra_logging模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md docs/todos/infrastructure/deliverables/LOG-BLK-003-LoggingHealthProbe设计收敛.md`：可定位到 logging 侧 probe descriptor、状态映射与解阻回写。

## 5. 后续边界

1. 本轮只完成 health probe 边界解阻，不实现 `LoggingHealthProbe` 代码；实现继续由后续 `LOG-TODO-017` 承接。
2. `LoggingHealthProbe` 只输出事实状态和 detail_ref，不输出恢复裁定或任何 runtime 指令，继续遵守 ADR-007 的恢复边界。