# AUD-TODO-015 AuditMetricsBridge 骨架收敛

日期：2026-04-03  
任务：AUD-TODO-015  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 已将 `AUD-TODO-015` 定义为“实现 AuditMetricsBridge 指标桥接骨架”，验收出口为 `InfraAuditHealthIntegrationTest`。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.10.1 已冻结 audit bridge 的职责边界：只走 `IMetricsProvider` / `IMeter`，只发七个指标族，只允许五元标签白名单，并要求 degraded 不反噬 audit 主写主结果。
3. [docs/architecture/DASALL_infra_metrics模块详细设计.md](docs/architecture/DASALL_infra_metrics模块详细设计.md) 6.6.2/6.8.2 已冻结 `infra.audit@v1` meter scope、七指标对象表，以及 provider/exporter degraded、`QueueFull`、`IdentityInvalid`/`ConfigInvalid` 的桥接失败语义。
4. `AUD-BLK-004` 已完成解阻，因此本轮不再缺设计上下文，剩余工作是把内部 bridge 与最小 integration 证据真正落盘。

## 2. 研究学习结果

### 2.1 本地证据

1. [infra/src/logging/LoggingMetricsBridge.h](infra/src/logging/LoggingMetricsBridge.h) / [infra/src/logging/LoggingMetricsBridge.cpp](infra/src/logging/LoggingMetricsBridge.cpp) 已提供仓内先例：bridge 通过 `IMetricsProvider -> IMeter -> record(sample)` 分层工作，并把 provider/exporter 故障限制在 bridge 自身 degraded 状态内。
2. [tests/integration/infra/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/InfraAuditHealthIntegrationTest.cpp) 在本轮前已经验证 audit health 的 Ready/Degraded/Unavailable 基线，但 metrics degraded 场景仍依赖 test-local 布尔值，缺少真实 bridge 驱动的 ground truth。
3. [infra/CMakeLists.txt](infra/CMakeLists.txt) 已具备独立 audit source 列表，适合以最小增量接入 `AuditMetricsBridge.cpp`，而无需扩张 public API。

### 2.2 外部参考

1. OpenTelemetry Metrics API 强调 instrument identity 由稳定的 meter scope、name、unit 决定，bridge 应只依赖 provider/meter 分层，而不应把 exporter 或内部队列重新暴露给业务模块；这与 audit v1 协议冻结保持一致。

### 2.3 可落地启发

1. audit bridge 首版保持 internal 边界即可，不需要新增 public header 或第二套 audit 私有 metrics sink。
2. 真实 integration 验收应直接读取 `AuditMetricsBridge::is_degraded()`，而不是继续手工注入 `metrics_bridge_degraded=true`。
3. `IdentityInvalid`/`ConfigInvalid` 应收敛为 no-op bridge，避免带着部分初始化状态继续发射；provider/exporter 故障则只把 bridge 标为 degraded，允许后续恢复。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 audit v1 bridge 的内部对象模型 | audit 设计 6.10.1；metrics 设计 6.6.2 | `AuditMetricKind` / `AuditMetricSignal` / `AuditMetricsEmitResult` | 七指标、五标签与结果状态可编译且可判定 |
| D2 | 冻结 degraded / no-op 回退语义 | metrics 设计 6.8.2 | `AuditMetricsBridge` | provider/exporter degraded 与 config/identity no-op 可区分 |
| D3 | 把 metrics degraded 场景接入现有 integration ground truth | 014 已落盘的根级测试出口 | 扩展 `InfraAuditHealthIntegrationTest` | 使用真实 bridge 状态验证 health 映射 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| audit v1 bridge 保持 internal provider/meter-only 分层 | 新增 [infra/src/audit/AuditMetricsBridge.h](infra/src/audit/AuditMetricsBridge.h) 与 [infra/src/audit/AuditMetricsBridge.cpp](infra/src/audit/AuditMetricsBridge.cpp) |
| 七指标对象表、五标签白名单与 degraded/no-op 语义需要自动化校验 | 扩展 [tests/integration/infra/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/InfraAuditHealthIntegrationTest.cpp) |
| 现有根级 integration 继续作为当前轮验收出口 | 更新 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt) 添加 `infra/src` include path |

### 4.2 Build 三件套

1. 代码目标：新增 internal `AuditMetricsBridge`，并更新 [infra/CMakeLists.txt](infra/CMakeLists.txt) 接入 `dasall_infra` 构建图。
2. 测试目标：扩展 `InfraAuditHealthIntegrationTest`，覆盖 `audit_write_total` 成功发射、fallback 路径下 bridge 仍健康、provider timeout -> bridge degraded 且主写结果保持成功、service stopped -> unavailable。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_infra_audit_health_integration_test`
   - `ctest --test-dir build-ci -N -R "InfraAuditHealthIntegrationTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "InfraAuditHealthIntegrationTest"`

### 4.3 D Gate

结论：PASS。

理由：

1. `AUD-BLK-004` 已清除，bridge 的 provider/meter 接入协议、指标族、标签白名单与失败语义都已固定。
2. 当前轮只补 internal bridge 与真实 degraded ground truth，不提前进入 018 的目录/标签拓扑收口。

## 5. Build 落地结果

1. 新增 [infra/src/audit/AuditMetricsBridge.h](infra/src/audit/AuditMetricsBridge.h) 与 [infra/src/audit/AuditMetricsBridge.cpp](infra/src/audit/AuditMetricsBridge.cpp)，冻结 `AuditMetricKind`、`AuditMetricSignal`、`AuditMetricsEmitResult`、`infra.audit@v1` meter scope、七指标 family 与五元标签白名单，并实现 provider degraded / config-invalid no-op 回退逻辑。
2. 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，将 `AuditMetricsBridge.cpp` 纳入 `DASALL_INFRA_AUDIT_SOURCES`。
3. 更新 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt)，为现有 `dasall_infra_audit_health_integration_test` 增加 `infra/src` include path，允许测试直接消费 internal bridge。
4. 更新 [tests/integration/infra/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/InfraAuditHealthIntegrationTest.cpp)，新增 fake `RecordingMetricsProvider` / `RecordingMeter`，把 `AuditServiceBackedHealthProbe` 改为读取真实 `AuditMetricsBridge::is_degraded()`，并断言 `infra.audit@v1` scope、七指标注册与 provider timeout -> `MetricsErrorCode::ExportFailure` 的映射。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_infra_audit_health_integration_test`：通过。
3. `ctest --test-dir build-ci -N -R "InfraAuditHealthIntegrationTest"`：发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "InfraAuditHealthIntegrationTest"`：1/1 通过。

## 7. 结论

1. `AUD-TODO-015` 已从“设计冻结”推进到“internal bridge + 真实 degraded 协同 ground truth”落盘完成。
2. 本轮继续沿用根级 `InfraAuditHealthIntegrationTest` 作为最小验收出口；`AUD-TODO-018` 仍保留为 audit integration 子目录、聚合 target 与 `integration;audit` 标签的 discoverability 收口任务。
