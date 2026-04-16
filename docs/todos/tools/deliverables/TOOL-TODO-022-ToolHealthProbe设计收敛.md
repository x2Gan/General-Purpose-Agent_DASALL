# TOOL-TODO-022 ToolHealthProbe 设计收敛

## 背景

`ToolAuditBridge`、`ToolMetricsBridge`、`ToolTraceBridge` 已经分别提供独立的 observability 退化信号，但 tools 子系统仍缺少一个统一的 health 汇聚点，把 registry revision、lane 可用性、MCP freshness 与 observability degraded 状态收敛成可供 ops 与 route 消费的事实快照。

根据详设 6.10、6.12.6 与专项 TODO 的完成条件，本轮 `ToolHealthProbe` 必须满足三条边界：

1. 只消费事实采样，不直接触发恢复或写回任何 health store。
2. 对 route 可用性的判断必须保守，但不能把可降级使用的 stale cache 伪装成 hard-down。
3. 对 registry、lane、cache、observability bridge 的失败要能形成统一 `HealthSnapshot`，并保留 `ToolRouteHealthSnapshot` 供 `ToolRouteSelector` 消费。

## 方案

### internal sample-provider 形态

`ToolHealthProbe` 作为 `tools/src/ops` 下的 internal probe，引入 `IToolHealthSignalProvider` 作为事实采样源。probe 本身不持有 `ToolManager`、`ToolRegistry`、`CapabilityCache`、bridge 实例，只处理采样结果到 health snapshot 的映射，这样后续无论按需采样还是周期采样，都能复用同一聚合逻辑。

采样结构拆分为四个域：

1. `ToolRegistryHealthSample`：提供 `revision`、`descriptor_catalog_ready` 与 `delta_pipeline_degraded`。
2. `ToolLaneHealthSample`：提供 `available`、`concurrency_budget`、`saturated`，用于 builtin / workflow lane 的 route switch 与 readiness 计算。
3. `ToolMCPHealthSample`：提供 `session_ready`、`freshness`、`stale_read_allowed`、`last_error`、`trust_marker`，把 capability freshness 与 MCP route 可用性分开表达。
4. `ToolHealthSample`：额外聚合 `audit_bridge_degraded`、`metrics_bridge_degraded`、`trace_bridge_degraded`、采样时间与采样延迟。

### health 与 route 双视图

`ToolHealthProbe::probe()` 同时更新两类输出：

1. `HealthSnapshot`
2. `ToolRouteHealthSnapshot`

两者遵循不同语义：

1. registry 缺失或 revision 为 0 时，整个 probe 直接 `Unhealthy`，并把 builtin / workflow / mcp 三条 route switch 全部置为 false。
2. builtin lane 不可用、预算为 0 或 sample 标为 saturated 时，probe 保持 liveness=true，但 readiness=false，并把 `tools.builtin_lane` 放入 `failed_components`。
3. workflow lane 不可用只标记 degraded，不把整个 tools 子系统判成 not-ready；这样 builtin 最小闭环不会被 workflow 未来能力反向拖死。
4. MCP session down、capability cache stale/expired 或 last_error 存在时，只要 route 还可安全消费 stale snapshot，就保留 `mcp_lane_healthy=true`；只有 session down、expired 或 stale 不允许时才关闭 mcp route switch。
5. audit / metrics / trace bridge degraded 统一写入 `failed_components`，但不改变 route health，也不阻断主执行链。

### fail-closed 与 fail-open 边界

1. provider 缺失或 sample 时间戳无效时，probe 返回 `ProbeStatus::Unknown`，同时缓存一个保守的 unhealthy snapshot，避免未采样状态被误读为 healthy。
2. 对 registry 缺失与 builtin lane blocked 采用 fail-closed；对 workflow / mcp / observability bridge 的退化采用 degraded-only，保留 builtin 最小闭环的 truthy readiness 语义。
3. `detail_ref` 按失败域生成稳定后缀，例如 `unhealthy/registry`、`degraded/builtin_lane`、`degraded/capability_cache_stale`、`degraded/trace_bridge`，便于后续 observability integration 直接复用。

## 验证

2026-04-16 已完成以下验证：

1. `Build_CMakeTools: all`
2. `RunCtest_CMakeTools: ToolHealthProbeTest`
3. `RunCtest_CMakeTools: ToolTraceBridgeTest`
4. `RunCtest_CMakeTools: ToolManagerPipelineTest`
5. `RunCtest_CMakeTools: ToolObservabilityIntegrationTest`
6. `RunCtest_CMakeTools: ToolServicesSmokeIntegrationTest`
7. `RunCtest_CMakeTools: 全量测试集`

结果：

1. 新增 `ToolHealthProbeTest` 覆盖 registry missing、builtin lane saturation、stale capability cache + trace degraded、provider 缺失四类断言并全部通过。
2. tools 相关定向 unit / integration 用例全部通过。
3. 全量聚合仍只剩既有 infra diagnostics 失败：`InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest`；不属于本任务改动范围。
4. CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，不影响通过结论。

## 收敛结论

`ToolHealthProbe` 已以 internal ops 组件落盘，能够把 registry、lane、MCP freshness 与 observability degraded 信号收敛为统一的 `HealthSnapshot`，同时暴露 `ToolRouteHealthSnapshot` 供 route 消费；实现保持事实聚合边界，没有把恢复逻辑、shared health ABI 或跨模块 live wiring 混入本轮原子任务。