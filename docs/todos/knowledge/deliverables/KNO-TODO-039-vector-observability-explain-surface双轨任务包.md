# KNO-TODO-039 vector observability / explain surface 双轨任务包

## 1. 输入与目标

1. 来源任务：`KNO-TODO-039 | 接入 vector observability / explain surface`。
2. 上游前置：`KNO-TODO-038-B` 已完成，mixed-corpus hybrid 路由现已能在 dense-capable subset 上继续选路；但 production-composed retrieve telemetry 与 daemon/CLI JSON payload 仍主要停留在通用 `mode/degraded/reason_codes/warning_count/corpus_summary` 级别，调用方无法稳定判断 dense lane 是否命中、vector backend 是否 ready、warning 明细是什么。
3. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 1.1、2.1、3.1、6.10、6.11、6.13.2、6.13.4；`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中第三阶段 `KNO-TODO-039` 定义；ADR-006 / ADR-007 / ADR-008 owner boundary。
4. 本轮目标：在不扩 `contracts/`、不把 Runtime owner 下沉到 Knowledge 的前提下，把 vector-specific explain / telemetry 事实补齐到 module-local result、production telemetry sink 与 daemon/CLI payload，使调用方能直接判断 sparse/dense lane 命中、selected corpora、degraded reasons、warning summary 与 backend ready，而不需要读内部日志。

## 2. 研究证据

### 2.1 本地证据

1. `knowledge/include/health/KnowledgeTelemetry.h` 当前 `KnowledgeTelemetryEvent` 只有 `event_name`、`request_id`、`component`、`snapshot_id`、`result`、`degraded`、`latency_ms`、`reason_codes`、`corpus_ids`、`profile_id`、`query_kind`、`retrieval_mode`、`corpus_count`、`result_count`、`error_category`；尚无 vector backend ready、dense/sparse lane hit 或 warning summary 字段。
2. `knowledge/src/facade/KnowledgeService.cpp` 在成功 retrieve 时已经同时握有 `recall_result.candidates.sparse_hits/dense_hits`、`route_result.route_reason_codes`、`route_result.plan->corpus_ids`、`normalize_result.normalized_query->warnings` 和 `recall_result.candidates.warnings`，但最终只把 `reason_codes`、`warning_count` 与 `corpus_summary` 写回 `KnowledgeRetrieveResult`，没有继续形成 lane-level explain 摘要。
3. `knowledge/include/retrieve/RecallTypes.h` 已明确 `RecallCandidateSet` 包含 `sparse_hits`、`dense_hits`、`sparse_succeeded`、`dense_succeeded` 与 `warnings`；因此 039 无需重做 recall owner，只需把既有事实提炼成稳定 surface。
4. `access/src/AccessGatewayFactory.cpp` 当前 `format_knowledge_retrieve_payload()` 只输出 `mode`、`degraded`、`reason_codes`、`warning_count`、`corpus_summary`、`slice_count`、`first_citation`、`first_snippet`；缺少 vector-specific payload 字段。
5. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 当前 production telemetry sink 只把 `component/snapshot_id/request_id/profile_id/query_kind/retrieval_mode/outcome/corpus_count/result_count` 挂到 trace/audit/log surface；`reason_codes` 也只在 audit side effects 中以聚合字符串出现，未暴露 lane hit、warning summary 或 backend ready。
6. `KnowledgeHealthSnapshot` 现已提供 `vector_backend_available`，说明 “backend ready” 事实已存在于 Knowledge owner 的健康面中；039 应复用该事实，而不是另造新的 backend owner。

### 2.2 外部实践

1. Azure AI Search《Hybrid search using vectors and full text in Azure AI Search》强调 hybrid query 是一次请求中并行执行 full-text 与 vector query，再通过 RRF 合并结果；因此 explain surface 至少要能说明本次请求用了哪些 retrieval lane，以及最终选择了哪些结果集合，而不应只返回单个 mode 标签。
2. OpenSearch《Hybrid search explain》强调 explain 应暴露 score normalization、combination 和子查询贡献细节，同时明确这类深度 explain 代价较高，更适合作为 troubleshooting surface；对 DASALL 来说，这意味着 production 默认 surface 应优先提供轻量、稳定的 lane/route/backend 摘要，让调用方在无需昂贵 explain 的情况下也能定位大多数 hybrid 退化原因。

## 3. 设计结论

### 3.1 边界与不变式

1. vector explain / telemetry 摘要仍属于 Knowledge module-local result 与 Runtime/Access surface 的责任，不扩 `contracts/`，也不把 `RecallCandidateSet`、`RankedHitSet` 直接上抬为共享 ABI。
2. backend ready 事实由 Knowledge 现有 health owner 提供；039 只做读取与透传，不新增第二个 vector health owner。
3. Runtime production observability 可以扩 trace/audit/log attrs 与 side effects，但不应把高基数字段塞进 metrics labels，避免把 `selected_corpora` / `reason_codes` / `warning_summary` 直接变成 cardinality 爆炸点。
4. daemon/CLI payload 采用 additive 扩展，保留 034 已发布的 `mode/degraded/reason_codes/warning_count/corpus_summary/slice_count` 字段不变，新字段只增不删。
5. 039 只补 observability / explain surface，不修改路由语义、canary admission 或 refresh owner；040/041 继续分别负责 installed evidence 与 Runtime-owned refresh automation。

### 3.2 explain / telemetry 设计

| 设计点 | Owner | 方案 | 理由 |
|---|---|---|---|
| vector explain supporting shape | Knowledge | 在 `KnowledgeRetrieveResult` 与 `KnowledgeTelemetryEvent` 增加 `vector_backend_ready`、`sparse_hit_count`、`dense_hit_count`、`warning_summary` | 这些事实都能直接从现有 `health_snapshot`、`RecallCandidateSet` 与 warnings 聚合得到，不需要扩张到 contracts |
| selected corpora surface | Access + Runtime telemetry | 保留 `corpus_summary/corpus_ids`，并在 payload / audit / trace 上增加 `selected_corpora` 语义化别名或等价 side effect | 让控制面不必理解内部字段命名也能直接读出本次实际命中的 corpus scope |
| degraded reason surface | Knowledge + Access | 继续以稳定 `reason_codes` 为主，避免新造一套 degraded reason schema | 034/035/038 已经把 route / canary reason codes 固定为主解释面，039 只补充可观测传递 |
| warning summary surface | Knowledge + Access + Runtime telemetry | 将 query normalization warnings 与 recall warnings 去重后形成 `warning_summary`，同时保留 `warning_count` | count 只能判断有无 warning，无法解释 warning 的具体原因 |
| production telemetry sinks | Runtime | trace/audit/log surface 增加 lane hit、backend ready、selected corpora、warning summary；metrics 仅保留低基数 outcome / profile / error_code 维度 | 兼顾 explainability 与 metrics cardinality 安全 |

### 3.3 最小实现面

1. `knowledge/include/KnowledgeTypes.h`
   - 为 `KnowledgeRetrieveResult` 增加 module-local vector explain supporting 字段：`vector_backend_ready`、`sparse_hit_count`、`dense_hit_count`、`warning_summary`。
2. `knowledge/include/health/KnowledgeTelemetry.h`
   - 为 `KnowledgeTelemetryEvent` 增加对应 telemetry 字段，并在一致性校验中保证 `warning_summary` 唯一值约束仍成立。
3. `knowledge/src/facade/KnowledgeService.cpp`
   - 复用现有 `RecallCandidateSet`、warnings 与 `collect_health_snapshot`，在 success path 上构造 vector explain summary 并写入 result + telemetry event。
4. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`
   - 仅扩展 log/trace/audit attrs 与 side effects，不改变 metrics label 基线。
