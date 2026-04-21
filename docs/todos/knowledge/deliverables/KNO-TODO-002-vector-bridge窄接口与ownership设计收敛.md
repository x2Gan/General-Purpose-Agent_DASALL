# KNO-TODO-002 vector bridge 窄接口与 ownership 设计收敛

- 日期：2026-04-21
- 任务：KNO-TODO-002
- 状态：已收敛
- 对应 Blocker：KNO-BLK-002

## 1. 输入与约束

1. `VectorRetrieverBridge` 是 Knowledge hybrid recall 的唯一 dense lane 入口；如果 owner、注入方向和失败语义不冻结，`RecallCoordinator`、profile compatibility、failure/degrade integration 都会反复返工。
2. ADR-006 要求 llm 只掌消息渲染权，不得反向接管 Knowledge 的检索与上下文语义。因此 query encoding 可以复用外部实现，但编码接口的语义边界不能下沉到 llm owner。
3. `profiles/*/runtime_policy.yaml` 已形成两类合法组合：
   - `desktop_full` / `cloud_full` / `edge_balanced`：`memory_vector=true`
   - `edge_minimal` / `factory_test`：`memory_vector=false`
   `memory_vector=true` 代表 capability intent，而不是“dense lane 必然在线”。
4. memory 侧已冻结 vector backend 默认顺序为 `sqlite-vss -> none`，但 concrete backend 仍未落盘；因此 Knowledge 不能把 hybrid 设计建立在某个 concrete sqlite-vss 实现已经存在的假设上。

## 2. 本地证据

| 证据 | 观察结果 | 结论 |
|---|---|---|
| knowledge 工厂签名 | `create_knowledge_service(..., std::unique_ptr<IQueryEncoder>, std::unique_ptr<IVectorRecallStore>)` 已声明双注入 | 组合根方向已经偏向“Knowledge 拥有 port，外部注入实现” |
| 当前 `VectorRetrieverBridge` 卡片 | 仅定义了 `IQueryEncoder`，但类构造函数仍吃 `BackendConfig`，且未给出 `IVectorRecallStore` 接口 | 详设内部口径不一致，必须在 002 统一 |
| memory 向量公共接口 | `VectorMemoryIndexAdapter::search(const std::string& query_text, int top_k)`；`IEmbeddingAdapter` 归 memory 所有 | memory 的向量接口服务于 memory 自身存储/检索路径，不应反向定义 Knowledge dense lane 语义 |
| memory backend 冻结结论 | v1 默认 `sqlite-vss -> none`；当前只有 `UnavailableVectorMemoryIndexAdapter`，无 concrete sqlite-vss backend | Knowledge 不能直接依赖 concrete memory backend，只能依赖窄 port + adapter |
| llm / services 现状 | 工作区没有现成的 embedding/query encoder 公共接口 | `IQueryEncoder` 若下沉到 llm 或 services，会额外制造新的跨模块依赖冻结任务 |
| tests 规划 | 详设文件布局已预留 `tests/mocks/include/MockVectorRecallStore.h` | `IVectorRecallStore` 更适合作为 Knowledge 自有 module-local port，用于单测与 degrade 用例替身 |

## 3. 设计结论

### 3.1 owner 归属

1. `IQueryEncoder` owner 冻结为 Knowledge 模块，头文件位置：`knowledge/include/retrieve/IQueryEncoder.h`。
2. `IVectorRecallStore` owner 同样冻结为 Knowledge 模块，头文件位置：`knowledge/include/retrieve/IVectorRecallStore.h`。
3. 两者都保持 module-local，不进入 `contracts/`，也不迁移到 `memory/include/` 或 `llm/include/`。

### 3.2 注入方向

1. 注入方向固定为 `composition root -> Knowledge ports -> VectorRetrieverBridge`。
2. `create_knowledge_service()` 继续通过构造注入 `std::unique_ptr<IQueryEncoder>` 与 `std::unique_ptr<IVectorRecallStore>`；Knowledge 核心实现不主动查询 memory/llm/service locator。
3. 外部模块只提供 adapter implementation：
   - memory 侧可提供 `MemoryVectorRecallStoreAdapter`，包装 `memory::VectorMemoryIndexAdapter`；
   - 未来如有 embedding provider，可提供 `LlmQueryEncoderAdapter` 或其他实现，但它们都不改变 owner 归属。

