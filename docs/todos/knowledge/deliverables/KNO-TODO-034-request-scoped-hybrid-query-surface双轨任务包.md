# KNO-TODO-034 request-scoped hybrid query surface 双轨任务包

## 1. 输入与目标

1. 来源任务：`KNO-TODO-034 | 暴露 request-scoped hybrid query surface`。
2. 上游缺口：`KNO-GAP-013`（请求面缺失）与 `KNO-GAP-018`（production explainability 不足）。
3. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.5、6.10、6.11、6.13.1、6.13.2 与 `KNO-C016` / `KNO-C017`；`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 2026-05-20 第一阶段任务定义。
4. 本轮目标：把 `KNO-TODO-034` 收敛为可直接执行的 `-D + -B` 双轨任务包，确保后续只需顺序执行 `KNO-TODO-034-B`，而不再在 Build 过程中补边界、补测试或临时找验收出口。

## 2. 研究证据

### 2.1 本地证据

1. `apps/cli/src/CliCommandParser.cpp` 当前 `knowledge retrieve` 只接受 `query_text`，无法显式表达 `preferred_mode`、corpus scope、tag filter 或 language filter。
2. `access/src/AccessGatewayFactory.cpp` 当前 `DaemonKnowledgePayload` 仅承载 `operation`、`query_text`、`changed_sources`，且 retrieve 响应只返回 `ok`、`mode`、`slice_count`、`first_citation`、`first_snippet`、`error_ref`，不足以解释 degraded 原因和命中 corpus。
3. `knowledge/include/KnowledgeTypes.h`、`knowledge/src/query/QueryNormalizer.cpp`、`knowledge/src/query/CorpusRouter.cpp` 已存在 module-local `KnowledgeQuery`、route reason code 与 mode 选择逻辑，说明缺口在“控制面没有暴露出来”，而不是检索内核完全缺失。
4. `knowledge/src/KnowledgeServiceFactory.cpp` 仍把 production baseline 固定为 `retrieval_mode_default = LexicalOnly`；因此 034 只能做 request-scoped canary surface，不能偷渡成 global default 切换。
5. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已能暴露 `vector_backend_available` 与 dense artifact ready 事实，说明 034 只需打通请求面和 explain surface，即可为 035 的 runtime-owned canary seam 提供输入。

### 2.2 外部实践

1. Azure AI Search Hybrid Search：hybrid/vector 参数按请求显式提供，filter/corpus narrowing 先行，默认不会因为底层 capability 已就绪就自动把所有流量切到 hybrid。
2. OpenSearch Hybrid Search：dense/sparse normalization 与 weighting 需要 request-local control 和可观测输出，否则 degraded / zero-hit 原因难以解释。
3. Pinecone hybrid search：dense/sparse blending 通常先从显式 canary query 与有限索引范围开始，再逐步扩大默认路由；rollout 之前必须先锁定可解释返回面和失败回退。

## 3. 设计结论

### 3.1 边界与不变式

1. `KNO-TODO-034` 只暴露 request-scoped retrieval control surface，不改变 runtime-owned rollout 主控权；是否允许 `Hybrid` / `DenseOnly` 仍由后续 `KNO-TODO-035` 决定。
2. 新增 retrieval control 字段保持 module-local，落点仅限 `knowledge/include/KnowledgeTypes.h` 与 control-plane adapter，不扩 `contracts/`。
3. 所有新字段必须是 additive optional surface：未显式提供时继续沿用现有 lexical-only baseline 和固定 query defaults。
4. retrieve 响应只做 additive explain surface 扩展，不删除现有 `mode` / `slice_count` / `first_citation` / `first_snippet` 兼容字段。
5. `KNO-TODO-034-B` 的完成不等于 hybrid production ready；它只证明“可以显式发起 canary query，并能看懂结果”。

### 3.2 请求面字段矩阵

| 字段 | 进入层级 | 语义 | 默认值 | 负向规则 |
|---|---|---|---|---|
| `preferred_mode` | CLI -> daemon/access -> `KnowledgeQuery` | 显式请求 `LexicalOnly` / `Hybrid` / `DenseOnly` | 缺省时沿用 projector/factory 默认值 | 非法枚举立即拒绝；不得在 034 内绕过 runtime canary gate 强制启用 dense |
| `query_kind` | CLI -> daemon/access -> `KnowledgeQuery` | 暴露现有 query intent，而不是固定 `FactLookup` | 缺省 `FactLookup` | 未知 kind 拒绝；`MultiHop` 仍沿用 v1 `NotSupported` |
| `allowed_corpora` | CLI -> daemon/access -> `KnowledgeQuery` | 显式缩小 corpus 候选集，支撑 canary | 缺省为空，表示不额外缩小 | 空字符串项、重复项、未注册 corpus id 需要失败或 warning 收敛，不能静默污染路由 |
| `domain_tags` | CLI -> daemon/access -> `KnowledgeQuery` | 驱动路由缩窄与 corpus prefilter | 缺省为空 | 需要 canonicalize / 去重；空白项拒绝 |
| `required_tags` | CLI -> daemon/access -> `KnowledgeQuery` | 强约束命中 tag | 缺省为空 | 空白项拒绝；不得与 `domain_tags` 混淆 |
| `required_language` | CLI -> daemon/access -> `KnowledgeQuery` | 强约束命中语言 | 缺省为空 | 非法或空白语言码拒绝 |

### 3.3 响应解释面字段矩阵

| 字段 | 返回层级 | 语义 | 兼容要求 |
|---|---|---|---|
| `mode` | daemon/access JSON | 实际执行模式 | 保留现有字段 |
| `degraded` | daemon/access JSON | 本次请求是否从偏好模式退化 | 新增可选字段；旧 consumer 不读取时不破坏 |
| `reason_codes` | daemon/access JSON | route / degrade / validation 关键原因 | 允许为空数组；不得只留内部日志 |
| `warning_count` | daemon/access JSON | warning 总数摘要 | 与现有 warning vector 保持松耦合 |
| `corpus_summary` | daemon/access JSON | 命中或参与评估的 corpus 摘要 | 仅返回摘要，不泄露内部 descriptor 全量结构 |

## 4. Design 原子清单

| D 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 request-scoped control surface 的字段矩阵、owner 与默认值 | 本地证据 1/2/3/4；`KNO-C016` / `KNO-C017` | 本文 §3.1 / §3.2 | 字段、默认值、拒绝规则与 owner 明确 | 若字段仍需跨模块共享，则停止 034，升级为 boundary blocker |
| D2 | 锁定 daemon/access explain payload 的最小增量面 | 本地证据 2/4/5；Azure / OpenSearch explainability 实践 | 本文 §3.3 | additive payload 明确，旧字段兼容不受破坏 | 若必须删除旧字段才可落地，则回退到保留旧字段并附加新摘要 |
| D3 | 锁定 Build 三件套与执行边界 | 本地证据 1/2/3/5；Pinecone canary rollout 实践 | 本文 §5 / §6 / §7 | `034-B` 的代码目标、测试目标、验收命令与回退策略完整可复制 | 若 Build 仍需在执行中临时找目标/测试/命令，则 D Gate 失败 |

## 5. Design -> Build 映射

| Design 项 | Build 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| D1 字段矩阵与 owner | B1 收口 module-local query control fields | `knowledge/include/KnowledgeTypes.h`；`knowledge/src/query/QueryNormalizer.cpp` | 扩展 `dasall_knowledge_interface_surface_unit_test` 或等价 surface coverage；必要时补 `QueryNormalizerBoundaryTest` | `cmake --build build/vscode-linux-ninja --target dasall_knowledge_interface_surface_unit_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R dasall_knowledge_interface_surface_unit_test --output-on-failure` |
| D2 request / response payload 解释面 | B2 扩展 CLI / access request builder 与 response projection | `apps/cli/src/CliCommandParser.cpp`；`apps/cli/src/CliRequestBuilder.h`；`apps/cli/src/CliRequestBuilder.cpp`；`access/src/AccessGatewayFactory.cpp` | `CliDaemonCommandParserTest`；`DaemonAccessPipelineFactoryTest` | `cmake --build build/vscode-linux-ninja --target dasall-cli_command_parser_unit_test dasall-daemon_access_pipeline_factory_unit_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R CliDaemonCommandParserTest --output-on-failure`；`ctest --test-dir build/vscode-linux-ninja -R DaemonAccessPipelineFactoryTest --output-on-failure` |
| D3 主链可执行性与 canary 入口 | B3 新增 control-plane 到 knowledge 的端到端 query surface integration | `tests/integration/access/KnowledgeRuntimeQuerySurfaceIntegrationTest.cpp`；`tests/integration/access/CMakeLists.txt` | `KnowledgeRuntimeQuerySurfaceIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_knowledge_runtime_query_surface_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeRuntimeQuerySurfaceIntegrationTest --output-on-failure` |

## 6. D Gate 结果

结论：PASS。

通过依据：

1. 本地证据已经把缺口收敛到 control-plane query surface 与 explain payload，而不是重新发明 retrieval core。
2. 外部实践已经证明 034 应先做 request-scoped canary surface，而不是直接切 production default。
3. `034-B` 的代码目标、测试目标、验收命令与回退策略已经锁定，可直接进入 Build。
4. 设计边界已经明确：不扩 `contracts/`、不改 default `LexicalOnly`、不把 rollout 主控下沉到 Knowledge。

进入 `KNO-TODO-034-B` 的前提：

1. 保持 additive request / response surface。
2. 保持 runtime-owned canary admission 仍在 035。
3. Build 至少覆盖 1 条正例和 1 条负例。
4. JSON payload 的新增字段必须在旧 consumer 缺失读取时仍可兼容。

## 7. Build 原子清单

1. B1：扩展 module-local query control fields
   - 代码目标：`knowledge/include/KnowledgeTypes.h`；`knowledge/src/query/QueryNormalizer.cpp`
   - 测试目标：扩展 `dasall_knowledge_interface_surface_unit_test`；如 normalization 规则发生变化，补 `QueryNormalizerBoundaryTest`
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_knowledge_interface_surface_unit_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R dasall_knowledge_interface_surface_unit_test --output-on-failure`
   - 风险与回退：若字段扩张逼近 shared contracts，则立即回退为 module-local only，禁止复用到 shared DTO
