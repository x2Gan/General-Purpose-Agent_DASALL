# KNO-TODO-043 production embedding provider contract 收口双轨任务包

## 1. 输入与目标

1. 来源任务：`KNO-TODO-043 | production embedding provider 替代 env-gated local fallback`。
2. 上游前置：`KNO-TODO-042` 已把 strict hybrid ready marker 与 vector-enabled manifest 收口，`KNO-TODO-036` 已把 live dense path 冻结为 `IQueryEncoder` + detached `dense.sqlite`，但当前默认 query encoder 仍只走 `DASALL_DETACHED_VECTOR_LOCAL_FALLBACK=1` 的本地 hash fallback。
3. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 关于 `IQueryEncoder` 的 owner / inbound port 边界；`docs/architecture/DASALL_llm子系统详细设计.md` 关于 provider asset、`auth_ref` 与 secret ref 规范；`docs/ssot/SecretConsumerMatrix.md` 关于 live secret read owner；ADR-006 / ADR-007 / ADR-008 owner boundary。
4. 本轮目标：在不扩 `contracts/`、不把 owner 下沉到 `knowledge` 的前提下，把 runtime_support 默认 query encoder 从 env-gated local fallback 提升为 provider-backed production path，并补齐 provider-ready、provider-missing、provider-empty-embedding、invalid-secret 的 fail-closed 证据。

## 2. 研究证据

### 2.1 本地证据

1. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 当前默认创建的是 `RuntimeDetachedVectorQueryEncoder`，其 `encode()` 和 `available()` 都直接调用 `memory::encode_detached_vector_query_for_local_fallback()` / `memory::detached_vector_local_query_encoder_available()`。
2. `memory/src/vector/DetachedVectorIndexFactory.cpp` 已把 `DASALL_DETACHED_VECTOR_LOCAL_FALLBACK` 冻结为 explicit opt-in；env 未开启时，默认 query encoder 返回空 embedding / unavailable。
3. `tests/integration/access/RuntimeKnowledgeQueryEncoderIntegrationTest.cpp` 当前正例除了 override encoder 外，只覆盖“显式开启 local fallback env 后 hybrid canary 可转绿”；这还不能证明 production provider path 已闭合。
4. `llm/src/provider/ProviderCatalogRepository.cpp` 与 `llm/assets/providers/*` 已提供受治理的 provider asset owner：manifest 必须包含 `provider_id`、`base_url`、`auth_ref`、`tags` 等字段，`auth_ref` 只能是 `secret://` 或 `profile://` 引用。
5. `docs/ssot/SecretConsumerMatrix.md` 已冻结 live secret read 语义：runtime consumer 应复用标准 secret 读链，provider asset 只保存 redacted `auth_ref`，不得把明文凭据写回资产。
6. `knowledge/src/KnowledgeServiceFactory.cpp` 当前 runtime canary admission 直接依赖 `dense_bridge->available()`；因此 provider-missing / invalid-secret / empty-embedding 若不能在 admission 前被识别，就会把 explicit canary 误放进 dense lane，再在 recall 期退化为 Hybrid degraded 或 fail-closed，而不是 lexical-only。

### 2.2 外部实践

1. Azure AI Search《Create a vector query in Azure AI Search》明确要求 query string 在进入 vector retrieval 前必须先转换为向量，并建议 query embedding 与文档 embedding 使用同一模型，而不是在 query path 上临时换一套近似逻辑。对 DASALL 来说，这意味着 production query encoder 不能继续把 local hash fallback 当成 production semantic quality 依据。
2. 同一文档还明确给出 query-time embedding 的生产形态：应用代码显式调用 embedding API，携带 provider auth，并从响应中的 `embedding` 数组提取 query vector。这与 DASALL 当前“runtime owner 负责 concrete provider wiring、Knowledge 只消费 `IQueryEncoder` port”的边界一致。

## 3. 设计结论

### 3.1 可证伪假设

如果 runtime_support 在创建默认 query encoder 时改为：

1. 从 installed provider assets 中选择一个显式声明为 knowledge query embedding owner 的 provider/model；
2. 通过 runtime live secret seam materialize 该 provider 的 `auth_ref`；
3. 用 provider-backed HTTP embedding request 生成 query embedding；

