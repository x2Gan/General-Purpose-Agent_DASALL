# KNO-TODO-003 corpus 资产与 metadata/trust 基线设计收敛

- 日期：2026-04-21
- 任务：KNO-TODO-003
- 状态：已收敛
- 对应 Blocker：KNO-BLK-003

## 1. 输入与约束

1. `SourceScanner`、`Canonicalizer`、`Chunker`、`IngestionCoordinator` 的接口名和职责卡片已经存在，但真实 Build 仍缺“扫描哪些资产”“哪些 metadata 是硬约束”“什么情况下必须 quarantine”的单一答案。
2. `SourceScanner` 在详设中已经要求“只接受 `trust_level >= Trusted` 的注册源”，`CorpusRouter` 又已经把 `authority_level` 作为过滤条件之一；但当前对象面没有把 `authority_level`、`source_kind`、`source_format` 和扫描 glob 规则写成稳定字段，导致 021 ~ 024 进入 Build 仍会反复返工。
3. repo 当前真实可用的 corpus 候选不是抽象概念，而是已有仓库资产：`docs/architecture/`、`docs/adr/`、`docs/ssot/` 与 `profiles/*/runtime_policy.yaml`。
4. `profiles/*/runtime_policy.yaml` 同时也是 `KnowledgeConfigProjector` 的上游配置来源；因此 003 必须显式声明“profile snapshot 可进入 retrieval corpus，但不改变配置投影 owner”。

## 2. 本地证据

| 证据 | 观察结果 | 结论 |
|---|---|---|
| knowledge 详设 §6.13.5 | `SourceKind`、`CorpusScanPlan`、`authority_level` 已在语义层被引用，但对象定义不完整 | 003 需要先补 typed schema，而不是只写 prose |
| knowledge 详设索引规则 | 仅口头写到 `MetadataExtractor` 生成 `corpus_id/source_uri/version/updated_at/tags/authority_level/language` | provenance 字段必须落到稳定字段，而不是留在自由文本 metadata |
| 仓库真实目录 | `docs/architecture/`、`docs/adr/`、`docs/ssot/`、`profiles/*/runtime_policy.yaml` 实际存在且长期受版本控制 | 首批 corpus baseline 可以直接锚定真实资产，而不是继续抽象成“local docs/config snapshots” |
| 波动性文档目录 | `docs/todos/`、`docs/worklog/`、`docs/plans/`、`docs/development/` 高度变动、包含过程性记录与提示词模板 | 不应进入 v1 Trusted/Normative baseline，否则 evidence 容易被过程文档污染 |
| repo memory：projection 矩阵 | Knowledge 的 profile 消费链 owner 已固定为 `KnowledgeConfigProjector -> KnowledgeConfigSnapshot` | profile YAML 若进入 corpus，只能作为 retrieval evidence，不得回写配置 owner |

## 3. 设计结论

### 3.1 首批 corpus baseline

1. v1 首批 corpus baseline 固定为四类真实仓库资产：
   - `architecture_reference`
   - `adr_normative`
   - `ssot_normative`
   - `profile_policy_normative`
2. 首批 baseline 明确排除 `docs/todos/`、`docs/worklog/`、`docs/plans/`、`docs/development/` 以及 `docs/architecture/*评审报告*.md`、`*迁移影响清单*.md` 等过程性/波动性文档。
3. `profile_policy_normative` 只为 query/evidence 提供引用文本，不改变 `KnowledgeConfigProjector` 对 profile 投影的唯一 owner 身份。

| `corpus_id` | `source_uri` | `include_globs` | `exclude_globs` | `source_kind` | `allowed_formats` | `authority_level` | `trust_level` | `supported_modes` | 说明 |
|---|---|---|---|---|---|---|---|---|---|
| `architecture_reference` | `docs/architecture/` | `DASALL_Agent_architecture.md`；`DASALL_Engineering_Blueprint.md`；`DASALL_*详细设计*.md`；`platform_linux_detailed_design.md` | `*评审报告*.md`；`*迁移影响清单*.md`；`DASALL_boundary治理与优化说明.md` | `File` | `Markdown` | `Reference` | `Trusted` | `LexicalOnly`、`Hybrid` | 子系统设计与工程蓝图的主 reference corpus |
| `adr_normative` | `docs/adr/` | `ADR-*.md` | — | `File` | `Markdown` | `Normative` | `Trusted` | `LexicalOnly`、`Hybrid` | 架构边界、owner 与 fail-closed 规则的最高优先级文本来源 |
| `ssot_normative` | `docs/ssot/` | `*.md` | — | `File` | `Markdown` | `Normative` | `Trusted` | `LexicalOnly`、`Hybrid` | 跨模块投影、并发和 integration topology 的单一真相来源 |
| `profile_policy_normative` | `profiles/` | `*/runtime_policy.yaml` | — | `ConfigSnapshot` | `Yaml` | `Normative` | `Trusted` | `LexicalOnly` | 仅用于 profile policy 文本引用，不参与配置 owner 重选 |

