# TRC-TODO-014 TraceHealthProbe 骨架收敛

## 任务定位

- 任务 ID：TRC-TODO-014
- 来源：[docs/todos/infrastructure/DASALL_infrastructure_tracing组件专项TODO.md](/home/gangan/DASALL/docs/todos/infrastructure/DASALL_infrastructure_tracing组件专项TODO.md)
- 目标：在不提前耦合统一 health 注册接口的前提下，为 tracing 导出链路补齐私有降级状态机与健康快照输出，满足“连续失败触发降级、成功恢复回清、快照可读”的最小闭环。

## 本地证据

1. [docs/architecture/DASALL_infra_tracing模块详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_infra_tracing模块详细设计.md)
   - 6.2 指定 TraceHealthProbe 是 tracing 子组件之一。
   - 6.4 指定 SpanProcessorPipeline/BatchSpanBuffer 需要把状态输出给 TraceHealthProbe。
   - 6.8 指定连续失败进入 tracer_degraded_mode，成功恢复后回清 healthy。
   - 6.10 指定健康输出至少要覆盖 queue_depth、dropped_total、exporter_state、degraded。
2. [infra/src/tracing/SpanProcessorPipeline.cpp](/home/gangan/DASALL/infra/src/tracing/SpanProcessorPipeline.cpp)
   - 013 已经把 ended span -> batch -> export 主链打通，并稳定产出 TraceModuleSnapshot 与 TraceOperationStatus。
3. [infra/src/tracing/TracerProviderImpl.cpp](/home/gangan/DASALL/infra/src/tracing/TracerProviderImpl.cpp)
   - provider 当前已经持有共享 pipeline，适合作为 tracing 私有健康快照的读取出口。
4. [infra/src/metrics/MetricsRecovery.cpp](/home/gangan/DASALL/infra/src/metrics/MetricsRecovery.cpp)
   - 已验证的仓库模式是“连续失败阈值触发 degraded，成功路径回清 healthy”，适合复用到 tracing 的 exporter/pipeline 失败治理。
5. [infra/src/secret/SecretHealthProbe.cpp](/home/gangan/DASALL/infra/src/secret/SecretHealthProbe.cpp)
   - 已验证的仓库模式是先输出子系统私有健康对象，再决定是否接统一 health 接口。

## 外部参考

1. Microsoft Azure Architecture Center, Health Endpoint Monitoring pattern
   - 健康检查应组合组件状态与延迟/错误事实，而不是只返回单一可用性位。
   - 健康结果应能暴露组件级细节，便于恢复和诊断。
   - 健康检查本身不能引入过量处理或阻塞主流程，适合采用缓存/快照方式暴露状态。

## 设计结论

1. 本轮不接统一 health 模块接口。
   - TODO 已明确“health 统一接口未冻结”，因此 014 只输出 tracing 私有对象 TraceHealthSnapshot，不注册到 IHealthProbe/HealthMonitor。
2. 区分 exporter 事实和 tracing 健康态。
   - 继续保留 TraceModuleSnapshot 作为 exporter/buffer 事实快照。
   - 新增 TraceHealthSnapshot 作为 tracing 私有健康对象，显式区分 degraded_mode 与 module_snapshot.degraded，避免把“exporter 已 fallback”与“健康判定已进入 degraded”混成一个字段。
3. 连续失败阈值固定为 2。
   - 016 当前未冻结 tracing.health.* 配置键，因此首版采用仓库已验证的最小阈值 2，避免单次瞬时抖动直接拉低健康态。
4. 健康判定挂在 pipeline 内部做 best-effort 观察。
   - SpanProcessorPipeline 在每次 on_end、export_batch、force_flush、shutdown 后根据 last_status + module_snapshot 更新 TraceHealthProbe。
   - 健康判定失败不反向覆盖主链返回值，避免 health skeleton 自身拖垮 tracing 主路径。
5. provider 只负责暴露私有快照读取面。
   - TracerProviderImpl 增加 health_snapshot() 只读出口，供当前单测和后续 015/014 之后的桥接任务消费。

## Design -> Build 映射

| Design 结论 | Build 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| 输出 tracing 私有健康对象 | 新增 infra/src/tracing/TraceHealthProbe.h 与 infra/src/tracing/TraceHealthProbe.cpp | TraceHealthProbeTest：阈值降级、恢复回清、非法输入 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_trace_health_probe_unit_test |
| pipeline best-effort 更新健康态 | 更新 infra/src/tracing/SpanProcessorPipeline.h 与 infra/src/tracing/SpanProcessorPipeline.cpp | TraceHealthProbeTest：provider/pipeline 暴露私有快照 | ctest --test-dir build-ci --output-on-failure -R "TraceHealthProbeTest|BatchExportTest|TracerProviderImplTest" |
| provider 暴露读取面 | 更新 infra/src/tracing/TracerProviderImpl.h 与 infra/src/tracing/TracerProviderImpl.cpp | TraceHealthProbeTest：provider 读取出口稳定 | ctest --test-dir build-ci --output-on-failure -L unit |

## D Gate

- D Gate：PASS
- 进入 Build 的条件：已锁定代码目标、测试目标、验收命令三件套，且范围保持在 tracing 私域内，没有越界到统一 health 接口、metrics bridge 或 audit bridge。