# DASALL Knowledge 子系统专项 TODO

最近更新时间：2026-04-21  
阶段：Detailed Design -> Special TODO  
适用范围：knowledge/  
当前结论：Knowledge 子系统当前可直接生成 L3/L2 混合专项 TODO。`KnowledgeQuery`、`EvidenceSlice`、`EvidenceBundle`、`KnowledgeRetrieveResult`、`IKnowledgeService`、`KnowledgeConfigProjector`、`QueryNormalizer`、`CorpusRouter`、`Reranker`、`EvidenceAssembler`、`KnowledgeServiceFacade`、`FreshnessController`、`VersionLedger`、`CorpusCatalog`、`SourceScanner`、`Canonicalizer`、`Chunker`、`IngestionCoordinator`、`KnowledgeTelemetry`、`KnowledgeHealthProbe`、`RetrievalQualityRegressionTest` 已具备接口级或数据结构级拆分条件；`KNO-TODO-001` 已冻结 lexical 路线为 SQLite FTS5，`KNO-TODO-002` 已冻结 `VectorRetrieverBridge` 的 owner、注入方向与 module-local ports（`IQueryEncoder` / `IVectorRecallStore`），`KNO-TODO-003` 已冻结首批 corpus baseline、`AuthorityLevel` / `SourceFormat` / `CorpusScanPlan` 与 quarantine 规则，`KNO-TODO-004` 已冻结 retrieval quality YAML manifest、样本覆盖下限、`MRR@10` / `NDCG@10` / `Recall@5` / `Recall@10` 阈值、`hard_fail` case 与 context-level 扩展槽位，`KNO-TODO-005` 已建立 `knowledge/include`、`dasall_knowledge_interface_surface_unit_test` 与 `KnowledgeIntegrationTopologySmokeTest` 的 build/discoverability 骨架，`KNO-TODO-006` 已冻结 `KnowledgeQuery` / `EvidenceBundle` / `CorpusDescriptor` / `KnowledgeConfigSnapshot` / `RefreshResult`、`IKnowledgeService` 四方法签名与 `KnowledgeErrorCode -> ErrorInfo` 映射 helper，`KNO-TODO-007` 已实现 `RuntimePolicySnapshot + BuildProfileManifest + overlay -> KnowledgeConfigSnapshot` 的单一投影入口，`KNO-TODO-008` 已实现 `QueryNormalizer` 的确定性文本规范化、alias/allowlist 收窄、warning 语义与 `MultiHop -> NotSupported` fail-fast，`KNO-TODO-009` 已实现 `CorpusRouter` 的 corpus 过滤、freshness gate、mode 选择、route reason code 与 `RetrievalPlan` supporting layer，`KNO-TODO-010` 已实现 `RecallHit/RecallCandidateSet` 最小 supporting shape、`Reranker` 的 RRF merge、stale penalty、authority weighting 与 ranked hit 输出，`KNO-TODO-011` 已实现 `EvidenceAssembler` 的 structured slice 组装、projection budget clamp、`omitted_sources` 与 single-line `context_projection` 映射，`KNO-TODO-016` 已实现 `CorpusCatalog` 的只读 route metadata snapshot、delta apply fail-closed 语义与三条 unit gate，`KNO-TODO-017` 已实现 `FreshnessController` / `FreshnessSnapshot` 的纯计算评估、stale dual gate 与两条 unit gate，`KNO-TODO-025` 已实现 `KnowledgeTelemetry` 的统一事件桥、invalid payload fallback 与 sink failure accounting，`KNO-TODO-026` 已实现 `KnowledgeHealthSnapshot` public ABI、provider-seam `KnowledgeHealthProbe` 聚合与三条 health unit gate，并解除 `KNO-BLK-001`、`KNO-BLK-002`、`KNO-BLK-003`、`KNO-BLK-004`。当前已无设计 blocker，公共 include、public ABI、配置投影、query 规范化、route planning、rerank 计算、evidence 组装、catalog snapshot、新鲜度评估、观测桥、健康快照、unit/integration 拓扑、lexical/hybrid/ingest/index/quality 全链路均可按任务前置关系进入 Build 排程。

## 1. 文档头

本文档严格基于以下输入生成：

1. `docs/architecture/DASALL_knowledge子系统详细设计.md`
2. `docs/architecture/DASALL_Agent_architecture.md`
3. `docs/architecture/DASALL_Engineering_Blueprint.md`
4. `docs/adr/ADR-005-architecture-review-baseline.md`
5. `docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md`
6. `docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md`
7. `docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md`
8. `docs/ssot/InfraConcurrencyPolicy.md`
9. `docs/ssot/InfraIntegrationTopology.md`
10. `docs/plans/DASALL_工程落地实现步骤指引.md`
11. `docs/development/DASALL_工程协作与编码规范.md`
12. 现有专项 TODO / 交付基线：`docs/todos/llm/DASALL_llm子系统专项TODO.md`、`docs/todos/memory/DASALL_memory子系统专项TODO.md`、`docs/todos/tools/DASALL_tools子系统专项TODO.md`、`docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md` 及对应 deliverables 目录
13. 当前代码与测试现状：`knowledge/CMakeLists.txt`、`knowledge/src/KnowledgeBuildSkeleton.cpp`、`knowledge/include/KnowledgeTypes.h`、`knowledge/include/KnowledgeErrors.h`、`knowledge/include/IKnowledgeService.h`、`knowledge/include/query/QueryNormalizer.h`、`knowledge/include/query/CorpusRouter.h`、`knowledge/include/retrieve/RecallTypes.h`、`knowledge/include/rerank/Reranker.h`、`knowledge/include/evidence/EvidenceAssembler.h`、`knowledge/src/query/QueryNormalizer.cpp`、`knowledge/src/query/CorpusRouter.cpp`、`knowledge/src/rerank/Reranker.cpp`、`knowledge/src/evidence/EvidenceAssembler.cpp`、`knowledge/include/index/CorpusCatalog.h`、`knowledge/src/index/CorpusCatalog.cpp`、`knowledge/include/health/FreshnessController.h`、`knowledge/src/health/FreshnessController.cpp`、`tests/unit/knowledge/CMakeLists.txt`、`tests/unit/knowledge/QueryNormalizerTest.cpp`、`tests/unit/knowledge/QueryNormalizerBoundaryTest.cpp`、`tests/unit/knowledge/CorpusRouterTest.cpp`、`tests/unit/knowledge/CorpusRouterFreshnessPolicyTest.cpp`、`tests/unit/knowledge/CorpusRouterModeSelectionTest.cpp`、`tests/unit/knowledge/RerankerTest.cpp`、`tests/unit/knowledge/HybridRrfMergeTest.cpp`、`tests/unit/knowledge/RerankerFreshnessPenaltyTest.cpp`、`tests/unit/knowledge/EvidenceAssemblerTest.cpp`、`tests/unit/knowledge/ContextProjectionMapperTest.cpp`、`tests/unit/knowledge/EvidenceBudgetClampTest.cpp`、`tests/unit/knowledge/CorpusCatalogTest.cpp`、`tests/unit/knowledge/CorpusCatalogDeltaApplyTest.cpp`、`tests/unit/knowledge/CorpusCatalogColdStartTest.cpp`、`tests/unit/knowledge/FreshnessControllerTest.cpp`、`tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp`、`tests/integration/knowledge/CMakeLists.txt`、`tests/integration/CMakeLists.txt`、顶层 `CMakeLists.txt`

本文档目的不是补写新架构，也不是宣称 Knowledge 已具备直接全量编码条件，而是把当前详细设计转换为：

1. 可回溯的约束清单。
2. 可执行的粒度评估结论。
3. Design -> TODO 的工程映射。
4. 最小原子化、可二值判定的任务清单。
5. 显式 blocker、解阻条件、质量门与回退策略。

生成原则：

1. 不改写已冻结 ADR 结论。
2. 不把 Knowledge 扩张为 LLM、Memory、Runtime、Tools 的替代实现。
3. 纯讨论事项不伪装为 Done-ready Build 任务。
4. 每项任务都必须具备代码目标、测试目标、验收命令三件套。
5. 设计证据不足之处必须先列为补设计/Blocked，而不是伪造实现任务。
6. 所有任务都必须回链到详细设计章节、现有代码现状或上层约束文档。

---

## 2. 子系统目标与范围

### 2.1 子系统目标

根据 Knowledge 详设 1.1、架构 5.9、蓝图 3.8/4.1/6、阶段 H 实施要求，Knowledge 子系统的工程目标固定为：

1. 向 Runtime 提供受治理的查询归一化、检索召回、重排与证据组装能力，而不是把原始索引结果直接泄漏给上层。
2. 以 `IKnowledgeService` 作为唯一 runtime-facing 公共入口，稳定输出 `KnowledgeRetrieveResult`、`EvidenceBundle` 与 `context_projection`，供 Runtime 再投影到 `ContextPacket.retrieval_evidence`。
3. 在 `knowledge=true && memory_vector=false` 的组合下稳定提供 lexical-only 主链，在向量后端不可用时显式退化而不是进程级失败。
4. 建立索引新鲜度、版本账本、增量更新和 snapshot-and-swap 闭环，并保证 query path 不读取半成品索引。
5. 为 `knowledge/include`、`knowledge/src`、`tests/unit/knowledge`、`tests/integration/knowledge`、CMake、质量门与交付证据提供可实施、可测试、可审计的落盘计划。

### 2.2 范围边界

纳入本专项 TODO 的对象：

1. `knowledge/include` 公共接口面与 `knowledge/src` 内部实现骨架。
2. `KnowledgeServiceFacade`、`QueryNormalizer`、`CorpusRouter`、`RecallCoordinator`、`SparseRetriever`、`VectorRetrieverBridge`、`Reranker`、`EvidenceAssembler`。
3. `CorpusCatalog`、`FreshnessController`、`VersionLedger`、`IndexReader`、`IndexWriter`、`SourceScanner`、`Canonicalizer`、`Chunker`、`IngestionCoordinator`。
4. `KnowledgeConfigProjector`、`KnowledgeTelemetry`、`KnowledgeHealthProbe` 与相关 profile 投影、可观测性和健康快照。
5. `MetadataExtractor`、`EmbeddingEncoder` 作为内部策略扩展点（详设 §6.13.6 标记为内部策略点，v1 不单独拆为编译单元，分别嵌入 `Canonicalizer` / `IngestionCoordinator` 实现内部）。
6. Knowledge 相关 unit / integration / quality regression / Gate 证据回写。

不纳入本专项 TODO 的对象：

1. `ContextPacket` 的生成、裁剪与写权限；该职责仍在 Memory / `ContextOrchestrator`。
2. Prompt 选择、消息装配、tokenizer 精确预算裁剪与 provider payload 生成；该职责仍在 LLM。
3. Runtime 的 retry / replan / degrade / abort_safe 裁定与调度。
4. Memory 的 Session/Fact 写回、Experience 沉淀、VectorMemory 生命周期管理。
5. shared contracts 的主动扩张；Knowledge supporting types 当前保持 module-local。
6. 独立文件监听器、独立调度循环或自行拥有定时器的刷新主控。
7. query rewrite、answer synthesis、LLM 直接调用、跨层语义增强。
8. `HotResultCache`（详设 §6.12 锁表列出 L2 层级，v1 明确不实现，不拆分任务）。
9. `KnowledgeQuery.latest_observation_digest_summary` / `belief_state_summary` 等 v1 标记为 not-consumed 的预留字段消费逻辑（在 KNO-TODO-006 中仅声明字段，不落实际处理链路）。
10. `KnowledgeQueryKind::MultiHop` 的实际多跳执行链路（v1 仅声明枚举值，不落 multi-hop 召回编排与证据拼接逻辑）。

---

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| KNO-TC001 | 架构 5.9.1；详设 1.1、6.1 | Must | Knowledge 负责查询理解、召回、重排、证据组装与索引新鲜度 | TODO 必须覆盖 query/evidence/index 三条主链，而不是只做 facade 空壳 |
| KNO-TC002 | ADR-008；详设 1.2、6.9 | Must-Not | Knowledge 不拥有独立调度循环或刷新触发权，`request_refresh()` 由 Runtime 调用 | ingest/refresh 任务不得引入自主管理 watcher/timer 主控 |
| KNO-TC003 | ADR-006；详设 1.2、6.5、6.6 | Must-Not | Knowledge 不生成 `ContextPacket`、Prompt、provider payload，不接管上下文装配权 | 不生成 PromptComposer、ContextPacket 写入或 query rewrite 任务 |
| KNO-TC004 | 蓝图 4.1/4.2；详设 1.2、6.3 | Must | Knowledge 只依赖 `contracts/`、`memory/` 窄接口与 `infra/`，不依赖 `llm/`、`cognition/`、`tools/` 实现 | 任务必须以 `knowledge/include` 的 module-local 接口为边界 |
| KNO-TC005 | 蓝图 6；详设 6.6 | Must | `IKnowledgeService` 必须先落 `knowledge/include`，保持 public surface 小而稳 | 必须先做 include/CMake/public ABI 冻结任务 |
| KNO-TC006 | 架构 3.8.2；详设 6.5、6.8 | Must | `KnowledgeRetrieveResult.error` 复用 `ErrorInfo`，失败语义映射到 Validation/Policy/Provider/Runtime | 错误码与 `ErrorInfo` 映射必须单列任务并带测试 |
| KNO-TC007 | 详设 6.6、6.10 | Must | `retrieve()` 只读且幂等；`request_refresh()` 是唯一写入口 | 任务拆分时必须把读路径与写路径分离，不做隐式 rebuild |
| KNO-TC008 | 详设 6.5、6.6 | Must | `EvidenceBundle.context_projection` 是写入 `ContextPacket.retrieval_evidence` 的唯一共享投影面 | evidence 组装和 projection mapping 必须原子建模 |
| KNO-TC009 | 详设 6.10；蓝图 5.1 | Must | 不新增 profile schema v1 顶层域，`KnowledgeConfigProjector` 只能消费既有 snapshot 域 | 需要单列配置投影任务，不允许自建平行配置系统 |
| KNO-TC010 | 蓝图 5.1；详设 6.10 | Must | `knowledge=true && memory_vector=false` 是合法组合，必须支持 lexical-only | Query/Router/Recall/Integration 任务必须覆盖 degrade 路径 |
| KNO-TC011 | SSOT `InfraConcurrencyPolicy`；详设 6.12、6.13.3 | Must | 新增 queue/buffer/snapshot 设计必须显式声明 overflow_policy、backpressure 与 lock order；不允许持 L2 锁做 I/O | IndexReader/IndexWriter/Telemetry/Health 任务必须回链并发 SSOT |
| KNO-TC012 | SSOT `InfraIntegrationTopology`；详设 9.1、9.3 | Must | 新增核心链路后必须补至少 1 个 `integration` smoke，并能被 `ctest -N` 发现 | 测试拓扑和 integration discoverability 必须前置 |
| KNO-TC013 | 编码规范 3.2/3.6/3.7 | Must | 公共接口进 `include/`；不吞错；新增 public interface 至少补 unit 或 integration 测试 | public ABI、错误映射、CMake 拓扑任务都必须绑定测试 |
| KNO-TC014 | 落地指引阶段 H；ADR-005 | Must | Knowledge 位于 memory 之后、cognition 之前推进；不得越过 memory/vector 边界冻结 | vector bridge 与 corpus/index 任务需要显式 blocker |
| KNO-TC015 | 详设 6.11、9.3、11.1 | Must | retrieve / ingest / snapshot swap / degraded return 四类关键路径必须可观测 | Telemetry 与 HealthProbe 不能后置成“实现后再补” |
| KNO-TC016 | 详设 11.1 KNO-B02、12.1 | Must | `VectorRetrieverBridge` 的窄接口 ownership 未冻结前，不允许推进 hybrid Build | hybrid recall、failure/degrade、profile integration 必须标记 Blocked |
| KNO-TC017 | 详设 11.1 KNO-B03、12.1 | Must | 词法索引技术选型未冻结前，不允许推进 `SparseRetriever` / `IndexReader` / `IndexWriter` 的真实 Build | lexical 主链任务需要补设计前置任务 |
| KNO-TC018 | 详设 6.9、11.1 KNO-B04 | Must | 首批 corpus 资产、metadata 必填字段与 trust 规则缺失时，不允许推进 ingest/index 验证任务 | `SourceScanner`、`Canonicalizer`、`Chunker`、`IngestionCoordinator` 需前置补设计 |
| KNO-TC019 | 详设 6.9 Ingest 触发模型、11.1 KNO-R10 | Must | `SourceScanner` 仅接受 `trust_level >= Trusted` 的注册源，坏源需 quarantine | corpus/trust 规则必须入设计、测试与审计 |