### 3.2 typed schema 冻结

```cpp
enum class TrustLevel {
  Trusted,
  Quarantined,
  Unregistered,
};

enum class AuthorityLevel {
  Normative,
  Reference,
  Advisory,
};

enum class SourceKind {
  File,
  ConfigSnapshot,
  CuratedBundle,
};

enum class SourceFormat {
  Markdown,
  Yaml,
  Text,
};

struct CorpusScanPlan {
  std::string corpus_id;
  std::string root_uri;
  SourceKind source_kind = SourceKind::File;
  std::vector<std::string> include_globs;
  std::vector<std::string> exclude_globs;
  std::vector<SourceFormat> allowed_formats;
  bool full_scan = false;
};
```

2. `CorpusDescriptor` 必须补齐 `authority_level`、`source_kind`、`allowed_formats`、`include_globs`、`exclude_globs`，否则 `CorpusCatalog` 无法为 `SourceScanner` 和 `CorpusRouter` 同时提供单一事实来源。
3. `SourceRecord`、`CanonicalDocument`、`ChunkRecord` 都必须携带同一套 provenance typed 字段：`version`、`updated_at_ms`、`authority_level`、`source_uri`，避免 chunk 阶段丢失来源语义。
4. `SourceScanDelta` 必须显式携带 `full_scan`，不能仅靠“空 inventory”隐式推断。

### 3.3 metadata / provenance 规则

| 对象 | 必填 typed 字段 | 允许 fallback 的字段 | 规则 |
|---|---|---|---|
| `CorpusDescriptor` | `corpus_id`、`source_uri`、`trust_level`、`authority_level`、`source_kind`、`allowed_formats`、`include_globs`、`supported_modes` | `display_name`、`tags`、`metadata` | `metadata` 只承载扩展键，不能替代 typed 必填字段 |
| `SourceRecord` | `source_id`、`corpus_id`、`source_uri`、`content_hash`、`version`、`updated_at_ms`、`kind`、`format`、`authority_level` | `language`、`tags` | `language` 缺失时回退 `und` 并写 warning；其余缺失直接 quarantine |
| `CanonicalDocument` | `document_id`、`corpus_id`、`source_id`、`source_uri`、`canonical_text`、`source_hash`、`version`、`updated_at_ms`、`source_format`、`authority_level` | `title`、`language`、`tags` | `title` 可回退到文件名/一级标题；空正文不可回退 |
| `ChunkRecord` | `chunk_id`、`document_id`、`corpus_id`、`source_id`、`source_uri`、`chunk_text`、`citation_ref`、`version`、`updated_at_ms`、`authority_level` | `language`、`tags`、`adjacent_chunk_refs` | provenance 字段必须从 `CanonicalDocument` 继承，不允许在 chunk 阶段缺失 |

扩展 metadata keyspace 约束：

1. `CorpusDescriptor.metadata` v1 必填 `baseline_class`、`owner_module`、`refresh_strategy`、`default_language`。
2. markdown corpus 的 `CanonicalDocument.metadata` 必填 `document_class`、`section_path`。
3. `profile_policy_normative` 的 `CanonicalDocument.metadata` 额外必填 `profile_name`、`policy_domain=runtime_policy`。
4. `ChunkRecord.metadata` 至少保留 `document_class` 与 `section_path`，用于 citation 和 router/tag filtering。

### 3.4 canonicalize 规则

1. markdown 文档只做编码、换行、heading/list/code fence 展平，不做语义改写。
2. `runtime_policy.yaml` 以稳定 key-path flatten 方式 canonicalize，例如 `runtime_budget.max_latency_ms=7000`；key 顺序按字典序固定，以保证稳定 `document_id` / `chunk_id`。
3. `version` 的生成顺序固定为：
   - 如果源内存在显式版本元信息，优先使用；
   - 否则回退到基于规范化正文的内容摘要版本。
4. `updated_at_ms` 优先使用源显式更新时间；不存在时回退到仓库文件时间戳或扫描快照时间。

### 3.5 quarantine / fail-closed 规则

| 触发条件 | 处置 | 说明 |
|---|---|---|
| source 不属于任何已注册 `corpus_id`，或 corpus `trust_level < Trusted` | quarantine source | 防止任意目录遍历与未注册资产注入 |
| 文件格式不在 corpus `allowed_formats` 中 | quarantine source | v1 只接受 markdown 与 runtime policy yaml |
| `content_hash` / `version` / `updated_at_ms` / `authority_level` 无法生成 | quarantine source | provenance 不完整时禁止进入 canonicalize |
| canonicalize 后正文为空、markup/yaml flatten 失败 | quarantine source | 禁止生成半有效文档 |
| `ChunkPolicy` 非法或 Build 配置冲突 | fail-closed batch | 这是系统配置错误，不属于单源 quarantine |

