# KNO-TODO-013 SparseRetriever 设计收敛

## 1. 输入与目标

1. 来源任务：`KNO-TODO-013 | 实现 SparseRetriever lexical 召回`。
2. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.13.2 `SparseRetriever` 卡片、7 KNO-D02 与 11.1 KNO-B03。
3. 前置满足：`KNO-TODO-006` 已冻结 `KnowledgeErrors` / `KnowledgeTypes` 公共语义，`KNO-TODO-008` 已提供 `NormalizedQuery`，`KNO-TODO-009` 已提供 `RetrievalPlan`，`KNO-TODO-016/017` 已把 corpus / freshness 纪律前移到 query path。
4. 本轮目标：落盘 lexical query expression、corpus / metadata / language / authority 过滤与 sentence-window 扩展，同时避免在 013 内抢跑 `KNO-TODO-019 IndexReader` 的 active snapshot concrete owner。

## 2. 设计结论

### 2.1 边界与职责

1. `SparseRetriever` 负责：
   - 把 `NormalizedQuery + RetrievalPlan` 收敛为可执行的 lexical query expression；
   - 把 route 约束转成 hard filters（corpus、metadata tag、language、authority floor）；
   - 对搜索结果执行 sentence-window 邻域扩展并映射为 `RecallHit`。
2. `SparseRetriever` 不负责：
   - 持有或切换 active snapshot；
   - 直接操作 SQLite 连接生命周期；
   - 做 hybrid lane 融合、freshness 裁定或 evidence 压缩。

### 2.2 接口收敛

1. 详细设计卡片把 `SparseRetriever` 简写成 `retrieve(const RetrievalPlan&)`，但现有 `RetrievalPlan` 不携带 lexical terms，无法独立表达查询内容。
2. 为避免反向修改 009 的 route contract，本轮新增 `SparseRetrieveRequest`：
   - `query::NormalizedQuery normalized_query`
   - `query::RetrievalPlan plan`
   - `optional<string> required_language`
3. 为避免抢跑 019，本轮不在 `SparseRetriever` 内直接拥有 SQLite snapshot，而采用 knowledge-owned search seam：
   - `SparseIndexSearchRequest`
   - `SparseIndexSearchResult`
   - `SparseRetrieverDeps::search_index`
4. 013 通过该 seam 固化“要搜什么、怎么过滤、怎么映射”；019 之后只需把 active snapshot 读路径接到同一 seam，不需要重写 query expression 或 snippet 规则。

### 2.3 查询表达式与过滤纪律

1. lexical query expression 统一使用 FTS5 安全 quoted literal，避免把用户文本当作裸 FTS 操作符注入。
2. 默认表达式为：
   - 单词项：`"term"`
   - 多词项：`"term_a" AND "term_b" ...`
3. 当 `prefer_exact_match=true` 且词项数 > 1 时，表达式升级为：
   - `("term_a term_b ...") OR ("term_a" AND "term_b" ...)`
4. hard filters 固定顺序：
   - `plan.corpus_ids` allowlist
   - `normalized_query.domain_tags` 全量命中
   - `required_language` 精确匹配
   - authority floor 与 `CorpusRouter` 保持一致：`PolicyEvidence -> Normative`，`FactLookup/ProcedureLookup -> Reference`，`DiagnosticContext -> Advisory`，`MultiHop -> Normative`
5. filter 冲突只允许产生 0 hit；禁止 retriever 私自放宽条件。

### 2.4 Sentence-Window 规则

1. `SparseRetriever` 使用搜索返回的 `chunk_text` 重新做轻量 sentence split，而不是依赖最终 evidence snippet 压缩逻辑。
2. v1 window 策略：
   - 选择 lexical term 命中数最高的 sentence 作为 anchor；
   - 左右各扩展 `sentence_window` 句；
   - 输出做 whitespace collapse 与最大字符数裁剪。
3. 若文本不可切句或无 anchor 命中，则回退到 chunk 文本裁剪后的原文片段。

### 2.5 业界参考与边界判断

1. SQLite FTS5 官方文档确认：
   - `MATCH` 查询应使用显式 quoted string；
   - `ORDER BY bm25(...)` 是标准相关性排序路径；
   - `snippet()` 适合短片段抽取，但 sentence-window 仍可在应用层补齐邻域。
2. 因此 013 选择“FTS5 搜索由 seam 执行，window/snippet 由 retriever 后处理”的拆分：既保留真实 SQLite lexical backend，又不让 013 提前吞掉 019 的 active snapshot owner。

## 3. Design -> Build 映射

1. `knowledge/include/retrieve/SparseRetriever.h`
   - 定义 `SparseRetrieveRequest`、`SparseQueryExpression`、`SparseIndexSearchRequest/Result`、`SparseRetrieverDeps`、`SparseRetrieverPolicy` 与 `SparseRetriever`。
2. `knowledge/src/retrieve/SparseRetriever.cpp`
   - 实现 query expression 构建、hard filter 校验、sentence-window 扩展与错误映射。
3. `tests/unit/knowledge/SparseRetrieverTest.cpp`
   - 使用真实 SQLite FTS5 测试夹具验证 lexical expression / BM25 排序与显式 index failure。
4. `tests/unit/knowledge/SparseRetrieverFilterTest.cpp`
   - 验证 metadata / language / authority filter 与 0-hit 合法成功语义。
5. `tests/unit/knowledge/SparseRetrieverSentenceWindowTest.cpp`
   - 验证 sentence-window 邻域扩展不会越窗或丢失 anchor sentence。
6. `knowledge/CMakeLists.txt`、`tests/unit/knowledge/CMakeLists.txt`
   - 注册 `SparseRetriever` 源文件、头文件与 3 条 unit tests；仅 SQLite 夹具测试目标显式链接 `dasall_sqlite3`。

## 4. 验证计划

1. build-ci configure：`cmake -S . -B build-ci -G "Unix Makefiles"`
2. 定向构建：
   - `cmake --build build-ci --target dasall_knowledge dasall_sparse_retriever_unit_test dasall_sparse_retriever_filter_unit_test dasall_sparse_retriever_sentence_window_unit_test`
3. 定向 `ctest`：
   - `ctest --test-dir build-ci -R "SparseRetriever.*Test" --output-on-failure`

## 5. 完成判定

1. `SparseRetriever` 已把 lexical query expression 与 hard filter 规则固化为可测试 contract。
2. 搜索 seam 缺失或返回 `IndexUnavailable` 时，retriever 必须显式失败，不得伪装成空命中成功。
3. filter 冲突导致的 0 hit 被视为合法成功，且不会自动放宽过滤条件。
4. 013 交付后，019 只需补 active snapshot 读路径与 manifest owner，不需要推翻 013 的 request/result/seam 形状。