### 3.3 窄接口边界

1. `IQueryEncoder` 是 Knowledge 的 inbound capability port，只负责把 query text 编码为 dense query embedding，不承担 profile 解释、lane 调度、rerank 或 backend health 逻辑。
2. `IVectorRecallStore` 是 Knowledge 的 outbound recall port，只负责执行 dense retrieval 并返回 Knowledge 可消费的结果，不承担 query normalization、hybrid merge、degrade policy 或 telemetry 聚合。
3. Knowledge 核心不直接 include `memory/include/vector/*` 或未来的 llm/provider 头文件；跨模块 concrete 类型必须被 adapter 吞掉。

### 3.4 输入输出语义冻结

```cpp
enum class DenseQueryInputMode {
  TextOnly,
  EmbeddingRequired,
};

struct DenseQueryRequest {
  std::string query_text;
  std::vector<float> query_embedding;
  std::size_t top_k = 0;
};

class IQueryEncoder {
public:
  virtual ~IQueryEncoder() = default;
  virtual std::vector<float> encode(std::string_view query_text) const = 0;
  virtual bool available() const = 0;
};

class IVectorRecallStore {
public:
  virtual ~IVectorRecallStore() = default;
  virtual bool available() const = 0;
  virtual DenseQueryInputMode query_input_mode() const = 0;
  virtual std::vector<RecallHit> search(const DenseQueryRequest& request) const = 0;
};
```

4. `DenseQueryInputMode` 解决当前 owner 冲突的根因：
   - 对 memory 当前 public adapter 而言，`query_input_mode() = TextOnly`，因为 `VectorMemoryIndexAdapter::search(query_text, top_k)` 已内聚 query encoding；
   - 对未来只接受向量输入的 ANN / remote backend，`query_input_mode() = EmbeddingRequired`，此时 `VectorRetrieverBridge` 才依赖 `IQueryEncoder` 产出 `query_embedding`。
5. `IVectorRecallStore` 直接返回 Knowledge 自有的 `RecallHit`，因此 backend 私有命中结构必须在 adapter 内部完成映射；`VectorRetrieverBridge` 不再承担 backend-specific hit schema 归一化。

### 3.5 dense lane 退化语义

1. 若 `memory_vector=false`，Knowledge 直接走 lexical-only；这是合法 profile 组合，不视为错误。
2. 若 `memory_vector=true` 但 `IVectorRecallStore` 缺失、`available()==false`，dense lane 返回 `VectorBackendUnavailable`，由 `RecallCoordinator` 退化为 lexical-only。
3. 若 `query_input_mode() = EmbeddingRequired` 且 `IQueryEncoder` 缺失或 `available()==false`，同样按 `VectorBackendUnavailable` 处理，不额外引入第二套错误分类。
4. 若 dense lane 超时，则沿用已有 `RecallTimeout` 语义；只要 sparse lane 成功且 `allow_partial_results=true`，整体结果仍可 `ok=true, degraded=true`。

### 3.6 禁止事项

1. 禁止把 `IQueryEncoder` 下沉为 llm owner。否则 Knowledge hybrid 路径会被迫依赖 llm 公共头和 provider 形态，违反当前依赖治理方向。
2. 禁止把 `IVectorRecallStore` 直接替换为 `memory::VectorMemoryIndexAdapter` 公共类型。否则 Knowledge 会被 memory 的 doc schema、best-effort/no-op 语义和 concrete backend 演进节奏反向牵引。
3. 禁止在 Knowledge 核心内再做“本地第二套向量索引 fallback”。v1 dense lane 只有外部注入 store 或 unavailable 两种状态，不复制 memory vector lifecycle。

## 4. 执行流冻结

