# KNO-TODO-028 failure/degrade integration 设计收敛

- 日期：2026-04-21
- 任务：KNO-TODO-028
- 状态：Design 收敛，进入 Build

## 1. 目标

本任务为 hybrid 与退化闭环补 integration 级证据，覆盖四类关键场景：

1. `vector unavailable`
2. `partial timeout`
3. `stale reject`
4. `refresh busy`

验证目标不是再补一批 unit stub，而是证明 `IKnowledgeService` 和真实 lexical supporting chain 在 hybrid/degrade 场景下能给出稳定的最终行为。

## 2. 设计边界

### 2.1 复用项

复用 027 已经验证过的真实 lexical supporting chain：

1. `QueryNormalizer`
2. `CorpusRouter`
3. `FreshnessController`
4. `CorpusCatalog`
5. `IndexReader`
6. `SparseRetriever`
7. `RecallCoordinator`
8. `Reranker`
9. `EvidenceAssembler`
10. `KnowledgeServiceFacade`

### 2.2 注入项

仅对以下退化缝做注入：

1. `dense_bridge`：真实 `VectorRetrieverBridge`，用于 `vector unavailable`
2. `dense_lane` fallback：用于构造 `recall_timeout`
3. `request_refresh` seam：用于构造 `Busy`
4. `manifest/now_ms/query.allow_stale`：用于构造 `stale reject`

### 2.3 不纳入本轮

1. 不做 profile 资产读取，那是 029 的职责。
2. 不做 quality regression，那是 030 的职责。
3. 不做 refresh -> ingest -> swap -> retrieve 端到端闭环，那是 033 的职责。

## 3. 场景与断言

### 3.1 vector unavailable

1. 配置：`knowledge_enabled=true`、`vector_enabled=true`、`retrieval_mode_default=Hybrid`
2. 语料：descriptor 支持 `LexicalOnly + Hybrid`
3. dense lane：真实 `VectorRetrieverBridge`，底层 store `available()==false`
4. 断言：
   - coordinator `ok=true`
   - `candidates.degraded=true`
   - warning 包含 `dense_vector_backend_unavailable`
   - facade `retrieve().ok=true`
   - `result.mode == Hybrid`
   - `result.evidence->degraded == true`
   - `context_projection` 非空

### 3.2 partial timeout

1. 配置同上，允许 partial results
2. dense lane：使用 fallback `dense_lane` 注入 `failure_reason_codes={"recall_timeout"}`
3. 断言：
   - coordinator `ok=true`
   - `candidates.degraded=true`
   - warning 包含 `dense_recall_timeout`
   - facade `retrieve().ok=true`
   - `result.evidence->degraded == true`

### 3.3 stale reject

1. manifest age 位于 `refresh_interval < age <= expire_after`
2. profile 允许 stale read，但 query 不 opt-in
3. 断言：
   - router / facade 返回 `IndexStaleRejected`
   - `retrieve().ok=false`
   - 不返回 evidence bundle

### 3.4 refresh busy

1. `request_refresh` seam 直接返回 `RefreshStatus::Busy`
2. 断言：
   - `IKnowledgeService::request_refresh()` 返回 `Busy`
   - 返回值 `has_consistent_values()==true`

## 4. 文件范围

### 4.1 新增文件

1. `tests/integration/knowledge/KnowledgeFailureDegradeTest.cpp`

### 4.2 修改文件

1. `tests/integration/knowledge/CMakeLists.txt`

## 5. 验收命令

```bash
cmake -S . -B build-ci -G "Unix Makefiles"
cmake --build build-ci --target dasall_knowledge_failure_degrade_integration_test
ctest --test-dir build-ci -R dasall_knowledge_failure_degrade_integration_test --output-on-failure
```

## 6. 完成判定

满足以下条件即可完成：

1. 4 个场景都在 integration 层有可执行证据。
2. vector unavailable 和 partial timeout 都表现为 degraded success，而不是进程级失败。
3. stale reject 和 refresh busy 继续保留显式拒绝语义。
