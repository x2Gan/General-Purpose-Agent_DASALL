# KNO-TODO-036 production-grade embedding / query encoder seam 双轨任务包

## 1. 输入与目标

1. 来源任务：`KNO-TODO-036 | 接入 production-grade embedding / query encoder seam`。
2. 上游前置：`KNO-TODO-035-B` 已完成，runtime-owned hybrid canary seam 已固定 owner/allowlist/ready marker，但 live dense path 仍残留“经由 detached adapter 对 query text 做隐式本地 embedding”的旧语义。
3. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.5、6.6、6.10、6.13.1、6.13.2；`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中第二阶段 `KNO-TODO-036` 定义；ADR-006 / ADR-007 / ADR-008 owner boundary。
4. 本轮目标：把 live dense path 从默认依赖 `SimpleLocalEmbeddingAdapter` 的 hash 结果，收口为显式 `IQueryEncoder` / embedding provider seam；当 encoder/provider 缺失、空 embedding 或 detached backend 不可用时，仍保持 lexical-only fail-safe，不把 rollout 主控下沉到 Knowledge public surface。

## 2. 研究证据

### 2.1 本地证据

1. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 在本轮前仍让 `RuntimeKnowledgeVectorRecallStore` 走 `DenseQueryInputMode::TextOnly`，并通过 detached adapter 的 `search(query_text)` 命中 `dense.sqlite`；这会让 runtime dense path 的语义继续由 Memory text-search adapter 的隐式 embedding 行为决定，而不是由 runtime/knowledge 明确提供 query encoder。
2. `memory/src/vector/DetachedVectorIndexFactory.cpp` 在本轮前会默认创建 `SimpleLocalEmbeddingAdapter`，即使 production/live path 没有显式声明允许 local fallback；这与 `KNO-TODO-035` 刚固定的 runtime-owned rollout 主控不一致。
3. `knowledge/src/retrieve/VectorRetrieverBridge.cpp` 已支持 `DenseQueryInputMode::EmbeddingRequired` 和 `IQueryEncoder`，但 installed/live factory 还没有把 query encoder seam 真正接入 runtime 组合根，导致 bridge capability 与 live runtime wiring 不一致。
4. `memory/src/vector/SqliteVssVectorBackend.cpp` 的 public `search(query_text)` 仍通过 embedder 生成 query embedding；若直接拿它定义 knowledge dense semantics，就会把 Memory 内部 adapter 细节误提升为 Knowledge live contract。同时，新实例在已有 DB 上 `indexed_doc_count=0` 时存在 fresh handle 下的潜在空命中风险，需要补 `search_embedding()` 和 indexed count refresh。
5. `tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp`、`RuntimeKnowledgeHybridCanaryIntegrationTest.cpp` 与 `VectorRetrieverBridgeTest.cpp` 已锁定 runtime owner/canary seam 和 bridge degrade 口径；036 必须在不回退这些边界的前提下，把 live dense path 改为显式 encoder-driven。

### 2.2 外部实践

1. Azure AI Search《Create a vector query in Azure AI Search》明确指出：vector query 的 query 自身必须是 vector，应用侧要么显式调用 embedding model/API，要么为 index 配置 query-time vectorizer；并建议 query embedding 与文档 embedding 使用同一模型，而不是在 query path 上隐式换一套本地近似逻辑。
2. OpenSearch《Neural query》要求 neural query 显式携带 `query_text` / `query_tokens` 与 `model_id`（或 semantic field 绑定的默认 model），说明 query-time embedding 生成是 request-local、model-aware 的能力，不应由底层 storage adapter 偷渡一个不可见的默认 encoder。

## 3. 设计结论

### 3.1 边界与不变式

1. runtime/installed composition 负责决定 live path 是否有 query encoder；Knowledge 只消费 `IQueryEncoder` / `IVectorRecallStore` 窄接口，不新增 `contracts/` supporting object。
2. live detached dense path 一旦宣称 `EmbeddingRequired`，就必须接受显式 query embedding，而不是继续把 text query 回落到 `SimpleLocalEmbeddingAdapter` 的隐式 hash 结果。
3. `SimpleLocalEmbeddingAdapter` 仅保留为 local/test fallback，并且只能在显式环境变量允许时启用；production default 不再自动带上这条路径。
4. encoder/provider 缺失、encoder 返回空 embedding、vector backend 不可用时，retrieve 继续 lexical-only fail-safe，并通过既有 reason code / marker 解释 degrade，不做进程级失败。
5. runtime `knowledge-hybrid-canary-ready` marker 必须代表“allowlisted hybrid canary probe 真实可走通”，不能继续依赖某个间接 health 字段去猜 encoder-ready 状态。

### 3.2 seam 设计

| 设计点 | Owner | 方案 | 理由 |
|---|---|---|---|
| query encoder injection | runtime -> knowledge factory option | 在 `RuntimeLiveDependencyCompositionOptions` / `InstalledAssetKnowledgeServiceOptions` 增加 `create_query_encoder` seam，由 runtime 组合根显式注入 live encoder 或留空 | 保持 owner boundary：runtime 决定 live path 是否具备 query-time embedding 能力 |
| detached dense search by embedding | memory | 在 `DetachedVectorIndexFactory` 暴露 detached backend availability 与 `search_detached_vector_index_by_embedding(...)` helper，并让 sqlite-vss backend 增加 `search_embedding(...)` | 让 runtime 直接对 detached `dense.sqlite` 走 embedding-required 查询，不再借道 text-search |
| local fallback gate | memory | 仅在 `DASALL_DETACHED_VECTOR_LOCAL_FALLBACK` 开启时，才让 detached adapter 自动创建 `SimpleLocalEmbeddingAdapter` | 把 local/test fallback 从 implicit live default 降为 explicit opt-in |
| stable local fallback | memory | `SimpleLocalEmbeddingAdapter` 改为稳定 FNV-1a token hash | 保留 local/test fallback 时也应避免 `std::hash` 跨进程/实现不稳定 |
| runtime ready marker | runtime | `knowledge-hybrid-canary-ready` 改为依赖一条真实 allowlisted hybrid canary probe，而不是 `health_snapshot.vector_backend_available` 的间接猜测 | encoder-ready 与 canary-ready 需要 functional truth，而不是间接布尔值 |

### 3.3 最小实现面

1. `memory/include/vector/DetachedVectorIndexFactory.h`、`memory/src/vector/DetachedVectorIndexFactory.cpp`
   - 新增 detached backend availability / embedding search helper。
   - local embedding fallback 改为 env-gated。
2. `memory/src/vector/SqliteVssVectorBackend.h`、`memory/src/vector/SqliteVssVectorBackend.cpp`
   - 新增 `search_embedding(...)`，并在 fresh backend handle 上补 indexed count refresh。
3. `memory/src/vector/SimpleLocalEmbeddingAdapter.cpp`
   - 将 local fallback hash 改为稳定 FNV-1a token hash。
4. `knowledge/include/KnowledgeServiceFactory.h`、`knowledge/src/KnowledgeServiceFactory.cpp`
   - installed/live factory 接入 `create_query_encoder` seam，并把 encoder 传入 `VectorRetrieverBridge`。
5. `apps/runtime_support/include/RuntimeLiveDependencyComposition.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`
   - `RuntimeKnowledgeVectorRecallStore` 改为 `EmbeddingRequired`；live detached dense path 直接对 `dense.sqlite` 做 embedding search；runtime ready marker 改为真实 canary probe。
6. 测试面
   - `tests/unit/knowledge/VectorRetrieverBridgeTest.cpp`
   - `tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp`
   - `tests/integration/access/RuntimeKnowledgeHybridCanaryIntegrationTest.cpp`
   - 新增 `tests/integration/access/RuntimeKnowledgeQueryEncoderIntegrationTest.cpp`

## 4. Design 原子清单

| D 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 live dense path 必须由显式 query encoder / embedding seam 驱动 | 本地证据 1/2/3/4；Azure / OpenSearch 实践 | 本文 §3.1 / §3.2 / §3.3 | 明确 `EmbeddingRequired`、detached embedding search 与 env-gated local fallback | 若必须扩 `contracts/` 才能表达 query encoder，则停止 036，升级为边界 blocker |
| D2 | 锁定 lexical-only fail-safe 与 ready marker 不回退的回退语义 | 本地证据 3/5 | 本文 §3.1 / §6 / §8 / §9 | encoder 缺失、空 embedding、backend 不可用都能解释性回退 lexical-only，且 canary marker 仍可 functional 验证 | 若 ready marker 只能靠 health 布尔值猜测，则回退到显式 probe，不复用旧判断 |
| D3 | 锁定 Build 三件套与回退验证出口 | 本地证据 4/5 | 本文 §5 / §7 / §10 | build target、focused test binary 与 direct fallback 命令可直接复制 | 若仍需在 Build 阶段现场寻找正负例或 acceptance 命令，则 D Gate 失败 |

## 5. Design -> Build 映射

| Design 项 | Build 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| D1 detached embedding seam | B1 收口 memory detached embedding search 与 local fallback gate | `memory/include/vector/DetachedVectorIndexFactory.h`；`memory/src/vector/DetachedVectorIndexFactory.cpp`；`memory/src/vector/SqliteVssVectorBackend.h`；`memory/src/vector/SqliteVssVectorBackend.cpp`；`memory/src/vector/SimpleLocalEmbeddingAdapter.cpp` | `dasall_memory_simple_local_embedding_adapter_unit_test` | `Build_CMakeTools(buildTargets=["dasall_memory"])`；`./build/vscode-linux-ninja/tests/unit/memory/dasall_memory_simple_local_embedding_adapter_unit_test` |
| D1 + D2 query encoder wiring | B2 接入 runtime/knowledge query encoder seam | `knowledge/include/KnowledgeServiceFactory.h`；`knowledge/src/KnowledgeServiceFactory.cpp`；`apps/runtime_support/include/RuntimeLiveDependencyComposition.h`；`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`；`knowledge/src/retrieve/VectorRetrieverBridge.cpp` | `VectorRetrieverBridgeTest`；`RuntimeKnowledgeQueryEncoderIntegrationTest` | `Build_CMakeTools(buildTargets=["dasall_apps_runtime_support","dasall_vector_retriever_bridge_unit_test","dasall_runtime_knowledge_query_encoder_integration_test"])`；`./build/vscode-linux-ninja/tests/unit/knowledge/dasall_vector_retriever_bridge_unit_test`；`./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_query_encoder_integration_test` |
| D2 + D3 marker / fallback regression | B3 收口 runtime matrix 与 hybrid canary regression | `tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp`；`tests/integration/access/RuntimeKnowledgeHybridCanaryIntegrationTest.cpp`；`tests/integration/access/CMakeLists.txt` | `RuntimeLiveCompositionFailureMatrixTest`；`RuntimeKnowledgeHybridCanaryIntegrationTest` | `Build_CMakeTools(buildTargets=["dasall_access_runtime_live_composition_failure_matrix_integration_test","dasall_runtime_knowledge_hybrid_canary_integration_test"])`；`./build/vscode-linux-ninja/tests/integration/access/dasall_access_runtime_live_composition_failure_matrix_integration_test`；`./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_hybrid_canary_integration_test` |

## 6. D Gate 结果

结论：PASS。

通过依据：

1. 036 已收敛到三块局部实现面：memory detached embedding search、knowledge/runtime query encoder wiring、runtime marker/regression closure。
2. 不需要新增 `contracts/`、不需要改变 production default `LexicalOnly`，也不需要回退 `KNO-TODO-035` 的 runtime-owned canary 主控权。
3. 正例、负例和 acceptance 出口都已锁定：encoder-ready positive path、encoder-missing lexical fallback、existing hybrid canary positive path、local fallback env gate。
4. Build 阶段的最窄风险已经明确：若 CMake Tools `RunCtest_CMakeTools` 继续命中仓库已知泛化 `生成失败`，则按 direct-binary fallback 验证，不把 validation blocker 误报成功能缺陷。

进入 `KNO-TODO-036-B` 的前提：

1. live dense path 不得继续把 `SimpleLocalEmbeddingAdapter` 作为 production 默认 encoder。
2. `EmbeddingRequired` path 缺失 encoder时必须 lexical-only fail-safe，而不是 silent empty vector hit 或 process fail。
3. 至少覆盖 1 条 encoder-ready 正例、1 条 encoder-missing 负例、1 条 existing canary regression、1 条 local fallback focused unit。

## 7. Build 原子清单

1. B1：补 detached embedding search 与 local fallback gate
   - 代码目标：`memory/include/vector/DetachedVectorIndexFactory.h`；`memory/src/vector/DetachedVectorIndexFactory.cpp`；`memory/src/vector/SqliteVssVectorBackend.h`；`memory/src/vector/SqliteVssVectorBackend.cpp`；`memory/src/vector/SimpleLocalEmbeddingAdapter.cpp`
   - 测试目标：`dasall_memory_simple_local_embedding_adapter_unit_test`
   - 验收命令：`Build_CMakeTools(buildTargets=["dasall_memory"])`；`./build/vscode-linux-ninja/tests/unit/memory/dasall_memory_simple_local_embedding_adapter_unit_test`
   - 风险与回退：若 detached backend 不可用，则只允许 lexical-only live fallback，不得重新启用 implicit local hash default
2. B2：接入 runtime/knowledge query encoder seam
   - 代码目标：`knowledge/include/KnowledgeServiceFactory.h`；`knowledge/src/KnowledgeServiceFactory.cpp`；`apps/runtime_support/include/RuntimeLiveDependencyComposition.h`；`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`；`knowledge/src/retrieve/VectorRetrieverBridge.cpp`
   - 测试目标：`VectorRetrieverBridgeTest`；`RuntimeKnowledgeQueryEncoderIntegrationTest`
   - 验收命令：`Build_CMakeTools(buildTargets=["dasall_apps_runtime_support","dasall_vector_retriever_bridge_unit_test","dasall_runtime_knowledge_query_encoder_integration_test"])`；`./build/vscode-linux-ninja/tests/unit/knowledge/dasall_vector_retriever_bridge_unit_test`；`./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_query_encoder_integration_test`
   - 风险与回退：若 positive path 仍隐式走 text search，则必须继续收口到 embedding-required detached search，而不是修改测试去适配旧语义
3. B3：补 runtime marker / canary regression closure
   - 代码目标：`tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp`；`tests/integration/access/RuntimeKnowledgeHybridCanaryIntegrationTest.cpp`；`tests/integration/access/CMakeLists.txt`
   - 测试目标：`RuntimeLiveCompositionFailureMatrixTest`；`RuntimeKnowledgeHybridCanaryIntegrationTest`
   - 验收命令：`Build_CMakeTools(buildTargets=["dasall_access_runtime_live_composition_failure_matrix_integration_test","dasall_runtime_knowledge_hybrid_canary_integration_test"])`；`./build/vscode-linux-ninja/tests/integration/access/dasall_access_runtime_live_composition_failure_matrix_integration_test`；`./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_hybrid_canary_integration_test`
   - 风险与回退：若 ready marker 误判，则只能收敛 marker probe，不得把 owner 状态重新下沉到 Knowledge public API 或放松 035 allowlist 约束

## 8. 回退与后继

1. 回退基线：runtime 未提供 query encoder 或 encoder 返回空 embedding 时，显式 hybrid canary 查询继续 lexical-only。
2. 兼容基线：production default `LexicalOnly` 与 035 的 allowlist/canary 主控保持不变。
3. local/test 基线：若需要 detached local fallback，只能显式设置 `DASALL_DETACHED_VECTOR_LOCAL_FALLBACK`，不能把它恢复成默认 live 路径。
4. 后继顺序：`KNO-TODO-036-B -> KNO-TODO-037 -> KNO-TODO-038`。
5. 禁区：036 不解决 hybrid/dense quality gate 阈值，也不解决 mixed-corpus route narrowing；这些留给 037 / 038。

## 9. 完成判定

`KNO-TODO-036-B` 仅当以下条件同时满足时完成：

1. live dense path 不再默认依赖 `SimpleLocalEmbeddingAdapter` 的 hash 结果，而是通过 `IQueryEncoder` + detached embedding search 驱动。
2. encoder/provider 缺失或返回空 embedding 时，explicit hybrid canary query 仍稳定 lexical-only fail-safe。
3. `RuntimeLiveCompositionFailureMatrixTest` 与 `RuntimeKnowledgeHybridCanaryIntegrationTest` 继续保持绿色，`knowledge-hybrid-canary-ready` marker 不回退。
4. 不新增 `contracts/`，不改变 production default `LexicalOnly`，不回退 runtime owner 对 rollout/canary admission 的主控权。

## 10. Build 完成证据（2026-05-26）

1. `memory/include/vector/DetachedVectorIndexFactory.h`、`memory/src/vector/DetachedVectorIndexFactory.cpp`、`memory/src/vector/SqliteVssVectorBackend.h`、`memory/src/vector/SqliteVssVectorBackend.cpp` 与 `memory/src/vector/SimpleLocalEmbeddingAdapter.cpp` 已完成 detached embedding seam 收口：detached backend availability / embedding search helper 已暴露，sqlite-vss backend 可直接按 embedding 检索，`SimpleLocalEmbeddingAdapter` 已降为 `DASALL_DETACHED_VECTOR_LOCAL_FALLBACK` 显式 opt-in，且 local hash 改为稳定 FNV-1a token hash。
2. `knowledge/include/KnowledgeServiceFactory.h`、`knowledge/src/KnowledgeServiceFactory.cpp`、`knowledge/src/retrieve/VectorRetrieverBridge.cpp`、`apps/runtime_support/include/RuntimeLiveDependencyComposition.h` 与 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已接通 query encoder seam：installed/live factory 现可创建 `IQueryEncoder`，runtime detached dense path 改为 `EmbeddingRequired` 并对 per-snapshot `dense.sqlite` 做 embedding search；当 encoder 缺失或返回空 embedding 时，retrieve 继续 `LexicalOnly` fail-safe，并附带既有 reason code。
3. `tests/unit/knowledge/VectorRetrieverBridgeTest.cpp`、`tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp`、`tests/integration/access/RuntimeKnowledgeHybridCanaryIntegrationTest.cpp` 与新增 `tests/integration/access/RuntimeKnowledgeQueryEncoderIntegrationTest.cpp` 已锁定三类关键回归：empty embedding negative path、runtime matrix marker stratification、existing hybrid canary positive path、encoder-ready / encoder-missing integration path。`RuntimeKnowledgeQueryEncoderIntegrationTest` 也已显式扣除 composition 阶段 canary probe 的预热调用，并把 encoder 输入断言收口到当前规范化 query text。
4. 验证结果：
   - `Build_CMakeTools(buildTargets=["dasall_apps_runtime_support","dasall_vector_retriever_bridge_unit_test","dasall_access_runtime_live_composition_failure_matrix_integration_test","dasall_runtime_knowledge_query_encoder_integration_test","dasall_runtime_knowledge_hybrid_canary_integration_test"])`：通过。
   - `RunCtest_CMakeTools(...)` 对本切片仍命中仓库已知泛化 `生成失败`，因此按既定回退口径直接执行 build-tree binaries。
   - `./build/vscode-linux-ninja/tests/unit/memory/dasall_memory_simple_local_embedding_adapter_unit_test && ./build/vscode-linux-ninja/tests/unit/knowledge/dasall_vector_retriever_bridge_unit_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_access_runtime_live_composition_failure_matrix_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_hybrid_canary_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_query_encoder_integration_test`：通过；退出码均为 `0`。
5. Build 结论：`KNO-TODO-036-B` 已完成。live dense path 现已从 implicit local hash fallback 收口为 explicit query encoder seam，035 的 runtime-owned canary marker / allowlist 语义未回退；后继可进入 037 的 retrieval quality gate 扩面。