1. `RecallCoordinator` 决定是否启动 dense lane，但不拥有 encoder/store 生命周期。
2. `VectorRetrieverBridge` 先检查 `vector_enabled`、`vector_store_`、`vector_store_->available()`。
3. 若 `query_input_mode() = EmbeddingRequired`，再检查 `query_encoder_` 与 `query_encoder_->available()` 并生成 `query_embedding`。
4. `VectorRetrieverBridge` 调用 `IVectorRecallStore::search()` 获取 `RecallHit` 列表。
5. dense lane 失败只返回 lane 级失败或 warning，不直接决定整体请求失败；是否退化由 `RecallCoordinator` 统一裁定。

## 5. Design -> Build 映射

| 后续任务 | Build 入口 | 本次冻结给出的前置结论 |
|---|---|---|
| KNO-TODO-015 `VectorRetrieverBridge` | `knowledge/include/retrieve/VectorRetrieverBridge.h`、`knowledge/include/retrieve/IQueryEncoder.h`、`knowledge/include/retrieve/IVectorRecallStore.h`、`knowledge/src/retrieve/VectorRetrieverBridge.cpp` | `VectorRetrieverBridge` 只编排 port，不拥有 backend config 或 concrete backend schema |
| KNO-TODO-014 `RecallCoordinator` | `knowledge/include/retrieve/RecallCoordinator.h`、`knowledge/src/retrieve/RecallCoordinator.cpp` | dense lane 的可用性判断来自 `VectorRetrieverBridge.available()` / lane result，而不是 profile 字段硬编码 |
| KNO-TODO-028 failure/degrade integration | `tests/integration/knowledge/KnowledgeFailureDegradeTest.cpp` | `memory_vector=true` 但 store/encoder unavailable 仍要退化为 lexical-only，不得升级为进程级失败 |
| KNO-TODO-029 profile compatibility | `tests/integration/knowledge/KnowledgeProfileCompatibilityTest.cpp` | `knowledge=true && memory_vector=false` 合法；`memory_vector=true` 也不等于 dense lane 必然在线 |

## 6. 本任务三件套

- 代码目标：更新 knowledge 详设与专项 TODO，冻结 `IQueryEncoder` / `IVectorRecallStore` owner、注入方向和 module-local 边界。
- 测试目标：确保工厂注入方向、dense lane degrade 语义和 memory vector adapter 的适配边界都能从文档中直接检索。
- 验收命令：

```bash
rg -n "IQueryEncoder|IVectorRecallStore|DenseQueryInputMode|VectorRetrieverBridge|VectorBackendUnavailable|memory_vector=false" \
  docs/architecture/DASALL_knowledge子系统详细设计.md \
  docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md \
  docs/todos/knowledge/deliverables/KNO-TODO-002-vector-bridge窄接口与ownership设计收敛.md
```

## 7. 风险与回退

1. 风险：当前 memory concrete sqlite-vss backend 尚未落盘，`MemoryVectorRecallStoreAdapter` 只能先对接 unavailable baseline。
   - 处置：002 只冻结 port owner 与边界，不把 concrete backend 可用性当成解阻前提。
2. 风险：未来若把 `IQueryEncoder` 误升格到 llm 或 services，会制造新的跨模块 ABI 冻结成本。
   - 处置：继续保持 module-local owner；外部只提供 adapter。
3. 风险：若直接重用 `VectorMemoryIndexAdapter` 作为 Knowledge 公共依赖，dense lane 的返回语义会被 memory best-effort/no-op 语义污染。
   - 处置：坚持 `IVectorRecallStore` 返回 Knowledge 自有 `RecallHit`，把 schema 映射放在 adapter 内。
4. 回退策略：若 015 实现期发现 concrete store 仍不可用，保持 `IVectorRecallStore` 注入为 mock/unavailable adapter，继续推进 `RecallCoordinator`、degrade、profile 兼容路径；禁止重新打开 owner 归属争论。

## 8. 收敛结论

1. `IQueryEncoder` 与 `IVectorRecallStore` 均归 Knowledge 模块所有，并保持 module-local。
2. Knowledge hybrid 路径的正确依赖方向是“外部 adapter 注入实现”，不是“Knowledge 直接依赖 memory/llm concrete API”。
3. `KNO-BLK-002` 可以关闭；后续剩余工作属于 port 实现与 adapter 接线，而不是继续争论 bridge owner。