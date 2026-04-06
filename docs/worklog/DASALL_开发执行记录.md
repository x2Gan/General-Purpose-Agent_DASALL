# DASALL 开发执行记录

## 使用说明

- 目的：用于在每次会话开始时快速回溯中断点，并继续推进实施计划。
- 追加规则：新记录追加在文件顶部（最新优先）。
- 记录最小字段：日期、阶段/任务、完成内容、关键产物、验证结果、下一步、风险/注意事项。

---

## 记录 #134

- 日期：2026-04-06
- 阶段：metrics 组件专项 TODO
- 任务：MET-TODO-013 实现 MetricReaderScheduler 调度骨架
- 状态：已完成

### 改动

1. 完成 MET-TODO-013-D/B 落盘：
   - 新增 infra/src/metrics/MetricReaderScheduler.h 与 infra/src/metrics/MetricReaderScheduler.cpp，落盘 `schedule_tick`、`flush_on_shutdown`、`pop_next_batch`、pending queue 与 last batch 观测面。
   - 通过 `MetricsResolvedConfig` 消费 016 已冻结的 `reader_interval_ms` 与 `exporter_type` 默认值，把 scheduler 固定为“到点生成 batch / shutdown 强制 flush”的单工作线程骨架，而不提前掺入 exporter 逻辑。
2. 完成 013 的 unit/CMake 收口：
   - 新增 tests/unit/infra/metrics/MetricsReaderSchedulerTest.cpp，覆盖 interval gating 与 shutdown flush 两条关键路径。
   - 更新 tests/unit/infra/CMakeLists.txt，新增 `dasall_metrics_reader_scheduler_unit_test` 与 `MetricsReaderSchedulerTest` 注册，并保留 `MetricsConfigMergeTest` 作为依赖回归。
3. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md，将 `MET-TODO-013` 标记为 Done，并补齐本轮 Design->Build 映射、Build_CMakeTools 回退记录与 unit gate 证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_metrics_reader_scheduler_unit_test dasall_metrics_config_merge_unit_test`
   - `ctest --test-dir build-ci -N -R "(MetricsReaderSchedulerTest|MetricsConfigMergeTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(MetricsReaderSchedulerTest|MetricsConfigMergeTest)"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
2. 结果：
   - `Build_CMakeTools` 再次失败，错误为“生成失败：无法配置项目”；已按仓库既定回退策略切回 build-ci 命令链。
   - build-ci 重新配置成功，`dasall_metrics_reader_scheduler_unit_test` 与 `dasall_metrics_config_merge_unit_test` 构建通过。
   - `MetricsConfigMergeTest` 与 `MetricsReaderSchedulerTest` 被 ctest 发现并定向执行通过，2/2 tests passed。
   - `dasall_unit_tests` 聚合目标构建通过；`ctest -L unit` 通过，unit 标签 140/140 tests passed。
   - 构建过程中仍存在仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是 013 新引入的问题。

### 结果

1. MET-TODO-013 已把 metrics 主链推进到 `aggregation -> reader scheduler`，AggregationSnapshot 现在可以按配置间隔形成待导出 batch，并在 shutdown 时强制 flush。
2. `MET-TODO-014` 现在可以直接消费 scheduler 产出的 `MetricExportBatch` 队列，实现 noop/prom_text 首版导出骨架。

### 下一步

1. 执行 `MET-TODO-014`，实现 `MetricsExporterAdapter` 的 noop/prom_text 导出、失败回退与 exporter 状态观测，并与 013 的 batch 队列连通。

### 风险

1. 当前 MetricReaderScheduler 只覆盖单队列调度骨架，没有线程模型、队列上限或 overflow policy；后续推进 014/015 时必须维持“调度器只决定何时出 batch，不承担恢复与退避策略”的边界。

## 记录 #133

- 日期：2026-04-06
- 阶段：metrics 组件专项 TODO
- 任务：MET-TODO-016 定义 MetricsConfigPolicy 配置模型与默认策略
- 状态：已完成

### 改动

1. 完成 MET-TODO-016-D/B 落盘：
   - 新增 infra/src/metrics/MetricsConfigPolicy.h 与 infra/src/metrics/MetricsConfigPolicy.cpp，落盘 `MetricsConfigPatch`、`MetricsResolvedConfig`、`merge(default/profile/deploy/runtime)` 与 `validate_histogram_buckets`，冻结 metrics 的最小配置模型、默认值和四层覆盖顺序。
   - 在不改动公共接口的前提下，让 private `MetricsConfigPolicy` 具体实现既有 `IMetricConfigPolicy`，保持 `validate_identity`、`normalize_labels`、`should_accept` 与已冻结接口门禁一致。
2. 完成 016 的 unit/CMake 收口：
   - 新增 tests/unit/infra/metrics/MetricsConfigMergeTest.cpp，覆盖默认值、覆盖优先级与非单调 histogram bucket 拒绝。
   - 更新 tests/unit/infra/CMakeLists.txt，新增 `dasall_metrics_config_merge_unit_test` 与 `MetricsConfigMergeTest` 注册，并保留 `MetricsConfigPolicyInterfaceTest` 做接口回归。
3. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md，将 `MET-TODO-016` 标记为 Done，并补齐本轮 Design->Build 映射、Build_CMakeTools 回退记录与 unit gate 证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_metrics_config_merge_unit_test dasall_metrics_config_policy_interface_unit_test`
   - `ctest --test-dir build-ci -N -R "(MetricsConfigMergeTest|MetricsConfigPolicyInterfaceTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(MetricsConfigMergeTest|MetricsConfigPolicyInterfaceTest)"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
2. 结果：
   - `Build_CMakeTools` 再次失败，错误为“生成失败：无法配置项目”；已按仓库既定回退策略切回 build-ci 命令链。
   - build-ci 重新配置成功，`dasall_metrics_config_merge_unit_test` 与 `dasall_metrics_config_policy_interface_unit_test` 构建通过。
   - `MetricsConfigPolicyInterfaceTest` 与 `MetricsConfigMergeTest` 被 ctest 发现并定向执行通过，2/2 tests passed。
   - `dasall_unit_tests` 聚合目标构建通过；`ctest -L unit` 通过，unit 标签 139/139 tests passed。
   - 本轮初版单测里 `MetricsConfigPatch` 的部分指定初始化曾触发新告警，已在同轮收口为显式 patch 变量初始化，最终验证输出不再包含该告警。

### 结果

1. MET-TODO-016 已为 metrics 后续的 scheduler/exporter 冻结 `enabled/provider/exporter/reader_interval/exporter_timeout/labels/histogram_buckets` 最小配置模型和覆盖顺序。
2. `MET-TODO-013` 的前置依赖已解除，下一轮可以直接实现 MetricReaderScheduler 的调度骨架。

### 下一步

1. 执行 `MET-TODO-013`，把 AggregationEngine 的快照读取与周期调度批次骨架落盘，并消费 016 已冻结的 reader interval / exporter timeout 默认值。

### 风险

1. 当前 MetricsConfigPolicy 只冻结了最小 patch/resolved config 结构，并未接入 ConfigCenter、Profile 文档解析或运行时回滚；后续推进 013/014 时必须把它视为局部策略骨架，而不是完整配置子系统桥接层。

## 记录 #132

- 日期：2026-04-06
- 阶段：metrics 组件专项 TODO
- 任务：MET-TODO-012 实现 CardinalityGuard 标签治理骨架
- 状态：已完成

### 改动

1. 完成 MET-TODO-012-D/B 落盘：
   - 新增 infra/src/metrics/CardinalityGuard.h 与 infra/src/metrics/CardinalityGuard.cpp，落盘 `validate_labels`、`reject_with_reason`、空 `error_code -> none` 归一化、未知标签拒绝与 per-metric label signature cardinality 上限控制，并统一复用 `MetricsErrors::LabelCardinalityExceeded` 错误面。
   - 通过 `MetricLabelEntry` 与 `CardinalityGuardDecision` 固定 allowlist 输入/输出语义，使“未知标签”与“高基数超阈值”两类拒绝路径都可二值判定。
2. 完成 012 的 façade 接线与 unit/CMake 收口：
   - 调整 infra/src/metrics/MetricsFacade.h 与 infra/src/metrics/MetricsFacade.cpp，把 `record()` 从 `registry -> aggregation` 推进到 `registry -> guard -> aggregation`，并新增 `module_snapshot()` 暴露 `guard_reject_total`。
   - 新增 tests/unit/infra/metrics/MetricsCardinalityGuardTest.cpp，覆盖 allowlist 正例、未知标签拒绝、高基数拒绝与 façade 集成回归；同步更新 tests/unit/infra/CMakeLists.txt，为 `MetricsFacadeTest`、`MetricsAggregationTest` 补编 `CardinalityGuard.cpp` 并新增 `MetricsCardinalityGuardTest` 注册。
3. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md，将 `MET-TODO-012` 标记为 Done，并补齐本轮 Design->Build 映射、Build_CMakeTools 回退记录、discoverability 与 unit gate 证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_metrics_facade_unit_test dasall_metrics_cardinality_guard_unit_test dasall_metrics_aggregation_unit_test`
   - `ctest --test-dir build-ci -N -R "(MetricsCardinalityGuardTest|MetricsFacadeTest|MetricsAggregationTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(MetricsCardinalityGuardTest|MetricsFacadeTest|MetricsAggregationTest)"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
2. 结果：
   - `Build_CMakeTools` 再次失败，错误为“生成失败：无法配置项目”；已按仓库既定回退策略切回 build-ci 命令链。
   - build-ci 重新配置成功，`dasall_metrics_facade_unit_test`、`dasall_metrics_cardinality_guard_unit_test`、`dasall_metrics_aggregation_unit_test` 构建通过。
   - `MetricsFacadeTest`、`MetricsCardinalityGuardTest`、`MetricsAggregationTest` 被 ctest 发现并定向执行通过，3/3 tests passed。
   - `dasall_unit_tests` 聚合目标构建通过；`ctest -L unit` 通过，unit 标签 138/138 tests passed。
   - 构建过程中仍存在仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是 012 新引入的问题。

### 结果

1. MET-TODO-012 已把 metrics 主链推进到 `record -> registry -> guard -> aggregation`，拒绝样本现在会在进入聚合前被拦截，并把 `guard_reject_total` 暴露到模块快照。
2. `MET-TODO-013` 仍依赖 `MET-TODO-016` 的配置模型，因此下一轮需要先执行 016 作为 scheduler 的前置解组任务。

### 下一步

1. 执行 `MET-TODO-016`，冻结 metrics 最小配置模型与默认策略，为 `MET-TODO-013` 的 reader interval / exporter timeout 语义提供稳定输入。

### 风险

1. 当前 CardinalityGuard 只覆盖固定 allowlist 与 per-metric signature 计数，还没有引入 context enricher、queue overflow 或动态 taxonomy 扩展；后续推进 013~015 时必须维持这一最小边界，不得把它误用为完整的治理中心。

## 记录 #131

- 日期：2026-04-06
- 阶段：metrics 组件专项 TODO
- 任务：MET-TODO-011 实现 AggregationEngine 聚合骨架
- 状态：已完成

### 改动

1. 完成 MET-TODO-011-D/B 落盘：
   - 新增 infra/src/metrics/AggregationEngine.h 与 infra/src/metrics/AggregationEngine.cpp，落盘 `aggregate_counter`、`aggregate_gauge`、`aggregate_histogram`、`aggregate` 与 `snapshot`，把 Counter/Gauge/Histogram 的单线程可测聚合语义固定下来。
   - 为 Histogram 增加默认 explicit bucket 计数，为 Counter/Gauge/UpDownCounter 固定累计值/最新值语义，并对 same-name semantic drift 保持 `IdentityInvalid` 错误面。
2. 完成 011 的 façade 接线与 unit/CMake 收口：
   - 调整 infra/src/metrics/MetricsFacade.h 与 infra/src/metrics/MetricsFacade.cpp，新增 `AggregationEngine` 成员和 `aggregation_snapshot()` 观测口，把 `record()` 从 registry-only 检查推进到 registry + aggregation 主链。
   - 新增 tests/unit/infra/metrics/MetricsAggregationTest.cpp，覆盖 Counter/Gauge/Histogram 聚合断言与 `record -> registry -> aggregation` 主链；同步更新 tests/unit/infra/CMakeLists.txt，为 `MetricsFacadeTest` 增编 `AggregationEngine.cpp` 并注册 `MetricsAggregationTest`。
3. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md，将 `MET-TODO-011` 标记为 Done，并补齐本轮 Design->Build 映射、discoverability、unit gate 与主链闭环证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_metrics_facade_unit_test dasall_instrument_registry_unit_test dasall_metrics_aggregation_unit_test`
   - `ctest --test-dir build-ci -N -R "(MetricsAggregationTest|InstrumentRegistryTest|MetricsFacadeTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(MetricsAggregationTest|InstrumentRegistryTest|MetricsFacadeTest)"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
2. 结果：
   - build-ci 重新配置成功，`dasall_metrics_facade_unit_test`、`dasall_instrument_registry_unit_test`、`dasall_metrics_aggregation_unit_test` 构建通过。
   - `MetricsFacadeTest`、`InstrumentRegistryTest`、`MetricsAggregationTest` 被 ctest 发现并定向执行通过，3/3 tests passed。
   - `dasall_unit_tests` 聚合目标构建通过；`ctest -L unit` 通过，unit 标签 137/137 tests passed。
   - 构建过程中仍存在仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是 011 新引入的问题。

### 结果

1. MET-TODO-011 已把 metrics 主链推进到 `record -> registry -> aggregation` 的最小闭环，Counter/Gauge/Histogram 三类聚合行为现在均可稳定回归。
2. 用户要求的主链路骨架 `MET-TODO-009~011` 已按顺序全部落盘并各自独立提交到远端。

### 下一步

1. 若继续推进 metrics，实现 `MET-TODO-012`，把标签治理和高基数防护接到当前 façade->registry->aggregation 主链上。

### 风险

1. 当前 `AggregationEngine` 仍是单线程、私有快照实现，尚未落窗口滚动、reader/exporter 对接和并发保护；后续推进 `MET-TODO-012~014` 时必须保持这一前提，不得把当前实现误判为最终性能形态。

## 记录 #130

- 日期：2026-04-06
- 阶段：metrics 组件专项 TODO
- 任务：MET-TODO-010 实现 InstrumentRegistry 唯一性管理骨架
- 状态：已完成

### 改动

1. 完成 MET-TODO-010-D/B 落盘：
   - 新增 infra/src/metrics/InstrumentRegistry.h 与 infra/src/metrics/InstrumentRegistry.cpp，落盘 `register_identity`、`find_identity`、`size` 与 `InstrumentRegistrationResult`，把 `MetricIdentity(name/type/unit/description)` 固化为 canonical identity 判定面。
   - 实现“同 identity 幂等返回同一 handle、同名异义拒绝注册”的最小唯一性约束，并统一复用 `MetricsErrors::IdentityInvalid` 错误面。
2. 完成 010 的 façade 接线与 unit/CMake 收口：
   - 调整 infra/src/metrics/MetricsFacade.h 与 infra/src/metrics/MetricsFacade.cpp，把 `FacadeMeter::create_*` 路径切到 registry，并要求 record 仅接受已注册 identity。
   - 新增 tests/unit/infra/metrics/InstrumentRegistryTest.cpp，覆盖重复注册正例与同名冲突负例；同时更新 tests/unit/infra/CMakeLists.txt，为 `MetricsFacadeTest` 补编 `InstrumentRegistry.cpp` 并新增 `InstrumentRegistryTest` 注册。
3. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md，将 `MET-TODO-010` 标记为 Done，并补齐本轮 Design->Build 映射、discoverability、unit gate 与 façade->registry 接线证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_metrics_facade_unit_test dasall_instrument_registry_unit_test`
   - `ctest --test-dir build-ci -N -R "(InstrumentRegistryTest|MetricsFacadeTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(InstrumentRegistryTest|MetricsFacadeTest)"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
2. 结果：
   - build-ci 重新配置成功，`dasall_metrics_facade_unit_test` 与 `dasall_instrument_registry_unit_test` 构建通过。
   - `MetricsFacadeTest` 与 `InstrumentRegistryTest` 被 ctest 发现并定向执行通过，2/2 tests passed。
   - `dasall_unit_tests` 聚合目标构建通过；`ctest -L unit` 通过，unit 标签 136/136 tests passed。
   - 构建过程中仍存在仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是 010 新引入的问题。

### 结果

1. MET-TODO-010 已把 metrics 主链从“只有 façade 占位”推进到“façade + registry 唯一性约束已可编译、可测试、可回归”的状态。
2. `MET-TODO-011` 现在可以在 registry 已稳定产出 canonical handle 的前提下，继续把 sample 写入推进到 `AggregationEngine` 的 Counter/Gauge/Histogram 聚合骨架。

### 下一步

1. 实现 `MET-TODO-011`，新增 `AggregationEngine` 私有实现与聚合单测，并把 `MetricsFacade` 的 record 路径从“只做 registry 检查”推进到“registry + aggregation”最小闭环。

### 风险

1. 当前 `InstrumentRegistry` 仅覆盖 canonical registration 和 lookup，没有落 remove/lifecycle cleanup，也未建模 scope 级冲突；在后续 exporter/scheduler/health 接线前，不应把它视为完整生命周期管理器。

## 记录 #129

- 日期：2026-04-06
- 阶段：metrics 组件专项 TODO
- 任务：MET-TODO-009 实现 MetricsFacade 初始化与写入骨架
- 状态：已完成

### 改动

1. 完成 MET-TODO-009-D/B 落盘：
   - 新增 infra/src/metrics/MetricsFacade.h 与 infra/src/metrics/MetricsFacade.cpp，落盘 `MetricsFacade` lifecycle、meter cache、last sample 观测面，以及以内嵌 `FacadeMeter` 承担的 `create_counter/create_gauge/create_histogram/record` 最小代理语义。
   - 使用已冻结 `MetricsErrors` 映射 `ProviderNotReady`、`ConfigInvalid`、`IdentityInvalid` 三类失败路径，保证未初始化、无效 deadline/config、非法 sample/identity 均返回可判定 contracts 错误面。
2. 完成 009 的 unit/CMake 接线：
   - 新增 tests/unit/infra/metrics/MetricsFacadeTest.cpp，覆盖未初始化拒绝、同 scope meter 缓存与有效 record 正例、非法 identity 负例。
   - 更新 tests/unit/infra/CMakeLists.txt，注册 `dasall_metrics_facade_unit_test` 与 `MetricsFacadeTest`；在 `MET-TODO-017` 完成前，暂由 unit target 直接编译 private `MetricsFacade.cpp`，不提前把 metrics 源码接入 `dasall_infra`。
3. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md，将 `MET-TODO-009` 标记为 Done，并补齐本轮 Design->Build 映射、discoverability、unit gate 与提交隔离证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_metrics_facade_unit_test`
   - `ctest --test-dir build-ci -N -R MetricsFacadeTest`
   - `ctest --test-dir build-ci --output-on-failure -R MetricsFacadeTest`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
2. 结果：
   - build-ci 重新配置成功，`dasall_metrics_facade_unit_test` 构建通过。
   - `MetricsFacadeTest` 被 ctest 发现并定向执行通过，1/1 tests passed。
   - `dasall_unit_tests` 聚合目标构建通过；`ctest -L unit` 通过，unit 标签 135/135 tests passed。
   - 构建过程中仍存在仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是 009 新引入的问题。

### 结果

1. MET-TODO-009 已从“metrics 无运行时源码”推进到“存在可编译、可测试、可观测的 MetricsFacade 最小主链入口骨架”。
2. `MET-TODO-010` 现在可以在不改写 provider/meter 错误面的前提下，继续把仪表唯一性管理从 façade 内部占位推进到独立 `InstrumentRegistry`。

### 下一步

1. 实现 `MET-TODO-010`，新增 `InstrumentRegistry` 私有实现与同名同语义唯一性单测，并把 `MetricsFacade` 的 instrument 创建路径切到 registry 骨架。

### 风险

1. 当前 `MetricsFacade` 的 instrument 管理仍是 façade 内部最小占位，尚未具备 6.3 要求的“同名同语义唯一”判定；在 `MET-TODO-010` 完成前，不能把当前 meter handle 缓存误判为 registry 已落地。

## 记录 #128

- 日期：2026-04-06
- 阶段：health 组件专项 TODO
- 任务：HLT-TODO-018 回写 health 质量门与交付证据
- 状态：已完成

### 改动

1. 完成 HLT-TODO-018-D/B 落盘：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md，将 `HLT-TODO-018` 标记为 Done，并补齐本轮 gate/blocked/rollback 收口记录。
   - 同步修正 9.1 的 integration 基线说明、10 的风险与回退策略，以及 11 的下一步建议，去除 `HLT-TODO-017` 完成前遗留的过时口径。
2. 完成 018 的 gate 证据归档：
   - 将 `HLT-GATE-01/02/03/05/06/07/09` 回写为 PASS，把 `HLT-GATE-04` 保持为 blocked 前置 gate，把 `HLT-GATE-08` 标记为本轮未触发。
   - 明确当前未解阻台账仍为 `HLT-TODO-009 -> HLT-BLK-001`、`HLT-TODO-012 -> HLT-BLK-002`、`HLT-TODO-014 -> HLT-BLK-003`。

### 测试

1. 验证命令：
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `ctest -N` 通过，总 discoverability 为 290 个测试。
   - `ctest -L unit` 通过，unit 标签 134/134 tests passed。
   - `ctest -L contract` 通过，contract 标签 140/140 tests passed。

### 结果

1. HLT-TODO-018 已把 health 当前可执行主链的 gate、blocked 现状与回退边界统一收口到专项 TODO。
2. health 当前已完成 façade、registry、executor、evaluator、error mapping、recovery hint、源码入图、测试发现性与质量证据闭环；后续只剩 009/012/014 三条 blocked 链路待解阻后继续推进。

### 下一步

1. 等待 `HLT-BLK-001`、`HLT-BLK-002`、`HLT-BLK-003` 解阻后，分别推进 `HLT-TODO-009`、`HLT-TODO-012`、`HLT-TODO-014`。

### 风险

1. 当前 health integration 仍只覆盖 minimal wiring smoke；即使主链 gate 已闭环，scheduler/event/config 三条 blocked 链路仍未进入 integration/failure/profile 范围，后续解阻后必须补齐对应用例，避免质量门出现“主链通过但扩展链路无证据”的断层。

## 记录 #127

- 日期：2026-04-06
- 阶段：health 组件专项 TODO
- 任务：HLT-TODO-017 注册 health 的 unit/contract/integration 测试入口
- 状态：已完成

### 改动

1. 完成 HLT-TODO-017-D/B 落盘：
   - 更新 tests/unit/infra/CMakeLists.txt 与 tests/contract/CMakeLists.txt，为现有 health unit/contract 测试补齐 `health` 标签，形成组件级 discoverability 入口。
   - 更新 tests/integration/CMakeLists.txt 与 tests/integration/infra/CMakeLists.txt，把 `dasall_health_wiring_integration_test` 纳入 integration 聚合目标并接入 health 子目录。
   - 新增 tests/integration/infra/health/CMakeLists.txt 与 tests/integration/infra/health/HealthWiringIntegrationTest.cpp，落盘 health 最小 wiring smoke，验证 registry/executor/evaluator/recovery hint 的可执行主链。
2. 完成 017 的测试收敛：
   - `HealthWiringIntegrationTest` 覆盖 all-healthy snapshot 与 repeated failure -> critical recovery hint 两条主路径。
   - 现有 health unit/contract 用例在保留 `unit`、`contract`、`smoke` 原标签的同时新增 `health` 标签，保证组件级定向回归不会影响全仓既有 gate。
3. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md，将 `HLT-TODO-017` 标记为 Done，并补齐本轮执行记录、discoverability、integration smoke 与全量 gate 证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests dasall_health_wiring_integration_test`
   - `ctest --test-dir build-ci -N -L health`
   - `ctest --test-dir build-ci --output-on-failure -R HealthWiringIntegrationTest`
   - `ctest --test-dir build-ci --output-on-failure -L health`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - build-ci 配置成功，health integration 目标与 unit/contract 聚合目标构建通过。
   - `ctest -N -L health` 发现 17 个带 `health` 标签的测试。
   - `HealthWiringIntegrationTest` 定向执行通过，1/1 tests passed。
   - `ctest -L health` 通过，health 标签 17/17 tests passed。
   - `ctest -L unit` 通过，unit 标签 134/134 tests passed；`ctest -L contract` 通过，contract 标签 140/140 tests passed。

### 结果

1. HLT-TODO-017 已从“health 测试存在但缺少统一 discoverability 与 integration 入口”推进到“health unit/contract/integration 测试可按组件统一发现并执行”。
2. `HLT-TODO-018` 现在可以基于稳定的 health 标签与 gate 结果，专门收口 health 质量门、阻塞台账与交付证据，而不必再兼顾测试注册实现。

### 下一步

1. 实现 `HLT-TODO-018`，回写 health 质量门、integration 准入变化、阻塞现状与交付证据，完成本轮 health 可执行主链收口。

### 风险

1. 当前 health integration 仍是最小 wiring smoke，并未覆盖 scheduler/event/config blocked 领域；后续推进 `HLT-TODO-009`、`HLT-TODO-012`、`HLT-TODO-014` 时，需要继续保持 `health` 标签下的 smoke 与 blocked 链路分离，避免把未解阻实现混入现有 discoverability 结果。

## 记录 #126

- 日期：2026-04-06
- 阶段：health 组件专项 TODO
- 任务：HLT-TODO-016 注册 health 源码到 infra CMake
- 状态：已完成

### 改动

1. 完成 HLT-TODO-016-D/B 落盘：
   - 更新 infra/CMakeLists.txt，新增 `DASALL_INFRA_HEALTH_SOURCES`、`DASALL_INFRA_HEALTH_PRIVATE_HEADERS`，把 health 私有实现统一纳入 `dasall_infra`。
   - 为 `dasall_infra` 增加 PRIVATE `src` include 路径，保证 health 私有头在库内按 `health/...` 路径可解析，同时不外泄到 public include 面。
2. 完成 016 对 health unit 目标的去重：
   - 更新 tests/unit/infra/CMakeLists.txt，使 health 相关 unit 目标不再直接编译 `infra/src/health/*.cpp`，改为只编测试文件并链接 `dasall_infra`，消除源码入图后的重复符号风险。
3. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md，将 `HLT-TODO-016` 标记为 Done，并补齐本轮执行记录、定向 health build 回归与全量 gate 证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_health_monitor_facade_unit_test dasall_probe_registry_unit_test dasall_probe_executor_unit_test dasall_health_evaluator_unit_test dasall_recovery_hint_emitter_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R "(HealthMonitorFacadeTest|ProbeRegistryTest|ProbeExecutorTest|HealthEvaluatorTest|RecoveryHintEmitterTest)"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `dasall_infra` 与 5 个 health unit 目标构建通过，health 源码正式入图后未出现重复符号或 include 路径错误。
   - 5 个定向 health unit 测试通过，5/5 tests passed。
   - `dasall_unit_tests` 通过，unit 标签 134/134 tests passed；`dasall_contract_tests` 通过，contract 标签 140/140 tests passed。

### 结果

1. HLT-TODO-016 已从“health 仅靠单测直编实现文件”推进到“health 私有源码正式成为 `dasall_infra` 的库内成员”。
2. `HLT-TODO-017` 现在可以在库接线稳定的前提下补齐 health 的 integration 注册与测试发现性，不必再兼顾源码重复编译问题。

### 下一步

1. 实现 `HLT-TODO-017`，新增 `tests/integration/infra/health/` 目录与 health integration 目标，并完成 unit/contract/integration 发现性验证。

### 风险

1. 当前 build 输出仍会看到仓库既有的 `IMetricsProvider.h` missing initializer warning；这不是 016 新引入的问题，但后续若要收紧 `-Werror` 门禁，需要单独处理该基线告警。

## 记录 #125

- 日期：2026-04-06
- 阶段：health 组件专项 TODO
- 任务：HLT-TODO-015 实现 RecoveryHintEmitter 边界守卫骨架
- 状态：已完成

### 改动

1. 完成 HLT-TODO-015-D/B 落盘：
   - 新增 infra/src/health/RecoveryHintEmitter.h 与 infra/src/health/RecoveryHintEmitter.cpp，落盘 `RecoveryHintEmissionResult`、`emit_hint` 与 `sanitize_hint_payload`，把三态快照收敛为 advisory-only `RecoveryHint` 输出。
   - 使用 `audit://health/recovery_hint/` 作为稳定 evidence_ref 锚点，把状态、snapshot version、failed_components 与 sanitize 后的 reason 写入建议证据，确保后续审计桥接前已有稳定引用面。
2. 完成 015 的 unit/CMake 接线：
   - 新增 tests/unit/infra/health/RecoveryHintEmitterTest.cpp，覆盖 degraded/unhealthy advisory 输出、healthy snapshot 拒绝与 sanitize 路径。
   - 更新 tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，注册 `dasall_recovery_hint_emitter_unit_test` 与 `RecoveryHintEmitterTest`。
3. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md，将 `HLT-TODO-015` 标记为 Done，并补齐本轮执行记录、发现性、全量 gate 与提交隔离证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_recovery_hint_emitter_unit_test dasall_contract_recovery_hint_boundary_test`
   - `ctest --test-dir build-ci --output-on-failure -R "(RecoveryHintEmitterTest|RecoveryHintBoundaryContractTest)"`
   - `ctest --test-dir build-ci -N -R "(RecoveryHintEmitterTest|RecoveryHintBoundaryContractTest)"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - build-ci 配置成功，定向 unit/contract 目标构建通过。
   - `RecoveryHintEmitterTest` 与 `RecoveryHintBoundaryContractTest` 定向执行通过，2/2 tests passed。
   - `RecoveryHintEmitterTest` 与 `RecoveryHintBoundaryContractTest` 被 ctest 发现并完成注册。
   - `dasall_unit_tests` 通过，unit 标签 134/134 tests passed；`dasall_contract_tests` 通过，contract 标签 140/140 tests passed。

### 结果

1. HLT-TODO-015 已从“只有 RecoveryHint 对象和 contract 模板”推进到“存在可编译、可测试、带审计锚点的 RecoveryHintEmitter 最小实现”。
2. `HLT-TODO-016` 现在具备把 health 私有源码整体注册进 `dasall_infra` 的前提条件，下一步可以收口 health 源码入图与测试去重。

### 下一步

1. 实现 `HLT-TODO-016`，把当前 health 私有源码统一接入 infra CMake，并同步调整 health unit 目标，避免源文件重复编译。

### 风险

1. 当前 `RecoveryHintEmitter` 仍是独立骨架，尚未与 `HealthEvaluator` 或未来事件发布链直接接线；后续推进 016/017 时需要保持“建议输出”和“恢复执行”分层，不得因为源码入图而绕过 ADR-007。

## 记录 #124

- 日期：2026-04-06
- 阶段：health 组件专项 TODO
- 任务：HLT-TODO-013 定义 HealthErrors 错误码域与映射
- 状态：已完成

### 改动

1. 完成 HLT-TODO-013-D/B 落盘：
   - 新增 infra/include/health/HealthErrors.h，冻结 `HealthErrorCode`、`HealthErrorMapping`、`health_error_code_name` 与 `map_health_error_code`，把 6.6 中 5 个 health 私有错误码固定到统一映射矩阵。
   - 更新 infra/CMakeLists.txt，将 `HealthErrors.h` 纳入 infra 公共头集合，确保错误语义进入对外边界清单。
2. 完成 013 对现有失败路径的统一接线：
   - 调整 infra/src/health/ProbeRegistry.cpp、infra/src/health/ProbeExecutor.cpp、infra/src/health/HealthEvaluator.cpp，使 missing probe、probe timeout、probe exception、policy invalid 等路径统一走 `HealthErrors` 映射，不再散落硬编码 `ResultCode`。
   - 同步更新 tests/unit/infra/health/HealthEvaluatorTest.cpp，使 invalid input 断言与新的 policy failure 映射一致。
3. 完成 013 的 unit/contract/CMake 接线：
   - 新增 tests/unit/infra/health/HealthErrorsTest.cpp，冻结枚举值、名称与 source anchor 可观察性。
   - 新增 tests/contract/smoke/HealthErrorMappingContractTest.cpp，冻结 health 私有错误码到 contracts `ResultCode` 的映射矩阵。
   - 更新 tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt，注册 `dasall_health_errors_unit_test`、`HealthErrorsTest` 与 `dasall_contract_health_error_mapping_test`。
4. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md，将 `HLT-TODO-013` 标记为 Done，并补齐本轮执行记录、发现性、全量 unit/contract gate 与提交隔离证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_health_errors_unit_test dasall_contract_health_error_mapping_test dasall_health_evaluator_unit_test dasall_probe_executor_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R "(HealthErrorsTest|HealthErrorMappingContractTest|HealthEvaluatorTest|ProbeExecutorTest)"`
   - `ctest --test-dir build-ci -N -R "(HealthErrorsTest|HealthErrorMappingContractTest)"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`
2. 结果：
   - build-ci 配置成功，上述定向 unit/contract 目标全部构建通过。
   - `HealthErrorsTest`、`HealthErrorMappingContractTest`、`HealthEvaluatorTest`、`ProbeExecutorTest` 定向执行通过，4/4 tests passed。
   - `HealthErrorsTest` 与 `HealthErrorMappingContractTest` 被 ctest 发现并完成注册。
   - `dasall_unit_tests` 通过，unit 标签 133/133 tests passed；`dasall_contract_tests` 通过，contract 标签 140/140 tests passed。

### 结果

1. HLT-TODO-013 已从“health 私有错误语义只存在于设计文档”推进到“存在可编译、可测试、可追溯的 `HealthErrors` 公共头与统一映射矩阵”。
2. health 当前已具备 façade、registry、executor、evaluator 与 error mapping 的最小闭环，后续可以在不改写错误语义的前提下继续推进 `HLT-TODO-015` 或解阻 009/012/014。

### 下一步

1. 优先实现 `HLT-TODO-015`，补齐 RecoveryHintEmitter 边界守卫骨架，并复用已冻结的 `RecoveryHintBoundaryContractTest` 与当前 health 三态/错误语义输出。

### 风险

1. `INF_E_HEALTH_EVENT_PUBLISH_FAIL` 已冻结名称与映射，但 `HLT-TODO-012` 仍阻塞于事件总线最小接口未冻结；后续实现 publisher 时必须严格复用本轮已固化的映射矩阵，避免引入第二套失败语义。

## 记录 #123

- 日期：2026-04-06
- 阶段：health 组件专项 TODO
- 任务：HLT-TODO-011 实现 HealthEvaluator 三态评估骨架
- 状态：已完成

### 改动

1. 完成 HLT-TODO-011-D/B 落盘：
   - 新增 infra/src/health/HealthEvaluator.h 与 infra/src/health/HealthEvaluator.cpp，落盘 `HealthEvaluator`、`policy_version()`、`evaluate` 与 `evaluate_transition`，固定默认三态收敛与状态转移输出。
   - 在 `HLT-BLK-003` 未解阻前，仅按 6.9 默认阈值实现最小策略，不引入 profile 覆盖；任一 `Unhealthy` 结果直接进入 failed snapshot，其余失败先收敛为 degraded snapshot。
2. 完成 011 的 unit/CMake 接线：
   - 新增 tests/unit/infra/health/HealthEvaluatorTest.cpp，覆盖 invalid input 失败、Healthy/Degraded/Unhealthy 判定与 transition 输出。
   - 更新 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt，注册 `dasall_health_evaluator_unit_test` 与 `HealthEvaluatorTest`。
3. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md，将 `HLT-TODO-011` 标记为 Done，并补齐本轮执行记录、外部参考、发现性和 unit gate 证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_health_evaluator_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R HealthEvaluatorTest`
   - `ctest --test-dir build-ci -N -R HealthEvaluatorTest`
   - `cmake --build build-ci --target dasall_unit_tests`
2. 结果：
   - build-ci 配置成功，`dasall_health_evaluator_unit_test` 构建通过。
   - `HealthEvaluatorTest` 定向执行通过，1/1 tests passed。
   - `HealthEvaluatorTest` 被 ctest 发现并注册到 unit 标签。
   - `dasall_unit_tests` 通过，unit 标签 132/132 tests passed。

### 结果

1. HLT-TODO-011 已从“ProbeResult 已可执行但无统一三态聚合策略”推进到“存在可编译、可测试、保持 `IHealthPolicy` 边界的 HealthEvaluator 最小实现”。
2. `HLT-TODO-013` 现在可以基于已落盘的 executor/evaluator 失败路径，冻结 health 私有错误码域与 contracts 映射矩阵。

### 下一步

1. 实现 `HLT-TODO-013`，冻结 `HealthErrors` 错误码域与映射测试，完成本轮“评估与错误语义”收口。

### 风险

1. 当前 evaluator 只按 `ProbeResult.status` 做默认三态聚合，尚未纳入 profile 驱动的 critical group 与运行时覆盖；待 `HLT-BLK-003` 解阻后，需要再评估是否补充更细的 readiness/liveness 差异策略。

## 记录 #122

- 日期：2026-04-06
- 阶段：health 组件专项 TODO
- 任务：HLT-TODO-010 实现 ProbeExecutor 执行骨架
- 状态：已完成

### 改动

1. 完成 HLT-TODO-010-D/B 落盘：
   - 新增 infra/src/health/ProbeExecutor.h 与 infra/src/health/ProbeExecutor.cpp，落盘 `ProbeExecutor`、`execute_once`、`execute_batch`、连续失败计数查询，以及 timeout/exception/missing probe 的结构化失败路径。
   - 保持 010 为同步执行骨架：不引入线程取消或调度抽象，而是通过执行耗时后验判定 timeout，并把单次失败映射为 `Degraded`、连续失败达到阈值后提升为 `Unhealthy`。
2. 完成 010 的 unit/CMake 接线：
   - 新增 tests/unit/infra/health/ProbeExecutorTest.cpp，覆盖 timeout、异常、批量执行与 repeated failure escalation。
   - 更新 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt，注册 `dasall_probe_executor_unit_test` 与 `ProbeExecutorTest`。
3. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md，将 `HLT-TODO-010` 标记为 Done，并补齐本轮执行记录、外部参考、发现性和 unit gate 证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_probe_executor_unit_test dasall_probe_registry_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R "(ProbeExecutorTest|ProbeRegistryTest)"`
   - `ctest --test-dir build-ci -N -R ProbeExecutorTest`
   - `cmake --build build-ci --target dasall_unit_tests`
2. 结果：
   - build-ci 配置成功，`dasall_probe_executor_unit_test` 与 `dasall_probe_registry_unit_test` 构建通过。
   - `ProbeExecutorTest` 与 `ProbeRegistryTest` 定向执行通过，2/2 tests passed。
   - `ProbeExecutorTest` 被 ctest 发现并注册到 unit 标签。
   - `dasall_unit_tests` 通过，unit 标签 131/131 tests passed。

### 结果

1. HLT-TODO-010 已从“registry 已具备但执行链为空”推进到“存在可编译、可测试的同步 ProbeExecutor 执行骨架”。
2. `HLT-TODO-011` 现在可以直接消费 `ProbeResult` 序列和连续失败语义，继续实现三态评估骨架。

### 下一步

1. 实现 `HLT-TODO-011`，落盘 `HealthEvaluator` 三态评估骨架，并补 Healthy/Degraded/Unhealthy 判定与状态转移 unit 验证。

### 风险

1. 当前 timeout 仍采用同步执行后的后验判定，不具备线程级提前中断能力；待 `HLT-TODO-009` 解阻后，需要再评估是否将 timeout 检测升级为真实调度超时控制。

## 记录 #121

- 日期：2026-04-06
- 阶段：health 组件专项 TODO
- 任务：HLT-TODO-008 实现 ProbeRegistry 注册治理骨架
- 状态：已完成

### 改动

1. 完成 HLT-TODO-008-D/B 落盘：
   - 新增 infra/src/health/ProbeRegistry.h 与 infra/src/health/ProbeRegistry.cpp，落盘 `ProbeRegistry`、`ProbeRegistryRegisterResult`、`ProbeRegistryRemoveResult`，覆盖同名唯一校验、按组查询、descriptor 查找与注销路径。
   - 按 health 详细设计 6.9 的默认周期/超时为 `ProbeDescriptor` 补齐最小默认值，并在 profile critical group 尚未冻结前将 `criticality` 保持为 `NonCritical` 占位，避免伪造运行策略。
2. 完成 008 对 007 的直接接线：
   - 调整 infra/src/health/HealthMonitorFacade.h 与 infra/src/health/HealthMonitorFacade.cpp，使 façade 的注册治理委托 `ProbeRegistry`，不再自行维护内部 map。
3. 完成 008 的 unit/CMake 接线：
   - 新增 tests/unit/infra/health/ProbeRegistryTest.cpp，覆盖重复注册拒绝、分组查询与注销一致性。
   - 更新 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt，注册 `dasall_probe_registry_unit_test`，并把 `ProbeRegistry.cpp` 纳入 `HealthMonitorFacadeTest` 回归编译链路。
4. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md，将 `HLT-TODO-008` 标记为 Done，并补齐本轮执行记录、外部参考、发现性和 unit gate 证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_probe_registry_unit_test dasall_health_monitor_facade_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R "(ProbeRegistryTest|HealthMonitorFacadeTest)"`
   - `ctest --test-dir build-ci -N -R ProbeRegistryTest`
   - `cmake --build build-ci --target dasall_unit_tests`
2. 结果：
   - build-ci 配置成功，`dasall_probe_registry_unit_test` 与 `dasall_health_monitor_facade_unit_test` 构建通过。
   - `ProbeRegistryTest` 与 `HealthMonitorFacadeTest` 定向执行通过，2/2 tests passed。
   - `ProbeRegistryTest` 被 ctest 发现并注册到 unit 标签。
   - `dasall_unit_tests` 通过，unit 标签 130/130 tests passed。

### 结果

1. HLT-TODO-008 已从“health façade 内部占位注册逻辑”推进到“存在独立 ProbeRegistry 注册治理骨架，并由 façade 直接委托”。
2. `HLT-TODO-010` 现在可以直接复用 `ProbeRegistry` 的 descriptor/probe 查询能力，继续实现执行骨架，而不必再重建注册存储。

### 下一步

1. 实现 `HLT-TODO-010`，落盘 `ProbeExecutor` 执行骨架，并补超时/异常结构化返回与批量执行 unit 验证。

### 风险

1. 当前 `ProbeRegistry` 仅填充默认 interval/timeout 与 `NonCritical` criticality，占位服务于执行链打通；待 profile 键命名与 critical group 策略冻结后，仍需由后续任务把默认值切换为真实配置驱动。

## 记录 #120

- 日期：2026-04-06
- 阶段：health 组件专项 TODO
- 任务：HLT-TODO-007 实现 HealthMonitorFacade 生命周期骨架
- 状态：已完成

### 改动

1. 完成 HLT-TODO-007-D/B 落盘：
   - 新增 infra/src/health/HealthMonitorFacade.h 与 infra/src/health/HealthMonitorFacade.cpp，落盘 `HealthMonitorFacade` 私有实现，覆盖 `register_probe`、`evaluate_now`、`get_snapshot`、`subscribe` 与 `enter_safe_observe_mode_for_test` 生命周期骨架。
   - 将 `evaluate_now` 收敛为 007 阶段允许的占位快照输出：仅在存在已注册 probe 时返回带 version/timestamp 的 `HealthSnapshot`，在 `safe_observe_mode` 下拒绝新评估并保留最近一次成功快照，不越权进入 registry/executor/evaluator 主链。
2. 完成 007 的 unit/CMake 接线：
   - 新增 tests/unit/infra/health/HealthMonitorFacadeTest.cpp，覆盖未初始化失败、注册后成功求值与 `safe_observe_mode` 失败分支。
   - 更新 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt，注册 `dasall_health_monitor_facade_unit_test` 与 `HealthMonitorFacadeTest`，保证新用例进入 unit 聚合目标。
3. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md，将 `HLT-TODO-007` 标记为 Done，并补齐本轮执行记录、外部参考、发现性和 unit gate 证据。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_health_monitor_facade_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R HealthMonitorFacadeTest`
   - `ctest --test-dir build-ci -N -R HealthMonitorFacadeTest`
   - `cmake --build build-ci --target dasall_unit_tests`
2. 结果：
   - build-ci 配置成功，`dasall_health_monitor_facade_unit_test` 构建通过。
   - `HealthMonitorFacadeTest` 被 ctest 发现并定向执行通过，1/1 tests passed。
   - `dasall_unit_tests` 通过，unit 标签 129/129 tests passed。

### 结果

1. HLT-TODO-007 已从“health 无服务实现”推进到“存在可编译、可测试、保持 frozen `IHealthMonitor` 边界的 façade 生命周期骨架”。
2. `HLT-TODO-008` 现在可以在不重改 public API 的前提下继续把注册治理从 façade 内部占位逻辑抽离到独立 `ProbeRegistry`。

### 下一步

1. 实现 `HLT-TODO-008`，落盘 `ProbeRegistry` 注册治理骨架，并补重复注册拒绝与分组查询 unit 验证。

### 风险

1. 当前 `HealthMonitorFacade` 仍通过占位快照固定主入口语义，尚未接入 `ProbeRegistry`、`ProbeExecutor` 与 `HealthEvaluator`；在 `HLT-TODO-008/010/011` 完成前，不应把 007 的最小实现误判为主链闭环已完成。

## 记录 #119

- 日期：2026-04-06
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-022 回写 policy 质量门与交付证据
- 状态：已完成

### 改动

1. 完成 POL-TODO-022 的专项 TODO 收口：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 `POL-TODO-022` 标记为 Done，并同步刷新阶段 G、验收命令基线、Gate 执行快照、Blocker 状态快照、验证/回退记录与维护态结论。
   - 新增 `## 36. 本轮执行记录（2026-04-06 / POL-TODO-022）`，把 022 的 Design 结论、最终 gate 统计与提交隔离要求回写到专项 TODO。
2. 完成专项质量门口径修正：
   - 移除“integration 尚未落盘”的过时表述，改为显式回链 `POL-TODO-018` 与 `POL-TODO-021` 的 integration 证据。
   - 将 policy 第 9 章补齐到 `POL-GATE-01`~`POL-GATE-08`、`POL-BLK-001`~`POL-BLK-006` 全量快照，确保 gate 结论与当前仓库状态一致。

### 测试

1. 验证命令：
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `ctest -N` 发现 267 个测试。
   - `ctest -L unit` 通过，128/128 tests passed。
   - `ctest -L contract` 通过，139/139 tests passed。
   - integration 证据沿用上一轮 `POL-TODO-021` 的聚合验收结果：`ctest --test-dir build-ci --output-on-failure -L integration` 15/15 通过。

### 结果

1. POL-TODO-022 已完成，policy 组件专项 TODO 的 `POL-TODO-001`~`POL-TODO-022` 现已全部 Done，`POL-BLK-001`~`POL-BLK-006` 全部为 Resolved。
2. policy 专项 TODO 的第 9 章现已与当前 build-ci gate、integration 交付和 blocker 状态同步，不再保留阶段 F 完成后的旧口径。

### 下一步

1. policy 组件专项 TODO 已收口；后续仅在 shared semantic、public boundary 或新的 integration/failure 场景出现时再新开原子任务。

### 风险

1. 若后续引入新的 policy public boundary、shared `PolicyDecision` 对象或 bridge 语义扩展，必须重新开任务并重跑 gate，不能直接沿用本次收口结论。

## 记录 #118

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-021 实现 PolicyHealthProbe 健康探针骨架
- 状态：已完成

### 改动

1. 完成 POL-TODO-021-D/B 落盘：
   - 新增 infra/src/policy/PolicyHealthProbe.h 与 infra/src/policy/PolicyHealthProbe.cpp，落盘 `PolicyHealthSignals`、`PolicyHealthSample`、`IPolicyHealthSignalProvider` 与 `PolicyHealthProbe`，把 current/LKG snapshot、最近失败原因、safe_mode 与 bridge degraded 事实映射到 `Healthy/Degraded/Unhealthy`。
   - 固定 probe descriptor 为 `infra.policy.snapshot` / `readiness` / `Critical`，并把 detail_ref 收敛到 `status://policy/health/...` 命名空间，同时在 ready/degraded 分支编码 snapshot generation。
2. 完成 021 的 CMake/test 接线：
   - 更新 infra/CMakeLists.txt，把 PolicyHealthProbe 私有实现纳入 dasall_infra。
   - 更新 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/integration/infra/policy/CMakeLists.txt、tests/integration/CMakeLists.txt，注册 `PolicyHealthProbeTest` 与 `PolicyHealthIntegrationTest` 并纳入聚合目标。
3. 完成 unit/integration 门禁落盘：
   - 新增 tests/unit/infra/PolicyHealthProbeTest.cpp，覆盖 frozen descriptor、ready/degraded/unavailable 映射与 timeout 结构化失败。
   - 新增 tests/integration/infra/policy/PolicyHealthIntegrationTest.cpp，使用真实 SecurityPolicyManager + PolicySnapshotStore 验证 commit fail 保持旧 generation 的 degraded readiness，以及 repeated patch failure 进入 safe_mode 后的 degraded readiness。
4. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 POL-TODO-021 标记为 Done，并补齐本轮执行记录、build-ci 定向测试结果与标签级验收结果。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_policy_health_probe_unit_test dasall_policy_health_integration_test`
   - `ctest --test-dir build-ci -N -R "PolicyHealth(Probe|Integration)Test"`
   - `ctest --test-dir build-ci --output-on-failure -R "PolicyHealth(Probe|Integration)Test"`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L integration`
2. 结果：
   - build-ci 配置成功，`dasall_infra`、`dasall_policy_health_probe_unit_test`、`dasall_policy_health_integration_test` 构建通过。
   - `ctest -N -R "PolicyHealth(Probe|Integration)Test"` 发现 2 个目标测试。
   - 定向执行通过，2/2 tests passed。
   - `ctest -L unit` 通过，128/128 tests passed；`ctest -L integration` 通过，15/15 tests passed。

### 结果

1. POL-TODO-021 已从“metrics/health 依赖已解阻但 policy health probe 未落盘”推进到“存在可编译、可测试、保持 frozen health boundary 的 PolicyHealthProbe 最小实现”。
2. policy 的观测桥接与集成阶段现已完成 018、019、020、021 四个原子任务；后续只剩 022 的质量门与交付证据收口。

### 下一步

1. 实现 `POL-TODO-022`，回写 policy 专项质量门、阻塞变化与交付证据，完成本专项 TODO 收口。

### 风险

1. 当前 PolicyHealthProbe 仍通过私有 signal provider 采样 manager/store 状态，尚未把审计/指标 bridge 的真实状态接入主链；在 022 收口前，不应把这种最小接线误判为缺失设计边界。

## 记录 #117

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-020 实现 PolicyMetricsBridge 指标桥接骨架
- 状态：已完成

### 改动

1. 完成 POL-TODO-020-D/B 落盘：
   - 新增 infra/src/policy/PolicyMetricsBridge.h 与 infra/src/policy/PolicyMetricsBridge.cpp，落盘 `PolicyMetricKind` 七个冻结指标族、`PolicyMetricSignal` 样本约束、固定 `infra.policy/v1` meter scope 与 `module/stage/profile/outcome/error_code` 标签白名单。
   - 复用 metrics 冻结接口与错误语义，把 bridge 失败统一收敛到既有 `MetricsErrorCode`，并保持 `active_generation` 为 gauge、其余 family 为 counter。
2. 完成 020 的 CMake/test 接线：
   - 更新 infra/CMakeLists.txt，把 PolicyMetricsBridge 私有实现纳入 dasall_infra。
   - 更新 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt，注册 `PolicyMetricsBridgeTest` 与 `PolicyMetricsBridgeBoundaryContractTest` 并纳入聚合目标。
3. 完成 unit/contract 门禁落盘：
   - 新增 tests/unit/infra/PolicyMetricsBridgeTest.cpp，覆盖计数器/gauge 发射、provider/meter 失败降级与非法 stage 预拒绝。
   - 新增 tests/contract/smoke/PolicyMetricsBridgeBoundaryContractTest.cpp，验证 policy metrics bridge 只注册七个冻结 metric family，且标签维持在既有 allowlist 内。
4. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 POL-TODO-020 标记为 Done，并补齐本轮执行记录、build-ci 定向测试结果与标签级验收结果。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_policy_metrics_bridge_unit_test dasall_contract_policy_metrics_bridge_boundary_test`
   - `ctest --test-dir build-ci -N -R "PolicyMetricsBridge(Test|BoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "PolicyMetricsBridge(Test|BoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - build-ci 配置成功，`dasall_infra`、`dasall_policy_metrics_bridge_unit_test`、`dasall_contract_policy_metrics_bridge_boundary_test` 构建通过。
   - `ctest -N -R "PolicyMetricsBridge(Test|BoundaryContractTest)"` 发现 2 个目标测试。
   - 定向执行通过，2/2 tests passed。
   - `ctest -L unit` 通过，127/127 tests passed；`ctest -L contract` 通过，139/139 tests passed。

### 结果

1. POL-TODO-020 已从“metrics/health 依赖已解阻但 policy metrics bridge 未落盘”推进到“存在可编译、可测试、保持 frozen metrics boundary 的 PolicyMetricsBridge 最小实现”。
2. policy 的观测桥接阶段现已完成 audit 与 metrics 两个分支；后续只剩 021 的 health probe 实现与最终 022 证据收口。

### 下一步

1. 实现 `POL-TODO-021`，落盘 PolicyHealthProbe 健康探针骨架，并补 unit/integration 验证。

### 风险

1. 当前 PolicyMetricsBridge 仍是私有 bridge 骨架，尚未接入 SecurityPolicyManager 主链；在 021 与后续 022 未闭环前，不应把未接线状态误判为缺失设计边界。

## 记录 #116

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-BLK-004 解阻校准
- 状态：已完成

### 改动

1. 完成 POL-BLK-004 证据核验：
   - 复核 metrics 组件专项 TODO 中 `MET-TODO-001`~`MET-TODO-008` 的完成状态，确认 `IMetricsProvider`、`IMeter`、`IMetricConfigPolicy`、`IMetricsHealthProbe`、`MetricTypes`、`MetricsSnapshots`、`MetricsErrors` 已冻结。
   - 复核 health 组件专项 TODO 中 `HLT-TODO-001`~`HLT-TODO-006` 的完成状态，确认 `IHealthProbe`、`IHealthMonitor`、`IHealthPolicy`、`HealthStateTypes`、`RecoveryHint` 已冻结。
   - 复核 tests/unit/infra/MetricTypesTest.cpp、tests/contract/smoke/MetricsConfigPolicyInterfaceBoundaryContractTest.cpp、tests/unit/infra/HealthSnapshotTest.cpp、tests/contract/smoke/HealthSnapshotBoundaryContractTest.cpp、tests/contract/smoke/HealthMonitorInterfaceBoundaryContractTest.cpp，确认 policy 所需的标签白名单和健康状态对象边界已有可执行门禁。
2. 完成 policy 台账校准：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 `POL-BLK-004` 标记为已解阻，并将 `POL-TODO-020`、`POL-TODO-021` 从 Blocked 校准为 Not Started。
   - 同步刷新“当前 Blocked 任务索引”和阶段 F 的顺序说明，使后续 020/021 可以继续串行推进。

### 测试

1. 验证命令：
   - `ctest --test-dir build-ci -N -R "(MetricsProviderInterfaceTest|MetricsMeterInterfaceTest|MetricsConfigPolicyInterfaceTest|MetricsHealthProbeInterfaceTest|MetricTypesTest|MetricsProviderInterfaceBoundaryContractTest|MetricsMeterInterfaceBoundaryContractTest|MetricsConfigPolicyInterfaceBoundaryContractTest|HealthSnapshotUnitTest|HealthSnapshotBoundaryContractTest|HealthMonitorInterfaceBoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(MetricsProviderInterfaceTest|MetricsMeterInterfaceTest|MetricsConfigPolicyInterfaceTest|MetricsHealthProbeInterfaceTest|MetricTypesTest|MetricsProviderInterfaceBoundaryContractTest|MetricsMeterInterfaceBoundaryContractTest|MetricsConfigPolicyInterfaceBoundaryContractTest|HealthSnapshotUnitTest|HealthSnapshotBoundaryContractTest|HealthMonitorInterfaceBoundaryContractTest)"`
2. 结果：
   - 相关 metrics/health gate 当前发现 11 个测试。
   - 定向执行通过，11/11 tests passed。

### 结果

1. POL-BLK-004 已从“policy 文档中的历史阻塞项”校准为“已由 metrics/health 接口冻结与边界门禁实质解阻”的状态。
2. `POL-TODO-020` 与 `POL-TODO-021` 现在可以作为下一轮可执行原子任务继续推进。

### 下一步

1. 实现 `POL-TODO-020`，落盘 PolicyMetricsBridge 指标桥接骨架，并补 unit/contract 验证。

### 风险

1. 若 metrics 标签白名单、provider/meter 接口或 health 状态对象边界未来回退，`POL-BLK-004` 需要重新转回 Blocked；当前解阻结论依赖 metrics/health 专项 TODO 与 build-ci 定向 gate 的持续有效性。

## 记录 #115

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-019 实现 PolicyAuditBridge 审计桥接骨架
- 状态：已完成

### 改动

1. 完成 POL-TODO-019-D/B 落盘：
   - 新增 infra/src/policy/PolicyAuditBridge.h 与 infra/src/policy/PolicyAuditBridge.cpp，落盘 `emit_load_result`、`emit_patch_result`、`emit_rollback_result`、`emit_high_risk_deny` 四类审计桥接路径，并维持 `AuditEvidenceKind::ToolResult` 与 `side_effects` 事实输出边界。
   - 新增 `PolicyAuditBridgeStatus` 与 `PolicyAuditEmitResult`，把 bridge 自身可观测性收敛到发射计数、失败计数、最后错误码和 detail_ref，不扩写 metrics/health 公共接口。
2. 完成 019 的 CMake/test 接线：
   - 更新 infra/CMakeLists.txt，把 PolicyAuditBridge 私有实现纳入 dasall_infra。
   - 更新 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt，注册 `PolicyAuditBridgeTest` 与 `PolicyAuditBridgeBoundaryContractTest` 并纳入聚合目标。
3. 完成 unit/contract 门禁落盘：
   - 新增 tests/unit/infra/PolicyAuditBridgeTest.cpp，覆盖高风险 deny 事件和 patch failure 事件的稳定事实组装。
   - 新增 tests/contract/smoke/PolicyAuditBridgeBoundaryContractTest.cpp，验证 policy bridge 保持在冻结 AuditEvent/AuditContext 边界内，且不泄露 `matched_rule_ids`、`effective_rules` 等 policy 内部结构。
4. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 POL-TODO-019 标记为 Done，并补齐本轮执行记录、build-ci 定向测试结果与标签级验收结果。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_policy_audit_bridge_unit_test dasall_contract_policy_audit_bridge_boundary_test`
   - `ctest --test-dir build-ci -N -R "PolicyAuditBridge(Test|BoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "PolicyAuditBridge(Test|BoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - build-ci 配置成功，`dasall_infra`、`dasall_policy_audit_bridge_unit_test`、`dasall_contract_policy_audit_bridge_boundary_test` 构建通过。
   - `ctest -N -R "PolicyAuditBridge(Test|BoundaryContractTest)"` 发现 2 个目标测试。
   - 定向执行通过，2/2 tests passed。
   - `ctest -L unit` 通过，126/126 tests passed；`ctest -L contract` 通过，138/138 tests passed。

### 结果

1. POL-TODO-019 已从“audit 侧已解阻但 policy bridge 未落盘”推进到“存在可编译、可测试、保持 frozen audit boundary 的 PolicyAuditBridge 最小实现”。
2. policy 的观测桥接阶段现已完成 audit 分支；后续只剩受 POL-BLK-004 约束的 metrics/health 两条桥接任务。

### 下一步

1. 核验 POL-BLK-004 是否仍为真实阻塞；若 metrics/health 专项已完成最小桥接接口冻结，则先做 blocker recovery，再推进 POL-TODO-020 与 POL-TODO-021。

### 风险

1. 当前 PolicyAuditBridge 仅覆盖 policy 侧最小审计桥接，不承担 metrics/health 状态输出；在 POL-BLK-004 未解阻前，不应把 bridge 状态直接暴露为外部健康或指标协议。

## 记录 #114

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-BLK-003 解阻校准
- 状态：已完成

### 改动

1. 完成 POL-BLK-003 证据核验：
   - 复核 audit 组件专项 TODO 中 `AUD-TODO-006`、`AUD-TODO-014`、`AUD-TODO-015` 与 `AUD-BLK-003/AUD-BLK-004` 的完成状态，确认 policy 所需的最小审计写入接口、核心字段与 health/metrics 协同语义已冻结。
   - 复核 infra/include/audit/IAuditLogger.h 与 infra/include/audit/AuditTypes.h，确认 `IAuditLogger`、`AuditEvent`、`AuditContext`、`AuditWriteOutcome` 已在当前代码树中稳定存在。
2. 完成 policy 台账校准：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 `POL-BLK-003` 标记为已解阻，并将 `POL-TODO-019` 从 Blocked 校准为 Not Started。
   - 同步刷新“当前 Blocked 任务索引”和阶段 F 的顺序说明，使后续 019 可以作为可执行原子任务继续推进。

### 测试

1. 验证命令：
   - `ctest --test-dir build-ci -N -L audit`
   - `ctest --test-dir build-ci --output-on-failure -L audit`
2. 结果：
   - audit gate 当前发现 9 个测试。
   - `ctest -L audit` 通过，9/9 tests passed。

### 结果

1. POL-BLK-003 已从“policy 文档中的历史阻塞项”校准为“已由 audit 组件专项和当前 gate 实质解阻”的状态。
2. `POL-TODO-019` 现在可以作为下一轮可执行原子任务继续推进。

### 下一步

1. 实现 `POL-TODO-019`，落盘 PolicyAuditBridge 审计桥接骨架，并补 unit/contract 验证。

### 风险

1. 若 audit 侧最小写入接口、核心字段或 health/metrics 协同语义未来回退，`POL-BLK-003` 需要重新转回 Blocked；当前解阻结论依赖 audit 专项 TODO 与 `ctest -L audit` 的持续有效性。

## 记录 #113

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-018 注册 policy integration 测试入口
- 状态：已完成

### 改动

1. 完成 POL-TODO-018-D/B 落盘：
   - 新增 tests/integration/infra/policy/CMakeLists.txt，注册 `dasall_policy_lifecycle_integration_test`，并统一打上 `integration;policy` 标签。
   - 新增 tests/integration/infra/policy/PolicyLifecycleIntegrationTest.cpp，覆盖 load -> snapshot -> evaluate -> patch -> rollback 闭环，以及 snapshot store commit fail 和 safe_mode failure injection。
   - 更新 tests/integration/infra/CMakeLists.txt 与 tests/integration/CMakeLists.txt，把 policy 子目录与新增 executable target 纳入顶层 integration 聚合图。
2. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 POL-TODO-018 从 Not Started 标记为 Done，并补齐本轮执行记录、工具态说明与 integration 发现性证据。
3. 保持范围约束：
   - 本轮只推进 integration 接线与测试落盘，没有提前混入 PolicyAuditBridge / PolicyMetricsBridge / PolicyHealthProbe 的实现。

### 测试

1. 验证命令：
   - `ListTests_CMakeTools`
   - `RunCtest_CMakeTools`
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_policy_lifecycle_integration_test`
   - `ctest --test-dir build-ci -N -R "PolicyLifecycleIntegrationTest|infra_integration_topology_smoke"`
   - `ctest --test-dir build-ci --output-on-failure -R PolicyLifecycleIntegrationTest`
   - `ctest --test-dir build-ci --output-on-failure -L integration`
2. 结果：
   - ListTests_CMakeTools 仍返回空 tests；RunCtest_CMakeTools 仍报“生成失败: 无法配置项目”。
   - build-ci 配置成功，`dasall_policy_lifecycle_integration_test` 构建通过。
   - `ctest -N -R "PolicyLifecycleIntegrationTest|infra_integration_topology_smoke"` 发现 2 个测试，其中 policy 新增用例为 `PolicyLifecycleIntegrationTest`。
   - `ctest -R PolicyLifecycleIntegrationTest` 通过，1/1 tests passed。
   - `ctest -L integration` 通过，14/14 tests passed。

### 结果

1. POL-TODO-018 已完成从“顶层 integration 拓扑已具备但 policy 子目录未落盘”到“policy integration 入口已注册、可被 CTest 发现并通过执行”的闭环。
2. 当前 integration 用例已覆盖 lifecycle 主闭环、commit fail 与 safe_mode；`source unavailable` 由于现有 loader-manager 边界会对缺失输入回退到 frozen defaults，暂不作为稳定 integration 注入点。

### 下一步

1. 进入 blocker 校准，核实 POL-BLK-003 与 POL-BLK-004 是否已被 audit/metrics/health 侧接口冻结任务实质解阻，再决定是否推进 019~021 的桥接实现。

### 风险

1. 工作区的 CMake Tools / RunCtest 仍处于“无法配置项目 / tests 为空”的工具态；后续 019~021 仍应默认保留 build-ci 回退链路证据。
2. `source unavailable` 失败注入目前不适合在现有 loader-manager 边界下伪造；若后续需要补齐该路径，应优先通过 loader/manager 组合接口而不是在 integration 测试中硬编码异常分支。

## 记录 #112

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-017 注册 policy 的 unit 与 contract 测试入口
- 状态：已完成

### 改动

1. 完成 POL-TODO-017-D/B 校准：
   - 核验 tests/unit/infra/CMakeLists.txt 已注册 16 个 policy 核心 unit 入口，覆盖对象、接口、loader、resolver、projector、snapshot store、manager 与错误语义基础路径。
   - 核验 tests/unit/CMakeLists.txt 已把上述 policy unit targets 纳入 DASALL_UNIT_TEST_EXECUTABLE_TARGETS 聚合。
   - 核验 tests/contract/CMakeLists.txt 已注册 10 个 policy 核心 contract 入口，覆盖 decision 语义、错误码映射、schema/interface 边界、loader/projector/manager 契约与 mapping catalog 门禁。
2. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 POL-TODO-017 从 Not Started 校准为 Done，并补齐本轮执行记录、工具态说明、ctest 发现性与标签级验收结果。
3. 完成本轮工作日志补记：
   - 在当前文件顶部追加 017 的执行记录，保持后续 018 integration 接线与 022 质量门回写可以直接复用本轮验收证据。

### 测试

1. 验证命令：
   - `RunCtest_CMakeTools`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci -N -R "^(Policy|SecurityPolicyManager)"`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - RunCtest_CMakeTools 仍返回“生成失败: 无法配置项目”，工作区 IDE 测试工具态未恢复。
   - build-ci 下 `dasall_unit_tests` 与 `dasall_contract_tests` 构建成功。
   - `ctest -N -R "^(Policy|SecurityPolicyManager)"` 发现 26 个 policy 核心测试，其中 unit 16 个、contract 10 个。
   - `ctest -L unit` 通过，125/125 tests passed；`ctest -L contract` 通过，137/137 tests passed。

### 结果

1. POL-TODO-017 已完成从“测试入口已落盘但 TODO 仍未回写”到“ctest 发现性、unit/contract 门禁与台账状态全部闭环”的校准。
2. policy 构建与测试接线任务 016/017 现已全部完成，并为后续 POL-TODO-018 integration 接线与 POL-TODO-022 质量门回写提供直接证据。

### 下一步

1. 若继续推进 policy TODO，优先进入 POL-TODO-018，围绕 load -> snapshot -> evaluate -> patch -> rollback 闭环补 integration 入口与发现性证据。

### 风险

1. 工作区的 CMake Tools / RunCtest 仍处于“无法配置项目 / targets/tests 为空”的工具态；后续 018/022 仍应默认保留 build-ci 回退链路证据。

## 记录 #111

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-016 注册 policy 源码到 infra CMake
- 状态：已完成

### 改动

1. 完成 POL-TODO-016-D/B 校准：
   - 核验 infra/CMakeLists.txt 已通过 DASALL_INFRA_POLICY_SOURCES 与 DASALL_INFRA_POLICY_PRIVATE_HEADERS 收录 PolicyLoader、PolicySchemaValidator、PolicyConflictResolver、PolicySnapshotStore、PolicyDecisionProjector、SecurityPolicyManager 六个 policy 私有实现及对应私有头。
   - 确认 dasall_infra 的 target_sources 已消费上述集合，说明 policy 实现不再停留在 placeholder 状态，而是已统一进入 infra 构建图。
2. 完成专项 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 POL-TODO-016 从 Not Started 校准为 Done，并补齐本轮执行记录、工具态说明与构建验收结果。
3. 完成本轮工作日志补记：
   - 在当前文件顶部追加 016 的执行记录，保持后续 017 与 018 可按最新构建图状态继续推进。

### 测试

1. 验证命令：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra`
2. 结果：
   - ListBuildTargets/ListTests 仍返回空 targets/tests，工作区 IDE 工具态未恢复。
   - build-ci 配置成功，`dasall_infra` 增量构建成功并链接 `libdasall_infra.a`，证明 policy 私有实现已进入 infra 构建图。

### 结果

1. POL-TODO-016 已完成从“旧台账仍显示 Not Started”到“构建图已核验、证据已回写、状态已校准”的闭环。
2. POL-TODO-017 现在可以直接在同一 build-ci 图上验证 unit/contract 入口发现性，而无需再补 build wiring 前置改动。

### 下一步

1. 推进 POL-TODO-017，核验 policy unit 与 contract 测试入口的 ctest 发现性、标签级执行结果与专项 TODO 状态。

### 风险

1. 工作区的 CMake Tools / RunCtest 仍处于“无法配置项目 / targets/tests 为空”的工具态；后续 017/018 仍应默认保留 build-ci 回退链路证据。

## 记录 #110

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-015 SecurityPolicyManager 主链骨架
- 状态：已完成

### 改动

1. 完成 POL-TODO-015-D/B 收敛：
   - 新增 infra/src/policy/SecurityPolicyManager.h 与 infra/src/policy/SecurityPolicyManager.cpp，落盘 bundle validate/resolve/commit、patch dry-run gate、apply fail-closed、rollback clone-commit、query projector 转发，以及连续 patch 失败进入 safe_mode 的最小状态机。
   - 复用并串接 PolicySchemaValidator、PolicyConflictResolver、PolicySnapshotStore、PolicyDecisionProjector 四个已完成组件，保持 manager 只做 orchestration，不吸收 audit/metrics/health 职责。
   - 从 admin patch gate rule.conditions 解析 `dry_run_required` 与 `safe_mode_threshold`，避免为 manager 主链新增额外公共配置入口。
2. 完成最小接线：
   - 更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt，把 manager 私有实现与新增 unit/contract tests 纳入构建图与 CTest 图。
3. 完成测试与契约落盘：
   - 新增 tests/unit/infra/SecurityPolicyManagerTest.cpp，覆盖正常 load+evaluate、patch reject 不切 current、dry_run+apply 后 rollback 成功、连续失败进入 safe_mode。
   - 新增 tests/contract/smoke/SecurityPolicyManagerFailureContractTest.cpp，验证 dry-run reject 与 safe_mode reject 继续停留在 policy failure domain。
4. 完成 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 POL-TODO-015 标记为 Done，并补齐本轮执行记录、回退链路与验收结果。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci -N -R "SecurityPolicyManager(Test|FailureContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "SecurityPolicyManager(Test|FailureContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - CMake Tools / RunCtest 仍无法配置项目，且 ListBuildTargets/ListTests 为空；已按仓库回退链路切换到 build-ci。
   - 新增 `SecurityPolicyManagerTest` 与 `SecurityPolicyManagerFailureContractTest` 可被发现并定向执行，2/2 通过；unit 125/125、contract 137/137 全部通过。

### 结果

1. POL-TODO-015 已把 policy 阶段 D 的第四步从“只有分散子组件骨架”推进到“存在可运行的 manager 主链、patch fail-closed gate、rollback 闭环和 safe_mode 失败阈值控制”的状态。
2. 用户指定的规则治理主链原子任务 POL-TODO-011、POL-TODO-012、POL-TODO-014、POL-TODO-015 已全部完成并各自独立提交推送。

### 下一步

1. 若继续推进 policy TODO，可先校准 POL-TODO-016、POL-TODO-017 的状态与交付范围，再决定是否转入 POL-TODO-018 integration 接线或 019~020 的桥接类任务。

### 风险

1. 当前 manager 仍只覆盖最小 orchestration 语义，safe_mode 也只冻结到“连续 patch 失败后拒绝后续 apply_patch”；若后续要引入自恢复、审计告警或更细粒度失败分类，应单独扩展状态机而不是在当前轮次内隐式加复杂度。
2. 工作区的 CMake Tools / RunCtest 仍处于“无法配置项目 / targets/tests 为空”的工具态；后续 policy 实现任务仍应默认保留 build-ci 回退链路证据。

## 记录 #109

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-014 PolicyDecisionProjector 查询投影骨架
- 状态：已完成

### 改动

1. 完成 POL-TODO-014-D/B 收敛：
   - 新增 infra/src/policy/PolicyDecisionProjector.h 与 infra/src/policy/PolicyDecisionProjector.cpp，落盘 query module -> domain 映射、target_selector specificity 优先、default_effect fallback、observe -> allow 告警映射和 evidence_ref 锚点生成。
   - 新增 tests/unit/infra/PolicyDecisionProjectorTest.cpp，覆盖 direct allow hit、default deny miss、require_confirmation 与 specificity-first deny 四类投影。
   - 新增 tests/contract/smoke/PolicyDecisionProjectorBoundaryContractTest.cpp，验证 projector 输出仍受 decision semantic catalog 与 evidence_ref private-field catalog 约束。
2. 完成最小接线：
   - 更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt，把 projector 私有实现与新增 unit/contract tests 纳入构建图与 CTest 图。
3. 完成 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 POL-TODO-014 标记为 Done，并补齐本轮执行记录、回退链路与验收结果。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci -N -R "PolicyDecisionProjector(Test|BoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "PolicyDecisionProjector(Test|BoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - CMake Tools / RunCtest 仍无法配置项目，且 ListBuildTargets/ListTests 为空；已按仓库回退链路切换到 build-ci。
   - 新增 `PolicyDecisionProjectorTest` 与 `PolicyDecisionProjectorBoundaryContractTest` 可被发现并定向执行，2/2 通过；unit 124/124、contract 136/136 全部通过。

### 结果

1. POL-TODO-014 已把 policy 阶段 D 的第三步从“只有 PolicyDecisionRef 与 mapping catalog 边界”推进到“存在可执行的最小查询投影骨架、default_effect fail-closed 回退和 decision/evidence_ref contract 证据”的状态。
2. 当前 policy 组件专项 TODO 已具备继续进入 POL-TODO-015 的前提，可按串行顺序转入 SecurityPolicyManager 主链骨架。

### 下一步

1. 执行 POL-TODO-015，实现 SecurityPolicyManager 最小主链骨架，并串接 loader、validator、resolver、snapshot store、projector 五段闭环。

### 风险

1. 当前 projector 仍保持私有实现面，且 default miss 依赖 loader 生成的 `evaluate_default` 规则作为最小兜底；若后续需要共享 DecisionTrace 或 richer evidence 对象，应单独冻结边界而不是在本轮实现里隐式扩张。
2. 工作区的 CMake Tools / RunCtest 仍处于“无法配置项目 / targets/tests 为空”的工具态；后续 policy 实现任务仍应默认保留 build-ci 回退链路证据。

## 记录 #108

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-012 PolicyConflictResolver 冲突裁定骨架
- 状态：已完成

### 改动

1. 完成 POL-TODO-012-D/B 收敛：
   - 新增 infra/src/policy/PolicyConflictResolver.h 与 infra/src/policy/PolicyConflictResolver.cpp，落盘私有 `ConflictResolutionResult`、scope grouping、deny-first / explicit-priority 裁定、compat downgrade warning 和 unresolved tie reject。
   - 新增 tests/unit/infra/PolicyConflictResolverTest.cpp，覆盖 deny-first、explicit-priority、enforced unresolved reject 和 compatibility-only downgrade warning。
2. 完成最小接线：
   - 更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt，把 conflict resolver 私有实现与新增 unit test 纳入构建图与 CTest 图。
3. 完成 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 POL-TODO-012 标记为 Done，并补齐本轮执行记录、回退链路与验收结果。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_unit_tests`
   - `ctest --test-dir build-ci -N -R PolicyConflictResolverTest`
   - `ctest --test-dir build-ci --output-on-failure -R PolicyConflictResolverTest`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
2. 结果：
   - CMake Tools / RunCtest 仍无法配置项目，已按仓库回退链路切换到 build-ci。
   - 首次 unit 聚合暴露了同-rank 冲突夹具设置不准确的问题；修正测试后，`PolicyConflictResolverTest` 可被发现并定向执行，1/1 通过；unit 123/123 全部通过。

### 结果

1. POL-TODO-012 已把 policy 阶段 D 的第二步从“priority_order 只存在设计和 loader 条件里”推进到“存在最小冲突裁定骨架、两档顺序语义和 unresolved/compat 边界证据”的状态。
2. 当前 policy 组件专项 TODO 已具备继续进入 POL-TODO-014 的前提，可按串行顺序转入查询投影骨架。

### 下一步

1. 执行 POL-TODO-014，实现 PolicyDecisionProjector 最小查询投影骨架，并固定 decision/evidence_ref 输出边界。

### 风险

1. 当前 resolver 仍保持私有返回面，且 same-rank tie 的处理只冻结到“enforced reject / compat downgrade”；若后续需要跨模块共享 EffectivePolicySet，需要单独做对象冻结而不是在当前轮次内隐式扩张。
2. 工作区的 CMake Tools / RunCtest 仍处于“无法配置项目 / targets/tests 为空”的工具态；后续 policy 实现任务仍应默认保留 build-ci 回退链路证据。

## 记录 #107

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-011 PolicySchemaValidator 最小校验骨架
- 状态：已完成

### 改动

1. 完成 POL-TODO-011-D/B 收敛：
   - 新增 infra/src/policy/PolicySchemaValidator.h 与 infra/src/policy/PolicySchemaValidator.cpp，落盘 bundle/rule/patch operation 三层最小校验骨架，覆盖缺字段、未知 domain、非法 effect、unsupported schema_version 与 base_generation mismatch。
   - 新增 tests/unit/infra/PolicySchemaValidatorTest.cpp 与 tests/contract/smoke/PolicySchemaValidatorBoundaryContractTest.cpp，分别覆盖实现正负例和实现边界仍只输出本地 ValidationReport 字符串字段。
2. 完成最小接线：
   - 更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt，把 validator 私有实现与新增 unit/contract tests 纳入构建图与 CTest 图。
3. 完成 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 POL-TODO-011 标记为 Done，并补齐本轮执行记录、回退链路与验收结果。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci -N -R "PolicySchemaValidator(Test|BoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "PolicySchemaValidator(Test|BoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - CMake Tools / RunCtest 仍无法配置项目，已按仓库回退链路切换到 build-ci。
   - 新增 `PolicySchemaValidatorTest` 与 `PolicySchemaValidatorBoundaryContractTest` 可被发现并定向执行，2/2 通过；unit 122/122、contract 135/135 全部通过。

### 结果

1. POL-TODO-011 已把 policy 阶段 D 的第一步从“接口已冻结但无真实校验实现”推进到“存在可定位 field_paths 的最小 validator 骨架和 unit/contract 证据”的状态。
2. 当前 policy 组件专项 TODO 已具备继续进入 POL-TODO-012 的前提，可按串行顺序转入 conflict resolver。

### 下一步

1. 执行 POL-TODO-012，实现 PolicyConflictResolver 最小冲突裁定骨架，固定 deny-first 与 explicit-priority 两档行为。

### 风险

1. 当前 validator 只覆盖最小字段/shape 校验，尚未把更细粒度的条件白名单和 source checksum 语义升级为独立规则矩阵；后续 resolver/manager 不应把它误当成完整策略 DSL 校验器。
2. 工作区的 CMake Tools / RunCtest 仍处于“无法配置项目 / targets/tests 为空”的工具态；后续 policy 实现任务仍应默认保留 build-ci 回退链路证据。

## 记录 #106

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-013 PolicySnapshotStore generation/LKG 骨架
- 状态：已完成

### 改动

1. 完成 POL-TODO-013-D/B 收敛：
   - 新增 infra/src/policy/PolicySnapshotStore.h 与 infra/src/policy/PolicySnapshotStore.cpp，落盘内存版 current/history/LKG store、generation 单调校验、缺省 last_known_good_ref 回填和 injected commit failure seam。
   - 新增 tests/unit/infra/PolicySnapshotStoreTest.cpp，覆盖成功提交、history trim、generation 自增、LKG linkage、invalid/non-monotonic commit 与 forced commit failure 保持旧状态。
2. 完成最小接线：
   - 更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt，把 snapshot store 私有实现与新增 unit test 纳入构建图与 CTest 图。
3. 完成 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 POL-TODO-013 标记为 Done，并补齐本轮执行记录、回退链路与验收结果。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_unit_tests`
   - `ctest --test-dir build-ci -N -R "PolicySnapshotStore(InterfaceTest|Test)"`
   - `ctest --test-dir build-ci --output-on-failure -R "PolicySnapshotStore(InterfaceTest|Test)"`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
2. 结果：
   - CMake Tools / RunCtest 仍无法配置项目，已按仓库回退链路切换到 build-ci。
   - 新增 `PolicySnapshotStoreTest` 与既有 `PolicySnapshotStoreInterfaceTest` 可被发现并定向执行，2/2 通过；unit 121/121 全部通过。

### 结果

1. POL-TODO-013 已把 policy 阶段 C 的第二步从“接口已冻结但无真实快照存储实现”推进到“存在最小内存版 snapshot store、generation/history/LKG 骨架与 commit failure test seam”的状态。
2. 当前 policy 组件专项 TODO 的阶段 C 已闭环，后续可转入 POL-TODO-011 / POL-TODO-012 的 validator 与 conflict resolver 主链。

### 下一步

1. 若继续推进 policy 主链，优先执行 POL-TODO-011，实现 PolicySchemaValidator 最小校验骨架并承接阶段 D 起点。

### 风险

1. 当前 PolicySnapshotStore 仍是内存版骨架，尚未接入真实持久化介质；后续 manager/rollback 任务不得把它误判为 durable store。
2. 工作区的 CMake Tools / RunCtest 仍处于“无法配置项目 / targets/tests 为空”的工具态；后续 policy 实现任务仍应默认保留 build-ci 回退链路证据。

## 记录 #105

- 日期：2026-04-05
- 阶段：policy 组件专项 TODO
- 任务：POL-TODO-010 PolicyLoader 配置读取骨架
- 状态：已完成

### 改动

1. 完成 POL-TODO-010-D/B 收敛：
   - 新增 infra/src/policy/PolicyLoader.h 与 infra/src/policy/PolicyLoader.cpp，落盘基于 IConfigCenter 的最小配置读取骨架，兼容 infra.security_policy.* 与历史 alias infra.security.policy.*，并把缺失/非法值回退到 frozen defaults。
   - 新增 tests/unit/infra/PolicyLoaderConfigReadTest.cpp 与 tests/contract/smoke/PolicyLoaderBoundaryContractTest.cpp，分别覆盖 strict/compat、alias key、hot_reload 关闭、default fallback，以及 Profile 裁剪不越出 PolicyAdmin 域的 fail-closed 边界。
2. 完成最小接线：
   - 更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt，把 loader 私有实现与新增 unit/contract tests 纳入构建图与 CTest 图。
3. 完成 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md，将 POL-TODO-010 标记为 Done，并补齐本轮执行记录、回退链路与验收结果。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci -N -R "PolicyLoader(ConfigReadTest|BoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "PolicyLoader(ConfigReadTest|BoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - CMake Tools 仍无法配置项目，已按仓库回退链路切换到 build-ci；修正一次私有头 include 路径后，构建全通过。
   - 新增 `PolicyLoaderConfigReadTest` 与 `PolicyLoaderBoundaryContractTest` 可被发现并定向执行，2/2 通过；unit 120/120、contract 134/134 全部通过。

### 结果

1. POL-TODO-010 已把 policy 阶段 C 的第一步从“接口已冻结但无真实读取路径”推进到“存在最小 loader 骨架、source/checksum trace、fail-closed skeleton rule 与可验证 tests”的状态。
2. 当前 policy 组件专项 TODO 已具备继续进入 POL-TODO-013 的前提，可按 7.1 的顺序转入快照存储 generation/LKG 骨架。

### 下一步

1. 执行 POL-TODO-013，落盘内存版 PolicySnapshotStore，补齐 generation/history/LKG 与 commit failure 不切 current 的最小闭环。

### 风险

1. infra.security_policy.* 与 infra.security.policy.* 的键域仍存在历史口径漂移；若 config/profiles 后续只保留一侧命名，需要同步回收 alias 与相关测试。
2. 工作区的 CMake Tools 仍处于“无法配置项目 / targets/tests 为空”的工具态；后续 policy 实现任务仍应默认保留 build-ci 回退链路证据。

## 记录 #104

- 日期：2026-04-04
- 阶段：secret 组件专项 TODO
- 任务：SEC-TODO-017 Secret 质量门与交付证据
- 状态：已完成

### 改动

1. 完成 SEC-TODO-017-D/B 收敛：
   - 新增 docs/todos/infrastructure/deliverables/SEC-TODO-017-Secret质量门与证据收口.md，补齐本地证据、gate 收口策略和验收结果。
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 `ctest -L secret` 固化为当前 secret 专项 gate，并新增 gate 结论表、blocker/rollback 摘要和新的下一步建议。
2. 完成执行记录回链：
   - 更新 docs/worklog/DASALL_开发执行记录.md，新增本轮质量门与交付证据收口记录。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests dasall_integration_tests`
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
   - `ctest --test-dir build-ci --output-on-failure -L integration`
   - `ctest --test-dir build-ci -N -L secret`
   - `ctest --test-dir build-ci --output-on-failure -L secret`
2. 结果：
   - 全部通过；unit 119/119、contract 133/133、integration 13/13，`ctest -N -L secret` 发现 20 个测试，`ctest -L secret` 20/20 通过。

### 结果

1. SEC-TODO-017 已把 secret 的当前轮收口为“统一 secret gate 基线 + 8 个 gate 结论 + blocker/rollback 摘要”的可追溯状态。
2. 当前 secret 组件专项 TODO 中 001~017 已全部完成；残余 blocker 仅剩 `SEC-BLK-003` 的 KMS 真实接入前置条件。

### 下一步

1. 若继续推进 secret 子域，应先处理 `SEC-BLK-003`，冻结 KMS 身份、限流、超时和测试夹具策略，再另起 v2 原子任务。

### 风险

1. 若后续新增 secret tests 未继续挂入 `secret` 标签，或在未解阻 `SEC-BLK-003` 前直接接入真实 KMS SDK，本轮 gate 结论需要重新评审。

## 记录 #103

- 日期：2026-04-04
- 阶段：secret 组件专项 TODO
- 任务：SEC-TODO-016 Secret integration 与故障注入入口
- 状态：已完成

### 改动

1. 完成 SEC-TODO-016-D/B 收敛：
   - 新增 docs/todos/infrastructure/deliverables/SEC-TODO-016-Secret集成与故障注入入口收敛.md，补齐本地证据、integration 收口策略和验收结果。
   - 新增 tests/integration/infra/secret/CMakeLists.txt，提供 `dasall_register_secret_integration_test(...)`，统一 secret integration target 注册与 `integration;secret` 标签。
   - 新增 tests/integration/infra/secret/SecretRotationWorkflowTest.cpp 与 tests/integration/infra/secret/SecretFailureInjectionTest.cpp，分别落盘 rotation workflow 与 failure injection 两条最小集成链路。
2. 完成 integration 接线收口：
   - 更新 tests/integration/infra/CMakeLists.txt，接入 secret 子目录。
   - 更新 tests/integration/CMakeLists.txt，把两个 secret integration targets 纳入 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS`。
3. 完成 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-TODO-016 标记为 Completed，并把下一入口切换到 SEC-TODO-017。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_integration_tests`
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -L integration`
2. 结果：
   - 全部通过；`ctest -N` 已发现 `SecretRotationWorkflowTest` 与 `SecretFailureInjectionTest`，integration 13/13 通过，`secret` 标签下 2 个测试。

### 结果

1. SEC-TODO-016 已把 secret 的 integration/failure injection 入口从“顶层拓扑存在但组件缺位”推进到“存在 secret 子目录、统一注册 helper、可聚合执行的最小 integration matrix”。
2. secret 子域当前下一执行入口已切换到 SEC-TODO-017，随后可统一回写质量门、阻塞变化和交付证据。

### 下一步

1. 执行 SEC-TODO-017，基于 unit/contract/integration 结果回写 secret 质量门和交付证据。

### 风险

1. 若后续新增 secret integration tests 未纳入 `tests/integration/infra/secret/CMakeLists.txt` 或遗漏 `integration;secret` 标签，本轮 integration discoverability 结论需要重新评审。

## 记录 #102

- 日期：2026-04-04
- 阶段：secret 组件专项 TODO
- 任务：SEC-TODO-015 Secret 测试入口注册
- 状态：已完成

### 改动

1. 完成 SEC-TODO-015-D/B 收敛：
   - 新增 docs/todos/infrastructure/deliverables/SEC-TODO-015-Secret测试入口注册收敛.md，补齐本地证据、测试入口收口策略和验收结果。
   - 更新 tests/unit/CMakeLists.txt，新增 `DASALL_SECRET_UNIT_TEST_EXECUTABLE_TARGETS` 并接入顶层 unit 聚合列表。
   - 更新 tests/unit/infra/CMakeLists.txt，为 secret interface/type unit tests 补齐 `unit;secret` 标签。
   - 更新 tests/contract/CMakeLists.txt，新增 `dasall_register_secret_contract_test(...)`，并把 secret contract tests 统一切到 `contract;smoke;secret`。
2. 完成 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-TODO-015 标记为 Completed，并把下一入口切换到 SEC-TODO-016。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - 全部通过；unit 119/119、contract 133/133，secret 相关 tests 现已可通过统一 `secret` 标签和聚合 target 过滤。

### 结果

1. SEC-TODO-015 已把 secret 的 unit/contract 测试入口从“可运行但分散”推进到“按域聚合、统一标签、可直接 gate”的收口状态。
2. secret 子域当前下一执行入口已切换到 SEC-TODO-016，随后可进入 integration 与 failure injection 用例落盘。

### 下一步

1. 执行 SEC-TODO-016，补齐 secret integration 与 failure injection 注册入口并验证用例 discoverability。

### 风险

1. 若后续新增 secret tests 未纳入 `DASALL_SECRET_UNIT_TEST_EXECUTABLE_TARGETS` 或未带 `secret` 标签，本轮测试入口收口结论需要重新评审。

## 记录 #101

- 日期：2026-04-04
- 阶段：secret 组件专项 TODO
- 任务：SEC-TODO-014 infra/secret CMake 收口
- 状态：已完成

### 改动

1. 完成 SEC-TODO-014-D/B 收敛：
   - 新增 docs/todos/infrastructure/deliverables/SEC-TODO-014-CMake收口基线确认.md，补齐本地证据、CMake 收口策略和验收结果。
   - 更新 infra/CMakeLists.txt，新增 `DASALL_INFRA_SECRET_PUBLIC_HEADERS` 与 `DASALL_INFRA_SECRET_PRIVATE_HEADERS`，并把 private headers 纳入 `target_sources(dasall_infra ...)`。
2. 完成 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-TODO-014 标记为 Completed，并把下一入口切换到 SEC-TODO-015。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra`
2. 结果：
   - configure/build 通过；secret public/private header 与 source 的集中入图未影响 `dasall_infra` 构建。

### 结果

1. SEC-TODO-014 已把 infra/secret 从“逐任务增量接入”推进到“集中声明、整树入图”的 CMake 基线。
2. secret 子域当前下一执行入口已切换到 SEC-TODO-015，随后可进入 unit/contract 测试入口的集中注册与矩阵收口。

### 下一步

1. 执行 SEC-TODO-015，收口 secret unit 与 contract 测试入口、矩阵标签和聚合构建基线。

### 风险

1. 若后续 `infra/CMakeLists.txt` 再次把 secret 头/源拆回零散声明，或遗漏新增长的 secret 子树文件，本轮 CMake 收口结论需要重新评审。

## 记录 #100

- 日期：2026-04-04
- 阶段：secret 组件专项 TODO
- 任务：SEC-TODO-013 SecretHealthProbe 健康出口骨架
- 状态：已完成

### 改动

1. 完成 SEC-TODO-013-D/B 收敛：
   - 新增 docs/todos/infrastructure/deliverables/SEC-TODO-013-SecretHealthProbe健康出口收敛.md，补齐本地证据、外部参考、Design -> Build 映射与验收结果。
   - 新增 infra/src/secret/SecretHealthProbe.h 与 infra/src/secret/SecretHealthProbe.cpp，落盘 secret 私有 signal provider 聚合和 `sample_secret_health()` 实现，收敛 backend down、rotation backlog 与 cache stale 到健康快照。
2. 完成测试与接线收口：
   - 新增 tests/unit/infra/secret/SecretHealthProbeTest.cpp，覆盖 healthy、backend down、rotation backlog 与 cache stale 四路径。
   - 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 probe 源码和 unit test target 纳入构建图。
3. 完成 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-TODO-013 标记为 Completed，并把下一入口切换到 SEC-TODO-014。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_secret_health_probe_unit_test`
   - `ctest --test-dir build-ci -N -R SecretHealthProbeTest`
   - `ctest --test-dir build-ci --output-on-failure -R SecretHealthProbeTest`
2. 结果：
   - configure/build 通过；`SecretHealthProbeTest` 可被发现，并定向执行 1/1 通过。

### 结果

1. SEC-TODO-013 已把 secret 健康链路从“接口已冻结但无实现”推进到“存在 provider 聚合 + 私有 snapshot + unit evidence 的可验证骨架”。
2. secret 子域当前下一执行入口已切换到 SEC-TODO-014，随后可进入 infra/secret 的集中 CMake 收口与构建基线确认。

### 下一步

1. 执行 SEC-TODO-014，收口 infra/secret 的 CMake 与文件入图基线，并验证 placeholder 不再是唯一入口。

### 风险

1. 若后续 SecretHealthProbe 直接吸收通用 `IHealthMonitor` 契约、遗漏 rotation backlog / cache stale 信号，或把 backend unavailable 重新解释为 healthy，本轮健康出口骨架与回归测试需要重新评审。

## 记录 #099

- 日期：2026-04-04
- 阶段：secret 组件专项 TODO
- 任务：SEC-TODO-012 SecretAuditBridge 审计桥骨架
- 状态：已完成

### 改动

1. 完成 SEC-TODO-012-D/B 收敛：
   - 新增 docs/todos/infrastructure/deliverables/SEC-TODO-012-SecretAuditBridge审计桥收敛.md，补齐 blocker 承接、本地证据、外部参考、Design -> Build 映射与验收结果。
   - 新增 infra/src/secret/SecretAuditBridge.h 与 infra/src/secret/SecretAuditBridge.cpp，落盘通用 `emit_event`、动作 wrapper、AuditEvent/AuditContext 映射、status 跟踪，以及 audit write failure 的 secret 错误码归一。
2. 完成测试与接线收口：
   - 新增 tests/unit/infra/secret/SecretAuditBridgeTest.cpp，覆盖 access/rotate/revoke 完整性、AccessDenied/ Fallback 特殊 outcome，以及 audit write hard failure。
   - 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 bridge 源码和 unit test target 纳入构建图。
3. 完成 TODO 回链：
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-TODO-012 标记为 Completed，并把下一入口切换到 SEC-TODO-013。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_secret_audit_bridge_unit_test`
   - `ctest --test-dir build-ci -N -R SecretAuditBridgeTest`
   - `ctest --test-dir build-ci --output-on-failure -R SecretAuditBridgeTest`
2. 结果：
   - configure/build 通过；`SecretAuditBridgeTest` 可被发现，并定向执行 1/1 通过。

### 结果

1. SEC-TODO-012 已把 secret 审计链路从“设计冻结但未编码”推进到“存在 IAuditLogger bridge + 字段映射 + failure 归一 + unit evidence 的可验证骨架”。
2. secret 子域当前下一执行入口已切换到 SEC-TODO-013，随后可进入健康探针的 degraded 聚合与快照出口实现。

### 下一步

1. 执行 SEC-TODO-013，落盘 SecretHealthProbe 健康出口骨架、单测与最小 CMake 接线。

### 风险

1. 若后续 SecretAuditBridge 偏离 6.10.1 的字段映射、把 `AccessDenied` / `Fallback` 的 AuditOutcome 语义改回布尔直传，或静默吞掉 write failure，本轮审计桥骨架与回归测试需要重新评审。

## 记录 #098

- 日期：2026-04-04
- 阶段：secret 组件专项 TODO
- 任务：SEC-BLK-004 SecretAuditBridge 接线解阻
- 状态：已完成

### 改动

1. 完成 SEC-BLK-004-D 设计解阻：
   - 更新 docs/architecture/DASALL_infra_secret模块详细设计.md，新增 6.10.1，冻结 `audit::IAuditLogger` 为 v1 唯一必选 sink，并明确 SecretAuditEvent -> AuditEvent/AuditContext 的 action、side_effects 与 request/task/worker context 字段映射。
   - 新增 docs/todos/infrastructure/deliverables/SEC-BLK-004-SecretAuditBridge接线解阻.md，把 blocker 根因收敛为“6.10 已有事件集合，但尚未冻结可直接编码的 sink contract 和字段映射”，并固定 required sink 的失败处理约束。
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-BLK-004 标记为已解阻，并把 SEC-TODO-012 的 blocker 列迁移为已解阻说明。
2. 完成执行入口切换：
   - 在 secret 专项 TODO 中新增本轮执行记录，并把下一步建议切换为直接推进 SEC-TODO-012。

### 测试

1. 验证命令：
   - `rg -n "SecretAuditBridge v1|IAuditLogger|AuditEvent|AuditContext|consumer_module|SEC-BLK-004|SEC-TODO-012" docs/architecture/DASALL_infra_secret模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md docs/todos/infrastructure/deliverables/SEC-BLK-004-SecretAuditBridge接线解阻.md`
2. 结果：
   - 已通过；命中 secret 设计 6.10.1 的 sink 合同与字段映射、secret TODO 的解阻状态与执行记录，以及 blocker deliverable 的交接约束，三处证据已一致回链。

### 结果

1. SEC-BLK-004 已不再阻塞 secret audit 链路；`audit::IAuditLogger` 接线、SecretAuditEvent -> AuditEvent/AuditContext 字段映射，以及 required sink 的失败语义已具备直接编码条件。
2. secret 子域当前下一执行入口已切换到 SEC-TODO-012，随后可进入审计桥骨架实现与 write failure 路径验证。

### 下一步

1. 执行 SEC-TODO-012，落盘 SecretAuditBridge 审计桥骨架、单测与最小 CMake 接线。

### 风险

1. 若后续 SecretAuditBridge 偏离 6.10.1 的 sink contract、静默吞掉 write failure，或把 required sink 降级为可选，本轮 blocker 解阻结论需要重新评审。

## 记录 #097

- 日期：2026-04-04
- 阶段：secret 组件专项 TODO
- 任务：SEC-TODO-010 SecretRotationCoordinator 轮换骨架
- 状态：已完成

### 改动

1. 完成 SEC-TODO-010-D/B 收敛：
   - 新增 docs/todos/infrastructure/deliverables/SEC-TODO-010-SecretRotationCoordinator轮换骨架收敛.md，补齐 blocker 承接、本地证据、外部参考、Design -> Build 映射与验收结果。
   - 新增 infra/src/secret/SecretRotationValidator.h、infra/src/secret/SecretRotationCoordinator.h 与 infra/src/secret/SecretRotationCoordinator.cpp，落盘 internal validator、candidate version 推导、promote/revoke/rollback skeleton 与 backlog status。
2. 完成 facade 轮换接线：
   - 更新 infra/src/secret/SecretManagerFacade.h 与 infra/src/secret/SecretManagerFacade.cpp，新增 rotation validator 注入与 rotation status 读取，并把 `rotate` 从 deferred failure 切到 coordinator 委托。
3. 完成测试与接线收口：
   - 新增 tests/unit/infra/secret/SecretRotationCoordinatorTest.cpp，覆盖 dual-slot backlog、validator reject、rollback success、rollback fail 四路径。
   - 更新 tests/unit/infra/secret/SecretManagerFacadeTest.cpp，补 manager rotate delegation 回归。
   - 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 coordinator 源码和 unit test target 纳入构建图。
4. 完成 TODO 回链：
   - 回写 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-TODO-010 标记为 Completed，并把下一入口切换到 SEC-BLK-004 -> SEC-TODO-012。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_secret_rotation_coordinator_unit_test`
   - `ctest --test-dir build-ci -N -R "SecretManagerFacadeTest|SecretRotationCoordinatorTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacadeTest|SecretRotationCoordinatorTest"`
2. 结果：
   - configure/build 通过；`SecretManagerFacadeTest` 与 `SecretRotationCoordinatorTest` 可被发现，并定向执行 2/2 通过。

### 结果

1. SEC-TODO-010 已把 secret 轮换能力从“占位 deferred failure”推进到“存在 internal validator + coordinator + manager delegation 的可验证骨架”。
2. secret 子域当前下一执行入口已切换到 SEC-BLK-004，之后再推进 SEC-TODO-012 的审计桥骨架。

### 下一步

1. 处理 SEC-BLK-004，冻结 `IAuditLogger` 接线与 SecretAuditEvent -> AuditEvent 的最小字段映射，再进入 SEC-TODO-012。

### 风险

1. 若后续 audit bridge 或 health probe 重写 coordinator 的 backlog / rollback failure 语义，或让 facade rotate 再次退化为 placeholder，本轮轮换骨架与回归测试需要重新评审。

## 记录 #096

- 日期：2026-04-04
- 阶段：secret 组件专项 TODO
- 任务：SEC-BLK-002 SecretRotationValidator 最小接口解阻
- 状态：已完成

### 改动

1. 完成 SEC-BLK-002-D 设计解阻：
   - 更新 docs/architecture/DASALL_infra_secret模块详细设计.md，新增 6.8.1，冻结 internal `RotationValidationContext` / `ISecretRotationValidator` 最小接口、candidate version 推导和 dual-slot / grace period / rollback 的最小时序规则。
   - 新增 docs/todos/infrastructure/deliverables/SEC-BLK-002-SecretRotationValidator最小接口解阻.md，把 blocker 根因收敛为“配置项已存在但尚未映射为 coordinator 可编码的最小执行语义”，并固定 validator / grace window / rollback 的交接约束。
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-BLK-002 标记为已解阻，并把 SEC-TODO-010 的 blocker 列迁移为“已由 secret 设计 6.8.1 / 6.9 解阻”。
2. 完成执行入口切换：
   - 在 secret 专项 TODO 中新增本轮执行记录，并把下一步建议切换为直接推进 SEC-TODO-010。

### 测试

1. 验证命令：
   - `rg -n "SecretRotationValidator|validation_required|grace_period_sec|SEC-BLK-002|SEC-TODO-010" docs/architecture/DASALL_infra_secret模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md docs/todos/infrastructure/deliverables/SEC-BLK-002-SecretRotationValidator最小接口解阻.md`
2. 结果：
   - 已通过；命中 secret 设计 6.8.1 / 6.9 的 validator 与宽限窗口语义、secret TODO 的解阻状态与执行记录，以及 blocker deliverable 的交接约束，architecture、TODO 与交付件三处证据已一致回链。

### 结果

1. SEC-BLK-002 已不再阻塞 secret rotation 链路；`validation_required` / `dual_slot_enabled` / `grace_period_sec` 的最小执行语义已具备直接编码条件。
2. secret 子域当前下一执行入口已切换到 SEC-TODO-010，后续应进入 SecretRotationCoordinator 轮换骨架实现与单测验收。

### 下一步

1. 进入 SEC-TODO-010，落盘 SecretRotationCoordinator 与 internal SecretRotationValidator 最小骨架，覆盖 validate_only、validation fail、promote/revoke 和 rollback fail 路径。

### 风险

1. 若后续 SecretRotationCoordinator 直接绕过 `validation_required`、把 dual-slot 请求静默降级为 inplace promote，或忽略 `grace_period_sec` / rollback 语义，本 blocker 需要重新转为 Blocked。

## 记录 #095

- 日期：2026-04-03
- 阶段：secret 组件专项 TODO
- 任务：SEC-TODO-009 SecretLeaseRegistry 生命周期管理
- 状态：已完成

### 改动

1. 完成 SEC-TODO-009-D/B 收敛：
   - 新增 docs/todos/infrastructure/deliverables/SEC-TODO-009-SecretLeaseRegistry生命周期收敛.md，补齐本地证据、外部参考、Design -> Build 映射与验收结果。
   - 新增 infra/src/secret/SecretLeaseRegistry.h 与 infra/src/secret/SecretLeaseRegistry.cpp，落盘 create/validate/expire/release 最小生命周期与按 secret 批量失效能力。
2. 完成 facade 生命周期接线：
   - 更新 infra/src/secret/SecretManagerFacade.h 与 infra/src/secret/SecretManagerFacade.cpp，移除临时 active lease map，把 materialize/release/revoke 改为委托 registry，并在 materialize 上补 stale handle 明确错误码。
3. 完成测试与接线收口：
   - 新增 tests/unit/infra/secret/SecretLeaseRegistryTest.cpp，覆盖 lease 创建、过期、释放和 rotation epoch 漂移导致的 stale 句柄。
   - 更新 tests/unit/infra/secret/SecretManagerFacadeTest.cpp，补充 backend 版本轮换后的 stale handle materialize 拒绝回归。
   - 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 registry 源码和 unit test target 纳入构建图。
4. 完成 TODO 回链：
   - 回写 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-TODO-009 标记为 Completed，并把下一入口切换到 SEC-BLK-002 -> SEC-TODO-010。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_secret_lease_registry_unit_test dasall_contract_secret_manager_facade_boundary_test`
   - `ctest --test-dir build-ci -N -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest"`
2. 结果：
   - configure/build 通过；`SecretManagerFacadeTest`、`SecretLeaseRegistryTest` 与 `SecretManagerFacadeBoundaryContractTest` 可被发现，并定向执行 3/3 通过。

### 结果

1. SEC-TODO-009 已把 secret 生命周期从“manager 内部临时 map”推进到“独立 registry + facade 委托”，为后续轮换链路提供稳定 lease 状态基线。
2. secret 子域当前下一执行入口已切换到 SEC-BLK-002，之后再推进 SEC-TODO-010 的轮换骨架。

### 下一步

1. 处理 SEC-BLK-002，冻结 dual-slot 验证器最小接口与 `rotation.validation` / `grace_period` 语义，再进入 SEC-TODO-010。

### 风险

1. 若后续 rotation coordinator 改写 `rotation_epoch` 语义或让 stale handle 重新退化为 backend 未命中，本轮 lifecycle contract 需要重新评审。

## 记录 #094

- 日期：2026-04-03
- 阶段：secret 组件专项 TODO
- 任务：SEC-TODO-008 SecretManagerFacade 访问骨架
- 状态：已完成

### 改动

1. 完成 SEC-TODO-008-D/B 收敛：
   - 新增 docs/todos/infrastructure/deliverables/SEC-TODO-008-SecretManagerFacade访问骨架收敛.md，补齐本地证据、外部参考、Design -> Build 映射与验收结果。
   - 新增 infra/src/secret/SecretManagerFacade.h 与 infra/src/secret/SecretManagerFacade.cpp，落盘 get/materialize/release/inspect 主链，以及 rotate deferred failure 和 revoke 最小 backend 委托。
2. 完成测试与接线收口：
   - 新增 tests/unit/infra/secret/SecretManagerFacadeTest.cpp，覆盖访问链正向和 expired handle 负向路径。
   - 新增 tests/contract/smoke/SecretManagerFacadeBoundaryContractTest.cpp，固化 handle/lease 不吸收 request/task/session 字段，以及 validation failure 只引用 contracts error payload 的边界。
   - 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt，将 manager facade 源码和 unit/contract test target 纳入构建图。
3. 完成 TODO 回链：
   - 回写 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-TODO-008 标记为 Completed，并把下一入口切换到 SEC-TODO-009。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_contract_secret_manager_facade_boundary_test`
   - `ctest --test-dir build-ci -N -R "SecretManagerFacade(Test|BoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacade(Test|BoundaryContractTest)"`
2. 结果：
   - configure/build 通过；`SecretManagerFacadeTest` 与 `SecretManagerFacadeBoundaryContractTest` 可被发现，并定向执行 2/2 通过。

### 结果

1. SEC-TODO-008 已把 secret manager 从“只有 public interface”推进到“存在可验证的访问骨架 + contract 边界守卫”。
2. secret 子域当前下一执行入口已切换到 SEC-TODO-009，随后再推进 lease 生命周期与轮换链路。

### 下一步

1. 进入 SEC-TODO-009，将当前 facade 内部的 active lease 映射收敛为独立 SecretLeaseRegistry，并覆盖创建/过期/释放/陈旧句柄路径。

### 风险

1. 若后续 SecretLeaseRegistry 抽取时改变现有 handle/lease 字段或把 request/task/session 复制进返回对象，本轮 contract 边界需要重新评审。

## 记录 #093

- 日期：2026-04-03
- 阶段：secret 组件专项 TODO
- 任务：SEC-TODO-007 FileSecretBackend 骨架
- 状态：已完成

### 改动

1. 完成 SEC-TODO-007-D/B 收敛：
   - 新增 docs/todos/infrastructure/deliverables/SEC-TODO-007-FileSecretBackend骨架收敛.md，补齐 blocker 解阻承接、本地证据、外部参考、Design -> Build 映射与验收结果。
   - 新增 infra/src/secret/backends/FileSecretBackend.h 与 infra/src/secret/backends/FileSecretBackend.cpp，落盘 root_dir 安全解析、key=value fixture 读取、`ciphertext_hex` 解码、backend unavailable/status 和最小 skeleton lifecycle 语义。
2. 完成测试与接线收口：
   - 新增 tests/unit/infra/secret/FileSecretBackendTest.cpp，覆盖成功路径、缺失路径、backend unavailable，并断言 materialize 不创建额外明文文件。
   - 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 file backend 源码和 unit test target 纳入构建图与 `dasall_unit_tests` 聚合目标。
3. 完成 TODO 回链：
   - 回写 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-TODO-007 标记为 Completed，并把下一入口切换到 SEC-TODO-008。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_file_secret_backend_unit_test`
   - `ctest --test-dir build-ci -N -R FileSecretBackendTest`
   - `ctest --test-dir build-ci --output-on-failure -R FileSecretBackendTest`
2. 结果：
   - configure/build 通过；`FileSecretBackendTest` 可被 `ctest -N -R` 发现，并定向执行 1/1 通过。

### 结果

1. SEC-TODO-007 已把 secret backend 骨架扩展到 file，实现了 root_dir/encrypt_at_rest 约束下的最小本地读取链路。
2. secret 子域当前下一执行入口已切换到 SEC-TODO-008，随后再推进 SEC-TODO-009。

### 下一步

1. 进入 SEC-TODO-008，基于 mock/file backend 落盘 SecretManagerFacade 的 get/materialize/release/inspect 主链。

### 风险

1. 若后续 file backend 真实加密接入改变当前 `ciphertext_hex` fixture 语义，需要以追加实现替换当前占位格式，不能回退 root_dir/encrypt_at_rest 的最小策略边界。

## 记录 #092

- 日期：2026-04-03
- 阶段：secret 组件专项 TODO
- 任务：SEC-TODO-006 MockSecretBackend 骨架
- 状态：已完成

### 改动

1. 完成 SEC-TODO-006-D/B 收敛：
   - 新增 docs/todos/infrastructure/deliverables/SEC-TODO-006-MockSecretBackend骨架收敛.md，补齐本地证据、外部参考、Design -> Build 映射与验收结果。
   - 新增 infra/src/secret/backends/MockSecretBackend.h 与 infra/src/secret/backends/MockSecretBackend.cpp，落盘 internal mock backend，支持 seeded record、permission-domain 守卫、backend availability 状态与最小 promote/revoke 语义。
2. 完成测试与接线收口：
   - 新增 tests/unit/infra/secret/MockSecretBackendTest.cpp，覆盖成功、未命中、拒绝和 backend down 四条路径。
   - 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 mock backend 源码和 unit test target 纳入构建图与 `dasall_unit_tests` 聚合目标。
3. 完成 TODO 回链：
   - 回写 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-TODO-006 标记为 Completed，并把下一入口切换到 SEC-TODO-007。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_mock_secret_backend_unit_test`
   - `ctest --test-dir build-ci -N -R MockSecretBackendTest`
   - `ctest --test-dir build-ci --output-on-failure -R MockSecretBackendTest`
2. 结果：
   - configure/build 通过；`MockSecretBackendTest` 可被 `ctest -N -R` 发现，并定向执行 1/1 通过。

### 结果

1. SEC-TODO-006 已把 secret backend 从“只有 public 协议”推进到“存在可运行的 internal mock backend + unit 验收出口”。
2. secret 子域当前下一执行入口已切换到 SEC-TODO-007，随后再推进 SEC-TODO-008 与 SEC-TODO-009。

### 下一步

1. 进入 SEC-TODO-007，按已解阻的 root_dir/encrypt_at_rest 最小策略落盘 FileSecretBackend 骨架。

### 风险

1. 若后续 facade/lease registry 直接绕过 MockSecretBackend 的 permission-domain 守卫或 backend status 语义，本轮四路径验收基线会失效，需要重新校准。

## 记录 #091

- 日期：2026-04-03
- 阶段：secret 组件专项 TODO
- 任务：SEC-BLK-001 FileSecretBackend 配置解阻
- 状态：已完成

### 改动

1. 完成 SEC-BLK-001-D 设计解阻：
   - 新增 docs/todos/infrastructure/deliverables/SEC-BLK-001-FileSecretBackend配置解阻.md，把 blocker 根因收敛为“TODO 状态未回链到已存在的 file backend 配置冻结证据”，并固定 root_dir/encrypt_at_rest 的最小策略和对 SEC-TODO-007 的交接约束。
   - 更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-BLK-001 标记为已解阻，并把 SEC-TODO-007 的 blocker 列迁移为“已由 secret 设计 6.9 解阻”。
2. 完成执行入口切换：
   - 在 secret 专项 TODO 中新增本轮执行记录，并把下一步建议切换为按顺序推进 SEC-TODO-006 -> SEC-TODO-007 -> SEC-TODO-008 -> SEC-TODO-009。

### 测试

1. 验证命令：
   - `rg -n "SEC-BLK-001|infra\.secret\.file\.root_dir|infra\.secret\.file\.encrypt_at_rest|SEC-TODO-007" docs/architecture/DASALL_infra_secret模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md docs/todos/infrastructure/deliverables/SEC-BLK-001-FileSecretBackend配置解阻.md`
2. 结果：
   - 已通过；命中 secret 设计 6.9 的 file 配置项、secret TODO 的解阻状态与执行记录，以及 blocker deliverable 的交接约束，architecture、TODO 与交付件三处证据已一致回链。

### 结果

1. SEC-BLK-001 已不再阻塞 secret backend 链路；FileSecretBackend 的 root_dir/encrypt_at_rest 最小策略已具备直接实现条件。
2. secret 子域当前下一执行入口已切换到 SEC-TODO-006，后续应按 006 -> 007 -> 008 -> 009 的顺序串行推进并逐轮提交。

### 下一步

1. 进入 SEC-TODO-006，落盘 MockSecretBackend 骨架与四路径单测。

### 风险

1. 若后续 FileSecretBackend 实现绕过 root_dir 边界、默认关闭 encrypt_at_rest，或把物理路径细节暴露到公共对象，本 blocker 需要重新转为 Blocked。

## 记录 #090

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-TODO-013 IAuditRetention 接口与 RetentionOutcome 对象
- 状态：已完成

### 改动

1. 完成 AUD-TODO-013-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-TODO-013-IAuditRetention接口冻结.md](docs/todos/infrastructure/deliverables/AUD-TODO-013-IAuditRetention%E6%8E%A5%E5%8F%A3%E5%86%BB%E7%BB%93.md)，补齐本地证据、Design -> Build 映射与验收结果。
   - 新增 [infra/include/audit/IAuditRetention.h](infra/include/audit/IAuditRetention.h)，冻结 `AuditCleanupTrigger`、`AuditArchiveAction`、`AuditCleanupEvidence`、`RetentionOutcome` 与 `IAuditRetention::apply_retention(now_ts)` 边界，并把 completed/error_code、archive action、cleanup evidence 的一致性检查收敛到 header-only 对象方法。
2. 完成测试出口收口：
   - 更新 [tests/unit/infra/AuditLoggerInterfaceTest.cpp](tests/unit/infra/AuditLoggerInterfaceTest.cpp)，新增 retention interface compile/success/failure 断言，验证 single-entry boundary 与 cleanup trace 负例。
   - 更新 [tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp](tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp)，新增 retention success/failure object 仍只映射既有 `contracts::ResultCode` 的 contract 守卫。
3. 完成 TODO 回链：
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-TODO-013` 标记为 Done，并把 audit 组件专项 TODO 结论切换为“当前列表已全部完成”。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test dasall_contract_infra_error_code_boundary_test`
   - `ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest|InfraErrorCodeMappingContractTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "AuditInterfaceCompileTest|InfraErrorCodeMappingContractTest"`
   - `cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test`
   - `ctest --test-dir build-ci -N -L audit`
   - `ctest --test-dir build-ci --output-on-failure -L audit`
2. 结果：
   - 定向 `AuditInterfaceCompileTest`、`InfraErrorCodeMappingContractTest` 2/2 通过。
   - audit 标签下 9 个测试全部通过，覆盖 4 个 unit、4 个 contract、1 个 integration。

### 结果

1. `AUD-TODO-013` 已把 retention 公共接口从“设计冻结”推进到“header + unit/contract + audit gate”落盘完成。
2. 当前 audit 组件专项 TODO 列表内的原子任务已经全部完成；后续若继续推进，应另起 retention manager / archive backend / 自动清理调度的新任务范围。

### 下一步

1. 若继续推进 audit retention 执行层，新增 manager/调度类 TODO，并保持现有 `IAuditRetention` 边界不漂移。

### 风险

1. 若后续 retention 执行层绕过 `RetentionOutcome` 的 completed/error_code、archive action 或 cleanup evidence 一致性检查，本轮公共接口边界需要重新评审。

## 记录 #089

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-BLK-002 RetentionOutcome 与 cleanup 证据语义解阻
- 状态：已完成

### 改动

1. 完成 AUD-BLK-002-D 设计解阻：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-BLK-002-RetentionOutcome设计收敛.md](docs/todos/infrastructure/deliverables/AUD-BLK-002-RetentionOutcome%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，把 blocker 根因收敛为“audit 未冻结 RetentionOutcome、archive action 与 cleanup trace 的最小协议”，并给出直达 `AUD-TODO-013` 的交接约束。
   - 更新 [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit%E6%A8%A1%E5%9D%97%E8%AF%A6%E7%BB%86%E8%AE%BE%E8%AE%A1.md)，补齐 retention 对象表，并新增 `6.6.2 RetentionOutcome 与归档/清理证据冻结（AUD-BLK-002）`，固定 `completed/error_code`、`archive_ref`/`cleanup_ref` 与 Manual/Scheduled cleanup trace 规则。
2. 完成 blocker 回链：
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-BLK-002` 标记为已解阻，并把 `AUD-TODO-013` 从 Blocked 迁移到 Not Started；同时将 `AUD-GATE-06` 切换为 PASS，并把下一步切换到 `AUD-TODO-013` 的接口落盘轮。

### 测试

1. 验证命令：
   - `rg -n "6\.6\.2 RetentionOutcome 与归档/清理证据冻结|AUD-BLK-002|AUD-TODO-013" docs/architecture/DASALL_infra_audit模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md docs/todos/infrastructure/deliverables/AUD-BLK-002-RetentionOutcome设计收敛.md`
2. 结果：
   - retention 冻结章节、TODO 解阻状态和 blocker 交付件均已可定位追溯，`AUD-TODO-013` 已具备进入接口落盘轮的前置条件。

### 结果

1. `AUD-BLK-002` 已把 retention 输出对象、archive action 与 cleanup trace 的最小协议冻结完成。
2. audit 子域当前下一执行入口已切换到 `AUD-TODO-013`，后续应直接落盘 `IAuditRetention.h` 与 compile tests。

### 下一步

1. 进入 `AUD-TODO-013`，按已冻结的 completed/error_code、archive action 与 cleanup evidence 规则落盘 `IAuditRetention`。

### 风险

1. 若后续 retention 实现允许无 `cleanup_evidence` 的删除成功结果，或把 archive 物理路径/第三方存储地址暴露到公共对象，本轮边界需要重新评审。

## 记录 #088

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-TODO-012 AuditExporter 导出与脱敏骨架
- 状态：已完成

### 改动

1. 完成 AUD-TODO-012-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-TODO-012-AuditExporter骨架收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-012-AuditExporter%E9%AA%A8%E6%9E%B6%E6%94%B6%E6%95%9B.md)，补齐本地证据、外部参考、Design -> Build 映射与验收结果。
   - 新增 [infra/src/audit/AuditExporter.h](infra/src/audit/AuditExporter.h) 与 [infra/src/audit/AuditExporter.cpp](infra/src/audit/AuditExporter.cpp)，把导出过滤、稳定排序、opaque resume token 与 AuditEvent-only 导出边界收口成独立 internal exporter。
   - 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)，将导出逻辑从 service 内联筛选切换为委托 `AuditExporter::export_records()`。
2. 完成测试与接线收口：
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，将 `AuditExporter.cpp` 纳入 `dasall_infra` 构建图。
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt) 与 [tests/unit/infra/AuditExportFilterTest.cpp](tests/unit/infra/AuditExportFilterTest.cpp)，为 exporter unit 测试补 `infra/src` include path，并新增主过滤、分页 token 与 token 失配负例覆盖。
   - 更新 [tests/contract/smoke/AuditBoundaryContractTest.cpp](tests/contract/smoke/AuditBoundaryContractTest.cpp)，固定“不引入 `target_pattern`/`outcome_reason`，不把 `AuditContext` 合并进导出载荷”的 contract 边界。
3. 完成 TODO 回链：
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-TODO-012` 标记为 Done，并把下一步切换到 `AUD-BLK-002` 的 retention 设计解阻。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_audit_export_filter_unit_test dasall_audit_service_fallback_unit_test dasall_contract_audit_event_boundary_test`
   - `ctest --test-dir build-ci -N -R "AuditExportFilterTest|AuditBoundaryContractTest|AuditServiceFallbackTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "AuditExportFilterTest|AuditBoundaryContractTest|AuditServiceFallbackTest"`
   - `cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test`
   - `ctest --test-dir build-ci -N -L audit`
   - `ctest --test-dir build-ci --output-on-failure -L audit`
2. 结果：
   - 定向 `AuditExportFilterTest`、`AuditBoundaryContractTest`、`AuditServiceFallbackTest` 3/3 通过。
   - audit 标签下 9 个测试全部通过，覆盖 4 个 unit、4 个 contract、1 个 integration。

### 结果

1. `AUD-TODO-012` 已把导出逻辑从 service 内联筛选推进为独立 internal exporter，并落盘了 v1 的过滤、分页与导出边界骨架。
2. audit 子域当前下一执行入口已切换到 `AUD-BLK-002`，后续应先补齐 RetentionOutcome 与归档/清理动作对象。

### 下一步

1. 进入 `AUD-BLK-002`，冻结 RetentionOutcome 与归档/清理动作对象，再恢复 `AUD-TODO-013`。

### 风险

1. 若后续 exporter 试图扩张到 `target_pattern`、free-text reason、AuditContext payload 或未绑定过滤元组的 page token，本轮边界需要重新评审。

## 记录 #087

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-BLK-001 AuditExporter 过滤语义解阻
- 状态：已完成

### 改动

1. 完成 AUD-BLK-001-D 设计解阻：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-BLK-001-ExportQuery过滤语义设计收敛.md](docs/todos/infrastructure/deliverables/AUD-BLK-001-ExportQuery%E8%BF%87%E6%BB%A4%E8%AF%AD%E4%B9%89%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，把 blocker 根因收敛为“audit 未冻结 ExportQuery 的主过滤轴、target/outcome 扩展规则、稳定 resume token 与导出边界”，并给出直达 `AUD-TODO-012` 的交接约束。
   - 更新 [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md)，新增 `6.5.1 ExportQuery 最小过滤与导出边界冻结（AUD-BLK-001）`，补齐窗口+actor+action 主过滤、target/outcome exact-match 扩展、稳定排序/分页与 AuditEvent-only 导出边界。
2. 完成 blocker 回链：
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-BLK-001` 标记为已解阻，并把 `AUD-TODO-012` 从 Blocked 迁移到 Not Started；同时清理当前态中仍残留的旧 blocker 话术，保证粒度扫描、可行性结论与下一步建议和当前状态一致。

### 测试

1. 验证命令：
   - `rg -n "6\.5\.1 ExportQuery 最小过滤与导出边界冻结|AUD-BLK-001|AUD-TODO-012" docs/architecture/DASALL_infra_audit模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md docs/todos/infrastructure/deliverables/AUD-BLK-001-ExportQuery过滤语义设计收敛.md`
2. 结果：
   - ExportQuery 过滤/边界冻结章节、TODO 解阻状态和 blocker 交付件均已可定位追溯，`AUD-TODO-012` 已具备进入实现轮的前置条件。

### 结果

1. `AUD-BLK-001` 已不再阻塞 audit 子域继续推进；下一轮可直接进入 `AUD-TODO-012` 的 exporter 过滤/分页/脱敏骨架落盘。

### 下一步

1. 进入 `AUD-TODO-012`，按已冻结的窗口+actor+action 主过滤、target/outcome 扩展规则与 AuditEvent-only 导出边界落盘 `AuditExporter`。

### 风险

1. 若后续 `ExportQuery` 回退为 pattern/wildcard 查询、让 page token 脱离过滤元组复用，或把 `AuditContext` 直接并入导出结果，本 blocker 需要重新转为 Blocked。

## 记录 #086

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-TODO-019 Audit 质量门与证据收口
- 状态：已完成

### 改动

1. 完成 AUD-TODO-019-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-TODO-019-Audit质量门与证据收口.md](docs/todos/infrastructure/deliverables/AUD-TODO-019-Audit%E8%B4%A8%E9%87%8F%E9%97%A8%E4%B8%8E%E8%AF%81%E6%8D%AE%E6%94%B6%E5%8F%A3.md)，补齐 gate 基线、PASS/BLOCKED 结论与阻塞变化说明。
   - 更新 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 019 标记为 Done，并新增 9.3 gate 结论表、当前 audit 专项 gate 基线与新的下一步建议。
2. 完成文档证据一致性修正：
   - 同步修正文档中 `InfraAuditHealthIntegrationTest` 的路径引用，使其与 018 收口后的 [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp) 实际落点一致。
3. 完成当前轮闭环：
   - 回写本执行记录，并将后续入口明确指向 `AUD-BLK-001` / `AUD-BLK-002`，不再对已完成的 health/metrics/integration 任务重复收口。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test`
   - `ctest --test-dir build-ci -N -L audit`
   - `ctest --test-dir build-ci --output-on-failure -L audit`
2. 结果：
   - audit 标签下共发现 9 个测试，覆盖 4 个 unit、4 个 contract、1 个 integration，执行 9/9 通过。

### 结果

1. `AUD-TODO-019` 已将 audit 当前轮收口为统一的 `ctest -L audit` gate 基线，并明确了 PASS gate 与仍受 blocker 限制的 BLOCKED gate。
2. audit 子域下一执行入口已切换到 `AUD-BLK-001` / `AUD-BLK-002`，后续若继续推进，应先解阻导出与 retention 设计缺口。

### 下一步

1. 若继续推进 audit 组件专项 TODO，优先进入 `AUD-BLK-001` 与 `AUD-BLK-002` 的解阻轮，再恢复 `AUD-TODO-012` / `AUD-TODO-013` 的执行。

### 风险

1. 若后续新增 audit 测试未继续纳入 `audit` 标签，或 tests 顶层聚合回退，当前 `ctest -L audit` gate 基线将失效，需要重新校准。

## 记录 #085

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-TODO-018 Audit integration 测试入口收口
- 状态：已完成

### 改动

1. 完成 AUD-TODO-018-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-TODO-018-AuditIntegration测试接线收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-018-AuditIntegration%E6%B5%8B%E8%AF%95%E6%8E%A5%E7%BA%BF%E6%94%B6%E6%95%9B.md)，补齐本地证据、discoverability 结论、Design -> Build 映射与验收结果。
   - 新增 [tests/integration/infra/audit/CMakeLists.txt](tests/integration/infra/audit/CMakeLists.txt)，定义 `dasall_register_audit_integration_test`，统一 `integration;audit` 标签与 `infra/src` include path。
   - 将现有用例迁移到 [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp)，保持 015 已落盘的 health/metrics 协同断言不变。
2. 完成顶层 integration 聚合收口：
   - 更新 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt)，移除 root-level audit 直连注册，改为 `add_subdirectory(audit)`。
   - 更新 [tests/integration/CMakeLists.txt](tests/integration/CMakeLists.txt)，将 `dasall_infra_audit_health_integration_test` 纳入 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS`，补齐顶层 integration gate 聚合边界。
3. 完成 TODO 回链：
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-TODO-018` 标记为 Done，并把下一步切换到 `AUD-TODO-019` 的质量门证据收口。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra_audit_health_integration_test`
   - `ctest --test-dir build-ci -N -R "InfraAuditHealthIntegrationTest"`
   - `ctest --test-dir build-ci -N -L audit -R "InfraAuditHealthIntegrationTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "InfraAuditHealthIntegrationTest"`
2. 结果：
   - `InfraAuditHealthIntegrationTest` 可被名字与 `audit` 标签同时发现，并稳定执行 1/1 通过。

### 结果

1. `AUD-TODO-018` 已将 audit integration 入口从“根级临时注册”推进到“audit 子目录 + integration;audit 标签 + 顶层 target 聚合”的稳定 discoverability 形态。
2. `AUD-TODO-019` 现在可以只聚焦 quality gate、阻塞变化与回退证据回写。

### 下一步

1. 进入 `AUD-TODO-019`，统一回写 unit/contract/integration 质量门、阻塞变化与回退证据，完成 audit 专项 TODO 当前轮收口。

### 风险

1. 若后续 tests 顶层 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS` 或 `integration;audit` 标签发生回退，audit integration discoverability 需要重新校准。

## 记录 #084

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-TODO-015 AuditMetricsBridge 指标桥接骨架
- 状态：已完成

### 改动

1. 完成 AUD-TODO-015-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-TODO-015-AuditMetricsBridge骨架收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-015-AuditMetricsBridge%E9%AA%A8%E6%9E%B6%E6%94%B6%E6%95%9B.md)，补齐本地证据、外部参考、Design -> Build 映射与验收结果。
   - 新增 [infra/src/audit/AuditMetricsBridge.h](infra/src/audit/AuditMetricsBridge.h) 与 [infra/src/audit/AuditMetricsBridge.cpp](infra/src/audit/AuditMetricsBridge.cpp)，冻结 `infra.audit@v1` meter scope、七指标 family、五元标签白名单，以及 provider degraded / config-invalid no-op 回退语义。
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，将 `AuditMetricsBridge.cpp` 纳入 `dasall_infra` 构建图。
2. 完成现有 integration ground truth 扩展：
   - 更新 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt)，为现有 audit integration 测试补 `infra/src` include path。
   - 更新 [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp)，新增 fake `RecordingMetricsProvider` / `RecordingMeter`，把 health probe 改为读取真实 `AuditMetricsBridge::is_degraded()`，并验证 `audit_write_total` 成功发射、fallback 路径、provider timeout -> bridge degraded 与 stopped unavailable 四类场景。
3. 完成 TODO 回链：
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-TODO-015` 标记为 Done，并将下一步切换到 `AUD-TODO-018` 的 integration 注册收口。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_infra_audit_health_integration_test`
   - `ctest --test-dir build-ci -N -R "InfraAuditHealthIntegrationTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "InfraAuditHealthIntegrationTest"`
2. 结果：
   - `InfraAuditHealthIntegrationTest` 共发现 1 个定向测试，执行 1/1 通过。

### 结果

1. `AUD-TODO-015` 已从“设计冻结”推进到“internal bridge + 真实 metrics degraded 协同 ground truth”全部落盘。
2. `AUD-TODO-018` 现在可以只聚焦 integration 子目录、顶层 target 聚合与 `integration;audit` 标签 discoverability 收口，不再承担 bridge 语义落盘。

### 下一步

1. 进入 `AUD-TODO-018`，把现有根级 `InfraAuditHealthIntegrationTest` 收口到 `tests/integration/infra/audit/` 子目录、顶层 integration target 聚合与 `integration;audit` 标签。

### 风险

1. 若后续 audit bridge 回退为动态 metric family、允许高基数标签，或把 bridge degraded 直接升级为 `Unavailable`，本轮 bridge 骨架需要重新评审。

## 记录 #083

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-TODO-014 IAuditHealthProbe 接口落盘
- 状态：已完成

### 改动

1. 完成 AUD-TODO-014-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-TODO-014-AuditHealthProbe接口收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-014-AuditHealthProbe%E6%8E%A5%E5%8F%A3%E6%94%B6%E6%95%9B.md)，补齐本地证据、外部参考、Design -> Build 映射与验收结果。
   - 新增 [infra/include/audit/IAuditHealthProbe.h](infra/include/audit/IAuditHealthProbe.h)，冻结 `AuditHealthState`、`AuditHealthStatus`、reason allowlist 与只读 `evaluate() const` 边界，并更新 [infra/CMakeLists.txt](infra/CMakeLists.txt) 将其纳入 audit public headers。
   - 更新 [tests/unit/infra/AuditLoggerInterfaceTest.cpp](tests/unit/infra/AuditLoggerInterfaceTest.cpp)，补齐 `IAuditHealthProbe` 签名冻结、`AuditHealthStatus` 正负例一致性断言。
2. 完成最小 integration ground truth：
   - 新增 [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp)，用 test-local `AuditServiceBackedHealthProbe` 验证 Ready、fallback degraded、metrics bridge degraded 与 stopped unavailable 四类状态映射。
   - 更新 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt)，新增根级 `InfraAuditHealthIntegrationTest` 注册，作为当前轮可执行验收出口；audit 专项目录、顶层 target 聚合与 `integration;audit` 标签收口仍留给 `AUD-TODO-018`。
3. 完成 TODO 回链：
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-TODO-014` 标记为 Done，并把 `AUD-TODO-018` 的描述更新为“已有根级用例，待目录/标签拓扑收口”。

### 测试

1. 验证命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test dasall_infra_audit_health_integration_test`
   - `ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest"`
2. 结果：
   - `AuditInterfaceCompileTest` 与 `InfraAuditHealthIntegrationTest` 共发现 2 个定向测试，执行 2/2 通过。

### 结果

1. `AUD-TODO-014` 已从“设计冻结”推进到“public interface + 状态对象守卫 + 最小 integration ground truth”全部落盘。
2. `AUD-TODO-015` 现在可以直接复用 `InfraAuditHealthIntegrationTest` 扩展 metrics degraded 场景；`AUD-TODO-018` 保留为 integration 目录/标签拓扑收口任务。

### 下一步

1. 进入 `AUD-TODO-015`，沿已冻结的 meter scope、七指标对象表、五元标签白名单与 non-recursive failure 语义落盘 `AuditMetricsBridge` 骨架，并复用现有 `InfraAuditHealthIntegrationTest` 补 metrics bridge degraded 断言。

### 风险

1. 若后续 `AuditHealthStatus` 被回退为自由文本对象、把 `metrics_bridge_degraded` 提升为 `Unavailable` 触发器，或让 `IAuditHealthProbe` 吸收 `probe/register_probe` 等额外职责，本轮接口冻结需要重新评审。

## 记录 #082

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-BLK-004 AuditMetricsBridge 协议解阻
- 状态：已完成

### 改动

1. 完成 AUD-BLK-004-D 设计解阻：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-BLK-004-AuditMetricsBridge设计收敛.md](docs/todos/infrastructure/deliverables/AUD-BLK-004-AuditMetricsBridge%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，把 blocker 根因收敛为“audit 未冻结 metrics provider/meter 接入协议、七指标对象表、标签白名单与 non-recursive failure 语义”，并给出直达 `AUD-TODO-015` 的交接约束。
   - 更新 [docs/architecture/DASALL_infra_metrics模块详细设计.md](docs/architecture/DASALL_infra_metrics模块详细设计.md)，新增 `6.6.2` 与 `6.8.2`，冻结 audit bridge 的 meter scope、七指标对象表、五元标签白名单和失败语义。
   - 更新 [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md)，新增 `6.10.1 AuditMetricsBridge 协议冻结（AUD-BLK-004）`，补齐 bridge degraded 到 `AuditHealthStatus` 的对齐规则。
2. 完成 blocker 回链：
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-BLK-004` 标记为已解阻，并把 `AUD-TODO-015` 从 Blocked 迁移到 Not Started；同时清理 TODO 中仍残留的旧 blocker 话术，保证对象扫描、风险和下一步建议与当前状态一致。

### 测试

1. 验证命令：
   - `rg -n "6\\.6\\.2 跨模块指标桥接协议（audit v1）|6\\.8\\.2 audit 指标桥接失败语义|6\\.10\\.1 AuditMetricsBridge 协议冻结|AUD-BLK-004|AUD-TODO-015" docs/architecture/DASALL_infra_metrics模块详细设计.md docs/architecture/DASALL_infra_audit模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md docs/todos/infrastructure/deliverables/AUD-BLK-004-AuditMetricsBridge设计收敛.md`
2. 结果：
   - audit bridge 的协议冻结章节、TODO 解阻状态和 blocker 交付件均已可定位追溯，`AUD-TODO-015` 已具备进入实现轮的前置条件。

### 结果

1. `AUD-BLK-004` 已不再阻塞 audit 子域继续推进；后续可按依赖顺序进入 `AUD-TODO-014` 与 `AUD-TODO-015` 的接口/桥接实现轮。

### 下一步

1. 进入 `AUD-TODO-014`，按已冻结的 `AuditHealthStatus` 三态与只读 evaluate 语义落盘 `IAuditHealthProbe.h`，再继续推进 `AUD-TODO-015`。

### 风险

1. 若后续 audit bridge 回退为动态 metric family、引入高基数标签，或把 bridge degraded 直接映射为 `Unavailable`，本 blocker 需要重新转为 Blocked。

## 记录 #081

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-BLK-003 AuditHealthProbe 接口边界解阻
- 状态：已完成

### 改动

1. 完成 AUD-BLK-003-D 设计解阻：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-BLK-003-AuditHealthProbe设计收敛.md](docs/todos/infrastructure/deliverables/AUD-BLK-003-AuditHealthProbe%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，把 blocker 根因收敛为“audit 未把健康三态和最近失败原因冻结成私有状态对象”，并明确 `Ready/Degraded/Unavailable` 三态、reason allowlist 与只读 evaluate 语义。
   - 更新 [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md)，新增 `AuditHealthStatus` 对象表与 6.6.1 状态冻结段。
2. 完成 blocker 回链：
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-BLK-003` 标记为已解阻，并把 `AUD-TODO-014` 从 Blocked 迁移到 Not Started。

### 测试

1. 验证命令：
   - `grep -n "AuditHealthStatus\|6.6.1 AuditHealthStatus 状态冻结\|AUD-BLK-003\|AUD-TODO-014" docs/architecture/DASALL_infra_audit模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md docs/todos/infrastructure/deliverables/AUD-BLK-003-AuditHealthProbe设计收敛.md`
2. 结果：
   - `AuditHealthStatus` 三态、最近失败原因字段和 `AUD-TODO-014` 的解阻状态均已可定位追溯。

### 结果

1. `AUD-BLK-003` 已不再阻塞 audit 子域继续推进；下一轮可直接进入 `AUD-TODO-014` 的 public interface 落盘。

### 下一步

1. 进入 `AUD-BLK-004`，冻结 audit metrics bridge 的 IMetricsProvider/IMeter 接入协议、指标名清单、标签白名单与 non-recursive failure 语义。

### 风险

1. 若后续 `AuditHealthStatus` 回退成自由文本对象，或试图直接暴露 `infra/health` 公共对象替代 audit 私有状态，本 blocker 需要重新转为 Blocked。

## 记录 #080

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-TODO-017 注册 audit 的 unit 与 contract 测试入口
- 状态：已完成

### 改动

1. 完成 AUD-TODO-017-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-TODO-017-Audit测试接线收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-017-Audit%E6%B5%8B%E8%AF%95%E6%8E%A5%E7%BA%BF%E6%94%B6%E6%95%9B.md)，补齐本地证据、Design->Build 映射与 discoverability 验收结果。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-TODO-017` 标记为 Done，并追加 12.13 执行记录与验收证据。
2. 完成 AUD-TODO-017-B 测试注册收口：
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)，新增 `dasall_register_audit_unit_test`，统一 audit unit 测试的注册与 `unit;audit` 标签。
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)，新增 `DASALL_AUDIT_UNIT_TEST_EXECUTABLE_TARGETS`，把 audit unit target 从 logging 与通用列表中抽出。
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)，新增 `dasall_register_audit_contract_test`，统一 audit contract 测试的 `contract;smoke;audit` 标签。

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
   - `ctest --test-dir build-ci -N -L audit`
   - `ctest --test-dir build-ci --output-on-failure -L audit`
2. 结果：
   - `ctest -N` 发现总计 254 个测试，其中 audit 标签下可发现 8 个测试。
   - `ctest -L unit` 112/112 通过，`ctest -L contract` 132/132 通过。
   - `ctest -L audit` 8/8 通过，覆盖 4 个 unit + 4 个 contract。

### 结果

1. audit 测试已从“分散在 logging/通用注册路径”推进到“具备独立 audit helper、顶层分组和模块级 discoverability 标签”。
2. 本轮没有搬迁测试源码目录，避免为接线任务引入不必要的路径级重构。

### 下一步

1. 进入 `AUD-TODO-018`，评估 audit integration 测试入口接线是否已具备最小可执行条件。

### 风险

1. `AUD-TODO-018` 仍取决于 integration 侧具体用例是否已经具备稳定落点；若 audit health/metrics 相关实现仍未冻结，可能只能先完成入口级收口而非完整业务覆盖。

## 记录 #079

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-TODO-016 注册 audit 源码到 infra CMake
- 状态：已完成

### 改动

1. 完成 AUD-TODO-016-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-TODO-016-Audit构建接线收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-016-Audit%E6%9E%84%E5%BB%BA%E6%8E%A5%E7%BA%BF%E6%94%B6%E6%95%9B.md)，补齐本地证据、外部参考、Design->Build 映射与 D Gate 结果。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-TODO-016` 标记为 Done，并追加 12.12 执行记录与验收证据。
2. 完成 AUD-TODO-016-B 构建接线收口：
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，新增 `DASALL_INFRA_AUDIT_SOURCES`，把 `AuditValidator.cpp`、`AuditPipeline.cpp`、`AuditFallbackPipeline.cpp`、`AuditService.cpp` 从通用 core 列表抽成独立 audit 构建入口。
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，新增 `DASALL_INFRA_AUDIT_PUBLIC_HEADERS`，把 audit public headers 从通用 header 列表抽成独立导出入口。

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test`
   - `ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest"`
   - `ctest --test-dir build-ci -R "AuditInterfaceCompileTest" --output-on-failure`
2. 结果：
   - `AuditInterfaceCompileTest` 定向发现 1 个，执行 1/1 通过。
   - `dasall_infra` 与 audit public header 接线在独立 CMake 变量下保持通过。

### 结果

1. audit source/header 已从“顺手挂进 core/public 列表”推进到“在 infra CMake 中具备独立可追踪的专项入口”。
2. 本轮没有扩张到测试标签和 discoverability 收口；这些后续由 `AUD-TODO-017` 单独处理。

### 下一步

1. 进入 `AUD-TODO-017`，收口 audit unit/contract 测试注册与 discoverability 标签面。

### 风险

1. 当前 `AuditInterfaceCompileTest` 仍带有历史 `logging` 标签，若不在 017 中修正，audit 测试 discoverability 仍然不够清晰。

## 记录 #078

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-TODO-011 实现 AuditServiceFacade 入口骨架
- 状态：已完成

### 改动

1. 完成 AUD-TODO-011-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-TODO-011-AuditServiceFacade骨架收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-011-AuditServiceFacade%E9%AA%A8%E6%9E%B6%E6%94%B6%E6%95%9B.md)，补齐本地证据、OWASP 外部参考、Design->Build 映射与 D Gate 结果。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-TODO-011` 标记为 Done，并追加 12.11 执行记录与验收证据。
2. 完成 AUD-TODO-011-B facade 骨架落地：
   - 更新 [infra/include/audit/AuditService.h](infra/include/audit/AuditService.h)，让 `AuditService` 收敛为 thin wrapper，持有 internal facade 指针，并显式声明构造、析构、拷贝、移动语义。
   - 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)，新增 internal `AuditServiceFacade`，统一处理生命周期、validator/pipeline/fallback 串接、export 选择和错误映射。
   - 更新 [tests/unit/infra/AuditServiceFallbackTest.cpp](tests/unit/infra/AuditServiceFallbackTest.cpp)，补充 lifecycle state 与 pre-start write gate 回归测试。

### 测试

1. 验收命令：
   - `cmake --build build-ci --target dasall_infra dasall_audit_service_fallback_unit_test dasall_contract_infra_error_code_boundary_test dasall_contract_audit_service_boundary_test`
   - `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest|InfraErrorCodeMappingContractTest|AuditServiceBoundaryContractTest"`
   - `ctest --test-dir build-ci -R "AuditServiceFallbackTest|InfraErrorCodeMappingContractTest|AuditServiceBoundaryContractTest" --output-on-failure`
2. 结果：
   - `AuditServiceFallbackTest`、`InfraErrorCodeMappingContractTest`、`AuditServiceBoundaryContractTest` 定向发现 3 个，执行 3/3 通过。
   - facade 化后，lifecycle/pre-start gate、错误码映射和 service 边界回归均保持通过。

### 结果

1. `AuditServiceFacade` 已从“设计职责存在但未显式收口”推进到“internal facade + public wrapper + lifecycle/write/export 串接已落盘并可测”。
2. 本轮没有引入新的 public audit interface，也没有扩张到 exporter/retention/metrics/health；`AUD-TODO-008` 到 `AUD-TODO-011` 的主链路骨架已全部完成。

### 下一步

1. 进入 `AUD-TODO-016` 与 `AUD-TODO-017`，正式收口 audit 源码接线与 unit/contract 测试发现性证据。

### 风险

1. facade 化当前仍以内嵌 internal class 直接持有本地 record store；后续若继续扩展 exporter/retention 能力，需要继续守住 public wrapper 不扩张、internal facade 不泄露的边界。

## 记录 #077

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-TODO-010 实现 AuditFallbackPipeline 降级骨架
- 状态：已完成

### 改动

1. 完成 AUD-TODO-010-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-TODO-010-AuditFallbackPipeline骨架收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-010-AuditFallbackPipeline%E9%AA%A8%E6%9E%B6%E6%94%B6%E6%95%9B.md)，补齐本地证据、OWASP 外部参考、Design->Build 映射与 D Gate 结果。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-TODO-010` 标记为 Done，并追加 12.10 执行记录与验收证据。
2. 完成 AUD-TODO-010-B 降级骨架落地：
   - 新增 [infra/src/audit/AuditFallbackPipeline.h](infra/src/audit/AuditFallbackPipeline.h) 与 [infra/src/audit/AuditFallbackPipeline.cpp](infra/src/audit/AuditFallbackPipeline.cpp)，定义 internal `AuditFallbackWriteResult` 与降级 `AuditFallbackPipeline`。
   - 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)，将主写失败后的降级 append 改为委托 fallback pipeline。
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，将 `AuditFallbackPipeline.cpp` 纳入 `dasall_infra`。
   - 更新 [tests/unit/infra/AuditServiceFallbackTest.cpp](tests/unit/infra/AuditServiceFallbackTest.cpp)，补充 fallback append 顺序正例，同时保持 fallback exhaustion 回归断言。

### 测试

1. 验收命令：
   - `cmake --build build-ci --target dasall_infra dasall_audit_service_fallback_unit_test`
   - `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest"`
   - `ctest --test-dir build-ci -R "AuditServiceFallbackTest" --output-on-failure`
2. 结果：
   - `AuditServiceFallbackTest` 定向发现 1 个，执行 1/1 通过。
   - 新增 fallback append 顺序断言通过，既有 fallback exhaustion 回归保持通过。

### 结果

1. `AuditFallbackPipeline` 已从“设计存在但实现缺失”推进到“独立 internal fallback pipeline + service 接线 + 单测回归已落盘”。
2. 本轮没有提前实现 facade 统一入口；audit 主链继续保持 validator -> pipeline -> fallback -> facade 的串行推进顺序。

### 下一步

1. 进入 `AUD-TODO-011`，将 validator/pipeline/fallback 串成统一的 AuditServiceFacade 入口骨架。

### 风险

1. 当前 fallback pipeline 仍以 internal helper 直接操作现有 degraded record store；后续在 011 做 facade 收敛时，要避免破坏 010 已建立的 fallback append 顺序与 exhaustion 语义。

## 记录 #076

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-TODO-009 实现 AuditPipeline 主写骨架
- 状态：已完成

### 改动

1. 完成 AUD-TODO-009-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-TODO-009-AuditPipeline骨架收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-009-AuditPipeline%E9%AA%A8%E6%9E%B6%E6%94%B6%E6%95%9B.md)，补齐本地证据、OWASP 外部参考、Design->Build 映射与 D Gate 结果。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-TODO-009` 标记为 Done，并追加 12.9 执行记录与验收证据。
2. 完成 AUD-TODO-009-B 主写骨架落地：
   - 新增 [infra/src/audit/AuditPipeline.h](infra/src/audit/AuditPipeline.h) 与 [infra/src/audit/AuditPipeline.cpp](infra/src/audit/AuditPipeline.cpp)，定义 internal `AuditPipelineWriteResult` 与 append-only `AuditPipeline`。
   - 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)，将 validator 通过后的主写 append 改为委托 pipeline。
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，将 `AuditPipeline.cpp` 纳入 `dasall_infra`。
   - 更新 [tests/unit/infra/AuditServiceFallbackTest.cpp](tests/unit/infra/AuditServiceFallbackTest.cpp)，补充主写 append-only 顺序正例，同时保持 fallback 回归断言。

### 测试

1. 验收命令：
   - `cmake --build build-ci --target dasall_infra dasall_audit_service_fallback_unit_test`
   - `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest"`
   - `ctest --test-dir build-ci -R "AuditServiceFallbackTest" --output-on-failure`
2. 结果：
   - `AuditServiceFallbackTest` 定向发现 1 个，执行 1/1 通过。
   - 新增主写 append-only 顺序断言通过，既有 fallback exhaustion 回归保持通过。

### 结果

1. `AuditPipeline` 已从“设计存在但实现缺失”推进到“append-only internal pipeline + service 接线 + 单测回归已落盘”。
2. 本轮没有提前实现 fallback 或 facade；audit 主链仍严格保持 validator -> pipeline -> fallback -> facade 的串行拆分顺序。

### 下一步

1. 进入 `AUD-TODO-010`，把降级写入链路从 `AuditService` 拆到独立 `AuditFallbackPipeline`。

### 风险

1. 本轮 pipeline 仍以 internal helper 直接操作现有 primary record store；后续在 010/011 继续拆分时，要避免为了 facade 收敛反向破坏 009 已建立的 append-only 顺序语义。

## 记录 #075

- 日期：2026-04-03
- 阶段：audit 组件专项 TODO
- 任务：AUD-TODO-008 实现 AuditValidator 字段校验骨架
- 状态：已完成

### 改动

1. 完成 AUD-TODO-008-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/AUD-TODO-008-AuditValidator骨架收敛.md](docs/todos/infrastructure/deliverables/AUD-TODO-008-AuditValidator%E9%AA%A8%E6%9E%B6%E6%94%B6%E6%95%9B.md)，补齐本地证据、OWASP/OTel 外部参考、Design->Build 映射与 D Gate 结果。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 `AUD-TODO-008` 标记为 Done，并追加 12.8 执行记录与验收证据。
2. 完成 AUD-TODO-008-B validator 骨架落地：
   - 新增 [infra/src/audit/AuditValidator.h](infra/src/audit/AuditValidator.h) 与 [infra/src/audit/AuditValidator.cpp](infra/src/audit/AuditValidator.cpp)，定义 internal `AuditValidationResult` 与 `AuditValidator`，统一收敛 write/export 输入校验。
   - 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)，将 `write_audit()` / `export_audit()` 的输入校验改为委托 validator。
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，最小接入 `AuditValidator.cpp` 到 `dasall_infra` 构建图。
   - 更新 [tests/unit/infra/AuditTypesTest.cpp](tests/unit/infra/AuditTypesTest.cpp) 与 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)，为既有 `AuditTypesTest` 增补 validator 正负例，并给该 test target 增加 `infra/src` include path。

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_audit_event_unit_test dasall_contract_audit_event_boundary_test dasall_audit_service_fallback_unit_test`
   - `ctest --test-dir build-ci -N -R "AuditTypesTest|AuditBoundaryContractTest"`
   - `ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest|AuditServiceFallbackTest" --output-on-failure`
2. 结果：
   - `AuditTypesTest` 与 `AuditBoundaryContractTest` 定向发现 2 个，3 个相关测试执行 3/3 通过。
   - `AuditServiceFallbackTest` 回归通过，说明 validator 下沉后未破坏现有 service 主写/fallback 语义。

### 结果

1. `AuditValidator` 已从“设计存在但实现缺失”推进到“internal validator + 统一校验结果 + service 接线 + 正负例验证已落盘”。
2. 本轮没有引入新的 public audit contract，也没有提前落地 pipeline/fallback/facade 后续职责；audit 主链依旧保持 008 -> 009 -> 010 -> 011 的串行推进顺序。

### 下一步

1. 进入 `AUD-TODO-009`，把 append-only 主写逻辑从 `AuditService` 拆分到独立 `AuditPipeline`。

### 风险

1. 本轮只完成 validator 骨架和最小 CMake 接线；`AUD-TODO-016` 的完整 audit 源码接线收敛仍未关闭，后续继续新增 audit internal 源文件时需要保持 source graph 与 discoverability 一致。

## 记录 #074

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-TODO-019 实现 LogQueryService 受控查询与本地 artifact 导出骨架
- 状态：已完成

### 改动

1. 完成 LOG-TODO-019-D/B 收敛：
   - 更新 [docs/todos/infrastructure/deliverables/LOG-TODO-019-LogQueryService骨架收敛.md](docs/todos/infrastructure/deliverables/LOG-TODO-019-LogQueryService骨架收敛.md)，将状态从“D Gate Pass，Build 进行中”回写为“已完成”，补齐 Build 落地结果、定向/标签验收证据与结论。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md)，将 `LOG-TODO-019` 标记为 Done，并同步更新 Gate 快照、logging/integration/unit 测试计数、blocker 说明与下一步建议。
2. 完成 LOG-TODO-019-B 受控查询骨架落地：
   - 新增 [infra/src/logging/LogQueryService.h](infra/src/logging/LogQueryService.h) 与 [infra/src/logging/LogQueryService.cpp](infra/src/logging/LogQueryService.cpp)，收敛 `LogQueryRequest` / `LogQueryAccessContext` / `LogQueryResult`、internal `ILogQueryRecordReader` 与 local artifact 摘要生成逻辑。
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，将 `LogQueryService.cpp` 纳入 `dasall_infra`。
   - 新增 [tests/unit/infra/logging/LogQueryServiceTest.cpp](tests/unit/infra/logging/LogQueryServiceTest.cpp)，覆盖 request 形态非法、allow proof 缺失/非 Allow、`enable_diag_pull` gate、缺少 local record reader 与 trace selector 正例。
   - 新增 [tests/integration/infra/logging/LogQueryIntegrationTest.cpp](tests/integration/infra/logging/LogQueryIntegrationTest.cpp)，通过 `LoggingFacade` 富化 `trace_id` / `session_id` 后验证 trace/session 查询命中、`max_records` 截断与 local artifact 摘要字段。
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)、[tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)、[tests/integration/infra/logging/CMakeLists.txt](tests/integration/infra/logging/CMakeLists.txt) 与 [tests/integration/CMakeLists.txt](tests/integration/CMakeLists.txt)，把新增 unit/integration 目标纳入 `logging` 标签与顶层聚合目标。

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_log_query_service_unit_test dasall_log_query_integration_test dasall_unit_tests dasall_integration_tests`
   - `ctest --test-dir build-ci -N -R "(LogQueryServiceTest|LogQueryIntegrationTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(LogQueryServiceTest|LogQueryIntegrationTest)"`
   - `ctest --test-dir build-ci -N -L integration`
   - `ctest --test-dir build-ci --output-on-failure -L integration`
   - `ctest --test-dir build-ci -N -L logging`
   - `ctest --test-dir build-ci --output-on-failure -L logging`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
2. 结果：
   - `LogQueryServiceTest` 与 `LogQueryIntegrationTest` 定向发现 2 个，执行 2/2 通过。
   - integration 套件发现 10 个，执行 10/10 通过，其中 logging integration 3/3 通过。
   - logging 标签测试发现 26 个，执行 26/26 通过。
   - unit 套件 112/112 通过；全量发现更新为 254 个测试。

### 结果

1. `LogQueryService` 已从“边界冻结”推进到“精确 selector + allow proof 校验 + local artifact 摘要导出骨架已落盘并可测”。
2. 本轮没有新增 public query/export 接口，也没有把 remote export 或二次授权带回 logging 子域；当前 logging 专项 TODO 的 001~019 原子任务已全部完成。

### 下一步

1. 若继续推进 logging 子域，应新开围绕 retention、真实索引或运行时 wiring 的后续原子任务，而不是回退当前骨架边界。

### 风险

1. 本轮仍只提供 internal record reader + local artifact 摘要骨架，尚未实现真实运行时索引与 retention 清理；后续扩展必须继续保持 local-only、allow-proof-required 与 diagnostics remote export 分层边界。

## 记录 #073

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-TODO-017 实现 LoggingHealthProbe 健康探针骨架
- 状态：已完成

### 改动

1. 完成 LOG-TODO-017-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-TODO-017-LoggingHealthProbe骨架收敛.md](docs/todos/infrastructure/deliverables/LOG-TODO-017-LoggingHealthProbe骨架收敛.md)，补齐本地证据、Kubernetes probe 外部参考、Design->Build 映射与 D Gate 结果。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md)，将 `LOG-TODO-017` 标记为 Done，并同步更新 logging/unit 发现计数、Gate 快照与下一步建议。
2. 完成 LOG-TODO-017-B 健康探针骨架落地：
   - 新增 [infra/src/logging/LoggingHealthProbe.h](infra/src/logging/LoggingHealthProbe.h) 与 [infra/src/logging/LoggingHealthProbe.cpp](infra/src/logging/LoggingHealthProbe.cpp)，以 internal `ILoggingHealthSignalProvider` 收敛 queue 高水位、drop delta、recovery degraded/fallback、unrecoverable failure 与 metrics bridge degraded 等本地健康信号。
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，将 `LoggingHealthProbe.cpp` 纳入 `dasall_infra`。
   - 新增 [tests/unit/infra/logging/LoggingHealthProbeTest.cpp](tests/unit/infra/logging/LoggingHealthProbeTest.cpp)，覆盖 descriptor 冻结值、Healthy/Degraded/Unhealthy 三态映射与 timeout failure。
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt) 与 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)，把 `LoggingHealthProbeTest` 纳入 `unit;logging` 标签与 unit 聚合目标。

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_logging_health_probe_unit_test dasall_unit_tests`
   - `ctest --test-dir build-ci -N -R "LoggingHealthProbeTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "LoggingHealthProbeTest"`
   - `ctest --test-dir build-ci -N -L logging`
   - `ctest --test-dir build-ci --output-on-failure -L logging`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
2. 结果：
   - `LoggingHealthProbeTest` 定向发现 1 个，执行 1/1 通过。
   - logging 标签测试发现 24 个，执行 24/24 通过。
   - unit 套件 111/111 通过；全量发现更新为 252 个测试。

### 结果

1. `LoggingHealthProbe` 已从“仅有设计冻结”推进到“internal provider + frozen descriptor + 三态映射 + timeout failure 骨架已落盘并可测”。
2. 本轮没有新增 public health interface，也没有改动 contracts 映射；logging 专项当前剩余未完成原子任务收敛到 `LOG-TODO-019`。

### 下一步

1. 进入 `LOG-TODO-019`，实现 `LogQueryService` 的受控查询与本地 artifact 导出骨架。

### 风险

1. 本轮只完成 `LoggingHealthProbe` 的 internal provider 骨架与单测，还未把真实运行时 wiring 接到服务组合层；后续扩展必须继续沿用 `IHealthProbe` + internal provider 边界，不能回退到 logging 私有 health result。

## 记录 #072

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-BLK-005 LogQueryService 查询模型与权限边界解阻
- 状态：已完成

### 改动

1. 完成 LOG-BLK-005-D 设计解阻：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-BLK-005-LogQueryService设计收敛.md](docs/todos/infrastructure/deliverables/LOG-BLK-005-LogQueryService设计收敛.md)，把 blocker 根因收敛为“缺 query schema、allow 证明与本地 artifact 导出限制”，而不是否定 trace/session 诊断拉取能力本身。
   - 更新 [docs/architecture/DASALL_infra_logging模块详细设计.md](docs/architecture/DASALL_infra_logging模块详细设计.md)，新增 6.10.2，并补齐 `LogQueryService` 在 6.2/6.3/6.5/6.6 的子组件、对象与接口语义。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md)，将 LOG-BLK-005 标记为已解阻，并新增后续执行任务 `LOG-TODO-019`。
2. 同步修正专项 TODO 中与 integration/gate 快照相关的过期描述，确保 `LOG-GATE-06`、LOG-BLK-004 与下一步执行建议保持一致。

### 测试

1. 验证命令：
   - `grep -n "结构化日志抓取和按 trace/session 检索" docs/architecture/DASALL_架构设计文档.md`
   - `grep -n "IDiagnosticsPolicyGuard\|remote 默认关闭\|导出" docs/architecture/DASALL_infra_diagnostics模块详细设计.md`
   - `grep -n "LogQueryService\|LogQueryRequest\|LogQueryAccessContext\|diag://infra/logging/query\|LOG-TODO-019" docs/architecture/DASALL_infra_logging模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md docs/todos/infrastructure/deliverables/LOG-BLK-005-LogQueryService设计收敛.md`
2. 结果：
   - trace/session 诊断拉取的上层架构要求、diagnostics 的 policy/export 边界，以及 logging 侧 query schema/allow proof/local artifact 约束已可双向定位。
   - `LOG-TODO-019` 已具备明确的代码目标、测试目标与验收命令，不再依赖额外设计 blocker。

### 结果

1. `LogQueryService` 的粒度已从 L1 提升到 L2；后续实现只需围绕本地索引、artifact retention 与 allow/deny 路径落代码。
2. logging 子域继续保留按 trace/session 的受控诊断拉取能力，同时把 remote export、目标白名单与上传策略留在 diagnostics 子域，避免越权扩张。

### 下一步

1. 进入 `LOG-TODO-019`，实现 `LogQueryService` 的受控查询与本地 artifact 导出骨架。

### 风险

1. 本轮只冻结 `LogQueryService` 边界，尚未实现本地索引与 retention 清理；后续若实现试图直接返回原始记录容器、绕过 Policy Gate allow 证明或自行持有 remote export，应立即回退并重新审查。

## 记录 #071

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-TODO-018 落盘 logging integration 用例与标签注册
- 状态：已完成

### 改动

1. 完成 LOG-TODO-018-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-TODO-018-Logging集成用例收敛.md](docs/todos/infrastructure/deliverables/LOG-TODO-018-Logging%E9%9B%86%E6%88%90%E7%94%A8%E4%BE%8B%E6%94%B6%E6%95%9B.md)，把 logging integration 落点、标签与 Gate-06 关闭证据收敛为正式交付物。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，新增完成任务 `LOG-TODO-018`，并将 `LOG-GATE-06` 从 Blocked 更新为 Pass。
2. 完成 LOG-TODO-018-B 集成用例落地：
   - 新增 [tests/integration/infra/logging/CMakeLists.txt](tests/integration/infra/logging/CMakeLists.txt)，统一注册 `integration;logging` 标签。
   - 新增 [tests/integration/infra/logging/LoggingPipelineIntegrationTest.cpp](tests/integration/infra/logging/LoggingPipelineIntegrationTest.cpp)，覆盖主链写入成功与 block policy 回压失败路径。
   - 新增 [tests/integration/infra/logging/LoggingAuditLinkIntegrationTest.cpp](tests/integration/infra/logging/LoggingAuditLinkIntegrationTest.cpp)，覆盖 audit link 成功路由与不完整 ref 拒绝路径。
   - 更新 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt) 与 [tests/integration/CMakeLists.txt](tests/integration/CMakeLists.txt)，将两个 logging integration target 纳入顶层聚合入口。

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_logging_pipeline_integration_test dasall_logging_audit_link_integration_test dasall_integration_tests`
   - `ctest --test-dir build-ci -N -R "(LoggingPipelineIntegrationTest|LoggingAuditLinkIntegrationTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(LoggingPipelineIntegrationTest|LoggingAuditLinkIntegrationTest)"`
   - `ctest --test-dir build-ci -N -L integration`
   - `ctest --test-dir build-ci --output-on-failure -L integration`
2. 结果：
   - logging integration 用例发现 2 个，执行 2/2 通过。
   - 全量 integration 套件发现 9 个，执行 9/9 通过。
   - logging 组件现已具备 `integration;logging` 标签 discoverability，可与 unit/contract 标签面并行存在。

### 结果

1. `tests/integration/infra/logging/` 已从空目录变成正式的组件测试落点，`LOG-GATE-06` 可以关闭。
2. logging 子域现在同时具备 unit、contract、integration 三类测试发现面，后续只需在同一目录和标签面上扩展新的场景。

### 下一步

1. 进入 `LOG-BLK-005`，冻结 `LogQueryService` 的 query 对象、授权边界与导出约束。

### 风险

1. 当前 integration 用例只覆盖已落盘骨架的主链与 audit link，尚未覆盖 `LoggingHealthProbe` 或 `LogQueryService`；后续扩展不要把这些未实现能力伪装成“已通过集成门禁”。

## 记录 #070

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-BLK-003 LoggingHealthProbe 接口边界解阻
- 状态：已完成

### 改动

1. 完成 LOG-BLK-003-D 设计解阻：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-BLK-003-LoggingHealthProbe设计收敛.md](docs/todos/infrastructure/deliverables/LOG-BLK-003-LoggingHealthProbe%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，把 blocker 根因收敛为“logging 未把 health 通用 probe 契约映射成自身 descriptor/status 设计”，并冻结 `LoggingHealthProbe` 的 descriptor、输入信号、三态映射与 timeout 语义。
   - 更新 [docs/architecture/DASALL_infra_logging模块详细设计.md](docs/architecture/DASALL_infra_logging模块详细设计.md)，新增 6.10.1，明确 `LoggingHealthProbe` 直接实现 `IHealthProbe`，不再引入 logging 私有 health result。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 LOG-BLK-003 标记为已解阻，并新增后续执行任务 `LOG-TODO-017`。

### 测试

1. 验证命令：
   - `grep -n "IHealthProbe\|ProbeDescriptor\|ProbeResult\|timeout_ms" docs/architecture/DASALL_infra_health模块详细设计.md infra/include/health/IHealthProbe.h infra/include/health/ProbeTypes.h`
   - `grep -n "infra.logging.pipeline\|LoggingHealthProbe\|readiness\|unrecoverable_failure_total" docs/architecture/DASALL_infra_logging模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md docs/todos/infrastructure/deliverables/LOG-BLK-003-LoggingHealthProbe设计收敛.md`
2. 结果：
   - health 通用 probe 契约和 logging 侧 descriptor/status mapping 已能在文档与头文件中双向定位。
   - LOG-TODO-017 已具备可执行的代码目标、测试目标与验收命令，不再依赖额外 health blocker。

### 结果

1. `LoggingHealthProbe` 的接口边界已从 L1 提升到 L2，后续实现只需围绕本地状态 provider 与三态映射落代码，不需要再等待 health 子域补新对象。
2. `LOG-BLK-003` 已不再阻塞 logging 子域继续推进；下一轮可以直接进入 logging integration 用例，随后再处理 `LOG-BLK-005`。

### 下一步

1. 进入 logging integration 用例与标签注册任务，补齐 `tests/integration/infra/logging/` 并关闭 `LOG-GATE-06`。

### 风险

1. 本轮只冻结 `LoggingHealthProbe` 边界，尚未实现 state provider 与阈值逻辑；若后续实现试图绕开 `IHealthProbe` 重新定义私有结果对象，应立即回退并重新审查边界。

## 记录 #069

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-TODO-016 回写 logging 质量门与交付证据
- 状态：已完成

### 改动

1. 完成 LOG-TODO-016-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-TODO-016-LoggingGate回写收敛.md](docs/todos/infrastructure/deliverables/LOG-TODO-016-LoggingGate%E5%9B%9E%E5%86%99%E6%94%B6%E6%95%9B.md)，把 Gate-LOG-01~06 结论、blocker 快照、工具态异常与“未触发代码回退”统一收敛为正式交付物。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 LOG-TODO-016 标记为 Done，并新增 9.3/9.4/9.5 执行快照。
2. 更新专项 TODO 尾部建议：
   - 将 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md) 11.5 从“先执行 001~011、014~016”改为“001~016 已完成，后续转入 integration/health/log query 的下一轮拆解”，消除过期执行指引。

### 测试

1. 验收命令：
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - CTest 全量发现 249 个测试。
   - `unit` 套件 110/110 通过。
   - `contract` 套件 132/132 通过。
   - `tests/integration/infra/logging/` 仍无用例文件，因此 Gate-LOG-06 明确保持 Blocked。

### 结果

1. logging 专项 TODO 已具备可评审的 gate/blocker 当前态，不再需要从多轮 worklog 和提交历史中人工拼接质量门结论。
2. 014~016 的执行状态已经统一封口：构建接线完成、测试注册完成、gate 与 blocker 状态完成回写，且 remote `origin/master` 与本地一致。

### 下一步

1. 若继续推进 logging 子域，应优先围绕 `LOG-BLK-003`、`LOG-BLK-005` 和 logging integration 用例生成新一轮任务，而不是重复打开 014~016。

### 风险

1. 由于 `tests/integration/infra/logging/` 仍为空，任何声称 logging 组件已通过 integration gate 的结论都应视为不成立，直到组件级 integration 用例实际落盘并纳入标签注册。

## 记录 #068

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-TODO-015 注册 logging 单元与契约测试入口
- 状态：已完成

### 改动

1. 完成 LOG-TODO-015-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-TODO-015-Logging测试注册收敛.md](docs/todos/infrastructure/deliverables/LOG-TODO-015-Logging%E6%B5%8B%E8%AF%95%E6%B3%A8%E5%86%8C%E6%94%B6%E6%95%9B.md)，将 logging 测试从“分散落点”收敛为“显式 target 分组 + 统一 discoverability 标签”。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 LOG-TODO-015 标记为 Done，并补齐 `ctest -N -L logging` 与 `ctest -L logging` 作为发现性验收证据。
2. 完成 LOG-TODO-015-B 测试注册收敛：
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)，新增 `DASALL_LOGGING_UNIT_TEST_EXECUTABLE_TARGETS`，把 logging 组件的 unit 目标显式归组到顶层 unit 列表中。
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)，新增 `dasall_register_logging_unit_test(...)` 并统一 `unit;logging` 标签。
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)，新增 `dasall_register_logging_contract_test(...)` 并统一 `contract;smoke;logging` 标签。

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci -N -L logging`
   - `ctest --test-dir build-ci --output-on-failure -L logging`
2. 结果：
   - 聚合 `unit` 套件 110/110 通过。
   - 聚合 `contract` 套件 132/132 通过。
   - `ctest -N -L logging` 发现 21 个 logging 标签测试。
   - `ctest -L logging` 执行 21/21 通过，其中 unit 12 个、contract 9 个。

### 结果

1. logging 组件首次具备独立的测试发现面，后续可以直接用 `ctest -L logging` 做组件级回归，而不必从全量 unit/contract 输出里手工筛选。
2. logging 相关 unit/contract 入口已经形成统一注册模式，后续追加 health/integration/log query 测试时可以沿用相同结构继续扩展。

### 下一步

1. 进入 LOG-TODO-016，统一回写 Gate-LOG-01~06、已解阻 blocker 和实际验收链路。

### 风险

1. `logging` 标签目前覆盖 unit 与 smoke contract，但 integration 用例尚未落盘，因此 `LOG-GATE-06` 仍需在下一轮文档回写中保持未通过或受限说明。

## 记录 #067

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-TODO-014 注册 logging 构建落点到 infra CMake
- 状态：已完成

### 改动

1. 完成 LOG-TODO-014-D/B 收敛：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-TODO-014-Logging构建接线收敛.md](docs/todos/infrastructure/deliverables/LOG-TODO-014-Logging%E6%9E%84%E5%BB%BA%E6%8E%A5%E7%BA%BF%E6%94%B6%E6%95%9B.md)，明确 logging skeleton 必须成为 `dasall_infra` 正式源码，而不是继续由测试目标各自直编一份实现副本。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 LOG-TODO-014 标记为 Done，并补齐显式 build/discovery/test 验收链路。
2. 完成 LOG-TODO-014-B 构建接线：
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，新增 `DASALL_INFRA_LOGGING_SOURCES` 并把 `AsyncQueueController.cpp`、`AuditLinkAdapter.cpp`、`LoggingConfigAdapter.cpp`、`LoggingFacade.cpp`、`LoggingMetricsBridge.cpp`、`LoggingRecovery.cpp`、`SinkDispatcher.cpp` 接入 `dasall_infra`。
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt) 与 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)，移除 logging 测试目标对同一批 logging `.cpp` 的重复编译，保留 internal header include path 并统一改为链接 `dasall_infra`。

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_logging_facade_unit_test dasall_sink_dispatcher_unit_test dasall_async_queue_controller_unit_test dasall_audit_link_adapter_unit_test dasall_logging_recovery_unit_test dasall_logging_config_merge_unit_test dasall_logging_metrics_bridge_unit_test dasall_contract_sink_dispatcher_boundary_test dasall_contract_audit_link_adapter_boundary_test dasall_contract_log_configurator_boundary_test dasall_contract_logging_metrics_bridge_boundary_test`
   - `ctest --test-dir build-ci -N -R "(LoggingFacadeTest|SinkDispatcherTest|AsyncQueueControllerTest|AuditLinkAdapterTest|LoggingRecoveryTest|LoggingConfigMergeTest|LoggingMetricsBridgeTest|SinkDispatcherBoundaryContractTest|AuditLinkAdapterBoundaryContractTest|LogConfiguratorBoundaryContractTest|LoggingMetricsBridgeBoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(LoggingFacadeTest|SinkDispatcherTest|AsyncQueueControllerTest|AuditLinkAdapterTest|LoggingRecoveryTest|LoggingConfigMergeTest|LoggingMetricsBridgeTest|SinkDispatcherBoundaryContractTest|AuditLinkAdapterBoundaryContractTest|LogConfiguratorBoundaryContractTest|LoggingMetricsBridgeBoundaryContractTest)"`
2. 结果：
   - `dasall_infra` 与 11 个受影响的 logging unit/contract 目标均可成功构建和链接。
   - CTest 可发现 11 个受影响测试，且 11/11 全部通过。
   - `Build_CMakeTools` / `RunCtest_CMakeTools` 仍报“无法配置项目”，本轮实际验收继续使用仓库既有显式 CMake/CTest 链路。

### 结果

1. logging 运行时骨架首次成为 `dasall_infra` 的正式构建产物，后续主链接线不再依赖测试目标临时拼装实现。
2. unit/contract 目标与主库源码列表已解耦成单一真实来源，后续可以在不引入重复定义风险的前提下继续做测试注册与 gate 收口。

### 下一步

1. 进入 LOG-TODO-015，收敛 logging unit/contract 测试注册和 discoverability 标签。

### 风险

1. 当前 CMake Tools 仍无法返回可用 target/test，后续门禁文档需要明确“IDE 工具态异常不等于仓库构建失败”，并保留显式 cmake/ctest 作为实际验收证据。

## 记录 #066

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-TODO-013 实现 LoggingMetricsBridge 指标桥接骨架
- 状态：已完成

### 改动

1. 完成 LOG-TODO-013-B 代码落地：
   - 新增 [infra/src/logging/LoggingMetricsBridge.h](infra/src/logging/LoggingMetricsBridge.h)
   - 新增 [infra/src/logging/LoggingMetricsBridge.cpp](infra/src/logging/LoggingMetricsBridge.cpp)
   - 新增 [tests/unit/infra/logging/LoggingMetricsBridgeTest.cpp](tests/unit/infra/logging/LoggingMetricsBridgeTest.cpp)
   - 新增 [tests/contract/smoke/LoggingMetricsBridgeBoundaryContractTest.cpp](tests/contract/smoke/LoggingMetricsBridgeBoundaryContractTest.cpp)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
2. 完成 LOG-TODO-013-D/B 证据收口：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-TODO-013-LoggingMetricsBridge设计收敛.md](docs/todos/infrastructure/deliverables/LOG-TODO-013-LoggingMetricsBridge%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，把 bridge skeleton 的 provider/meter/sample 入口、本地白名单校验与 non-recursive failure 结果对象收敛为正式交付物。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 LOG-TODO-013 标记为 Done，并补齐定向/聚合验证证据。

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_logging_metrics_bridge_unit_test dasall_contract_logging_metrics_bridge_boundary_test`
   - `ctest --test-dir build-ci -N -R "(LoggingMetricsBridgeTest|LoggingMetricsBridgeBoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(LoggingMetricsBridgeTest|LoggingMetricsBridgeBoundaryContractTest)"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - 定向目标构建通过，CTest 发现 2 个新增测试。
   - `LoggingMetricsBridgeTest` 与 `LoggingMetricsBridgeBoundaryContractTest` 全部通过。
   - 聚合 `unit` 套件 110/110 通过。
   - 聚合 `contract` 套件 132/132 通过。

### 结果

1. logging 组件已具备最小 `LoggingMetricsBridge` skeleton，可以在不依赖 metrics runtime/exporter 实现的前提下，通过 provider/meter/sample 边界稳定发射五个 frozen metric family。
2. bridge failure 已被收敛到 `MetricsErrorCode` + `MetricsOperationStatus`，并通过 local degraded/no-op 语义阻止 metrics 失败递归反噬 logging 主链。

### 下一步

1. 若继续按专项 TODO 推进，可进入 LOG-TODO-014 或 LOG-TODO-015，完成 logging 源码与测试注册的构建接线收口。

### 风险

1. 本轮只完成 bridge skeleton，尚未把 bridge 接到 LoggingFacade / SinkDispatcher 主链，也未接入 `dasall_infra` 静态库源码列表；后续 wiring 任务需要显式接线，不能默认假定主链已自动产出 metrics。

## 记录 #065

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-BLK-002 metrics 接口冻结解阻
- 状态：已完成

### 改动

1. 完成 LOG-BLK-002-D 设计解阻：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-BLK-002-LoggingMetricsBridge设计收敛.md](docs/todos/infrastructure/deliverables/LOG-BLK-002-LoggingMetricsBridge%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，把 blocker 根因收敛为“跨模块桥接协议未成文”，并冻结 provider/meter/sample 唯一路径、五指标对象表、MetricLabels 取值规则与 non-recursive failure 语义。
   - 更新 [docs/architecture/DASALL_infra_metrics模块详细设计.md](docs/architecture/DASALL_infra_metrics%E6%A8%A1%E5%9D%97%E8%AF%A6%E7%BB%86%E8%AE%BE%E8%AE%A1.md)，新增 6.6.1 与 6.8.1，明确 logging 只能通过 IMetricsProvider/IMeter 发射指标，且 record 失败不得递归反噬 logging 主链。
   - 更新 [docs/architecture/DASALL_infra_logging模块详细设计.md](docs/architecture/DASALL_infra_logging%E6%A8%A1%E5%9D%97%E8%AF%A6%E7%BB%86%E8%AE%BE%E8%AE%A1.md) 6.10，把 LoggingMetricsBridge 的五指标、标签规则和失败语义回链到 metrics 侧冻结结论。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 LOG-BLK-002 标记为已解阻，并把 LOG-TODO-013 从 Blocked 迁移到 Not Started，同时把测试出口收敛为可执行的 unit/contract 边界验证。

### 测试

1. 验证命令：
   - `grep -n "6.6.1 跨模块指标桥接协议\|6.8.1 logging 指标桥接失败语义\|logging_write_total\|logging_flush_latency_ms" docs/architecture/DASALL_infra_metrics模块详细设计.md docs/architecture/DASALL_infra_logging模块详细设计.md docs/todos/infrastructure/deliverables/LOG-BLK-002-LoggingMetricsBridge设计收敛.md`
   - `grep -n "LOG-BLK-002\|LOG-TODO-013\|Not Started\|已解阻" docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md`
2. 结果：
   - metrics 设计、logging 设计、交付物与专项 TODO 均已能定位到 provider/meter/sample 接入协议、标签治理与 non-recursive failure 语义。
   - LOG-TODO-013 已具备可执行的代码目标、测试目标与验收命令，不再依赖额外 metrics blocker。

### 结果

1. LOG-BLK-002 已从“metrics 接口未冻结”转为已解阻，logging bridge skeleton 可以直接复用现有 metrics public headers 推进实现。
2. LOG-TODO-013 的最小粒度已从 L1 收敛到 L2，后续实现只需关注 bridge skeleton 与定向 unit/contract 验证，不必等待 metrics runtime/exporter 先落盘。

### 下一步

1. 直接进入 LOG-TODO-013，实现 LoggingMetricsBridge 骨架、测试与验收回写。

### 风险

1. metrics 运行时实现仍为空，因此 LOG-TODO-013 只能先以接口驱动的 fake provider/meter 测试收敛桥接边界；真实 exporter 联通需由 metrics 子域后续任务继续承接。

## 记录 #064

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-TODO-012 实现 LoggingConfigAdapter 四层配置适配
- 状态：已完成

### 改动

1. 完成 LOG-TODO-012-D 设计收敛：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-TODO-012-LoggingConfigAdapter设计收敛.md](docs/todos/infrastructure/deliverables/LOG-TODO-012-LoggingConfigAdapter%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，明确 `ILogConfigurator` 只暴露 `LoggingConfig`/`LoggingConfigApplyResult`，`LoggingConfigAdapter` 只消费 ConfigCenter active typed config 并执行本地 key 接受规则。
   - 把原任务行中过弱的验收命令升级为“显式构建新增 unit/contract 目标 + CTest 发现性 + 聚合 unit/contract”的完整闭环，作为最小 validation blocker fix。
2. 完成 LOG-TODO-012-B 代码落地：
   - 新增 [infra/include/logging/ILogConfigurator.h](infra/include/logging/ILogConfigurator.h)
   - 新增 [infra/src/logging/LoggingConfigAdapter.h](infra/src/logging/LoggingConfigAdapter.h)
   - 新增 [infra/src/logging/LoggingConfigAdapter.cpp](infra/src/logging/LoggingConfigAdapter.cpp)
   - 新增 [tests/unit/infra/logging/LoggingConfigMergeTest.cpp](tests/unit/infra/logging/LoggingConfigMergeTest.cpp)
   - 新增 [tests/contract/smoke/LogConfiguratorBoundaryContractTest.cpp](tests/contract/smoke/LogConfiguratorBoundaryContractTest.cpp)
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_logging_config_merge_unit_test dasall_contract_log_configurator_boundary_test`
   - `ctest --test-dir build-ci -N -R "(LoggingConfigMergeTest|LogConfiguratorBoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(LoggingConfigMergeTest|LogConfiguratorBoundaryContractTest)"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - 定向目标构建通过，CTest 发现 2 个新增测试。
   - `LoggingConfigMergeTest` 与 `LogConfiguratorBoundaryContractTest` 全部通过。
   - 聚合 `unit` 套件 109/109 通过。
   - 聚合 `contract` 套件 131/131 通过。

### 结果

1. logging 组件已具备最小 public config surface：`ILogConfigurator`、`LoggingConfig` 与 `LoggingConfigApplyResult` 可以稳定承接四层 active config。
2. `LoggingConfigAdapter` 已复用 ConfigCenter typed config，并在 logging 本地固化 runtime tunable 白名单、per-key source acceptance 与 `infra.audit.required` 审计主链保护。

### 下一步

1. 若继续按专项 TODO 推进，后继可进入 LOG-TODO-013 或 LOG-TODO-014/015，具体取决于是否优先做 bridge 还是构建/测试接线收口。

### 风险

1. `LoggingConfigAdapter` 当前不订阅 ConfigChanged 事件；若后续需要自动刷新，应在现有 config surface 之外追加 bridge，不要回退到 logging 私有 patch 模型。

## 记录 #063

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-BLK-001 logging config 模型解阻
- 状态：已完成

### 改动

1. 完成 LOG-BLK-001 设计解阻：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-BLK-001-LoggingConfig设计收敛.md](docs/todos/infrastructure/deliverables/LOG-BLK-001-LoggingConfig%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，把 `ILogConfigurator` 的输入对象、结果对象、frozen key set 与 per-key 层级接受规则收敛为正式设计证据。
   - 在 [docs/architecture/DASALL_infra_logging模块详细设计.md](docs/architecture/DASALL_infra_logging%E6%A8%A1%E5%9D%97%E8%AF%A6%E7%BB%86%E8%AE%BE%E8%AE%A1.md) 6.6/6.9 补齐 LoggingConfig/LoggingConfigApplyResult 对象表、`infra.logging.*` 命名空间、runtime override 白名单与 `infra.audit.required` 准入门。
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)，将 LOG-BLK-001 标记为已解阻，并将 LOG-TODO-012 从 Blocked 迁移到 Not Started。

### 测试

1. 验证命令：
   - `grep -n "ILogConfigurator\|infra.logging.level\|infra.audit.required" docs/architecture/DASALL_infra_logging模块详细设计.md docs/todos/infrastructure/deliverables/LOG-BLK-001-LoggingConfig设计收敛.md`
   - `grep -n "LOG-BLK-001\|LOG-TODO-012" docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md`
2. 结果：
   - 设计文档、交付物与 TODO 回写均可定位到新增的 LoggingConfig 对象表、键域冻结与解阻状态。

### 结果

1. LOG-TODO-012 已从 Blocked 转为 Not Started，后续实现可以直接复用 ConfigCenter typed config，而无需再发明 logging 私有 patch 模型。
2. logging 配置键域已与 infra/config 对齐到 `infra.logging.*`，并明确 `infra.audit.required` 不可被 profile/deployment/runtime 配置关闭。

### 下一步

1. 直接进入 LOG-TODO-012，落 ILogConfigurator + LoggingConfigAdapter 骨架、unit/contract 测试和验收命令。

### 风险

1. 若后续 infra/config 的 key 域或 ConfigSourceKind 契约回退，logging 本地的 per-key 接受规则需要同步 review，否则 LOG-BLK-001 应重新挂起。

## 记录 #062

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-TODO-011 实现 LoggingRecovery 故障降级骨架
- 状态：已完成

### 改动

1. 完成 LOG-TODO-011-D 设计收敛：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-TODO-011-LoggingRecovery设计收敛.md](docs/todos/infrastructure/deliverables/LOG-TODO-011-LoggingRecovery%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，把 6.8 的 sink IO/format failure/fallback/retry 约束收敛为内部恢复骨架。
   - 将原 blocker“失败注入桩不足”最小化为 internal `ILogRecoverySink` 注入接口，避免真实 IO 成为单测前提。
   - 明确 Design -> Build 映射：内部 sink 接口 + degraded 状态机 + failure-injection 单测，不越界到真实 audit/health bridge。
2. 完成 LOG-TODO-011-B 代码落地：
   - 新增 [infra/src/logging/LoggingRecovery.h](infra/src/logging/LoggingRecovery.h)
   - 新增 [infra/src/logging/LoggingRecovery.cpp](infra/src/logging/LoggingRecovery.cpp)
   - 新增 [tests/unit/infra/logging/LoggingRecoveryTest.cpp](tests/unit/infra/logging/LoggingRecoveryTest.cpp)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_logging_recovery_unit_test`
   - `ctest --test-dir build-ci -N -R "LoggingRecoveryTest"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
2. 结果：
   - `cmake -S . -B build-ci -G "Unix Makefiles"` 通过。
   - `cmake --build build-ci --target dasall_logging_recovery_unit_test` 通过。
   - `ctest --test-dir build-ci -N -R "LoggingRecoveryTest"` 通过，发现 1 个测试。
   - `cmake --build build-ci --target dasall_unit_tests` 通过，108/108 unit tests passed。
   - `ctest --test-dir build-ci --output-on-failure -L unit` 通过，108/108 unit tests passed。

### 结果

1. logging 组件已经具备最小可测的故障降级状态机，后续真实 sink adapter 可以直接挂到 `ILogRecoverySink` 注入点而不重写恢复判定。
2. sink IO、format failure、retry success、retry failure 四条路径都进入 unit failure-injection 覆盖面，为后续 health/metrics bridge 留出稳定入口。

### 下一步

1. 若继续按专项 TODO 推进，直接后继应进入 LOG-TODO-014/015 的构建与测试接线，或在解阻后再进入 LOG-TODO-012/013。

### 风险

1. `LoggingRecovery` 当前只保留 internal state 与 fallback 路径，不接入真实 recovery 审计或健康探针；后续扩展应走 adapter/bridge，不要把跨子系统逻辑压回恢复骨架。

## 记录 #061

- 日期：2026-04-03
- 阶段：logging 组件专项 TODO
- 任务：LOG-TODO-010 定义 LoggingErrors 错误码域
- 状态：已完成

### 改动

1. 完成 LOG-TODO-010-D 设计收敛：
   - 新增 [docs/todos/infrastructure/deliverables/LOG-TODO-010-LoggingErrors设计收敛.md](docs/todos/infrastructure/deliverables/LOG-TODO-010-LoggingErrors%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，把 6.6/6.8 中分散的 queue full、sink IO、format invalid、config invalid 收敛为四个冻结 `LOG_E_*` 私有错误码。
   - 在设计文档中补齐 logging 私有码到 `contracts::ResultCode` 的映射矩阵，解决“与 contracts 映射矩阵未成文”的 context blocker。
   - 对齐仓库既有模式，明确 010 采用 header-only 子域错误码，不扩张共享 contracts 枚举，也不提前把 logging 错误合并到 `InfraErrorCode`。
2. 完成 LOG-TODO-010-B 代码落地：
   - 新增 [infra/include/logging/LoggingErrors.h](infra/include/logging/LoggingErrors.h)
   - 新增 [tests/unit/infra/LoggingErrorsTest.cpp](tests/unit/infra/LoggingErrorsTest.cpp)
   - 新增 [tests/contract/smoke/LoggingErrorsBoundaryContractTest.cpp](tests/contract/smoke/LoggingErrorsBoundaryContractTest.cpp)
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_logging_errors_unit_test dasall_contract_logging_errors_boundary_test`
   - `ctest --test-dir build-ci -N -R "LoggingErrorsTest|LoggingErrorsBoundaryContractTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "LoggingErrorsTest|LoggingErrorsBoundaryContractTest"`
2. 结果：
   - `cmake -S . -B build-ci -G "Unix Makefiles"` 通过。
   - `cmake --build build-ci --target dasall_logging_errors_unit_test dasall_contract_logging_errors_boundary_test` 通过。
   - `ctest --test-dir build-ci -N -R "LoggingErrorsTest|LoggingErrorsBoundaryContractTest"` 通过，发现 2 个测试。
   - `ctest --test-dir build-ci --output-on-failure -R "LoggingErrorsTest|LoggingErrorsBoundaryContractTest"` 通过，2/2 tests passed。

### 结果

1. logging 组件已具备独立、可追溯的私有错误码域，后续 011 恢复骨架可以直接复用统一的错误语义而不再散落使用通用 contracts 码值。
2. 四个错误码的名字、数值、来源锚点和一级 contracts 映射已经进入 unit/contract 测试保护面。

### 下一步

1. 按专项 TODO 的串行顺序推进 LOG-TODO-011，把 sink IO/format 异常恢复骨架切到 LoggingErrors 与可注入 failure path 上。

### 风险

1. `LOG_E_CONFIG_INVALID` 当前只冻结到 validation 类别；若 012 后续要求更细粒度配置差异，只能通过 reason 或配置诊断对象扩展，不能直接改写 010 的码名和映射。

## 记录 #060

- 日期：2026-04-01
- 阶段：infra/plugin 组件专项 TODO
- 任务：PLG-TODO-004 新增 IPluginPolicyGate 接口
- 状态：已完成

### 改动

1. 完成 PLG-TODO-004-D 设计收敛：
   - 新增 [docs/todos/infrastructure/PLG-TODO-004-IPluginPolicyGate设计收敛.md](docs/todos/infrastructure/PLG-TODO-004-IPluginPolicyGate%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，把 manifest 输入缺口收敛为 PluginPolicyRequest，并记录 evaluate(request, policy_snapshot) 的签名收敛结论。
   - 对原任务验收命令做最小 blocker-fix：显式构建新增 unit/contract 测试目标，避免 CTest 在未生成可执行文件时误判失败。
2. 完成 PLG-TODO-004-B 代码落地：
   - 新增 [infra/include/plugin/IPluginPolicyGate.h](infra/include/plugin/IPluginPolicyGate.h)
   - 新增 [tests/unit/infra/plugin/PluginPolicyGateInterfaceTest.cpp](tests/unit/infra/plugin/PluginPolicyGateInterfaceTest.cpp)
   - 新增 [tests/contract/smoke/PluginPolicyGateBoundaryContractTest.cpp](tests/contract/smoke/PluginPolicyGateBoundaryContractTest.cpp)
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_plugin%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci --target dasall_infra dasall_plugin_policy_gate_interface_unit_test dasall_contract_plugin_policy_gate_boundary_test`
   - `ctest --test-dir build-ci -N -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest"`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci --target dasall_infra dasall_plugin_policy_gate_interface_unit_test dasall_contract_plugin_policy_gate_boundary_test` 通过。
   - `ctest --test-dir build-ci -N -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest"` 通过，发现 2 个测试。
   - `ctest --test-dir build-ci --output-on-failure -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest"` 通过，2/2 tests passed。

### 结果

1. IPluginPolicyGate 已以最小 request + PolicyDecisionRef 形式落盘，后续 validation pipeline 可以直接复用统一的准入判定边界。
2. 本轮先修复了 manifest 输入对象仍未冻结的 blocker，再落到接口与定向 unit/contract 测试，未越界到 Manifest/PolicyBundle 的完整对象冻结或策略引擎实现。

### 下一步

1. Phase 2 的两个核心接口冻结任务已经完成；若继续按专项 TODO 推进，直接后继应进入 PLG-TODO-005/006 或 Phase 3 接线/基线完善任务。

### 风险

1. PluginPolicyRequest 当前只承接 descriptor、manifest_ref、profile_id；待 PluginManifest 解阻后，如果策略判定需要 richer manifest 视图，应通过增量 request 扩展承接，而不是替换现有接口边界。

## 记录 #059

- 日期：2026-04-01
- 阶段：infra/plugin 组件专项 TODO
- 任务：PLG-TODO-003 新增 IPluginManager 接口与骨架实现
- 状态：已完成

### 改动

1. 完成 PLG-TODO-003-D 设计收敛：
   - 新增 [docs/todos/infrastructure/PLG-TODO-003-IPluginManager设计收敛.md](docs/todos/infrastructure/PLG-TODO-003-IPluginManager%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，把 ValidationResult/LoadOptions/UnloadResult/ActivePluginSet 的缺口收敛为六个最小边界对象，并记录 discover/profile 与 load/load_options 的签名收敛结论。
   - 对原任务验收命令做最小 blocker-fix：显式构建新增 unit/contract 测试目标，避免 CTest 在未生成可执行文件时误判失败。
2. 完成 PLG-TODO-003-B 代码落地：
   - 新增 [infra/include/plugin/IPluginManager.h](infra/include/plugin/IPluginManager.h)
   - 新增 [infra/src/plugin/PluginManager.cpp](infra/src/plugin/PluginManager.cpp)
   - 新增 [tests/unit/infra/plugin/PluginManagerInterfaceTest.cpp](tests/unit/infra/plugin/PluginManagerInterfaceTest.cpp)
   - 新增 [tests/contract/smoke/PluginManagerBoundaryContractTest.cpp](tests/contract/smoke/PluginManagerBoundaryContractTest.cpp)
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_plugin%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci --target dasall_infra dasall_plugin_manager_interface_unit_test dasall_contract_plugin_manager_boundary_test`
   - `ctest --test-dir build-ci -N -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest"`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci --target dasall_infra dasall_plugin_manager_interface_unit_test dasall_contract_plugin_manager_boundary_test` 通过。
   - `ctest --test-dir build-ci -N -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest"` 通过，发现 2 个测试。
   - `ctest --test-dir build-ci --output-on-failure -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest"` 通过，2/2 tests passed。

### 结果

1. IPluginManager 已以最小 request/result + skeleton 形式落盘，后续 validation pipeline、lifecycle manager 和 audit adapter 可以直接复用统一的管理器边界。
2. 本轮先修复了接口边界对象缺失与签名粒度不一致的 context blocker，再落到接口、skeleton 与定向 unit/contract 测试，未越界到 Manifest/Signature/Compatibility 的完整对象冻结。

### 下一步

1. 继续推进 PLG-TODO-004，冻结 IPluginPolicyGate 接口，并与本轮的 PolicyDecisionRef / profile-aware 边界保持一致。

### 风险

1. validate 当前只冻结 manifest_ref、signature_report_ref、compatibility_report_ref 三个 ref 锚点；待 INF-BLK-09 解阻后，如果需要 richer report 对象，应通过增量对象承接而不是替换现有接口边界。

## 记录 #058

- 日期：2026-04-01
- 阶段：infra/plugin 组件专项 TODO
- 任务：PLG-TODO-007 定义 plugin 私有错误码域
- 状态：已完成

### 改动

1. 完成 PLG-TODO-007-D 设计收敛：
   - 新增 [docs/todos/infrastructure/PLG-TODO-007-PluginErrorCode设计收敛.md](docs/todos/infrastructure/PLG-TODO-007-PluginErrorCode%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，将 6.6 的 validate/load 锚点与 6.8/9.1 的失败类别收敛为六个冻结 `INF_E_PLUGIN_*` 码名，并给出 blocker 修复说明、Design->Build 映射与 D Gate。
   - 对原任务验收命令做最小 blocker-fix：显式构建新增 unit/contract 测试目标，避免 CTest 在未生成可执行文件时误判失败。
2. 完成 PLG-TODO-007-B 代码落地：
   - 新增 [infra/include/plugin/PluginErrorCode.h](infra/include/plugin/PluginErrorCode.h)
   - 新增 [tests/unit/infra/plugin/PluginErrorCodeTest.cpp](tests/unit/infra/plugin/PluginErrorCodeTest.cpp)
   - 新增 [tests/contract/smoke/PluginErrorCodeBoundaryContractTest.cpp](tests/contract/smoke/PluginErrorCodeBoundaryContractTest.cpp)
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_plugin%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci --target dasall_infra dasall_plugin_error_code_unit_test dasall_contract_plugin_error_code_boundary_test`
   - `ctest --test-dir build-ci -N -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest"`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci --target dasall_infra dasall_plugin_error_code_unit_test dasall_contract_plugin_error_code_boundary_test` 通过。
   - `ctest --test-dir build-ci -N -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest"` 通过，发现 2 个测试。
   - `ctest --test-dir build-ci --output-on-failure -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest"` 通过，2/2 tests passed。

### 结果

1. plugin 私有错误码域已以最小 header-only 形式落盘，后续 validation/lifecycle/audit 任务可直接复用统一的 `INF_E_PLUGIN_*` 名称与一级 contracts 映射。
2. 本轮先完成了“六个错误码名未完整冻结”的 blocker 修复，再落到代码与测试，未越界扩张到签名链、ABI 规则或 facade 实现。

### 下一步

1. Phase 1 的三个基础对象冻结任务已经完成；若继续按专项 TODO 推进，下一个直接后继应进入 PLG-TODO-003/004 或 Phase 3 接线任务。

### 风险

1. `SIGNATURE_FAIL` 与 `COMPATIBILITY_FAIL` 目前只冻结到一级 contracts 映射；待 INF-BLK-09 解阻后，若需要更细粒度语义，应通过增量设计承接而非替换现有码名。

## 记录 #057

- 日期：2026-04-01
- 阶段：infra/plugin 组件专项 TODO
- 任务：PLG-TODO-002 定义 PluginCatalog 数据结构
- 状态：已完成

### 改动

1. 完成 PLG-TODO-002-D 设计收敛：
   - 新增 [docs/todos/infrastructure/PLG-TODO-002-PluginCatalog设计收敛.md](docs/todos/infrastructure/PLG-TODO-002-PluginCatalog%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，固化 discovered/rejected 双集合、RejectedPluginRecord、Design->Build 映射与 D Gate。
   - 对原任务验收命令做最小 blocker-fix：显式构建新增 unit/contract 测试目标，避免 CTest 在未生成可执行文件时误判失败。
2. 完成 PLG-TODO-002-B 代码落地：
   - 新增 [infra/include/plugin/PluginCatalog.h](infra/include/plugin/PluginCatalog.h)
   - 新增 [tests/unit/infra/plugin/PluginCatalogTest.cpp](tests/unit/infra/plugin/PluginCatalogTest.cpp)
   - 新增 [tests/contract/smoke/PluginCatalogBoundaryContractTest.cpp](tests/contract/smoke/PluginCatalogBoundaryContractTest.cpp)
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_plugin%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci --target dasall_infra dasall_plugin_catalog_unit_test dasall_contract_plugin_catalog_boundary_test`
   - `ctest --test-dir build-ci -N -R "PluginCatalogTest|PluginCatalogBoundaryContractTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "PluginCatalogTest|PluginCatalogBoundaryContractTest"`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci --target dasall_infra dasall_plugin_catalog_unit_test dasall_contract_plugin_catalog_boundary_test` 通过。
   - `ctest --test-dir build-ci -N -R "PluginCatalogTest|PluginCatalogBoundaryContractTest"` 通过，发现 2 个测试。
   - `ctest --test-dir build-ci --output-on-failure -R "PluginCatalogTest|PluginCatalogBoundaryContractTest"` 通过，2/2 tests passed。

### 结果

1. PluginCatalog 已以最小 discovery result 对象落盘，后续 discover() 和 validation pipeline 可以直接复用统一的发现/拒绝聚合结构。
2. 本轮仅冻结 discovery result 及其 evidence_ref 对齐约束，不提前扩张到 validation report、load result 或 active set。

### 下一步

1. 继续推进 PLG-TODO-007，定义 plugin 私有错误码域。

### 风险

1. 当前 rejected_plugins 仅冻结 reason_code/evidence_ref 两个追踪锚点；若后续设计要求承载 richer report 引用，应以增量字段方式追加，避免破坏现有 catalog 契约。

## 记录 #056

- 日期：2026-04-01
- 阶段：infra/plugin 组件专项 TODO
- 任务：PLG-TODO-001 定义 PluginDescriptor 数据结构
- 状态：已完成

### 改动

1. 完成 PLG-TODO-001-D 设计收敛：
   - 新增 [docs/todos/infrastructure/PLG-TODO-001-PluginDescriptor设计收敛.md](docs/todos/infrastructure/PLG-TODO-001-PluginDescriptor%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，固化字段集合、unknown 归一化、Design->Build 映射与 D Gate。
   - 对原任务验收命令做最小 blocker-fix：显式构建新增 unit/contract 测试目标，避免 CTest 在未生成可执行文件时误判失败。
2. 完成 PLG-TODO-001-B 代码落地：
   - 新增 [infra/include/plugin/PluginDescriptor.h](infra/include/plugin/PluginDescriptor.h)
   - 新增 [tests/unit/infra/plugin/PluginDescriptorTest.cpp](tests/unit/infra/plugin/PluginDescriptorTest.cpp)
   - 新增 [tests/contract/smoke/PluginDescriptorBoundaryContractTest.cpp](tests/contract/smoke/PluginDescriptorBoundaryContractTest.cpp)
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_plugin%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci --target dasall_infra dasall_plugin_descriptor_unit_test dasall_contract_plugin_descriptor_boundary_test`
   - `ctest --test-dir build-ci -N -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest"`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci --target dasall_infra dasall_plugin_descriptor_unit_test dasall_contract_plugin_descriptor_boundary_test` 通过。
   - `ctest --test-dir build-ci -N -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest"` 通过，发现 2 个测试。
   - `ctest --test-dir build-ci --output-on-failure -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest"` 通过，2/2 tests passed。

### 结果

1. PluginDescriptor 已以最小 header-only 治理对象落盘，后续 PluginCatalog、IPluginManager 和 ValidationPipeline 可以复用统一字段与 unknown 归一化规则。
2. 本轮仅冻结 PluginDescriptor 字段与边界测试，不提前扩张到 manifest、签名、ABI 或 lifecycle 实现。

### 下一步

1. 按依赖顺序继续推进 PLG-TODO-002，定义 PluginCatalog 数据结构。

### 风险

1. trust_level/status 当前仅冻结最小枚举；若后续评审要求新增状态或细化等级，需通过单独评审保持兼容演进。

## 记录 #055

- 日期：2026-03-27
- 阶段：platform/linux 组件专项 TODO
- 任务：PLAT-LNX-TODO-004 定义 IThread 接口头文件
- 状态：已完成

### 改动

1. 完成 PLAT-LNX-TODO-004-D 设计收敛：
   - 新增 [docs/todos/platform/PLAT-LNX-TODO-004-IThread设计收敛.md](docs/todos/platform/PLAT-LNX-TODO-004-IThread%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，固化 IThread 调用面、ThreadOptions 字段边界、Design->Build 映射与 D Gate。
2. 完成 PLAT-LNX-TODO-004-B 代码落地：
   - 新增 [platform/include/IThread.h](platform/include/IThread.h)
   - 新增 [tests/unit/platform/linux/InterfaceSurfaceTest.cpp](tests/unit/platform/linux/InterfaceSurfaceTest.cpp)
   - 更新 [tests/unit/platform/linux/CMakeLists.txt](tests/unit/platform/linux/CMakeLists.txt)
   - 回写 [docs/todos/platform/DASALL_platform_linux组件专项TODO.md](docs/todos/platform/DASALL_platform_linux%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test`
   - `ctest --test-dir build-ci -N -R InterfaceSurfaceTest`
   - `ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test` 通过。
   - `ctest --test-dir build-ci -N -R InterfaceSurfaceTest` 通过，发现 1 个测试。
   - `ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure` 通过，1/1 tests passed。

### 结果

1. platform/linux 线程接口调用面已冻结，后续 PosixThreadProvider 与 LinuxPlatformFactory 可以复用统一接口契约。
2. 当前只完成 IThread 单接口冻结，ITimer/IQueue/IFileSystem/INetwork/IIPC 将按后续原子任务继续推进。

### 下一步

1. 按依赖顺序继续推进 PLAT-LNX-TODO-005，冻结 ITimer 接口头文件。

### 风险

1. 目前 ThreadJoinResult 只承载 joined 最小事实，若后续实现需要扩展 join 统计信息，应先经过接口评审避免隐式 breaking。

## 记录 #054

- 日期：2026-03-27
- 阶段：platform/linux 组件专项 TODO
- 任务：PLAT-LNX-TODO-003 定义 PlatformError 与 PlatformResult 头文件
- 状态：已完成

### 改动

1. 完成 PLAT-LNX-TODO-003-D 设计收敛：
   - 新增 [docs/todos/platform/PLAT-LNX-TODO-003-PlatformError设计收敛.md](docs/todos/platform/PLAT-LNX-TODO-003-PlatformError%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，固化字段集合、最小 contracts 映射锚点、Design->Build 映射与 D Gate。
   - 针对 BLK-04，采用“冻结 category->contracts 一级失败域映射 + 单测”完成最小解阻，不提前扩张细粒度 ErrorInfo 映射评审范围。
2. 完成 PLAT-LNX-TODO-003-B 代码落地：
   - 新增 [platform/include/PlatformError.h](platform/include/PlatformError.h)
   - 新增 [platform/include/PlatformResult.h](platform/include/PlatformResult.h)
   - 新增 [tests/unit/platform/linux/PlatformErrorMappingTest.cpp](tests/unit/platform/linux/PlatformErrorMappingTest.cpp)
   - 更新 [tests/unit/platform/linux/CMakeLists.txt](tests/unit/platform/linux/CMakeLists.txt)
   - 回写 [docs/todos/platform/DASALL_platform_linux组件专项TODO.md](docs/todos/platform/DASALL_platform_linux%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci --target dasall_platform dasall_platform_error_mapping_unit_test`
   - `ctest --test-dir build-ci -N -R PlatformErrorMappingTest`
   - `ctest --test-dir build-ci -R PlatformErrorMappingTest --output-on-failure`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci --target dasall_platform dasall_platform_error_mapping_unit_test` 通过，`ninja: no work to do.`。
   - `ctest --test-dir build-ci -N -R PlatformErrorMappingTest` 通过，发现 1 个测试。
   - `ctest --test-dir build-ci -R PlatformErrorMappingTest --output-on-failure` 通过，1/1 tests passed。

### 结果

1. platform/linux 错误模型已具备可编译、可测试的最小落地形态，后续接口和 provider 任务可以复用统一错误事实结构。
2. BLK-04 在本轮以最小映射锚点完成解阻；更细粒度 ErrorInfo 评审可在后续任务中增量推进。

### 下一步

1. 按依赖顺序继续推进 PLAT-LNX-TODO-004，冻结 IThread 接口头文件。

### 风险

1. 当前 category 映射只覆盖 contracts 一级失败域，未扩展到更细粒度错误语义；后续扩展需保证现有映射测试稳定。
2. 当前前台终端输出回传偶发失败；若后续复现，应继续使用后台终端 + 输出回读链路保证验收证据完整。

## 记录 #053

- 日期：2026-03-27
- 阶段：platform/linux 组件专项 TODO
- 任务：PLAT-LNX-TODO-002 定义 PlatformCapabilitySet 数据结构头文件
- 状态：已完成

### 改动

1. 完成 PLAT-LNX-TODO-002-D 设计收敛：
   - 新增 [docs/todos/platform/PLAT-LNX-TODO-002-LinuxPlatformCapabilities设计收敛.md](docs/todos/platform/PLAT-LNX-TODO-002-LinuxPlatformCapabilities%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，固化能力三态、reason 约束、Design->Build 映射与 D Gate。
   - 明确本轮只冻结状态三态和 reason 文本，不提前扩张独立 reason_code 域或 CapabilityRegistry 行为。
2. 完成 PLAT-LNX-TODO-002-B 代码落地：
   - 新增 [platform/include/linux/LinuxPlatformCapabilities.h](platform/include/linux/LinuxPlatformCapabilities.h)
   - 新增 [tests/unit/platform/linux/LinuxPlatformCapabilitiesTest.cpp](tests/unit/platform/linux/LinuxPlatformCapabilitiesTest.cpp)
   - 更新 [tests/unit/platform/linux/CMakeLists.txt](tests/unit/platform/linux/CMakeLists.txt)
   - 回写 [docs/todos/platform/DASALL_platform_linux组件专项TODO.md](docs/todos/platform/DASALL_platform_linux%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci --target dasall_platform dasall_linux_platform_capabilities_unit_test`
   - `ctest --test-dir build-ci -N -R LinuxPlatformCapabilitiesTest`
   - `ctest --test-dir build-ci -R LinuxPlatformCapabilitiesTest --output-on-failure`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci --target dasall_platform dasall_linux_platform_capabilities_unit_test` 通过。
   - `ctest --test-dir build-ci -N -R LinuxPlatformCapabilitiesTest` 通过，发现 1 个测试。
   - `ctest --test-dir build-ci -R LinuxPlatformCapabilitiesTest --output-on-failure` 通过，1/1 tests passed。

### 结果

1. platform/linux 能力表对象已经以最小 header-only 形式落盘，后续 CapabilityRegistry 和 LinuxPlatformFactory 可以直接复用统一的三态与 reason 约束。
2. 本轮只接入当前任务所需的定向 unit 测试，不声称完成 PLAT-LNX-TODO-019 的完整平台注册矩阵。

### 下一步

1. 按依赖顺序继续推进 PLAT-LNX-TODO-003，冻结 PlatformError 与 PlatformResult。

### 风险

1. `NotProbed` 当前作为默认 reason 文本使用；如果后续 reason 规范评审要求更严格的 token 词典，应在不改变三态语义的前提下局部替换。
2. 当前 VS Code CMake Tools target 解析仍不可用；后续 platform 任务在该问题未恢复前，仍应优先使用仓库已验证的 build-ci 验证链路。

## 记录 #052

- 日期：2026-03-26
- 阶段：platform/linux 组件专项 TODO
- 任务：PLAT-LNX-TODO-001 定义 PlatformInitConfig 数据结构头文件
- 状态：已完成

### 改动

1. 完成 PLAT-LNX-TODO-001-D 设计收敛：
   - 新增 [docs/todos/platform/PLAT-LNX-TODO-001-PlatformInitConfig设计收敛.md](docs/todos/platform/PLAT-LNX-TODO-001-PlatformInitConfig%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)，固化字段集合、默认值、Design->Build 映射与 D Gate。
   - 明确本轮只冻结 `target_platform/profile_name/enable_hal/queue_defaults/io_timeouts`，不提前扩张到 profile 注入键统一或工厂逻辑。
2. 完成 PLAT-LNX-TODO-001-B 代码落地：
   - 新增 [platform/include/linux/PlatformInitConfig.h](platform/include/linux/PlatformInitConfig.h)
   - 新增 [tests/unit/platform/linux/PlatformInitConfigTest.cpp](tests/unit/platform/linux/PlatformInitConfigTest.cpp)
   - 新增 [tests/unit/platform/CMakeLists.txt](tests/unit/platform/CMakeLists.txt)
   - 新增 [tests/unit/platform/linux/CMakeLists.txt](tests/unit/platform/linux/CMakeLists.txt)
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)
   - 回写 [docs/todos/platform/DASALL_platform_linux组件专项TODO.md](docs/todos/platform/DASALL_platform_linux%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci --target dasall_platform dasall_platform_init_config_unit_test`
   - `ctest --test-dir build-ci -N -R PlatformInitConfigTest`
   - `ctest --test-dir build-ci -R PlatformInitConfigTest --output-on-failure`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci --target dasall_platform dasall_platform_init_config_unit_test` 通过，`ninja: no work to do.`。
   - `ctest --test-dir build-ci -N -R PlatformInitConfigTest` 通过，发现 1 个测试。
   - `ctest --test-dir build-ci -R PlatformInitConfigTest --output-on-failure` 通过，1/1 tests passed。

### 结果

1. platform/linux 初始化配置对象已经以最小 header-only 形式落盘，后续 PLAT-LNX-TODO-002~010 可以直接复用该对象而无需再次猜测默认值。
2. 本轮只为当前任务接入最小 unit 注册路径，未声称完成 PLAT-LNX-TODO-019 的完整平台注册矩阵。

### 下一步

1. 按依赖顺序继续推进 PLAT-LNX-TODO-002，冻结 PlatformCapabilitySet。

### 风险

1. `profile_name` 与 `target_platform` 当前仍为字符串；如果后续冻结为 enum 或强类型包装，必须通过接口变更评审单独处理，不能在后续任务中隐式替换。
2. 当前 VS Code CMake Tools 仍未解析出可用 build target；后续 platform 任务在该问题未恢复前，仍应优先使用仓库已验证的 build-ci 验证链路。

## 记录 #051

- 日期：2026-03-26
- 阶段：infrastructure 子系统专项 TODO
- 任务：INF-TODO-012 注册 infra contracts 边界测试入口
- 状态：已完成

### 改动

1. 完成 INF-TODO-012-D 设计对账：
   - 核对 `tests/contract/CMakeLists.txt` 已通过 centralized registration 机制接入 9 个 infra 边界 contract 用例。
   - 核对相关 infra contract 目标已显式链接 `dasall_infra`，并统一打上 `contract` 标签。
2. 完成 INF-TODO-012-B 证据闭环：
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci`
   - `ctest --test-dir build-ci -N -L contract`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci` 通过，`ninja: no work to do.`。
   - `ctest --test-dir build-ci -N -L contract` 通过，发现 90 个 `contract` 标签测试，其中包含 9 个 infra 边界 contract 用例。
   - `ctest --test-dir build-ci --output-on-failure -L contract` 通过，90/90 tests passed。

### 结果

1. infra contract 注册入口已经在前序对象/接口/错误码任务落盘时同步接通，本轮已完成对账并补齐正式验收证据。
2. 阶段 D 的 contract 门已经具备稳定基线，后续可以进入阶段 E 的审计组件骨架与策略/诊断接口冻结任务。

### 下一步

1. 按阶段 E 顺序继续推进 INF-TODO-016，建立 AuditService 独立组件骨架。

### 风险

1. 当前 `contract` 标签集合覆盖全仓 contracts 基线而不是 infra 专属子集；后续如果需要更细粒度门禁，应考虑补充 infra 专属标签或命名筛选规则，但本轮不扩大范围。

## 记录 #050

- 日期：2026-03-26
- 阶段：infrastructure 子系统专项 TODO
- 任务：INF-TODO-011 注册 infra 单元测试入口
- 状态：已完成

### 改动

1. 完成 INF-TODO-011-D 设计对账：
   - 核对 `tests/unit/CMakeLists.txt` 已接入 `infra` 子目录。
   - 核对 `tests/unit/infra/CMakeLists.txt` 已注册 9 个 infra unit 目标并统一打上 `unit` 标签。
2. 完成 INF-TODO-011-B 证据闭环：
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci`
   - `ctest --test-dir build-ci -N -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci` 通过，`ninja: no work to do.`。
   - `ctest --test-dir build-ci -N -L unit` 通过，发现 10 个 `unit` 标签测试，其中包含 9 个 infra unit 用例。
   - `ctest --test-dir build-ci --output-on-failure -L unit` 通过，10/10 tests passed。

### 结果

1. infra unit 注册入口已经在前序任务中随测试落盘完成，本轮已完成对账并补齐正式验收证据。
2. 阶段 D 的 unit 门已经具备稳定基线，下一轮可以继续推进 INF-TODO-012 的 contract 注册与边界执行证据。

### 下一步

1. 按阶段 D 顺序继续推进 INF-TODO-012，复核 infra contract 测试入口与执行证据。

### 风险

1. 当前 `unit` 标签集合仍包含非 infra 的 `dasall_runtime_smoke_test`；后续如果需要更细粒度门禁，应考虑为 infra unit 单独补充标签或正则筛选规则，但本轮不扩大范围。

## 记录 #049

- 日期：2026-03-26
- 阶段：infrastructure 子系统专项 TODO
- 任务：INF-TODO-010 infra CMake 落盘入口
- 状态：已完成

### 改动

1. 完成 INF-TODO-010-D 设计收敛：
   - 基于 infrastructure 详细设计 7/8.1，将 dasall_infra 目标的现有真实源码收敛为 core/tracing 分组，并把公开头文件通过 `PUBLIC_HEADER` 属性显式接入目标。
   - 当时保留 `src/placeholder.cpp` 作为过渡期 non-empty 兜底；当前已完成真实源文件入图，该说明仅保留为阶段性记录，不再代表现行基线。
2. 完成 INF-TODO-010-B 代码落地：
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci --target dasall_infra`
   - `ctest --test-dir build-ci -N`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci --target dasall_infra` 通过，`ninja: no work to do.`。
   - `ctest --test-dir build-ci -N` 通过，发现 101 个测试，包含既有 infra unit 与 contract 用例。

### 结果

1. dasall_infra 目标已具备明确的公开头文件入口和按角色分组的真实源文件入口，后续子域可以在现有变量上增量接线，而不必继续把 CMake 收敛逻辑散落到单行 target_sources 追加中。
2. 当前收敛仍保持 L2 边界，只整理现有已冻结对象/接口的构建入口，不提前把未冻结子域实现接进目标。

### 下一步

1. 按阶段 D 顺序继续推进 INF-TODO-011，复核 infra 单元测试入口与按标签执行证据。

### 风险

1. `PUBLIC_HEADER` 当前只覆盖已冻结公开头文件；后续新增 config/secret/ota/plugin 等头文件时，必须在任务完成时同步接入该列表，否则会再次形成构建入口漂移。

## 记录 #048

- 日期：2026-03-26
- 阶段：infrastructure 子系统专项 TODO
- 任务：INF-TODO-009 infra 私有错误码域
- 状态：已完成

### 改动

1. 完成 INF-TODO-009-D 设计收敛：
   - 基于 infrastructure 详细设计 6.6/6.8/9.1，冻结 `INF_E_CONFIG_INVALID`、`INF_E_SECRET_UNAVAILABLE`、`INF_E_LOG_QUEUE_FULL`、`INF_E_AUDIT_WRITE_FAIL`、`INF_E_HEALTH_PROBE_TIMEOUT`、`INF_E_OTA_VERIFY_FAIL`、`INF_E_OTA_ROLLBACK_FAIL` 七个 infra 私有码。
   - 鉴于 contracts 当前只冻结五个粗粒度 `ResultCode` 样本码，本轮只建立 infra 私有码到 contracts validation/provider/runtime 三类结果码的一对多映射规则，不扩写共享 contracts 枚举。
2. 完成 INF-TODO-009-B 代码落地：
   - 新增 [infra/include/InfraErrorCode.h](infra/include/InfraErrorCode.h)
   - 新增 [infra/src/InfraErrorCode.cpp](infra/src/InfraErrorCode.cpp)
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)
   - 新增 [tests/unit/infra/InfraErrorCodeTest.cpp](tests/unit/infra/InfraErrorCodeTest.cpp)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 新增 [tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp](tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci` 通过。
   - `ctest --test-dir build-ci --output-on-failure -L unit` 通过，10/10 tests passed，新增 `InfraErrorCodeUnitTest` 被发现并执行。
   - `ctest --test-dir build-ci --output-on-failure -L contract` 通过，90/90 tests passed，新增 `InfraErrorCodeBoundaryContractTest` 被发现并执行。

### 结果

1. infra 已获得一个独立、可测试、可追溯的私有错误码域，后续接口和组件可以先引用私有码，再通过映射稳定收敛到 contracts 粗粒度失败语义。
2. 当前映射规则仍受 contracts 一级类别粒度限制，后续若要细化 plugin/policy/diagnostics 等错误语义，必须先走 contracts 或专项设计冻结，而不是直接扩写共享结果码。

### 下一步

1. 按阶段 C 顺序继续推进 INF-TODO-010，接线 infra CMake 落盘入口。

### 风险

1. `InfraErrorCode` 当前只覆盖主 TODO 行列出的七个 Build-ready 私有码；详细设计中 plugin/policy/diagnostics 扩展错误还未纳入本轮，不应在后续实现中越过该边界直接追加共享映射。

## 记录 #047

- 日期：2026-03-26
- 阶段：infrastructure 子系统专项 TODO
- 任务：INF-TODO-008 IHealthMonitor 接口
- 状态：已完成

### 改动

1. 完成 INF-TODO-008-D 设计收敛：
   - 基于 infrastructure 详细设计 6.6/6.8 与 health 模块详细设计 6.5/6.6，冻结 `IHealthMonitor::register_probe` 与 `IHealthMonitor::evaluate` 两个最小接口。
   - 针对未冻结的 `IHealthProbe` 形状与 probe timeout 细节，本轮只引入 `HealthProbeRegistration` 占位类型，保留 `probe_name/probe_group/opaque_probe_ref` 三个最小字段，避免过早引入具体探针抽象与调度模型。
   - 统一返回 `HealthMonitorRegistrationResult` 与 `HealthEvaluationResult`，仅引用 contracts `ResultCode` 与 `ErrorInfo`，保持健康评估失败语义可观测。
2. 完成 INF-TODO-008-B 代码落地：
   - 当时新增健康监视入口；当前 canonical 头文件已统一为 [infra/include/health/IHealthMonitor.h](infra/include/health/IHealthMonitor.h)，旧根层路径已退出
   - 新增 [tests/unit/infra/HealthMonitorInterfaceTest.cpp](tests/unit/infra/HealthMonitorInterfaceTest.cpp)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 新增 [tests/contract/smoke/HealthMonitorInterfaceBoundaryContractTest.cpp](tests/contract/smoke/HealthMonitorInterfaceBoundaryContractTest.cpp)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci` 通过。
   - `ctest --test-dir build-ci --output-on-failure -L unit` 通过，9/9 tests passed，新增 `HealthMonitorInterfaceTest` 被发现并执行。
   - `ctest --test-dir build-ci --output-on-failure -L contract` 通过，89/89 tests passed，新增 `HealthMonitorInterfaceBoundaryContractTest` 被发现并执行。

### 结果

1. `IHealthMonitor` 已与 `HealthSnapshot` 建立稳定的头文件级接口关系，为后续 health facade、probe registry 和 evaluator 接线提供固定调用面。
2. probe 注册语义当前被严格限制在 infra 私有占位类型内，后续只能扩展具体 probe 抽象、timeout 和订阅细节，不能破坏本轮 contracts 对齐的返回语义与输出边界。

### 下一步

1. 按阶段 C 顺序继续推进 INF-TODO-009，冻结 infra 私有错误码域。

### 风险

1. `HealthProbeRegistration` 当前只是最小占位类型，后续引入真实 IHealthProbe、策略阈值与调度周期时必须通过专项设计补充，不应直接把实现细节写回接口冻结层。

## 记录 #046

- 日期：2026-03-26
- 阶段：infrastructure 子系统专项 TODO
- 任务：INF-TODO-006 IAuditLogger 接口
- 状态：已完成

### 改动

1. 完成 INF-TODO-006-D 设计收敛：
   - 基于 infrastructure 详细设计 6.6 与 audit 模块详细设计 6.5/6.6/6.8，冻结 `IAuditLogger::write_audit` 与 `IAuditLogger::export_audit` 两个最小接口。
   - 针对 `export_audit(filter)` 的未冻结细节，本轮只引入 `AuditExportFilter.opaque_selector` 占位类型，避免过早引入真实过滤模型和导出分页语义。
   - 统一返回 `AuditWriteResult` 与 `AuditExportResult`，仅引用 contracts `ResultCode` 与 `ErrorInfo`，保持审计失败语义可观测。
2. 完成 INF-TODO-006-B 代码落地：
   - 新增 [infra/include/audit/IAuditLogger.h](infra/include/audit/IAuditLogger.h)
   - 新增 [tests/unit/infra/AuditLoggerInterfaceTest.cpp](tests/unit/infra/AuditLoggerInterfaceTest.cpp)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 新增 [tests/contract/smoke/AuditLoggerInterfaceBoundaryContractTest.cpp](tests/contract/smoke/AuditLoggerInterfaceBoundaryContractTest.cpp)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci` 通过。
   - `ctest --test-dir build-ci --output-on-failure -L unit` 通过，8/8 tests passed，新增 `AuditLoggerInterfaceTest` 被发现并执行。
   - `ctest --test-dir build-ci --output-on-failure -L contract` 通过，88/88 tests passed，新增 `AuditLoggerInterfaceBoundaryContractTest` 被发现并执行。

### 结果

1. `IAuditLogger` 已与 `AuditEvent` 建立稳定的头文件级接口关系，并保持与 `ILogger` 的职责分离，为后续 AuditService 与 fallback/export 组件接线提供固定调用面。
2. export 语义当前被严格限制在 infra 私有占位 filter 内，后续只能扩展过滤和分页细节，不能破坏本轮 contracts 对齐的返回语义。

### 下一步

1. 按阶段 B 顺序继续推进 INF-TODO-008，冻结 `IHealthMonitor` 接口。

### 风险

1. `AuditExportFilter` 当前只是最小占位类型，后续引入真实过滤窗口与分页语义时必须通过专项设计补充，不应直接把实现细节写回接口冻结层。

## 记录 #045

- 日期：2026-03-26
- 阶段：infrastructure 子系统专项 TODO
- 任务：INF-TODO-005 ILogger 接口
- 状态：已完成

### 改动

1. 完成 INF-TODO-005-D 设计收敛：
   - 基于 infrastructure 详细设计 6.6 与 logging 模块详细设计 6.5/6.6/6.8，冻结 `ILogger::log` 与 `ILogger::flush` 两个最小接口。
   - 针对 `flush(deadline)` 的未冻结细节，本轮只引入 `LogFlushDeadline.timeout_ms` 占位类型，避免过早引入 scheduler 或异步队列实现细节。
   - 统一返回 `LogWriteResult`，仅引用 contracts `ResultCode` 与 `ErrorInfo`，保持日志失败语义可观测。
2. 完成 INF-TODO-005-B 代码落地：
   - 当时新增日志入口；当前 canonical 头文件已统一为 [infra/include/logging/ILogger.h](infra/include/logging/ILogger.h)，旧根层路径已退出
   - 新增 [tests/unit/infra/LoggerInterfaceTest.cpp](tests/unit/infra/LoggerInterfaceTest.cpp)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 新增 [tests/contract/smoke/LoggerInterfaceBoundaryContractTest.cpp](tests/contract/smoke/LoggerInterfaceBoundaryContractTest.cpp)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci` 通过。
   - `ctest --test-dir build-ci --output-on-failure -L unit` 通过，7/7 tests passed，新增 `LoggerInterfaceTest` 被发现并执行。
   - `ctest --test-dir build-ci --output-on-failure -L contract` 通过，87/87 tests passed，新增 `LoggerInterfaceBoundaryContractTest` 被发现并执行。

### 结果

1. `ILogger` 已与 `LogEvent` 建立稳定的头文件级接口关系，为后续 logging facade、sink 路由和配置接线提供固定调用面。
2. flush 语义当前被严格限制在 infra 私有占位类型内，后续只能扩展 deadline 细节，不能破坏本轮 contracts 对齐的返回语义。

### 下一步

1. 按阶段 B 顺序继续推进 INF-TODO-006，冻结 `IAuditLogger` 接口。

### 风险

1. `LogFlushDeadline` 当前只是最小占位类型，后续引入真实 deadline/scheduler 语义时必须通过专项设计补充，不应直接把实现细节写回接口冻结层。

## 记录 #044

- 日期：2026-03-26
- 阶段：infrastructure 子系统专项 TODO
- 任务：INF-TODO-002 IInfrastructureService 接口与 Facade 生命周期骨架
- 状态：已完成

### 改动

1. 完成 INF-TODO-002-D 设计收敛：
   - 基于 infrastructure 详细设计 6.6/6.7，将基础设施统一入口收敛为 `init/start/stop/execute` 四个最小生命周期方法。
   - 鉴于 `execute(command)` 的 payload 与 config 细节尚未冻结，本轮仅保留 `InfrastructureConfig.profile` 与 `InfraCommandRequest.name` 两个最小骨架字段，避免过早侵入 diagnostics、ota 等子域对象。
   - 统一返回 `InfraOperationResult`，仅引用 contracts 既有 `ResultCode` 与 `ErrorInfo` 作为错误语义出口，保持接口边界稳定。
2. 完成 INF-TODO-002-B 代码落地：
   - 新增 [infra/include/IInfrastructureService.h](infra/include/IInfrastructureService.h)
   - 新增 [infra/src/InfraServiceFacade.cpp](infra/src/InfraServiceFacade.cpp)
   - 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)
   - 新增 [tests/unit/infra/InfraServiceFacadeTest.cpp](tests/unit/infra/InfraServiceFacadeTest.cpp)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 新增 [tests/contract/smoke/InfrastructureServiceBoundaryContractTest.cpp](tests/contract/smoke/InfrastructureServiceBoundaryContractTest.cpp)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci` 通过。
   - `ctest --test-dir build-ci --output-on-failure -L unit` 通过，6/6 tests passed，新增 `InfraServiceFacadeTest` 被发现并执行。
   - `ctest --test-dir build-ci --output-on-failure -L contract` 通过，86/86 tests passed，新增 `InfrastructureServiceBoundaryContractTest` 被发现并执行。

### 结果

1. placeholder 不再是 infra 唯一真实源码入口，统一生命周期主控点已经以可编译骨架形式落盘。
2. 基础设施服务返回语义已固定为 contracts `ResultCode/ErrorInfo`，为后续 `ILogger`、`IAuditLogger`、`IHealthMonitor` 与私有错误码域任务保留稳定边界。

### 下一步

1. 按阶段 B 顺序继续推进 INF-TODO-005，冻结 `ILogger` 接口。

### 风险

1. `InfrastructureConfig` 和 `InfraCommandRequest` 目前都是最小占位形状，后续扩展必须来自专项设计补充，不能直接在实现层面自行膨胀。

## 记录 #043

- 日期：2026-03-26
- 阶段：infrastructure 子系统专项 TODO
- 任务：INF-TODO-007 HealthSnapshot 数据结构
- 状态：已完成

### 改动

1. 完成 INF-TODO-007-D 设计收敛：
   - 基于 infrastructure 详细设计 6.5、health 模块详细设计 6.5/6.8 和 Azure Health Endpoint Monitoring 模式，冻结 HealthSnapshot 的 `liveness/readiness/degraded/failed_components` 四字段。
   - 采用最小一致性守卫区分 ready、degraded、failed 三类状态，并禁止非存活快照继续标记 ready/degraded。
   - 将 `failed_components` 收敛为最小字符串集合，并显式拒绝空值、重复项以及 `final_runtime_state` 等 runtime-state 保留字段名，避免健康快照越权回写 runtime 状态。
2. 完成 INF-TODO-007-B 代码落地：
   - 当时冻结健康快照对象；当前 canonical 入口已统一收敛到 [infra/include/health/HealthStateTypes.h](infra/include/health/HealthStateTypes.h)，不再使用根层 HealthSnapshot 头文件
   - 新增 [tests/unit/infra/HealthSnapshotTest.cpp](tests/unit/infra/HealthSnapshotTest.cpp)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 新增 [tests/contract/smoke/HealthSnapshotBoundaryContractTest.cpp](tests/contract/smoke/HealthSnapshotBoundaryContractTest.cpp)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci` 通过。
   - `ctest --test-dir build-ci --output-on-failure -L unit` 通过，5/5 tests passed，新增 `HealthSnapshotUnitTest` 被发现并执行。
   - `ctest --test-dir build-ci --output-on-failure -L contract` 通过，85/85 tests passed，新增 `HealthSnapshotBoundaryContractTest` 被发现并执行。

### 结果

1. HealthSnapshot 已从详细设计字段表收敛为可编译、可测试、可追溯的数据结构，为后续 IHealthMonitor 与 probe policy 任务提供稳定输出对象。
2. 健康三态与 runtime state 的边界已经固定在 infra 私有布尔位与组件列表上，后续任务不能把 recovery/runtime 状态字段直接并入健康快照。

### 下一步

1. 按依赖顺序推进 INF-TODO-008，冻结 IHealthMonitor 接口。

### 风险

1. 当前 `failed_components` 仍是最小字符串集合，后续任务只能增加解释或策略映射，不应破坏本轮去重/非空的可序列化基线。
2. HealthSnapshot 目前未引入 version/ts 等扩展字段；若后续需要回放窗口信息，应新增专用对象或单独评审，而不是直接扩写本轮四字段表。

## 记录 #042

- 日期：2026-03-26
- 阶段：infrastructure 子系统专项 TODO
- 任务：INF-TODO-004 AuditEvent 数据结构
- 状态：已完成

### 改动

1. 完成 INF-TODO-004-D 设计收敛：
   - 基于 infrastructure 详细设计 6.5、audit 模块详细设计 6.5/6.8 和 ToolResult/RecoveryOutcome contracts guards，冻结 AuditEvent 的 `action/actor/target/evidence_ref/outcome/side_effects` 六字段。
   - 将 `evidence_ref` 收敛为最小类型化锚点 `AuditEvidenceRef`，仅允许 `ToolResult` 或 `RecoveryOutcome` 两类 execution-result 引用，不嵌入 contracts 对象本体。
   - 保持 `side_effects` 为最小字符串集合，只校验可序列化、非空和无重复，不提前扩展成复杂 effect schema。
2. 完成 INF-TODO-004-B 代码落地：
   - 当时冻结审计事件对象；当前 canonical 入口已统一收敛到 [infra/include/audit/AuditTypes.h](infra/include/audit/AuditTypes.h)，不再使用根层 AuditEvent 头文件
   - 新增 [tests/unit/infra/AuditEventTest.cpp](tests/unit/infra/AuditEventTest.cpp)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 新增 [tests/contract/smoke/AuditEventBoundaryContractTest.cpp](tests/contract/smoke/AuditEventBoundaryContractTest.cpp)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci` 通过。
   - `ctest --test-dir build-ci --output-on-failure -L unit` 通过，4/4 tests passed，新增 `AuditEventUnitTest` 被发现并执行。
   - `ctest --test-dir build-ci --output-on-failure -L contract` 通过，84/84 tests passed，新增 `AuditEventBoundaryContractTest` 被发现并执行。

### 结果

1. AuditEvent 已从详细设计字段表收敛为可编译、可测试、可追溯的数据结构，为后续 IAuditLogger 和 AuditService 任务提供稳定输入对象。
2. evidence_ref 的 contracts 边界已固定在 ToolResult/RecoveryOutcome 两类 execution-result 语义上，避免在 infra 审计对象里扩写 recovery 或 tool 的控制字段。

### 下一步

1. 按 audit 依赖顺序推进 INF-TODO-006，冻结 IAuditLogger 接口。

### 风险

1. 当前 `side_effects` 仍只是最小字符串集合，后续任务只能增加解释或导出策略，不应破坏本轮去重/非空的可序列化基线。
2. evidence_ref 目前只覆盖 ToolResult/RecoveryOutcome；若后续确需引入其他 evidence 类型，应新增明确评审而不是顺手扩写本轮枚举。

## 记录 #041

- 日期：2026-03-26
- 阶段：infrastructure 子系统专项 TODO
- 任务：INF-TODO-003 LogEvent 数据结构
- 状态：已完成

### 改动

1. 完成 INF-TODO-003-D 设计收敛：
   - 基于 infrastructure 详细设计 6.5 与 logging 模块详细设计 6.5/6.7，冻结 LogEvent 的 `level/module/message/attrs/ts` 五字段。
   - 明确 attrs 白名单尚未冻结，因此本轮只收敛为可序列化字符串键值映射，不提前做复杂 schema 或 sink 约束。
   - 采用最小 redaction helper 约束 token/secret/password/authorization 等敏感 attr 键，确保明文不直接进入后续 pipeline。
2. 完成 INF-TODO-003-B 代码落地：
   - 新增 [infra/include/LogEvent.h](infra/include/LogEvent.h)
   - 新增 [tests/unit/infra/LogEventTest.cpp](tests/unit/infra/LogEventTest.cpp)
   - 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 新增 [tests/contract/smoke/LogEventBoundaryContractTest.cpp](tests/contract/smoke/LogEventBoundaryContractTest.cpp)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci` 通过。
   - `ctest --test-dir build-ci --output-on-failure -L unit` 通过，3/3 tests passed，新增 `LogEventUnitTest` 被发现并执行。
   - `ctest --test-dir build-ci --output-on-failure -L contract` 通过，83/83 tests passed，新增 `LogEventBoundaryContractTest` 被发现并执行。

### 结果

1. LogEvent 已从设计字段表收敛为可编译、可测试、可追溯的数据结构，并为后续 ILogger/formatter/redaction 任务提供稳定输入对象。
2. `module` 作为顶层稳定字段冻结，同时提供 `category()` 访问别名，避免 logging 组件任务在术语层面引入破坏式改动。

### 下一步

1. 按依赖顺序推进 INF-TODO-004，冻结 AuditEvent 数据结构。

### 风险

1. attrs 键白名单仍未冻结，后续任务只能扩展规则，不应破坏本轮字符串键值映射的可序列化基线。
2. 当前 redaction helper 只覆盖最小敏感键片段，真正 ruleset 热更新和 formatter/sink 脱敏仍应留给 logging 组件后续任务。

## 记录 #040

- 日期：2026-03-26
- 阶段：infrastructure 子系统专项 TODO
- 任务：INF-TODO-001 InfraContext 数据结构
- 状态：已完成

### 改动

1. 完成 INF-TODO-001-D 设计收敛：
   - 基于 infrastructure 详细设计 6.5、AgentRequest/WorkerTask/WorkerLease contracts 和架构 6.11 多 Agent 追踪要求，冻结 InfraContext 六字段。
   - 明确 Design -> Build 映射：header-only 数据结构 + unit/contract 双测试出口。
   - 采用 unknown 作为缺失/空字符串的统一兜底语义，避免空指针和空字符串透传到 infra 可观测链路。
2. 完成 INF-TODO-001-B 代码落地：
   - 新增 [infra/include/InfraContext.h](infra/include/InfraContext.h)
   - 新增 [tests/unit/infra/InfraContextTest.cpp](tests/unit/infra/InfraContextTest.cpp)
   - 新增 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)
   - 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)
   - 新增 [tests/contract/smoke/InfraContextBoundaryContractTest.cpp](tests/contract/smoke/InfraContextBoundaryContractTest.cpp)
   - 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)
   - 回写 [docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)

### 测试

1. 验收命令：
   - `cmake -S . -B build-ci -G Ninja`
   - `cmake --build build-ci`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
2. 结果：
   - `cmake -S . -B build-ci -G Ninja` 通过。
   - `cmake --build build-ci` 通过。
   - `ctest --test-dir build-ci --output-on-failure -L unit` 通过，2/2 tests passed，新增 `InfraContextUnitTest` 被发现并执行。
   - `ctest --test-dir build-ci --output-on-failure -L contract` 通过，82/82 tests passed，新增 `InfraContextBoundaryContractTest` 被发现并执行。

### 结果

1. InfraContext 已从 TODO 设计条目收敛为可编译、可测试、可追溯的数据结构。
2. INF-TODO-002 以后可直接复用该对象作为 infra 对外接口和日志/审计/健康对象的共同上下文锚点。

### 下一步

1. 按顺序推进 INF-TODO-002，冻结 IInfrastructureService 与 Facade 生命周期骨架。

### 风险

1. 当前 InfraContext 仅冻结横切标识语义，不应在后续任务中顺手加入 worker_type、span_id 或 profile_id 等未在 INF-TODO-001 范围内的字段。
2. 如果后续接口任务要求更细的 tracing/span 传播对象，应新增专用对象而不是修改本轮已冻结的六字段表。

## 记录 #039

- 日期：2026-03-21
- 阶段：contracts 冻结（WP-05 双轨执行）
- 任务：WP05-T012 接口准入评估单与 InterfaceAdmissionGuards
- 状态：已完成

### 改动

1. 完成 WP05-T012-D 交付：
   - 新增 design 文档：
     - [docs/todos/contracts/deliverables/WP05-T012-接口准入评估单.md](docs/todos/contracts/deliverables/WP05-T012-%E6%8E%A5%E5%8F%A3%E5%87%86%E5%85%A5%E8%AF%84%E4%BC%B0%E5%8D%95.md)
   - 基于 T011 目录、阶段 5 准入原则、架构依赖规则与 ADR-006/008，明确 `Admit`、`Postpone`、`Return` 三类准入结论。
   - 固化首版结论：`IToolManager`、`ILLMAdapter` 为 Admit；其余 8 个 catalog 候选为 Postpone；目录外/元数据不完整/同模块伪依赖为 Return。
2. 完成 WP05-T012-B 代码落地：
   - 新增 header-only 准入守卫：
     - [contracts/include/boundary/InterfaceAdmissionGuards.h](contracts/include/boundary/InterfaceAdmissionGuards.h)
   - 提供 `InterfaceAdmissionDecision`、`InterfaceAdmissionResult`、metadata completeness、cross-module boundary、按条目/按名称准入评估与 admitted-count helper。
3. 新增 smoke contract test 并接入：
   - [tests/contract/smoke/InterfaceAdmissionContractTest.cpp](tests/contract/smoke/InterfaceAdmissionContractTest.cpp)
   - [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 注册 `InterfaceAdmissionContractTest`。
4. 回写任务状态：
   - [docs/todos/contracts/WP-05-子域细化与ContractTestsTODO.md](docs/todos/contracts/WP-05-%E5%AD%90%E5%9F%9F%E7%BB%86%E5%8C%96%E4%B8%8EContractTestsTODO.md) 将 WP05-T012-D/B 更新为 Done，并补充发现性与验收证据。

### 测试

1. 构建前发现性检查：
   - `ctest --test-dir build-ci -N -R InterfaceAdmissionContractTest`
   - 结果：`Total Tests: 0`，说明新测试在重配置前尚未被发现。
2. 重配置：
   - `cmake -S . -B build-ci -G Ninja`
   - 结果：通过；build-ci 成功重新生成。
3. 聚合验收：
   - `cmake --build build-ci --target dasall_contract_tests`
   - 结果：通过；72/72 contract tests passed，新增 `InterfaceAdmissionContractTest` 被纳入 `contract;smoke` 标签。
4. 构建后发现性检查：
   - `ctest --test-dir build-ci -N -R InterfaceAdmissionContractTest`
   - 结果：发现 1 个测试。
5. 指定测试验收：
   - `ctest --test-dir build-ci -R InterfaceAdmissionContractTest --output-on-failure`
   - 结果：通过；1/1 test passed。

### 结果

1. WP05-T012-D/B 已完成，接口准入规则已从文档结论收敛为可程序化执行的 compile-time 守卫。
2. T013 以后若新增 shared interface，已具备可复用的 admit/postpone/return 基线。

### 下一步

1. 按顺序推进 WP05-T013-D/B（序列化稳定性测试矩阵与首版自动化 contract tests）。

### 风险

1. 当前 admission baseline 只允许 2 个接口直接准入；其余候选仍依赖 supporting contracts 继续冻结，后续任务不应绕过 `Postpone` 结论直接把它们落入 contracts。
2. CMake Tools 在当前 VS Code 环境仍会报“无法配置项目”，本轮验收继续依赖仓库已验证的 `build-ci` 命令链路。

## 记录 #038

- 日期：2026-03-21
- 阶段：contracts 冻结（WP-05 双轨执行）
- 任务：WP05-T011 跨模块接口候选清单与 InterfaceCatalog
- 状态：已完成

### 改动

1. 完成 WP05-T011-D 交付：
   - 新增 design 文档：
     - [docs/todos/contracts/deliverables/WP05-T011-接口候选清单.md](docs/todos/contracts/deliverables/WP05-T011-接口候选清单.md)
   - 基于阶段 5 准入原则、架构 7.4 模块依赖规则、Blueprint 接口文件分布与 ADR-006/008，锁定 10 个跨模块接口候选。
   - 明确剔除 platform/infra/protocol-internal 接口，并区分 `ReviewReady` 与 `AwaitingSupportingContracts`。
2. 完成 WP05-T011-B 代码落地：
   - 新增 header-only 候选目录：
     - [contracts/include/boundary/InterfaceCatalog.h](contracts/include/boundary/InterfaceCatalog.h)
   - 提供 `InterfaceCandidate`、owner/consumer/readiness 枚举、静态 catalog 表与查询 helper，供 T012 准入守卫复用。
3. 新增 smoke contract test 并接入：
   - [tests/contract/smoke/InterfaceCatalogContractTest.cpp](tests/contract/smoke/InterfaceCatalogContractTest.cpp)
   - [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 注册 `InterfaceCatalogContractTest`。
4. 回写任务状态：
   - [docs/todos/contracts/WP-05-子域细化与ContractTestsTODO.md](docs/todos/contracts/WP-05-子域细化与ContractTestsTODO.md) 将 WP05-T011-D/B 更新为 Done，并补充发现性与验收证据。

### 测试

1. 构建前发现性检查：
   - `ctest --test-dir build-ci -N -R InterfaceCatalogContractTest`
   - 结果：`Total Tests: 0`，说明新测试在重配置前尚未被发现。
2. 重配置：
   - `cmake -S . -B build-ci -G Ninja`
   - 结果：通过；build-ci 成功重新生成。
3. 聚合验收：
   - `cmake --build build-ci --target dasall_contract_tests`
   - 结果：通过；71/71 contract tests passed，新增 `InterfaceCatalogContractTest` 被纳入 `contract;smoke` 标签。
4. 构建后发现性检查：
   - `ctest --test-dir build-ci -N -R InterfaceCatalogContractTest`
   - 结果：发现 1 个测试。
5. 指定测试验收：
   - `ctest --test-dir build-ci -R InterfaceCatalogContractTest --output-on-failure`
   - 结果：通过；1/1 test passed。

### 结果

1. WP05-T011-D/B 已完成，T012 可直接基于 `InterfaceCatalog.h` 进入接口准入守卫实现。
2. 接口候选集已从分散的架构文本收敛为可程序化审查的 compile-time catalog。

### 下一步

1. 按顺序推进 WP05-T012-D/B（接口准入评估单与 InterfaceAdmissionGuards）。

### 风险

1. 当前 `ReviewReady` 仅覆盖 `IToolManager` 与 `ILLMAdapter`；其余候选仍依赖 supporting contracts 继续冻结，T012 不应提前把它们直接准入。
2. CMake Tools 在当前 VS Code 环境仍会报“无法配置项目”，本轮验收继续依赖仓库已验证的 `build-ci` 命令链路。

## 记录 #037

- 日期：2026-03-19
- 阶段：contracts 冻结（WP-05 双轨执行）
- 任务：WP05-T001 子域推进顺序与执行顺序守卫
- 状态：已完成

### 改动

1. 完成 WP05-T001-D 交付：
   - 新增 design 文档：
     - [docs/todos/contracts/deliverables/WP05-T001-子域推进顺序表.md](docs/todos/contracts/deliverables/WP05-T001-子域推进顺序表.md)
   - 固化四波 rollout：Wave1 `tool`；Wave2 `prompt + memory`；Wave3 `task + event`；Wave4 `llm`。
   - 明确允许并行、禁止并行、越权禁区和 Design->Build 映射。
2. 完成 WP05-T001-B 代码落地：
   - 新增 header-only 守卫：
     - [contracts/include/boundary/DomainRolloutGuards.h](contracts/include/boundary/DomainRolloutGuards.h)
   - 提供 `DomainSubdomain`、`DomainRolloutWave`、`DomainRolloutDecision`、`DomainRolloutSnapshot`、`evaluate_domain_rollout_start()` 和完成计数 helper。
3. 新增 smoke contract test 并接入：
   - [tests/contract/smoke/DomainRolloutContractTest.cpp](tests/contract/smoke/DomainRolloutContractTest.cpp)
   - [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 注册 `DomainRolloutContractTest`。
4. 回写任务状态：
   - [docs/todos/contracts/WP-05-子域细化与ContractTestsTODO.md](docs/todos/contracts/WP-05-子域细化与ContractTestsTODO.md) 将 WP05-T001-D/B 更新为 Done，并补充验收证据。

### 测试

1. 聚合验收：
   - `cmake --build build-ci --target dasall_contract_tests`
   - 结果：通过；CMake 自动重生成后，61/61 contract tests passed，新增 `DomainRolloutContractTest` 被纳入 `contract;smoke` 标签。
2. 指定测试验收：
   - `ctest --test-dir build-ci -R DomainRolloutContractTest --output-on-failure`
   - 结果：通过；1/1 test passed。
3. 负例覆盖由新增测试内联验证：
   - `prompt` 在 `tool` 未完成时被阻断。
   - `prompt` 在 `task` 已启动的跨波次场景下被阻断。
   - `llm` 在 `event` 未完成时被阻断。
   - 已完成子域重复启动被阻断。

### 结果

1. WP05-T001-D/B 已完成，后续 T002-T010 可基于统一 rollout guard 继续推进。
2. WP05 当前推荐顺序已从“文档建议”收敛为可执行的 compile-time/contracts 守卫。

### 下一步

1. 按顺序推进 WP05-T002-D/B（ToolRequest 职责边界与契约对象）。

### 风险

1. 当前 rollout wave 属于 WP05 的首版节奏守卫；若后续评审决定扩大或收缩并行窗口，需要同步修订设计文档和 `DomainRolloutGuards.h`，避免文档与守卫漂移。
2. CMake Tools 在当前 VS Code 环境仍无法成功配置项目，构建验收暂时依赖仓库既有 `build-ci` 目录上的命令链路。

## 记录 #036

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：针对评审问题组织修复与完善（代码 + 测试 + 文档收敛）
- 状态：已完成

### 改动

1. 修复 Critical（头文件 helper 重定义）：
   - 新增公共 helper 头：[contracts/include/boundary/GuardCommon.h](contracts/include/boundary/GuardCommon.h)
   - 去重并改为复用：
     - [contracts/include/boundary/IdentityMetadata.h](contracts/include/boundary/IdentityMetadata.h)
     - [contracts/include/event/EventEnvelopeGuards.h](contracts/include/event/EventEnvelopeGuards.h)
     - [contracts/include/error/ErrorInfoGuards.h](contracts/include/error/ErrorInfoGuards.h)
     - [contracts/include/error/ErrorSourceGuards.h](contracts/include/error/ErrorSourceGuards.h)
2. 修复 Major（timeout 迁移溢出）：
   - [contracts/include/boundary/CompatibilityGuards.h](contracts/include/boundary/CompatibilityGuards.h)
   - 新增 `timeout_seconds -> timeout_ms` 上界校验，溢出时失败返回。
3. 修复 Major（BudgetSnapshot 大数转换风险）：
   - [contracts/include/checkpoint/BudgetSnapshotGuards.h](contracts/include/checkpoint/BudgetSnapshotGuards.h)
   - 改为安全 remaining 计算路径，超可表示范围时返回 `remaining computation overflow`。
4. 补充测试：
   - [tests/contract/smoke/CompatibilityContractTest.cpp](tests/contract/smoke/CompatibilityContractTest.cpp)
     - 新增 `test_timeout_seconds_overflow_is_rejected`。
   - [tests/contract/checkpoint/BudgetSnapshotContractTest.cpp](tests/contract/checkpoint/BudgetSnapshotContractTest.cpp)
     - 新增 `test_remaining_computation_overflow_is_rejected`。
5. 文档完善收敛：
   - [docs/todos/contracts/deliverables/WP02-T013-ReviewChecklist-v1.md](docs/todos/contracts/deliverables/WP02-T013-ReviewChecklist-v1.md) 状态更新为 Done。
   - [docs/todos/contracts/deliverables/WP02-T014-评审纪要.md](docs/todos/contracts/deliverables/WP02-T014-评审纪要.md) 评审范围扩展到 T001-T013 并补 D0 决议。
   - [docs/todos/contracts/WP-02-横切基础对象TODO.md](docs/todos/contracts/WP-02-横切基础对象TODO.md) 状态统一收敛为 Done。
   - [docs/todos/contracts/deliverables/WP02-T015-M2冻结包.md](docs/todos/contracts/deliverables/WP02-T015-M2冻结包.md) 冻结资产清单补全至 T015 自包含。
   - [docs/todos/contracts/deliverables/WP02-评审覆盖矩阵与代码审计报告-2026-03-16.md](docs/todos/contracts/deliverables/WP02-评审覆盖矩阵与代码审计报告-2026-03-16.md) 追加修复执行记录与修复后结论。

### 测试

1. 组合 include 编译复验：
   - `c++ -std=c++17 -Icontracts/include -c /tmp/dup_check.cpp -o /tmp/dup_check.o`
   - 结果：通过（无重定义错误）。
2. 门禁复验：
   - `bash scripts/ci/wp02_contract_gate.sh`
   - 结果：返回 0；contract tests 20/20 通过；关键门禁测试 5/5 通过。

### 结果

1. 评审报告中的 1 个 Critical + 2 个 Major 代码问题已修复并通过验收。
2. WP-02 相关评审/冻结文档状态完成一轮一致性收敛。
3. 审计结论从 `Changes Requested` 收敛为“可合并（在保持现有 gate 前提下）”。

### 下一步

1. 若继续推进，建议执行一次提交前整体验证（含 gate + 关键单测）并按“代码修复/文档收敛”拆分提交。

### 风险

1. 当前工作区仍有较多未提交历史改动；提交前需按变更意图分组，避免把不相关改动混入同一提交。

## 记录 #035

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：评审遗留项 L1/L2 闭环复验与文档一致性修复
- 状态：已完成

### 改动

1. 闭环复核评审遗留项：
   - L1（`timeout_seconds` -> `timeout_ms` 迁移一致性）对应实现与测试已在 `CompatibilityGuards` / `TimeDeadlineGuards` 落盘。
   - L2（unknown 枚举值降级证据）对应实现与测试已在 `EnumLifecycleGuards` 落盘。
2. 修正文档状态一致性：
   - `WP-02-横切基础对象-Build开发TODO.md` 的 Quality Gate 从“B014 Blocked”修正为“无 Blocked”。
   - `WP02-T014-评审纪要.md` 从 In Review 更新为 Done，并将 L1/L2 标注为 Closed。

### 测试

1. 执行门禁命令：
   - `bash scripts/ci/wp02_contract_gate.sh`
2. 结果：
   - 返回 0。
   - 关键门禁测试 5/5 通过：CompatibilityContractTest、TimeDeadlineContractTest、EventEnvelopeContractTest、EnumLifecycleContractTest、M2ChecklistContractTest。
   - 全量 contract 标签测试 20/20 通过。

### 结果

1. 评审遗留项 L1/L2 已形成“实现 + 测试 + gate”闭环证据。
2. WP-02 评审与 Build 文档状态一致，可作为后续冻结发布输入。

### 下一步

1. 进入 T015 发布准备时，复用本记录与 T014 纪要作为审计证据。

### 风险

1. 当前环境下 CMake Tools 扩展未能完成项目配置，暂以脚本门禁结果作为执行证据；后续建议补充一次 CMake Tools 侧复验。

## 记录 #034

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B014 新增 WP-02 CI 门禁脚本并接入流水线
- 状态：已完成

### 改动

1. 新增 WP-02 gate 脚本：
   - [scripts/ci/wp02_contract_gate.sh](scripts/ci/wp02_contract_gate.sh)
   - 脚本流程：configure -> build `dasall_contract_tests` -> 注册校验(`ctest -N -L contract`) -> 执行关键 WP02 测试 -> 执行全量 contract 标签测试。
2. 新增可配置 required tests 列表：
   - 默认门禁测试：CompatibilityContractTest、TimeDeadlineContractTest、EventEnvelopeContractTest、EnumLifecycleContractTest、M2ChecklistContractTest。
   - 支持 `WP02_GATE_REQUIRED_TESTS` 覆盖，便于 CI 场景注入与诊断。
3. 门禁失败语义落盘：
   - 注册缺失时脚本非 0 退出并打印缺失测试名。

### 测试

1. 执行验收命令（B014 原样）：
   - `bash scripts/ci/wp02_contract_gate.sh`
2. 结果：
   - 返回 0。
   - 输出包含 configure/build/registration/ctest 摘要。
   - 全量 contract 标签测试 20/20 通过。
3. 负例校验：
   - `WP02_GATE_REQUIRED_TESTS=DefinitelyMissingContractTest bash scripts/ci/wp02_contract_gate.sh`
   - 返回 `NEGATIVE_RC=1`，并输出缺失注册测试名，符合“门禁失败非 0”要求。

### 结果

1. WP02-B014 达成 Done 判定：脚本在可配置环境返回 0，且门禁失败场景稳定返回非 0。

### 下一步

1. WP-02 核心原子任务 B001-B014 已完成，下一步建议转入收尾复核（同步 CI 流水线调用并执行一次端到端 dry-run）。

### 风险

1. 当前脚本默认 generator 为 Ninja；若 CI 机型无 Ninja，需要在流水线设置 `CMAKE_GENERATOR`。
2. 脚本复用了 contract 标签全集执行，后续若测试规模显著增长，可考虑拆分为“关键门禁 + 全量夜跑”两级策略。

## 记录 #033

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B013 新增 M2 Checklist 自动校验入口
- 状态：已完成

### 改动

1. 新增 M2 Checklist 守卫头文件：
   - [contracts/include/boundary/M2ChecklistGuards.h](contracts/include/boundary/M2ChecklistGuards.h)
   - 定义 `M2ChecklistInputs`、`M2ChecklistResult`，并提供 `validate_m2_checklist(...)`。
2. 新增 A-F 六组门禁程序化判定：
   - 约束为“六组全部通过才通过”，并输出 `first_failed_gate` 便于定位。
3. 新增合同测试并接入 smoke 组：
   - [tests/contract/smoke/M2ChecklistContractTest.cpp](tests/contract/smoke/M2ChecklistContractTest.cpp)
   - [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 注册 `M2ChecklistContractTest`。

### 测试

1. 执行验收命令（B013 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R M2ChecklistContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 20/20 通过（含新增测试）。
   - `M2ChecklistContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：A-F 六组全部通过时 checklist 通过。
   - 负例：C 组失败时 checklist 阻断，且返回 first_failed_gate=C。

### 结果

1. WP02-B013 达成 Done 判定：Checklist 核心条目可程序化判定并通过测试。

### 下一步

1. 按顺序推进 WP02-B014（WP-02 CI 门禁脚本接入）。

### 风险

1. 当前 A-F 由布尔输入表示，若后续要承载更细粒度失败原因，需要在不破坏现有 API 的前提下扩展结果结构。
2. 目前 checklist 只做“聚合判定”，不替代各单项守卫；后续若单项守卫语义变化，需要同步维护 checklist 输入映射。

## 记录 #032

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B012 收敛 contract 测试编排并接入 CMake
- 状态：已完成

### 改动

1. 更新 contract 测试统一注册入口：
   - `tests/contract/CMakeLists.txt`
   - 将 `dasall_register_contract_test(...)` 扩展为四参数形式（可接收 group_label）。
2. 收敛四组 contract 测试编排：
   - 显式按 smoke/error/checkpoint/event 四组注册测试。
   - 每个测试统一打上 `contract` 与组标签（如 `contract;smoke`）。
3. 保持既有 contract tests 目标不变，仅增强可发现性与分组可观测性。

### 测试

1. 执行验收命令（B012 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 19/19 通过。
   - label 汇总显示：smoke=13、error=3、checkpoint=2、event=1。
3. 负例发现校验：
   - `ctest --test-dir build-ci -N -R DefinitelyMissingContractTest`
   - 输出 `Total Tests: 0`，验证未注册测试不会被误发现。

### 结果

1. WP02-B012 达成 Done 判定：新增/既有测试均可被 ctest 发现，且 label=contract 与四组分层正确生效。

### 下一步

1. 按顺序推进 WP02-B013（新增 M2 Checklist 自动校验入口）。

### 风险

1. 当前分组标签由 CMake 注册参数维护，后续新增测试若遗漏组标签，会影响分组统计但不影响 contract 主标签执行。
2. 若未来希望按组单独门禁（例如 `ctest -L event`），需在 CI 脚本中同步加入分组命令，避免本地与 CI 行为漂移。

## 记录 #031

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B011 补齐枚举降级与弃用生命周期守卫
- 状态：已完成

### 改动

1. 扩展枚举兼容辅助：
   - `contracts/include/boundary/CompatibilityGuards.h`
   - 新增 `has_unspecified_enum_sentinel(...)`，用于检测未知值降级路径是否具备 Unspecified 哨兵。
2. 新增枚举生命周期守卫：
   - `contracts/include/boundary/EnumLifecycleGuards.h`
   - 提供 `validate_enum_lifecycle_descriptor(...)` 与 `normalize_enum_with_lifecycle(...)`，实现：
     - 已知值保留；
     - 未知值降级到 Unspecified；
     - 删除 Unspecified 哨兵直接阻断；
     - deprecated 值必须属于 known_values。
3. 扩展/新增合同测试并接入：
   - `tests/contract/smoke/CompatibilityContractTest.cpp`（扩展）：新增 “缺失 Unspecified 哨兵可检测” 负例。
   - `tests/contract/smoke/EnumLifecycleContractTest.cpp`（新增）：
     - 正例：已知值保留；
     - 正例：未知值降级到 Unspecified；
     - 负例：删除 Unspecified 哨兵阻断。
   - `tests/contract/CMakeLists.txt` 注册 `EnumLifecycleContractTest`。

### 测试

1. 执行验收命令（B011 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "CompatibilityContractTest|EnumLifecycleContractTest" --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 19/19 通过（含新增测试）。
   - `CompatibilityContractTest` 与 `EnumLifecycleContractTest` 2/2 通过。
3. 覆盖摘要：
   - 已知值保留。
   - 未知值降级到 Unspecified。
   - 删除 Unspecified 哨兵被门禁阻断。

### 结果

1. WP02-B011 达成 Done 判定：unknown->Unspecified 稳定可测，且 Unspecified 删除动作被拦截。

### 下一步

1. 按顺序推进 WP02-B012（收敛 contract 测试编排并接入 CMake）。

### 风险

1. 当前生命周期描述符基于整数枚举值集合，若后续引入字符串枚举编码，需要新增编码层映射而非改写现有守卫语义。
2. deprecated 值当前保留可读路径并通过标志位暴露，若后续需要“强阻断 deprecated 输入”，应通过新门禁开关实现，避免改变已落地兼容行为。

## 记录 #030

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B010 新增 EventEnvelope 头部对象与白名单校验器
- 状态：已完成

### 改动

1. 新增 EventEnvelope 契约对象：
   - [contracts/include/event/EventEnvelope.h](contracts/include/event/EventEnvelope.h)
   - 定义 `EventEnvelopeHeader` 与 `EventEnvelope`，头部仅承载公共元数据，模块私有信息保留在 payload。
2. 新增 EventEnvelope 白名单守卫：
   - [contracts/include/event/EventEnvelopeGuards.h](contracts/include/event/EventEnvelopeGuards.h)
   - 提供 `validate_event_envelope(...)`，校验：
     - 公共头字段必填（event_id/event_type/event_version/occurred_at_ms/request_id/trace_id）；
     - payload 载体必填（payload_type/payload_json）；
     - 头部键必须在白名单中，阻断模块私有字段上浮头部。
3. 新增 event 合同测试并接入：
   - [tests/contract/event/EventEnvelopeContractTest.cpp](tests/contract/event/EventEnvelopeContractTest.cpp)
   - [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 注册 `EventEnvelopeContractTest`。

### 测试

1. 执行验收命令（B010 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R EventEnvelopeContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 18/18 通过（含新增测试）。
   - `EventEnvelopeContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：头部仅公共字段、payload 承载私有数据时通过。
   - 负例：头部上浮私有字段 `worker_internal_state` 被拒绝。

### 结果

1. WP02-B010 达成 Done 判定：头部仅允许通用字段，payload 分层规则可自动验证。

### 下一步

1. 按顺序推进 WP02-B011（枚举降级与弃用生命周期守卫）。

### 风险

1. 当前白名单基于 header_keys 文本校验，若后续事件编解码层字段命名存在别名，需要增加别名映射层以避免误判。
2. 当前仅校验“禁止私有字段上浮头部”，后续若需要检查 payload 结构完整性，应在后续任务新增 payload 级守卫，避免扩大本任务职责。

## 记录 #029

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B009 收敛时间语义迁移与 TimeDeadline 校验器
- 状态：已完成

### 改动

1. 扩展时间兼容守卫：
   - `contracts/include/boundary/CompatibilityGuards.h`
   - 在 `TimeoutNormalizationResult` 中新增 `used_deadline_priority`，并在 `deadline_at_ms` 存在时标记 deadline 优先路径。
2. 新增 TimeDeadline 校验器：
   - `contracts/include/boundary/TimeDeadlineGuards.h`
   - 提供 `validate_time_deadline_fields(...)`：
     - 复用 timeout 归一化；
     - 保障 `timeout_seconds` 仅兼容迁移读取；
     - 当 `created_at_ms + timeout_ms` 可与 `deadline_at_ms` 同时推导时，冲突即失败。
3. 扩展/新增合同测试并接入：
   - `tests/contract/smoke/CompatibilityContractTest.cpp`（扩展）：
     - 新增 `timeout_ms` 与 `timeout_seconds` 双字段冲突负例；
     - 增加 deadline 优先路径断言。
   - `tests/contract/smoke/TimeDeadlineContractTest.cpp`（新增）：
     - 正例：deadline 与 timeout 一致时通过；
     - 负例：deadline 与 timeout 冲突时失败。
   - `tests/contract/CMakeLists.txt`：
     - compatibility 测试名对齐为 `CompatibilityContractTest`；
     - 注册 `TimeDeadlineContractTest`。

### 测试

1. 执行验收命令（B009 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "CompatibilityContractTest|TimeDeadlineContractTest" --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 17/17 通过（含新增测试）。
   - `CompatibilityContractTest` 与 `TimeDeadlineContractTest` 2/2 通过。
3. 覆盖摘要：
   - 正例：`timeout_seconds -> timeout_ms` 迁移路径可用，deadline 优先路径可验证。
   - 负例：`timeout_ms` 与 `timeout_seconds` 不一致冲突被拒绝。
   - 负例：`deadline_at_ms` 与 `created_at_ms + timeout_ms` 冲突被拒绝。

### 结果

1. WP02-B009 达成 Done 判定：`timeout_seconds` 仅兼容读取、双字段冲突可失败、`deadline_at` 优先规则可自动验证。

### 下一步

1. 按顺序推进 WP02-B010（EventEnvelope 头部对象与白名单校验器）。

### 风险

1. 当前冲突判定依赖 `created_at_ms` 可用；若上游出现缺失 `created_at_ms` 但同时提供 deadline 与 timeout 的输入，系统会按“deadline 优先”通过，后续若要强约束需在新任务中显式冻结。
2. compatibility 测试名已与 B009 验收命令对齐；若外部脚本仍依赖旧测试名，需要同步更新脚本以避免误报漏测。

## 记录 #028

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B008 新增统一标识元数据对象与传播校验器
- 状态：已完成

### 改动

1. 新增统一标识元数据对象与传播校验器：
   - `contracts/include/boundary/IdentityMetadata.h`
   - 定义 `IdentityMetadata`，统一承载 request/session/trace/task/lease 五类 ID 与 `parent_task_id`。
   - 提供 `validate_identity_metadata(...)`，校验五类 ID 必填、child task 必须携带 `parent_task_id`、root task 禁止携带 `parent_task_id`、以及 `parent_task_id != task_id`。
2. 新增 smoke 合同测试并接入：
   - `tests/contract/smoke/IdentityMetadataContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `IdentityMetadataContractTest`。

### 测试

1. 执行验收命令（B008 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R IdentityMetadataContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 16/16 通过（含新增测试）。
   - `IdentityMetadataContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：child task 场景下五类 ID 齐全且 parent_task_id 合法时通过。
   - 负例：child task 缺失 `parent_task_id` 被拒绝。
   - 负例：`parent_task_id` 与 `task_id` 自引用相等被拒绝。

### 结果

1. WP02-B008 达成 Done 判定：五类 ID 与 `parent_task_id` 传播关系可程序化校验且测试通过。

### 下一步

1. 按顺序继续推进 WP02-B009（收敛时间语义迁移与 TimeDeadline 校验器）。

### 风险

1. 当前传播校验依赖 `is_child_task` 语义开关，若后续系统改为通过任务拓扑自动推断父子关系，需要新增兼容入口而非改写现有字段语义。
2. 目前仅约束 parent 直接引用关系，若后续引入多级链路完整性校验（祖先追溯），应新增独立守卫，避免放大当前最小契约责任。

## 记录 #027

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B007 新增 BudgetSnapshot 契约对象与一致性校验器
- 状态：已完成

### 改动

1. 新增 BudgetSnapshot 契约对象：
   - `contracts/include/checkpoint/BudgetSnapshot.h`
   - 定义 `BudgetType`、`BudgetSnapshotEntry`、`BudgetSnapshot`，覆盖 current/max/remaining/reject_reason 统一表达。
2. 新增一致性校验器：
   - `contracts/include/checkpoint/BudgetSnapshotGuards.h`
   - 提供 `validate_budget_snapshot(...)`，校验：
     - remaining 必须等于 max-current；
     - reject_reason 仅在 remaining<0 时填写；
     - 同一快照中 budget_type 唯一。
3. 新增 checkpoint 合同测试并接入：
   - `tests/contract/checkpoint/BudgetSnapshotContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `BudgetSnapshotContractTest`。

### 测试

1. 执行验收命令（B007 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R BudgetSnapshotContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 15/15 通过（含新增测试）。
   - `BudgetSnapshotContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：合法快照通过（含非超限和超限条目）。
   - 负例：remaining 与 max-current 不一致被拒绝。
   - 负例：未超限却填写 reject_reason 被拒绝。

### 结果

1. WP02-B007 达成 Done 判定：remaining 不一致和 reject_reason 误填可被稳定拦截，合法快照通过。

### 下一步

1. 按顺序推进 WP02-B008（统一标识元数据对象与传播校验器）。

### 风险

1. 当前 `remaining` 使用有符号值表达超限（可负值）；若后续输出通道限制为无符号，需要新增兼容映射字段，避免改写当前语义。
2. 目前只做单快照一致性约束，后续若引入连续快照趋势判断，应新增规则而非更改现有判定口径。

## 记录 #026

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B006 新增 RuntimeBudget 契约对象与阈值校验器
- 状态：已完成

### 改动

1. 新增 RuntimeBudget 契约对象：
   - `contracts/include/checkpoint/RuntimeBudget.h`
   - 冻结五维预算字段：max_tokens、max_turns、max_tool_calls、max_latency_ms、max_replan_count。
2. 新增 RuntimeBudget 校验器：
   - `contracts/include/checkpoint/RuntimeBudgetGuards.h`
   - 提供 `validate_runtime_budget(...)`，校验五维必填与正阈值约束。
3. 新增 checkpoint 合同测试并接入：
   - `tests/contract/checkpoint/RuntimeBudgetContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `RuntimeBudgetContractTest`。

### 测试

1. 执行验收命令（B006 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RuntimeBudgetContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 14/14 通过（含新增测试）。
   - `RuntimeBudgetContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：五维字段齐全且均为正值时通过。
   - 负例：缺失 `max_turns` 被拒绝。
   - 负例：`max_latency_ms=0`（ms 口径无效阈值）被拒绝。

### 结果

1. WP02-B006 达成 Done 判定：max_tokens/max_turns/max_tool_calls/max_latency_ms/max_replan_count 均可校验且测试通过。

### 下一步

1. 按顺序推进 WP02-B007（BudgetSnapshot 契约对象与一致性校验器）。

### 风险

1. 当前守卫将五维阈值统一约束为 >0；若后续存在“某维允许 0 表示禁用”的策略，需通过新增策略字段承载，避免改写既有字段语义。
2. 历史实现若仍使用 `max_rounds` 命名，后续集成需要兼容映射层以避免命名切换带来的 breaking 风险。

## 记录 #025

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B005 新增 ErrorSource 结构与引用校验器
- 状态：已完成

### 改动

1. 新增 ErrorSource 引用结构：
   - `contracts/include/error/ErrorSourceRef.h`
   - 定义 `ErrorSourceRefEntry` 与 `ErrorSourceRefSet`，支持 primary + related 语义。
2. 新增 ErrorSource 校验器：
   - `contracts/include/error/ErrorSourceGuards.h`
   - 提供 `validate_error_source_refs(...)`，校验 primary 唯一、四类 ref_type、ref_id 非空。
3. 新增 error 合同测试并接入：
   - `tests/contract/error/ErrorSourceContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `ErrorSourceContractTest`。

### 测试

1. 执行验收命令（B005 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ErrorSourceContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 13/13 通过（含新增测试）。
   - `ErrorSourceContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：四类引用 observation/tool_call/worker_task/checkpoint 全覆盖且单 primary 通过。
   - 负例：multiple primary 被拒绝。
   - 负例：空 ref_id 被拒绝。

### 结果

1. WP02-B005 达成 Done 判定：四类引用全覆盖且非法输入可被稳定拦截。

### 下一步

1. 按顺序推进 WP02-B006（RuntimeBudget 契约对象与阈值校验器）。

### 风险

1. 当前模型允许 related 列表无序，若后续审计链路要求严格时序，需要在不破坏现有结构前提下新增序号或时间戳字段。
2. `ErrorInfo` 仍保留 B004 最小 `source_ref` 表达，后续若对接 B005 结构化集合，需通过兼容层渐进迁移，避免直接替换造成 breaking。

## 记录 #024

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B004 新增 ErrorInfo 与最小校验器
- 状态：已完成

### 改动

1. 新增 ErrorInfo 契约对象：
   - `contracts/include/error/ErrorInfo.h`
   - 定义五个必填顶层字段对应承载：failure_type、retryable、safe_to_replan、details、source_ref。
2. 新增最小校验器：
   - `contracts/include/error/ErrorInfoGuards.h`
   - 提供 `validate_error_info_required_fields(...)` 与 `is_supported_error_source_ref_type(...)`。
3. 新增 error 合同测试并接入：
   - `tests/contract/error/ErrorInfoContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `ErrorInfoContractTest`。

### 测试

1. 执行验收命令（B004 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ErrorInfoContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 12/12 通过（含新增测试）。
   - `ErrorInfoContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：五个必填字段齐全时通过。
   - 负例：缺失 `failure_type` 被拒绝。
   - 负例：`source_ref.ref_type` 非法取值被拒绝。

### 结果

1. WP02-B004 达成 Done 判定：failure_type/retryable/safe_to_replan/details/source_ref 缺一即失败，合法样例通过。

### 下一步

1. 按顺序推进 WP02-B005（ErrorSource 结构与引用校验器）。

### 风险

1. 当前 `source_ref` 仅实现最小键约束，B005 若引入更强引用结构需保持向后兼容，避免语义重解释。
2. `retryable` 与 `safe_to_replan` 当前只表达候选语义，后续实现层若把它们当作“已执行动作”会偏离 ADR-007，需要在集成层加门禁。

## 记录 #023

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B003 新增 ResultCode 分类与判定枚举
- 状态：已完成

### 改动

1. 新增 ResultCode 分类头文件：
   - `contracts/include/error/ResultCode.h`
   - 定义五类一级域：validation/policy/tool/provider/runtime。
2. 新增分类判定辅助能力：
   - `classify_result_code_segment(...)` 按编码段判定分类。
   - `classify_result_code(...)` 对枚举值执行分类。
   - `classify_result_code_value(...)` 对 raw code 执行 gate 友好判定（含 unknown 拒绝）。
3. 新增 error 目录合同测试并接入：
   - `tests/contract/error/ResultCodeContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `ResultCodeContractTest`

### 测试

1. 执行验收命令（B003 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ResultCodeContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 11/11 通过（含新增测试）。
   - `ResultCodeContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：五类枚举样例稳定映射到 validation/policy/tool/provider/runtime。
   - 边界例：3999 归 tool、4000 归 provider。
   - 负例：7000（越界码）被拒绝并判定为 unknown。

### 结果

1. WP02-B003 达成 Done 判定：五类失败域判定可程序化复现且边界负例通过。

### 下一步

1. 按顺序推进 WP02-B004（ErrorInfo 与最小校验器）。

### 风险

1. 当前实现采用分段分类，后续扩展具体码值时需保持段边界稳定，避免跨段重解释导致 breaking 风险。
2. 若未来新增一级分类，将触发兼容性重大变更，应走专门评审，不应在当前段内硬塞。

## 记录 #022

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B002 新增字段演进兼容判定辅助器
- 状态：已完成

### 改动

1. 新增字段演进兼容判定头文件：
   - `contracts/include/boundary/FieldEvolutionGuards.h`
   - 提供 `FieldEvolutionDecision`（non-breaking/review-required/breaking）与 `FieldEvolutionResult`。
2. 新增三类字段演进判定辅助器：
   - `classify_type_evolution(...)`（B1）
   - `classify_optionality_evolution(...)`（B2）
   - `classify_cardinality_evolution(...)`（B3）
3. 新增 contract 测试并接入：
   - `tests/contract/smoke/FieldEvolutionGuardsContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `FieldEvolutionGuardsContractTest`

### 测试

1. 执行验收命令（B002 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R FieldEvolutionGuardsContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 10/10 通过（含新增测试）。
   - `FieldEvolutionGuardsContractTest` 1/1 通过。
3. 覆盖摘要：
   - non-breaking：类型并行新增字段且保留旧语义。
   - review-required：单值扩多值但缺少消费兼容证据。
   - breaking：既有可选字段改为强制。

### 结果

1. WP02-B002 达成 Done 判定：non-breaking/review-required/breaking 三类判定可程序化复现，断言全通过。

### 下一步

1. 按顺序推进 WP02-B003（ResultCode 分类与判定枚举）。

### 风险

1. 当前判定器是字段属性层规则，若后续引入“对象职责边界变化”场景，需由上层 checklist（A3/A5）补充门禁，避免误判为字段级变更。
2. `single->multi` 的 non-breaking 依赖“消费方兼容证据”输入，若证据口径不统一，可能导致 review-required 漏判；后续可在 B013 统一证据模板。

## 记录 #021

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B001 新增横切基础对象总入口头文件
- 状态：已完成

### 改动

1. 新增横切基础对象聚合入口头文件：
   - `contracts/include/boundary/CrossCuttingContracts.h`
   - 统一暴露五类入口：error/event/checkpoint/id-time/enum。
2. 新增 WP02-B001 对应 smoke 合同测试：
   - `tests/contract/smoke/CrossCuttingContractsSmokeTest.cpp`
   - 正例：聚合头可统一访问 error/event/checkpoint/time 入口并完成时间归一化。
   - 负例：未知枚举值通过聚合入口降级到 `Unspecified`。
3. 更新 contract 测试注册：
   - `tests/contract/CMakeLists.txt`
   - 新增 `CrossCuttingContractsSmokeTest` 注册，纳入 `dasall_contract_tests` 聚合链路。

### 测试

1. 执行验收命令（B001 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R CrossCuttingContractsSmokeTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 9/9 通过（含新增测试）。
   - `CrossCuttingContractsSmokeTest` 1/1 通过。

### 结果

1. WP02-B001 达成 Done 判定：聚合头已覆盖 error/event/checkpoint/id-time/enum 五类入口，且测试链路可执行并通过。

### 下一步

1. 按 WP-02 执行顺序推进 WP02-B002（字段演进兼容判定辅助器）。

### 风险

1. 当前 event 入口为阶段性 marker（字段 schema 仍待 WP02-B010），后续落地 EventEnvelope 时需保持聚合入口 API 稳定。
2. 枚举降级路径复用了 CompatibilityGuards，若后续引入生命周期守卫，需要在 WP02-B011 增补组合负例防回退。

## 记录 #020

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B007 收敛 contracts 测试入口并接入 CMake
- 状态：已完成

### 改动

1. 收敛 contract 测试注册入口：
   - `tests/contract/CMakeLists.txt`
   - 新增 `dasall_register_contract_test(...)` 统一封装 `add_executable`、`add_test`、`LABELS=contract`。
2. 收敛 contract 聚合目标依赖：
   - `tests/CMakeLists.txt`
   - `dasall_contract_tests` 改为依赖 `DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS` 统一列表，避免分散手工维护。
3. 增加注册空列表防护（负向守卫）：
   - 当收敛列表为空时，配置阶段 `FATAL_ERROR`，阻断“脚本通过但测试未注册”风险。

### 测试

1. 执行验收命令（B007 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 8/8 通过。
3. 发现性正反校验（B007 证据补充）：
   - 正例：`ctest --test-dir build-ci -N -L contract` -> `Total Tests: 8`，包含 WP01 边界测试。
   - 负例：`ctest --test-dir build-ci -N -R DefinitelyMissingContractTest` -> `Total Tests: 0`。

### 结果

1. WP01-B007 达成 Done 判定：contract 测试入口已收敛，且 ctest 可发现性与标签接入可验证。

### 下一步

1. 若后续新增边界回归测试，同步更新门禁脚本 required tests 列表并复验 gate。

### 风险

1. 统一注册函数若被绕过（直接新增 add_test 且漏 label），可能导致 gate 漏检；需在评审中强制走注册函数。
2. 当前空列表防护在 configure 阶段触发，若未来存在按 profile 裁剪测试的需求，需要同步定义白名单策略。

## 记录 #019

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B009 增加协同语义回归组合测试
- 状态：已完成

### 改动

1. 扩展协同语义 contract 测试：
   - `tests/contract/smoke/MultiAgentBoundaryContractTest.cpp`
2. 新增组合回归矩阵用例 `test_multi_agent_semantics_combination_regression_matrix`：
   - 合法组合（3 组）：
     - MultiAgentRequest: `goal_fragment`（允许）
     - MultiAgentResult: `merged_result`（允许）
     - WorkerTask: `lease_id`（允许）
   - 非法组合（3 组）：
     - MultiAgentRequest: `agent_request`（拒绝）
     - MultiAgentResult: `agent_result`（拒绝）
     - WorkerTask: `global_fsm_state`（拒绝）
3. 断言强化：
   - 对越权矩阵中每组样本同时断言 `allowed`、`decision`、`reason`，确保分层阻断行为可追溯。

### 测试

1. 执行验收命令（B009 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MultiAgentBoundaryContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `MultiAgentBoundaryContractTest` 1/1 通过。
3. 覆盖说明：
   - 满足 B009 完成判定：Request/Result/WorkerTask 三组对象的越权矩阵断言全通过。

### 结果

1. WP01-B009 达成 Done 判定：协同语义“全局主控/协同子域分层”在组合场景下具备可执行回归保护。

### 下一步

1. 按顺序推进 WP01-B007（收敛 contracts 测试入口并接入 CMake，补齐 ctest 发现性证据）。

### 风险

1. 当前越权矩阵仍以字段名边界为主，若后续出现语义别名字段，需要补充矩阵覆盖。
2. reason 断言为精确字符串匹配，若后续守卫文案规范调整，需要同步更新断言预期。

## 记录 #018

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B008 增加恢复语义回归组合测试
- 状态：已完成

### 改动

1. 扩展恢复语义 contract 测试：
   - `tests/contract/smoke/RecoveryBoundaryContractTest.cpp`
2. 新增组合回归矩阵用例 `test_recovery_semantics_combination_regression_matrix`：
   - 合法组合（1 组）：
     - ReflectionDecision: `decision_kind`（允许）
     - RecoveryOutcome: `executed_action`（允许）
   - 非法组合（3 组）：
     - ReflectionDecision: `retry_after_ms`（拒绝）
     - ReflectionDecision: `backoff_strategy`（拒绝）
     - RecoveryOutcome: `failure_root_cause`（拒绝）
3. 断言强化：
   - 对每组组合同时断言 `allowed`、`decision`、`reason`，保证阻断行为与归一化原因文本可追溯。

### 测试

1. 执行验收命令（B008 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RecoveryBoundaryContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `RecoveryBoundaryContractTest` 1/1 通过。
3. 覆盖说明：
   - 满足 B008 完成判定：至少 1 组合法 + 3 组非法组合断言全部通过。

### 结果

1. WP01-B008 达成 Done 判定：恢复语义“建议权/执行权分层”在组合场景下具备可执行回归保护。

### 下一步

1. 按顺序推进 WP01-B009（协同语义回归组合测试）。

### 风险

1. 当前组合回归覆盖的是字段名边界语义；若后续引入语义等价别名字段，需同步补充矩阵样本。
2. 目前 reason 断言为精确字符串匹配，若未来规范化文案调整，需同步更新测试预期。

## 记录 #017

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B010 固化 WP01 M1 本地与 CI 门禁脚本入口
- 状态：已完成

### 改动

1. 新增 WP01 门禁脚本：
   - `scripts/ci/wp01_contract_gate.sh`
2. 脚本职责（对齐 WP01-T013 M1 Gate）：
   - 执行 configure：`cmake -S <root> -B <build-ci>`。
   - 执行 build：`cmake --build <build-ci> --target dasall_contract_tests`。
   - 执行注册校验：`ctest -N -L contract` 并强制检查关键边界测试注册存在（ContextPacketBoundaryContractTest / RecoveryBoundaryContractTest / MultiAgentBoundaryContractTest）。
   - 执行 gate：`ctest --test-dir <build-ci> -L contract --output-on-failure`。
3. 新增失败闭锁机制：
   - 任一关键 contract 测试未注册时，脚本输出 missing 项并返回非 0。
   - 支持通过环境变量 `WP01_GATE_REQUIRED_TESTS` 覆盖必需测试名列表，用于 CI 场景定制与负路径验证。

### 测试

1. 执行验收命令（B010 原样）：
   - `bash scripts/ci/wp01_contract_gate.sh`
2. 结果：
   - configure 成功。
   - build 成功。
   - 注册校验通过。
   - contract label 测试 8/8 通过。
3. 负路径验证（失败闭锁）：
   - 命令：`WP01_GATE_REQUIRED_TESTS=DefinitelyMissingContractTest bash scripts/ci/wp01_contract_gate.sh`
   - 结果：脚本返回 `NEGATIVE_RC=1`，并输出 missing required contract test registration。

### 结果

1. WP01-B010 达成 Done 判定：脚本在正常路径返回 0，并能在边界回归缺失注册时返回非 0。

### 下一步

1. 按顺序推进 WP01-B008（恢复语义回归组合测试）。

### 风险

1. 当前关键测试注册检查聚焦 WP01 三类边界核心用例，若后续新增强制边界测试，需同步更新 `WP01_GATE_REQUIRED_TESTS` 默认列表。
2. 在不同 CTest 版本下 `ctest -N` 输出格式可能存在细微差异，若格式变化导致解析误判，需要补充更稳健的解析规则。

## 记录 #016

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B006 校验协同语义分层守卫
- 状态：已完成

### 改动

1. 新增协同语义边界守卫头文件：
   - `contracts/include/boundary/MultiAgentBoundaryGuards.h`
   - 提供 `MultiAgentBoundaryDecision`、`MultiAgentBoundaryResult`、
     `kMultiAgentRequestForbiddenFields`、`kMultiAgentResultForbiddenFields`、
     `kWorkerTaskGlobalStateForbiddenFields`、
     `evaluate_multi_agent_request_field_boundary`、
     `evaluate_multi_agent_result_field_boundary`、
     `evaluate_worker_task_field_boundary`。
2. 守卫规则来源：
   - 对齐 ADR-008 与 WP01-T011，落实三类越权阻断：
     - MultiAgentRequest 不得复用 AgentRequest 语义。
     - MultiAgentResult 不得替代 AgentResult 语义。
     - WorkerTask 不得承载全局 Session/FSM 状态语义。
3. 新增 contract 测试并接入：
   - `tests/contract/smoke/MultiAgentBoundaryContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `MultiAgentBoundaryContractTest`
   - `tests/CMakeLists.txt` 将 `dasall_contract_multi_agent_boundary_test` 纳入 `dasall_contract_tests` 依赖。

### 测试

1. 执行验收命令（B006 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MultiAgentBoundaryContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `MultiAgentBoundaryContractTest` 1/1 通过。
   - `dasall_contract_tests` 聚合链路 contract tests 8/8 通过。
3. 正负例覆盖：
   - 正例：`goal_fragment`、`merged_result`、`lease_id` 允许通过守卫。
   - 负例：`agent_request`、`agent_result`、`global_fsm_state` 均被守卫拒绝。

### 结果

1. WP01-B006 达成 Done 判定：三类协同语义越权场景全部被自动校验阻断。

### 下一步

1. 按执行顺序推进 WP01-B007（收敛 contracts 测试入口并接入 CMake）。

### 风险

1. 当前策略为字段名边界守卫，若后续引入语义等价别名字段，需要补充规则与回归用例。
2. 若后续通过嵌套结构隐式承载全局态，需要在 WP01-B009 组合回归阶段加强覆盖。

## 记录 #015

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B005 校验恢复语义分层守卫
- 状态：已完成

### 改动

1. 新增恢复语义边界守卫头文件：
   - `contracts/include/boundary/RecoveryBoundaryGuards.h`
   - 提供 `RecoveryBoundaryDecision`、`RecoveryBoundaryResult`、
     `kReflectionSchedulingForbiddenFields`、`kRecoveryAttributionForbiddenFields`、
     `evaluate_reflection_decision_field_boundary`、`evaluate_recovery_outcome_field_boundary`。
2. 守卫规则来源：
   - 对齐 ADR-007 与 WP01-T010，明确 ReflectionDecision 禁入运行时调度字段，RecoveryOutcome 禁入失败归因语义字段。
3. 新增 contract 测试并接入：
   - `tests/contract/smoke/RecoveryBoundaryContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `RecoveryBoundaryContractTest`
   - `tests/CMakeLists.txt` 将 `dasall_contract_recovery_boundary_test` 纳入 `dasall_contract_tests` 依赖。

### 测试

1. 执行验收命令（B005 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RecoveryBoundaryContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `RecoveryBoundaryContractTest` 1/1 通过。
   - `dasall_contract_tests` 聚合链路 contract tests 7/7 通过。
3. 正负例覆盖：
   - 正例：`decision_kind` 可进入 ReflectionDecision；`executed_action` 可进入 RecoveryOutcome。
   - 负例：`retry_after_ms` 在 ReflectionDecision 被拒绝；`failure_root_cause` 在 RecoveryOutcome 被拒绝。

### 结果

1. WP01-B005 达成 Done 判定：ReflectionDecision 的调度字段误入与 RecoveryOutcome 的归因字段误入均被守卫阻断。

### 下一步

1. 按执行顺序推进 WP01-B006（协同语义分层守卫）。

### 风险

1. 当前为字段名显式黑名单策略，若后续出现语义等价别名字段，需要补充规则与回归用例。
2. 若后续将复杂归因对象以嵌套字段形式注入 RecoveryOutcome，需要在 WP01-B008 回归阶段强化防护。

## 记录 #014

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B004 校验 ContextPacket 禁入字段守卫
- 状态：已完成

### 改动

1. 新增 ContextPacket 边界守卫头文件：
   - `contracts/include/boundary/ContextBoundaryGuards.h`
   - 提供 `ContextBoundaryDecision`（AllowField/RejectForbiddenField）、`ContextBoundaryResult`、`kForbiddenContextFields`、`evaluate_context_field_boundary`、`is_allowed_context_field`。
2. 守卫规则来源：
   - 对齐 ADR-006 与 WP01-T009，仅做字段名禁入校验，拒绝 `final_messages`、`provider_payload`、`rendered_prompt`，不扩张到字段级 schema 设计。
3. 新增 contract 测试并接入：
   - `tests/contract/smoke/ContextPacketBoundaryContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `ContextPacketBoundaryContractTest`
   - `tests/CMakeLists.txt` 将 `dasall_contract_context_packet_boundary_test` 纳入 `dasall_contract_tests` 依赖。

### 测试

1. 执行验收命令（B004 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ContextPacketBoundaryContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `ContextPacketBoundaryContractTest` 1/1 通过。
   - `dasall_contract_tests` 聚合链路 contract tests 6/6 通过。
3. 正负例覆盖：
   - 正例：`recent_history` 允许通过守卫。
   - 负例：`final_messages`、`provider_payload`、`rendered_prompt` 均被守卫拒绝。

### 结果

1. WP01-B004 达成 Done 判定：三项禁入字段全部被阻断，合法字段未被误杀。

### 下一步

1. 按执行顺序推进 WP01-B005（恢复语义分层守卫）。

### 风险

1. 当前实现是字段名精确匹配守卫，若后续引入别名或大小写变体策略，需要在不改变 ADR 结论前提下补充统一规范与测试。
2. 若后续把 provider 或消息层字段通过嵌套对象间接引入 ContextPacket，需要在 WP01-B007/B008 门禁中继续强化覆盖。

## 记录 #013

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B003 新增 Blocked/Deferred 外溢守卫接口
- 状态：已完成

### 改动

1. 新增边界守卫头文件：
   - `contracts/include/boundary/BoundaryGuards.h`
   - 提供 `BoundaryGuardDecision`（AllowStable/RejectBlocked/RejectDeferred）、`BoundaryGuardResult`、`evaluate_stable_boundary`、`can_enter_stable_boundary`。
2. 守卫逻辑来源：
   - 直接复用 `ObjectBoundaryCatalog` 的 Stable/Blocked/Deferred 分类，不新增字段级判定规则。
3. 新增 contract 测试并接入：
   - `tests/contract/smoke/BoundaryGuardsContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `BoundaryGuardsContractTest`
   - `tests/CMakeLists.txt` 将 `dasall_contract_boundary_guards_test` 纳入 `dasall_contract_tests` 依赖。

### 测试

1. 执行验收命令（B003 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R BoundaryGuardsContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `BoundaryGuardsContractTest` 1/1 通过。
   - `dasall_contract_tests` 聚合链路 contract tests 5/5 通过。
3. 正负例覆盖：
   - 正例：Stable 对象 `AgentRequest` 被允许进入 Stable 边界。
   - 负例：Blocked 对象 `MemoryEvidence` 被拒绝，Deferred 对象 `ToolRequest` 被拒绝。

### 结果

1. WP01-B003 达成 Done 判定：Blocked/Deferred 对象均被守卫拒绝进入 Stable 清单。

### 下一步

1. 按执行顺序推进 WP01-B004（ContextPacket 禁入字段守卫）。

### 风险

1. 当前守卫仅覆盖对象级边界，若后续误把字段级语义塞入该守卫，会造成 WP 边界越界。
2. Deferred 对象在 WP-05 复审后可能调整判定，需保证守卫与冻结结论同步演进。

## 记录 #012

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B002 补齐 Stable 对象编译期标识与最小占位类型
- 状态：已完成

### 改动

1. 新增 14 个 Stable 对象 Tag 头文件（仅命名与类型标识，不定义字段语义）：
   - agent: `AgentRequestTag.h`、`GoalContractTag.h`、`ActionDecisionTag.h`、`AgentResultTag.h`、`MultiAgentRequestTag.h`、`MultiAgentResultTag.h`
   - context: `ContextPacketTag.h`
   - observation: `ObservationTag.h`、`ObservationDigestTag.h`、`ErrorInfoTag.h`
   - checkpoint: `CheckpointTag.h`、`ReflectionDecisionTag.h`、`RecoveryOutcomeTag.h`
   - task: `WorkerTaskTag.h`
2. 新增 contract 测试：
   - `tests/contract/smoke/StableTypePresenceContractTest.cpp`
   - 覆盖正例：14 个 Stable 占位类型可 include 且为空类型，且与 Stable 名册一致。
   - 覆盖负例：`MemoryEvidence`（Blocked）与 `ToolRequest`（Deferred）不得被判定为 Stable。
3. 更新测试接入：
   - `tests/contract/CMakeLists.txt` 新增 `StableTypePresenceContractTest`。
   - `tests/CMakeLists.txt` 将 `dasall_contract_stable_type_presence_test` 加入 `dasall_contract_tests` 依赖。

### 测试

1. 执行验收命令（B002 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R StableTypePresenceContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `StableTypePresenceContractTest` 1/1 通过。
   - `dasall_contract_tests` 聚合链路中 contract tests 4/4 通过。

### 结果

1. WP01-B002 达成 Done 判定：14 个 Stable 名称均具备可 include 的占位类型，且未引入字段语义。

### 下一步

1. 按执行顺序推进 WP01-B003（Blocked/Deferred 外溢守卫接口）。

### 风险

1. 当前仅完成对象级 Tag，占位层与后续守卫层之间仍可能出现“名称一致但行为未绑定”的漂移风险。
2. 若后续任务误在 Tag 头文件中添加字段，可能跨入 WP-02/03/04 范围并引入 breaking 风险；需继续以 contract tests 约束“空类型”不变式。

## 记录 #011

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B001 新增对象边界名册与分类枚举（复验闭环）
- 状态：已完成

### 改动

1. 沿用已落盘代码与测试产物完成复验闭环：
   - `contracts/include/boundary/ObjectBoundaryCatalog.h`
   - `tests/contract/smoke/ObjectBoundaryCatalogContractTest.cpp`
2. 依赖 WP01-B011 的 CTest 兼容修复后，恢复 B001 验收命令可执行性。

### 测试

1. 执行验收命令（B001 定义原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure`
2. 结果：
   - contract tests 3/3 通过：
     - `dasall_contract_smoke_test`
     - `dasall_contract_compatibility_test`
     - `dasall_contract_object_boundary_catalog_test`

### 结果

1. WP01-B001 从 Blocked 更新为 Done。
2. 满足 B001 完成判定：14 个 Stable、13 个 Blocked、2 个 Deferred 可枚举且测试通过。

### 下一步

1. 按执行顺序推进 WP01-B002（Stable 对象编译期标识与最小占位类型）。

### 风险

1. 当前 contract 用例数量仍偏少，后续若新增边界守卫规则需同步扩展回归测试，防止边界枚举与守卫实现漂移。

## 记录 #010

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B011 解阻 CMake 配置并恢复 contract tests 可执行性
- 状态：已完成

### 改动

1. 新增 CTest 兼容入口文件：
   - `CTestTestfile.cmake`
   - 作用：适配当前环境 CTest 3.16 不支持 `--test-dir` 的行为差异，确保在仓库根目录执行 `ctest --test-dir build-ci` 时仍可回溯到 `build-ci` 的测试图。
2. 保持最小修复边界：
   - 未改写 ADR 结论。
   - 未扩张到 WP-02/WP-03 任务范围。
   - 未新增业务语义代码，仅修复测试发现路径。

### 测试

1. 验收命令（任务定义原样执行）：
   - `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure`
2. 正例结果：
   - configure 成功。
   - build 成功。
   - ctest 执行 contract tests 3/3 通过（`dasall_contract_smoke_test`、`dasall_contract_compatibility_test`、`dasall_contract_object_boundary_catalog_test`）。
3. 负例验证：
   - 修复前（记录 #009 证据）同命令尾部会出现 `No tests were found!!!`，导致验收链不可闭环。
   - 修复后同命令可稳定发现并执行 contract tests，负例场景已消失。

### 结果

1. WP01-B011 解阻完成，状态可从 Blocked 更新为 Done。
2. B001~B010 的公共前置“contract tests 可执行”已恢复。

### 下一步

1. 回到 WP01-B001，基于已解阻环境复核并更新其状态证据。
2. 按执行顺序推进 WP01-B002（Stable 对象编译期标识与最小占位类型）。

### 风险

1. 本次采用 CTest 兼容入口文件属于“工具链兼容补丁”，若后续升级到支持 `--test-dir` 的 CTest 版本，需要确认该入口不会造成重复发现或路径歧义。
2. 若后续改变默认构建目录名称（非 `build-ci`），需同步更新该兼容入口或改为由统一脚本注入。

## 记录 #009

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B001 新增对象边界名册与分类枚举
- 状态：Blocked

### 改动

1. 新增对象边界名册头文件：
   - `contracts/include/boundary/ObjectBoundaryCatalog.h`
   - 落盘 Stable/Blocked/Deferred 三层分类与 29 个对象名册（14/13/2）。
2. 新增契约测试：
   - `tests/contract/smoke/ObjectBoundaryCatalogContractTest.cpp`
   - 覆盖正例（计数与 Stable 命名）和负例（Blocked 不可误判 Stable、Deferred 不可误判 Blocked）。
3. 更新测试注册：
   - `tests/contract/CMakeLists.txt` 新增 `dasall_contract_object_boundary_catalog_test`。
   - `tests/CMakeLists.txt` 更新 `dasall_contract_tests` 依赖，确保聚合目标会构建新增测试可执行文件。

### 测试

1. 执行验收命令：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure`
2. 结果摘要：
   - `dasall_contract_tests` 内部执行的 contract tests 为 3/3 通过（含新增 `dasall_contract_object_boundary_catalog_test`）。
   - 随后的独立 `ctest --test-dir build-ci -L contract` 在当前环境输出 `No tests were found!!!`。

### 结果

1. 代码与测试实现完成，且新增测试可编译并可在聚合目标内通过。
2. 由于验收链尾部命令在当前环境无法发现测试，按 Build TODO 规则将 WP01-B001 标记为 Blocked。

### 下一步

1. 先解阻 `ctest --test-dir build-ci` 可发现测试的问题（建议纳入 WP01-B011 解阻链处理）。
2. 解阻后复跑 WP01-B001 验收命令并将状态从 Blocked 更新为 Done。

### 风险

1. 若忽略该环境差异直接标记 Done，会导致“同一验收命令在不同环境结果不一致”的门禁漂移。
2. 本次为保证验收可执行性触及 `tests/CMakeLists.txt` 聚合依赖，存在轻微跨任务边界风险，后续需在 WP01-B007 统一收敛测试编排。

## 记录 #008

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-02 收束 + WP-03 启动）
- 任务：修正“仅 Design 输出”偏差，补齐 Build 落地基线与执行约束
- 状态：进行中

### 完成内容

1. 明确并记录决策偏差：
   - 识别出“按强 design 约束推进时，任务可在文档层通过但缺少 build 落盘证据”的过程问题。
   - 形成统一结论：后续任务采用“Design 先行 + 分批 Build 验证”模式，禁止全量设计后一次性回补实现。
2. 新设计并落地两份 Build TODO 相关文档：
   - 完成 B1 build 向文档：`WP02-T015-B1-timeout迁移清单.md`（迁移映射、冲突判定、弃用窗口、回退策略）。
   - 完成 B2 build 向文档：`WP02-T015-B2-枚举降级契约测试基线.md`（unknown->Unspecified 证据基线）。
3. 完成 Build 落盘与验证闭环：
   - 新增兼容辅助代码与契约测试：`CompatibilityGuards.h`、`CompatibilityContractTest.cpp`。
   - 清理历史 `build-ci` 缓存路径冲突后，完成构建与 contract tests 执行。
   - `dasall_contract_compatibility_test` 执行通过，B2 由 In Review 转 Closed。
4. 完成冻结状态同步：
   - WP02-T015 M2 冻结包从 CONDITIONAL FREEZE 收束为 FROZEN。
   - WP-02 看板 T015 状态更新为 Done。
   - WP03-T001 解除 Blocked 并转 In Review（前置依赖闭环）。
5. 新增流程模板资产：
   - 在 `docs/development/` 新增 Build TODO 生成提示词模板，用于后续任务强制输出“代码+测试+验收命令”三件套。

### 关键产物

- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP02-T015-B1-timeout迁移清单.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP02-T015-B2-枚举降级契约测试基线.md`
- `/home/gangan/DASALL-Agent/contracts/include/boundary/CompatibilityGuards.h`
- `/home/gangan/DASALL-Agent/tests/contract/smoke/CompatibilityContractTest.cpp`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP02-T015-M2冻结包.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/WP-02-横切基础对象TODO.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP03-T001-主链路对象依赖表.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/WP-03-主链路对象TODO.md`
- `/home/gangan/DASALL-Agent/docs/development/Build开发任务TODO生成提示词模板.md`

### 验证结果

1. `bash scripts/ci/build.sh` 通过（修复历史 cache 路径冲突后）。
2. `bash scripts/ci/contract_tests.sh` 通过，`dasall_contract_compatibility_test` 通过并留档。
3. 相关更新文档、头文件、测试文件均通过文件级错误检查（No errors found）。
4. WP02-T015 与 WP03-T001 状态同步一致，无“文档结论与看板状态”漂移。

### 中断恢复点（下次会话从这里继续）

- WP-02 已冻结完成（M2=FROZEN，T015=Done）。
- WP-03 已解除前置阻塞，当前从 T002/T003 继续推进“Design+Build 并行落地”。
- 建议优先顺序：
  - `docs/todos/contracts/WP-03-主链路对象TODO.md`
  - `docs/todos/contracts/deliverables/WP03-T002-AgentRequest语义说明.md`
  - `docs/todos/contracts/deliverables/WP03-T003-AgentRequest字段表.md`
  - `tests/contract/smoke/`（同步新增 WP-03 契约测试）

### 风险/注意事项

- 若后续再次只产出 design 文档而不落盘 build 证据，WP-03/WP-04 将累计实现债务并放大返工成本。
- 需将“代码+测试+验收命令”作为应有 build 任务的硬门槛，未满足不得标记 Done。
- 新增 build 任务应继续遵守 M2 Gate，不得回退横切语义冻结结论。

## 记录 #007

- 日期：2026-03-14
- 阶段：contracts 冻结（WP-02 横切基础对象）
- 任务：收束 WP02 横切基础对象冻结，发布 M2 冻结包并补齐 B1/B2 阻塞处置资产
- 状态：进行中

### 完成内容

1. 完成 WP-02 冻结发布收束：
   - 形成 WP02-T015 M2 冻结包，汇总横切错误、预算、标识、时间、事件封套、枚举规则与 M2 Gate 门禁。
   - 更新 WP-02 TODO，将 T015 挂接到正式交付物并置为 In Review。
2. 完成 B1 设计闭环：
   - 识别 `timeout_seconds -> timeout_ms` 属于设计阶段的兼容性迁移问题，而非实现返工问题。
   - 落地 B1 迁移清单，明确字段映射、冲突判定、弃用窗口和回退策略。
3. 完成 B2 基线补齐：
   - 落地枚举 unknown -> Unspecified 降级契约测试基线文档。
   - 在 contracts/include 下新增最小兼容辅助头，在 tests/contract 下新增 compatibility contract test 与 CMake 接入。
4. 完成冻结包状态校正：
   - 将 B1 标记为 Closed。
   - 将 B2 保持为 In Review，等待 contract test 实际执行通过后再关闭。

### 关键产物

- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP02-T014-评审纪要.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP02-T015-M2冻结包.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP02-T015-B1-timeout迁移清单.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP02-T015-B2-枚举降级契约测试基线.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/WP-02-横切基础对象TODO.md`
- `/home/gangan/DASALL-Agent/contracts/include/boundary/CompatibilityGuards.h`
- `/home/gangan/DASALL-Agent/tests/contract/smoke/CompatibilityContractTest.cpp`
- `/home/gangan/DASALL-Agent/tests/contract/CMakeLists.txt`

### 验证结果

1. 新增与更新的文档、头文件、测试文件均通过文件级错误检查（No errors found）。
2. 已确认 `contracts/` 当前仍无正式接口/数据结构实现，新增代码仅为兼容辅助层与契约测试基线。
3. 已确认 `tests/contract/` 除 smoke 基线外新增 compatibility contract test 入口。
4. CMake Tools 当前无法完成项目配置，导致 build/ctest 无法执行；因此 B2 不能标记为 Closed。

### 中断恢复点（下次会话从这里继续）

- WP-02 已基本收束：M2 冻结包已发布，B1 已关闭，B2 待执行验证。
- 下一任务建议：先修复当前工作区 CMake 配置问题并执行 `dasall_contract_compatibility_test`，通过后关闭 B2。
- 之后进入 WP-03 主链路对象的首个原子任务。
- 建议优先顺序：
  - `docs/todos/contracts/deliverables/WP02-T015-M2冻结包.md`
  - `docs/todos/contracts/deliverables/WP02-T015-B1-timeout迁移清单.md`
  - `docs/todos/contracts/deliverables/WP02-T015-B2-枚举降级契约测试基线.md`
  - `tests/contract/smoke/CompatibilityContractTest.cpp`

### 风险/注意事项

- 当前最大阻塞不是语义设计，而是 CMake 配置失败；在测试未实际跑通前，B2 只能保持 In Review。
- `timeout_seconds` 的问题是设计阶段主动暴露的兼容性风险，不代表已有大规模实现返工，但后续实现必须严格遵守迁移清单。
- unknown 枚举值降级必须集中走兼容辅助层，避免各子域自行定义 fallback 逻辑。

## 记录 #006

- 日期：2026-03-14
- 阶段：contracts 冻结（WP-01 术语与对象地图）
- 任务：完成 WP01-T002 至 WP01-T013，发布 M1 冻结包
- 状态：已完成

### 完成内容

1. 完成术语基线收束：
   - 术语归并、定义、消费者分层完成并形成稳定主名称集合。
2. 完成对象地图收束：
   - 顶层对象流图、稳定对象标注、内部/禁止外溢对象清单完成。
3. 完成边界规则收束：
   - 发布 contracts 边界说明 v1，固化 Stable/Blocked/Deferred 三层模型。
4. 完成 ADR 对齐核对：
   - ADR-006（ContextPacket 禁入字段）
   - ADR-007（建议权与执行权分层）
   - ADR-008（全局主控与协同子域分层）
5. 完成整体评审与冻结发布：
   - 形成 WP01-T012 评审纪要（有条件通过）
   - 发布 WP01-T013 M1 冻结包并将 T013 状态更新为 Completed。

### 关键产物

- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP01-T003-术语定义表-v1.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP01-T004-术语消费者矩阵.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP01-T005-顶层对象流图-v1.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP01-T006-稳定对象标注版流图.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP01-T007-内部对象边界清单.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP01-T008-contracts边界说明-v1.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP01-T009-ContextPacket约束核对单.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP01-T010-恢复语义核对单.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP01-T011-协同语义核对单.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP01-T012-整体骨架评审纪要.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/deliverables/WP01-T013-M1冻结包.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts/WP-01-术语与对象地图TODO.md`

### 验证结果

1. WP01-T009、T010、T011 核对单均完成并通过一致性检查。
2. WP01-T012 形成“可进入 WP-02”的评审结论与门禁条件。
3. WP01-T013 冻结包发布完成，T013 已标记为 Completed。
4. 本轮新增与更新文档均通过文件级错误检查（No errors found）。

### 中断恢复点（下次会话从这里继续）

- WP-01 已闭环完成（T013 Completed）
- 下一任务建议：进入 WP-02 横切基础对象，优先冻结入口/结果/标识元数据与错误域基线
- 建议优先顺序：
  - `docs/todos/contracts/WP-02-横切基础对象TODO.md`
  - `contracts/include/agent/`
  - `contracts/include/error/`
  - `contracts/include/context/`

### 风险/注意事项

- Deferred 对象 `ToolRequest`、`ToolResult` 在 WP-05 前仍为阶段性不外溢，避免被误判为永久禁止或提前冻结。
- 文档中若出现 `Orchestrator` 简称，需明确区分 `AgentOrchestrator` 与 `MultiAgentCoordinator`，避免主控权误读。
- 学习材料中的 ContextPacket 历史示例与 ADR-006 存在旧口径偏差，不作为冻结依据，但需在文档治理任务中纠偏。

## 记录 #005

- 日期：2026-03-12
- 阶段：阶段 A（工程基线与开发骨架）
- 任务：建立编码规范、命名规范、分支与提交流程
- 状态：已完成

### 完成内容

1. 新建工程协作规范文档：
   - `/home/gangan/DASALL-OS/docs/development/DASALL_工程协作与编码规范.md`
   - 内容覆盖编码规范、命名规范、分支策略、提交格式、PR 要求、阶段 A/B 特殊约束
2. 新建基础格式控制文件：
   - `/home/gangan/DASALL-OS/.editorconfig`
   - `/home/gangan/DASALL-OS/.clang-format`
3. 新建提交与 PR 模板：
   - `/home/gangan/DASALL-OS/.gitmessage.txt`
   - `/home/gangan/DASALL-OS/.github/pull_request_template.md`
4. 固化协作约定：
   - 分支命名规则：`feature/`、`fix/`、`refactor/`、`docs/`、`test/`、`chore/`、`release/`
   - 提交格式：`type(scope): summary`
   - PR 模板要求包含阶段/任务、影响范围、验证方式、风险与回滚点

### 关键产物

- `/home/gangan/DASALL-OS/docs/development/DASALL_工程协作与编码规范.md`
- `/home/gangan/DASALL-OS/.editorconfig`
- `/home/gangan/DASALL-OS/.clang-format`
- `/home/gangan/DASALL-OS/.gitmessage.txt`
- `/home/gangan/DASALL-OS/.github/pull_request_template.md`

### 验证结果

1. 规范文档已落地，可直接作为阶段 A 之后的统一协作基线。
2. `.editorconfig`、`.clang-format`、提交模板、PR 模板均已创建，可被后续 IDE、格式化工具和代码评审流程直接使用。

### 中断恢复点（下次会话从这里继续）

- 阶段 A 已全部完成
- 下一任务建议：进入阶段 B，开始 `contracts/` 契约层冻结与契约测试
- 建议优先顺序：
  - `contracts/include/agent/`
  - `contracts/include/error/`
  - `contracts/include/context/`
  - `tests/contract/`

### 对后续有用的信息

- 当前协作约定已形成“文档 + 模板 + 基础格式配置”三层结构，不要再分散定义第二套规范。
- 命名规则已经固定：类型 PascalCase，函数/变量 lower_snake_case，成员变量以 `_` 结尾，常量 `kPascalCase`。
- 在 contracts 冻结前，优先保持接口、命名、目录结构稳定，不要过早引入风格分歧或临时命名。

## 记录 #004

- 日期：2026-03-12
- 阶段：阶段 A（工程基线与开发骨架）
- 任务：初始化 tests 目录结构与公共 Mock 框架
- 状态：已完成

### 完成内容

1. 将 tests 根入口升级为分层结构：
   - 更新 `/home/gangan/DASALL-OS/tests/CMakeLists.txt`
   - 接入 `mocks/`、`unit/`、`contract/` 子目录
   - 保留 `unit` / `contract` 标签约定，并改为真实测试可执行程序
2. 建立公共测试支持库：
   - 新建 `/home/gangan/DASALL-OS/tests/mocks/CMakeLists.txt`
   - 提供 `dasall_test_support` 供后续单元测试和契约测试复用
3. 建立首批公共 Mock 头文件：
   - `/home/gangan/DASALL-OS/tests/mocks/include/dasall/tests/mocks/MockLLMAdapter.h`
   - `/home/gangan/DASALL-OS/tests/mocks/include/dasall/tests/mocks/MockTool.h`
   - `/home/gangan/DASALL-OS/tests/mocks/include/dasall/tests/mocks/MockExecutionService.h`
   - `/home/gangan/DASALL-OS/tests/mocks/include/dasall/tests/mocks/MockMemoryStore.h`
   - `/home/gangan/DASALL-OS/tests/mocks/include/dasall/tests/support/TestAssertions.h`
4. 初始化 unit/contract 测试目录入口：
   - 新建 `/home/gangan/DASALL-OS/tests/unit/CMakeLists.txt`
   - 新建各子目录 CMakeLists（runtime/cognition/llm/tools/memory/knowledge）
   - 新建 `/home/gangan/DASALL-OS/tests/contract/CMakeLists.txt`
5. 新增首批真实测试程序：
   - `/home/gangan/DASALL-OS/tests/unit/runtime/RuntimeSmokeTest.cpp`
   - `/home/gangan/DASALL-OS/tests/contract/smoke/ContractSmokeTest.cpp`

### 关键产物

- `/home/gangan/DASALL-OS/tests/CMakeLists.txt`
- `/home/gangan/DASALL-OS/tests/mocks/CMakeLists.txt`
- `/home/gangan/DASALL-OS/tests/unit/CMakeLists.txt`
- `/home/gangan/DASALL-OS/tests/contract/CMakeLists.txt`
- `/home/gangan/DASALL-OS/tests/mocks/include/dasall/tests/mocks/`
- `/home/gangan/DASALL-OS/tests/unit/runtime/RuntimeSmokeTest.cpp`
- `/home/gangan/DASALL-OS/tests/contract/smoke/ContractSmokeTest.cpp`

### 验证结果

1. 重新执行 `scripts/ci/build.sh` 通过。
2. `scripts/ci/unit_tests.sh` 通过，真实单测程序 `dasall_runtime_smoke_test` 运行通过。
3. `scripts/ci/contract_tests.sh` 通过，真实契约测试程序 `dasall_contract_smoke_test` 运行通过。

### 中断恢复点（下次会话从这里继续）

- 下一任务：阶段 A 第 5 项
- 任务内容：建立编码规范、命名规范、分支与提交流程
- 建议先落地：
  - `/home/gangan/DASALL-OS/docs/`
  - `/home/gangan/DASALL-OS/.github/`
  - 或 `/home/gangan/DASALL-OS/docs/worklog/` 中追加工程约定文档引用

### 对后续有用的信息

- 当前 `tests/mocks` 是“测试脚手架层”，故意不依赖未来生产接口，避免在 `contracts/` 冻结前反复返工。
- 等阶段 B 冻结 `IXxx` 接口后，可以将 `MockLLMAdapter`、`MockExecutionService`、`MockMemoryStore` 逐步替换为真正继承生产接口的 mock。
- 当前已有稳定标签约定：`unit`、`contract`；CI 与本地脚本都依赖该约定。

## 记录 #003

- 日期：2026-03-12
- 阶段：阶段 A（工程基线与开发骨架）
- 任务：建立基础 CI 流水线（编译、单测、契约测试、静态检查）
- 状态：已完成

### 完成内容

1. 建立本地与 CI 复用脚本：
   - 新建 `/home/gangan/DASALL-OS/scripts/ci/build.sh`
   - 新建 `/home/gangan/DASALL-OS/scripts/ci/unit_tests.sh`
   - 新建 `/home/gangan/DASALL-OS/scripts/ci/contract_tests.sh`
   - 新建 `/home/gangan/DASALL-OS/scripts/ci/static_check.sh`
   - 新建 `/home/gangan/DASALL-OS/scripts/ci/ci_local.sh`
2. 建立 GitHub Actions 工作流：
   - 新建 `/home/gangan/DASALL-OS/.github/workflows/ci.yml`
   - 流程顺序：Build -> Unit tests -> Contract tests -> Static checks
3. 完善测试标签与目标：
   - 更新 `/home/gangan/DASALL-OS/tests/CMakeLists.txt`
   - 增加 `dasall_unit_smoke`（label: unit）
   - 增加 `dasall_contract_smoke`（label: contract）
4. CI 稳定性修正：
   - CI 脚本默认使用独立构建目录 `build-ci`，避免与手工构建目录 generator 冲突
   - 将 `ctest` 改为在构建目录内执行，兼容本地工具链

### 关键产物

- `/home/gangan/DASALL-OS/.github/workflows/ci.yml`
- `/home/gangan/DASALL-OS/scripts/ci/build.sh`
- `/home/gangan/DASALL-OS/scripts/ci/unit_tests.sh`
- `/home/gangan/DASALL-OS/scripts/ci/contract_tests.sh`
- `/home/gangan/DASALL-OS/scripts/ci/static_check.sh`
- `/home/gangan/DASALL-OS/scripts/ci/ci_local.sh`
- `/home/gangan/DASALL-OS/tests/CMakeLists.txt`

### 验证结果

1. 本地执行 `build.sh` 通过，编译成功。
2. 本地执行 `unit_tests.sh` 通过，`unit` 标签测试 1 项通过。
3. 本地执行 `contract_tests.sh` 通过，`contract` 标签测试 1 项通过。
4. 本地执行 `static_check.sh` 成功退出；由于本机未安装 `cppcheck`/`clang-tidy`，当前为跳过状态。

### 中断恢复点（下次会话从这里继续）

- 下一任务：阶段 A 第 4 项
- 任务内容：初始化 `tests/` 目录结构与公共 Mock 框架（从 smoke 升级到可复用测试基座）
- 建议先落地：
  - `/home/gangan/DASALL-OS/tests/mocks/`
  - `/home/gangan/DASALL-OS/tests/unit/`
  - `/home/gangan/DASALL-OS/tests/contract/`

### 对后续有用的信息

- 统一本地 CI 入口为：`bash scripts/ci/ci_local.sh`。
- 若需在本地启用静态检查，安装依赖：`clang-tidy` 与 `cppcheck`。
- 当前单测/契约测试是 smoke 基线，后续可替换为 GoogleTest 并保留 `unit`/`contract` 标签约定。

## 记录 #002

- 日期：2026-03-12
- 阶段：阶段 A（工程基线与开发骨架）
- 任务：建立统一编译选项、第三方依赖接入策略（submodule + 本地 cache + FetchContent）
- 状态：已完成

### 完成内容

1. 新增统一编译选项模块：
   - 新建 `/home/gangan/DASALL-OS/cmake/DASALLOptions.cmake`
   - 定义 `dasall_build_options` 与 `dasall_apply_common_options()`
   - 按 `CMAKE_SYSTEM_PROCESSOR` 自动区分 x86/ARM/Generic，并注入架构宏
   - 统一 GCC/Clang 编译与链接选项，支持 Linux x86 与 ARM 交叉场景
2. 新增第三方依赖解析策略模块：
   - 新建 `/home/gangan/DASALL-OS/cmake/DASALLThirdParty.cmake`
   - 实现统一依赖解析函数 `dasall_resolve_dependency()`
   - 解析优先级：submodule > 本地 cache > FetchContent（严格按要求）
3. 接入根工程与模块：
   - 根 CMake 引入上述两个模块并输出依赖策略信息
   - 各模块与 apps 目标统一接入 `dasall_build_options`
   - 修复模块 include 路径错误（`/include` -> `${CMAKE_CURRENT_SOURCE_DIR}/include`）
4. 建立本地 cache 落地点与说明：
   - 新建 `/home/gangan/DASALL-OS/third_party/.cache/`
   - 新建 `/home/gangan/DASALL-OS/third_party/README.md`

### 关键产物

- `/home/gangan/DASALL-OS/cmake/DASALLOptions.cmake`
- `/home/gangan/DASALL-OS/cmake/DASALLThirdParty.cmake`
- `/home/gangan/DASALL-OS/CMakeLists.txt`
- `/home/gangan/DASALL-OS/third_party/.cache/.gitkeep`
- `/home/gangan/DASALL-OS/third_party/README.md`

### 验证结果

1. 重新执行 CMake 配置通过，成功生成 build 系统。
2. 配置日志显示策略生效：`submodule > local cache > FetchContent`。
3. 本地 cache 在源码目录 `third_party/.cache` 下，常规编译清理不会删除该目录。

### 中断恢复点（下次会话从这里继续）

- 下一任务：阶段 A 第 3 项
- 任务内容：建立基础 CI 流水线（编译、单测、契约测试、静态检查）
- 建议先落地：
  - `/home/gangan/DASALL-OS/.github/workflows/`（若使用 GitHub Actions）
  - 或 `/home/gangan/DASALL-OS/scripts/ci/`

### 对后续有用的信息

- 依赖默认不会在 configure 阶段自动联网拉取，`DASALL_BOOTSTRAP_THIRD_PARTY` 默认 OFF。
- 如需严格离线构建，建议设定：`-DDASALL_ALLOW_FETCHCONTENT=OFF`。
- 统一编译选项已集中到 cmake 模块，后续新增 target 需调用 `dasall_apply_common_options()`。

## 记录 #001

- 日期：2026-03-12
- 阶段：阶段 A（工程基线与开发骨架）
- 任务：创建顶层目录骨架与各模块 CMakeLists.txt
- 状态：已完成

### 完成内容

1. 创建工程顶层目录骨架：
   - apps, contracts, runtime, cognition, llm, tools, memory, knowledge, services, multi_agent, platform, infra, profiles, skills, tests, third_party, cmake, scripts, sysroots, debian
2. 为核心模块创建 CMakeLists：
   - 根 CMakeLists
   - 各子模块 CMakeLists
   - apps 子模块及占位 main.cpp
3. 创建 profiles 初始文件：
   - 每个 profile 包含 profile.cmake 与 runtime_policy.yaml

### 关键产物

- 根构建文件：/home/gangan/DASALL-OS/CMakeLists.txt
- 模块构建文件：/home/gangan/DASALL-OS/*/CMakeLists.txt
- 执行指引：/home/gangan/DASALL-Agent/docs/plans/DASALL_工程落地实现步骤指引.md

### 验证结果

1. 已完成 CMake 配置验证：build 目录成功生成。
2. 本机 CMake 为 3.16.3，根工程最低版本已设为 3.16，配置通过。

### 中断恢复点（下次会话从这里继续）

- 下一任务：阶段 A 第 2 项
- 任务内容：建立统一编译选项、第三方依赖接入策略（submodule 或 FetchContent）
- 建议落地点：
  - /home/gangan/DASALL-OS/cmake/
  - /home/gangan/DASALL-OS/third_party/
  - /home/gangan/DASALL-OS/CMakeLists.txt

### 对后续有用的信息

- 当前骨架已可配置，但尚未建立统一 warning、sanitizer、build type 策略。
- tests 目录为占位，后续需引入 GoogleTest 并替换 placeholder 测试目标。
- 当前 apps 为占位可执行，后续应改为依赖真实 runtime 接口与装配层。
