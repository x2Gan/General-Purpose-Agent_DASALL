# KNO-TODO-035 runtime-owned hybrid canary seam 双轨任务包

## 1. 输入与目标

1. 来源任务：`KNO-TODO-035 | 收口 runtime-owned hybrid canary seam`。
2. 上游前置：`KNO-TODO-034-B` 已完成，request-scoped `preferred_mode` / corpus / tag / language surface 已打通，但 runtime 仍未决定哪些请求可以真正把 route mode 提升到 `Hybrid` / `DenseOnly`。
3. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.5、6.6、6.10、6.13.1、6.13.2；`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 2026-05-20 第一阶段第二项任务定义；ADR-006 / ADR-007 / ADR-008 owner boundary。
4. 本轮目标：把 `KNO-TODO-035` 收敛为可直接执行的 `-D + -B` 双轨任务包，明确 runtime owner 如何在不新增 profile schema v1 键的前提下，仅对 allowlisted corpus 的显式 canary query 放行 `Hybrid` / `DenseOnly`，其余路径继续保持 `LexicalOnly` fail-safe。

## 2. 研究证据

### 2.1 本地证据

1. `knowledge/src/query/CorpusRouter.cpp` 的 `select_mode()` 目前只看 `KnowledgeConfigSnapshot.retrieval_mode_default`、`query_kind` 和 corpus capability，尚未消费 `query.preferred_mode`；因此 034 暴露出的 `preferred_mode` 还不会改变真实 route mode。
2. `knowledge/src/KnowledgeServiceFactory.cpp` 当前把 installed asset service 的 `retrieval_mode_default` 固定为 `LexicalOnly`，并且 factory 组合根拥有 `build_plan` seam，说明 035 应该在 factory/runtime 组合根收口 runtime-owned gate，而不是把 rollout 判定散落到 CLI/access 或 contracts。
3. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已能识别 `knowledge_vector_runtime_available`、构建 dense snapshot、实例化 `RuntimeKnowledgeVectorRecallStore`，并在 ready path 写入 `runtime:<owner>:knowledge-installed-assets-ready`；这说明 runtime 已经掌握“vector backend ready”事实，只差一条显式 canary admission seam。
4. `tests/integration/knowledge/KnowledgeProfileCompatibilityTest.cpp` 已锁定 `desktop_full`、`cloud_full`、`edge_balanced` 为 vector-capable 且 production default 仍 `LexicalOnly`；`edge_minimal` / `factory_test` 不应被 035 偷渡放开。
5. `tests/integration/knowledge/KnowledgeInstalledNormativeCorpusTest.cpp` 已证明 `architecture_reference`、`adr_normative`、`ssot_normative` 在 installed asset baseline 中可稳定命中，而 `profile_policy_normative` 仍是 lexical-only corpus，可作为 035 的 allowlist positive / fallback 对照样本。

### 2.2 外部实践

1. Azure AI Search Hybrid Search：hybrid query 是显式 request-local control，text/vector lane 并行执行并融合结果；filter 与 corpus narrowing 先行，不应因为底层向量能力已就绪就默认把全部请求切到 hybrid。
2. Google Cloud Deploy Canary：canary rollout 的核心不是“新能力存在即默认放量”，而是分阶段、受控、可验证地只向一部分目标放行；失败时必须可以停在当前阶段并维持旧路径稳定。

## 3. 设计结论

### 3.1 边界与不变式

1. runtime 拥有 hybrid canary admission 主控权；Knowledge 只消费 runtime 提供的 allowlist seam，不自行决定哪些 profile / corpus 可以开启 canary。
2. 不新增 profile schema v1 键。profile allow 仅由 `RuntimeLiveDependencyComposition` 基于既有 `effective_profile_id()` 和 vector-ready 事实派生。
3. `preferred_mode=LexicalOnly` 永远允许；`preferred_mode=Hybrid` / `DenseOnly` 只有在 vector backend ready、runtime 已为当前 profile 打开 canary、且 query 显式命中 corpus allowlist 时才允许生效。
4. 未显式提供 `allowed_corpora`，或 `allowed_corpora` 中存在任何非 allowlisted corpus 时，显式 `Hybrid` / `DenseOnly` 一律 fail-safe 回落到 `LexicalOnly`。
5. 035 不改变 production default retrieval mode；`KnowledgeConfigSnapshot.retrieval_mode_default` 的全局默认值仍保持 `LexicalOnly`。

### 3.2 runtime-owned canary seam

| 设计点 | Owner | 方案 | 理由 |
|---|---|---|---|
| profile allow | runtime | 在 `RuntimeLiveDependencyComposition` 内用既有 profile id 派生 allow，首批仅 `desktop_full`、`cloud_full`、`edge_balanced` | 这些 profile 已被现有 compatibility integration 证明为 vector-capable 且 knowledge-enabled |
| corpus allowlist | runtime -> knowledge factory option | 首批 allowlist 固定为 `architecture_reference`、`adr_normative`、`ssot_normative` | 三者在 installed asset baseline 中既可稳定命中，又已声明支持 `Hybrid`；`profile_policy_normative` 保持 lexical-only 作为 fallback 对照 |
| query admission seam | knowledge factory | factory 包装 `build_plan` seam：仅当显式 mode 被 runtime allowlist 接受时，才对该请求临时覆盖 `retrieval_mode_default` | 改动局限于 035 指定组合根；不需要把 rollout 判定写进 router 公共逻辑 |
| fallback explain | knowledge factory | 对未放行的显式 `Hybrid` / `DenseOnly` 请求追加 `runtime_canary_not_admitted` / `runtime_canary_allowlist_miss` 等 route reason code | 调用方无需读内部日志即可区分“request 想要 hybrid”和“runtime 没有放行” |
| runtime ready marker | runtime | 在 ready path 额外写入 `runtime:<owner>:knowledge-hybrid-canary-ready` | 让组合根测试可以把“vector ready 且 canary seam active”与“仅 installed assets ready”区分开 |

### 3.3 最小实现面

1. `knowledge/include/KnowledgeServiceFactory.h`
   - 为 `InstalledAssetKnowledgeServiceOptions` 增加 module-local canary allowlist 字段，默认空集合表示不放行任何显式 hybrid canary。
2. `knowledge/src/KnowledgeServiceFactory.cpp`
   - 在 installed service factory 中派生 `canary_active`，并包装 `deps.build_plan`：
   - `preferred_mode` 缺省或 `LexicalOnly` 时，不改现有行为。
   - `preferred_mode=Hybrid` / `DenseOnly` 且 `allowed_corpora` 非空、并且全部属于 runtime 提供的 allowlist 时，对当前请求临时提升 `effective_config.retrieval_mode_default`。
   - 其余情况保持 `LexicalOnly` 并追加 fallback reason code。
3. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`
   - 用既有 `policy_snapshot->effective_profile_id()` 与 `knowledge_vector_runtime_available` 派生 canary allow；允许 profile 时把 corpus allowlist 传入 factory option，并在 ready path 追加 `knowledge-hybrid-canary-ready` marker。
