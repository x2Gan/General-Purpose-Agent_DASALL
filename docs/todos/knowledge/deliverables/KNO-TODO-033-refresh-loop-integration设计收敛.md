# KNO-TODO-033 refresh loop integration 设计收敛

- 日期：2026-04-22
- 任务：KNO-TODO-033
- 状态：已完成
- 对应 Gate：QG-K08

## 1. 输入与约束

1. `KNO-TODO-032` 已把 `KnowledgeServiceFacade::request_refresh()` 收敛为 real component owner + seam 共存的组合根，并在 `run_real_refresh()` 中固定为 `IngestionCoordinator::build_update_batch()` 后接 `IndexWriter::apply_update_batch()`。
2. `KNO-TODO-020` 已冻结 `IndexWriter` 的 last-known-good rollback 语义：若 `mark_active()` 失败，active snapshot 必须回退到前一版，不能污染读路径。
3. 033 的缺口不是补新生产能力，而是把 `request_refresh -> ingest -> shadow build -> snapshot swap -> retrieve` 闭环拉到 integration 层，同时把 busy reject 与 rollback 语义放进同一验收入口。
4. ADR-006 / ADR-008 继续约束 033 只验证 knowledge 子系统 refresh/retrieve 一致性，不能把 runtime 调度、answer synthesis 或跨子系统恢复裁定混入同一任务。

## 2. 本地与外部证据

### 2.1 本地证据

1. `knowledge/src/facade/KnowledgeService.cpp` 已明确：`request_refresh()` 先以 `refresh_in_flight_.exchange(true)` 做 busy guard，再走 `run_real_refresh()`，因此 busy reject 可通过并发 integration harness 直接二值判定。
2. `tests/unit/knowledge/KnowledgeServiceFacadeRealRefreshTest.cpp` 已证明真实 `IngestionCoordinator + IndexWriter` owner 链可以在临时目录内完成 refresh 并产生活跃 snapshot。
3. `tests/unit/knowledge/IndexWriterRecoveryTest.cpp` 已证明第二次激活失败时，`IndexWriter` 会保留 last-known-good snapshot，不会把 candidate snapshot 留在读路径上。
4. `tests/integration/knowledge/KnowledgeRetrievalSmokeTest.cpp` 已提供真实 lexical retrieve 组合根，说明 033 只需在同类 harness 上接入 real refresh owner，而不需要改 retrieval 生产逻辑。

### 2.2 外部参考

1. Algolia《Data synchronization strategies》指出 full reindex 应通过原子替换活跃索引来保证 search availability；这与 033 要验证的 `shadow build -> snapshot swap -> next retrieve observes new data` 模式一致。

## 3. 边界与职责

### 3.1 任务边界

033 只完成以下内容：

1. 新增 `KnowledgeRefreshLoopTest.cpp` integration harness。
2. 在真实 refresh owner 链上验证成功闭环、busy reject 和 swap failure rollback。
3. 注册对应 integration target，并通过定向验收命令。

033 不负责以下内容：

1. 不修改 `KnowledgeServiceFacade`、`IngestionCoordinator`、`IndexWriter`、`IndexReader` 的生产行为。
2. 不扩展到 quality aggregate gate；那是 031 的职责。
3. 不引入新的 refresh queue、异步 worker 或 inventory 持久化实现。

### 3.2 组件职责分解

1. integration harness：持有临时 corpus 根目录、真实 retrieval 组件、真实 refresh owner 与最小 inventory 镜像。
2. `refresh_catalog` hook：只负责在 refresh 成功后更新 `CorpusCatalog.active_snapshot_id` 与测试内 inventory 镜像；不改变 writer 的 swap/rollback 语义。
3. busy 测试注入点：仅在 `refresh_catalog` 阶段阻塞首个 refresh，使第二次 `request_refresh()` 能稳定命中 facade busy guard。
4. rollback 测试注入点：仅通过 `IndexWriterDeps.mark_active` 失败注入，验证 last-known-good snapshot 仍是读路径唯一来源。

## 4. 数据与接口说明

### 4.1 集成 harness 最小模型

1. corpus：临时目录下单 corpus、单文档 markdown 文件。
2. retrieval query：使用唯一 token 区分 baseline 内容与 refreshed 内容，避免词项歧义。
3. inventory：测试内 `std::vector<SourceRecord>`，成功 refresh 后由 `SourceScanner` 全量重建，用于下一轮增量 `updated_sources`/`removed_sources` 判定。

### 4.2 关键断言

1. 成功闭环：第二次 refresh 接受后，下一次 `retrieve()` 必须命中新 token，且旧 token 不再出现在 active snapshot 中。
2. busy reject：当首个真实 refresh 仍在进行时，第二次 `request_refresh()` 必须返回 `RefreshStatus::Busy`。
3. rollback：当第二次激活失败时，refresh 结果必须为 `Failed`，且 retrieve/search 仍只能看见上一版 snapshot。

## 5. 流程与时序

1. 初始化临时 corpus 根目录、`CorpusCatalog`、`IndexReader`、`VersionLedger`、`IngestionCoordinator`、`IndexWriter`、`QueryNormalizer`、`CorpusRouter`、`RecallCoordinator`、`Reranker`、`EvidenceAssembler` 与 `KnowledgeServiceFacade`。
2. 写入 baseline 文档并执行首次 refresh，建立 last-known-good snapshot。
3. 成功闭环用例：
   - 更新文档内容；
   - 调用 `request_refresh(updated_sources)`；
   - 随后调用 `retrieve()`，断言 refreshed token 可见。
