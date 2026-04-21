# KNO-TODO-010 Reranker 设计收敛

## 1. 输入与目标

1. 来源任务：`KNO-TODO-010 | 实现 Reranker`。
2. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.13.2 `Reranker` 卡片与 7 KNO-D04。
3. 前置满足：`KNO-TODO-017` 已提供 `FreshnessSnapshot`；`KNO-TODO-009` 已提供 `RetrievalPlan`；本轮仍不依赖真实 retriever 或 I/O。
4. 本轮目标：只落纯计算排序层，为 011 `EvidenceAssembler` 提供稳定的 `RankedHitSet` 输入。

## 2. 设计结论

### 2.1 边界与职责

1. `Reranker` 负责：
   - 以 `chunk_id` 去重；
   - sparse/dense 两路 RRF 融合；
   - stale penalty；
   - authority weighting；
   - cutoff 与 top-k 收敛。
2. `Reranker` 不负责：
   - 发起 recall；
   - 访问 index / vector backend；
   - 生成 `EvidenceBundle`；
   - 做 refresh 或 health 判定。

### 2.2 数据与接口

1. 补充 `knowledge/include/retrieve/RecallTypes.h`：
   - `RecallHit`
   - `RecallCandidateSet`
2. 新增 `knowledge/include/rerank/Reranker.h`：
   - `RankedHit`
   - `RankedHitSet`
   - `RerankPolicy`
   - `Reranker::rerank()`
3. `RecallHit` 本轮补充 `authority_level`，因为 authority weighting 必须在 rerank 阶段稳定可见，不能让 011/013 再自行补字段。

### 2.3 规则收敛

1. RRF 公式采用：

$$
\text{score}(d) = \sum_{r \in \text{lanes}} \frac{1}{k + \text{rank}_r(d)}
$$

其中 `k = RerankPolicy.rrf_k`，v1 默认 `60`，允许范围 `[1, 200]`。

2. 执行顺序固定为：
   - lane 内去重；
   - RRF raw score 聚合；
   - 归一化到 `[0, 1]`；
   - authority weighting；
   - stale penalty；
   - cutoff / top-k。
3. penalty/boost 纪律：
   - `stale_penalty_factor <= 1.0`
   - `normative_authority_boost >= 1.0`
   - `advisory_authority_boost <= 1.0`
4. 若 `RerankPolicy` 非法，回退 lexical-first 排序，并标记 `degraded=true`，不抛业务异常。
5. 空 `RecallCandidateSet` 是合法输入，返回空 `RankedHitSet`。

### 2.4 评审补充

1. 为了让 stale penalty 真正可观测，本轮在 RRF 归一化之后不再二次全局归一，而是直接对归一化分数乘 penalty/boost 后做 `[0, 1]` clamp。否则全局 stale penalty 会在再次归一化时被抵消。
2. `RecallHit` / `RankedHit` 仍是 Knowledge 内部 supporting types，不升格到 contracts，保持 KNO-C010 约束。

## 3. Design -> Build 映射

1. `knowledge/include/retrieve/RecallTypes.h`
   - 定义 `RecallHit` / `RecallCandidateSet`。
2. `knowledge/include/rerank/Reranker.h`
   - 定义 `RerankPolicy`、`RankedHit`、`RankedHitSet`、`Reranker`。
3. `knowledge/src/rerank/Reranker.cpp`
   - 实现 RRF merge、penalty/boost、fallback 与排序截断。
4. `tests/unit/knowledge/RerankerTest.cpp`
   - 验证空输入与 invalid policy fallback。
5. `tests/unit/knowledge/HybridRrfMergeTest.cpp`
   - 验证 hybrid RRF merge 和去重。
6. `tests/unit/knowledge/RerankerFreshnessPenaltyTest.cpp`
   - 验证 stale penalty 与 authority weighting。

## 4. 验证计划

1. Build_CMakeTools：`dasall_knowledge`、`dasall_reranker_unit_test`、`dasall_hybrid_rrf_merge_unit_test`、`dasall_reranker_freshness_penalty_unit_test`。
2. RunCtest / 显式 `ctest`：`RerankerTest`、`HybridRrfMergeTest`、`RerankerFreshnessPenaltyTest`。
3. build-ci 验收：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_knowledge dasall_reranker_unit_test dasall_hybrid_rrf_merge_unit_test dasall_reranker_freshness_penalty_unit_test`
   - `ctest --test-dir build-ci -R "(Reranker|HybridRrfMerge).*Test" --output-on-failure`

## 5. 完成判定

1. RRF 融合、penalty/boost 和空结果语义都可稳定测试。
2. invalid policy 只允许 degraded fallback，不允许抛业务异常或发起 I/O。
3. 010 交付后，011 可直接消费稳定的 `RankedHitSet` 输入，而不再重写排序逻辑。