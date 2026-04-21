# KNO-TODO-014 dense lane 真实接入设计补充

- 日期：2026-04-21
- 任务：KNO-TODO-014（补齐 dense lane 真实接入）
- 前置：`KNO-TODO-015` 已完成

## 1. 补充背景

014 第一阶段已经完成 lane 编排、partial/degraded 语义和失败形状，但当时 dense lane 仍只能通过 `dense_lane` 回调 seam 表达，原因是 015 的 `VectorRetrieverBridge` 还未落盘。

015 现已完成，因此本补充只解决一个问题：让 `RecallCoordinator` 可以直接消费真实 `VectorRetrieverBridge`，而不是继续把 dense lane 永久停留在 stub 层。

## 2. 设计结论

### 2.1 新增真实 dense bridge 依赖

在 `RecallCoordinatorDeps` 中新增：

1. `std::shared_ptr<const VectorRetrieverBridge> dense_bridge`

执行规则：

1. 若 `dense_bridge` 存在，`run_dense_lane()` 优先调用 `dense_bridge->retrieve()`。
2. 若 `dense_bridge` 不存在，再退回既有 `dense_lane` 回调 seam。
3. 若两者都不存在，沿用既有 `lane_unavailable` 失败语义。

### 2.2 为什么不直接删除 `dense_lane` 回调

本轮保留回调 seam 是有意设计，而不是过渡残留，原因有二：

1. 028 需要稳定构造 `recall_timeout`、部分超时等 lane 级失败，而 015 的 bridge 当前只表达 unavailable / result inconsistency，不表达超时。
2. 保留回调 seam 可以让 coordinator 在真实 bridge 存在时走生产路径，在 bridge 不存在或需要故障注入时走测试路径，避免为了单测再反向污染 bridge 接口。

### 2.3 行为约束

1. `dense_bridge` 与 `dense_lane` 同时存在时，必须优先 bridge，确保真实路径不被 stub 覆盖。
2. `dense_bridge` 返回的 `DenseRecallResult` 仍只保留 lane 级事实，不直接映射 `ErrorInfo`。
3. 014 本轮不引入并行调度，也不改变 `sparse -> dense` 的执行顺序。

## 3. 测试策略

新增一条 014 补充单测：

1. `RecallCoordinatorDenseBridgeTest`
   - 构造真实 `VectorRetrieverBridge`
   - 同时提供一个会失败的 `dense_lane` fallback
   - 断言 coordinator 优先命中 bridge，且 dense hits 成功进入 `RecallCandidateSet`

保留既有测试：

1. `RecallCoordinatorTest`
2. `RecallCoordinatorDegradedTest`
3. `RecallCoordinatorSerialExecutionTest`

原因：

1. 既有测试继续覆盖 lexical-only、partial degrade、双 lane 失败和串行顺序。
2. 新测试只补“真实 bridge 接线优先级”这一缺口。

## 4. 文件范围

### 4.1 新增文件

1. `tests/unit/knowledge/RecallCoordinatorDenseBridgeTest.cpp`

### 4.2 修改文件

1. `knowledge/include/retrieve/RecallCoordinator.h`
2. `knowledge/src/retrieve/RecallCoordinator.cpp`
3. `tests/unit/knowledge/CMakeLists.txt`
4. 所有 `RecallCoordinatorDeps` 初始化点

## 5. 验收命令

```bash
cmake -S . -B build-ci -G "Unix Makefiles"
cmake --build build-ci --target \
  dasall_recall_coordinator_unit_test \
  dasall_recall_coordinator_degraded_unit_test \
  dasall_recall_coordinator_serial_execution_unit_test \
  dasall_recall_coordinator_dense_bridge_unit_test
ctest --test-dir build-ci -R "RecallCoordinator.*Test" --output-on-failure
```

## 6. 完成判定

满足以下条件即可认为 014 dense lane 真实接入补齐完成：

1. `RecallCoordinator` 可以直接消费 `VectorRetrieverBridge`。
2. bridge 与 fallback seam 同时存在时，真实 bridge 优先。
3. 既有 coordinator 单测基线不回归，新补充的 bridge 接线单测通过。