### 3.2 当前代码与测试现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| `knowledge/CMakeLists.txt` | 已定义 `dasall_knowledge`，并通过 `FILE_SET public_headers` 显式导出 `KnowledgeTypes.h` / `KnowledgeErrors.h` / `IKnowledgeService.h`，编译锚点切换为 `src/KnowledgeBuildSkeleton.cpp` | Knowledge 已具备 public include skeleton，不再是 placeholder-only 静态库 |
| `knowledge/src/KnowledgeBuildSkeleton.cpp` | 负责锚定 `dasall_knowledge` 编译单元并校验 skeleton headers 可编译 | 后续 public ABI / config / observability 任务可以在不回退库拓扑的前提下增量落盘 |
| `knowledge/include/` | 目录已存在，并已在 `KnowledgeTypes.h` / `KnowledgeErrors.h` / `IKnowledgeService.h` 中冻结 query/evidence/corpus/config/refresh ABI、错误映射 helper 与 service 四方法签名 | public interface 根目录、真实 ABI 与 ErrorInfo 投影边界均已建立，007/025 可直接增量实现 |
| `knowledge/include/query/QueryNormalizer.h`、`knowledge/src/query/QueryNormalizer.cpp` | 已落盘 `QueryNormalizePolicy`、`NormalizedQuery`、`NormalizeResult` 与 `QueryNormalizer::normalize()`，提供 text canonicalization、alias/allowlist 收窄、warning 语义和 `MultiHop -> NotSupported` fail-fast | `CorpusRouter` 已具备稳定的标准化 query supporting header，不再需要在路由层重复做文本清洗或 tag 归并 |
| `knowledge/include/query/CorpusRouter.h`、`knowledge/src/query/CorpusRouter.cpp` | 已落盘 `RetrievalPlan`、`RoutePlanResult` 与 `CorpusRouter::build_plan()`，提供 corpus 过滤、freshness gate、mode 选择、partial result 准入和 route reason code | 013/014/015/010 已具备稳定的 routing plan supporting header，不再需要在召回/重排层重复决策 corpus、mode 或 stale policy |
| `knowledge/include/retrieve/RecallTypes.h`、`knowledge/include/rerank/Reranker.h`、`knowledge/src/rerank/Reranker.cpp` | 已落盘 `RecallHit` / `RecallCandidateSet`、`RankedHit` / `RankedHitSet`、`RerankPolicy` 与 `Reranker::rerank()`，提供去重、RRF、authority weighting、stale penalty 和 lexical-first fallback | 011 `EvidenceAssembler` 已具备稳定的 ranked hit supporting header，不再需要在证据组装层重复排序逻辑 |
| `knowledge/include/evidence/EvidenceAssembler.h`、`knowledge/src/evidence/EvidenceAssembler.cpp` | 已落盘 `EvidenceAssemblePolicy` 与 `EvidenceAssembler::assemble()`，提供 structured slice 生成、single-line `context_projection` 映射、budget clamp、`omitted_sources` 与 `evidence_insufficient` 语义 | 012/027 已具备稳定的 evidence bundle supporting layer，不再需要在 facade 或 smoke 路径重复 projection/budget 规则 |
| `knowledge/include/index/CorpusCatalog.h`、`knowledge/src/index/CorpusCatalog.cpp` | 已落盘 `CorpusCatalog` / `CorpusCatalogSnapshot` / `CorpusCatalogDelta`，提供只读 snapshot、按 id/tags/mode 过滤和 delta apply fail-closed 语义 | `CorpusRouter` 已具备稳定的 route metadata supporting header，不再需要临时 descriptor 容器 |
| `knowledge/include/health/FreshnessController.h`、`knowledge/src/health/FreshnessController.cpp` | 已落盘 `IndexManifest` 视图、`FreshnessSnapshot` 与 `FreshnessController::evaluate()`，固定 fresh/stale/unknown 判定与 stale dual gate | `CorpusRouter`、`Reranker` 与 `KnowledgeHealthProbe` 已具备稳定的新鲜度 supporting header |
| `knowledge/include/health/KnowledgeHealthProbe.h`、`knowledge/src/health/KnowledgeHealthProbe.cpp` | 已落盘 `HealthProbeDeps` 与 `KnowledgeHealthProbe::collect()`，提供 lifecycle/manifest/freshness/vector/telemetry 聚合、reason code 去重与 `HealthState` 分类 | 012/029/032 已具备稳定的 health snapshot supporting layer，不再需要在 facade 或 profile gate 内重复拼装健康语义 |
| `tests/unit/knowledge/CMakeLists.txt` | 已注册 interface surface、config projection、telemetry、QueryNormalizer、CorpusRouter、Reranker、EvidenceAssembler、CorpusCatalog、FreshnessController 与 KnowledgeHealthProbe unit targets | knowledge unit discoverability 与 public surface / query normalization / route planning / rerank / evidence assembly / catalog snapshot / freshness gate / health snapshot 已建立 |
| `tests/unit/CMakeLists.txt` | 已 `add_subdirectory(knowledge)` 并聚合 `${DASALL_KNOWLEDGE_UNIT_TEST_EXECUTABLE_TARGETS}` | knowledge unit 拓扑挂点与顶层执行链已建立 |
| `tests/integration/CMakeLists.txt` | 已接入 `knowledge` 子目录，并聚合 `${DASALL_KNOWLEDGE_INTEGRATION_TEST_EXECUTABLE_TARGETS}` | knowledge integration discoverability 已建立 |
| `tests/integration/knowledge/CMakeLists.txt` | 已注册 `KnowledgeIntegrationTopologySmokeTest` 并导出 knowledge integration targets | 006/025 之后的 smoke / observability / failure integration 已有稳定挂点 |
| `CMakeLists.txt` | 顶层已 `add_subdirectory(knowledge)` | 不需要新增顶层模块注册，只需补知识侧 include/src/tests 落点 |
| `docs/todos/knowledge/` | 已存在并持续沉淀 deliverables | knowledge 的设计收敛、执行状态与验收证据已具备固定回写落点 |

---

## 4. 粒度可行性评估

### 4.1 总体结论

结论：Knowledge 当前可直接生成 L3/L2 混合专项 TODO，但不能整体按“无 blocker 的纯 L3”推进。

当前最细可安全落盘粒度：

1. L3：`KnowledgeQuery`、`EvidenceSlice`、`EvidenceBundle`、`KnowledgeRetrieveResult`、`KnowledgeErrorCode`、`IKnowledgeService`、`KnowledgeConfigProjector`、`QueryNormalizer`、`CorpusRouter`、`Reranker`、`EvidenceAssembler`、`FreshnessController`。
2. L2：`KnowledgeServiceFacade`、`RecallCoordinator`、`SparseRetriever`、`VectorRetrieverBridge`、`CorpusCatalog`、`VersionLedger`、`IndexReader`、`IndexWriter`、`SourceScanner`、`Canonicalizer`、`Chunker`、`IngestionCoordinator`、`KnowledgeTelemetry`、`KnowledgeHealthProbe`。
3. 当前无额外设计 Blocked 项；剩余约束仅表现为任务前置依赖，而不是语义未定。
4. L0：四项补设计前置任务 001/002/003/004 已全部完成。

判断依据：

1. 详设 6.6、6.10、6.13 已给出明确的接口名、字段、错误语义、主流程、异常流程、目录建议和测试出口。
2. 详设 7、8、9 已给出 Design -> Build 映射、里程碑、测试矩阵和质量门，足以支撑接口级、数据结构级和组件级任务拆分。
3. 当前主要约束已从设计缺口转为 Build 前置顺序与基线资产落盘，而不再是 lexical/vector/corpus/quality 语义不清晰。
4. 因此专项 TODO 已可整体进入执行；quality regression Gate 仍需等待 027/028/030 的实现顺序，但不再受设计 blocker 约束。

### 4.2 可落盘对象提取表（Step 2 输出）

| 类别 | 可落盘对象 | 设计锚点 | 建议落位 | 当前状态 |
|---|---|---|---|---|
| Public ABI | `IKnowledgeService`、`KnowledgeTypes`、`KnowledgeErrors` | 6.5、6.6、7 KNO-D01 | `knowledge/include/` | 005/006 已落盘；interface surface unit gate 已建立 |
| 配置投影 | `KnowledgeConfigProjector`、`KnowledgeConfigSnapshot` | 6.10、6.13.6 | `knowledge/src/config/` | 006/007 已落盘；projector 采用 `RuntimePolicySnapshot + BuildProfileManifest + overlay` 单一入口 |
| Query Plane | `QueryNormalizer`、`CorpusRouter` | 6.13.1、7 KNO-D02 | `knowledge/include/query/`、`knowledge/src/query/` | `QueryNormalizer` 与 `CorpusRouter` 已落盘 |
| Recall / Rerank / Evidence | `RecallCoordinator`、`SparseRetriever`、`VectorRetrieverBridge`、`Reranker`、`EvidenceAssembler` | 6.13.2、7 KNO-D02~D04 | `knowledge/include/retrieve/`、`knowledge/src/retrieve/`、`knowledge/include/rerank/`、`knowledge/include/evidence/` | `RecallHit` / `RecallCandidateSet` supporting shape、`Reranker` 与 `EvidenceAssembler` 已落盘；其余组件未落盘 |
| Catalog / Freshness | `CorpusCatalog`、`FreshnessController` | 6.10、6.13.3、6.13.6 | `knowledge/include/index/`、`knowledge/include/health/` | `CorpusCatalog` 与 `FreshnessController` 已落盘，并分别具备 snapshot/delta apply 与 stale policy unit gate |
| Index Plane | `VersionLedger`、`IndexReader`、`IndexWriter` | 6.9、6.13.3、10.4 | `knowledge/include/index/`、`knowledge/src/index/` | 未落盘 |
| Ingest Plane | `SourceScanner`、`Canonicalizer`、`Chunker`、`IngestionCoordinator` | 6.9、6.13.3、6.13.5 | `knowledge/include/ingest/`、`knowledge/src/ingest/` | 未落盘 |
| Observability / Health | `KnowledgeTelemetry`、`KnowledgeHealthProbe` | 6.11、6.13.4、7 KNO-D06 | `knowledge/include/health/`、`knowledge/src/observability/`、`knowledge/src/health/` | `KnowledgeTelemetry` 与 `KnowledgeHealthProbe` 已落盘，并分别具备 failure accounting 与 health state 聚合 / dependency-missing guard |
| Unit 测试出口 | `KnowledgeServiceFacade*Test`、`QueryNormalizer*Test`、`CorpusRouter*Test`、`SparseRetriever*Test`、`VectorRetrieverBridge*Test`、`Reranker*Test`、`EvidenceAssembler*Test`、`Index*Test`、`IngestionCoordinator*Test`、`SourceScanner*Test`、`Canonicalizer*Test`、`Chunker*Test`、`KnowledgeTelemetry*Test`、`KnowledgeHealthProbe*Test` | 6.13、9.1 | `tests/unit/knowledge/` | telemetry 三条 unit gate、QueryNormalizer 两条 unit gate、CorpusRouter 三条 unit gate、Reranker 三条 unit gate、EvidenceAssembler 三条 unit gate、CorpusCatalog 三条 unit gate、FreshnessController 两条 unit gate与 KnowledgeHealthProbe 三条 unit gate已落盘；其余核心组件测试仍待实现 |
| Integration 测试出口 | `dasall_knowledge_retrieval_smoke_integration_test`、`dasall_knowledge_failure_degrade_integration_test`、`dasall_knowledge_profile_compatibility_integration_test` | 7 KNO-D07、9.1、9.3 | `tests/integration/knowledge/` | 目录不存在 |
| 质量资产 | golden set、`MRR@10` / `NDCG@10` / `Recall@5` / `Recall@10` baseline、`hard_fail` case 规则、context-level 扩展槽位 | 9.1、12.2 | `tests/integration/knowledge/golden/retrieval_quality_v1.yaml` | schema 已冻结；Build 资产待 KNO-TODO-030 落盘 |
| CMake / 注册点 | `knowledge/CMakeLists.txt`、`tests/unit/knowledge/CMakeLists.txt`、`tests/integration/knowledge/CMakeLists.txt`、`tests/integration/CMakeLists.txt` | 8.1、9.3 | 现有 CMake 拓扑 | knowledge 库 target、QueryNormalizer unit targets、CorpusRouter unit targets、Reranker unit targets、EvidenceAssembler unit targets、CorpusCatalog unit targets、FreshnessController unit targets 与 KnowledgeHealthProbe unit targets 已接入 |

