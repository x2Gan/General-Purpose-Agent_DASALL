# WP-MEM-GAP-011 retention decay closeout

来源任务：WP-MEM-GAP-011
关联缺口：GAP-P2-C / MEM-E02
完成日期：2026-06-17

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-011 / GAP-P2-C / MEM-E02`，不把 `WP-MEM-GAP-012` 的完整 composite scoring、`WP-MEM-GAP-013` 的 desktop_full 默认开向量灰度、installed gate 或 soak gate 增强混入同一轮。
2. authoritative 问题定义固定为：Memory 当前只按 `confidence_score` / `effectiveness_score`、turn retention 与 TTL 进行选择和清理，缺少“访问热度 + 时间衰减”这一层长期记忆治理，导致冷数据不会随时间自然降权，也无法在 retention 阶段按遗忘曲线淘汰。
3. owner 边界保持不变：`last_accessed_at` / `hit_count` 只作为 Memory store 内部 schema 元数据存在，不扩 shared contracts；shared `MemoryFact` / `ExperienceMemory` 仍只承载冻结的稳定语义面。

## 2. 研究与设计依据

1. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §4.1.1 已将 `MEM-E02` 固定为“参考 MemoryOS 引入遗忘曲线权重衰减”，并明确 DASALL 当前只有压缩，没有 forgetting curve。
2. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §12.3 将 `MEM-E02` 列为在 Fact / Experience 仓储稳定后推进的演进项；当前前置 `MEM-E05` 已于 2026-06-17 闭合，因此 011 已可执行。
3. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已将 `WP-MEM-GAP-011` 固定为“V004 schema 增加 `last_accessed_at` / `hit_count`、maintenance 引入 exponential decay、CandidateCollector 评分增加 decay 权重”。
4. 外部参考采用 MemoryOS：其 MTM→LPM 更新使用 heat-based replacement，热度由 `Nvisit`、`Linteraction` 与 `Rrecency = exp(-Δt/μ)` 组成，并在检索后更新访问计数与最近访问时间；这直接支持本轮在 DASALL 里用 `hit_count + last_accessed_at + exponential decay` 做最小可执行收口，而不是只继续依赖创建时间或固定 TTL。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| store 需要持久化访问热度元数据，但不能把 retrieval bookkeeping 泄漏到 shared contracts | [sql/memory/V004__retention_decay_metadata.sql](../../../../sql/memory/V004__retention_decay_metadata.sql)、[memory/src/store/sqlite/SqliteMemoryStore.cpp](../../../../memory/src/store/sqlite/SqliteMemoryStore.cpp)、[tests/unit/memory/SchemaMigrationV004Test.cpp](../../../../tests/unit/memory/SchemaMigrationV004Test.cpp) |
| CandidateCollector 必须把 decay 作为 retrieval ranking 的一部分，但不能提前把 012 的完整 multi-factor scoring 混入本轮 | [memory/src/context/CandidateCollector.cpp](../../../../memory/src/context/CandidateCollector.cpp)、[memory/include/IFactStore.h](../../../../memory/include/IFactStore.h)、[memory/include/IExperienceStore.h](../../../../memory/include/IExperienceStore.h)、[tests/unit/memory/MemoryRetentionDecayTest.cpp](../../../../tests/unit/memory/MemoryRetentionDecayTest.cpp) |
| prepare_context 读取路径需要回写访问热度，但不能破坏现有单 writer 纪律 | [memory/src/context/CandidateCollector.h](../../../../memory/src/context/CandidateCollector.h)、[memory/src/MemoryManagerFactory.cpp](../../../../memory/src/MemoryManagerFactory.cpp)、[memory/src/MemoryManagerInternal.h](../../../../memory/src/MemoryManagerInternal.h) |
| maintenance 需要按遗忘曲线淘汰“足够老且足够冷”的 fact / experience，同时保持既有 TTL / superseded 语义不回退 | [memory/include/config/MemoryConfig.h](../../../../memory/include/config/MemoryConfig.h)、[memory/src/config/MemoryConfigProjector.cpp](../../../../memory/src/config/MemoryConfigProjector.cpp)、[memory/src/maintenance/MemoryMaintenanceWorker.cpp](../../../../memory/src/maintenance/MemoryMaintenanceWorker.cpp)、[tests/unit/memory/MemoryMaintenanceRetentionTest.cpp](../../../../tests/unit/memory/MemoryMaintenanceRetentionTest.cpp) |

## 4. 设计决策

1. 不新增 shared contracts 字段。`last_accessed_at` / `hit_count` 只在 SQLite schema 与 Memory module-local query metadata 内存在，避免把 retrieval bookkeeping 扩张到共享语义面。
2. 本轮不引入 `WP-MEM-GAP-012` 的完整 `confidence × recency × hit_rate × source_weight` 组合评分；011 只补“base score × decay weight”这一条最小闭环，让 012 后续只需要在同一 scoring seam 上继续扩维。
3. `CandidateCollector` 的 access touch 通过显式 `touch_facts(...)` / `touch_experiences(...)` seam 走 writer path，并复用已有 `store_writer_mutex`，避免把 `query_facts()` / `query_experiences()` 变成隐式写路径。
4. decay purge 不直接替代 TTL / superseded 规则，而是新增一条“足够老且足够冷”淘汰条件：记录只有在超过最小 age gate 后，才因 decay score 低于阈值而进入 purge 集合，降低对新近数据的误删风险。
5. config 路径延续现有 `memory.maintenance.*` 体系：为避免在本轮引入 profile schema churn，decay 参数投影到 `MemoryConfig.maintenance.decay`，由 `MemoryConfigProjector` 给出默认值；规划文档中的“retention.decay”在本轮按现有 maintenance owner 语义落地。

## 5. D Gate

1. 设计边界明确：不扩 shared contracts、不提前混入 012、不在 query path 偷写 SQLite。
2. Build 三件套完整：代码目标、测试目标、验收命令都已固定为 V004 schema、decay scoring/touch 与 retention purge 三部分。
3. blocker 结论：当前不存在必须先切走的前置 BLOCK 任务；`WP-MEM-GAP-012` 反而以 011 为前置，因此本轮直接推进 011 合规。

## 6. 计划中的代码结果

1. 新增 `sql/memory/V004__retention_decay_metadata.sql`，为 `facts` / `experiences` 增加 `last_accessed_at`、`hit_count` 并回填历史行。
2. 更新 `memory/include/config/MemoryConfig.h` 与 `memory/src/config/MemoryConfigProjector.cpp`，引入 module-local decay policy 默认值。
3. 更新 `memory/include/IFactStore.h`、`memory/include/IExperienceStore.h`、`memory/src/store/sqlite/SqliteMemoryStore.*` 与测试替身，新增 retrieval metadata / touch seam。
4. 更新 `memory/src/context/CandidateCollector.*` 与 `memory/src/MemoryManagerFactory.cpp`，把 decay score 纳入 fact / experience 排序，并在 collect 成功后 best-effort 回写 access touch。
5. 更新 `memory/src/store/sqlite/SqliteMemoryStore.cpp` 与 `memory/src/maintenance/MemoryMaintenanceWorker.cpp`，让 retention 在既有 TTL / superseded 之外识别低 decay 的冷数据。
6. 新增 `tests/unit/memory/SchemaMigrationV004Test.cpp`、`tests/unit/memory/MemoryRetentionDecayTest.cpp`，并扩展 compile/profile/retention focused tests 与 CMake discoverability。

## 7. 验收目标

1. `SchemaMigrationV004Test` 证明 fresh DB 与 V003→V004 upgrade 都会得到 `last_accessed_at` / `hit_count` 字段与回填。
2. `MemoryRetentionDecayTest` 证明 decay 权重可以影响 `CandidateCollector` 的 retrieval ranking，并在 collect 后更新访问热度。
3. 现有 retention focused tests 证明 maintenance 会淘汰“足够老且足够冷”的事实/经验，同时不回退既有 TTL / superseded 路径。

## 8. 验收命令

1. `ctest --test-dir build-ci -R "MemoryRetentionDecay|SchemaMigrationV004" --output-on-failure`