那么 explicit hybrid canary 在禁用 `DASALL_DETACHED_VECTOR_LOCAL_FALLBACK` 后仍可走通 production-ready 正例；反之，provider 资产缺失、secret 缺失/不可 materialize、provider 返回空 embedding 或认证失败时，runtime canary 必须在 admission 前 fail-closed 回 lexical-only，并留下明确 reason code。

廉价反证：禁用 local fallback env 后，若 provider-ready 路径仍只能依赖 `create_query_encoder_override` 才转绿，或者 provider-missing / invalid-secret path 仍被误 admit 为 Hybrid，则假设不成立。

### 3.2 owner 边界与不变式

1. `IQueryEncoder` owner 继续是 `knowledge/include/retrieve/IQueryEncoder.h`；本轮不改 Knowledge port ABI。
2. provider asset / secret / HTTP transport 的 concrete wiring owner 继续归 runtime_support；Knowledge 不直接 include `llm` concrete provider types，也不解释 `auth_ref`。
3. local fallback 继续保留，但只能作为 local/test/package fallback：显式 env 开启或测试 override 时才允许使用；production provider positive path 不再把它视为通过依据。
4. secret read 继续走 runtime live seam 的标准读链；provider asset 只保存 redacted `auth_ref`，不新增第二套 secret owner。
5. 默认 Agent 主路径仍保持 lexical-only baseline；只有 explicit allowlisted hybrid canary 才会因为 provider-backed encoder ready 被 admit 为 Hybrid/Dense。

### 3.3 provider asset / secret / health contract

| 设计点 | Owner | 方案 | 理由 |
|---|---|---|---|
| provider asset contract | `llm/assets/providers/*` | 在现有 provider catalog 中声明一个 `feature_notes` 命中的 query embedding model，并复用既有 `provider_id/base_url/auth_ref` 资产治理 | 不新增 profile schema，也不把 provider 选择硬编码成 runtime-only 常量 |
| secret contract | runtime live secret seam | query encoder 只消费 redacted `auth_ref`，运行时 materialize bearer；secret 缺失或 materialize 失败时直接标记 backend-not-ready | 与 `SecretConsumerMatrix` 和现有 LLM live read 语义保持一致 |
| health contract | runtime_support 本地缓存状态 | provider-missing、secret-missing、invalid-secret、empty-embedding 都要更新 runtime-local encoder health，并让 explicit canary admission 读取该状态做 lexical fallback | 既要把 explicit canary fail-closed 压到 admission 前，又不能让普通 lexical retrieve 每次都被动联网 |
| telemetry contract | knowledge production telemetry | explicit canary 请求与 provider-backed encoder 状态要能在 shared audit / metrics / trace 中留下 `runtime_canary_*` 与 `query_encoder_*` reason code / warning token | 让生产诊断不依赖内部日志 grep |

### 3.4 最小实现面

1. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`
   - 新增 provider-backed query encoder 与 runtime-local health state。
   - 默认 encoder 选择顺序改为：provider-backed production path -> explicit local fallback env -> unavailable。
   - runtime canary admission 读取 provider-backed health state；对 provider-missing / invalid-secret / empty-embedding path 返回 lexical-only。
2. `llm/assets/providers/deepseek/models.yaml`
   - 补一个受治理的 embedding model 元数据，并以 `feature_notes` 标记为 Knowledge query embedding 默认候选。
3. `tests/integration/access/RuntimeKnowledgeQueryEncoderIntegrationTest.cpp`
   - 新增 provider-ready、provider-missing、provider-empty-embedding、invalid-secret 四条 focused case；provider-ready 正例必须在 local fallback env 关闭时仍通过。
4. `tests/integration/knowledge/KnowledgeProductionTelemetryIntegrationTest.cpp`
   - 增加 explicit canary telemetry 验证，锁定 provider-backed ready / fail-closed 的 shared sink token。

## 4. Design 原子清单

| D 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 固定 provider-backed query encoder 的 owner 边界 | 本地证据 1 / 4 / 5 / 6 | 本文 §3.2 / §3.3 | concrete wiring 明确停留在 runtime_support，不改 Knowledge ABI | 若必须把 port 扩到 `llm` / `knowledge` 公共面，则 D Gate 失败 |
| D2 | 固定 provider asset / secret / health contract | 本地证据 4 / 5 / 6；外部实践 1 / 2 | 本文 §3.3 | provider、secret、health 三者的 fail-closed 口径明确 | 若 explicit canary 仍只能在 dense recall 期才暴露 secret/auth 问题，则 D Gate 失败 |
| D3 | 锁定最小 Build 切口与 focused tests | 本地证据 2 / 3 / 6 | 本文 §3.4 / §5 / §7 | 代码目标、测试目标、验收命令三件套完整 | 若必须引入 release / qemu / package rerun 才能表达完成判定，则 D Gate 失败 |

## 5. Design -> Build 映射

| Design 项 | Build 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| D1 | B1 实装 provider-backed query encoder | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | focused compile + runtime query encoder integration | `cmake --build build/vscode-linux-ninja --target dasall_runtime_knowledge_query_encoder_integration_test -j2` |
| D2 | B2 补 provider asset 元数据 | `llm/assets/providers/deepseek/models.yaml` | provider catalog parse / runtime integration | `./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_query_encoder_integration_test` |
| D2 | B3 收口 telemetry / reason code | `tests/integration/knowledge/KnowledgeProductionTelemetryIntegrationTest.cpp` 与必要 runtime code | provider-ready / provider-missing / invalid-secret explain surface | `cmake --build build/vscode-linux-ninja --target dasall_knowledge_production_telemetry_integration_test -j2`；`./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_production_telemetry_integration_test` |
| D3 | B4 focused integration matrix | `tests/integration/access/RuntimeKnowledgeQueryEncoderIntegrationTest.cpp` | provider-ready、provider-missing、provider-empty-embedding、invalid-secret | `./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_query_encoder_integration_test` |

## 6. D Gate 结果

结论：PASS。

通过依据：

1. 当前缺口已局部化到 runtime_support 默认 query encoder wiring，而不是 Knowledge port owner 或 profile schema。
2. provider asset、secret ref 与 live secret read 语义在仓库中已有稳定 owner，可直接复用。
3. explicit canary admission 已有单一控制点：只要在 admission 前读取 provider-backed health 状态，就能把 provider-missing / invalid-secret / empty-embedding 统一压回 lexical-only。
4. focused 验收出口清晰：`RuntimeKnowledgeQueryEncoderIntegrationTest` 与 `KnowledgeProductionTelemetryIntegrationTest` 足以证伪本轮假设。

进入 `KNO-TODO-043-B` 的前提：

1. 不新增 `contracts/` 字段，不改 Knowledge public ABI。
2. 不把 provider 选择写成 profile 顶层新键。
3. provider-ready 正例必须在关闭 `DASALL_DETACHED_VECTOR_LOCAL_FALLBACK` 后仍通过。
4. provider-missing / invalid-secret / empty-embedding 必须在 canary admission 前 fail-closed 到 lexical-only。

## 7. Build 原子清单

1. B1：实现 provider-backed query encoder
   - 代码目标：`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`
   - 测试目标：focused compile；provider-backed query encoder 可替代默认 local fallback
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_runtime_knowledge_query_encoder_integration_test -j2`
   - 风险与回退：若 provider asset 或 secret 未 ready，显式 canary 直接 lexical fallback；不把异常泄露成 knowledge owner 语义漂移
2. B2：补 provider asset 元数据
   - 代码目标：`llm/assets/providers/deepseek/models.yaml`
   - 测试目标：provider model 可被 runtime encoder 发现为默认 embedding 候选
   - 验收命令：`./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_query_encoder_integration_test`
   - 风险与回退：若 asset 合同不足以表达 embedding 候选，优先在 `feature_notes` 补最小标记，不新增新目录层级或 profile 语义
