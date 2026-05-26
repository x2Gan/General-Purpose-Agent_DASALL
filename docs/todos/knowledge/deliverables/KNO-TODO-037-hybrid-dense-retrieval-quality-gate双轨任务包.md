# KNO-TODO-037 hybrid / dense retrieval quality gate 双轨任务包

## 1. 输入与目标

1. 来源任务：`KNO-TODO-037 | 扩展 hybrid / dense retrieval quality gate`。
2. 上游前置：`KNO-TODO-036-B` 已完成，live dense path 已收口到显式 `IQueryEncoder` + detached embedding search；但 `tests/integration/knowledge/RetrievalQualityRegressionTest.cpp` 仍把质量 gate 固定在 lexical-only harness，无法对 `Hybrid` / `DenseOnly` 做独立回归判定。
3. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.10、6.13.1、6.13.2、`evaluation / quality` 与 `RecallCoordinator` / `VectorRetrieverBridge` 相关条目；`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中第二阶段 `KNO-TODO-037` 定义；`docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md` 中 6.10 后续阶段编排；ADR-006 / ADR-007 / ADR-008 owner boundary。
4. 本轮目标：把 retrieval quality regression gate 从“只证明 lexical baseline”扩展为“对 architecture / ADR / SSOT 语料执行 hybrid / dense mode gate”，补齐 mode-specific threshold、hard-fail 规则和 deterministic dense fixture materialization，同时保持 production default `LexicalOnly` 与 owner boundary 不回退。

## 2. 研究证据

### 2.1 本地证据

1. `tests/integration/knowledge/RetrievalQualityRegressionTest.cpp` 在本轮前把 `KnowledgeConfigSnapshot.vector_enabled` 硬编码为 `false`，并把 `retrieval_mode_default` 与结果断言都固定为 `LexicalOnly`；即使 manifest 增加 hybrid case，route 也无法 materialize dense lane。
2. 同一 harness 在本轮前把 `CorpusDescriptor.supported_modes` 固定为 `{LexicalOnly}`，且 `RecallCoordinatorDeps.dense_lane` 直接返回 `dense_lane_disabled`；这说明问题不在算法阈值，而在测试夹具根本没有 dense-capable path。
3. `tests/integration/knowledge/golden/retrieval_quality_v1.yaml` 在本轮前全部 30 个 case 的 `allowed_modes` 都只有 `LexicalOnly`，因此顶层 `evaluate_manifest(..., LexicalOnly, ...)` 虽然可通过，但无法证明 hybrid / dense 质量门禁真实存在。
4. `tests/unit/knowledge/RecallCoordinatorDenseBridgeTest.cpp` 已锁定 `RecallCoordinator` 会优先消费 real dense bridge 而不是 fallback dense seam；`tests/unit/knowledge/VectorRetrieverBridgeTest.cpp` 已锁定 `EmbeddingRequired`、empty embedding negative path 与 stable reason code。这两条单测已能承担 lane-level 语义回归，因此 037 的质量 gate 不需要再把真实 runtime composition 或 sqlite-vss 依赖拉进集成夹具。
5. repo baseline `knowledge-quality-baseline-004-2026-04-21.md` 已冻结 lexical gate 的 aggregate threshold：`MRR@10 >= 0.70`、`NDCG@10 >= 0.82`、`Recall@5 >= 0.80`、`Recall@10 >= 0.90`，并要求 `hard_fail` 与 corpus/query-kind 覆盖。这意味着 037 应在不破坏 lexical gate 的前提下，为 hybrid / dense 再补 mode-scoped gate，而不是重写原有 baseline。

### 2.2 外部实践

1. Azure AI Search《Hybrid search using vectors and full text in Azure AI Search》明确指出 hybrid query 会并行执行 full-text search 与 vector search，并通过 RRF 融合结果；相关 relevance 需要按 hybrid path 单独检验，而不能只靠 lexical-only 结果外推。
2. Pinecone《Evaluation Measures in Information Retrieval》指出 Recall@K、MRR、NDCG@K 属于 deploy 前的核心 offline metrics，适合在 isolated regression gate 中比较不同 retrieval mode 的质量表现；单一指标不足以判断真实回归，因此需要同时保留 Recall 与 order-aware metrics。

## 3. 设计结论

### 3.1 边界与不变式

1. 037 只扩展 quality gate 与测试夹具，不改变 production runtime wiring，不新增 `contracts/`，也不把 mixed-corpus 路由问题提前吸收到本轮。
2. hybrid / dense 质量门禁必须在 `RetrievalQualityRegressionTest` 内真实执行 dense-capable path，而不是继续通过 `allowed_modes` 标注制造“名义支持”。
3. quality harness 的 dense path 允许使用 deterministic fixture materialization，不要求直接接入 production sqlite-vss backend；lane-level 语义继续由 `RecallCoordinatorDenseBridgeTest` 与 `VectorRetrieverBridgeTest` 兜底。
4. lexical aggregate baseline 必须保持不变；hybrid / dense 通过新增 mode gate 独立评估，不得冲掉 004 已冻结的 lexical threshold。
5. 038 负责 mixed-corpus hybrid route narrowing；037 只在单语料、dense-capable corpus 上建立 mode-scoped regression gate，不提前改 `CorpusRouter` 的 mixed-candidate 行为。

### 3.2 gate 设计

| 设计点 | Owner | 方案 | 理由 |
|---|---|---|---|
| mode-specific threshold | golden manifest | 在 `retrieval_quality_v1.yaml` 新增 `mode_gates.Hybrid` / `mode_gates.DenseOnly`，分别声明 aggregate threshold、baseline metrics、case / hard-fail / corpus coverage 下限 | 让 hybrid / dense gate 数据驱动，而不把阈值硬编码进测试实现 |
| dense fixture materialization | integration harness | 在 `RetrievalQualityRegressionTest.cpp` 内对每个 case 的 `query_text -> expected_chunk_ref` materialize deterministic dense hit，供 `DenseOnly` / `Hybrid` 路径消费 | 只验证质量 gate 本身，不重复承担 concrete backend owner 责任 |
| route activation | integration harness | 按 active mode 初始化 `KnowledgeConfigSnapshot`，为 architecture / ADR / SSOT descriptor 打开 dense-capable `supported_modes`，并将 query `preferred_mode` 与 active mode 对齐 | 让 hybrid / dense case 真正进入相应 route，而不再被全量 skip |
| lane-level backstop | unit tests | 保持 `RecallCoordinatorDenseBridgeTest`、`VectorRetrieverBridgeTest` 作为 focused 回归辅助门禁 | 质量 gate 覆盖 aggregate behavior，lane 细节继续由单测收口 |

### 3.3 最小实现面

1. `tests/integration/knowledge/RetrievalQualityRegressionTest.cpp`
   - 新增 `mode_gates` 解析、mode-scoped evaluation、count helper。
   - 将 harness 提升为 active-mode aware，并 materialize deterministic dense lane。
   - 新增 hybrid / dense positive gate 与 dense hard-fail negative gate。
2. `tests/integration/knowledge/golden/retrieval_quality_v1.yaml`
   - 新增 `mode_gates.Hybrid` / `mode_gates.DenseOnly`。
   - 为 architecture / ADR / SSOT 追加 `Hybrid`、`DenseOnly` case。
3. 验证面
   - `RetrievalQualityRegressionTest`
   - `RecallCoordinatorDenseBridgeTest`
   - `VectorRetrieverBridgeTest`

## 4. Design 原子清单

| D 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 冻结 hybrid / dense mode gate schema | 本地证据 1/2/3/5；Pinecone offline evaluation | 本文 §3.1 / §3.2 / §3.3 | manifest 已能声明 hybrid / dense 的 aggregate threshold、baseline metrics 与最小 coverage | 若仍需把阈值硬编码进测试实现，则 D Gate 失败 |
| D2 | 冻结 deterministic dense fixture 方案 | 本地证据 2/4；Azure hybrid search | 本文 §3.2 / §3.3 | harness 可在不接入 production backend 的前提下真实执行 `Hybrid` / `DenseOnly` | 若必须改 runtime composition 才能验证质量 gate，则升级为 scope blocker |
| D3 | 锁定 Build 三件套与回退验证出口 | 本地证据 4/5 | 本文 §5 / §7 / §10 | 代码目标、测试目标、验收命令可直接复制执行 | 若仍需在 Build 阶段临时找 acceptance 命令，则 D Gate 失败 |

## 5. Design -> Build 映射

| Design 项 | Build 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| D1 + D2 mode gate / harness | B1 扩展 active-mode quality harness 与 dense fixture | `tests/integration/knowledge/RetrievalQualityRegressionTest.cpp` | `RetrievalQualityRegressionTest` | `Build_CMakeTools(buildTargets=["dasall_knowledge_retrieval_quality_regression_integration_test"])`；若 `RunCtest_CMakeTools` 继续命中仓库已知泛化 `生成失败`，回退执行 `./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_retrieval_quality_regression_integration_test` |
| D1 manifest schema / cases | B2 扩展 hybrid / dense golden manifest | `tests/integration/knowledge/golden/retrieval_quality_v1.yaml` | `RetrievalQualityRegressionTest` | 同 B1 |
| D2 + D3 lane-level backstop | B3 保持 dense lane focused regression | 无新增代码目标；复用既有 lane tests | `RecallCoordinatorDenseBridgeTest`；`VectorRetrieverBridgeTest` | `Build_CMakeTools(buildTargets=["dasall_recall_coordinator_dense_bridge_unit_test","dasall_vector_retriever_bridge_unit_test"])`；回退执行 `./build/vscode-linux-ninja/tests/unit/knowledge/dasall_recall_coordinator_dense_bridge_unit_test && ./build/vscode-linux-ninja/tests/unit/knowledge/dasall_vector_retriever_bridge_unit_test` |

## 6. D Gate 结果

结论：PASS。

通过依据：

1. 037 的问题根因已收敛到 quality harness 与 golden manifest 的 lexical-only 硬编码，不需要扩张到 runtime / memory owner surface。
2. 通过 mode-scoped gate + deterministic dense fixture，可以在现有 integration harness 内补齐 hybrid / dense 质量门禁，而不引入新的 production dependency。
3. lexical baseline、lane-level backstop 与 mixed-corpus follow-up 已分层清楚：037 只补 mode gate，038 再处理 mixed-corpus 路由语义。
4. focused validation 出口已锁定：先构建 `dasall_knowledge_retrieval_quality_regression_integration_test`，若 `RunCtest_CMakeTools` 再次泛化失败，则直接执行 build-tree binary；再补两条 lane-level binary。

进入 `KNO-TODO-037-B` 的前提：

1. hybrid / dense gate 不得依赖 production sqlite-vss backend availability 才能执行。
2. lexical aggregate baseline 与 004 既有阈值不得回退。
3. 至少覆盖 1 条 hybrid hard-fail 正例、1 条 dense hard-fail 正例与 1 条 dense hard-fail 负例。

## 7. Build 原子清单

1. B1：扩展 active-mode quality harness 与 dense fixture
   - 代码目标：`tests/integration/knowledge/RetrievalQualityRegressionTest.cpp`
   - 测试目标：`RetrievalQualityRegressionTest`
   - 验收命令：`Build_CMakeTools(buildTargets=["dasall_knowledge_retrieval_quality_regression_integration_test"])`；`./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_retrieval_quality_regression_integration_test`
   - 风险与回退：若 CTest 仍泛化失败，只允许回退到 direct binary，不得把工具噪声误判成测试缺陷
2. B2：扩展 hybrid / dense manifest schema 与 case coverage
   - 代码目标：`tests/integration/knowledge/golden/retrieval_quality_v1.yaml`
   - 测试目标：`RetrievalQualityRegressionTest`
   - 验收命令：同 B1
   - 风险与回退：若 manifest schema 扩展导致 lexical gate 失效，必须修正 parser / gate，而不是删除 hybrid / dense case
3. B3：保持 lane-level focused backstop
   - 代码目标：无新增；复用既有单测目标
   - 测试目标：`RecallCoordinatorDenseBridgeTest`；`VectorRetrieverBridgeTest`
   - 验收命令：`Build_CMakeTools(buildTargets=["dasall_recall_coordinator_dense_bridge_unit_test","dasall_vector_retriever_bridge_unit_test"])`；`./build/vscode-linux-ninja/tests/unit/knowledge/dasall_recall_coordinator_dense_bridge_unit_test && ./build/vscode-linux-ninja/tests/unit/knowledge/dasall_vector_retriever_bridge_unit_test`
   - 风险与回退：若 lane-level test 自身回归，应先修 lane 语义，再回看 quality gate，不得在 gate 层掩盖 root cause

## 8. 回退与后继

1. 回退基线：lexical aggregate gate 仍按 004 冻结的 global threshold 工作，不因新增 hybrid / dense case 降级。
2. 兼容基线：production default `LexicalOnly` 与 036 的 query encoder seam 保持不变；037 不修改 runtime canary allowlist 语义。
3. 测试基线：若 `RunCtest_CMakeTools` 继续命中仓库已知泛化 `生成失败`，只允许回退 direct binary，不允许省略 focused validation。
4. 后继顺序：`KNO-TODO-037-B -> KNO-TODO-038`。
5. 禁区：037 不解决 mixed-corpus `all_dense_capable(candidates)` 路由压回 lexical-only 的语义，这一项留给 038。

## 9. 完成判定

`KNO-TODO-037-B` 仅当以下条件同时满足时完成：

1. `RetrievalQualityRegressionTest` 已能对 `LexicalOnly`、`Hybrid`、`DenseOnly` 分别执行 gate，而不是只在 manifest 中名义标注 mode。
2. architecture / ADR / SSOT 三类 dense-capable corpus 均已具 hybrid / dense case，且具 mode-specific threshold 与 hard-fail coverage。
3. `RecallCoordinatorDenseBridgeTest` 与 `VectorRetrieverBridgeTest` 继续保持绿色，说明 lane-level 语义未被 quality harness 改动破坏。
4. 不新增 `contracts/`、不改变 production default `LexicalOnly`、不提前吸收 038 的 mixed-corpus 路由问题。

## 10. Build 完成证据（2026-05-26）

1. `tests/integration/knowledge/RetrievalQualityRegressionTest.cpp` 已扩展为 active-mode aware harness：新增 `mode_gates` 解析、`evaluate_mode_gate(...)`、mode-scoped case counting、deterministic dense fixture materialization，并增加 hybrid / dense positive gate 与 dense hard-fail negative gate。quality harness 现会按 active mode 初始化 `KnowledgeConfigSnapshot`、开放 dense-capable corpus descriptor，并断言结果 mode 不再固定为 `LexicalOnly`。
2. `tests/integration/knowledge/golden/retrieval_quality_v1.yaml` 已新增 `mode_gates.Hybrid` / `mode_gates.DenseOnly`，并为 architecture / ADR / SSOT 语料补齐 3 条 hybrid case 与 3 条 dense-only case；每个 mode 均具 aggregate threshold、baseline metrics、最小 case 数、hard-fail 数与 corpus coverage 下限。
3. 2026-05-26 已通过 `Build_CMakeTools(buildTargets=["dasall_knowledge_retrieval_quality_regression_integration_test"])`；`RunCtest_CMakeTools(tests=["RetrievalQualityRegressionTest"])` 仍命中仓库已知泛化 `生成失败`，因此按既定回退口径直接执行 `./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_retrieval_quality_regression_integration_test && echo PASS`，结果 `PASS`。
4. 2026-05-26 已通过 `Build_CMakeTools(buildTargets=["dasall_recall_coordinator_dense_bridge_unit_test","dasall_vector_retriever_bridge_unit_test"])`，并直接执行 `./build/vscode-linux-ninja/tests/unit/knowledge/dasall_recall_coordinator_dense_bridge_unit_test && ./build/vscode-linux-ninja/tests/unit/knowledge/dasall_vector_retriever_bridge_unit_test && echo PASS`，结果 `PASS`。
5. Build 结论：`KNO-TODO-037-B` 已完成。hybrid / dense 质量门禁现已从 lexical-only 名义支持提升为可执行 regression gate，后继可进入 038 的 mixed-corpus hybrid 路由语义收口。