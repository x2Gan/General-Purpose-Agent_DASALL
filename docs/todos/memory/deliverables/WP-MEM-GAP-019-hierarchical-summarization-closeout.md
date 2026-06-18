# WP-MEM-GAP-019 hierarchical summarization closeout

来源任务：WP-MEM-GAP-019
关联缺口：V2 分层递归摘要 / MemGPT + MemoryOS 对齐
完成日期：2026-06-18

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-019`，不把 `WP-MEM-GAP-020` 的质量 SLO、`WP-MEM-GAP-021` 的 reflection feedback loop、installed gate 或 soak gate 增强混入同一轮。
2. authoritative 问题定义固定为：当前 Memory 只有“recent turns -> dialog summary”单层压缩；`ISummaryStore` 与 SQLite schema 只表达 latest-per-session 语义，无法表示 dialog/topic/profile 分层 page、批次晋升与 child->parent summary 链路。
3. owner 边界保持不变：层级元数据与 parent 链路只存在于 Memory module-local seam 与 SQLite schema，不扩 shared `SummaryMemory` contracts；runtime 不拥有 hierarchy 调度权，writeback 只在本模块内 best-effort 触发层级晋升。

## 2. 研究与设计依据

1. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已将 `WP-MEM-GAP-019` 固定为“`HierarchicalSummaryLevel` + `HierarchicalSummarizationCoordinator` + schema `summary_parent_id` + hierarchy config 投影 + soak 断言”。
2. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §4.1.1 已把 MemoryOS 列为 DASALL 长期记忆演进的行业对标，指出当前 `CompressionCoordinator` 只覆盖 Mid->Long 的单层摘要，尚未形成层级页组织。
3. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §6.3.1、§6.5.3b、§6.12.2 已冻结 `ISummarizer`、`SummaryGenerationRequest/Result` 与 `SummaryProjection` 都是 module-local seam；因此 019 应复用现有 summarizer seam，而不是回扩 shared contracts 或把 hierarchy prompt owner 推给 runtime。
4. 外部参考采用 MemGPT 与 MemoryOS：MemGPT 强调基于层级记忆系统的 virtual context management，把有限上下文之外的信息在快慢存储层间移动；MemoryOS 明确采用 short-term / mid-term / long-term personal memory 三层存储，并以 segmented page organization 做 mid->long 动态更新。这两点共同支持 DASALL 在 019 中采用“dialog page -> topic page -> profile page”的分层页组织，而不是继续把所有历史摘要压成单条 latest summary。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| hierarchy 元数据不能污染 shared `SummaryMemory`，但 summary store 必须能表达 level 与 child->parent 链路 | [memory/include/writeback/HierarchicalSummaryRequest.h](../../../../memory/include/writeback/HierarchicalSummaryRequest.h)、[memory/include/ISummaryStore.h](../../../../memory/include/ISummaryStore.h)、[memory/src/store/sqlite/SqliteMemoryStore.cpp](../../../../memory/src/store/sqlite/SqliteMemoryStore.cpp)、[sql/memory/V006__summary_hierarchy.sql](../../../../sql/memory/V006__summary_hierarchy.sql)、[tests/unit/memory/SchemaMigrationV006Test.cpp](../../../../tests/unit/memory/SchemaMigrationV006Test.cpp) |
| hierarchy page 的 owner 应挂在 Memory writeback 后置链，而不是 runtime / ContextPacket 装配链 | [memory/src/writeback/HierarchicalSummarizationCoordinator.h](../../../../memory/src/writeback/HierarchicalSummarizationCoordinator.h)、[memory/src/writeback/HierarchicalSummarizationCoordinator.cpp](../../../../memory/src/writeback/HierarchicalSummarizationCoordinator.cpp)、[memory/src/writeback/WritebackCoordinator.cpp](../../../../memory/src/writeback/WritebackCoordinator.cpp) |
| 上层摘要必须复用现有 `ISummarizer` seam，并允许 dialog/topic/profile 走不同 prompt hint | [memory/include/writeback/SummaryGenerationRequest.h](../../../../memory/include/writeback/SummaryGenerationRequest.h)、[memory/src/writeback/CompressionCoordinator.cpp](../../../../memory/src/writeback/CompressionCoordinator.cpp)、[memory/src/MemoryManagerFactory.cpp](../../../../memory/src/MemoryManagerFactory.cpp)、[tests/unit/memory/HierarchicalSummarizationCoordinatorTest.cpp](../../../../tests/unit/memory/HierarchicalSummarizationCoordinatorTest.cpp) |
| hierarchy 开关和阈值必须经 `MemoryConfig` 投影，且长跑测试要看到 topic/profile page 实际出现 | [memory/include/config/MemoryConfig.h](../../../../memory/include/config/MemoryConfig.h)、[memory/src/config/MemoryConfigProjector.cpp](../../../../memory/src/config/MemoryConfigProjector.cpp)、[tests/integration/memory/MemoryLongRunningSoakTest.cpp](../../../../tests/integration/memory/MemoryLongRunningSoakTest.cpp) |

## 4. 设计决策

1. 不修改 shared `SummaryMemory`。`HierarchicalSummaryLevel`、批次请求、parent assignment 和 hierarchy query 全部停留在 Memory module-local seam；`SummaryMemory.tags` 只承载 level tag，不承载新的结构化强语义字段。
2. hierarchy owner 固定在 writeback 后置链：dialog summary 先按既有 core transaction 落库，再由 `HierarchicalSummarizationCoordinator` 在同一 writer mutex 下 best-effort 检查阈值并晋升 topic/profile page。若 hierarchy 失败，只回写 warning，不回滚 turn/session/dialog summary 的核心提交。
3. 上层摘要不再做“单条 latest summary 的无限增量合并”，而改为 segmented page：当某层 unparented child summaries 达阈值时，抽取一批 child summaries 生成一条新的 parent summary，并把 child rows 的 `summary_parent_id` 指向新 parent，形成可追溯 child->parent 链。
4. 019 仍保持 session-anchored `SummaryMemory` contract：`Profile` level 的 page 语义在本轮以“最高层 hierarchy page”落地，持久化仍复用冻结的 `session_id` 字段与现有 sessions FK，不引入新的 shared owner object；更高层 user-profile 检索/质量度量继续留给 `WP-MEM-GAP-020/-021` 演进。
5. `ISummarizer` 继续复用既有 `SummaryGenerationRequest`，但通过 `strategy_hint=hierarchy:topic` / `hierarchy:profile` 让 concrete summarizer 或模板路径区分 prompt 语义；不新增第二套 summarizer interface。

## 5. D Gate

1. 设计边界明确：不扩 shared contracts、不改 runtime owner、不把 hierarchy 误挂到 `ContextOrchestrator` 读取链。
2. Build 三件套完整：代码目标、测试目标、验收命令已固定为 hierarchy store/schema、coordinator wiring 与 soak 断言三部分。
3. blocker 结论：`WP-MEM-GAP-001` 已在前序轮次闭合，当前不存在必须先切走的前置 BLOCK 任务；本轮可直接进入 B。

## 6. 计划中的代码结果

1. 新增 `memory/include/writeback/HierarchicalSummaryRequest.h` 与 `memory/src/writeback/HierarchicalSummarizationCoordinator.*`，定义 `Dialog / Topic / Profile` level、批次晋升请求与 child->parent 分层协调器。
2. 更新 `memory/include/ISummaryStore.h`、`memory/src/store/sqlite/SqliteMemoryStore.*` 与测试替身，增加按 level 查询 latest/unparented summaries 以及 child parent assignment seam。
3. 新增 `sql/memory/V006__summary_hierarchy.sql`，为 `summaries` 增加 `summary_parent_id` 与 hierarchy 辅助索引；保持现有 `SummaryMemory` row mapper 不回扩 shared contract 字段。
4. 更新 `memory/include/config/MemoryConfig.h` 与 `memory/src/config/MemoryConfigProjector.cpp`，新增 `MemoryConfig.compression.hierarchy.{enabled, dialog_to_topic_threshold, topic_to_profile_threshold}` 默认投影。
5. 更新 `memory/src/writeback/WritebackCoordinator.*` 与 `memory/src/MemoryManagerFactory.cpp`，在 summary core commit 后 best-effort 触发 hierarchy promotion，并把 warnings 回传 `WritebackResult`。
6. 新增 `tests/unit/memory/HierarchicalSummarizationCoordinatorTest.cpp`、`tests/unit/memory/SchemaMigrationV006Test.cpp`，并扩展 `tests/integration/memory/MemoryLongRunningSoakTest.cpp` 验证 topic/profile page 与 parent 链实际出现。

## 7. 验收目标

1. `HierarchicalSummarizationCoordinatorTest` 证明 threshold 未达时不会晋升，达阈值时会创建带 level tag 的 parent summary 并回填 child `summary_parent_id`。
2. `SchemaMigrationV006Test` 证明 fresh DB 与 V004->V006 upgrade 都会得到 `summaries.summary_parent_id` 与 hierarchy 索引。
3. `MemoryLongRunningSoakTest` 证明长跑写回后会形成 topic/profile level summary page，而不是只累积 dialog summaries。

## 8. 验收命令

1. `ctest --test-dir build-ci -R "HierarchicalSummarizationCoordinator|SchemaMigrationV006|MemoryLongRunningSoak" --output-on-failure`