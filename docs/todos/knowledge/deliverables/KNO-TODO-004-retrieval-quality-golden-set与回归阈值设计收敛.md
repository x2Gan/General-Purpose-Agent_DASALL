# KNO-TODO-004 retrieval quality golden set 与回归阈值设计收敛

- 日期：2026-04-21
- 任务：KNO-TODO-004
- 状态：已收敛
- 对应 Blocker：KNO-BLK-004

## 1. 输入与约束

1. `KNO-TODO-030` 的目标是把 `RetrievalQualityRegressionTest` 注册为硬 Gate；如果 golden set 文件格式、样本覆盖下限、绝对阈值和 hard-fail 规则没有先冻结，030 最终只会得到“会跑但没有二值约束力”的测试壳。
2. 当前 knowledge 详设 9.1 只有 `query_text + expected_doc_ids + min_recall@k + min_mrr + baseline 下降 5% fail` 的宽泛描述，仍不足以回答 030 实现真正需要的四个问题：
   - 资产文件到底落成什么格式；
   - gate 以 source-level、chunk-level 还是 answer-level 为统计粒度；
   - coverage 到底如何覆盖 `FactLookup` / `ProcedureLookup` / `DiagnosticContext` 与不同 corpus；
   - `Context Precision` / `Context Recall` / `Faithfulness` 是 v1 硬门禁还是 schema 预留。
3. ADR-006 / ADR-008 已经要求 Knowledge 只负责“检索与产证据”，不负责 answer synthesis 与主控裁定。因此 004 的质量门必须优先围绕 retrieval 结果本身建立，而不是把 LLM 输出质量混进同一个 Gate。
4. `KNO-TODO-003` 已冻结 `source_uri`、`citation_ref`、`version`、`updated_at_ms` 等 typed provenance；004 必须复用这些稳定字段，不能再把 golden set 锚定到未来可能变化的 opaque hash id。

## 2. 本地证据

| 证据 | 观察结果 | 结论 |
|---|---|---|
| knowledge 详设 §9.1 | 已出现 `MRR/NDCG@k/Recall@k` 与“baseline 下降 5% fail”的原则描述，但没有固定 `k`、manifest schema、coverage floor 和 hard-fail 语义 | 004 需要把质量门从“原则”收敛到“可执行 schema + 数字阈值” |
| 专项 TODO `KNO-TODO-030` | 已要求 `tests/integration/knowledge/golden/` 与 regression gate，但仍未定义资产文件格式 | 030 在 004 完成前只能保持 Blocked |
| 工作区当前测试目录 | 尚不存在 `tests/integration/knowledge/golden/` 基线资产 | 004 必须先冻结资产格式与目录，再交给 030 落盘真实样本 |
| `profiles/src/ProfileYamlParser.cpp` | 仓库已存在轻量 YAML 解析模式，支持标量、map、list-of-scalars 的简单结构 | v1 golden manifest 采用 YAML 可避免额外引入外部 JSON / evaluation 框架依赖 |
| `KNO-TODO-003` 设计结论 | `source_uri` / `citation_ref` 已冻结为 provenance 字段，profile YAML 也具备稳定 key-path flatten 规则 | 004 可把 gate 锚定到 `source_uri` 和 `citation_ref`，不依赖不透明的 document hash |

## 3. 设计结论

### 3.1 文件格式与目录布局

1. v1 golden baseline 统一落成单文件 manifest：`tests/integration/knowledge/golden/retrieval_quality_v1.yaml`。
2. 不拆成多个 CSV / Markdown / ad hoc text 文件；所有 coverage、threshold、baseline metrics 和 cases 都以同一 manifest 管理，避免 030 在目录扫描与跨文件一致性校验上返工。
3. manifest 只允许使用轻量 YAML 结构：标量、嵌套 map 和 list-of-scalars。禁止 list-of-maps 或需要外部 schema 引擎才能解析的复杂结构，确保 030 可以用仓库现有 YAML 解析风格实现。
4. v1 quality gate 统计粒度固定为 source-level retrieval gate，不把 answer generation 混入同一个 Gate；chunk-level / context-level 信息只作为未来扩展槽位保留。

### 3.2 manifest schema 冻结