### 4.3 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| `IKnowledgeService` + public surface | 6.5、6.6、7 KNO-D01 | L3 | 接口、返回语义、health/refresh 入口明确 | 无实质缺口 | 直接拆成 ABI 冻结任务 |
| `KnowledgeConfigProjector` | 6.10、6.13.6 | L3 | 配置来源、派生规则、profile 行为明确 | 无实质缺口 | 直接拆配置投影任务 |
| `KnowledgeServiceFacade` | 6.13.1 | L3 | deps 注入、生命周期、内部方法、deadline 传播与失败语义明确 | 依赖组件需先落盘 | 作为单独编排任务，依赖下游组件完成 |
| `QueryNormalizer` | 6.13.1 | L3 | 数据结构、步骤、失败语义、测试出口明确 | 无实质缺口 | 直接拆实现任务 |
| `CorpusRouter` | 6.13.1、6.10 | L3 | `RetrievalPlan` 字段、freshness / mode 选择规则、错误语义明确 | 需要 `CorpusCatalogSnapshot` 与 `FreshnessSnapshot` 头文件 | 先补 supporting headers，再落实现 |
| `RecallCoordinator` | 6.13.2 | L2 | lane 执行策略、超时、degraded 规则明确 | 依赖 lexical/vector lane 实现 | 先 lexical，再补 vector，再闭环 coordinator |
| `SparseRetriever` | 6.13.2、11.1 KNO-B03 | L2 | 接口、过滤、sentence-window、测试出口明确 | lexical index 技术栈已冻结 | 可直接进入 Build |
| `VectorRetrieverBridge` | 6.13.2、11.1 KNO-B02 | L2 | `IQueryEncoder`、`IVectorRecallStore`、backend health、lane 失败语义明确 | owner 与注入方向已冻结；待 port 实现与 adapter 接线 | 可直接进入 Build |
| `Reranker` | 6.13.2 | L3 | RRF 公式、参数、freshness penalty、测试出口明确 | 无实质缺口 | 直接拆实现任务 |
| `EvidenceAssembler` | 6.13.2 | L3 | budget 协调、confidence 公式、projection 规则、错误语义明确 | golden set 尚未建立，不影响实现 | 直接拆实现任务 |
| `CorpusCatalog` | 6.13.6、8.1 | L2 | schema、只读 snapshot 接口、delta apply 语义明确；首批 corpus baseline、glob/format/authority 字段已冻结 | 无阻塞性设计缺口 | 可直接实现容器与 snapshot，并承载 route + ingest 的共享视图 |
| `FreshnessController` | 6.10、6.13.3 | L3 | 输入输出、reason codes、纯计算边界明确 | 无实质缺口 | 直接拆实现任务 |
| `VersionLedger` | 6.13.4、10.4 | L2 | append-only 账本、candidate/active/superseded 状态流明确 | 持久化形式未在代码层定型 | 先做本地账本实现，再与 snapshot 绑定 |
| `IndexReader` | 6.13.3 | L2 | active snapshot、lock-free 读语义、manifest 查询明确 | lexical snapshot 实现依赖技术选型 | 选型冻结后实现 search/read path |
| `IndexWriter` | 6.13.3、10.4 | L2 | shadow build、swap、rollback、事务边界明确 | lexical snapshot 实现依赖技术选型 | 选型冻结后实现写路径 |
| `SourceScanner` | 6.13.5、11.1 KNO-B04 | L2 | source delta、quarantine、SHA-256 hash/version/update diff、full scan 语义明确；首批 source trust/metadata baseline 已冻结 | 无阻塞性设计缺口 | 可直接进入 Build |
| `Canonicalizer` | 6.13.5、11.1 KNO-B04 | L2 | `CanonicalDocument`、markdown/yaml canonicalize、typed metadata/fallback 语义明确 | 无阻塞性设计缺口 | 可直接进入 Build |
| `Chunker` | 6.13.5、11.1 KNO-B04 | L2 | `ChunkRecord`、stable id、provenance 继承、切分策略、测试出口明确 | 无阻塞性设计缺口 | 可直接进入 Build |
| `IngestionCoordinator` | 6.13.3、6.13.5 | L2 | `CorpusChangeSet`、`IndexUpdateBatch`、流程与失败语义明确；scanner/canonicalizer/chunker 输入基线已冻结 | 无阻塞性设计缺口 | 在 021/022/023 落盘后直接做编排 |
| `KnowledgeTelemetry` | 6.11、6.13.4 | L2 | 事件字段、四类关键路径、drop 规则明确 | 需与现有 infra sink 对齐 | 直接拆实现任务 |
| `KnowledgeHealthProbe` | 6.13.4 | L2 | 依赖源、状态分类、只读聚合边界明确 | 依赖 manifest/ledger/freshness/telemetry 基础 | 在 supporting components 后实施 |
| retrieval quality regression | 9.1、12.2 | L2 | YAML manifest、source-level 指标、coverage floor、`hard_fail` 规则与 context-level 槽位已冻结 | golden 资产与 test harness 尚未落盘 | 先完成 027/028 与 golden 资产，再注册 030 Gate |

---

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| lexical index 选型缺口 | 11.1 KNO-B03、12.1 | 补设计 / PoC | KNO-TODO-001 | 先确定 `SparseRetriever` / `IndexReader` / `IndexWriter` 的技术底座 |
| vector bridge ownership 缺口 | 11.1 KNO-B02、12.1 | 补设计 / 边界冻结 | KNO-TODO-002 | hybrid recall 前置边界任务 |
| corpus asset / metadata / trust baseline 缺口 | 6.9、11.1 KNO-B04、12.1 | 补设计 / 资产规范 | KNO-TODO-003 | ingest/index 任务前置 |
| golden set / 质量阈值缺口 | 9.1、12.2 | 补设计 / 质量资产 | KNO-TODO-004 | quality regression Gate 前置 |
| public surface 与工程骨架 | 7 KNO-D01、8.1 | 目录 / CMake / 测试拓扑 | KNO-TODO-005、006 | 先解 include 与 discoverability |
| 配置投影 | 6.10、6.13.6 | 配置与 Profile 裁剪 | KNO-TODO-007 | 保证不新增 profile schema v1 顶层域 |
| query 归一化与路由 | 6.13.1、7 KNO-D02 | 组件实现 | KNO-TODO-008、009 | lexical/hybrid 主链共同依赖 |
| rerank 与 evidence 投影 | 6.13.2、7 KNO-D04 | 数据处理 / 输出投影 | KNO-TODO-010、011 | 收敛到 `EvidenceBundle` 与 `context_projection` |
| facade 编排与 refresh 入口 | 6.13.1、6.6 | 生命周期 / 组合根 | KNO-TODO-012 | Runtime-facing 唯一同步读入口与异步刷新入口 |
| lexical recall 最小链 | 6.13.2、7 KNO-D02 | 检索执行 | KNO-TODO-013、014 | 先 lexical-only，再补 lane 协调 |
| hybrid/vector 增强 | 6.13.2、7 KNO-D03 | 适配器 / 协调 | KNO-TODO-015、014 | vector lane 与 degrade 闭环 |
| catalog / freshness / ledger | 6.10、6.13.3、6.13.4 | 配置/元数据/健康支撑 | KNO-TODO-016、017、018 | query/index 共享支撑层 |
| index read/write | 6.13.3、10.4 | 生命周期 / snapshot 协议 | KNO-TODO-019、020 | 严格按 read/write 拆分 |
| ingest pipeline | 6.9、6.13.3、6.13.5 | 编排 / 数据准备 | KNO-TODO-021、022、023、024 | scanner/canonicalizer/chunker 分离后再编排 |
| observability / health | 6.11、6.13.4、7 KNO-D06 | 观测 / 诊断 | KNO-TODO-025、026 | 关键字段、degrade 计数与 health 状态收口 |
| smoke / failure / profile / quality Gate | 7 KNO-D07、9.1、9.3 | integration / regression / Gate | KNO-TODO-027、028、029、030 | 至少一条 smoke + degrade + profile + quality 证据链 |
| TODO / deliverables / worklog 回写 | 8.2 Phase K5、9.3 | 文档 / 交付证据 | KNO-TODO-031 | 显式证据回写与残余风险记录 |
| Facade 完整编排（评审补充） | 6.6、6.10、6.13.1；评审 D-1 | 生命周期 / 组合根 | KNO-TODO-032 | 骨架 012 升级为真实组件编排 |
| refresh 端到端集成闭环（评审补充） | 6.6、6.9、10.4；评审 D-4 | integration / 端到端 | KNO-TODO-033 | 验证 refresh → ingest → swap → retrieve 闭环 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 任务 ID |
|---|---|---|
| 接口定义类任务 | 是 | KNO-TODO-005、006 |
| 数据结构定义类任务 | 是 | KNO-TODO-006 |
| 生命周期与初始化类任务 | 是 | KNO-TODO-007、012、018、019、020 |
| 适配器 / 桥接类任务 | 是 | KNO-TODO-002、015 |
| 异常与错误处理类任务 | 是 | KNO-TODO-006、011、012、025、026、028 |
| 配置与 Profile 裁剪类任务 | 是 | KNO-TODO-007、017、029 |
| 测试与门禁类任务 | 是 | KNO-TODO-005、027、028、029、030 |
| 文档 / 交付证据回写类任务 | 是 | KNO-TODO-001、002、003、004、031 |
| Facade 拆分 / 分阶段编排类任务 | 是 | KNO-TODO-012、032 |
| 端到端集成闭环类任务 | 是 | KNO-TODO-027、028、029、033 |

---

## 6. 原子任务清单

说明：除文档一致性任务使用 `rg` 检索外，本章验收命令统一以 `cmake -S . -B build-ci -G "Unix Makefiles" &&` 作为配置前缀。若后续确认本地 `build-ci` 使用 Ninja 且未污染，可切换为 Ninja；若 CMake Tools 预设状态异常，则以显式 `cmake --build` / `ctest --test-dir build-ci` 为准。

### 6.1 前置补设计任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| KNO-TODO-001 | Done | 收敛 lexical 索引技术选型与 PoC 证据 | 详设 6.13.2、10.4、11.1 KNO-B03、12.1 | `SparseRetriever`；`IndexReader`；`IndexWriter` | L0 | 已更新 `docs/architecture/DASALL_knowledge子系统详细设计.md` 与本专项 TODO，明确 lexical index 唯一实现为 SQLite FTS5、共享 sqlite target 接入方向、cross-compile 约束和最小 PoC 结论；**评审补充**：已补 edge_balanced 10k chunks BM25 host-side p99 延迟基准（作为 QG-K04 前置数据） | `SparseRetriever`；lexical snapshot；`IndexManifest.format_version`；`IndexManifest.tokenizer_profile` | 文档一致性：单一技术选型、回退条件和格式版本约束可检索；host-side p99 延迟基准存在；mixed-language tokenizer 证据已回写 | `rg -n "SQLite FTS5|tokenizer_profile|format_version|KNO-BLK-001|0.7640ms|5.7153ms" docs/architecture/DASALL_knowledge子系统详细设计.md docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md docs/todos/knowledge/deliverables/KNO-TODO-001-lexical索引技术选型与PoC设计收敛.md` | 无 | KNO-BLK-001 | 形成唯一 lexical 技术路线并补 PoC 证据 | 更新后的 knowledge 详设；更新后的专项 TODO；`docs/todos/knowledge/deliverables/KNO-TODO-001-lexical索引技术选型与PoC设计收敛.md` | 仅当 lexical index 唯一收敛为 SQLite FTS5、`SparseRetriever` / `IndexReader` / `IndexWriter` 不再存在多候选歧义、`format_version` / `tokenizer_profile` / host-side PoC 证据可回链时完成 |
| KNO-TODO-002 | Done | 冻结 vector bridge 窄接口与 ownership | 详设 6.13.2、11.1 KNO-B02、12.1；阶段 H 与 memory 边界 | `VectorRetrieverBridge`；`IQueryEncoder`；`IVectorRecallStore` | L0 | 已更新 `docs/architecture/DASALL_knowledge子系统详细设计.md` 与本专项 TODO，明确 `IQueryEncoder` / `IVectorRecallStore` 归 Knowledge module-local owner、注入方向为 composition root -> Knowledge、并锁定 dense lane degrade 语义 | `VectorRetrieverBridge`；`IQueryEncoder`；`IVectorRecallStore`；`DenseQueryInputMode` | 文档一致性：窄接口归属、注入方向、`DenseQueryInputMode` 和 degrade 语义可检索 | `rg -n "IQueryEncoder|IVectorRecallStore|DenseQueryInputMode|VectorRetrieverBridge|VectorBackendUnavailable|memory_vector=false" docs/architecture/DASALL_knowledge子系统详细设计.md docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md docs/todos/knowledge/deliverables/KNO-TODO-002-vector-bridge窄接口与ownership设计收敛.md` | 无 | KNO-BLK-002 | 形成单一 ownership 结论，并锁定 bridge 输入输出语义 | 更新后的 knowledge 详设；更新后的专项 TODO；`docs/todos/knowledge/deliverables/KNO-TODO-002-vector-bridge窄接口与ownership设计收敛.md` | 仅当 `IQueryEncoder` / `IVectorRecallStore` owner 唯一收敛为 Knowledge module-local、注入方向固定、dense lane degrade 语义不再依赖未定义 owner 时完成 |
| KNO-TODO-003 | Done | 补齐首批 corpus 资产与 metadata/trust 规范 | 详设 6.9、6.13.5、11.1 KNO-B04、12.1 | `SourceScanner`；`Canonicalizer`；`Chunker`；`CorpusCatalog` | L0 | 已更新 `docs/architecture/DASALL_knowledge子系统详细设计.md` 与本专项 TODO，冻结首批 corpus baseline（architecture / ADR / SSOT / profile runtime policy snapshots）、`AuthorityLevel` / `SourceFormat` / `CorpusScanPlan`、typed provenance 字段与 quarantine 条件 | `CorpusDescriptor`；`SourceRecord`；`CanonicalDocument`；`ChunkRecord`；`CorpusScanPlan` | 文档一致性：corpus baseline、typed metadata 字段、trust/quarantine 规则与 profile snapshot canonicalize 规则可检索 | `rg -n "AuthorityLevel|SourceFormat|CorpusScanPlan|architecture_reference|profile_policy_normative|quarantine|updated_at_ms|source_format" docs/architecture/DASALL_knowledge子系统详细设计.md docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md docs/todos/knowledge/deliverables/KNO-TODO-003-corpus资产与metadata-trust基线设计收敛.md` | 无 | KNO-BLK-003 | 形成最小 corpus baseline 与 source trust SSOT | 更新后的 knowledge 详设；更新后的专项 TODO；`docs/todos/knowledge/deliverables/KNO-TODO-003-corpus资产与metadata-trust基线设计收敛.md` | 仅当 ingest/index 相关组件拥有明确的输入资产、metadata 必填字段与 trust 规则，且坏源隔离条件明确时完成 |
| KNO-TODO-004 | Done | 定义 retrieval quality golden set 与回归阈值 | 详设 9.1、12.2 | `RetrievalQualityRegressionTest`；golden set；`MRR@10` / `NDCG@10` / `Recall@5` / `Recall@10` | L0 | 已更新 `docs/architecture/DASALL_knowledge子系统详细设计.md` 与本专项 TODO，冻结 `tests/integration/knowledge/golden/retrieval_quality_v1.yaml` manifest、样本覆盖下限、source-level 指标阈值、`hard_fail` case 规则与 context-level 扩展槽位 | golden set manifest schema；`expected_source_uris`；`baseline_metrics`；context metric slots | 文档一致性：quality gate 样本格式、绝对阈值、回归公式和 context-level 扩展槽位可检索 | `rg -n "retrieval_quality_v1.yaml|expected_source_uris|min_mrr_at_10|min_ndcg_at_10|min_recall_at_10|context_metric_slots|hard_fail|KNO-BLK-004" docs/architecture/DASALL_knowledge子系统详细设计.md docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md docs/todos/knowledge/deliverables/KNO-TODO-004-retrieval-quality-golden-set与回归阈值设计收敛.md` | 无 | KNO-BLK-004 | 形成 retrieval quality baseline 定义 | 更新后的 knowledge 详设；更新后的专项 TODO；`docs/todos/knowledge/deliverables/KNO-TODO-004-retrieval-quality-golden-set与回归阈值设计收敛.md` | 仅当 regression gate 的输入格式、样本覆盖下限、绝对阈值、relative regression 公式和 `hard_fail` 规则都可二值执行时完成 |

