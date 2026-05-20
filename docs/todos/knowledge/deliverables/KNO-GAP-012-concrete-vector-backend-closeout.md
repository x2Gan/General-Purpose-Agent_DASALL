# KNO-GAP-012 concrete vector backend closeout

来源任务：KNO-GAP-012
完成日期：2026-05-20
关联修复：KNO-FIX-011

## 1. 任务边界

1. 本轮只收口 `KNO-GAP-012` 的 concrete vector backend 缺口，不把更高层 dense rollout、profile mode 切换或 qemu / soak 证据混入本轮。
2. authoritative 问题定义固定为：Knowledge 是否已经在不越过 owner boundary 的前提下 materialize 真实 dense artifact，并通过 runtime composition 暴露 concrete backend health，而不是只保留 bridge / degrade seam。
3. 本轮继续守住 ADR-006 / ADR-007 / ADR-008：Knowledge 不直接 include/link Memory private implementation，不接管 Context / Recovery / Agent orchestration owner。

## 2. 现有本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| Memory concrete backend | `memory/include/vector/DetachedVectorIndexFactory.h` 与 `memory/src/vector/DetachedVectorIndexFactory.cpp` 已把 sqlite-vss concrete backend 提升为 detached public factory，可附着任意 SQLite snapshot 文件 | concrete vector engine 不再只活在 Memory manager internal wiring 里，runtime composition 可安全复用 |
| Knowledge owner-safe contract | `knowledge/include/index/IndexWriter.h`、`knowledge/include/KnowledgeServiceFactory.h` 与对应实现现接收外部 dense snapshot builder / recall store contract | Knowledge 本体只拥有 dense artifact / recall contract，不直接依赖 Memory private surface |
| Runtime composition evidence | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 现为 active snapshot materialize `dense.sqlite`，并把 dense hit 通过 `chunk_id` 回查 `lexical.sqlite` 元数据；`RuntimeLiveCompositionFailureMatrixTest` 已断言 `vector_backend_available=true` 且 dense artifact 存在 | concrete backend 已成为 runtime-composed build-tree 真实 artifact / health evidence，而非纸面 seam |

## 3. 设计回链

1. 行业实践上，vector index 与 metadata store 通常分层治理：向量召回负责 KNN，过滤与引用元数据仍回落到权威元数据存储。本轮采用 `dense.sqlite` + `lexical.sqlite` sidecar 组合，符合 pgvector / sqlite-vss 常见的“vector index + metadata sidecar”模式。
2. SQLite loadable extension 的行业实践要求 extension asset 显式装配并在缺失时 fail-closed。本轮沿用 Memory 已有 sqlite-vss packaging / asset path contract，不让 Knowledge 自己管理 loadable extension 生命周期。
3. sqlite-vss 自身不擅长复杂 metadata filtering，因此本轮 recall store 采用 overfetch 后按 `allowed_corpus_ids` / `required_tags` / `required_language` 回查 lexical metadata 过滤，避免把 filtering owner 下沉到 vector backend。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 目标 |
|---|---|
| concrete vector backend 必须能附着真实 Knowledge snapshot，而不是只存在于 Memory 内部 | 代码目标：`DetachedVectorIndexFactory` 可对任意 SQLite 文件打开 sqlite-vss backend |
| Knowledge 必须只声明 dense artifact / recall contract，不直接依赖 Memory private implementation | 代码目标：`IndexWriterDeps.build_dense_snapshot` 与 `InstalledAssetKnowledgeServiceOptions.create_vector_recall_store` |
| runtime composition 必须真正 materialize dense artifact，并把 health 对齐到 concrete backend 可用性 | 验收命令：`ctest --test-dir build/vscode-linux-ninja -R RuntimeLiveCompositionFailureMatrixTest --output-on-failure` |
| v1 production default 仍保持 lexical-only，避免在本轮把 rollout 策略与 backend closeout 混成一个任务 | 完成判定：retrieve mode 仍为 `RetrievalMode::LexicalOnly`，但 `vector_backend_available=true` 且 `dense.sqlite` 存在 |

## 5. D Gate

1. 范围单一：只处理 concrete vector backend owner-safe 落地。
2. 不新增 Knowledge -> Memory private include/link，不修改 ADR owner 归属。
3. 不在本轮把 default retrieval mode 从 lexical-only 推到 hybrid/dense。

## 6. 验证结果

1. `cmake --build build/vscode-linux-ninja --target dasall_memory -j2`
	- 结果：通过。
2. `cmake --build build/vscode-linux-ninja --target dasall_knowledge -j2`
	- 结果：通过。
3. `cmake --build build/vscode-linux-ninja --target dasall_apps_runtime_support -j2`
	- 结果：通过。
4. `ctest --test-dir build/vscode-linux-ninja -R RuntimeLiveCompositionFailureMatrixTest --output-on-failure`
	- 结果：通过；ready baseline 下 `DeepSeek Chat` 查询仍保持 `RetrievalMode::LexicalOnly`，同时 health `vector_backend_available=true` 且 active snapshot 下存在 `dense.sqlite`。

## 7. 完成判定

1. `KNO-GAP-012` 已关闭。
2. Knowledge concrete vector backend 已从 bridge seam 提升为 owner-safe concrete artifact：runtime composition 现可 materialize per-snapshot `dense.sqlite`，并通过 health/readiness 暴露可用性。
3. 本结论不外推为 default hybrid rollout、installed-package 全量 dense acceptance、qemu / soak 通过；这些作为后续 profile / environment follow-up 单独推进。