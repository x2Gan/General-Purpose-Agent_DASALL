# KNO-TODO-015 VectorRetrieverBridge 与 IQueryEncoder seam 设计收敛

- 日期：2026-04-21
- 任务：KNO-TODO-015
- 状态：Design 收敛，进入 Build
- 前置：`KNO-TODO-002` 已完成，`KNO-BLK-002` 已解除

## 1. 目标与边界

### 1.1 目标

本任务在 Knowledge 模块内落盘 dense lane 的最小可执行桥接层，补齐以下能力：

1. 以 Knowledge 自有 module-local ports 暴露 query encoding 与 vector recall 能力。
2. 以 `VectorRetrieverBridge` 统一承接 dense lane 的输入构造、可用性判断与 lane 级失败语义。
3. 为后续 `KNO-TODO-014` 的 dense lane 真实接入提供稳定的 `DenseRecallRequest -> DenseRecallResult` 入口。

### 1.2 非职责

本任务明确不承担以下内容：

1. 不接入 memory/llm 的 concrete adapter。
2. 不决定 profile 是否启用 hybrid，也不做 lane 选择。
3. 不在 Knowledge 内实现第二套向量索引 fallback。
4. 不扩展 facade、refresh、integration 闭环；这些分别由 014、028、029 承接。

## 2. 输入依据

1. `docs/architecture/DASALL_knowledge子系统详细设计.md`
2. `docs/todos/knowledge/deliverables/KNO-TODO-002-vector-bridge窄接口与ownership设计收敛.md`
3. `docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md`
4. 现有代码：`knowledge/include/retrieve/RecallCoordinator.h`、`knowledge/src/retrieve/RecallCoordinator.cpp`

## 3. 数据与接口收敛

### 3.1 端口 owner 与注入方向

1. `IQueryEncoder` owner 固定为 `knowledge/include/retrieve/IQueryEncoder.h`。
2. `IVectorRecallStore` owner 固定为 `knowledge/include/retrieve/IVectorRecallStore.h`。
3. 注入方向固定为 `composition root -> Knowledge ports -> VectorRetrieverBridge`。

### 3.2 核心结构

新增以下结构与接口：

1. `DenseQueryInputMode`
   - `TextOnly`
   - `EmbeddingRequired`
2. `DenseQueryRequest`
   - `query_text`
   - `query_embedding`
   - `allowed_corpus_ids`
   - `required_tags`
   - `required_language`
   - `top_k`
3. `IQueryEncoder`
   - `encode(std::string_view)`
   - `available()`
4. `IVectorRecallStore`
   - `available()`
   - `query_input_mode()`
   - `search(const DenseQueryRequest&)`
5. `VectorRetrieverBridge`
   - `available()`
   - `retrieve(const DenseRecallRequest&)`

### 3.3 Dense recall 输入输出归属

1. `DenseRecallRequest` 和 `DenseRecallResult` 从 `RecallCoordinator` 头文件迁移到 `VectorRetrieverBridge` 头文件。
2. `RecallCoordinator` 继续消费这两个类型，但其 owner 转为 dense bridge 语义域。
3. `DenseRecallResult.failure_reason_codes` 保持 lane 级稳定 reason code，供 014/028/029 直接复用。

## 4. 失败语义

### 4.1 lane 级失败码

本任务固定以下 dense lane failure reason code：

1. `vector_backend_unavailable`
2. `request_inconsistent`
3. `result_inconsistent`

### 4.2 映射原则

1. `vector_store` 缺失或 `available()==false` 时返回 `vector_backend_unavailable`。
2. `query_input_mode() == EmbeddingRequired` 且 encoder 缺失、不可用或返回空 embedding 时，也统一返回 `vector_backend_unavailable`。
3. bridge 不把 lane 失败直接升级为 `ErrorInfo`；是否退化由 `RecallCoordinator` 决定。

## 5. 流程与时序

### 5.1 正常流程

1. `RecallCoordinator` 或测试 harness 构造 `DenseRecallRequest`。
2. `VectorRetrieverBridge::retrieve()` 校验 request 一致性。
3. bridge 检查 `vector_store` 可用性。
4. 若 backend 需要 embedding，则检查 encoder 可用性并生成 query embedding。
5. bridge 构造 `DenseQueryRequest`，透传 corpus/tag/language/top-k 过滤。
6. `IVectorRecallStore::search()` 返回 `RecallHit` 列表。
7. bridge 校验命中结构一致性，输出 `DenseRecallResult`。

### 5.2 退化流程

1. dense lane 不可用时，bridge 返回 `ok=false` 且 `failure_reason_codes={"vector_backend_unavailable"}`。
2. 014 中 `RecallCoordinator` 若同时有 sparse 成功且允许 partial，将其映射为 degraded success。
3. 028/029 直接消费该稳定 reason code 验证 hybrid degrade 与 profile compatibility。

## 6. 文件范围

### 6.1 新增文件

1. `knowledge/include/retrieve/IQueryEncoder.h`
2. `knowledge/include/retrieve/IVectorRecallStore.h`
3. `knowledge/include/retrieve/VectorRetrieverBridge.h`
4. `knowledge/src/retrieve/VectorRetrieverBridge.cpp`
5. `tests/unit/knowledge/VectorRetrieverBridgeTest.cpp`
6. `tests/unit/knowledge/VectorRetrieverBridgeUnavailableTest.cpp`

### 6.2 修改文件

1. `knowledge/include/retrieve/RecallCoordinator.h`
2. `knowledge/src/retrieve/RecallCoordinator.cpp`
3. `knowledge/CMakeLists.txt`
4. `tests/unit/knowledge/CMakeLists.txt`

## 7. Design -> Build 映射

| Build 项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| ports 头文件 | 新增 `IQueryEncoder.h`、`IVectorRecallStore.h` | 编译通过，接口可被 unit test fake 实现 | `cmake --build build-ci --target dasall_knowledge` |
| dense bridge | 新增 `VectorRetrieverBridge.h/.cpp`，迁移 `DenseRecallRequest/Result` owner | 正例覆盖 `TextOnly` 和 `EmbeddingRequired` 两种输入模式 | `ctest --test-dir build-ci -R "VectorRetrieverBridge.*Test" --output-on-failure` |
| recall 接口对齐 | `RecallCoordinator` 改为消费 bridge 头中的 dense 类型 | 原有 014 前置单测继续通过 | `ctest --test-dir build-ci -R "RecallCoordinator.*Test" --output-on-failure` |

## 8. 风险与回退

1. 风险：当前尚无 concrete vector adapter，bridge 只能靠 fake/unavailable store 验证。
   - 处置：015 只固化端口和 bridge，不抢跑 adapter 接线。
2. 风险：dense request 过滤字段如果漏传，会导致 028/029 的行为和 lexical lane 不一致。
   - 处置：`DenseQueryRequest` 明确保留 corpus/tag/language/top-k 过滤字段，并在正例测试中断言。
3. 回退：若 014 接线时发现 dense request owner 仍不稳，只允许在 Knowledge 内调整 dense 类型位置，不重新讨论 owner 归属。

## 9. 完成判定

满足以下条件即可进入 Done：

1. bridge 与 ports 均已落盘并编译通过。
2. `VectorRetrieverBridgeTest`、`VectorRetrieverBridgeUnavailableTest` 通过。
3. `RecallCoordinator` 继续能消费 dense lane 类型，未破坏既有 lexical/hybrid 单测基线。