### 6.2 公共骨架与 ABI 任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| KNO-TODO-005 | Done | 新增 knowledge 公共 include 与测试/CMake 骨架 | 详设 7 KNO-D01、8.1、9.3；当前代码现状 | 7 KNO-D01；8.1 目录建议；9.3 QG-K03 | L2 | 建立 `knowledge/include/`；重写 `knowledge/CMakeLists.txt` 的源文件布局；替换 `tests/unit/knowledge/CMakeLists.txt` 占位；新增 `tests/integration/knowledge/CMakeLists.txt` 并接入 `tests/integration/CMakeLists.txt` | `dasall_knowledge` 构建骨架；knowledge unit/integration 注册点 | discoverability：`ctest -N` 可发现至少 1 个 knowledge unit 和 1 个 knowledge integration 入口 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -N` | 无 | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-005-knowledge公共include与测试CMake骨架设计收敛.md`；`knowledge/CMakeLists.txt`；`knowledge/src/KnowledgeBuildSkeleton.cpp`；`knowledge/include/KnowledgeTypes.h`；`knowledge/include/KnowledgeErrors.h`；`knowledge/include/IKnowledgeService.h`；`tests/unit/knowledge/CMakeLists.txt`；`tests/unit/knowledge/KnowledgeInterfaceSurfaceSkeletonTest.cpp`；`tests/integration/knowledge/CMakeLists.txt`；`tests/integration/knowledge/KnowledgeIntegrationTopologySmokeTest.cpp`；更新后的 `tests/unit/CMakeLists.txt`、`tests/integration/CMakeLists.txt`；2026-04-21 已通过 build 与 `ctest -N` discoverability | 仅当 `dasall_knowledge` 不再依赖单一 placeholder 布局，且 `ctest -N` 能发现 knowledge 的 unit/integration 挂点时完成 |
| KNO-TODO-006 | Done | 定义 Knowledge public surface 对象、错误映射与 IKnowledgeService | 详设 6.5、6.6、7 KNO-D01；蓝图 6；规范 3.2/3.6/3.7 | 6.5/6.6 `KnowledgeQuery`、`EvidenceBundle`、`KnowledgeRetrieveResult`、`IKnowledgeService` | L3 | 更新 `knowledge/include/KnowledgeTypes.h`、`knowledge/include/KnowledgeErrors.h`、`knowledge/include/IKnowledgeService.h`，冻结 `KnowledgeQueryKind`（含 `MultiHop` 枚举值，**v1 仅声明不落执行链路**）、`RetrievalMode`、`FreshnessState`、`TrustLevel`、`KnowledgeQuery`（含 `latest_observation_digest_summary` / `belief_state_summary` 预留字段，**v1 标记 not-consumed**）、`EvidenceSlice`、`EvidenceBundle`、`CorpusDescriptor`、`KnowledgeConfigSnapshot`、`KnowledgeRetrieveResult`、`CorpusChangeSet`、`RefreshResult` 与 `KnowledgeErrorCode` 到 `ErrorInfo` 的映射面 | `IKnowledgeService::init/retrieve/health_snapshot/request_refresh`；public supporting types；错误码映射 | unit：`dasall_knowledge_interface_surface_unit_test` 覆盖 ABI 可见性、错误语义与 `ErrorInfo` 投影 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_knowledge_interface_surface_unit_test && ctest --test-dir build-ci -R dasall_knowledge_interface_surface_unit_test --output-on-failure` | KNO-TODO-005 | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-006-Knowledge-public-surface与错误映射设计收敛.md`；`knowledge/include/KnowledgeTypes.h`；`knowledge/include/KnowledgeErrors.h`；`knowledge/include/IKnowledgeService.h`；`knowledge/src/KnowledgeBuildSkeleton.cpp`；`tests/unit/knowledge/KnowledgeInterfaceSurfaceSkeletonTest.cpp`；2026-04-21 已通过 build 与 `dasall_knowledge_interface_surface_unit_test` | 仅当 public ABI、错误映射和 Runtime-facing 接口签名稳定可编译，且不把 supporting types 推入 shared contracts 时完成 |
| KNO-TODO-007 | Done | 实现 KnowledgeConfigProjector 配置投影 | 详设 6.10、6.13.6、9.1；蓝图 5.1 | 6.10 `KnowledgeConfigSnapshot`；6.13.6 `KnowledgeConfigProjector` | L3 | 新增 `knowledge/src/config/KnowledgeConfigProjector.cpp/.h`，落盘 `knowledge_enabled`、`vector_enabled`、`retrieval_mode_default`、`evidence_budget_tokens`、`catalog_refresh_interval_ms`、`request_deadline_ms`、`max_parallel_recall` 等派生规则，并把 enabled 状态 owner 固定为 `BuildProfileManifest` | `KnowledgeConfigProjector::project`；`KnowledgeConfigSnapshot` | unit：`KnowledgeConfigProjectionTest` 覆盖 profile 派生、override merge 和 `knowledge=true && memory_vector=false` 兼容性 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_knowledge_config_projection_unit_test dasall_knowledge_interface_surface_unit_test && ctest --test-dir build-ci -R "KnowledgeConfigProjectionTest|dasall_knowledge_interface_surface_unit_test" --output-on-failure` | KNO-TODO-005、006 | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-007-KnowledgeConfigProjector配置投影设计收敛.md`；`knowledge/src/config/KnowledgeConfigProjector.h`；`knowledge/src/config/KnowledgeConfigProjector.cpp`；`tests/unit/knowledge/KnowledgeConfigProjectionTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 build 与 `KnowledgeConfigProjectionTest` | 仅当 Knowledge 不再自建平行配置系统，且 profile/deployment/runtime override 的优先级与派生规则可自动验证时完成 |

### 6.3 Query / Evidence 主链任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| KNO-TODO-008 | Done | 实现 QueryNormalizer | 详设 6.13.1、7 KNO-D02 | `QueryNormalizer`；`NormalizeResult`；`NormalizedQuery` | L3 | 新增 `knowledge/include/query/QueryNormalizer.h`、`knowledge/src/query/QueryNormalizer.cpp`，落盘 query text 规范化、tag/corpus 去重、top-k 边界裁剪和 warning 语义 | `QueryNormalizer::normalize`；`canonicalize_text`；`derive_lexical_terms` | unit：`QueryNormalizerTest`、`QueryNormalizerBoundaryTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests && ctest --test-dir build-ci -R "QueryNormalizer.*Test" --output-on-failure` | KNO-TODO-006、007 | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-008-QueryNormalizer设计收敛.md`；`knowledge/include/query/QueryNormalizer.h`；`knowledge/src/query/QueryNormalizer.cpp`；`knowledge/include/KnowledgeErrors.h`；`tests/unit/knowledge/QueryNormalizerTest.cpp`；`tests/unit/knowledge/QueryNormalizerBoundaryTest.cpp`；`tests/unit/knowledge/KnowledgeInterfaceSurfaceSkeletonTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools、显式 `ctest`、build-ci 定向验收 | 仅当空 query、超长 query、tag 归一与 warning 语义都可二值判定，且不引入 llm rewrite 依赖时完成 |
| KNO-TODO-009 | Done | 实现 CorpusRouter | 详设 6.10、6.13.1、7 KNO-D02 | `CorpusRouter`；`RetrievalPlan` | L3 | 新增 `knowledge/include/query/CorpusRouter.h`、`knowledge/src/query/CorpusRouter.cpp`，落盘 corpus 过滤、mode 选择、route reason codes 和 stale/read 准入规则 | `CorpusRouter::build_plan`；`select_mode`；`RetrievalPlan` | unit：`CorpusRouterTest`、`CorpusRouterFreshnessPolicyTest`、`CorpusRouterModeSelectionTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests && ctest --test-dir build-ci -R "CorpusRouter.*Test" --output-on-failure` | KNO-TODO-006、007、016、017 | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-009-CorpusRouter设计收敛.md`；`knowledge/include/query/CorpusRouter.h`；`knowledge/src/query/CorpusRouter.cpp`；`tests/unit/knowledge/CorpusRouterTest.cpp`；`tests/unit/knowledge/CorpusRouterFreshnessPolicyTest.cpp`；`tests/unit/knowledge/CorpusRouterModeSelectionTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools、显式 `ctest`、build-ci 定向验收 | 仅当 Router 不再默认扫全库、能对 stale/vector/legal combinations 做显式选择，并把 route reason codes 暴露给上游时完成 |
| KNO-TODO-010 | Done | 实现 Reranker | 详设 6.13.2、7 KNO-D04 | `Reranker`；`RankedHitSet`；RRF 公式 | L3 | 新增 `knowledge/include/rerank/Reranker.h`、`knowledge/src/rerank/Reranker.cpp`，落盘去重、RRF（**v1 默认融合策略，接口预留 relative score fusion 扩展槽位**）、freshness penalty、authority weighting 和 top-k 截断 | `Reranker::rerank`；`RankedHit`；`RankedHitSet` | unit：`RerankerTest`、`HybridRrfMergeTest`、`RerankerFreshnessPenaltyTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests && ctest --test-dir build-ci -R "(Reranker|HybridRrfMerge).*Test" --output-on-failure` | KNO-TODO-006、017 | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-010-Reranker设计收敛.md`；`knowledge/include/retrieve/RecallTypes.h`；`knowledge/include/rerank/Reranker.h`；`knowledge/src/rerank/Reranker.cpp`；`tests/unit/knowledge/RerankerTest.cpp`；`tests/unit/knowledge/HybridRrfMergeTest.cpp`；`tests/unit/knowledge/RerankerFreshnessPenaltyTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools、显式 `ctest`、build-ci 定向验收 | 仅当 RRF 融合、penalty/boost 和空结果语义都可稳定测试，且不在组件内发起 I/O 时完成 |
| KNO-TODO-011 | Done | 实现 EvidenceAssembler 与 ContextProjectionMapper | 详设 6.13.2、7 KNO-D04 | `EvidenceAssembler`；`EvidenceBundle`；`context_projection` | L3 | 新增 `knowledge/include/evidence/EvidenceAssembler.h`、`knowledge/src/evidence/EvidenceAssembler.cpp`，落盘 `EvidenceSlice` 构造、budget clamp、projection line 生成、`omitted_sources` 和 `evidence_insufficient` 语义；**评审补充**：token 估算采用 `chars/4` 近似，CJK 文本精度约 70%，需在 budget clamp 中预留 10% 安全余量；`EvidenceSlice` 需携带 `freshness_state` 字段（stale 时显式标记，避免过时证据无法追踪） | `EvidenceAssembler::assemble`；`build_slice`；`build_projection_line` | unit：`EvidenceAssemblerTest`、`ContextProjectionMapperTest`、`EvidenceBudgetClampTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests && ctest --test-dir build-ci -R "(EvidenceAssembler|ContextProjectionMapper|EvidenceBudgetClamp).*Test" --output-on-failure` | KNO-TODO-006、007、010 | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-011-EvidenceAssembler设计收敛.md`；`knowledge/include/evidence/EvidenceAssembler.h`；`knowledge/src/evidence/EvidenceAssembler.cpp`；`tests/unit/knowledge/EvidenceAssemblerTest.cpp`；`tests/unit/knowledge/ContextProjectionMapperTest.cpp`；`tests/unit/knowledge/EvidenceBudgetClampTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools 定向构建、显式 `ctest` 与 build-ci 定向验收；默认 `all` 目标仍受无关 Freshness 测试历史坏文件影响，未作为本轮主验收信号 | 仅当结构化 `slices`、`context_projection` 与 budget/degraded 语义可分离验证，且不直接写 `ContextPacket` 时完成 |
| KNO-TODO-012 | Done | 实现 KnowledgeServiceFacade lexical-only 骨架编排 | 详设 6.6、6.10、6.13.1、7 KNO-D01；评审修正：拆分 Facade 为骨架 + 完整两阶段 | `KnowledgeServiceFacade`；`compute_stage_budget`；`request_refresh` stub | L3 | 新增 `knowledge/src/facade/KnowledgeService.cpp` 与 module-local `knowledge/src/facade/KnowledgeService.h`，按 function-seam deps 组合根落盘 lifecycle、deadline 传播（normalize+route 5% / sparse 35% / dense 35% / rerank+evidence 15% / telemetry 10%）、fail-closed、disabled/not-initialized 错误路径、lexical-only retrieve 编排、基础 `health_snapshot` 与 refresh busy guard，占位隔离尚未接入的 RecallCoordinator / IndexReader / IngestionCoordinator / HealthProbe concrete owner | `KnowledgeServiceFacade::init`；`retrieve`（lexical-only 路径）；`health_snapshot`（基础版）；`request_refresh`（busy guard）；`compute_stage_budget` | unit：`KnowledgeServiceFacadeSmokeTest`、`KnowledgeServiceFacadeFailurePathTest`、`KnowledgeServiceFacadeDegradedModeTest`、`KnowledgeServiceFacadeDeadlineBudgetTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_knowledge_service_facade_smoke_unit_test dasall_knowledge_service_facade_failure_path_unit_test dasall_knowledge_service_facade_degraded_mode_unit_test dasall_knowledge_service_facade_deadline_budget_unit_test && ctest --test-dir build-ci -R "KnowledgeServiceFacade.*Test" --output-on-failure` | KNO-TODO-006、007、008、009、010、011、016、017、018、025 | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-012-KnowledgeServiceFacade骨架设计收敛.md`；`knowledge/src/facade/KnowledgeService.h`；`knowledge/src/facade/KnowledgeService.cpp`；`tests/unit/knowledge/KnowledgeServiceFacadeSmokeTest.cpp`；`tests/unit/knowledge/KnowledgeServiceFacadeFailurePathTest.cpp`；`tests/unit/knowledge/KnowledgeServiceFacadeDegradedModeTest.cpp`；`tests/unit/knowledge/KnowledgeServiceFacadeDeadlineBudgetTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools 定向构建；`RunCtest_CMakeTools` 工具态仍报 `生成失败` 后，已按仓库回退路径使用 build-ci `ctest` 4/4 验收通过 | 仅当 facade 骨架能完成 lifecycle 校验、stage deadline 传播、disabled/not-initialized/fail-closed 映射与 refresh busy guard，使用 stub seam 隔离未解阻的 recall/index/ingest/health 依赖，且不承担跨层恢复裁定时完成 |
| KNO-TODO-013 | Done | 实现 SparseRetriever lexical 召回 | 详设 6.13.2、7 KNO-D02、11.1 KNO-B03 | `SparseRetriever`；`RecallHit`；lexical search path | L2 | 新增 `knowledge/include/retrieve/SparseRetriever.h`、`knowledge/src/retrieve/SparseRetriever.cpp`，落盘 lexical query expression、metadata filter、language/trust filter 与 sentence-window 扩展；补充 `SparseRetrieveRequest` / `SparseRetrieverDeps` seam，避免在 013 内提前锁死 019 `IndexReader` active snapshot owner | `SparseRetriever::retrieve`；`build_query_expression`；`expand_sentence_window` | unit：`SparseRetrieverTest`、`SparseRetrieverFilterTest`、`SparseRetrieverSentenceWindowTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_sparse_retriever_unit_test dasall_sparse_retriever_filter_unit_test dasall_sparse_retriever_sentence_window_unit_test && ctest --test-dir build-ci -R "SparseRetriever.*Test" --output-on-failure` | KNO-TODO-001、005、006 | 已解除：KNO-BLK-001 | 已完成 KNO-TODO-001 | `docs/todos/knowledge/deliverables/KNO-TODO-013-SparseRetriever设计收敛.md`；`knowledge/include/retrieve/SparseRetriever.h`；`knowledge/src/retrieve/SparseRetriever.cpp`；`tests/unit/knowledge/SparseRetrieverTest.cpp`；`tests/unit/knowledge/SparseRetrieverFilterTest.cpp`；`tests/unit/knowledge/SparseRetrieverSentenceWindowTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；`memory/CMakeLists.txt`；2026-04-21 已通过 build-ci 定向构建与 `ctest` 3/3；本轮同时补齐共享 `dasall_sqlite3` 的 `SQLITE_ENABLE_FTS5=1`，修复真实 SQLite 夹具报错 `no such module: fts5` | 仅当 lexical index 技术栈已冻结，且 filter/0-hit/损坏索引三类路径都能二值验证时完成 |
| KNO-TODO-014 | Done | 实现 RecallCoordinator lane 编排 | 详设 6.10、6.13.2、7 KNO-D03 | `RecallCoordinator`；`RecallCandidateSet` | L2 | 新增 `knowledge/include/retrieve/RecallCoordinator.h`、`knowledge/src/retrieve/RecallCoordinator.cpp`，落盘 sparse/dense lane 调度、**v1 默认串行执行**（不开放并行度控制，为 v2 hybrid 并发预留 lane executor 接口）、partial results 和 degraded 标记；补充 `RecallRequest`、`RecallCoordinatorResult`，并在 2026-04-21 通过 `dense_bridge` 依赖补齐真实 `VectorRetrieverBridge` 接入，同时保留 `dense_lane` 回调作为 timeout/failure 注入兜底，供 028 继续验证退化路径 | `RecallCoordinator::recall`；`run_sparse_lane`；`run_dense_lane` | unit：`RecallCoordinatorTest`、`RecallCoordinatorDegradedTest`、`RecallCoordinatorSerialExecutionTest`、`RecallCoordinatorDenseBridgeTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_recall_coordinator_unit_test dasall_recall_coordinator_degraded_unit_test dasall_recall_coordinator_serial_execution_unit_test dasall_recall_coordinator_dense_bridge_unit_test dasall_knowledge_retrieval_smoke_integration_test && ctest --test-dir build-ci -R "(RecallCoordinator.*Test|dasall_knowledge_retrieval_smoke_integration_test)" --output-on-failure` | KNO-TODO-007、013、015 | 无 | 015 已落盘真实 dense bridge；timeout/failure 注入仍由 fallback seam 提供 | `docs/todos/knowledge/deliverables/KNO-TODO-014-RecallCoordinator设计收敛.md`；`docs/todos/knowledge/deliverables/KNO-TODO-014-dense-lane真实接入设计补充.md`；`knowledge/include/retrieve/RecallCoordinator.h`；`knowledge/src/retrieve/RecallCoordinator.cpp`；`tests/unit/knowledge/RecallCoordinatorTest.cpp`；`tests/unit/knowledge/RecallCoordinatorDegradedTest.cpp`；`tests/unit/knowledge/RecallCoordinatorSerialExecutionTest.cpp`；`tests/unit/knowledge/RecallCoordinatorDenseBridgeTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`tests/integration/knowledge/KnowledgeRetrievalSmokeTest.cpp`；2026-04-21 已通过 Build_CMakeTools 定向构建；`RunCtest_CMakeTools` 工具态报 `生成失败` 后，已按仓库回退路径使用 build-ci `ctest` 5/5 验收通过 | 仅当 coordinator 能优先消费真实 dense bridge、保留 timeout/failure 注入兜底、且不破坏 lexical-only smoke 主链时完成 |
| KNO-TODO-015 | Done | 实现 VectorRetrieverBridge 与 IQueryEncoder seam | 详设 6.13.2、7 KNO-D03、11.1 KNO-B02 | `VectorRetrieverBridge`；`IQueryEncoder`；`IVectorRecallStore` | L2 | 新增 `knowledge/include/retrieve/VectorRetrieverBridge.h`、`knowledge/include/retrieve/IQueryEncoder.h`、`knowledge/include/retrieve/IVectorRecallStore.h`、`knowledge/src/retrieve/VectorRetrieverBridge.cpp`，落盘 backend health 检查、`DenseQueryInputMode` 驱动的 dense query 构造与 lane degrade 语义，并将 `DenseRecallRequest` / `DenseRecallResult` owner 收敛到 dense bridge 语义域，供 014 dense lane 真实接入复用 | `VectorRetrieverBridge::retrieve`；`available`；`IQueryEncoder::encode`；`IVectorRecallStore::search` | unit：`VectorRetrieverBridgeTest`、`VectorRetrieverBridgeUnavailableTest`，并回归 `RecallCoordinator.*Test` 验证 dense 类型迁移未破坏既有编排基线 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_vector_retriever_bridge_unit_test dasall_vector_retriever_bridge_unavailable_unit_test dasall_recall_coordinator_unit_test dasall_recall_coordinator_degraded_unit_test dasall_recall_coordinator_serial_execution_unit_test && ctest --test-dir build-ci -R "(VectorRetrieverBridge|RecallCoordinator).*Test" --output-on-failure` | KNO-TODO-002、005、006 | 已解除：KNO-BLK-002 | 已完成 KNO-TODO-002 | `docs/todos/knowledge/deliverables/KNO-TODO-015-VectorRetrieverBridge设计收敛.md`；`knowledge/include/retrieve/IQueryEncoder.h`；`knowledge/include/retrieve/IVectorRecallStore.h`；`knowledge/include/retrieve/VectorRetrieverBridge.h`；`knowledge/src/retrieve/VectorRetrieverBridge.cpp`；`knowledge/include/retrieve/RecallCoordinator.h`；`knowledge/src/retrieve/RecallCoordinator.cpp`；`tests/unit/knowledge/VectorRetrieverBridgeTest.cpp`；`tests/unit/knowledge/VectorRetrieverBridgeUnavailableTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools 定向构建；`RunCtest_CMakeTools` 工具态仍报 `生成失败` 后，已按仓库回退路径使用 build-ci `ctest` 5/5 验收通过 | 仅当 bridge ownership 已冻结，且 backend unavailable / required encoder unavailable / text-only vs embedding-required 两类 dense 输入模式都能以稳定 lane result 表达，并且 dense 类型迁移未破坏 RecallCoordinator 基线时完成 |

### 6.4 Catalog / Freshness / Index 任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| KNO-TODO-016 | Done | 实现 CorpusCatalog route metadata snapshot | 详设 6.13.6、8.1、7 KNO-D02/KNO-D05 | `CorpusCatalog`；`CorpusCatalogSnapshot`；`CorpusDescriptor` | L2 | 新增 `knowledge/include/index/CorpusCatalog.h`、`knowledge/src/index/CorpusCatalog.cpp`，落盘只读 snapshot、按 id/tags/mode 过滤和 delta apply 失败保留上一 valid snapshot 的语义 | `CorpusCatalogSnapshot::list_all/find_by_id/filter_by_tags/filter_by_mode` | unit：`CorpusCatalogTest`、`CorpusCatalogDeltaApplyTest`、**`CorpusCatalogColdStartTest`**（验证空 catalog bootstrap 场景） | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests && ctest --test-dir build-ci -R "CorpusCatalog.*Test" --output-on-failure` | KNO-TODO-006 | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-016-CorpusCatalog-route-metadata-snapshot设计收敛.md`；`knowledge/include/index/CorpusCatalog.h`；`knowledge/src/index/CorpusCatalog.cpp`；`tests/unit/knowledge/CorpusCatalogTest.cpp`；`tests/unit/knowledge/CorpusCatalogDeltaApplyTest.cpp`；`tests/unit/knowledge/CorpusCatalogColdStartTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools、RunCtest_CMakeTools 与 build-ci 定向验收 | 仅当 catalog 支持只读 snapshot、delta apply 回滚和 trust/mode 过滤，不允许 query path 写入 catalog 时完成 |
| KNO-TODO-017 | Done | 实现 FreshnessController 新鲜度评估 | 详设 6.10、6.13.3、7 KNO-D05 | `FreshnessController`；`FreshnessSnapshot` | L3 | 新增 `knowledge/include/health/FreshnessController.h`、`knowledge/src/health/FreshnessController.cpp`，落盘 `age_ms`、`stale_read_allowed`、`rebuild_recommended` 和 `reason_codes` 计算；**评审补充**：新鲜度状态需通过 `FreshnessSnapshot` 传播至下游 `EvidenceSlice.freshness_state`，确保 stale 证据可被上层显式追踪 | `FreshnessController::evaluate`；`FreshnessSnapshot` | unit：`FreshnessControllerTest`、`FreshnessControllerStalePolicyTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests && ctest --test-dir build-ci -R "FreshnessController.*Test" --output-on-failure` | KNO-TODO-006、007 | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-017-FreshnessController新鲜度评估设计收敛.md`；`knowledge/include/health/FreshnessController.h`；`knowledge/src/health/FreshnessController.cpp`；`tests/unit/knowledge/FreshnessControllerTest.cpp`；`tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools、RunCtest_CMakeTools 与 build-ci 定向验收 | 仅当 manifest 缺失、stale reject、stale allowed 和 pure-compute 边界都能自动验证时完成 |
| KNO-TODO-018 | Done | 实现 VersionLedger snapshot 账本 | 详设 6.13.4、10.4、7 KNO-D05 | `VersionLedger`；`VersionLedgerEntry` | L2 | 新增 `knowledge/include/index/VersionLedger.h`、`knowledge/src/index/VersionLedger.cpp`，落盘 candidate/active/superseded 状态流转、`last_known_good()` 与 checksum 校验；补充 `VersionLedgerDeps::read_snapshot_checksum` seam，避免在 018 内抢跑 019/020 的 active snapshot 读写 owner | `record_candidate`；`mark_active`；`mark_superseded`；`last_known_good` | unit：`VersionLedgerTest`、`VersionLedgerActivationTest`、`VersionLedgerRollbackEligibilityTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_version_ledger_unit_test dasall_version_ledger_activation_unit_test dasall_version_ledger_rollback_eligibility_unit_test && ctest --test-dir build-ci -R "VersionLedger.*Test" --output-on-failure` | KNO-TODO-006 | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-018-VersionLedger设计收敛.md`；`knowledge/include/index/VersionLedger.h`；`knowledge/src/index/VersionLedger.cpp`；`tests/unit/knowledge/VersionLedgerTest.cpp`；`tests/unit/knowledge/VersionLedgerActivationTest.cpp`；`tests/unit/knowledge/VersionLedgerRollbackEligibilityTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools 定向构建；`RunCtest_CMakeTools` 工具态报 `生成失败` 后，已使用 build-ci 回退路径 `ctest` 3/3 验收通过 | 仅当 candidate/active/rollback eligibility 三类状态转换和 checksum 失配拒绝都可验证时完成 |
| KNO-TODO-019 | Done | 实现 IndexReader active snapshot 读路径 | 详设 6.13.3、7 KNO-D05、11.1 KNO-B03 | `IndexReader`；`IndexManifest`；active snapshot | L2 | 新增 `knowledge/include/index/IndexReader.h`、`knowledge/src/index/IndexReader.cpp`，落盘 active snapshot 原子读取、manifest 查询与 lexical search 只读路径；复用 013 `SparseIndexSearchRequest/Result` seam，并补充 writer-facing `swap_active_snapshot()` 供 020 接线 | `IndexReader::search_sparse`；`current_manifest` | unit：`IndexReaderTest`、`IndexReaderConcurrentSwapTest`、**`IndexReaderNoActiveSnapshotTest`**（验证 bootstrap/无活跃 snapshot 场景显式失败） | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_index_reader_unit_test dasall_index_reader_concurrent_swap_unit_test dasall_index_reader_no_active_snapshot_unit_test && ctest --test-dir build-ci -R "IndexReader.*Test" --output-on-failure` | KNO-TODO-001、006、018 | 已解除：KNO-BLK-001 | 已完成 KNO-TODO-001 | `docs/todos/knowledge/deliverables/KNO-TODO-019-IndexReader设计收敛.md`；`knowledge/include/index/IndexReader.h`；`knowledge/src/index/IndexReader.cpp`；`tests/unit/knowledge/IndexReaderTest.cpp`；`tests/unit/knowledge/IndexReaderConcurrentSwapTest.cpp`；`tests/unit/knowledge/IndexReaderNoActiveSnapshotTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools 定向构建；`RunCtest_CMakeTools` 工具态报 `生成失败` 后，已使用 build-ci 回退路径 `ctest` 3/3 验收通过 | 仅当 lexical snapshot 方案已冻结，且读路径满足 lock-free/MVCC 语义、active snapshot 缺失时显式失败时完成 |
| KNO-TODO-020 | NotStarted | 实现 IndexWriter shadow build 与 snapshot swap | 详设 6.13.3、10.4、11.1 KNO-B03 | `IndexWriter`；`swap_active_snapshot`；核心事务边界 | L2 | 新增 `knowledge/include/index/IndexWriter.h`、`knowledge/src/index/IndexWriter.cpp`，落盘 shadow build、candidate 记录、active swap、last-known-good 回退和 core/sidecar 分层语义 | `apply_update_batch`；`rebuild_all`；`build_shadow_index`；`swap_active_snapshot` | unit：`IndexWriterTest`、`IndexWriterSnapshotSwapTest`、`IndexWriterRecoveryTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests && ctest --test-dir build-ci -R "IndexWriter.*Test" --output-on-failure` | KNO-TODO-001、006、018、019 | 已解除：KNO-BLK-001 | 已完成 KNO-TODO-001 | `knowledge/include/index/IndexWriter.h`；`knowledge/src/index/IndexWriter.cpp`；对应 unit tests | 仅当 lexical snapshot 方案已冻结，且 `record_candidate -> swap -> mark_active -> catalog refresh` 顺序可二值验证、失败时不污染 active snapshot 时完成 |