```yaml
format_version: 1
dataset_id: knowledge_retrieval_quality_v1
retrieval_top_k: 10
aggregate_thresholds:
  min_mrr_at_10: 0.70
  min_ndcg_at_10: 0.82
  min_recall_at_5: 0.80
  min_recall_at_10: 0.90
  max_regression_pct: 5
baseline_metrics:
  mrr_at_10: 0.00
  ndcg_at_10: 0.00
  recall_at_5: 0.00
  recall_at_10: 0.00
coverage:
  min_total_cases: 30
  min_fact_lookup_cases: 10
  min_procedure_lookup_cases: 10
  min_diagnostic_context_cases: 10
  min_hard_fail_cases: 6
  min_architecture_reference_cases: 6
  min_adr_normative_cases: 8
  min_ssot_normative_cases: 8
  min_profile_policy_normative_cases: 4
context_metric_slots:
  enabled: false
  fields:
    - context_precision_reference
    - context_recall_reference
    - faithfulness_reference
cases:
  sample_case:
    query_kind: FactLookup
    query_text: "<query>"
    primary_corpus_id: adr_normative
    allowed_modes:
      - LexicalOnly
      - Hybrid
    expected_source_uris:
      - docs/adr/<doc>.md
      - docs/ssot/<doc>.md
    expected_chunk_refs:
      - <citation_ref>
    required_top_k: 5
    hard_fail: true
    min_recall_at_5: 1.0
    min_mrr_at_10: 1.0
    context_extensions:
      reference_answer: ""
      expected_claims:
        - "<claim>"
      supporting_chunk_refs:
        - <citation_ref>
```

schema 规则：

1. `format_version` 固定为 `1`；后续任何 breaking schema 变化都必须升版本，不允许“悄悄加字段”。
2. `expected_source_uris` 是 v1 唯一必填 relevance truth；其顺序代表 relevance 递减。
3. `expected_chunk_refs` 是可选字段，用于 future context-level 指标与更细粒度诊断；absence 不导致 v1 gate fail。
4. `baseline_metrics` 在 manifest 首次真正落盘时必须写入测得的 committed baseline；不允许把 `0.00` 占位值提交到真实 gate 资产中。
5. `allowed_modes` 用于标记 case 可参与 `LexicalOnly`、`Hybrid` 哪种检索模式；当前模式不在白名单内的 case 必须被标记 `skipped`，且不能计入通过样本数。

### 3.3 指标与阈值冻结

1. v1 aggregate quality threshold 固定为：
   - `MRR@10 >= 0.70`
   - `NDCG@10 >= 0.82`
   - `Recall@5 >= 0.80`
   - `Recall@10 >= 0.90`
2. relative regression 规则固定为：当前测得 aggregate 指标不得低于 committed `baseline_metrics` 的 95%。也就是说 gate 的真实阈值是：
   - `effective_threshold(metric) = max(absolute_threshold(metric), baseline_metrics(metric) * 0.95)`
3. 统计时必须先按 `source_uri` 去重，再计算 `MRR` / `NDCG` / `Recall`，避免同一 source 的多个 chunk 命中把指标虚高。
4. `expected_source_uris` 顺序用于 NDCG 的隐式 gain 计算：
   - 第 1 个期望 source 的 gain = 3
   - 第 2 个期望 source 的 gain = 2
   - 第 3 个及之后的 gain = 1
5. case 级 override 只允许收紧，不允许放宽 aggregate baseline。`min_recall_at_5`、`min_mrr_at_10` 主要用于 hard-fail case，帮助 030 在 aggregate 未失守前就拦截关键查询退化。

### 3.4 样本覆盖下限

1. 首批 dataset 至少 30 条，且 `FactLookup` / `ProcedureLookup` / `DiagnosticContext` 各至少 10 条。
2. 至少 6 条 `hard_fail=true` case，且每类 query kind 至少 2 条 hard-fail，避免只把关键查询集中在单一 query kind。
3. 按 `primary_corpus_id` 计数的最小覆盖：
   - `architecture_reference >= 6`
   - `adr_normative >= 8`
   - `ssot_normative >= 8`
   - `profile_policy_normative >= 4`
4. 单个 case 可以命中多个 corpus，但覆盖统计只看 `primary_corpus_id`，防止通过“多标注”虚增 coverage。
5. `profile_policy_normative` case 必须至少覆盖 2 条 `ProcedureLookup` 与 2 条 `DiagnosticContext`，确保 runtime policy corpus 不被退化成只做静态 fact lookup 的装饰性样本。

### 3.5 hard-fail 与 skipped 语义

1. `hard_fail=true` 的 case 必须满足：其第一个 `expected_source_uri` 出现在 `required_top_k` 内；否则无论 aggregate 指标是否仍高于阈值，都立即 fail。
2. `required_top_k` 默认允许取值 `3` 或 `5`；v1 不接受其他值，避免 harness 语义扩散。
3. 若当前检索模式不在 `allowed_modes` 中，则 case 记为 `skipped`，并且：
   - 不计入 aggregate 分母；
   - 不可用于满足 `coverage.min_total_cases`；
   - 必须在测试输出中显式列出 `skip_reason`。
4. `skipped` 只允许来源于 mode 不匹配；不允许把 parser 错误、缺字段或 retrieval runtime 错误伪装成 skipped。

### 3.6 context-level 扩展槽位

1. `context_metric_slots` 在 004 中只冻结 schema，不把 `Context Precision` / `Context Recall` / `Faithfulness` 设为 v1 硬门禁。
2. per-case 的 `context_extensions.reference_answer`、`expected_claims`、`supporting_chunk_refs` 全部允许为空；缺失时 030 只能报告 `not_evaluated`，不能把 case 判为失败。
3. future 如果要把这三项指标升级为 Gate，必须满足两个新增前置条件：
   - 存在稳定的 answer-level evaluator；
   - golden 资产已经为多数 case 补齐 `reference_answer` 与 `expected_claims`。