5. `access/src/AccessGatewayFactory.cpp`
   - 在 retrieve JSON payload 上追加 `vector_backend_ready`、`sparse_hit_count`、`dense_hit_count`、`warning_summary`、`selected_corpora`。
6. 测试面
   - `tests/unit/knowledge/KnowledgeRetrieveTelemetryFieldsTest.cpp`
   - `tests/integration/knowledge/KnowledgeProductionTelemetryIntegrationTest.cpp`
   - 新增 `tests/unit/access/AccessKnowledgeRetrievePayloadTest.cpp`
   - `tests/unit/access/CMakeLists.txt`

## 4. Design 原子清单

| D 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 固定 vector explain supporting shape，不扩 contracts | 本地证据 1/2/3/6；ADR-006/007/008 | 本文 §3.1 / §3.2 / §3.3 | result/telemetry 字段与 owner 边界明确 | 若必须把 `RecallCandidateSet` 或 `EvidenceSlice` 上抬到 contracts 才能表达 explain，则 D Gate 失败 |
| D2 | 固定 production observability surface，而不炸 metrics cardinality | 本地证据 4/5；OpenSearch explain | 本文 §3.1 / §3.2 | log/trace/audit 与 metrics 的分工明确 | 若需要把高基数字段直接塞进 metrics label，升级为 observability blocker |
| D3 | 锁定 daemon/CLI additive payload 与 focused validation 出口 | 本地证据 4；034 additive payload 基线 | 本文 §5 / §7 / §9 | 新 payload 字段、目标测试和 acceptance command 可直接复制 | 若 Access 验证仍需在 Build 阶段临时摸索入口，则 D Gate 失败 |