4. busy 用例：
   - 在 `refresh_catalog` 中阻塞首个真实 refresh；
   - 并发发起第二次 `request_refresh()`；
   - 断言返回 `Busy`；
   - 放行首个 refresh 并收尾。
5. rollback 用例：
   - 在已有 active snapshot 基础上更新文档；
   - 注入下一次 `mark_active()` 失败；
   - 调用 `request_refresh(updated_sources)`，断言返回 `Failed`；
   - 通过 retrieve/search 断言 last-known-good 仍可读、candidate token 不可见。

## 6. 文件范围

1. `docs/todos/knowledge/deliverables/KNO-TODO-033-refresh-loop-integration设计收敛.md`
2. `tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp`
3. `tests/integration/knowledge/CMakeLists.txt`
4. `docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md`
5. `docs/worklog/DASALL_开发执行记录.md`

## 7. Design -> Build 映射

| Build 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| B1 | 新增真实 refresh/retrieve integration harness 与临时 corpus fixture | refresh 成功后 next retrieve 命中新 token，旧 token 从 active snapshot 消失 | `ctest --test-dir build-ci -R KnowledgeRefreshLoopTest --output-on-failure` |
| B2 | 在 harness 中加入 busy 阻塞与 `mark_active` 失败注入 | busy 返回 `RefreshStatus::Busy`；swap failure 后 last-known-good 仍可读 | `ctest --test-dir build-ci -R KnowledgeRefreshLoopTest --output-on-failure` |
| B3 | 注册 `dasall_knowledge_refresh_loop_integration_test` | target 可被定向构建与 `ctest -R` 发现 | `cmake --build build-ci --target dasall_knowledge_refresh_loop_integration_test && ctest --test-dir build-ci -R KnowledgeRefreshLoopTest --output-on-failure` |

## 8. Build 三件套

- 代码目标：实现 `tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp` 与对应 integration CMake 注册。
- 测试目标：至少覆盖 1 条 refresh 成功闭环、1 条 busy reject、1 条 swap failure rollback。
- 验收命令：

```bash
cmake -S . -B build-ci -G "Unix Makefiles"
cmake --build build-ci --target dasall_knowledge_refresh_loop_integration_test
ctest --test-dir build-ci -R KnowledgeRefreshLoopTest --output-on-failure
```

## 9. 风险与回退

1. 风险：若测试不维护上一轮 inventory，更新文档会被错误识别为新增文档，导致旧 chunk 未被删除。
   - 处置：在 refresh 成功后用 `SourceScanner` 重建 inventory 镜像，供下一轮增量 refresh 使用。
2. 风险：busy 用例若依赖 `sleep`，会引入脆弱竞争条件。
   - 处置：使用条件变量在 `refresh_catalog` 阶段做确定性阻塞与放行。
3. 风险：rollback 用例若只看 refresh 返回值，无法证明 active snapshot 未污染。
   - 处置：失败后同时断言 last-known-good token 仍可 retrieve/search，而 candidate token 不可见。
4. 回退策略：若 retrieve 对空结果的 envelope 断言不稳定，可对“candidate token 不可见”退回 `IndexReader::search_sparse()` 级别验证，但成功闭环仍必须经 `IKnowledgeService::retrieve()` 验证。

## 10. D Gate

### 10.1 通过条件

1. 033 的实现边界已限定为 integration harness，不扩张到生产逻辑修改。
2. 成功闭环、busy reject、swap failure rollback 三类断言都已有稳定注入点。
3. Build 三件套已经锁定。

### 10.2 结论

`D Gate = PASS`

033 可以进入 Build 阶段，且首选最小实现是“真实 refresh/retrieve 组合根 + inventory 镜像 + 条件变量阻塞 + `mark_active` 失败注入”的单入口 integration harness。

## 11. Build 结果

1. 已新增 `tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp`，落地真实 `KnowledgeServiceFacade + IngestionCoordinator + IndexWriter + IndexReader + QueryNormalizer + CorpusRouter + RecallCoordinator + Reranker + EvidenceAssembler` integration harness，并通过测试内 inventory 镜像保证更新文档会走 `updated_sources` 路径而不是重复新增。
2. 成功闭环已自动验证：第二次 `request_refresh(updated_sources)` 接受后，新的 active snapshot id 生效，后续 `retrieve()` 能命中 refreshed token，旧 token 会从 active snapshot 消失。
3. busy reject 已自动验证：在 `refresh_catalog` 阶段阻塞首个真实 refresh 时，第二次 `request_refresh()` 会稳定返回 `RefreshStatus::Busy`。
4. rollback 已自动验证：对下一次 `mark_active()` 注入失败后，refresh 返回 `Failed`，且 last-known-good snapshot 继续提供旧 token，candidate token 不会污染读路径。
5. 定向验证结果：
   - `Build_CMakeTools` 构建 `dasall_knowledge_refresh_loop_integration_test` 通过；
   - `RunCtest_CMakeTools` 对 `KnowledgeRefreshLoopTest` 仍返回仓库已知工具态错误 `生成失败`；
   - 回退到 `build-ci` 后，`ctest --test-dir build-ci -R KnowledgeRefreshLoopTest --output-on-failure` 1/1 Passed。
6. 聚合验收补充：`cmake --build build-ci --target dasall_integration_tests` 中 `KnowledgeRefreshLoopTest` 已 Passed，但 aggregate target 仍被仓库既有 `InfraDiagnosticsSmokeTest` 与 `InfraDiagnosticsIntegrationTest` 失败拖住，不作为 033 缺陷信号。