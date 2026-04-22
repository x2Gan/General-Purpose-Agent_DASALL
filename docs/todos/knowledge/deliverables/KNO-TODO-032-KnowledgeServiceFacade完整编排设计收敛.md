# KNO-TODO-032 KnowledgeServiceFacade 完整编排设计收敛

## 1. 输入与目标

1. 来源任务：`KNO-TODO-032 | 补全 KnowledgeServiceFacade 完整编排（hybrid/ingest/health）`。
2. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.6、6.10、6.13.1 `KnowledgeServiceFacade` 卡片，及专项 TODO 对 032 的交付要求。
3. 前置满足：`KNO-TODO-012` 已冻结 facade 骨架与 fail-closed 语义，`KNO-TODO-013/014/015/019/020/024/026` 已分别提供 sparse recall、hybrid recall orchestration、rerank、IndexReader、IndexWriter、IngestionCoordinator、KnowledgeHealthProbe。
4. 本轮目标：在不破坏 012 既有 unit/integration seam 注入模式的前提下，把 facade 从 callback skeleton 升级为可直接持有真实组件的组合根，补齐三条缺口：
   - `retrieve()` 可在 seam 缺失时自动走真实 hybrid recall 链；
   - `request_refresh()` 可真实串起 `IngestionCoordinator -> IndexWriter`；
   - `health_snapshot()` 可真实接入 `KnowledgeHealthProbe` 聚合结果。

## 2. 研究证据

### 2.1 本地证据

1. `knowledge/src/facade/KnowledgeService.h/.cpp` 当前仍以 function seams 为主；`retrieve()` 主链已完整，`request_refresh()` 与 `health_snapshot()` 仍是 stub delegation。
2. `tests/unit/knowledge/KnowledgeServiceFacade*.cpp` 现有回归均通过 lambda seam 驱动 facade，证明 012 的可测试性依赖不能被 032 直接打破。
3. `tests/integration/knowledge/KnowledgeRetrievalSmokeTest.cpp` 与 `KnowledgeFailureDegradeTest.cpp` 已证明“真实组件 + lambda 包装”模式可行，说明 032 的最小升级面应位于 facade 组合根，而不是推翻 supporting objects。
4. `knowledge/include/health/KnowledgeHealthProbe.h` 已冻结健康分类规则；032 的职责是接线，不是重写健康语义。
5. `knowledge/include/index/IndexWriter.h` 与 `knowledge/include/ingest/IngestionCoordinator.h` 已提供真实 snapshot write path 与 ingestion batch build path，032 可直接复用，不必再引入 mock refresh pipeline。

### 2.2 外部参考

1. Martin Fowler, *Inversion of Control Containers and the Dependency Injection pattern*：强调“separating configuration from use”与 constructor injection 可让依赖关系显式可见，适合作为 facade 组合根升级方向。
2. Microsoft Azure Architecture Center, *Health Endpoint Monitoring pattern*：强调健康检查应聚合依赖组件状态，而不是只回 status code；也强调可缓存/复用已有 instrumentation，支持 032 直接复用 `KnowledgeHealthProbe` 而不在 facade 内重复实现健康分类。

## 3. 设计结论

### 3.1 边界与职责

1. `KnowledgeServiceFacade` 继续是 `IKnowledgeService` 的唯一实现，并负责 knowledge 子系统对 Runtime 的单一入口。
2. 032 后 facade 负责两层职责：
   - 保持 012 已有 lifecycle、deadline budget、fail-closed、degraded 传播与 telemetry 语义；
   - 作为 module-local composition root，持有真实 supporting components，并在 seam 缺失时绑定默认执行路径。
3. 032 不负责：
   - 引入后台 refresh worker、任务队列或 runtime recovery policy；
   - 改写 `RecallCoordinator`、`KnowledgeHealthProbe`、`IndexWriter` 既有语义；
   - 把 facade 提升为 public header 或在 032 内新增对外 ABI。

### 3.2 依赖注入策略

1. `KnowledgeServiceDeps` 从“纯 function seams”升级为“real component ownership + function seams 共存”：
   - 保留现有 function seams，作为测试优先覆盖入口；
   - 新增可选的 concrete owner，例如 `QueryNormalizer`、`CorpusRouter`、`IndexReader`、`FreshnessController`、`RecallCoordinator`、`Reranker`、`EvidenceAssembler`、`IngestionCoordinator`、`IndexWriter`、`KnowledgeHealthProbe`；
   - constructor 内执行一次默认绑定：只有在对应 seam 缺失时，才把 concrete owner 绑定为默认 callable。
2. 这样做的理由：
   - 满足详细设计对 facade 组合根的要求；
   - 保持现有 unit/integration tests 无需整体重写为工厂或 service locator；
   - 让“真实组件装配”和“可控 seam 覆盖”同时成立，便于 033 做端到端 refresh 闭环。
3. 由于 `KnowledgeServiceDeps` 新增 `unique_ptr` concrete owner，它将自然转为 move-only；现有唯一 lvalue 构造点应同步改成 `std::move(deps)`。

### 3.3 retrieve 完整编排