### 6.5 Ingest 任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| KNO-TODO-021 | Done | 实现 SourceScanner source delta 与 quarantine | 详设 6.9、6.13.5、11.1 KNO-B04/KNO-R10 | `SourceScanner`；`SourceRecord`；`SourceScanDelta` | L2 | 新增 `knowledge/include/ingest/SourceScanner.h`、`knowledge/src/ingest/SourceScanner.cpp`，落盘 source 枚举、SHA-256 content hash、`version`/`updated_at_ms` diff、format allowlist、不可读源 quarantine 和 full scan 标志 | `SourceScanner::scan`；`compute_source_hash` | unit：`SourceScannerTest`、`SourceScannerDeltaDiffTest`、`SourceScannerQuarantineTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_source_scanner_unit_test dasall_source_scanner_delta_diff_unit_test dasall_source_scanner_quarantine_unit_test && ctest --test-dir build-ci -R "SourceScanner.*Test" --output-on-failure` | KNO-TODO-003、005、006 | 已解除：KNO-BLK-003 | 已完成 KNO-TODO-003 | `docs/todos/knowledge/deliverables/KNO-TODO-021-SourceScanner设计收敛.md`；`knowledge/include/ingest/SourceScanner.h`；`knowledge/src/ingest/SourceScanner.cpp`；`tests/unit/knowledge/SourceScannerTest.cpp`；`tests/unit/knowledge/SourceScannerDeltaDiffTest.cpp`；`tests/unit/knowledge/SourceScannerQuarantineTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools 定向构建；`RunCtest_CMakeTools` 工具态仍报 `生成失败` 后，已按仓库回退路径使用 build-ci `ctest` 3/3 验收通过 | 仅当 source baseline/trust 规则已冻结，且 added/updated/removed/quarantine 四类 delta 都可验证时完成 |
| KNO-TODO-022 | NotStarted | 实现 Canonicalizer 文档规范化 | 详设 6.13.5、11.1 KNO-B04 | `Canonicalizer`；`CanonicalDocument` | L2 | 新增 `knowledge/include/ingest/Canonicalizer.h`、`knowledge/src/ingest/Canonicalizer.cpp`，落盘 markdown 展平、runtime policy yaml key-path flatten、typed metadata 提取、fallback 规则与 stable `document_id` 生成 | `Canonicalizer::canonicalize`；`normalize_markup`；`extract_metadata` | unit：`CanonicalizerTest`、`CanonicalizerMarkupNormalizeTest`、`CanonicalizerMetadataFallbackTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests && ctest --test-dir build-ci -R "Canonicalizer.*Test" --output-on-failure` | KNO-TODO-003、005、006 | 已解除：KNO-BLK-003 | 已完成 KNO-TODO-003 | `knowledge/include/ingest/Canonicalizer.h`；`knowledge/src/ingest/Canonicalizer.cpp`；对应 unit tests | 仅当 metadata baseline 已冻结，且 markup 规范化失败会显式 quarantine/ warning，而不是生成半有效文档时完成 |
| KNO-TODO-023 | NotStarted | 实现 Chunker stable chunk 切分 | 详设 6.13.5、11.1 KNO-B04 | `Chunker`；`ChunkRecord`；stable chunk id | L2 | 新增 `knowledge/include/ingest/Chunker.h`、`knowledge/src/ingest/Chunker.cpp`，落盘段落/章节优先切分、overlap、token estimate、citation span、typed provenance 继承与 `chunk_id` 生成；**评审补充**：v1 默认 fixed-size 切分，接口预留 `ChunkStrategy`（fixed-size / semantic / document-aware）可配置扩展槽位 | `Chunker::chunk`；`split_into_spans`；`build_chunk_id` | unit：`ChunkerTest`、`ChunkerStableIdTest`、`ChunkerBoundaryFallbackTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests && ctest --test-dir build-ci -R "Chunker.*Test" --output-on-failure` | KNO-TODO-003、005、006 | 已解除：KNO-BLK-003 | 已完成 KNO-TODO-003 | `knowledge/include/ingest/Chunker.h`；`knowledge/src/ingest/Chunker.cpp`；对应 unit tests | 仅当 chunk policy 与 corpus baseline 已冻结，且 stable id、empty text、forced split 三类路径可二值验证时完成 |
| KNO-TODO-024 | NotStarted | 实现 IngestionCoordinator update batch 编排 | 详设 6.9、6.13.3、7 KNO-D05、11.1 KNO-B04 | `IngestionCoordinator`；`CorpusChangeSet`；`IndexUpdateBatch` | L2 | 新增 `knowledge/include/ingest/IngestionCoordinator.h`、`knowledge/src/ingest/IngestionCoordinator.cpp`，落盘 scan/canonicalize/chunk 组合编排、warning 收敛、quarantine 透传与 batch 装配 | `build_update_batch`；`scan_and_canonicalize`；`build_chunk_records` | unit：`IngestionCoordinatorTest`、`IngestionCoordinatorSelectiveRefreshTest`、`IngestionCoordinatorBadSourceTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests && ctest --test-dir build-ci -R "IngestionCoordinator.*Test" --output-on-failure` | KNO-TODO-003、021、022、023 | 已解除：KNO-BLK-003 | 已完成 KNO-TODO-003 | `knowledge/include/ingest/IngestionCoordinator.h`；`knowledge/src/ingest/IngestionCoordinator.cpp`；对应 unit tests | 仅当 source baseline 已冻结，且单源失败 warning、selective refresh 与 batch 组装都可自动验证时完成 |

