# WP-MEM-GAP-020 memory quality SLO closeout

来源任务：WP-MEM-GAP-020
关联缺口：V2 Memory 质量 SLO 与质量指标
完成日期：2026-06-18

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-020`，不把 `WP-MEM-GAP-021` 的 reflection feedback loop、installed gate 或 soak gate 增强混入同一轮。
2. authoritative 问题定义固定为：Memory 已具备事件级 observability，但缺少可直接回答“召回是否退化、摘要是否 grounded、冲突裁定是否稳定、writeback/compression 是否持续部分降级”的质量指标面，因此 V2 质量门仍停留在主观判断。
3. owner 边界保持不变：质量指标 schema 与采样器都停留在 Memory module-local seam；不扩 shared contracts，不把 quality scoring 下沉到 runtime / llm public surface。

## 2. 研究与设计依据

1. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已将 `WP-MEM-GAP-020` 固定为“`MemoryQualityMetrics` + `MemoryQualityProbe` + `MemoryQualityProbeIntegrationTest` + `MemoryRecallAtKBaselineTest` + `MemorySummaryFaithfulnessBaselineTest`”。
2. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §11 / §12 已冻结 observability owner 在 memory module-local bridge，允许通过 infra metrics/audit/trace sink 承载 memory 自身的质量信号，而不是把质量判定抛给 runtime。
3. Pinecone《Evaluation Measures in Information Retrieval》将 `Recall@K` 归类为 deploy 前最常用的 offline retrieval 指标，适合作为 curated baseline / regression gate 的第一层召回覆盖率信号。
4. Ragas `Faithfulness` 指标将 groundedness 定义为“response 中被 supporting context 支持的 claims 数 / response 总 claims 数”，这直接支撑本轮把 `summary_faithfulness_score` 收口为 claim-support ratio，而不是只看字符串相等。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| quality metric 名称、SLO floor/ceiling 必须先冻结，避免测试和 observability 各自发散 | [memory/include/observability/MemoryQualityMetrics.h](../../../../memory/include/observability/MemoryQualityMetrics.h)、[tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp) |
| quality probe 不应另起第二套 telemetry stack，而应复用现有 MemoryObservability -> infra metrics sink | [memory/src/observability/MemoryObservability.h](../../../../memory/src/observability/MemoryObservability.h)、[memory/src/observability/MemoryObservability.cpp](../../../../memory/src/observability/MemoryObservability.cpp)、[memory/src/observability/MemoryQualityProbe.h](../../../../memory/src/observability/MemoryQualityProbe.h)、[memory/src/observability/MemoryQualityProbe.cpp](../../../../memory/src/observability/MemoryQualityProbe.cpp) |
| recall / faithfulness 应在 prepare_context 路径采样，partial-rate / conflict precision 应在 write_back 路径采样 | [memory/src/context/ContextOrchestrator.h](../../../../memory/src/context/ContextOrchestrator.h)、[memory/src/context/ContextOrchestrator.cpp](../../../../memory/src/context/ContextOrchestrator.cpp)、[memory/src/writeback/WritebackCoordinator.h](../../../../memory/src/writeback/WritebackCoordinator.h)、[memory/src/writeback/WritebackCoordinator.cpp](../../../../memory/src/writeback/WritebackCoordinator.cpp)、[memory/src/MemoryManagerFactory.cpp](../../../../memory/src/MemoryManagerFactory.cpp) |
| quality gate 需要 curated baseline，而不是只看 smoke“跑没跑” | [tests/integration/memory/MemoryQualityProbeIntegrationTest.cpp](../../../../tests/integration/memory/MemoryQualityProbeIntegrationTest.cpp)、[tests/integration/memory/MemoryRecallAtKBaselineTest.cpp](../../../../tests/integration/memory/MemoryRecallAtKBaselineTest.cpp)、[tests/integration/memory/MemorySummaryFaithfulnessBaselineTest.cpp](../../../../tests/integration/memory/MemorySummaryFaithfulnessBaselineTest.cpp)、[tests/integration/memory/CMakeLists.txt](../../../../tests/integration/memory/CMakeLists.txt) |

## 4. 设计决策

1. `MemoryQualityMetrics.h` 只冻结 metric identity、unit 与默认 SLO floor/ceiling，不把 profile-specific threshold 写死到 shared interface。
2. `MemoryQualityProbe` 通过 `MemoryObservability::emit_metric_sample()` 直接向既有 metrics facade 发 `Gauge` sample；事件计数和质量指标共用同一 telemetry sink，不新建第二条 metrics owner 链。
3. `summary_faithfulness_score` 使用 trim 前的原始 summary text 与 supporting contexts 采样，避免 packet slot trimming 把“摘要质量”误降级成“预算裁剪质量”。
4. `fact_conflict_precision` 采用写回期 rolling precision：只要 conflict record 已实际落地并未发生 partial failure，就计入 resolved precision；不为此回扩 shared `WritebackResult` contract。
5. `writeback_partial_rate` 与 `compression_fallback_rate` 都采用 rolling rate，适合后续在 soak gate 中读取 latest/min/max，而不是只看单次事件。

## 5. D Gate

1. 设计边界明确：不扩 shared contracts、不新增 runtime owner、不把质量判定塞回 llm public surface。
2. Build 三件套完整：代码目标、测试目标、验收命令均已锁定在 quality metrics schema、probe wiring 和三条 focused integration baselines。
3. blocker 结论：文档内引用的 `GAP-P0-A / -B` 已在前序轮次闭合，本轮不存在必须先切走的前置 BLOCK 任务。

## 6. 代码结果

1. 新增 [memory/include/observability/MemoryQualityMetrics.h](../../../../memory/include/observability/MemoryQualityMetrics.h)，冻结 `memory_quality_recall_at_k`、`memory_quality_summary_faithfulness_score`、`memory_quality_fact_conflict_precision`、`memory_quality_writeback_partial_rate`、`memory_quality_compression_fallback_rate` 及默认 SLO 常量。
2. 更新 [memory/src/observability/MemoryObservability.h](../../../../memory/src/observability/MemoryObservability.h) 与 [memory/src/observability/MemoryObservability.cpp](../../../../memory/src/observability/MemoryObservability.cpp)，新增 `MemoryMetricSample` 与 `emit_metric_sample()`，让 quality probe 能通过现有 memory telemetry sink 发 gauge sample。
3. 新增 [memory/src/observability/MemoryQualityProbe.h](../../../../memory/src/observability/MemoryQualityProbe.h) 与 [memory/src/observability/MemoryQualityProbe.cpp](../../../../memory/src/observability/MemoryQualityProbe.cpp)，在 module-local seam 内收口 recall@k、summary faithfulness、fact conflict precision、writeback partial rate 与 compression fallback rate 的 rolling 采样。
4. 更新 [memory/src/context/ContextOrchestrator.h](../../../../memory/src/context/ContextOrchestrator.h) / [memory/src/context/ContextOrchestrator.cpp](../../../../memory/src/context/ContextOrchestrator.cpp) 与 [memory/src/writeback/WritebackCoordinator.h](../../../../memory/src/writeback/WritebackCoordinator.h) / [memory/src/writeback/WritebackCoordinator.cpp](../../../../memory/src/writeback/WritebackCoordinator.cpp)，在 `prepare_context()` 与 `write_back()` owner 路径注入并调用 `MemoryQualityProbe`。
5. 更新 [memory/src/MemoryManagerFactory.cpp](../../../../memory/src/MemoryManagerFactory.cpp) 与 [memory/CMakeLists.txt](../../../../memory/CMakeLists.txt)，创建共享 probe、把 `observability` 公共 include 目录纳入 memory public layout，并把 `MemoryQualityProbe.cpp` 纳入主库。
6. 新增 [tests/integration/memory/MemoryQualityProbeIntegrationTest.cpp](../../../../tests/integration/memory/MemoryQualityProbeIntegrationTest.cpp)、[tests/integration/memory/MemoryRecallAtKBaselineTest.cpp](../../../../tests/integration/memory/MemoryRecallAtKBaselineTest.cpp)、[tests/integration/memory/MemorySummaryFaithfulnessBaselineTest.cpp](../../../../tests/integration/memory/MemorySummaryFaithfulnessBaselineTest.cpp)，并更新 [tests/integration/memory/CMakeLists.txt](../../../../tests/integration/memory/CMakeLists.txt) 完成 discoverability / link / include / SQL migration wiring。
7. 更新 [tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp)，把 `observability/MemoryQualityMetrics.h` 与新增 public include 子目录纳入 interface surface regression。

## 7. 测试结果

1. `MemoryQualityProbeIntegrationTest` 验证同一 manager 下的 curated quality baseline：`recall_at_k`、`summary_faithfulness_score`、`fact_conflict_precision`、`writeback_partial_rate`、`compression_fallback_rate` 均会在 metrics facade 中留下可聚合样本。
2. `MemoryRecallAtKBaselineTest` 验证 relaxed budget 与 tight budget 下的 retrieval evidence recall 会形成可区分 baseline，不再只有事件级 success/failure。
3. `MemorySummaryFaithfulnessBaselineTest` 验证 fully grounded single-claim summary 与 partially unsupported summary 能在 `summary_faithfulness_score` 上形成数值分离。
4. `MemoryInterfaceCompileTest` 继续锁定新的 `observability` public include surface 与 `MemoryQualityMetrics` 常量不回退。

## 8. 验收证据

1. `cmake --build build-ci --target dasall_memory`
   - 结果：通过。
2. `cmake --build build-ci --target dasall_memory_quality_probe_integration_test && cmake --build build-ci --target dasall_memory_recall_at_k_baseline_integration_test && cmake --build build-ci --target dasall_memory_summary_faithfulness_baseline_integration_test`
   - 结果：通过。
3. `ctest --test-dir build-ci -R "MemoryQualityProbe|MemoryRecallAtKBaseline|MemorySummaryFaithfulness" --output-on-failure`
   - 结果：通过，3/3。
4. `cmake --build build-ci --target dasall_memory_interface_compile_unit_test && ctest --test-dir build-ci -R "^MemoryInterfaceCompileTest$" --output-on-failure`
   - 结果：通过，1/1。

## 9. 结果

1. `WP-MEM-GAP-020` 已闭合；Memory 现在具备 module-local quality SLO baseline，可对 `prepare_context()` 和 `write_back()` 的质量表现输出可聚合的 recall/faithfulness/conflict/partial/fallback 指标。
2. 本轮保持了 owner 边界：quality metrics 仍由 memory 自己定义和采样，runtime_support 只继续提供 telemetry provider / LLM-backed summarizer glue，不掌管质量判定逻辑。
3. Memory 当前剩余 V2 焦点已从 `WP-MEM-GAP-020 / -021` 收窄为 `WP-MEM-GAP-021` 与更高层 soak report 联动；质量 SLO baseline 不再是 open gap。
