# KNO-TODO-027 lexical retrieval smoke integration 设计收敛

## 1. 输入与目标

1. 来源任务：`KNO-TODO-027 | 验证 lexical retrieval smoke integration`。
2. 设计锚点：详设 7 KNO-D07、8.2 Phase K1、9.1，以及 `KNO-TODO-012/013/014/016/017/019` 已落盘的 lexical supporting chain。
3. 本轮目标：新增一个真正经过 facade 的 lexical-only smoke integration，用真实 supporting components 证明 `IKnowledgeService::retrieve()` 可以返回非空 `context_projection`。

## 2. 设计结论

### 2.1 集成边界

1. 027 不再停留在文件存在性或纯 stub smoke。
2. 最小闭环收口为：
   - `IKnowledgeService` / `KnowledgeServiceFacade`
   - `QueryNormalizer`
   - `CorpusRouter`
   - `FreshnessController`
   - `CorpusCatalog`
   - `IndexReader`
   - `SparseRetriever`
   - `RecallCoordinator`
   - `Reranker`
   - `EvidenceAssembler`
3. `Telemetry` / `HealthProbe` / `request_refresh` 在 027 中不作为主验证目标，只保留 no-op / default seam，避免把 smoke 范围扩展到 032/033。

### 2.2 数据面策略

1. 为了让 smoke 具备真实 lexical 检索语义，本轮使用 SQLite FTS5 in-memory fixture，直接创建 `chunks_fts` 虚表并插入 1 条 normative 证据。
2. `IndexReader` 使用真实 active snapshot callback 把 facade -> recall -> sparse retriever 链接到该 FTS5 fixture。
3. `CorpusCatalog` 使用 1 个 trusted + normative + lexical-capable descriptor，确保 route 结果稳定落在 `LexicalOnly`。

### 2.3 接口与验证策略

1. smoke test 通过 `std::unique_ptr<IKnowledgeService>` 调用 `retrieve()`，尽量贴近 runtime-facing 边界，而不是直接调用下游 supporting components。
2. 断言最小集：
   - `retrieve().ok == true`
   - `retrieve().mode == LexicalOnly`
   - `EvidenceBundle.context_projection` 非空
   - projection line 带有 normative 标签与 citation ref
3. discoverability 不在测试进程内自检，而通过 `ctest -N` / `ctest -R` 作为验收命令保证。

## 3. Design -> Build 映射

1. `tests/integration/knowledge/KnowledgeRetrievalSmokeTest.cpp`
   - 新增真实 lexical smoke integration，内建 SQLite FTS5 fixture 和 facade harness。
2. `tests/integration/knowledge/CMakeLists.txt`
   - 注册 `dasall_knowledge_retrieval_smoke_integration_test`；
   - 为该 target 追加 `knowledge/src` internal include path 与 `dasall_sqlite3` 链接。

## 4. 验证计划

1. `cmake -S . -B build-ci -G "Unix Makefiles"`
2. `cmake --build build-ci --target dasall_knowledge_retrieval_smoke_integration_test`
3. `ctest --test-dir build-ci -N | grep dasall_knowledge_retrieval_smoke_integration_test`
4. `ctest --test-dir build-ci -R dasall_knowledge_retrieval_smoke_integration_test --output-on-failure`

## 5. 完成判定

1. smoke integration 经过 facade 返回非空 `context_projection`。
2. `ctest -N` 可发现 `dasall_knowledge_retrieval_smoke_integration_test`。
3. 027 只验证 lexical-only 最小闭环，不把 hybrid、degrade matrix 或 refresh 真实编排提前带入本轮。