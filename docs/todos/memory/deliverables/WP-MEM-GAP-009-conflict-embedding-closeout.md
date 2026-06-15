# WP-MEM-GAP-009 conflict embedding closeout

来源任务：WP-MEM-GAP-009
关联缺口：GAP-P2-A / MEM-E09
完成日期：2026-06-15

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-009 / GAP-P2-A / MEM-E09`，不把跨 session FactQuery、遗忘曲线、composite scoring 或 desktop_full 默认开向量灰度混入同一轮。
2. authoritative 问题定义固定为：当 `MemoryConflictResolver` 的关键词 / polarity / negation / numeric 规则无法高置信度区分 `Coexist` 与 `Supersede` 时，是否能利用已有 embedding seam 为跨语言 / 同义改写提供额外判别信号，同时保持 fail-soft。
3. owner 边界保持不变：embedding provider 继续由 runtime_support 注入，resolver 只消费 `IEmbeddingAdapter` 抽象，不新增 llm/runtime 反向依赖。

## 2. 研究与设计依据

1. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §6.12.3 已把 `MemoryConflictResolver` 固定为规则化冲突裁定 owner，且明确允许在主链稳定后补入向量相似度辅助。
2. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已将 `WP-MEM-GAP-009` 固定为“增加可选 `IEmbeddingAdapter*` 与阈值投影，不改变主链 owner 边界”。
3. 外部参考：Sentence Transformers STS / multilingual usage 文档把 embedding 相似度的主流做法固定为对句向量计算 cosine similarity，并明确多语言 embedding 可用于跨语言语义相似度评估；这支持本轮把 embedding similarity 作为歧义冲突的辅助信号，而不是替代现有规则引擎。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| resolver 只在规则路径不充分时使用 embedding 相似度辅助 | [memory/src/conflict/MemoryConflictResolver.cpp](../../../memory/src/conflict/MemoryConflictResolver.cpp) + `MemoryConflictResolverWithEmbeddingTest` |
| embedding 判定阈值必须由 profile 可投影配置承载 | [memory/include/config/MemoryConfig.h](../../../memory/include/config/MemoryConfig.h)、[memory/src/config/MemoryConfigProjector.cpp](../../../memory/src/config/MemoryConfigProjector.cpp)、`MemoryInterfaceCompileTest`、`MemoryProfileCompatibilityTest` |
| embedding 失败不得阻断写回主链路 | [memory/src/conflict/MemoryConflictResolver.cpp](../../../memory/src/conflict/MemoryConflictResolver.cpp) 的 `conflict_embedding_similarity_skipped` fail-soft 路径 |
| task acceptance 必须锁定 focused target / test discoverability | [tests/unit/memory/CMakeLists.txt](../../../tests/unit/memory/CMakeLists.txt) + `Build_CMakeTools` / `RunCtest_CMakeTools` |

## 4. 本轮代码结果

1. 更新 [memory/include/config/MemoryConfig.h](../../../memory/include/config/MemoryConfig.h) 与 [memory/src/config/MemoryConfigProjector.cpp](../../../memory/src/config/MemoryConfigProjector.cpp)，新增 `ConflictConfig.embedding_similarity_threshold`，默认阈值固定为 `0.85` 并纳入统一 projection。
2. 更新 [memory/src/conflict/MemoryConflictResolver.h](../../../memory/src/conflict/MemoryConflictResolver.h) 与 [memory/src/conflict/MemoryConflictResolver.cpp](../../../memory/src/conflict/MemoryConflictResolver.cpp)，新增可选 `IEmbeddingAdapter*` 注入、余弦相似度计算，以及“规则路径无法高置信度判定时才触发”的 embedding assist。
3. 更新 [memory/src/MemoryManagerFactory.cpp](../../../memory/src/MemoryManagerFactory.cpp)，把 `config.conflict` 与 runtime-owned `embedding_adapter` 注入 resolver，保持 runtime_support owner glue 不回退。
4. 新增 [tests/unit/memory/MemoryConflictResolverWithEmbeddingTest.cpp](../../../tests/unit/memory/MemoryConflictResolverWithEmbeddingTest.cpp)，并更新 [tests/unit/memory/CMakeLists.txt](../../../tests/unit/memory/CMakeLists.txt)、[tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp)、[tests/integration/memory/MemoryProfileCompatibilityTest.cpp](../../../tests/integration/memory/MemoryProfileCompatibilityTest.cpp)，锁定新 target/test discoverability 与 config projection。
5. 更新 [docs/architecture/DASALL_memory子系统详细设计.md](../../../docs/architecture/DASALL_memory子系统详细设计.md)、[docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md)、[docs/todos/DASALL_子系统查漏补缺专项记录.md](../../../docs/todos/DASALL_子系统查漏补缺专项记录.md) 与 [docs/worklog/DASALL_开发执行记录.md](../../../docs/worklog/DASALL_开发执行记录.md)，回写 closeout 状态与证据。

## 5. D Gate

1. 设计边界明确：resolver 仍是规则 owner，embedding 只做歧义裁定辅助。
2. Build 三件套完整：代码目标、测试目标、验收命令已在规划文档和本 closeout 中一一对应。
3. fail-soft 语义明确：embedding 缺失或失败时退回规则路径并保留 warning，不产生新的主链 blocker。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_memory_conflict_resolver_with_embedding_unit_test","dasall_memory_interface_compile_unit_test","dasall_memory_profile_compatibility_integration_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["MemoryConflictResolverWithEmbeddingTest","MemoryInterfaceCompileTest","MemoryProfileCompatibilityTest"])`
   - 结果：通过，3/3。

## 7. 完成判定

1. `WP-MEM-GAP-009 / GAP-P2-A / MEM-E09` 已闭合。
2. `MemoryConflictResolver` 现可在关键词锚点重叠但规则路径不充分时，用 embedding 余弦相似度把跨语言 / 同义改写歧义从 `Coexist` 收敛到 `Supersede`。
3. embedding assist 的阈值与 fail-soft 语义都已被 config projection 与 focused tests 锁定，不会把 runtime_support 的 provider 细节反向耦合进 memory。