4. 测试面
   - `RuntimeLiveCompositionFailureMatrixTest`：验证 ready baseline 除 installed ready marker 外，还会在允许 profile + vector-ready path 上追加 hybrid canary ready marker。
   - `RuntimeKnowledgeHybridCanaryIntegrationTest`：验证 live runtime path 上 allowlisted explicit hybrid 请求可返回 `mode=Hybrid`，non-allowlisted corpus 或未允许 profile 时保持 `LexicalOnly`。
   - `KnowledgeInstalledAssetHybridProbeTest`：直接验证 factory seam 的 positive / fallback path，不依赖 CLI/access。

## 4. Design 原子清单

| D 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 runtime-owned canary admission seam，而不是把 gate 写进 router 公共逻辑 | 本地证据 1/2/3/4 | 本文 §3.1 / §3.2 / §3.3 | profile allow、corpus allowlist、factory seam 和 ready marker 都明确 | 若必须修改 contracts / profile schema 才能表达 allow，则停止 035，升级为 design blocker |
| D2 | 锁定 positive / fallback query 样本与 ready marker 样本 | 本地证据 3/4/5 | 本文 §3.2 / §5 / §7 | allowlisted corpus 与 lexical-only 对照 corpus 明确，测试无需现场再找夹具 | 若 allowlisted corpus 命中不稳定，则回退到 direct factory fake dense store probe 固定正例 |
| D3 | 锁定 Build 三件套与回退策略 | 本地证据 1/2/3；Azure / Canary 实践 | 本文 §5 / §6 / §7 / §8 | 035-B 的代码目标、测试目标、验收命令与 direct-binary fallback 可直接复制 | 若 Build 还需临时扩面找入口，则 D Gate 失败 |

## 5. Design -> Build 映射

