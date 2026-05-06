# RetrievalEvidenceProjectionV1 (Single Source of Truth)

状态：Frozen
Owner：contracts / knowledge / memory / runtime
关联任务：INT-TODO-003
关联阻塞：INT-BLK-03
关联 Gate：Gate-INT-01、Gate-INT-04

## 1. 目的

本文件冻结 knowledge -> runtime -> memory/cognition 的最小结构化共享证据投影，避免 `EvidenceSlice` / `EvidenceBundle` 的丰富语义在进入 shared contracts 时被完全扁平化成字符串数组。

## 2. 范围与设计约束

适用范围：

1. `KnowledgeRetrieveResult.evidence` 到 `MemoryContextRequest` / `ContextPacket` 的共享投影。
2. 仅覆盖可进入 shared contracts 的最小结构化 evidence ref，不覆盖 Knowledge 内部检索策略、rerank 细节与 corpus 元数据全集。
3. 保持现有 `external_evidence` / `retrieval_evidence` 字符串路径继续兼容存在。

冻结约束：

1. 只能采用 additive + optional 演进；不得破坏现有 `std::vector<std::string>` ABI。
2. 不允许把 `KnowledgeQuery`、`EvidenceSlice`、`EvidenceBundle`、`CorpusDescriptor` 整体抬升进 shared contracts。
3. shared contracts 只承载最小引用与摘要字段；source-of-truth 仍然归 knowledge。

## 3. `RetrievalEvidenceRef` 最小字段表

| 字段 | 是否必填 | 来源映射 | 共享语义 | 明确禁止 |
|---|---|---|---|---|
| `evidence_ref` | 必填 | `EvidenceSlice.evidence_id` | 当前检索切片的稳定引用 | 不得复用为 source URI 或 query id |
| `source_ref` | 必填 | `EvidenceSlice.citation_ref` 或等价知识源锚点 | 指向上游来源对象/文档/快照的稳定引用 | 不得塞入整段原文或 provider payload |
| `source_kind` | 必填 | `CorpusDescriptor.source_kind` 的稳定枚举投影 | 指出来源是 `file` / `config_snapshot` / `curated_bundle` 等哪类资产 | 不得扩写为完整 corpus descriptor |
| `summary_text` | 必填 | `EvidenceSlice.snippet` 的裁剪/脱敏摘要 | 供 runtime、memory、cognition 读取的最小证据摘要 | 不得假装是全文或原始 chunk |
| `trust_level` | 必填 | `CorpusDescriptor.trust_level` | 共享的来源可信度标签 | 不得改写为模型生成分数 |
| `freshness` | 必填 | `EvidenceSlice.freshness` | 共享的 freshness 状态标签 | 不得反向推出刷新策略或 catalog 内部状态 |
| `anchor_locator` | 可选 | `citation_ref` 片段、行号、章节锚点等 knowledge 可稳定提供的定位信息 | 用于 citation preservation 和 explainability 回链 | 不得要求所有 source 都强制提供 |

## 4. 投影规则

1. `RetrievalEvidenceRef` 是 `EvidenceSlice` 的共享最小投影，而不是 `EvidenceSlice` 本体；knowledge 仍保留完整 source-of-truth。
2. `summary_text` 只保留面向主链消费的摘要，不得承载 raw chunk、原始 JSON、数据库行内容、二进制句柄或 rerank 内部特征。
3. `source_kind` 与 `trust_level` 来自知识目录的稳定来源属性，不由 runtime / memory / cognition 二次推断。
4. `freshness` 只表达 `Fresh` / `StaleAllowed` / `StaleRejected` / `Unknown` 的共享标签；刷新间隔、catalog age、后台任务状态保持 module-local。
5. `anchor_locator` 缺失时不得阻断主链；这类场景只意味着 citation 回链能力降级，而不是 evidence 本身无效。

## 5. 与现有字符串路径的兼容规则

1. `external_evidence` / `retrieval_evidence` 继续保留，作为面向上下文拼装与 prompt 消费的文本投影视图。
2. 新增的 `RetrievalEvidenceRef[]` 只承担结构化 provenance / freshness / citation preserve 的最小共享职责，不替代现有文本视图。
3. 下游模块若只消费文本，不必理解 `RetrievalEvidenceRef`；若需要 freshness / trust / citation，必须优先读取结构化 refs，而不是尝试从字符串反解。
4. 任何 consumer 都不得把 `RetrievalEvidenceRef` 补写结果反向视为 knowledge source-of-truth。

## 6. 明确不进入 shared contracts 的字段

以下字段继续保留在 knowledge module-local 或 invoke-scoped 事实中：

1. `EvidenceSlice.confidence`
2. `EvidenceSlice.tags`
3. `EvidenceBundle.omitted_sources`、`coverage_notes`、`degraded`、`evidence_insufficient`
4. `CorpusDescriptor.source_uri`、`allowed_formats`、`metadata`、`authority_level`
5. retrieval query、rerank score、召回模式、budget hint、catalog refresh 状态

## 7. Design -> Build 映射

| 设计决策 | 后续 Build 任务 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| 在 contracts 层新增 additive + optional 的 `RetrievalEvidenceRef` | `INT-TODO-008` | `contracts/include/context/RetrievalEvidenceRef.h`、`contracts/include/context/ContextPacket.h` | `ContextPacketContractTest`、`RetrievalEvidenceRefContractTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "ContextPacketContractTest|RetrievalEvidenceRefContractTest" --output-on-failure` |
| memory surface 同时消费文本 evidence 与结构化 refs | `INT-TODO-009` | `memory/include/context/MemoryContextRequest.h`、`memory/src/context/ContextOrchestrator.cpp` | `ContextOrchestratorEvidenceProjectionTest`、`MemoryEvidenceProjectionCompileTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory && ctest --test-dir build-ci -R "ContextOrchestratorEvidenceProjectionTest|MemoryEvidenceProjectionCompileTest" --output-on-failure` |
| knowledge -> runtime -> memory/cognition 主链保留 citation/freshness | `INT-TODO-013`、`INT-TODO-019` | `knowledge/src/facade/KnowledgeService.cpp`、`runtime/src/AgentOrchestrator.cpp`、integration tests | `RuntimeKnowledgeEvidenceIntegrationTest`、`RuntimeEvidenceProjectionIntegrationTest`、`KnowledgeEvidencePreservationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "RuntimeKnowledgeEvidenceIntegrationTest|RuntimeEvidenceProjectionIntegrationTest|KnowledgeEvidencePreservationTest" --output-on-failure` |

## 8. 验证锚点

```bash
rg -n "evidence_ref|source_ref|source_kind|summary_text|trust_level|freshness|anchor_locator" \
  docs/ssot/RetrievalEvidenceProjectionV1.md \
  docs/ssot/CrossModuleDataProjectionMatrix.md \
  docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md
```

## 9. 结论

1. `INT-BLK-03` 的设计出口已经固定：shared contracts 只新增最小 `RetrievalEvidenceRef`，不提升 whole `EvidenceSlice` / `EvidenceBundle`。
2. 003 完成后，后续 Build 只需把 additive fields 与主链 preserve 行为落到 contracts、memory、runtime、knowledge 和 integration gate，不再需要口头讨论字段边界。