1. `retrieve()` 的主顺序保持 012 不变：lifecycle/config -> deadline -> normalize -> manifest/freshness -> route -> recall -> rerank -> evidence -> telemetry。
2. 032 的核心升级不是改顺序，而是让以下 seam 在缺失时自动绑定真实组件：
   - `normalize_query` -> `QueryNormalizer::normalize`
   - `catalog_snapshot` -> `CorpusCatalog::snapshot`
   - `current_manifest` -> `IndexReader::current_manifest`
   - `evaluate_freshness` -> `FreshnessController::evaluate`
   - `build_plan` -> `CorpusRouter::build_plan`
   - `recall` -> `RecallCoordinator::recall`
   - `rerank` -> `Reranker::rerank`
   - `assemble_evidence` -> `EvidenceAssembler::assemble`
3. 012 既有错误映射与 degraded 语义全部保留，032 只更换执行来源，不改变 public result shape。

### 3.4 request_refresh 真实 ingest 编排

1. `request_refresh()` 保留单飞 busy guard：同一时刻只允许一个 refresh 在途。
2. 若显式提供 `request_refresh` seam，则继续以 seam 为最高优先级，保证既有测试可精确控制 busy/failure path。
3. 若 seam 缺失但 `IngestionCoordinator` 与 `IndexWriter` 已注入，则执行同步 refresh：
   - `build_update_batch(changes)`
   - `apply_update_batch(batch)`
   - 成功时返回 `RefreshStatus::Accepted`，`refresh_id` 使用 batch 维度 id；
   - 失败时透传 `IndexWriter` 错误或包装成 `RefreshStatus::Failed`。
4. 若既无 seam 又无真实 refresh 组件，则保持 012 的兼容返回：`RefreshStatus::Busy`，避免旧调用方从“占位 stub”突变为 hard failure。
5. 本轮明确不引入异步 worker、后台队列或恢复编排；这些控制权仍留给 runtime/recovery 边界。

### 3.5 health_snapshot 完整接线

1. `health_snapshot()` 优先走 `collect_health_snapshot` seam。
2. 若 seam 缺失但 `KnowledgeHealthProbe` 已注入，则默认绑定到 `KnowledgeHealthProbe::collect()`。
3. 若两者都缺失，仍返回 `Unknown + health_probe_unavailable`，保持 012 fail-closed 语义。
4. facade 不重复实现健康分类；健康语义唯一来源仍是 `KnowledgeHealthProbe`。

## 4. Design -> Build 映射

1. `knowledge/src/facade/KnowledgeService.h`
   - 为 `KnowledgeServiceDeps` 增加 real component ownership；
   - 保持 function seams；
   - 维持 facade module-local 可见性。
2. `knowledge/src/facade/KnowledgeService.cpp`
   - 在 constructor 中实现默认 seam 绑定；
   - 将 `request_refresh()` 从 stub delegation 升级为真实 ingest/index orchestration；
   - 将 `health_snapshot()` 从 stub delegation 升级为真实 probe 接线。
3. `tests/unit/knowledge/KnowledgeServiceFacadeHybridRecallTest.cpp`
   - 只注入真实 component owners，验证 facade 可在无 lambda seams 情况下跑通 hybrid retrieve。
4. `tests/unit/knowledge/KnowledgeServiceFacadeRealRefreshTest.cpp`
   - 以真实 `IngestionCoordinator + IndexWriter + IndexReader` 验证 `request_refresh()` 能生成 batch 并切换 active snapshot。
5. `tests/unit/knowledge/KnowledgeServiceFacadeFullHealthSnapshotTest.cpp`
   - 以真实 `KnowledgeHealthProbe` owner 验证 facade 输出完整健康快照，而不是 `health_probe_unavailable` stub。
6. `tests/unit/knowledge/CMakeLists.txt`
   - 注册上述 3 个 unit tests，并补齐 `dasall_sqlite3` 链接。

## 5. 验证计划

1. build-ci configure：`cmake -S . -B build-ci -G "Unix Makefiles"`
2. 定向构建：
   - `cmake --build build-ci --target dasall_knowledge dasall_knowledge_service_facade_hybrid_recall_unit_test dasall_knowledge_service_facade_real_refresh_unit_test dasall_knowledge_service_facade_full_health_snapshot_unit_test`
3. 定向 `ctest`：
   - `ctest --test-dir build-ci -R "KnowledgeServiceFacade.*(Hybrid|RealRefresh|FullHealth).*Test" --output-on-failure`
4. 若 CMake Tools 测试入口继续出现通用 `生成失败`，按仓库既有回退链仅以 build-ci 命令链作为 032 的主验证证据，并在 worklog 中记录工具态问题。

## 6. 完成判定

1. facade 可在不提供 lambda seams 的情况下，直接通过 real component owners 跑通 hybrid retrieve。
2. `request_refresh()` 可真实串起 `IngestionCoordinator -> IndexWriter`，并在成功后产生新的 active snapshot。
3. `health_snapshot()` 可通过 `KnowledgeHealthProbe` 输出真实聚合状态，不再默认落入 `health_probe_unavailable` stub。
4. 012 既有 fail-closed、degraded 与测试 seam 兼容性不被破坏。
5. 032 完成后，033 只需补端到端 integration 闭环，而不必再返工 facade 组合根。