## 4. Design -> Build 映射

| 后续任务 | Build 入口 | 本次冻结给出的前置结论 |
|---|---|---|
| KNO-TODO-016 `CorpusCatalog` | `knowledge/include/index/CorpusCatalog.h`、`knowledge/src/index/CorpusCatalog.cpp` | `CorpusDescriptor` 已具备 authority/source/glob/format 字段，可直接承载 route + ingest 共同视图 |
| KNO-TODO-021 `SourceScanner` | `knowledge/include/ingest/SourceScanner.h`、`knowledge/src/ingest/SourceScanner.cpp` | 已固定四类首批 corpus、`CorpusScanPlan`、`allowed_formats`、`full_scan` 与 quarantine 规则 |
| KNO-TODO-022 `Canonicalizer` | `knowledge/include/ingest/Canonicalizer.h`、`knowledge/src/ingest/Canonicalizer.cpp` | markdown / runtime policy yaml 的 canonicalize 口径、metadata fallback 与 quarantine 语义已固定 |
| KNO-TODO-023 `Chunker` | `knowledge/include/ingest/Chunker.h`、`knowledge/src/ingest/Chunker.cpp` | `ChunkRecord` 必填 provenance 字段和 `adjacent_chunk_refs` 继承规则已固定 |
| KNO-TODO-024 `IngestionCoordinator` | `knowledge/include/ingest/IngestionCoordinator.h`、`knowledge/src/ingest/IngestionCoordinator.cpp` | scan -> canonicalize -> chunk 的输入资产与 warning/quarantine 基线已统一 |
| KNO-TODO-032 `KnowledgeServiceFacade` 完整版 | `knowledge/src/facade/KnowledgeService.cpp` | refresh 真实编排可安全依赖 ingest baseline，不再需要用“测试语料夹具”掩盖 corpus 语义缺口 |
| KNO-TODO-033 refresh 闭环集成 | `tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp` | 真实 corpus snapshot 与 quarantine 规则已具备单一 SSOT，可验证 refresh -> swap -> retrieve 闭环 |

## 5. 本任务三件套

- 代码目标：更新 knowledge 详设与专项 TODO，冻结首批 corpus baseline、typed provenance 字段、profile snapshot canonicalize 规则与 quarantine 语义。
- 测试目标：确保 corpus baseline、`AuthorityLevel` / `SourceFormat`、`CorpusScanPlan`、quarantine 条件都能从文档中直接检索。
- 验收命令：

```bash
rg -n "AuthorityLevel|SourceFormat|CorpusScanPlan|architecture_reference|profile_policy_normative|quarantine|updated_at_ms|source_format" \
  docs/architecture/DASALL_knowledge子系统详细设计.md \
  docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md \
  docs/todos/knowledge/deliverables/KNO-TODO-003-corpus资产与metadata-trust基线设计收敛.md
```

## 6. 风险与回退

1. 风险：如果后续把 `docs/todos/`、`docs/worklog/` 等过程文档直接并入 Trusted corpus，retrieval evidence 将被高波动文本污染。
   - 处置：继续保持这些目录在 v1 baseline 之外；若未来需要纳入，只能走 `AuthorityLevel::Advisory` 单独评审。
2. 风险：profile YAML 同时是配置与 evidence 来源，容易被误读成“Knowledge 可以直接读 YAML 配置”。
   - 处置：在设计中显式保留 `KnowledgeConfigProjector` owner；profile corpus 仅供 retrieval 引用。
3. 风险：如果 provenance 字段仍停留在自由文本 metadata，`Chunker` / `VersionLedger` / `RefreshLoop` 在 Build 期会出现字段丢失与 schema 漂移。
   - 处置：把 `authority_level`、`source_format`、`version`、`updated_at_ms` 固定为 typed 字段。
4. 回退策略：若真实资产接线暂时不能一次完成，可继续使用与本设计同 schema 的测试 corpus fixtures；禁止重新打开 corpus 范围和 metadata keyspace 争论。

## 7. 收敛结论

1. 首批 corpus baseline 已冻结为 architecture / ADR / SSOT / profile runtime policy snapshots 四类真实仓库资产。
2. `AuthorityLevel`、`SourceKind`、`SourceFormat`、`CorpusScanPlan` 与 provenance typed 字段已补齐，`SourceScanner` / `Canonicalizer` / `Chunker` / `CorpusCatalog` 可以直接进入 Build 准备期。
3. `KNO-BLK-003` 可以关闭；后续剩余工作属于 ingest/index 实现与真实资产接线，不再是“先决定 ingest 到底吃什么”的设计争论。
