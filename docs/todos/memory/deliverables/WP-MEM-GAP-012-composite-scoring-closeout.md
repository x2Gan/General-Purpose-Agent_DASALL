# WP-MEM-GAP-012 composite scoring closeout

来源任务：WP-MEM-GAP-012
关联缺口：GAP-P2-D / MEM-E03
完成日期：2026-06-18

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-012 / GAP-P2-D / MEM-E03`，不把 `WP-MEM-GAP-013` 的 desktop_full 默认开向量灰度、installed gate、ProgrammaticMemory 或质量 SLO 混入同一轮。
2. authoritative 问题定义固定为：011 已经闭合了 `last_accessed_at` / `hit_count` / decay seam，但 `CandidateCollector` 仍只按 `confidence/effectiveness × decay_weight` 排序，缺少可调的 multi-factor scoring，导致 provenance 更强、近期高频的候选无法稳定压过单一高置信旧候选。
3. owner 边界保持不变：组合评分逻辑继续留在 `memory/src/context/CandidateCollector.cpp`，配置继续由 `MemoryConfigProjector` 投影到 module-local `MemoryConfig.context.scoring`，不扩 shared contracts，也不把评分策略泄漏到 runtime / llm / profiles shared surface 之外。

## 2. 研究与设计依据

1. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §4.1.1 已把 CrewAI 的 composite scoring 口径固定为“recency + relevance + importance”，并明确 DASALL 需要在 `CandidateCollector` 中分阶段引入同方向的可控评分。
2. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §12.3 将 `MEM-E03` 固定为 `CandidateCollector composite scoring`，依赖 `MEM-E02` 遗忘曲线与 `MEM-E05` 跨 session 事实查询先提供稳定 scoring seam；这两个前置已分别于 2026-06-17 闭合。
3. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已将 `WP-MEM-GAP-012` 固定为“`score = w1·confidence + w2·recency + w3·hit_rate + w4·source_weight`，权重由 `MemoryConfig.context.scoring` 投影，保留 confidence-only fallback”。
4. 外部参考采用 CrewAI Memory 文档：其 recall path 用 composite scoring 混合 semantic similarity、recency 与 importance，并允许按场景调节各权重；这直接支持本轮把 DASALL 的 multi-factor scoring 收口为配置驱动的 `confidence + recency + hit_rate + source_weight`，同时保留简单 fallback path，而不是把评分硬编码为单一公式。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| scoring 权重必须是 module-local config，而不是散落常量 | [memory/include/config/MemoryConfig.h](../../../../memory/include/config/MemoryConfig.h)、[memory/src/config/MemoryConfigProjector.cpp](../../../../memory/src/config/MemoryConfigProjector.cpp)、[tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp)、[tests/integration/memory/MemoryProfileCompatibilityTest.cpp](../../../../tests/integration/memory/MemoryProfileCompatibilityTest.cpp) |
| store 需要向 collector 暴露 raw scoring signal，但不能把访问元数据抬进 shared contracts | [memory/include/IFactStore.h](../../../../memory/include/IFactStore.h)、[memory/include/IExperienceStore.h](../../../../memory/include/IExperienceStore.h)、[memory/src/store/sqlite/SqliteMemoryStore.cpp](../../../../memory/src/store/sqlite/SqliteMemoryStore.cpp)、[tests/mocks/include/FakeMemoryStore.h](../../../../tests/mocks/include/FakeMemoryStore.h) |
| `CandidateCollector` 必须以 composite scoring 排序，同时保留 confidence-only fallback | [memory/src/context/CandidateCollector.cpp](../../../../memory/src/context/CandidateCollector.cpp)、[tests/unit/memory/CandidateCollectorCompositeScoringTest.cpp](../../../../tests/unit/memory/CandidateCollectorCompositeScoringTest.cpp) |
| multi-factor scoring 不应改变 `BudgetAllocator` 的预算分配与 trim 行为 | [tests/unit/memory/BudgetAllocatorScoringDriftTest.cpp](../../../../tests/unit/memory/BudgetAllocatorScoringDriftTest.cpp)、[tests/unit/memory/BudgetAllocatorTest.cpp](../../../../tests/unit/memory/BudgetAllocatorTest.cpp) |

## 4. 设计决策

1. `MemoryConfig.context.scoring` 以 module-local `ScoringConfig` 形式落地，默认启用 composite scoring，并显式提供 `confidence_weight`、`recency_weight`、`hit_rate_weight`、`source_weight` 四个权重；权重投影继续留在 `MemoryConfigProjector`，不新增 shared profile schema。
2. `IFactStore` / `IExperienceStore` 的 query result 新增 `recency_score_by_*` 与 `hit_rate_score_by_*`，让 collector 组合 raw signal，而不是在 store 里直接返回预混合分数；这样既保留 011 的 decay seam，又不把评分策略固化到持久层。
3. `source_weight` 不新增 schema 字段，而由 collector 按 provenance 完整度计算：facts 优先看 `source_observation_refs` / `evidence_digest` / `source_turn_ids`，experiences 优先看 `source_fact_ids` / `source_turn_ids` / `applicable_domains` / `risk_notes`。这保持了 source bias 可解释，同时不扩 contracts。
4. fallback path 以 `composite_enabled=false` 实现 confidence-only 排序；默认 profile 继续走 composite，不让 011 已闭合的热度元数据变成死字段，但也保留了最小回退面，便于边缘/回归场景做 A/B 切换。
5. `BudgetAllocator` 本轮不改逻辑。012 只新增 focused drift test，锁定“候选顺序变化不会改变 slot budget / trim action”这一非功能约束，避免把预算策略和检索评分意外耦合。

## 5. D Gate

1. 设计边界明确：不扩 shared contracts、不改 runtime / llm owner、不把 composite scoring 下沉到 store 预混合实现。
2. Build 三件套完整：代码目标、测试目标、验收命令都已固定为 scoring config、collector 排序与 budget drift 三部分。
3. blocker 结论：当前不存在必须先切走的前置 BLOCK 任务；011 已经提供 decay metadata / touch seam /排序基线，本轮可直接推进 012。

## 6. 代码结果

1. 更新 [memory/include/config/MemoryConfig.h](../../../../memory/include/config/MemoryConfig.h) 与 [memory/src/config/MemoryConfigProjector.cpp](../../../../memory/src/config/MemoryConfigProjector.cpp)，新增 `ContextConfig::ScoringConfig` 与 profile 默认投影；同步更新 [tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp) 与 [tests/integration/memory/MemoryProfileCompatibilityTest.cpp](../../../../tests/integration/memory/MemoryProfileCompatibilityTest.cpp) 锁定新配置面。
2. 更新 [memory/include/IFactStore.h](../../../../memory/include/IFactStore.h)、[memory/include/IExperienceStore.h](../../../../memory/include/IExperienceStore.h)、[memory/src/store/sqlite/SqliteMemoryStore.cpp](../../../../memory/src/store/sqlite/SqliteMemoryStore.cpp) 与 [tests/mocks/include/FakeMemoryStore.h](../../../../tests/mocks/include/FakeMemoryStore.h)，在 query result 中新增 `recency_score_by_*` / `hit_rate_score_by_*`，并把 SQLite / fake store 的 `last_accessed_at`、`hit_count` 投影成 bounded raw signal，同时保留既有 `decay_weight_by_*`。
3. 更新 [memory/src/context/CandidateCollector.cpp](../../../../memory/src/context/CandidateCollector.cpp)，新增 `composite_score_or_fallback(...)`、`fact_source_weight(...)`、`experience_source_weight(...)`，让 fact / experience 排序切换为 `confidence + recency + hit_rate + source_weight` 的配置驱动组合评分；当 `composite_enabled=false` 或权重失效时回退到 confidence-only 路径。
4. 新增 [tests/unit/memory/CandidateCollectorCompositeScoringTest.cpp](../../../../tests/unit/memory/CandidateCollectorCompositeScoringTest.cpp)，覆盖“composite scoring 会让近期高频且来源更完整的候选压过单一高置信旧候选”与 “关闭 composite 后回退到 confidence-only 排序”两条 focused behavior。
5. 新增 [tests/unit/memory/BudgetAllocatorScoringDriftTest.cpp](../../../../tests/unit/memory/BudgetAllocatorScoringDriftTest.cpp)，并更新 [tests/unit/memory/CMakeLists.txt](../../../../tests/unit/memory/CMakeLists.txt) 完成 target / include / link / CTest discoverability wiring，锁定 candidate order 改变不会引发 `BudgetAllocator` budget / trim plan 漂移。

## 7. 验证

1. `Build_CMakeTools(buildTargets=["dasall_memory_interface_compile_unit_test","dasall_memory_profile_compatibility_integration_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["MemoryInterfaceCompileTest","MemoryProfileCompatibilityTest"])`
   - 结果：通过，2/2。
3. `Build_CMakeTools(buildTargets=["dasall_memory_candidate_collector_composite_scoring_unit_test","dasall_memory_budget_allocator_scoring_drift_unit_test"])`
   - 结果：通过。
4. `RunCtest_CMakeTools(tests=["CandidateCollectorCompositeScoringTest","BudgetAllocatorScoringDriftTest"])`
   - 结果：通过，2/2。
5. `Build_CMakeTools(buildTargets=["dasall_memory_interface_compile_unit_test","dasall_memory_profile_compatibility_integration_test","dasall_memory_candidate_collector_unit_test","dasall_memory_candidate_collector_vector_off_unit_test","dasall_memory_budget_allocator_unit_test","dasall_memory_retention_decay_unit_test","dasall_memory_sqlite_store_unit_test"])`
   - 结果：通过。
6. `RunCtest_CMakeTools(tests=["MemoryInterfaceCompileTest","MemoryProfileCompatibilityTest","CandidateCollectorTest","CandidateCollectorVectorOffTest","BudgetAllocatorTest","MemoryRetentionDecayTest","SqliteMemoryStoreTest"])`
   - 结果：通过，7/7。

## 8. 结果

1. `WP-MEM-GAP-012 / GAP-P2-D / MEM-E03` 已闭合；Memory 现已具备可配置的 multi-factor candidate scoring，能够在 `CandidateCollector` 中把 confidence、recency、hit-rate 与 provenance completeness 合成为稳定排序分数。
2. 本轮没有扩 shared contracts，也没有把评分策略下沉为 store 固化分数；011 提供的 decay seam 被保留并扩展为 raw scoring signal，符合“先闭合热度元数据，再在 collector 扩维”的设计顺序。
3. `BudgetAllocator` 预算分配与 trim 行为未漂移；当前剩余 V2 焦点从 `WP-MEM-GAP-012 / -013` 收敛为 `WP-MEM-GAP-013` 与更高层质量 SLO / soak gate。