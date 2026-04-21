# KNO-TODO-009 CorpusRouter 设计收敛

## 1. 输入与目标

1. 来源任务：`KNO-TODO-009 | 实现 CorpusRouter`。
2. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.10、6.13.1 `CorpusRouter` 卡片与 7 KNO-D02。
3. 前置满足：`KNO-TODO-008` 已提供 `NormalizedQuery`，`KNO-TODO-016` 已提供 `CorpusCatalogSnapshot`，`KNO-TODO-017` 已提供 `FreshnessSnapshot`。
4. 本轮目标：只生成稳定的 `RetrievalPlan` / 路由结果，为 013/014/015/010 提供可执行的 recall 输入，不提前做真正召回或 rerank。

## 2. 设计结论

### 2.1 边界与职责

1. `CorpusRouter` 负责：
   - corpus 过滤；
   - retrieval mode 选择；
   - stale/read 准入；
   - sparse/dense top-k 分配；
   - route reason code 生成。
2. `CorpusRouter` 不负责：
   - 访问 lexical / vector backend；
   - 生成 recall hit；
   - 做最终排序或 evidence packing；
   - 决定 refresh 执行。

### 2.2 数据与接口

1. 新增 `RetrievalPlan`：固定 `mode`、`corpus_ids`、`sparse_top_k`、`dense_top_k`、`allow_partial_results`、`allow_stale_snapshot`、`max_projection_items` 与 `route_reason_codes`。
2. 新增 `RoutePlanResult`：
   - 成功时携带 `RetrievalPlan`；
   - 失败时携带结构化 `ErrorInfo`；
   - 无论成功或失败都暴露 `route_reason_codes`，供上层 observability / facade 复用。
3. `CorpusRouter::build_plan()` 输入固定为：
   - `NormalizedQuery`
   - `KnowledgeConfigSnapshot`
   - `CorpusCatalogSnapshot`
   - `FreshnessSnapshot`

### 2.3 规则收敛

1. corpus 过滤顺序固定为：
   - `domain_tags`
   - `TrustLevel::Trusted`
   - `allowed_corpora`
   - `authority_level`
2. authority floor 由 query kind 决定：
   - `PolicyEvidence` -> `Normative`
   - `FactLookup` / `ProcedureLookup` -> `Reference`
   - `DiagnosticContext` -> `Advisory`
3. freshness 纪律：
   - `Fresh` -> 可路由；
   - `StaleAllowed` 且 query 允许 stale -> 可路由并标记 `allow_stale_snapshot=true`；
   - `StaleRejected` / `Unknown` -> `IndexStaleRejected`。
4. mode 纪律：
   - `DiagnosticContext` 在 vector fully available 时优先 `DenseOnly`；
   - `FactLookup` / `ProcedureLookup` / `PolicyEvidence` 在 vector fully available 时优先 `Hybrid`；
   - 只要任一候选 corpus 不支持 dense 路径，就显式退化到 `LexicalOnly`，并写 `corpus_mode_capability_downgraded`。
5. Router 绝不默认扫全库；过滤后为空时必须返回 `NoCorpusAvailable`。

### 2.4 评审补充

1. 由于当前 `RetrievalPlan` 仍只有一组 `corpus_ids`，本轮不拆分 lane-specific corpus 列表，而采用“所有入选 corpus 必须共同支持所选 mode”的保守策略。这样可保证 013/015 的消费面稳定，不在 009 提前引入 per-lane 分叉结构。
2. `allow_partial_results` 只在 `Hybrid && allow_budget_degrade=true` 时打开，确保 014 后续可沿用这一降级契约，而不重新解释 route policy。

## 3. Design -> Build 映射

1. `knowledge/include/query/CorpusRouter.h`
   - 定义 `RetrievalPlan`、`RoutePlanResult`、`CorpusRouter`。
2. `knowledge/src/query/CorpusRouter.cpp`
   - 实现 filter、freshness gate、mode 选择与 reason code。
3. `tests/unit/knowledge/CorpusRouterTest.cpp`
   - 验证正常 hybrid 路由与 no-corpus 失败。
4. `tests/unit/knowledge/CorpusRouterFreshnessPolicyTest.cpp`
   - 验证 stale allow / stale reject。
5. `tests/unit/knowledge/CorpusRouterModeSelectionTest.cpp`
   - 验证 dense-only 与 lexical fallback。

## 4. 验证计划

1. Build_CMakeTools：`dasall_knowledge`、`dasall_corpus_router_unit_test`、`dasall_corpus_router_freshness_policy_unit_test`、`dasall_corpus_router_mode_selection_unit_test`。
2. RunCtest / 显式 `ctest`：`CorpusRouterTest`、`CorpusRouterFreshnessPolicyTest`、`CorpusRouterModeSelectionTest`。
3. build-ci 验收：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_knowledge dasall_corpus_router_unit_test dasall_corpus_router_freshness_policy_unit_test dasall_corpus_router_mode_selection_unit_test`
   - `ctest --test-dir build-ci -R "CorpusRouter.*Test" --output-on-failure`

## 5. 完成判定

1. Router 不再默认扫全库，过滤空集时必须显式失败。
2. stale / vector / authority 组合的 route 选择都可二值验证。
3. 009 交付后，013/014/015/010 可直接消费稳定的 `RetrievalPlan` 输入，而不再重写路由规则。