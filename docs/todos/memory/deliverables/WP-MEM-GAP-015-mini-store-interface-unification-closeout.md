# WP-MEM-GAP-015 mini-store interface unification closeout

来源任务：WP-MEM-GAP-015
关联缺口：GAP-P3-B
完成日期：2026-06-25

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-015 / GAP-P3-B`，不把 `WP-MEM-GAP-016` 的 shared contracts 提升、`WP-MEM-GAP-017` 的历史遗留清理或 `WP-MEM-GAP-018` 的 soak gate 增强混入同一轮。
2. authoritative 问题定义固定为：Memory 详细设计 §6.6 一直把 `IMemoryStore` 定义为单一持久化 façade，但代码在后续 ISP 收敛中保留了 `IFactStore` / `IExperienceStore` / `ISessionStore` / `ISummaryStore` / `IMaintenanceStore` 五个 public mini-store seam，导致内部组件构造签名、测试替身与设计口径重新分叉。
3. owner 边界保持不变：本轮只收敛 Memory 模块内部 public store surface 与内部依赖口径；继续保留 `IStoreTransaction` / `ITransactionalStore` 事务 seam，不把 store supporting types 或 Context contracts 推入 shared contracts。

## 2. 研究与设计依据

1. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §6.6 已明确：`SessionTimelineRepository`、`SummaryRepository`、`FactRepository`、`ExperienceRepository` 在组件级设计里统一由单一 `IMemoryStore + SqliteMemoryStore` 承载，内部消费者应通过 `IMemoryStore` 访问，而不是依赖独立 repository / mini-store 实例。
2. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已将 `WP-MEM-GAP-015` 固定为“评估将 mini-store 接口合并到 `IMemoryStore`；保留 `IStoreTransaction` 与 `ITransactionalStore`；新增 `MemoryStoreInterfaceUnificationCompileTest`；不得破坏 mock 行为隔离”。
3. Martin Fowler 的 Repository 模式强调用单一 collection-like façade 集中 query / persistence logic，有助于减少重复 query seam 并保持 domain 到 mapping layer 的单向依赖；这与 DASALL 在单 SQLite 逻辑主库上的 `IMemoryStore` 统一 façade 口径一致。
4. SQLite transaction 文档明确同一数据库同时只允许一个 write transaction，`BEGIN IMMEDIATE` / `COMMIT` 在并发 writer 场景下会返回 `SQLITE_BUSY`；这直接支持本轮把 `CandidateCollector` 的 access-touch 写路径也收口到 shared writer mutex，而不是继续让内部 mini-store 风格的局部写路径绕开统一 writer serialization。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| `IMemoryStore` 必须重新成为唯一 public store façade；mini-store headers 只保留 supporting types 与兼容别名 | [memory/include/IMemoryStore.h](../../../../memory/include/IMemoryStore.h)、[memory/include/IFactStore.h](../../../../memory/include/IFactStore.h)、[memory/include/IExperienceStore.h](../../../../memory/include/IExperienceStore.h)、[memory/include/ISessionStore.h](../../../../memory/include/ISessionStore.h)、[memory/include/ISummaryStore.h](../../../../memory/include/ISummaryStore.h)、[memory/include/IMaintenanceStore.h](../../../../memory/include/IMaintenanceStore.h) |
| 内部组件必须直接依赖 `IMemoryStore`，不再通过五个 mini-store 构造注入 | [memory/src/context/CandidateCollector.h](../../../../memory/src/context/CandidateCollector.h)、[memory/src/writeback/CompressionCoordinator.h](../../../../memory/src/writeback/CompressionCoordinator.h)、[memory/src/writeback/HierarchicalSummarizationCoordinator.h](../../../../memory/src/writeback/HierarchicalSummarizationCoordinator.h)、[memory/src/writeback/WritebackCoordinator.h](../../../../memory/src/writeback/WritebackCoordinator.h)、[memory/src/conflict/MemoryConflictResolver.h](../../../../memory/src/conflict/MemoryConflictResolver.h)、[memory/src/maintenance/MemoryMaintenanceWorker.h](../../../../memory/src/maintenance/MemoryMaintenanceWorker.h) |
| 测试替身必须保留行为隔离能力，不能因接口收敛失去 targeted fault injection | [tests/mocks/include/FakeMemoryStore.h](../../../../tests/mocks/include/FakeMemoryStore.h)、[tests/unit/memory/CandidateCollectorTest.cpp](../../../../tests/unit/memory/CandidateCollectorTest.cpp)、[tests/unit/memory/ConflictResolverDegradedTest.cpp](../../../../tests/unit/memory/ConflictResolverDegradedTest.cpp)、[tests/unit/memory/WritebackCoordinatorCoreTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorCoreTest.cpp)、[tests/unit/memory/WritebackCoordinatorPartialTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorPartialTest.cpp) |
| 接口收敛后必须有新的 compile gate 锁住“mini-store alias -> IMemoryStore”语义，并证明 memory-scoped 回归不回退 | [tests/unit/memory/MemoryStoreInterfaceUnificationCompileTest.cpp](../../../../tests/unit/memory/MemoryStoreInterfaceUnificationCompileTest.cpp)、[tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp)、[tests/unit/memory/CMakeLists.txt](../../../../tests/unit/memory/CMakeLists.txt) |
| 并发 maintenance / touch 写路径必须保持同一 writer serialization，避免接口收敛后出现新的 SQLite writer 竞争 | [memory/src/context/CandidateCollector.cpp](../../../../memory/src/context/CandidateCollector.cpp)、[memory/src/MemoryManagerFactory.cpp](../../../../memory/src/MemoryManagerFactory.cpp)、[tests/unit/memory/MemoryConcurrencyStressTest.cpp](../../../../tests/unit/memory/MemoryConcurrencyStressTest.cpp) |

## 4. 设计决策

1. `IMemoryStore` 重新收口为唯一 public store façade：Session / Summary / Fact / Experience / Maintenance 方法全部回归 `IMemoryStore`，而 `IFactStore` / `IExperienceStore` / `ISessionStore` / `ISummaryStore` / `IMaintenanceStore` 改为“supporting type + compatibility alias”头文件，既保留 include 路径稳定性，也消除独立抽象的冗余。
2. `ITransactionalStore` / `IStoreTransaction` 保持独立。事务语义仍是唯一需要单独隔离的 seam，因为它既服务 `WritebackCoordinator` / maintenance，又不应与 query/update supporting types 强耦合。
3. 内部组件构造口径统一收敛到 `IMemoryStore&`：`CandidateCollector`、`CompressionCoordinator`、`HierarchicalSummarizationCoordinator`、`MemoryConflictResolver`、`WritebackCoordinator`、`MemoryMaintenanceWorker` 不再接受多个 mini-store 参数，直接回到详细设计 §6.6 的单 façade 依赖面。
4. mock/fake 不因接口收敛而退化：`FakeMemoryStore` 继续作为完整 store façade；局部 fault-injection test doubles 只在需要时覆盖 targeted method，同时补齐 ProgrammaticMemory seam，保证行为隔离能力仍由测试替身而不是额外 public interface 数量来承载。
5. 本轮发现的直接 blocker 不是设计 blocker，而是 build/validation blocker：llm metadata seam 落地后，`ScriptedCognitionFirstLLMManager` 与 `MockLLMManager` 未实现新的 `lookup_prompt_asset_metadata()` 纯虚方法，导致 memory-scoped 全量 build 失败。该 blocker 在同轮以最小 override 解掉，不扩大任务范围。
6. `CandidateCollector::touch_facts()` / `touch_experiences()` 改为复用 shared writer mutex。这不是额外 feature，而是把 access-touch 写路径重新纳入统一 writer serialization，避免 `prepare_context()` 和 maintenance 在同一 SQLite writer 上并发写入，导致 `MemoryConcurrencyStressTest` 误触发 maintenance hard-failure warnings。

## 5. D Gate

1. 设计边界明确：不扩 shared contracts，不引入新的 repository façade，不回退 `ITransactionalStore` 独立事务 seam。
2. Build 三件套完整：代码目标、测试目标、验收命令已锁定为 store public surface 收敛、内部构造依赖收敛、memory-scoped 66 条回归全绿。
3. blocker 结论：本轮无前置设计 blocker；实施中出现的 build blocker（两个 `ILLMManager` test double 缺失 metadata lookup override）已在同轮最小修复并清除。

## 6. 代码结果

1. 更新 [memory/include/IMemoryStore.h](../../../../memory/include/IMemoryStore.h)，把 session / summary / fact / experience / maintenance 方法重新收口到单一 `IMemoryStore` surface；同时保留 `ITransactionalStore` / `IStoreTransaction` 事务抽象。
2. 更新 [memory/include/IFactStore.h](../../../../memory/include/IFactStore.h)、[memory/include/IExperienceStore.h](../../../../memory/include/IExperienceStore.h)、[memory/include/ISessionStore.h](../../../../memory/include/ISessionStore.h)、[memory/include/ISummaryStore.h](../../../../memory/include/ISummaryStore.h)、[memory/include/IMaintenanceStore.h](../../../../memory/include/IMaintenanceStore.h)，把它们收口为“supporting type + compatibility alias”头文件，不再各自定义独立虚接口。
3. 更新 [memory/src/context/CandidateCollector.h](../../../../memory/src/context/CandidateCollector.h) / [memory/src/context/CandidateCollector.cpp](../../../../memory/src/context/CandidateCollector.cpp)、[memory/src/writeback/CompressionCoordinator.h](../../../../memory/src/writeback/CompressionCoordinator.h) / [memory/src/writeback/CompressionCoordinator.cpp](../../../../memory/src/writeback/CompressionCoordinator.cpp)、[memory/src/writeback/HierarchicalSummarizationCoordinator.h](../../../../memory/src/writeback/HierarchicalSummarizationCoordinator.h) / [memory/src/writeback/HierarchicalSummarizationCoordinator.cpp](../../../../memory/src/writeback/HierarchicalSummarizationCoordinator.cpp)、[memory/src/conflict/MemoryConflictResolver.h](../../../../memory/src/conflict/MemoryConflictResolver.h) / [memory/src/conflict/MemoryConflictResolver.cpp](../../../../memory/src/conflict/MemoryConflictResolver.cpp)、[memory/src/writeback/WritebackCoordinator.h](../../../../memory/src/writeback/WritebackCoordinator.h) / [memory/src/writeback/WritebackCoordinator.cpp](../../../../memory/src/writeback/WritebackCoordinator.cpp)、[memory/src/maintenance/MemoryMaintenanceWorker.h](../../../../memory/src/maintenance/MemoryMaintenanceWorker.h) / [memory/src/maintenance/MemoryMaintenanceWorker.cpp](../../../../memory/src/maintenance/MemoryMaintenanceWorker.cpp)，将内部 owning component 构造口径统一为 `IMemoryStore&`。
4. 更新 [memory/src/MemoryManagerFactory.cpp](../../../../memory/src/MemoryManagerFactory.cpp)，统一用单一 `IMemoryStore` wiring 组装 `CandidateCollector`、`HierarchicalSummarizationCoordinator`、`MemoryConflictResolver`、`WritebackCoordinator` 和 `MemoryMaintenanceWorker`；同时把 shared writer mutex 注入 `CandidateCollector` access-touch 写路径。
5. 更新 [memory/src/store/sqlite/SqliteMemoryStore.cpp](../../../../memory/src/store/sqlite/SqliteMemoryStore.cpp)，把 maintenance helper 的 `BEGIN IMMEDIATE` / `COMMIT` 返回码显式化，对 busy exhaustion 改发 `*_busy` warning，而不是误记成 hard failure；这与 shared writer mutex 收口一起消除了 `MemoryConcurrencyStressTest` 的稳定失败。
6. 更新 [tests/unit/memory/CandidateCollectorTest.cpp](../../../../tests/unit/memory/CandidateCollectorTest.cpp)、[tests/unit/memory/CandidateCollectorVectorOffTest.cpp](../../../../tests/unit/memory/CandidateCollectorVectorOffTest.cpp)、[tests/unit/memory/CandidateCollectorCompositeScoringTest.cpp](../../../../tests/unit/memory/CandidateCollectorCompositeScoringTest.cpp)、[tests/unit/memory/MemoryRetentionDecayTest.cpp](../../../../tests/unit/memory/MemoryRetentionDecayTest.cpp)、[tests/unit/memory/HierarchicalSummarizationCoordinatorTest.cpp](../../../../tests/unit/memory/HierarchicalSummarizationCoordinatorTest.cpp)、[tests/unit/memory/ContextOrchestratorTest.cpp](../../../../tests/unit/memory/ContextOrchestratorTest.cpp)、[tests/unit/memory/ContextOrchestratorDegradedTest.cpp](../../../../tests/unit/memory/ContextOrchestratorDegradedTest.cpp)、[tests/unit/memory/ContextOrchestratorEvidenceProjectionTest.cpp](../../../../tests/unit/memory/ContextOrchestratorEvidenceProjectionTest.cpp)、[tests/integration/memory/MemoryCrossSessionFactQueryTest.cpp](../../../../tests/integration/memory/MemoryCrossSessionFactQueryTest.cpp)、[tests/unit/memory/WritebackCoordinatorCoreTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorCoreTest.cpp) 与 [tests/unit/memory/WritebackCoordinatorPartialTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorPartialTest.cpp)，同步收敛到新的单-store 构造签名。
7. 更新 [tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp) 并新增 [tests/unit/memory/MemoryStoreInterfaceUnificationCompileTest.cpp](../../../../tests/unit/memory/MemoryStoreInterfaceUnificationCompileTest.cpp)，锁定 “mini-store compatibility alias == `IMemoryStore`” 和 “内部组件直接依赖单一 store façade” 的 compile-time 语义。
8. 更新 [tests/unit/memory/MemoryConcurrencyStressTest.cpp](../../../../tests/unit/memory/MemoryConcurrencyStressTest.cpp)，增强并发失败诊断，并以共享 writer mutex + busy-warning 语义复验 maintenance / prepare_context / write_back 并发路径。
9. 作为同轮 blocker-fix，更新 [apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp](../../../../apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp) 与 [tests/mocks/include/MockLLMManager.h](../../../../tests/mocks/include/MockLLMManager.h)，为 `ILLMManager::lookup_prompt_asset_metadata()` 提供最小 override，解除 memory-scoped 全量 build 被 llm-backed memory tests 间接阻塞的问题。

## 7. 验证

1. `Build_CMakeTools(buildTargets=["dasall_memory_interface_compile_unit_test","dasall_memory_store_interface_unification_compile_unit_test","dasall_memory_candidate_collector_unit_test","dasall_memory_candidate_collector_vector_off_unit_test","dasall_memory_candidate_collector_composite_scoring_unit_test","dasall_memory_retention_decay_unit_test","dasall_memory_writeback_core_unit_test","dasall_memory_writeback_partial_unit_test"])`
   - 结果：通过。
2. `Build_CMakeTools(buildTargets=["dasall_memory_store_interface_unification_compile_unit_test","dasall_memory_hierarchical_summarization_coordinator_unit_test","dasall_memory_context_orchestrator_unit_test","dasall_memory_context_orchestrator_degraded_unit_test","dasall_memory_context_orchestrator_evidence_unit_test","dasall_memory_cross_session_fact_query_integration_test","dasall_memory_writeback_reflection_lesson_unit_test","dasall_memory_maintenance_retention_unit_test","dasall_memory_maintenance_checkpoint_unit_test","dasall_memory_conflict_resolver_unit_test","dasall_memory_conflict_resolver_with_embedding_unit_test","dasall_memory_conflict_resolver_degraded_unit_test","dasall_memory_fact_conflict_resolver_unit_test"])`
   - 结果：通过。
3. `Build_CMakeTools(buildTargets=[memory-scoped unit/integration/contract targets])`
   - 结果：同轮首次暴露 blocker：`ScriptedCognitionFirstLLMManager` 与 `MockLLMManager` 未实现 `lookup_prompt_asset_metadata()`；补齐最小 override 后，再次执行通过。
4. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(Memory.*|CandidateCollector.*|BudgetAllocator.*|CompressionCoordinator.*|ContextOrchestrator.*|ConflictResolverDegradedTest|FactConflictResolverTest|HierarchicalSummarizationCoordinatorTest|LLMBacked.*|SimpleLocalEmbeddingAdapterTest|Sqlite.*|TiktokenEstimatorAccuracyTest|VectorMemoryAdapterTest|WorkingMemory.*|TurnSessionSummaryMemoryContractTest|MemoryFactExperienceContractTest|ContextPacketFieldContractTest)$'`
   - 结果：通过，66/66。
5. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^MemoryConcurrencyStressTest$'`
   - 结果：在 shared writer mutex + maintenance busy warning 收口后再次通过，1/1。

## 8. 结果

1. `WP-MEM-GAP-015 / GAP-P3-B` 已闭合；Memory public store surface 已重新对齐到详细设计 §6.6 的单一 `IMemoryStore` façade，mini-store headers 仅保留兼容别名与 supporting type 容器。
2. 本轮没有牺牲测试隔离能力：`FakeMemoryStore`、`DelegatingMemoryStore`、`ThrowingFactQueryStore`、`CommitFailingStore`、`DerivedFailureStore` 等 targeted double 仍能做精确 fault injection，而不再依赖额外 public mini-store 虚接口数量来承载。
3. `MemoryConcurrencyStressTest` 暴露出的并发 writer 竞争根因已在同轮闭合：`prepare_context()` 的 access-touch 写路径现在和 writeback / maintenance 共用 shared writer mutex，memory-scoped 66 条 unit / integration / contract 回归已全部通过；当前 P3 焦点收敛为 `WP-MEM-GAP-016 / -017 / -018`。