## 5. Design -> Build 映射

| Design 项 | Build 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| D1 vector explain shape | B1 扩展 Knowledge retrieve result / telemetry event | `knowledge/include/KnowledgeTypes.h`；`knowledge/include/health/KnowledgeTelemetry.h`；`knowledge/src/facade/KnowledgeService.cpp`；`tests/unit/knowledge/KnowledgeRetrieveTelemetryFieldsTest.cpp` | `dasall_knowledge_retrieve_telemetry_fields_unit_test` | `cmake --build build/vscode-linux-ninja --target dasall_knowledge_retrieve_telemetry_fields_unit_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeRetrieveTelemetryFieldsTest --output-on-failure` |
| D2 production observability | B2 扩展 Runtime telemetry sink attrs / side effects | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`；`tests/integration/knowledge/KnowledgeProductionTelemetryIntegrationTest.cpp` | `KnowledgeProductionTelemetryIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_knowledge_production_telemetry_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeProductionTelemetryIntegrationTest --output-on-failure` |
| D3 daemon explain payload | B3 新增 access retrieve payload focused gate | `access/src/AccessGatewayFactory.cpp`；新增 `tests/unit/access/AccessKnowledgeRetrievePayloadTest.cpp`；`tests/unit/access/CMakeLists.txt` | `AccessKnowledgeRetrievePayloadTest` | `cmake --build build/vscode-linux-ninja --target dasall_access_knowledge_retrieve_payload_unit_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R AccessKnowledgeRetrievePayloadTest --output-on-failure` |

## 6. D Gate 结果

结论：PASS。

通过依据：

1. 039 的根因已收敛到 result/telemetry/payload surface 缺字段，而不是 recall/rerank/router owner 错位。
2. lane hit、warning summary 与 selected corpora 都已经在 facade 当前控制路径上可得，backend ready 也已有现成 health owner；本轮不需要扩张到新的组件边界。
3. production telemetry 与 access payload 都有现成 focused tests 挂载点，Build 可以保持小切片推进。
4. 验收出口明确：Knowledge 单测、production telemetry integration 和 access payload unit 可以分别 build + ctest；若 ctest 再命中仓库已知泛化噪音，按仓库口径回退 direct binary。

进入 `KNO-TODO-039-B` 的前提：

1. 不新增 `contracts/` 或 profile schema v1 字段。
2. 不把高基数字段写入 metrics labels。
3. 至少覆盖 1 条 lexical-only baseline positive、1 条 hybrid-capable lane summary positive、1 条 additive payload 断言。

## 7. Build 原子清单

1. B1：扩展 Knowledge retrieve explain / telemetry summary
   - 代码目标：`knowledge/include/KnowledgeTypes.h`；`knowledge/include/health/KnowledgeTelemetry.h`；`knowledge/src/facade/KnowledgeService.cpp`；`tests/unit/knowledge/KnowledgeRetrieveTelemetryFieldsTest.cpp`
   - 测试目标：`KnowledgeRetrieveTelemetryFieldsTest`
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_knowledge_retrieve_telemetry_fields_unit_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeRetrieveTelemetryFieldsTest --output-on-failure`
   - 风险与回退：若 `collect_health_snapshot` 在 focused 单测中不可用，则允许以 provider seam stub 固定 `vector_backend_ready`，但不能改动 health owner