| Design 项 | Build 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| D1 seam 与 owner | B1 收口 knowledge factory request admission seam | `knowledge/include/KnowledgeServiceFactory.h`；`knowledge/src/KnowledgeServiceFactory.cpp` | `KnowledgeInstalledAssetHybridProbeTest` | `cmake --build build/vscode-linux-ninja --target dasall_knowledge_installed_asset_hybrid_probe_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeInstalledAssetHybridProbeTest --output-on-failure` |
| D1 + D2 runtime allow / ready marker | B2 收口 runtime live composition allowlist 与 ready marker | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`；`tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp` | `RuntimeLiveCompositionFailureMatrixTest`；`RuntimeKnowledgeHybridCanaryIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_access_runtime_live_composition_failure_matrix_integration_test dasall_runtime_knowledge_hybrid_canary_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R RuntimeLiveCompositionFailureMatrixTest --output-on-failure`；`ctest --test-dir build/vscode-linux-ninja -R RuntimeKnowledgeHybridCanaryIntegrationTest --output-on-failure` |
| D3 discoverability 与 registration | B3 注册新 integration targets | `tests/integration/access/CMakeLists.txt`；`tests/integration/knowledge/CMakeLists.txt`；新增两条 integration test 源文件 | `ctest -N` 可发现 `RuntimeKnowledgeHybridCanaryIntegrationTest` 与 `KnowledgeInstalledAssetHybridProbeTest` | `cmake --build build/vscode-linux-ninja --target dasall_runtime_knowledge_hybrid_canary_integration_test dasall_knowledge_installed_asset_hybrid_probe_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -N` |

## 6. D Gate 结果

结论：PASS。

通过依据：

1. 缺口已经被收敛到两个 runtime-owned 组合根：`RuntimeLiveDependencyComposition` 决定 allow，`KnowledgeServiceFactory` 决定如何把 allow 翻译成 request-local route override。
2. 不需要新增 profile schema v1 键，也不需要修改 `contracts/`，即可完成 canary seam。
3. positive path、fallback path 和 ready marker 的样本都已锁定，Build 不需要继续探索“测什么、拿什么 corpus 做对照”。
4. fail-safe 边界已经明确：未放行时继续 `LexicalOnly`，而不是请求失败或静默切全局默认值。

进入 `KNO-TODO-035-B` 的前提：

1. canary allowlist 只由 runtime 组合根提供，不得在 CLI/access 暴露新的全局开关。
2. 未命中 allowlist 时必须是 lexical fallback，而不是 hard failure。
3. 至少覆盖 1 条 allowlisted positive path、1 条 corpus allowlist miss fallback、1 条 ready marker 断言。
4. `desktop_full`、`cloud_full`、`edge_balanced` 允许显式 canary；`edge_minimal` / `factory_test` 不得被偷渡打开。

## 7. Build 原子清单

1. B1：扩展 installed knowledge factory canary seam
   - 代码目标：`knowledge/include/KnowledgeServiceFactory.h`；`knowledge/src/KnowledgeServiceFactory.cpp`
   - 测试目标：新增 `KnowledgeInstalledAssetHybridProbeTest`，覆盖 allowlisted `Hybrid` 正例、non-allowlisted corpus fallback 与 `DenseOnly` fail-safe
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_knowledge_installed_asset_hybrid_probe_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeInstalledAssetHybridProbeTest --output-on-failure`
   - 风险与回退：若真实 dense backend 夹具不稳定，则 probe test 退回到 fake dense store + real lexical snapshot 组合，但 factory seam owner 不变