3. B3：补 focused integration 与 telemetry gate
   - 代码目标：`tests/integration/access/RuntimeKnowledgeQueryEncoderIntegrationTest.cpp`、`tests/integration/knowledge/KnowledgeProductionTelemetryIntegrationTest.cpp`
   - 测试目标：provider-ready、provider-missing、provider-empty-embedding、invalid-secret；shared sink reason code
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_runtime_knowledge_query_encoder_integration_test dasall_knowledge_production_telemetry_integration_test -j2`；随后直接执行两个 binaries
   - 风险与回退：若 direct binary 验证通过而 `RunCtest_CMakeTools` 继续命中仓库已知泛化失败，则沿用 direct-binary fallback，不扩大验证面

## 8. 回退与后继

1. 回退基线：`DASALL_DETACHED_VECTOR_LOCAL_FALLBACK=1` 继续保留给 local/test/package fallback，但不再作为 production provider 通过证据。
2. 非目标：本轮不重跑 installed proof / soak，不把 qemu / release-runner 证据混入 043；这些由 044 的已闭合证据和后续 release owner 继续承担。
3. 后继顺序：`KNO-TODO-043-B` 已完成；本轮进入 TODO/worklog 回写与提交推送，不扩大到 installed / qemu / release 证据。

## 9. 完成判定

`KNO-TODO-043-B` 已于 2026-05-26 完成；本轮满足以下条件：

1. 默认 query encoder 已改为 provider-backed production path，且关闭 `DASALL_DETACHED_VECTOR_LOCAL_FALLBACK` 后 provider-ready 正例仍可返回 Hybrid/Dense evidence；
2. provider-missing、provider-empty-embedding、invalid-secret 三条路径都会在 explicit canary admission 前 fail-closed 到 lexical-only；
3. shared telemetry / audit surface 能暴露与 provider query encoder 相关的明确 reason code / warning token；
4. focused 验收 `dasall_runtime_knowledge_query_encoder_integration_test` 与 `dasall_knowledge_production_telemetry_integration_test` 通过；
5. 未回退 ADR-006 / ADR-007 / ADR-008 owner boundary，Knowledge 仍不拥有 provider asset / secret / transport concrete owner。

## 10. Build 完成证据（2026-05-26）

1. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已新增 provider-backed query encoder 与 runtime-local health state，并把默认 encoder 选择顺序收口为：测试 override -> 显式 `DASALL_DETACHED_VECTOR_LOCAL_FALLBACK=1` local fallback -> provider-backed production path -> unavailable fallback。provider-backed path 统一处理 provider 选择、secret materialize、HTTP embedding request、cached embedding 与 reason code，不改 `knowledge/include/retrieve/IQueryEncoder.h` ABI。
2. `knowledge/include/KnowledgeServiceFactory.h` 与 `knowledge/src/KnowledgeServiceFactory.cpp` 已新增 runtime-owned passive health / active canary seam：普通 lexical retrieve 不被动联网；只有 explicit hybrid canary admission 才读取 `runtime_canary_backend_ready(query)`。因此 provider-missing、provider-empty-embedding、invalid-secret 都会在 admission 前 lexical-only fail-closed，并附带 `runtime_canary_backend_not_ready` 与对应 `query_encoder_*` reason code。
3. `llm/assets/providers/deepseek/models.yaml` 已新增 `deepseek-embedding` metadata 并以 `feature_notes: [knowledge_query_embedding_default]` 标记默认 query embedding 候选。`tests/integration/access/RuntimeKnowledgeQueryEncoderIntegrationTest.cpp` 已覆盖 provider-ready、provider-missing、provider-empty-embedding、invalid-secret、local-fallback 五条 focused case；provider-ready 正例在关闭 local fallback env 后仍返回 Hybrid，并复用 startup probe 预热的 cached embedding。
4. `tests/integration/knowledge/KnowledgeProductionTelemetryIntegrationTest.cpp` 与 `tests/integration/knowledge/CMakeLists.txt` 已补 provider-backed telemetry gate。2026-05-26 已通过 `Build_CMakeTools(buildTargets=["dasall_runtime_knowledge_query_encoder_integration_test","dasall_knowledge_production_telemetry_integration_test"])`；`RunCtest_CMakeTools(tests=["KnowledgeProductionTelemetryIntegrationTest"])` 继续命中仓库已知泛化 `生成失败`，因此沿用 direct-binary fallback，执行 `./build/vscode-linux-ninja/tests/integration/access/dasall_runtime_knowledge_query_encoder_integration_test && echo PASS` 与 `./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_production_telemetry_integration_test && echo PASS`，结果均为 `PASS`。