### 6.6 Observability / Gate 任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| KNO-TODO-025 | Done | 实现 KnowledgeTelemetry 观测桥 | 详设 6.11、6.13.4、7 KNO-D06；SSOT 并发策略 | `KnowledgeTelemetry`；`KnowledgeTelemetryEvent` | L2 | 新增 `knowledge/include/health/KnowledgeTelemetry.h`、`knowledge/src/observability/KnowledgeTelemetry.cpp`，落盘 retrieve/ingest/health/snapshot_swap 事件统一字段、sink 容错、invalid payload fallback 与 drop counter | `emit_retrieve_event`；`emit_ingest_event`；`emit_health_event`；`emit_snapshot_swap_event` | unit：`KnowledgeTelemetryTest`、`KnowledgeTelemetryFieldSetTest`、`KnowledgeTelemetryDegradeEventTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_knowledge_telemetry_unit_test dasall_knowledge_observability_fields_unit_test dasall_knowledge_telemetry_degrade_event_unit_test && ctest --test-dir build-ci -R "KnowledgeTelemetry(Test|FieldSetTest|DegradeEventTest)" --output-on-failure` | KNO-TODO-005、006 | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-025-KnowledgeTelemetry观测桥设计收敛.md`；`knowledge/include/health/KnowledgeTelemetry.h`；`knowledge/src/observability/KnowledgeTelemetry.cpp`；`tests/unit/knowledge/KnowledgeTelemetryTest.cpp`；`tests/unit/knowledge/KnowledgeTelemetryFieldSetTest.cpp`；`tests/unit/knowledge/KnowledgeTelemetryDegradeEventTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 build 与 telemetry 三条 unit test | 仅当关键事件字段齐全、sink 失败不阻断主链路、并能记录 drop/invalid payload 事实时完成 |
| KNO-TODO-026 | Done | 实现 KnowledgeHealthProbe 健康快照 | 详设 6.13.4、7 KNO-D06 | `KnowledgeHealthProbe`；`KnowledgeHealthSnapshot` | L2 | 新增 `knowledge/include/health/KnowledgeHealthProbe.h`、`knowledge/src/health/KnowledgeHealthProbe.cpp`，并在 `knowledge/include/KnowledgeTypes.h` 中补齐 `KnowledgeHealthSnapshot` / `HealthState` public ABI；通过 provider seam 落盘 lifecycle/manifest/freshness/vector/degraded count 聚合与 `HealthState` 分类，避免在 026 内提前锁死 018/019 concrete owner | `KnowledgeHealthProbe::collect`；`classify_state`；`IKnowledgeService::health_snapshot` | unit：`KnowledgeHealthProbeTest`、`KnowledgeHealthProbeDegradedStateTest`、`KnowledgeHealthProbeUnknownDependencyTest`、`dasall_knowledge_interface_surface_unit_test` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_knowledge_interface_surface_unit_test dasall_knowledge_health_probe_unit_test dasall_knowledge_health_probe_degraded_state_unit_test dasall_knowledge_health_probe_unknown_dependency_unit_test && ctest --test-dir build-ci -R "(dasall_knowledge_interface_surface_unit_test|KnowledgeHealthProbe.*Test)" --output-on-failure` | KNO-TODO-006、017、025（018/019 通过 provider seam 隔离） | 无 | 无 | `docs/todos/knowledge/deliverables/KNO-TODO-026-KnowledgeHealthProbe健康快照设计收敛.md`；`knowledge/include/KnowledgeTypes.h`；`knowledge/include/health/KnowledgeHealthProbe.h`；`knowledge/src/health/KnowledgeHealthProbe.cpp`；`tests/unit/knowledge/KnowledgeInterfaceSurfaceSkeletonTest.cpp`；`tests/unit/knowledge/KnowledgeHealthProbeTest.cpp`；`tests/unit/knowledge/KnowledgeHealthProbeDegradedStateTest.cpp`；`tests/unit/knowledge/KnowledgeHealthProbeUnknownDependencyTest.cpp`；`tests/unit/knowledge/CMakeLists.txt`；`knowledge/CMakeLists.txt`；2026-04-21 已通过 build-ci 定向构建与 `ctest` 4/4 | 仅当 HealthProbe 在依赖缺失时不误判 Healthy、能区分 lexical-only degrade 与真正不可用状态、且 `KnowledgeHealthSnapshot` public ABI 已稳定可编译时完成 |
| KNO-TODO-027 | Done | 验证 lexical retrieval smoke integration | 详设 7 KNO-D07、8.2 Phase K1、9.1 | `dasall_knowledge_retrieval_smoke_integration_test` | L2 | 新增 `tests/integration/knowledge/KnowledgeRetrievalSmokeTest.cpp`，使用真实 `QueryNormalizer` / `CorpusRouter` / `FreshnessController` / `CorpusCatalog` / `IndexReader` / `SparseRetriever` / `RecallCoordinator` / `Reranker` / `EvidenceAssembler` 与 SQLite FTS5 fixture，打通 `Runtime-facing IKnowledgeService -> retrieve -> context_projection` 最小闭环 | `KnowledgeRetrievalSmokeTest`；`dasall_knowledge_retrieval_smoke_integration_test` | integration：最小 lexical retrieval 闭环可通过且 `ctest -N` 可发现 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge_retrieval_smoke_integration_test && ctest --test-dir build-ci -N | grep dasall_knowledge_retrieval_smoke_integration_test && ctest --test-dir build-ci -R dasall_knowledge_retrieval_smoke_integration_test --output-on-failure` | KNO-TODO-005、006、007、008、009、011、012、013、014、016、017、019 | 已解除：KNO-BLK-001 | 已完成 KNO-TODO-001 | `docs/todos/knowledge/deliverables/KNO-TODO-027-lexical-retrieval-smoke-integration设计收敛.md`；`tests/integration/knowledge/KnowledgeRetrievalSmokeTest.cpp`；`tests/integration/knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools 定向构建与 `ListTests_CMakeTools` 发现；`RunCtest_CMakeTools` 工具态仍报 `生成失败` 后，已按仓库回退路径使用 build-ci 验证 discoverability 与 smoke 执行通过 | 仅当 lexical-only 主链可对外返回非空 `context_projection`，且 smoke 用例可被 `ctest -N` 发现时完成 |
| KNO-TODO-028 | Done | 验证 failure/degrade integration | 详设 7 KNO-D07、9.1、11.1 KNO-R06/KNO-R07 | `dasall_knowledge_failure_degrade_integration_test` | L2 | 新增 `tests/integration/knowledge/KnowledgeFailureDegradeTest.cpp`，复用真实 lexical supporting chain 与 facade，覆盖 vector unavailable、partial timeout、stale reject、refresh busy 四类退化路径；其中 vector unavailable 走真实 `VectorRetrieverBridge` unavailable store，partial timeout 走 014 保留的 fallback dense seam，stale reject / refresh busy 走现有 freshness 与 refresh surface | `KnowledgeFailureDegradeTest`；`dasall_knowledge_failure_degrade_integration_test` | integration：degrade 路径以显式 `degraded` / `error` / `reason_codes` 形式可验证 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge_failure_degrade_integration_test && ctest --test-dir build-ci -R dasall_knowledge_failure_degrade_integration_test --output-on-failure` | KNO-TODO-012、014、015、017、019、025、026 | 已解除：KNO-BLK-001、KNO-BLK-002 | 已完成 KNO-TODO-001、002 | `docs/todos/knowledge/deliverables/KNO-TODO-028-failure-degrade-integration设计收敛.md`；`tests/integration/knowledge/KnowledgeFailureDegradeTest.cpp`；`tests/integration/knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools 定向构建；`RunCtest_CMakeTools` 工具态仍报 `生成失败` 后，已按仓库回退路径使用 build-ci `ctest` 1/1 验收通过 | 仅当 vector unavailable、partial timeout、stale reject、refresh busy 四类路径都能在 integration 层自动验证，且 degraded success 与显式拒绝语义均稳定可回归时完成 |
| KNO-TODO-029 | Done | 验证 profile compatibility integration | 详设 8.2 Phase K2/K4、9.1、10.2、QG-K04/K05 | `dasall_knowledge_profile_compatibility_integration_test` | L2 | 新增 `tests/integration/knowledge/KnowledgeProfileCompatibilityTest.cpp`，通过真实 `ProfileCatalog` / `BuildProfileResolver` / `RuntimePolicyProvider` / `KnowledgeConfigProjector` 覆盖 `desktop_full` / `cloud_full` / `edge_balanced` / `edge_minimal` / `factory_test` 的 enabled/vector/degrade 行为，并补一个基于真实 `desktop_full` manifest 的 synthetic vector downgrade case 锁定 `knowledge=true && memory_vector=false` 兼容规则 | `KnowledgeProfileCompatibilityTest`；`dasall_knowledge_profile_compatibility_integration_test` | integration：`knowledge=true && memory_vector=false` 合法；`edge_minimal` 默认关闭；hybrid 可在支持档位灰度 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge_profile_compatibility_integration_test && ctest --test-dir build-ci -N | grep KnowledgeProfileCompatibilityTest && ctest --test-dir build-ci -R KnowledgeProfileCompatibilityTest --output-on-failure` | KNO-TODO-007、012、015、017、025、026 | 已解除：KNO-BLK-002 | 已完成 KNO-TODO-002 | `docs/todos/knowledge/deliverables/KNO-TODO-029-profile-compatibility-integration设计收敛.md`；`tests/integration/knowledge/KnowledgeProfileCompatibilityTest.cpp`；`tests/integration/knowledge/CMakeLists.txt`；2026-04-21 已通过 Build_CMakeTools 定向构建；`RunCtest_CMakeTools` 工具态仍报 `生成失败` 后，已按仓库回退路径使用 build-ci `ctest -N` 发现 `KnowledgeProfileCompatibilityTest` 并 1/1 验收通过 | 仅当 profile 行为全部来自统一投影规则，且 `knowledge=false` / `knowledge=true && vector=false` / hybrid 三种模式都能自动验证时完成 |
| KNO-TODO-030 | NotStarted | 验证 retrieval quality regression gate | 详设 9.1、12.2 | `RetrievalQualityRegressionTest`；golden set | L2 | 新增 `tests/integration/knowledge/RetrievalQualityRegressionTest.cpp` 与 `tests/integration/knowledge/golden/retrieval_quality_v1.yaml` 基线资产，落盘 `source_uri` 去重后的 `MRR@10` / `NDCG@10` / `Recall@5` / `Recall@10` 回归校验与 `hard_fail` case 阻断 | `RetrievalQualityRegressionTest`；golden set manifest schema | integration：golden set ≥30 条，aggregate 指标不低于基线阈值，`hard_fail` case 全通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R RetrievalQualityRegressionTest --output-on-failure` | KNO-TODO-004、027、028 | 已解除：KNO-BLK-004 | 已完成 KNO-TODO-004 | `tests/integration/knowledge/RetrievalQualityRegressionTest.cpp`；`tests/integration/knowledge/golden/retrieval_quality_v1.yaml` 资产 | 仅当 golden set 资产、绝对阈值、relative regression 公式与 `hard_fail` case 都被自动验证，且质量退化会被自动阻断时完成 |
| KNO-TODO-031 | NotStarted | 回写 knowledge 专项 Gate 与交付证据 | 阶段 H；详设 8.2 Phase K5、9.3；工程执行规范 | 8.2 Phase K5；9.3 质量门 | L2 | 更新 `docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md`，回写 build / unit / integration / quality / blocker / rollback 证据 | Gate 结果、命令、风险、blocker、后继动作 | process：所有 Gate 都有命令证据、结果摘要和残余风险回写 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -R "Knowledge|dasall_knowledge"` | KNO-TODO-027、028、029、030、032、033 | 无 | 无 | 更新后的专项 TODO；更新后的 worklog；对应 deliverables 记录 | 仅当每个 Gate 都存在可追溯证据、blocker 状态与后继动作回写，不再依赖口头结论时完成 |