2. B2：扩展 runtime live composition allowlist 与 ready marker
   - 代码目标：`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`；`tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp`
   - 测试目标：ready baseline 额外断言 `knowledge-hybrid-canary-ready`；negative profile 不应出现该 marker
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_access_runtime_live_composition_failure_matrix_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R RuntimeLiveCompositionFailureMatrixTest --output-on-failure`
   - 风险与回退：若 ready marker 无法稳定表达 runtime allow，就保留 installed ready marker 不变并新增独立 canary-ready marker，禁止复用 degrade marker 语义
3. B3：新增 runtime canary integration 闭环
   - 代码目标：新增 `tests/integration/access/RuntimeKnowledgeHybridCanaryIntegrationTest.cpp` 与 `tests/integration/knowledge/KnowledgeInstalledAssetHybridProbeTest.cpp`，并更新各自 `CMakeLists.txt`
   - 测试目标：`RuntimeKnowledgeHybridCanaryIntegrationTest` 覆盖 `desktop_full` / `cloud_full` / `edge_balanced` allowlisted explicit hybrid positive path，以及 non-allowlisted corpus fallback；`KnowledgeInstalledAssetHybridProbeTest` 直接锁定 factory seam
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_runtime_knowledge_hybrid_canary_integration_test dasall_knowledge_installed_asset_hybrid_probe_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R RuntimeKnowledgeHybridCanaryIntegrationTest --output-on-failure`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeInstalledAssetHybridProbeTest --output-on-failure`
   - 风险与回退：若 full runtime path 的 dense positive path 受无关环境噪音影响，则至少保住 factory seam 正例与 runtime fallback/ready marker 断言，不能把 allowlist 主控重新塞回 Knowledge public API

## 8. 回退与后继

1. 回退基线：runtime 未提供 allowlist 时，显式 `preferred_mode=Hybrid` / `DenseOnly` 继续 lexical-only。
2. 兼容基线：`retrieval_mode_default` 仍为 production lexical default；035 只做 per-request canary admission。
3. 后继顺序：`KNO-TODO-035-B -> KNO-TODO-036/037/038`。
4. 禁区：035 不解决 mixed-corpus rank blending、生产级 explain surface 扩面、profile schema 扩张；这些继续留给 036 / 038 / 039。

## 9. 完成判定

`KNO-TODO-035-B` 仅当以下条件同时满足时完成：

1. `desktop_full`、`cloud_full`、`edge_balanced` 的 vector-ready installed runtime path 上，allowlisted explicit `preferred_mode=Hybrid` 请求可返回 `mode=Hybrid`。
2. non-allowlisted corpus、未显式 corpus scope 或未允许 profile 时，显式 `Hybrid` / `DenseOnly` 请求仍保持 `mode=LexicalOnly` fail-safe。
3. runtime ready baseline 除 `knowledge-installed-assets-ready` 外，还能明确暴露 `knowledge-hybrid-canary-ready` marker。
4. 不新增 profile schema v1 键，不修改 `contracts/`，不改变 production default `LexicalOnly`。

## 10. Build 完成证据（2026-05-26）

1. `knowledge/include/KnowledgeServiceFactory.h` 与 `knowledge/src/KnowledgeServiceFactory.cpp` 已新增 runtime canary allowlist seam，并把 explicit `preferred_mode=Hybrid` / `DenseOnly` 的 admission 收口到 factory `build_plan` wrapper：只有在 `dense_bridge->available()` 且 query `allowed_corpora` 全部命中 runtime allowlist 时，才对该请求临时提升 `retrieval_mode_default`；否则继续 `LexicalOnly`，并返回 `runtime_canary_not_admitted`、`runtime_canary_allowlist_miss`、`runtime_canary_backend_not_ready` 等 explain reason code。
2. `apps/runtime_support/include/RuntimeLiveDependencyComposition.h` 与 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已把 profile allow、corpus allowlist、ready marker 和 integration-only fake vector override seam 收口到 runtime owner：production 默认路径仍只依赖真实 runtime vector config，integration override 仅用于构造稳定的 vector-ready runtime path 来验证 owner 逻辑。
3. `tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp` 现锁定 `knowledge-installed-assets-ready` 与 `knowledge-hybrid-canary-ready` 的 stratified marker；新增 `tests/integration/access/RuntimeKnowledgeHybridCanaryIntegrationTest.cpp` 验证 `desktop_full` / `cloud_full` / `edge_balanced` allowlisted hybrid 正例与 non-allowlisted corpus fallback；新增 `tests/integration/knowledge/KnowledgeInstalledAssetHybridProbeIntegrationTest.cpp` 直接锁定 factory seam 的正负例。
4. 验证结果：
   - `cmake --build build/vscode-linux-ninja --target dasall_access_runtime_live_composition_failure_matrix_integration_test dasall_runtime_knowledge_hybrid_canary_integration_test dasall_knowledge_installed_asset_hybrid_probe_integration_test -j2`：通过。
   - `ctest --test-dir build/vscode-linux-ninja -N | rg "RuntimeKnowledgeHybridCanaryIntegrationTest|KnowledgeInstalledAssetHybridProbeTest"`：可发现新测试。
   - `ctest --test-dir build/vscode-linux-ninja -R RuntimeLiveCompositionFailureMatrixTest --output-on-failure`：通过。
   - `ctest --test-dir build/vscode-linux-ninja -R RuntimeKnowledgeHybridCanaryIntegrationTest --output-on-failure`：通过。
   - `ctest --test-dir build/vscode-linux-ninja -R KnowledgeInstalledAssetHybridProbeTest --output-on-failure`：通过。
5. Build 结论：`KNO-TODO-035-B` 已完成，Gate-J（runtime canary seam ready）可判定通过；后继可以转入 036 / 037 / 038 的向量能力扩面，但不得回退 runtime owner 对 canary admission 的主控权。