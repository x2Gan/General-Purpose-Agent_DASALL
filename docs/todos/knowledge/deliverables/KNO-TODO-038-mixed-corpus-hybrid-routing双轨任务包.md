# KNO-TODO-038 mixed-corpus hybrid routing 双轨任务包

## 1. 输入与目标

1. 来源任务：`KNO-TODO-038 | 优化 mixed-corpus hybrid 路由语义`。
2. 上游前置：`KNO-TODO-037-B` 已完成，hybrid / dense quality gate 已能按 mode 独立执行；但 `knowledge/src/query/CorpusRouter.cpp` 仍以 `all_dense_capable(candidates)` 作为混合路由前提，导致 mixed catalog 下只要混入 `profile_policy_normative` 或 `dasall_llm_providers` 这类 lexical-only corpus，就会把 generic hybrid / dense query 全局压回 `LexicalOnly`。
3. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.10、6.13.1、6.13.2、`CorpusRouter` / `RecallCoordinator` / `VectorRetrieverBridge` 条目；`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中第二阶段 `KNO-TODO-038` 定义；ADR-006 / ADR-007 / ADR-008 owner boundary。
4. 本轮目标：让 mixed catalog 下的 hybrid / dense 路由能够基于 dense-capable subset 或 runtime canary allowlisted subset 选路，而不是被 lexical-only corpus 全局拖回 `LexicalOnly`；同时保持 lexical-only corpus 不被误标 dense-capable，production default `LexicalOnly` 不回退。

## 2. 研究证据

### 2.1 本地证据

1. `knowledge/src/query/CorpusRouter.cpp` 在本轮前使用 `all_dense_capable(candidates)` 判定 dense / hybrid 是否可用；这意味着只要过滤后的候选集中存在一个 lexical-only corpus，`select_mode()` 就会把 mixed query 降回 `LexicalOnly`。
2. `knowledge/src/KnowledgeServiceFactory.cpp` 在本轮前对 explicit runtime canary query 采用 `all_corpora_allowlisted(query.allowed_corpora, runtime_canary_allowed_corpora)` 判定；当 `allowed_corpora` 同时包含 allowlisted dense-capable corpus 与 lexical-only non-allowlisted corpus 时，factory 会直接给出 `runtime_canary_allowlist_miss`，而不是把 query scope 收窄到 allowlisted subset。
3. installed asset 默认 descriptor 中，`architecture_reference`、`adr_normative`、`ssot_normative` 支持 `LexicalOnly + Hybrid`，而 `profile_policy_normative`、`dasall_llm_providers` 仍是 lexical-only；因此 mixed catalog 是常态，不是边缘数据形态。
4. `tests/unit/knowledge/CorpusRouterModeSelectionTest.cpp` 在本轮前把 mixed capability 语义锁成“只要任一 candidate 缺 dense support 就降 lexical-only”；仓库没有独立的 `CorpusRouterMixedCapabilityTest` 或 mixed-corpus integration probe 来证明 route narrowing。
5. 037 已证明 deterministic dense fixture 与 mode gate 可以在不接入 production backend 的前提下验证 hybrid / dense 行为，这使得 038 可以继续聚焦 route semantics，而不必扩张到 vector backend owner surface。

### 2.2 外部实践

1. Azure AI Search《Hybrid search using vectors and full text in Azure AI Search》指出 hybrid query 是单次请求内并行执行 full-text 与 vector query，并允许结合 filter / facet / semantic ranking；这意味着混合检索并不要求“索引里所有 corpus 都同时 dense-capable”，而是应基于当前 query scope 选择可参与 hybrid 的子集。
2. DASALL 详设在行业实践对照中已明确采用 pipeline-side hybrid，而不是把 sparse / dense 混进单一 retriever；因此 route narrowing 也应发生在 `CorpusRouter` / `KnowledgeServiceFactory` 的 query planning 层，而不是在下游 lane 或 backend 上隐式兜底。

## 3. 设计结论

### 3.1 边界与不变式

1. `CorpusRouter` 有权对当前 query 的候选集做 route-appropriate narrowing：`Hybrid` 只保留同时具 lexical+dense 能力的 subset，`DenseOnly` 只保留 dense-capable subset；这不等于修改 corpus descriptor，也不把 lexical-only corpus 伪装成 dense-capable。
2. `KnowledgeServiceFactory` 在 explicit runtime canary path 上可以把 mixed `allowed_corpora` 收窄到 allowlisted subset，只要该 subset 非空；若收窄后为空，仍保持 `runtime_canary_allowlist_miss` fail-safe。
3. production default `LexicalOnly`、runtime canary scope required 规则、profile schema v1 和 owner boundary 均保持不变。
4. 038 不改变 dense backend 或 query encoder seam；只收口 route planning / canary scoping 语义，并由 focused unit/integration tests 验证。

### 3.2 route 设计

| 设计点 | Owner | 方案 | 理由 |
|---|---|---|---|
| mixed-corpus route narrowing | `CorpusRouter` | 为 `Hybrid` 选择同时支持 lexical+dense 的 subset，为 `DenseOnly` 选择 dense-capable subset；当 subset 小于原候选集时追加 `dense_capable_subset_selected` reason code | 让 generic mixed query 能继续走 hybrid/dense，而不是被 lexical-only corpus 全局压回 |
| lexical fallback guard | `CorpusRouter` | 仅在“完全无 dense-capable subset”时才追加 `corpus_mode_capability_downgraded` 并退回 `LexicalOnly` | 保留 fail-safe，但不把 subset-available 场景误判为 fallback |
| explicit canary scope narrowing | `KnowledgeServiceFactory` | 对 admitted runtime canary query，把 `allowed_corpora` 收窄到 allowlisted subset，并追加 `runtime_canary_scope_narrowed` | 保持 runtime owner allowlist 不变，同时避免 mixed scope 被直接拒绝 |
| focused regression set | tests | 新增 `CorpusRouterMixedCapabilityTest`、`KnowledgeMixedCorpusHybridRoutingIntegrationTest`，并保留 `CorpusRouterModeSelectionTest`、`KnowledgeInstalledAssetHybridProbeIntegrationTest`、`RuntimeKnowledgeHybridCanaryIntegrationTest` | 新增能力与既有 owner/canary 语义都需要并行锁定 |

### 3.3 最小实现面

1. `knowledge/src/query/CorpusRouter.cpp`
   - 改为基于 dense-capable / hybrid-capable subset 选路，并把 route plan 的 `corpus_ids` 收窄到所选 subset。
2. `knowledge/src/KnowledgeServiceFactory.cpp`
   - 对 explicit runtime canary query 计算 allowlisted subset，并在 admitted path 上收窄 `effective_query.allowed_corpora`。
3. 测试面
   - 新增 `tests/unit/knowledge/CorpusRouterMixedCapabilityTest.cpp`
   - 更新 `tests/unit/knowledge/CorpusRouterModeSelectionTest.cpp`
   - 新增 `tests/integration/knowledge/KnowledgeMixedCorpusHybridRoutingIntegrationTest.cpp`
   - 更新 `tests/unit/knowledge/CMakeLists.txt` 与 `tests/integration/knowledge/CMakeLists.txt`

## 4. Design 原子清单

| D 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 冻结 mixed-corpus route narrowing 语义 | 本地证据 1/3/4；Azure hybrid search | 本文 §3.1 / §3.2 / §3.3 | `Hybrid` / `DenseOnly` 的 subset 规则与 fallback 边界明确 | 若只能通过修改 corpus descriptor 冒充 dense-capable 才能达标，则 D Gate 失败 |
| D2 | 冻结 explicit canary mixed-scope 语义 | 本地证据 2；035 runtime canary seam | 本文 §3.1 / §3.2 | allowlisted subset 非空时可 admitted，并保留 `runtime_canary_scope_required` / `allowlist_miss` 负例 | 若必须放松 runtime allowlist owner 权限，升级为 blocker |
| D3 | 锁定 Build 三件套与 focused regression set | 本地证据 4/5 | 本文 §5 / §7 / §10 | code target、test target、acceptance command 可直接复制 | 若仍需到 Build 阶段现场摸索测试入口，则 D Gate 失败 |

## 5. Design -> Build 映射

| Design 项 | Build 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| D1 subset routing | B1 收口 `CorpusRouter` mixed-corpus route narrowing | `knowledge/src/query/CorpusRouter.cpp`；`tests/unit/knowledge/CorpusRouterModeSelectionTest.cpp`；`tests/unit/knowledge/CorpusRouterMixedCapabilityTest.cpp`；`tests/unit/knowledge/CMakeLists.txt` | `CorpusRouterModeSelectionTest`；`CorpusRouterMixedCapabilityTest` | `Build_CMakeTools(buildTargets=["dasall_corpus_router_mode_selection_unit_test","dasall_corpus_router_mixed_capability_unit_test"])`；回退执行 `./build/vscode-linux-ninja/tests/unit/knowledge/dasall_corpus_router_mode_selection_unit_test && ./build/vscode-linux-ninja/tests/unit/knowledge/dasall_corpus_router_mixed_capability_unit_test` |
| D2 canary scope narrowing | B2 收口 `KnowledgeServiceFactory` mixed allowlisted scope | `knowledge/src/KnowledgeServiceFactory.cpp`；`tests/integration/knowledge/KnowledgeMixedCorpusHybridRoutingIntegrationTest.cpp`；`tests/integration/knowledge/CMakeLists.txt` | `KnowledgeMixedCorpusHybridRoutingIntegrationTest`；`KnowledgeInstalledAssetHybridProbeTest`；`RuntimeKnowledgeHybridCanaryIntegrationTest` | `Build_CMakeTools(buildTargets=["dasall_knowledge_mixed_corpus_hybrid_routing_integration_test","dasall_knowledge_installed_asset_hybrid_probe_integration_test","dasall_runtime_knowledge_hybrid_canary_integration_test"])`；回退执行 `./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_mixed_corpus_hybrid_routing_integration_test && ./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_installed_asset_hybrid_probe_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_hybrid_canary_integration_test` |

## 6. D Gate 结果

结论：PASS。

通过依据：

1. 038 的根因已收敛到 route planning / canary scoping，而不是 dense backend 或 query encoder owner surface。
2. `CorpusRouter` subset narrowing 与 `KnowledgeServiceFactory` allowlisted scope narrowing 可分别覆盖“无 allowed_corpora 的 generic mixed query”和“带 mixed allowed_corpora 的 explicit canary query”两种主路径。
3. focused regression set 已锁定为 2 条新 probe + 3 条相邻既有回归，不需要扩张到 runtime composition 或 packaging。
4. 验收出口明确：若 `RunCtest_CMakeTools` 再次命中仓库已知泛化 `生成失败`，按仓库既有口径回退到 build-tree direct binaries。

进入 `KNO-TODO-038-B` 的前提：

1. lexical-only corpus 不得被误标 dense-capable。
2. runtime canary allowlist owner 权限不得放松，只允许在 admitted path 上缩窄 query scope。
3. 至少覆盖 1 条 generic mixed-corpus positive、1 条 explicit canary mixed-scope positive、1 条无 dense-capable corpus lexical fallback 正例。

## 7. Build 原子清单

1. B1：收口 `CorpusRouter` mixed-corpus subset routing
   - 代码目标：`knowledge/src/query/CorpusRouter.cpp`；`tests/unit/knowledge/CorpusRouterModeSelectionTest.cpp`；`tests/unit/knowledge/CorpusRouterMixedCapabilityTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`
   - 测试目标：`CorpusRouterModeSelectionTest`；`CorpusRouterMixedCapabilityTest`
   - 验收命令：`Build_CMakeTools(buildTargets=["dasall_corpus_router_mode_selection_unit_test","dasall_corpus_router_mixed_capability_unit_test"])`；`./build/vscode-linux-ninja/tests/unit/knowledge/dasall_corpus_router_mode_selection_unit_test && ./build/vscode-linux-ninja/tests/unit/knowledge/dasall_corpus_router_mixed_capability_unit_test`
   - 风险与回退：若 subset 为空，必须保持 lexical-only fail-safe，不得返回半有效 `Hybrid` / `DenseOnly` 计划
2. B2：收口 explicit canary mixed-scope narrowing
   - 代码目标：`knowledge/src/KnowledgeServiceFactory.cpp`；`tests/integration/knowledge/KnowledgeMixedCorpusHybridRoutingIntegrationTest.cpp`；`tests/integration/knowledge/CMakeLists.txt`
   - 测试目标：`KnowledgeMixedCorpusHybridRoutingIntegrationTest`；`KnowledgeInstalledAssetHybridProbeTest`；`RuntimeKnowledgeHybridCanaryIntegrationTest`
   - 验收命令：`Build_CMakeTools(buildTargets=["dasall_knowledge_mixed_corpus_hybrid_routing_integration_test","dasall_knowledge_installed_asset_hybrid_probe_integration_test","dasall_runtime_knowledge_hybrid_canary_integration_test"])`；`./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_mixed_corpus_hybrid_routing_integration_test && ./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_installed_asset_hybrid_probe_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_hybrid_canary_integration_test`
   - 风险与回退：若 mixed scope 中无 allowlisted corpus，仍必须返回 `runtime_canary_allowlist_miss`，不得 silently widen 到 full catalog

## 8. 回退与后继

1. 回退基线：production default `LexicalOnly` 保持不变；只有 dense-capable subset 或 allowlisted subset 非空时才会提升到 non-lexical route。
2. 兼容基线：`KnowledgeInstalledAssetHybridProbeTest` 与 `RuntimeKnowledgeHybridCanaryIntegrationTest` 的既有 allowlist-miss 负例保持有效。
3. 测试基线：若 `RunCtest_CMakeTools` 继续命中仓库已知泛化 `生成失败`，只允许回退 direct binary，不允许省略 focused regression。
4. 后继顺序：`KNO-TODO-038-B -> KNO-TODO-039`。
5. 禁区：038 不推进 production default hybrid rollout，也不修改 vector observability / explain payload，这些留给 039。

## 9. 完成判定

`KNO-TODO-038-B` 仅当以下条件同时满足时完成：

1. mixed catalog 下的 generic hybrid / dense query 能基于 dense-capable subset 选路，而不是被 lexical-only corpus 全局压回 `LexicalOnly`。
2. explicit runtime canary mixed scope 在存在 allowlisted subset 时可被 narrowed 并 admitted；当 subset 为空时仍保持 `allowlist_miss` fail-safe。
3. `CorpusRouterModeSelectionTest`、`CorpusRouterMixedCapabilityTest`、`KnowledgeMixedCorpusHybridRoutingIntegrationTest`、`KnowledgeInstalledAssetHybridProbeTest` 与 `RuntimeKnowledgeHybridCanaryIntegrationTest` 全部保持绿色。
4. 不新增 `contracts/`、不改变 profile schema v1、不中断 035/036 已固定的 runtime owner / query encoder seam。

## 10. Build 完成证据（2026-05-26）

1. `knowledge/src/query/CorpusRouter.cpp` 已从“全体 dense-capable 才能 non-lexical”收口为“按 route-appropriate subset 选路”：`Hybrid` 现在保留同时支持 lexical+dense 的 subset，`DenseOnly` 保留 dense-capable subset；若 subset 小于原候选集，会追加 `dense_capable_subset_selected` reason code。
2. `knowledge/src/KnowledgeServiceFactory.cpp` 已在 explicit runtime canary path 上增加 allowlisted subset 计算：当 mixed `allowed_corpora` 中存在 allowlisted corpus 时，factory 会追加 `runtime_canary_scope_narrowed` 并收窄 `effective_query.allowed_corpora`；当收窄结果为空时，仍保持 `runtime_canary_allowlist_miss`。
3. 已新增 `tests/unit/knowledge/CorpusRouterMixedCapabilityTest.cpp` 与 `tests/integration/knowledge/KnowledgeMixedCorpusHybridRoutingIntegrationTest.cpp`，并分别在 `tests/unit/knowledge/CMakeLists.txt`、`tests/integration/knowledge/CMakeLists.txt` 注册；同时已更新 `tests/unit/knowledge/CorpusRouterModeSelectionTest.cpp`，把旧 mixed-capability lexical fallback 断言收口到“完全无 dense-capable corpus 时才降 lexical-only”。
4. 2026-05-26 已通过 `Build_CMakeTools(buildTargets=["dasall_corpus_router_mode_selection_unit_test","dasall_corpus_router_mixed_capability_unit_test","dasall_knowledge_mixed_corpus_hybrid_routing_integration_test","dasall_knowledge_installed_asset_hybrid_probe_integration_test","dasall_runtime_knowledge_hybrid_canary_integration_test"])`；`RunCtest_CMakeTools` 仍可能命中仓库已知泛化 `生成失败`，因此按回退口径直接执行 `./build/vscode-linux-ninja/tests/unit/knowledge/dasall_corpus_router_mode_selection_unit_test && ./build/vscode-linux-ninja/tests/unit/knowledge/dasall_corpus_router_mixed_capability_unit_test && ./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_mixed_corpus_hybrid_routing_integration_test && ./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_installed_asset_hybrid_probe_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_hybrid_canary_integration_test && echo PASS`，结果 `PASS`。
5. Build 结论：`KNO-TODO-038-B` 已完成。mixed-corpus hybrid 路由现已能在不放松 owner boundary 的前提下做 subset narrowing，039 可继续推进 vector observability / explain surface。