### 6.7 评审补充任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| KNO-TODO-032 | NotStarted | 补全 KnowledgeServiceFacade 完整编排（hybrid/ingest/health） | 评审 D-1：Facade 依赖过重需拆分为骨架 + 完整两阶段；详设 6.6、6.10、6.13.1 | `KnowledgeServiceFacade`；`RecallCoordinator` 真实接入；`IngestionCoordinator` 真实 refresh 编排；`KnowledgeHealthProbe` 接入 | L2 | 在 KNO-TODO-012 骨架基础上，替换 stub 为真实组件：接入 `RecallCoordinator`（含 sparse + dense lane）、`IndexReader` 真实 snapshot 读取、`IngestionCoordinator` 真实 refresh 编排、`KnowledgeHealthProbe` 完整健康聚合 | `KnowledgeServiceFacade::retrieve`（完整 hybrid 路径）；`request_refresh`（真实 ingest 编排）；`health_snapshot`（完整 probe 接入） | unit：`KnowledgeServiceFacadeHybridRecallTest`、`KnowledgeServiceFacadeRealRefreshTest`、`KnowledgeServiceFacadeFullHealthSnapshotTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge dasall_unit_tests && ctest --test-dir build-ci -R "KnowledgeServiceFacade.*(Hybrid|RealRefresh|FullHealth).*Test" --output-on-failure` | KNO-TODO-012、013、014、015、019、020、024、026 | 已解除：KNO-BLK-001、KNO-BLK-002、KNO-BLK-003 | 已完成 KNO-TODO-001、002、003 | 更新后的 `knowledge/src/facade/KnowledgeService.cpp`；对应 unit tests | 仅当 facade 内所有 stub seam 被替换为真实组件、hybrid recall / real ingest refresh / full health 路径可自动验证，且 facade 不再包含硬编码 mock 时完成 |
| KNO-TODO-033 | NotStarted | 验证 request_refresh → ingest → snapshot swap → retrieve 端到端集成闭环 | 评审 D-4：缺少 refresh 闭环集成测试；详设 6.6、6.9、10.4 | `request_refresh`；`IngestionCoordinator`；`IndexWriter.swap_active_snapshot`；`IndexReader`；retrieve 闭环 | L2 | 新增 `tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp`，验证 `request_refresh(changes)` → 触发 ingest → shadow build → snapshot swap → 下次 `retrieve()` 返回更新后数据的完整闭环；同时验证 refresh busy reject 与 swap 失败回退语义 | `KnowledgeRefreshLoopTest`；`dasall_knowledge_refresh_loop_integration_test` | integration：refresh → retrieve 闭环可通过；refresh busy reject 可验证；swap 失败时 active snapshot 不污染 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R dasall_knowledge_refresh_loop_integration_test --output-on-failure` | KNO-TODO-020、024、032 | 已解除：KNO-BLK-001、KNO-BLK-003 | 已完成 KNO-TODO-001、003 | `tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp`；对应 integration CMake | 仅当 refresh → ingest → swap → retrieve 端到端闭环可自动验证、refresh busy 拒绝可二值判定、swap 失败不丢失 last-known-good snapshot 时完成 |

---

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 补设计解阻 | KNO-TODO-001 ~ 004 | 已完成 | lexical/vector/corpus/quality 四类 blocker 已冻结完毕，后续阶段不再受设计 blocker 返工扩散 |
| B 公共骨架与 ABI + 观测基础 | KNO-TODO-005、006、007、025 | 005 先起；006 紧随；007 依赖 006；025 在 005/006 后启动 | 先建立 include/CMake/test discoverability，再冻结 public surface 与配置投影；**025（Telemetry）前移至此阶段**以遵守 KNO-TC015"四类关键路径必须可观测、不能后置"的约束 |
| C Route / Evidence 纯计算链 + 健康探针 | KNO-TODO-016、017、008、009、010、011、026 | 016/017 可并行；008/010 可并行；009 依赖 016/017/008；011 依赖 010；026 在 017/018/025 后启动 | 先把无外部 I/O 的纯计算与 snapshot 支撑层做稳；**026（HealthProbe）前移至此阶段**以确保健康状态可在 lexical 主链闭环前就位 |
| D lexical 最小主链 + 骨架 Facade | KNO-TODO-013、018、019、014、012、027 | 013/018 在 KNO-BLK-001 解阻后并行；019 依赖 013/018；014 依赖 013（dense lane 使用 stub）；012 依赖上游纯计算链与 stub seam；027 最后验证 | 形成 lexical-only 最小闭环；**012 已拆分为骨架版本**，仅依赖已解阻组件 + stub |
| E hybrid 与退化闭环 | KNO-TODO-015、014（补齐 dense lane）、028、029 | 015 解阻后先做；014 补齐 dense lane 真实接入；028/029 串行验证 | hybrid 与 profile 只在边界冻结后推进 |
| F ingest / snapshot 治理 + Facade 完整版 | KNO-TODO-021、022、023、024、020、032 | 021/022/023 现在可并行；024 依赖前三者；020 依赖 001/018/019/024；**032 在 013/014/015/019/020/024/026 就绪后补全 Facade 真实编排** | 构建 source -> canonical -> chunk -> batch -> snapshot swap 全链；Facade 从骨架升级为完整编排 |
| G 质量门与证据收口 | KNO-TODO-030、033、031 | 030 依赖 004、027、028；**033 在 020/024/032 后验证 refresh 闭环**；031 最后收口 | 把 golden set、refresh 闭环与证据回写收口到 Gate |

### 7.2 必过门禁表

| 门禁 | 通过条件 | 对应任务 | 未通过时禁止推进 |
|---|---|---|---|
| Gate-A：补设计冻结 | KNO-BLK-001 ~ 004 已全部完成解阻 | KNO-TODO-001 ~ 004 | 禁止在 blocker 未解前启动 lexical / hybrid / ingest / quality Build |
| Gate-B：ABI 冻结 | `IKnowledgeService`、public types、error mapping 编译与 surface test 全绿 | KNO-TODO-005 ~ 007 | 禁止 Runtime 侧接线与上游 mock 依赖 |
| Gate-B+：观测基础就位 | `KnowledgeTelemetry` 四类关键事件字段齐全、sink 容错可验证 | KNO-TODO-025 | 禁止组件实现不接入观测（KNO-TC015） |
| Gate-C：discoverability | `ctest -N` 可发现 knowledge 的 unit / integration 入口 | KNO-TODO-005 | 禁止 Phase D/E/F 的测试主张 |
| Gate-D：lexical smoke | lexical-only 主链返回非空 `context_projection` | KNO-TODO-013、014、019、012、027 | 禁止 hybrid/profile 推进 |
| Gate-E：hybrid degrade | vector unavailable/timeout 时明确退 lexical-only，不进程失败 | KNO-TODO-015、028、029 | 禁止 profile 扩面 |
| Gate-F：index consistency | `record_candidate -> swap -> mark_active` 顺序与 rollback 可验证 | KNO-TODO-018、019、020、024 | 禁止 stale/read/rebuild 灰度 |
| Gate-F+：refresh 闭环 | `request_refresh()` → ingest → swap → retrieve 返回更新数据的端到端闭环可验证 | KNO-TODO-032、033 | 禁止声称 ingest/index 治理完成 |
| Gate-G：quality baseline | golden set 的 `MRR@10` / `NDCG@10` / `Recall@5` / `Recall@10` 不低于阈值，且 `hard_fail` case 全通过 | KNO-TODO-004、030 | 禁止把 Knowledge 标记为 ready |
| Gate-H：evidence 回写 | TODO / worklog / deliverables 证据齐全 | KNO-TODO-031 | 禁止收尾宣称完成 |

---

## 8. 阻塞项与解阻条件

| Blocker ID | 阻塞描述 | 直接受影响任务 | 解阻条件 | 当前处置策略 |
|---|---|---|---|---|
| KNO-BLK-001 | lexical index 技术栈未冻结，`SparseRetriever` / `IndexReader` / `IndexWriter` 无法安全编码；现已由 KNO-TODO-001 解阻 | KNO-TODO-013、019、020、027、028、032、033 | 完成 KNO-TODO-001，形成唯一 lexical 方案与 PoC 证据 | 已解阻；lexical 主链、smoke 与相关 integration 可进入 Build |
| KNO-BLK-002 | `VectorRetrieverBridge` 的窄接口 owner 与注入方向未冻结；现已由 KNO-TODO-002 解阻 | KNO-TODO-015、014、028、029、032 | 完成 KNO-TODO-002，锁定 `IQueryEncoder` / `IVectorRecallStore` 边界 | 已解阻；hybrid bridge、failure/degrade 与 profile compatibility 可进入 Build |
| KNO-BLK-003 | corpus baseline、metadata 必填字段与 trust/quarantine 规则缺失；现已由 KNO-TODO-003 解阻 | KNO-TODO-021、022、023、024、032、033 | 完成 KNO-TODO-003，建立最小语料包与 metadata/trust SSOT | 已解阻；ingest/index/Facade 完整版/refresh 闭环可进入 Build 排程 |
| KNO-BLK-004 | golden set 资产与质量阈值未冻结；现已由 KNO-TODO-004 解阻 | KNO-TODO-030 | 完成 KNO-TODO-004，冻结样本格式、条数下限、`hard_fail` 规则和 fail 阈值 | 已解阻；quality regression Gate 可进入 Build 排程 |

### 8.1 Blocker 校准记录

执行期间逐 blocker 填写校准记录，当前为占位。

| Blocker ID | 校准时间 | 校准结果 | 剩余阻塞范围 | 校准者 |
|---|---|---|---|---|
| KNO-BLK-001 | 2026-04-21 | 已解阻：SQLite FTS5 + 显式 tokenizer profile + host-side 10k chunks PoC 已回写 | 无；后续只剩 Build 接线与实现验证 | GitHub Copilot |
| KNO-BLK-002 | 2026-04-21 | 已解阻：`IQueryEncoder` / `IVectorRecallStore` owner、注入方向与 degrade 语义已回写 | 无；后续只剩 port 实现与 adapter 接线 | GitHub Copilot |
| KNO-BLK-003 | 2026-04-21 | 已解阻：首批 corpus baseline、`AuthorityLevel` / `SourceFormat` / `CorpusScanPlan`、typed provenance 字段与 quarantine 条件已回写 | 无；后续只剩 ingest/index 实现与真实资产接线 | GitHub Copilot |
| KNO-BLK-004 | 2026-04-21 | 已解阻：`retrieval_quality_v1.yaml` manifest、样本覆盖下限、绝对阈值、relative regression 公式与 `hard_fail` 规则已回写 | 无；后续只剩 golden 资产与 regression harness 实现 | GitHub Copilot |

---

## 9. 验收与质量门

### 9.1 质量门清单

| Gate ID | 质量门 | 通过标准 | 验收命令 |
|---|---|---|---|
| QG-K01 | 构建门 | `dasall_knowledge` 可编译，且不再仅由 placeholder 维持 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_knowledge` |
| QG-K02 | Public ABI 门 | `IKnowledgeService` 与 public types/error mapping 有至少 1 个 surface unit test 通过 | `ctest --test-dir build-ci -R dasall_knowledge_interface_surface_unit_test --output-on-failure` |
| QG-K03 | Discoverability 门 | `ctest -N` 可发现至少 1 个 knowledge integration 用例 | `ctest --test-dir build-ci -N` |
| QG-K04 | Lexical-only 兼容门 | `knowledge=true && memory_vector=false` 不报配置错误，lexical-only smoke 通过 | `ctest --test-dir build-ci -R dasall_knowledge_retrieval_smoke_integration_test --output-on-failure` |
| QG-K05 | Degrade 门 | vector unavailable/timeout/stale reject/refresh busy 路径均显式表达，不进程级失败 | `ctest --test-dir build-ci -R dasall_knowledge_failure_degrade_integration_test --output-on-failure` |
| QG-K06 | Profile 门 | `desktop_full` / `cloud_full` / `edge_balanced` / `edge_minimal` / `factory_test` 的启停与降级行为符合投影规则 | `ctest --test-dir build-ci -R dasall_knowledge_profile_compatibility_integration_test --output-on-failure` |
| QG-K07 | 质量回归门 | golden set 的 `MRR@10` / `NDCG@10` / `Recall@5` / `Recall@10` 不低于基线阈值，且 `hard_fail` case 全通过 | `ctest --test-dir build-ci -R RetrievalQualityRegressionTest --output-on-failure` |
| QG-K08 | 可观测性门 | retrieve/ingest/swap/degraded 四类关键字段齐全，缺失字段视为失败 | `ctest --test-dir build-ci -R "Knowledge(Telemetry|HealthProbe).*Test" --output-on-failure` |
| QG-K09 | 证据回写门 | TODO、deliverables、worklog 全部写入命令、结果、风险和 blocker 状态 | `rg -n "KNO-TODO-|Knowledge|quality|blocker|rollback" docs/todos/knowledge docs/worklog/DASALL_开发执行记录.md` |
| QG-K10 | ADR 边界门 | Knowledge 不生成 ContextPacket（ADR-006）、不拥有调度环（ADR-008）、不做恢复裁定（ADR-007），通过代码审查与 `rg` 反向搜索确认 | `rg -n "ContextPacket\|PromptComposer\|RecoveryManager\|timer\|watcher\|schedule_loop" knowledge/include knowledge/src` 结果为空 |
| QG-K11 | 并发安全门 | lock ordering 符合 `InfraConcurrencyPolicy`（L0→L1→L2），不持 L2 锁做 I/O；queue/buffer 声明 overflow_policy | `rg -n "mutex\|lock_guard\|unique_lock\|atomic" knowledge/src` 结果与 lock ordering 声明一致；`rg -n "overflow_policy\|backpressure" knowledge/include knowledge/src` 可命中 |
| QG-K12 | Refresh 闭环门 | `request_refresh()` → ingest → swap → retrieve 端到端闭环可验证，refresh busy reject 与 swap 失败回退可验证 | `ctest --test-dir build-ci -R dasall_knowledge_refresh_loop_integration_test --output-on-failure` |

