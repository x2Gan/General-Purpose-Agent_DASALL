# KNO-TODO-030 retrieval quality regression gate 设计收敛

- 日期：2026-04-22
- 任务：KNO-TODO-030
- 状态：已完成
- 对应 Gate：QG-K07

## 1. 输入与约束

1. `KNO-TODO-004` 已冻结 `tests/integration/knowledge/golden/retrieval_quality_v1.yaml` 的 schema、coverage floor、绝对阈值、95% relative regression 规则与 `hard_fail` 语义；030 不再重定义 baseline，只负责把它实现成真实 gate。
2. `KNO-TODO-027` 已打通 lexical retrieval smoke，`KNO-TODO-028` 已打通 failure/degrade integration，因此 030 的任务边界应保持在“质量回归评估”而不是“再造 retrieval 主链”。
3. ADR-006 / ADR-008 继续约束 030 只能评价 retrieval output 本身，不能把 answer synthesis、Prompt、Runtime 主控裁定或 ContextPacket 写权限混进同一个 gate。
4. 仓库当前没有通用 YAML 第三方依赖，但 `profiles/src/ProfileYamlParser.cpp` 已证明仓库接受轻量、固定 schema 的手写 YAML 解析风格，因此 030 应沿用同一策略，不为测试引入新依赖。

## 2. 本地与外部证据

### 2.1 本地证据

1. `tests/integration/knowledge/CMakeLists.txt` 已具备 knowledge integration 注册模式，可直接新增 `RetrievalQualityRegressionTest.cpp` target。
2. `tests/integration/knowledge/KnowledgeRetrievalSmokeTest.cpp` 已提供真实 `KnowledgeServiceFacade + QueryNormalizer + CorpusRouter + IndexReader + SparseRetriever + RecallCoordinator + Reranker + EvidenceAssembler` 的最小 lexical fixture，说明 030 可以复用同类 harness，而不需要再补生产代码。
3. `docs/todos/knowledge/deliverables/KNO-TODO-004-retrieval-quality-golden-set与回归阈值设计收敛.md` 已明确：v1 gate 使用 `source_uri` 去重后的 `MRR@10` / `NDCG@10` / `Recall@5` / `Recall@10`，并要求至少 30 条 case 与至少 6 条 `hard_fail` case。

### 2.2 外部参考

1. Pinecone《Evaluation Measures in Information Retrieval》确认 offline IR gate 常用 `Recall@K`、`MRR`、`NDCG@K` 作为部署前回归指标；其中 `MRR` 更关注第一个命中位置，`Recall@K` 负责覆盖率。
2. Evidently《Normalized Discounted Cumulative Gain (NDCG) explained》确认 `NDCG@K` 适合评价排序质量，且应基于理想排序归一化到 0~1 范围，便于做跨轮次回归比较。

## 3. 边界与职责

### 3.1 任务边界

030 只完成以下内容：

1. 新增 retrieval quality golden manifest 资产。
2. 新增 integration harness，解析 manifest、执行真实 retrieval、计算 aggregate metrics。
3. 将该 harness 注册进 knowledge integration CMake 拓扑并通过验收命令。

030 不负责以下内容：

1. 不修改 `KnowledgeServiceFacade`、`IndexReader`、`IndexWriter`、`IngestionCoordinator` 的生产语义。
2. 不把 answer-level 指标升级为硬 gate。
3. 不回写全量 Gate 证据；那是 031 的职责。

### 3.2 组件职责分解

1. golden manifest：承载 dataset metadata、aggregate threshold、baseline metrics、coverage 约束和 case truth。
2. regression harness：
   - 读取并校验 manifest；
   - 为每个 case 构造真实 `KnowledgeQuery`；
   - 运行 retrieval；
   - 基于去重后的 `source_uri` 序列计算 case metrics 与 aggregate metrics；
   - 执行 `hard_fail`、coverage、absolute threshold、relative regression 四类 gate。
3. integration registration：确保 `ctest -R RetrievalQualityRegressionTest` 可以发现并执行该 gate。

## 4. 数据与接口说明

### 4.1 manifest 顶层字段

1. `format_version`：固定为 `1`。
2. `dataset_id`：固定为当前 retrieval quality 数据集 id。
3. `retrieval_top_k`：固定为 `10`，对应 `MRR@10` / `NDCG@10` / `Recall@10`。
4. `aggregate_thresholds.*`：绝对阈值与最大回归百分比。
5. `baseline_metrics.*`：当前 committed baseline；gate 使用 `max(absolute, baseline*0.95)`。
6. `coverage.*`：最小总样本数、按 query kind 和 corpus 的覆盖下限。
7. `cases.<case_id>.*`：单条查询的真值与 hard-fail 规则。

### 4.2 harness 内部数据结构

1. `ParsedQualityManifest`：manifest 的只读内存视图。
2. `QualityCaseSpec`：单 case 的 query kind、query text、expected source URIs、expected chunk refs、allowed modes、hard-fail 参数。
3. `QualityCaseResult`：单 case 的 retrieved sources、去重序列、`MRR@10` / `NDCG@10` / `Recall@5` / `Recall@10`、skip/error/hard-fail 结果。
4. `QualityAggregateResult`：所有非 skipped case 的 aggregate metrics、coverage 统计、失败摘要。

### 4.3 接口约束

1. harness 只通过 `IKnowledgeService::retrieve()` 取 retrieval 结果，不跨层直接拼接指标输入。
2. `source_uri` 优先从 `EvidenceSlice.source_uri` 提取；若 retrieval 结果缺失 `source_uri`，case 直接记为错误而不是跳过。
3. `allowed_modes` 仅决定是否跳过 case；跳过不计入 aggregate 分母，也不能满足 coverage。

