# KNO-TODO-014 RecallCoordinator 设计收敛

## 1. 输入与目标

1. 来源任务：`KNO-TODO-014 | 实现 RecallCoordinator lane 编排`。
2. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.10、6.13.2 `RecallCoordinator` 卡片与 7 KNO-D03。
3. 前置满足：`KNO-TODO-013` 已提供 `SparseRetriever` 与稳定的 sparse lane seam，`KNO-TODO-002` 已冻结 dense lane 的 owner 和 module-local ports，`KNO-TODO-019` 已补 active snapshot read path，但 015 `VectorRetrieverBridge` 仍未落盘。
4. 本轮目标：落盘 sparse/dense lane 的串行编排、partial results / degraded 语义与双 lane 失败显式结果形状，同时避免在 014 内抢跑 015 dense backend concrete owner。

## 2. 设计结论

### 2.1 边界与职责

1. `RecallCoordinator` 负责：
   - 根据 `RetrievalPlan.mode` 决定要执行的 lane；
   - 协调 sparse/dense lane 的成功、失败与 partial results 语义；
   - 输出统一的 `RecallCandidateSet`，供下游 rerank 消费。
2. `RecallCoordinator` 不负责：
   - 直接做最终融合评分；
   - 直接生成 `EvidenceBundle`；
   - 拥有向量 backend 生命周期；
   - 把 lane 失败直接映射成 `ErrorInfo`。

### 2.2 接口收敛

1. 详细设计卡片把接口简写为 `recall(const RetrievalPlan&)`，但 013 已证明 sparse lane 至少需要 `NormalizedQuery + RetrievalPlan`。
2. 为避免回改 009，本轮新增 `RecallRequest`：
   - `query::NormalizedQuery normalized_query`
   - `query::RetrievalPlan plan`
   - `optional<string> required_language`
3. 因 dense lane 还没有 015 concrete owner，本轮新增最小 dense seam：
   - `DenseRecallRequest`
   - `DenseRecallResult`
   - `RecallCoordinatorDeps::dense_lane`
4. 为表达“双 lane 都失败，但 coordinator 自己不产 `ErrorInfo`”这一语义缺口，本轮新增 `RecallCoordinatorResult`：
   - `ok=true`：返回可消费的 `RecallCandidateSet`
   - `ok=false`：仅返回 `failure_reason_codes`，由 012 facade 统一映射到 `ErrorInfo`

### 2.3 v1 编排策略

1. v1 固定串行执行，不根据 `max_parallel_recall` 启动 `std::async`；该字段仅作为未来 v2 并发扩展的配置占位。
2. lane 执行顺序固定为：
   - `LexicalOnly`：只跑 sparse
   - `DenseOnly`：只跑 dense
   - `Hybrid`：固定 `sparse -> dense`
3. 若 hybrid 双 lane 中只有单 lane 成功：
   - `allow_partial_results=true`：返回 `ok=true`，`degraded=true`
   - `allow_partial_results=false`：返回 `ok=false`
4. lane 失败 reason code 统一收敛为稳定字符串，例如：
   - `sparse_index_unavailable`
   - `dense_recall_timeout`
   - `dense_lane_unavailable`

### 2.4 与 015/012 的边界

1. dense lane 当前只通过 stub/mock seam 表达，不硬依赖 015 `VectorRetrieverBridge`。
2. 012 facade 下一轮只需要消费 `RecallCoordinatorResult`：
   - `ok=true` 时继续 rerank/evidence；
   - `ok=false` 时把 `failure_reason_codes` 统一映射为 facade 级 `ErrorInfo`。
3. 因此 014 必须保持“lane 级事实”而不是直接升级成跨层错误对象。

## 3. Design -> Build 映射

1. `knowledge/include/retrieve/RecallCoordinator.h`
   - 定义 `RecallRequest`、`DenseRecallRequest/Result`、`RecallCoordinatorResult`、`RecallCoordinatorDeps`、`RecallCoordinatorPolicy` 与 `RecallCoordinator`。
2. `knowledge/src/retrieve/RecallCoordinator.cpp`
   - 实现串行 lane 编排、partial results、双 lane 失败和 stable reason codes。
3. `tests/unit/knowledge/RecallCoordinatorTest.cpp`
   - 验证 lexical-only 只跑 sparse，与 hybrid 双 lane 成功聚合。
4. `tests/unit/knowledge/RecallCoordinatorDegradedTest.cpp`
   - 验证 dense lane 失败但 sparse 成功时的 degraded 语义，以及双 lane 都失败时的显式失败。
5. `tests/unit/knowledge/RecallCoordinatorSerialExecutionTest.cpp`
   - 验证 v1 固定串行执行，`Hybrid` 顺序恒为 `sparse -> dense`。
6. `knowledge/CMakeLists.txt`、`tests/unit/knowledge/CMakeLists.txt`
   - 注册 `RecallCoordinator` 头文件、源文件与 3 条 unit tests。

## 4. 验证计划

1. Build_CMakeTools：
   - `dasall_knowledge`
   - `dasall_recall_coordinator_unit_test`
   - `dasall_recall_coordinator_degraded_unit_test`
   - `dasall_recall_coordinator_serial_execution_unit_test`
2. build-ci 回退路径：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_knowledge dasall_recall_coordinator_unit_test dasall_recall_coordinator_degraded_unit_test dasall_recall_coordinator_serial_execution_unit_test`
   - `ctest --test-dir build-ci -R "RecallCoordinator.*Test" --output-on-failure`

## 5. 完成判定

1. `RecallCoordinator` 已把 sparse/dense lane 成功、partial success 与双 lane 失败固化成可测试 contract。
2. dense lane 失败不会拖垮 sparse lane 的合法返回。
3. v1 仍固定串行执行，不引入并行调度面。
4. 014 交付后，012 facade 只需消费 `RecallCoordinatorResult`，不必再重建 lane 失败语义。