2. B2：扩展 CLI / access request builder 与 response projection
   - 代码目标：`apps/cli/src/CliCommandParser.cpp`；`apps/cli/src/CliRequestBuilder.h`；`apps/cli/src/CliRequestBuilder.cpp`；`access/src/AccessGatewayFactory.cpp`
   - 测试目标：`CliDaemonCommandParserTest` 正向覆盖 explicit `preferred_mode + allowed_corpora`；负向覆盖非法 mode / 空白 corpus item；`DaemonAccessPipelineFactoryTest` 覆盖 request mapping 与 response payload 增量字段
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall-cli_command_parser_unit_test dasall-daemon_access_pipeline_factory_unit_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R CliDaemonCommandParserTest --output-on-failure`；`ctest --test-dir build/vscode-linux-ninja -R DaemonAccessPipelineFactoryTest --output-on-failure`
   - 风险与回退：若 response payload 兼容性不稳，只追加新字段，不移除 `first_citation` / `first_snippet`
3. B3：新增 request surface integration 闭环
   - 代码目标：`tests/integration/access/KnowledgeRuntimeQuerySurfaceIntegrationTest.cpp`；`tests/integration/access/CMakeLists.txt`
   - 测试目标：正向覆盖 `CLI -> daemon/access -> KnowledgeQuery` 映射与 `degraded` / `reason_codes` / `corpus_summary` 回传；负向覆盖 unsupported `query_kind` 或 runtime canary 未放行时仍 lexical fallback
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_knowledge_runtime_query_surface_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeRuntimeQuerySurfaceIntegrationTest --output-on-failure`
   - 风险与回退：若 integration 只能在 full runtime path 下成立，则先以 access integration 固定 contract，再把 runtime-owned canary 放到 035

## 8. 回退与后继

1. 回退基线：所有新增 request 字段均为 optional；缺省仍走当前 lexical-only 行为。
2. 兼容基线：daemon/access 返回面只增不删；旧脚本不读取新字段时结果不变。
3. 后继顺序：`KNO-TODO-034-B -> KNO-TODO-035 -> KNO-TODO-036/037/038`。
4. 禁区：034 不解决 mixed-corpus route narrowing、production-grade embedding、vector telemetry 扩面；这些分别留给 035 / 036 / 038 / 039。

## 9. 完成判定

`KNO-TODO-034-B` 仅当以下条件同时满足时完成：

1. `knowledge retrieve` 能显式接收 `preferred_mode`、`query_kind`、`allowed_corpora`、`domain_tags`、`required_tags`、`required_language`。
2. daemon/access JSON payload 能返回 `mode`、`degraded`、`reason_codes`、`warning_count` 与 `corpus_summary`。
3. 未显式提供新字段时，现有 lexical-only baseline 与旧命令行为保持不变。
4. `contracts/` 无新增 retrieval supporting object，runtime-owned canary admission 仍未下沉到 Knowledge。