### 9.2 最小验收命令组合

```bash
cmake -S . -B build-ci -G "Unix Makefiles"
cmake --build build-ci --target dasall_knowledge dasall_unit_tests dasall_integration_tests
ctest --test-dir build-ci -N
ctest --test-dir build-ci -R "Knowledge|dasall_knowledge" --output-on-failure
```

### 9.3 契约影响检查点

1. `EvidenceBundle.context_projection` 只能投影到既有 `ContextPacket.retrieval_evidence`，不得引入新的 shared contracts 字段。
2. `KnowledgeRetrieveResult.error` 必须复用 `ErrorInfo`，不得私造跨模块错误对象。
3. `IKnowledgeService` 与 supporting types 当前维持 module-local，不得越级 admission 到 shared contracts。

### 9.4 Gate 执行证据表

执行期间逐 Gate 填写，当前为占位。

| Gate ID | 执行时间 | 执行命令 | 执行结果 | 残余风险 | 后继动作 |
|---|---|---|---|---|---|
| QG-K01 | — | — | — | — | — |
| QG-K02 | — | — | — | — | — |
| QG-K03 | — | — | — | — | — |
| QG-K04 | — | — | — | — | — |
| QG-K05 | — | — | — | — | — |
| QG-K06 | — | — | — | — | — |
| QG-K07 | — | — | — | — | — |
| QG-K08 | — | — | — | — | — |
| QG-K09 | — | — | — | — | — |
| QG-K10 | — | — | — | — | — |
| QG-K11 | — | — | — | — | — |
| QG-K12 | — | — | — | — | — |

---

## 10. 风险与回退策略

| 风险 ID | 风险描述 | 影响 | 缓解策略 | 回退策略 |
|---|---|---|---|---|
| KNO-R01 | lexical index 选型与目标平台不匹配 | `SparseRetriever` 延迟、体积或交叉编译失败 | `KNO-TODO-001` 已冻结 SQLite FTS5；Build 阶段继续验证共享 sqlite target、交叉编译和目标机 smoke | 回退到最小可工作的 FTS5 测试夹具索引；暂不打开其它 engine 选型 |
| KNO-R02 | vector bridge ownership 继续漂移 | hybrid 任务返工扩散到 memory / profile / tests | `KNO-TODO-002` 已冻结 Knowledge module-local ports 与注入方向；Build 阶段继续验证 adapter 落点 | 回退到 lexical-only，保持 `memory_vector=false` 合法组合 |
| KNO-R03 | stale read 被滥用 | 过时证据进入上游，影响 grounding | `FreshnessController` + `KnowledgeHealthProbe` + 审计事件强校验 stale serve 计数 | profile 层禁用 stale read，并回退到 last-known-good snapshot |
| KNO-R04 | corpus 投毒或间接 prompt injection | 恶意语料污染 evidence 与 LLM 推理 | 建立 trust level + quarantine 规则，坏源不入 active snapshot | 立即将对应 corpus 标记 `Quarantined`，并禁用 Knowledge 或切回 memory-only |
| KNO-R05 | snapshot swap 或 ledger 失配 | 查询读到不一致状态或丢失回退依据 | 强制 `record_candidate -> swap -> mark_active` 顺序和 checksum 校验 | 回退 `last_known_good()`，拒绝继续激活异常 snapshot |
| KNO-R06 | observability 字段缺失 | 退化与失败不可追踪，Gate 形同虚设 | `KnowledgeTelemetryFieldSetTest` 与 QG-K08 强阻断 | 停止发布，先补字段而不是带病上线 |
| KNO-R07 | edge 设备 lexical recall 超时 | `edge_balanced` 预算被击穿 | `KNO-TODO-001` 已补 host-side PoC；后续在 KNO-TODO-030 中继续纳入质量基线与目标机测量 | 对 edge profile 回退到 `knowledge=false` 或更小 corpus 范围 |
| KNO-R08 | golden set 不足或偏斜 | quality gate 失真，排序优化无有效反馈 | `KNO-TODO-004` 已冻结样本覆盖标准、`hard_fail` case 与 absolute threshold；KNO-TODO-030 落盘时按该标准补齐资产 | 暂停 quality-ready 结论，只保留 smoke/degrade 级可用 |
| KNO-R09 | token 估算 `chars/4` 对 CJK 文本精度仅 ~70% | evidence budget 超支或浪费，context projection 截断不准 | `EvidenceAssembler` budget clamp 预留 10% 安全余量；在 v2 中评估引入真实 tokenizer 估算 | 回退到更保守的 budget 比例（`chars/5`），宁可少填不超 |
| KNO-R10 | `MultiHop` 枚举值 v1 仅声明不落链路，后续扩展可能影响 QueryNormalizer/CorpusRouter 接口 | v2 新增 multi-hop 执行链路时发现 v1 接口不兼容，需回退 ABI | 在 KNO-TODO-006 中把 `MultiHop` 标记为 `reserved`，并在 `QueryNormalizer` 中遇到 MultiHop 时返回 `NotSupported` 错误 | 延迟 MultiHop 到 v2 版本周期，v1 仅保留枚举声明 |

系统级回退基线：

1. Profile 回退：`enabled_modules.knowledge=false`，主链回退到 memory-only 上下文。
2. 检索回退：强制 `retrieval_mode_default=LexicalOnly`，禁用 vector bridge。
3. 索引回退：切回 `VersionLedger.last_known_good()` 对应 snapshot，并记录审计事件。
4. 语义回退：返回 `evidence_insufficient=true`，由 Runtime 走澄清、低置信回复或继续无 Knowledge 路径。

---

## 11. 可行性结论

结论：Knowledge 专项 TODO 可以直接进入执行，但执行策略必须分成“立即可做”和“先解阻再做”两段。

当前可直接落到的最细粒度：

1. L3：public ABI、配置投影、QueryNormalizer、CorpusRouter、Reranker、EvidenceAssembler、FreshnessController。
2. L2：KnowledgeServiceFacade（骨架版，已拆分为 012 + 032 两阶段）、SparseRetriever、VectorRetrieverBridge、CorpusCatalog、VersionLedger、IndexReader、IndexWriter、SourceScanner、Canonicalizer、Chunker、IngestionCoordinator、KnowledgeTelemetry、KnowledgeHealthProbe、RetrievalQualityRegression。
3. 当前无额外设计 Blocked 项。

评审修正要点（共 33 项任务，较原始版本新增 032/033）：

1. **Facade 拆分（P0 D-1）**：KNO-TODO-012 降级为 lexical-only 骨架，使用 stub 隔离 14 → 10 项依赖；KNO-TODO-032 承接完整编排，当前已完成设计解阻。
2. **Telemetry 前移（P0 D-2）**：025 从 Phase G 前移至 Phase B+，026 前移至 Phase C+，遵守 KNO-TC015"不能后置"约束。
3. **新增质量门（P1）**：QG-K10 ADR 边界门、QG-K11 并发安全门、QG-K12 Refresh 闭环门，共计 12 个质量门。
4. **新增门禁（P1）**：Gate-B+ 观测基础、Gate-F+ refresh 闭环，共计 10 个必过门禁。
5. **任务级微调（P2/P3）**：001 补 edge BM25 基准、004 补 context-level metrics 槽位、006 补 MultiHop/预留字段 v1 声明、010 补融合策略扩展接口、011 补 token 估算精度与 stale 标注、014 修正 v1 串行与 stub 策略、016/019 补 cold-start 测试、017 补 freshness 传播注解、021 补 SHA-256 约束、023 补可配置 chunk 策略。
6. **新增风险（P2/P3）**：KNO-R09 CJK token 估算精度、KNO-R10 MultiHop 延迟风险。

直接执行建议：

1. `KNO-TODO-001` ~ `KNO-TODO-004` 已全部完成；下一步进入 005 ~ 012、016 ~ 020、025 ~ 029 的 Build 实现。
2. `KNO-BLK-001` ~ `KNO-BLK-004` 已全部解锁；030 仅受 027/028 与 golden 资产落盘前置约束，不再受设计 blocker 限制。
3. `KNO-TODO-021` ~ `024` 与 `032` ~ `033` 可按 Phase F/G 排程，先完成 ingest/index 主链，再收口 refresh/quality Gate。
4. 当前最合理的串行顺序是：005 -> 006 -> 007 -> 016/017 -> 008/010 -> 009/011 -> 025/026 -> 013/018/019 -> 014/012 -> 027 -> 015 -> 028/029 -> 021/022/023/024 -> 020/032 -> 030/033 -> 031。

四项 blocker 已全部完成，Knowledge 后续工作的关键已不再是“继续冻边界”，而是按 Phase B ~ G 顺序把 ABI、query/evidence、index/ingest、refresh 与 quality Gate 真正落到代码和测试。当前最合理的工程策略是：先起骨架与纯计算链，再做 lexical/hybrid 闭环，最后用 refresh/quality Gate 收口。