2. B2：扩展 Runtime production telemetry sinks
   - 代码目标：`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`；`tests/integration/knowledge/KnowledgeProductionTelemetryIntegrationTest.cpp`
   - 测试目标：`KnowledgeProductionTelemetryIntegrationTest`
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_knowledge_production_telemetry_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeProductionTelemetryIntegrationTest --output-on-failure`
   - 风险与回退：若某类高基数字段不适合进入 log attrs，则至少保留 trace/audit side effects，不得退回“只能读内部日志”状态
3. B3：新增 Access focused payload gate
   - 代码目标：`access/src/AccessGatewayFactory.cpp`；新增 `tests/unit/access/AccessKnowledgeRetrievePayloadTest.cpp`；`tests/unit/access/CMakeLists.txt`
   - 测试目标：`AccessKnowledgeRetrievePayloadTest`
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_access_knowledge_retrieve_payload_unit_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R AccessKnowledgeRetrievePayloadTest --output-on-failure`
   - 风险与回退：若无法直接复用 access gateway harness，可在 unit/access 下最小复制 daemon packet fixture，但不得把 `format_knowledge_retrieve_payload()` 暴露到 public header

## 8. 回退与后继

1. 回退基线：034 的 `mode/degraded/reason_codes/warning_count/corpus_summary` 继续保留，039 仅追加 vector-specific 字段。
2. 兼容基线：production default `LexicalOnly`、035 runtime-owned canary seam、038 mixed-corpus route narrowing 都不得因 observability surface 扩展而回退。
3. 测试基线：若 `RunCtest_CMakeTools` 或 `ctest` 继续命中仓库已知泛化噪音，可回退 direct binary，但不得跳过三条 focused gate。
4. 后继顺序：`KNO-TODO-039-B -> KNO-TODO-040`。
5. 禁区：039 不固化 installed canary artifact，也不自动触发 selective refresh；这些留给 040 / 041。

## 9. 完成判定

`KNO-TODO-039-B` 仅当以下条件同时满足时完成：

1. Knowledge retrieve result 与 telemetry event 已能稳定表达 `vector_backend_ready`、`sparse_hit_count`、`dense_hit_count`、`warning_summary`。
2. production-composed path 的 log/trace/audit surface 已能暴露 selected corpora、lane hit 与 degraded reason，不需要读内部 debug 日志才能解释 hybrid 行为。
3. daemon/CLI retrieve JSON payload 已能直接返回 vector-specific explain 字段，并保持 034 的旧字段兼容。
4. `KnowledgeRetrieveTelemetryFieldsTest`、`KnowledgeProductionTelemetryIntegrationTest` 与 `AccessKnowledgeRetrievePayloadTest` 全部保持绿色。
5. 未新增 `contracts/`、未放松 ADR-006 / ADR-007 / ADR-008 owner boundary、未把高基数字段塞入 metrics labels。

## 10. Build 完成证据

1. `knowledge/include/KnowledgeTypes.h`、`knowledge/include/health/KnowledgeTelemetry.h`、`knowledge/src/observability/KnowledgeTelemetry.cpp` 与 `knowledge/src/facade/KnowledgeService.cpp` 已把 vector explain supporting shape 收口到 module-local result / telemetry：retrieve success path 现稳定聚合 `warning_summary`、`vector_backend_ready`、`sparse_hit_count`、`dense_hit_count`，并在一致性校验中保证 `warning_summary` 去重与 `warning_count >= warning_summary.size()`。
2. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 与 `tests/integration/knowledge/KnowledgeProductionTelemetryIntegrationTest.cpp` 已把 production telemetry explain surface 扩展到 log / trace / audit：调用方现可直接读到 `selected_corpora`、`warning_summary`、`vector_backend_ready`、`sparse_hit_count`、`dense_hit_count`，同时保持 metrics labels 不引入高基数新维度。
3. `access/src/AccessGatewayFactory.cpp`、`tests/unit/access/AccessKnowledgeRetrievePayloadTest.cpp` 与 `tests/unit/access/CMakeLists.txt` 已为 daemon/access retrieve payload 追加 `selected_corpora`、`warning_summary`、`vector_backend_ready`、`sparse_hit_count`、`dense_hit_count` additive 字段，并保持 034 已发布的 `mode/degraded/reason_codes/warning_count/corpus_summary/slice_count` 向后兼容。
4. 2026-05-26 已通过 `cmake --build build/vscode-linux-ninja --target dasall_knowledge_retrieve_telemetry_fields_unit_test dasall_knowledge_production_telemetry_integration_test dasall_access_knowledge_retrieve_payload_unit_test -j2`；随后直接执行 `./build/vscode-linux-ninja/tests/unit/knowledge/dasall_knowledge_retrieve_telemetry_fields_unit_test && ./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_production_telemetry_integration_test && ./build/vscode-linux-ninja/tests/unit/access/dasall_access_knowledge_retrieve_payload_unit_test && echo PASS`，结果 `PASS`。