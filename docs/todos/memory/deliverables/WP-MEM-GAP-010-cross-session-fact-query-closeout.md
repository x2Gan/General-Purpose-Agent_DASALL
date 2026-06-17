# WP-MEM-GAP-010 cross-session fact query closeout

来源任务：WP-MEM-GAP-010
关联缺口：GAP-P2-B / MEM-E05
完成日期：2026-06-17

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-010 / GAP-P2-B / MEM-E05`，不把遗忘曲线、composite scoring、desktop_full 默认开向量灰度或分层递归摘要混入同一轮。
2. authoritative 问题定义固定为：当前 `FactQuery` 虽然已有 `user_id` 字段，但 `CandidateCollector` 同时写入 `session_id + user_id`，导致当前 session 外的 user-level facts 被 `session_id` 过滤掉，`ContextOrchestrator` 的 `belief_state_summary` 无法跨 session 召回同一用户的历史偏好。
3. owner 边界保持不变：`user_id` 字段继续留在既有 contracts / schema 面，不新增 shared contracts admission；Memory 只补显式 user-scoped query seam、SQLite 索引与 context 装配消费路径。

## 2. 研究与设计依据

1. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §6.12.5 已把 `MEM-E05` 固定为“跨 session 事实共享（user-level FactQuery）”，前置条件是 Fact 仓储稳定，而不是新增 contracts 字段。
2. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已将 `WP-MEM-GAP-010` 固定为“在 `IFactStore` 增加 `query_facts_by_user(user_id, ...)`，SchemaMigrator V003 增加 `idx_facts_user_id`，并让 `CandidateCollector` 在 ContextOrchestrator 装配时消费 user-level facts”。
3. 本地 blocker 复核表明无需先做 contracts 解阻：[contracts/include/memory/Session.h](../../../../contracts/include/memory/Session.h) 与 [sql/memory/V001__initial_schema.sql](../../../../sql/memory/V001__initial_schema.sql) 在本轮前已具备 `user_id` 字段，缺口只在 Memory 内部语义面。
4. 外部参考：SQLite Query Planner 文档明确指出，没有索引时 `WHERE` 过滤会退化为 full table scan，而为等值过滤列建立 index 后，query planner 才能用 binary search / indexed lookup 避免全表扫描；`CREATE INDEX` 文档则固定了 `CREATE INDEX ... ON table(column)` 的标准语义。这直接支撑本轮为 `facts.user_id` 增加 V003 索引，避免 user-level fact lookup 在数据增长后退化。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| Memory 需要一条显式的“按用户跨 session 查事实”语义面，不能再靠调用方手工拼 `FactQuery` 避免误带 `session_id` | [memory/include/IFactStore.h](../../../../memory/include/IFactStore.h)、[memory/src/store/sqlite/SqliteMemoryStore.cpp](../../../../memory/src/store/sqlite/SqliteMemoryStore.cpp)、[tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp) |
| user-level fact lookup 需要避免随着 `facts` 表增长退化为全表扫描 | [sql/memory/V003__fact_user_lookup_index.sql](../../../../sql/memory/V003__fact_user_lookup_index.sql)、[tests/unit/memory/SchemaMigrationV003Test.cpp](../../../../tests/unit/memory/SchemaMigrationV003Test.cpp) |
| CandidateCollector / ContextOrchestrator 必须真正消费 sibling-session facts，而不只是存在一个可调用 API | [memory/src/context/CandidateCollector.cpp](../../../../memory/src/context/CandidateCollector.cpp)、[tests/integration/memory/MemoryCrossSessionFactQueryTest.cpp](../../../../tests/integration/memory/MemoryCrossSessionFactQueryTest.cpp) |
| 既有 compile surface 与测试替身不能因接口扩展回退 | [tests/mocks/include/FakeMemoryStore.h](../../../../tests/mocks/include/FakeMemoryStore.h)、[tests/unit/memory/CandidateCollectorTest.cpp](../../../../tests/unit/memory/CandidateCollectorTest.cpp)、[tests/unit/memory/WritebackCoordinatorCoreTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorCoreTest.cpp)、[tests/unit/memory/WritebackCoordinatorPartialTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorPartialTest.cpp)、[tests/unit/memory/ConflictResolverDegradedTest.cpp](../../../../tests/unit/memory/ConflictResolverDegradedTest.cpp) |

## 4. 本轮代码结果

1. 更新 [memory/include/IFactStore.h](../../../../memory/include/IFactStore.h)，新增 `query_facts_by_user(const std::string& user_id, const FactQuery& query)`，把“按用户跨 session 查事实”从隐式调用约定提升为显式接口面。
2. 更新 [memory/src/store/sqlite/SqliteMemoryStore.h](../../../../memory/src/store/sqlite/SqliteMemoryStore.h) 与 [memory/src/store/sqlite/SqliteMemoryStore.cpp](../../../../memory/src/store/sqlite/SqliteMemoryStore.cpp)，新增 user-scoped query 实现：复用既有 `query_facts` SQL path，但强制清空 `session_id` 并设置 `user_id` filter，避免调用方再手工规避 session 过滤。
3. 新增 [sql/memory/V003__fact_user_lookup_index.sql](../../../../sql/memory/V003__fact_user_lookup_index.sql)，创建 `idx_facts_user_id`；同步更新 [tests/unit/memory/SchemaMigrationTest.cpp](../../../../tests/unit/memory/SchemaMigrationTest.cpp) 的 bundled migration target version 基线。
4. 更新 [memory/src/context/CandidateCollector.cpp](../../../../memory/src/context/CandidateCollector.cpp)，当 session bundle 带有非空 `user_id` 时优先调用 `query_facts_by_user(...)`，让 [memory/src/context/ContextOrchestrator.cpp](../../../../memory/src/context/ContextOrchestrator.cpp) 投影 `belief_state_summary` 时自然收到 user-level facts。
5. 更新 [tests/mocks/include/FakeMemoryStore.h](../../../../tests/mocks/include/FakeMemoryStore.h)、[tests/unit/memory/CandidateCollectorTest.cpp](../../../../tests/unit/memory/CandidateCollectorTest.cpp)、[tests/unit/memory/WritebackCoordinatorCoreTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorCoreTest.cpp)、[tests/unit/memory/WritebackCoordinatorPartialTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorPartialTest.cpp)、[tests/unit/memory/ConflictResolverDegradedTest.cpp](../../../../tests/unit/memory/ConflictResolverDegradedTest.cpp) 与 [tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp)，保持新接口面下的 compile surface 与替身一致。
6. 更新 [tests/unit/memory/SqliteMemoryStoreTest.cpp](../../../../tests/unit/memory/SqliteMemoryStoreTest.cpp)，新增 sibling-session user-scoped lookup 覆盖；新增 [tests/unit/memory/SchemaMigrationV003Test.cpp](../../../../tests/unit/memory/SchemaMigrationV003Test.cpp) 与 [tests/integration/memory/MemoryCrossSessionFactQueryTest.cpp](../../../../tests/integration/memory/MemoryCrossSessionFactQueryTest.cpp)，并更新 [tests/unit/memory/CMakeLists.txt](../../../../tests/unit/memory/CMakeLists.txt) 与 [tests/integration/memory/CMakeLists.txt](../../../../tests/integration/memory/CMakeLists.txt) 完成注册。
7. 更新 [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md)、[docs/todos/DASALL_子系统查漏补缺专项记录.md](../../../todos/DASALL_子系统查漏补缺专项记录.md) 与 [docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_开发执行记录.md)，回写 closeout 状态与证据。

## 5. D Gate

1. 设计边界明确：本轮不新增 contracts 字段，只复用已有 `user_id` 并在 Memory 内部显式化 user-scoped query seam。
2. Build 三件套完整：代码目标、测试目标、验收命令均已在规划文档与本 closeout 中逐项落地。
3. blocker 已被证伪：原规划中的“可能需要 contracts 扩字段”在本地代码复核后不成立，因此没有额外 blocker fix 任务混入本轮提交。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_memory_interface_compile_unit_test","dasall_memory_candidate_collector_unit_test","dasall_memory_sqlite_store_unit_test","dasall_memory_writeback_core_unit_test","dasall_memory_writeback_partial_unit_test","dasall_memory_conflict_resolver_degraded_unit_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["MemoryInterfaceCompileTest","CandidateCollectorTest","SqliteMemoryStoreTest","WritebackCoordinatorCoreTest","WritebackCoordinatorPartialTest","ConflictResolverDegradedTest"])`
   - 结果：通过，6/6。
3. `Build_CMakeTools(buildTargets=["dasall_memory_schema_migration_unit_test","dasall_memory_schema_migration_v003_unit_test","dasall_memory_sqlite_store_unit_test","dasall_memory_cross_session_fact_query_integration_test"])`
   - 结果：通过。
4. `RunCtest_CMakeTools(tests=["SchemaMigrationTest","SchemaMigrationV003Test","SqliteMemoryStoreTest","MemoryCrossSessionFactQueryTest"])`
   - 结果：通过，4/4。

## 7. 完成判定

1. `WP-MEM-GAP-010 / GAP-P2-B / MEM-E05` 已闭合。
2. `ContextOrchestrator` 现在可以在当前 session 未再次声明偏好的情况下，仍从同一 `user_id` 的历史 session 中召回 user-level facts，并把它们投影到 `belief_state_summary`。
3. `idx_facts_user_id` 已通过 V003 迁移闭合行为与 schema 证据，compile surface / test doubles 也已同步更新，不会因接口扩展造成隐性回退。