4. 030 在 v1 只能把 context-level 槽位作为诊断输出，不得让它们影响 aggregate pass/fail。

## 4. Design -> Build 映射

| 后续任务 | Build 入口 | 本次冻结给出的前置结论 |
|---|---|---|
| KNO-TODO-030 `RetrievalQualityRegressionTest` | `tests/integration/knowledge/RetrievalQualityRegressionTest.cpp`、`tests/integration/knowledge/golden/retrieval_quality_v1.yaml` | regression harness 固定读取 YAML manifest，以 `source_uri` 去重后的 `MRR@10` / `NDCG@10` / `Recall@5` / `Recall@10` 与 `hard_fail` case 作为硬门禁 |
| KNO-TODO-027 lexical smoke | `tests/integration/knowledge/KnowledgeRetrievalSmokeTest.cpp` | 027 产出的稳定 source-level retrieval 结果将成为 030 的前置输入链，quality gate 不再重新定义 retrieve 语义 |
| KNO-TODO-028 failure/degrade integration | `tests/integration/knowledge/KnowledgeFailureDegradeTest.cpp` | `allowed_modes` / `skipped` / degrade 语义必须与 028 中的 lexical-only / hybrid failure 语义保持一致 |
| KNO-TODO-011 `EvidenceAssembler` | `knowledge/include/evidence/EvidenceAssembler.h`、`knowledge/src/evidence/EvidenceAssembler.cpp` | 004 只在 source-level 设硬门；`expected_chunk_refs` 和 context slot 为 011 / future answer evaluator 预留，不阻断当前 evidence budget 实现 |
| KNO-TODO-031 证据回写 | `docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` | 030 完成后必须同时回写 aggregate metrics、skip case、hard-fail case 与 residual risk，不能只写“quality passed” |

## 5. 本任务三件套

- 代码目标：更新 knowledge 详设与专项 TODO，冻结 retrieval quality manifest、样本覆盖下限、aggregate 阈值、relative regression 公式、`hard_fail` 规则与 context-level 扩展槽位。
- 测试目标：确保 `retrieval_quality_v1.yaml`、`expected_source_uris`、`min_mrr_at_10`、`min_ndcg_at_10`、`min_recall_at_10`、`context_metric_slots`、`hard_fail` 和 `KNO-BLK-004` 都能从文档中直接检索。
- 验收命令：

```bash
rg -n "retrieval_quality_v1.yaml|expected_source_uris|min_mrr_at_10|min_ndcg_at_10|min_recall_at_10|context_metric_slots|hard_fail|KNO-BLK-004" \
  docs/architecture/DASALL_knowledge子系统详细设计.md \
  docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md \
  docs/todos/knowledge/deliverables/KNO-TODO-004-retrieval-quality-golden-set与回归阈值设计收敛.md
```

## 6. 风险与回退

1. 风险：如果 v1 直接把 answer synthesis 或 LLM evaluator 拉进 quality gate，会把检索问题和答案问题混在一起，导致 030 难以定位回归根因。
   - 处置：坚持 004 只冻结 source-level retrieval gate；answer-level 指标留在 context slot。
2. 风险：如果 manifest 采用复杂 schema（例如 list-of-maps 深层嵌套），030 将被迫引入新解析依赖或写复杂 parser。
   - 处置：保持 YAML 仅使用标量、map 和 list-of-scalars。
3. 风险：若 golden 资产只看 aggregate 平均值，没有 `hard_fail` case，关键 query 的严重退化可能被平均值掩盖。
   - 处置：至少 6 条 `hard_fail=true`，且每类 query kind 至少 2 条。
4. 风险：若后续 retrieval hit 不再暴露 `source_uri` / `citation_ref`，quality gate 将失去稳定对齐键。
   - 处置：继续沿用 003 冻结的 provenance 字段，禁止在 Build 期删改它们。
5. 回退策略：若 030 实现期暂时无法一次补齐真实 golden 资产，可先按本 schema 用最小 curated fixture author baseline，但禁止重新打开文件格式、统计粒度和阈值争论。

## 7. 收敛结论

1. retrieval quality gate 已冻结为 `tests/integration/knowledge/golden/retrieval_quality_v1.yaml` 驱动的 source-level gate，而不是 answer synthesis gate。
2. golden set 的样本覆盖下限、`MRR@10` / `NDCG@10` / `Recall@5` / `Recall@10` 阈值、95% relative regression 公式与 `hard_fail` case 规则已形成单一 SSOT。
3. `KNO-BLK-004` 可以关闭；后续剩余工作属于 golden 资产 authoring 与 regression harness 实现，不再是“quality gate 到底怎么定义”的设计争论。