## 5. 流程与时序

1. 测试启动。
2. 读取 `retrieval_quality_v1.yaml` 并做 schema 校验。
3. 构造真实 retrieval harness 与 corpus fixture。
4. 逐 case：
   - 生成 `KnowledgeQuery`；
   - 调用 `IKnowledgeService::retrieve()`；
   - 从 evidence 中抽取按 rank 排序的 `source_uri`；
   - 执行 `source_uri` 去重；
   - 计算 case metrics；
   - 若 `hard_fail=true`，检查首个期望 source 是否进入 `required_top_k`；
   - 记录 pass/fail/skip/error。
5. 聚合所有非 skipped case 的 metrics。
6. 校验 coverage floor、absolute threshold、relative regression。
7. 任何一步失败即退出非零，作为 gate 失败信号。

## 6. 文件范围

1. `tests/integration/knowledge/RetrievalQualityRegressionTest.cpp`
2. `tests/integration/knowledge/golden/retrieval_quality_v1.yaml`
3. `tests/integration/knowledge/CMakeLists.txt`
4. `docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md`
5. `docs/worklog/DASALL_开发执行记录.md`

## 7. Design -> Build 映射

| Build 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| B1 | 新增 golden manifest 与最小 parser/loader | schema 可解析、case 数量与 coverage 下限可校验 | `ctest --test-dir build-ci -R RetrievalQualityRegressionTest --output-on-failure` |
| B2 | 新增真实 retrieval harness 与指标计算 | 正例：aggregate metrics 达标；负例：hard-fail 与 coverage 校验存在显式断言 | `ctest --test-dir build-ci -R RetrievalQualityRegressionTest --output-on-failure` |
| B3 | 注册 integration target | discoverability 可见，单测名可被 `ctest -N` 或定向 `ctest -R` 命中 | `cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R RetrievalQualityRegressionTest --output-on-failure` |

## 8. Build 三件套

- 代码目标：实现 `RetrievalQualityRegressionTest.cpp`、`retrieval_quality_v1.yaml` 与对应 integration CMake 注册。
- 测试目标：至少覆盖 1 条 aggregate 达标正例与 1 条 `hard_fail`/coverage 负向断言路径，且总数据集满足 30 条 case 下限。
- 验收命令：

```bash
cmake -S . -B build-ci -G "Unix Makefiles"
cmake --build build-ci --target dasall_integration_tests
ctest --test-dir build-ci -R RetrievalQualityRegressionTest --output-on-failure
```

## 9. 风险与回退

1. 风险：若直接复用 smoke test 的单条 fixture，case 数无法满足 30 条 coverage floor。
   - 处置：使用可扩展的内存 SQLite fixture，一次性 author 30+ 条受控 case 与 source truth。
2. 风险：若 YAML 解析器支持过宽，测试复杂度会反向吞噬 gate 价值。
   - 处置：只支持 004 已冻结的固定字段集与简单列表，未知字段直接忽略，缺失必填字段直接失败。
3. 风险：若 retrieval 结果中 `source_uri` 不稳定，metrics 会和 004 的 SSOT 脱节。
   - 处置：将 `source_uri` 缺失视为 case 错误，逼出上游契约回归。
4. 回退策略：若真实 30 条 curated case 在首轮实现中过大，可先以固定 corpus fixture author 完整 30 条最小样本，不扩张到真实仓库文档扫描；但不能降低 coverage floor 或删除 `hard_fail` 语义。

## 10. D Gate

### 10.1 通过条件

1. 030 的实现边界已限定为 integration gate，不扩张到生产组件修改。
2. manifest schema、aggregate metric、hard-fail、coverage、relative regression 规则已和 004 对齐。
3. Build 三件套已经锁定。

### 10.2 结论

`D Gate = PASS`

030 可以进入 Build 阶段，且首选最小实现是“固定 schema manifest + 专用 parser + 真实 retrieval harness + integration 注册”。

## 11. Build 结果

1. 已新增 `tests/integration/knowledge/golden/retrieval_quality_v1.yaml`，固化 30 条 curated retrieval regression cases、aggregate threshold、95% relative regression 规则与 `hard_fail` 约束。
2. 已新增 `tests/integration/knowledge/RetrievalQualityRegressionTest.cpp`，通过固定 schema YAML 解析、真实 lexical retrieval harness、`source_uri` 去重后的 `MRR@10` / `NDCG@10` / `Recall@5` / `Recall@10` 计算和 `hard_fail` 断言，完成 quality gate 落地。
3. 已更新 `tests/integration/knowledge/CMakeLists.txt`，注册 `dasall_knowledge_retrieval_quality_regression_integration_test` 并接入 `dasall_sqlite3` 与 `knowledge/src` include。
4. 定向验证结果：
   - `Build_CMakeTools` 构建 `dasall_knowledge_retrieval_quality_regression_integration_test` 通过；
   - `RunCtest_CMakeTools` 仍触发仓库已知工具态错误 `生成失败`；
   - 回退到 `build-ci` 后，`ctest --test-dir build-ci -R RetrievalQualityRegressionTest --output-on-failure` 1/1 Passed。
5. 聚合验收补充：`cmake --build build-ci --target dasall_integration_tests` 会连带执行整个 integration 套件；其中 `RetrievalQualityRegressionTest` 已 Passed，但 aggregate target 仍被仓库既有的 `InfraDiagnosticsSmokeTest` 与 `InfraDiagnosticsIntegrationTest` 失败拖住，不作为 030 缺陷信号。