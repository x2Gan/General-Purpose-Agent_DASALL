# KNO-TODO-012 KnowledgeServiceFacade 骨架设计收敛

## 1. 输入与目标

1. 来源任务：`KNO-TODO-012 | 实现 KnowledgeServiceFacade lexical-only 骨架编排`。
2. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.6、6.10、6.13.1 `KnowledgeServiceFacade` 卡片与 7 KNO-D01。
3. 前置满足：`KNO-TODO-008/009/010/011` 已形成 lexical query -> route -> recall -> rerank -> evidence 的 supporting chain，`KNO-TODO-014` 已补齐 recall orchestration，`KNO-TODO-017/025/026` 已提供 freshness / telemetry / health supporting objects。
4. 本轮目标：落盘 runtime-facing facade 骨架、deadline 传播、disabled/not-initialized/fail-closed 与 refresh busy guard；只做 lexical-only skeleton，不抢跑完整 hybrid / ingest / recovery 控制面。

## 2. 设计结论

### 2.1 边界与职责

1. `KnowledgeServiceFacade` 负责：
   - 作为 `IKnowledgeService` 唯一实现，串联 `init -> retrieve -> health_snapshot -> request_refresh`；
   - 统一执行 lifecycle 校验、config gating、deadline 预算切分与错误映射；
   - 把 supporting components 输出收口成单一 `KnowledgeRetrieveResult`。
2. `KnowledgeServiceFacade` 不负责：
   - 直接维护 lexical/vector index；
   - 直接管理 refresh worker 或恢复控制权；
   - 把 runtime 的 `ContextPacket` 拼装职责拉进 Knowledge。

### 2.2 依赖注入策略

1. 详细设计示意使用 `unique_ptr<Xxx>` concrete objects，但当前仓库多数 supporting components 尚无抽象接口。
2. 为保持 012 可测试且不引入额外 interface 冻结成本，本轮 `KnowledgeServiceDeps` 采用 function-based seams：
   - `normalize_query`
   - `catalog_snapshot`
   - `current_manifest`
   - `evaluate_freshness`
   - `build_plan`
   - `recall`
   - `rerank`
   - `assemble_evidence`
   - `collect_health_snapshot`
   - `request_refresh`
   - `emit_retrieve_event`
3. concrete facade 类放在 `knowledge/src/facade/KnowledgeService.h/.cpp`，保持 module-internal，不提前升格成 public header。

### 2.3 执行流与错误映射

1. `retrieve()` 固定顺序：
   - lifecycle / enablement 校验
   - deadline budget 切分
   - normalize
   - manifest + freshness
   - route
   - recall
   - rerank
   - evidence assemble
   - telemetry emit + wrap result
2. `compute_stage_budget()` 采用固定比例：
   - normalize + route：5%
   - sparse recall：35%
   - dense recall：35%
   - rerank + evidence：15%
   - telemetry + wrap：10%
3. fail-closed 规则：
   - 未 init：`NotInitialized`
   - `knowledge_enabled=false`：`Disabled`
   - supporting seam 缺失 / 返回不一致：`InternalError`
   - recall failure reason 含 `*_recall_timeout`：`RecallTimeout`
   - recall failure reason 含 `*_index_unavailable`：`IndexUnavailable`
   - recall failure reason 含 `dense_lane_unavailable`：`VectorBackendUnavailable`
4. degraded 传播规则：
   - `RecallCoordinatorResult.candidates.degraded` 或 `RankedHitSet.degraded` 任一为真，则最终 `EvidenceBundle.degraded=true`。

### 2.4 refresh / health 骨架

1. `health_snapshot()` 仅委托 `collect_health_snapshot` seam；若缺失则返回 `Unknown + health_probe_unavailable`。
2. `request_refresh()` 只实现 busy guard：
   - 同时只允许一个 refresh handler 在途；
   - 若缺少 handler，则返回 `RefreshStatus::Busy` 作为占位 stub；
   - 不在 012 内落盘真正的 ingest queue / worker 生命周期。

## 3. Design -> Build 映射

1. `knowledge/src/facade/KnowledgeService.h`
   - 定义 module-internal `LifecycleState`、`StageBudget`、`KnowledgeServiceDeps` 与 `KnowledgeServiceFacade`。
2. `knowledge/src/facade/KnowledgeService.cpp`
   - 实现 skeleton lifecycle、deadline budget、retrieve fail-closed、health stub 与 refresh busy guard。
3. `tests/unit/knowledge/KnowledgeServiceFacadeSmokeTest.cpp`
   - 验证 lexical-only 正常 retrieve 闭环。
4. `tests/unit/knowledge/KnowledgeServiceFacadeFailurePathTest.cpp`
   - 验证 not-initialized / disabled / normalize failure 三类 fail-closed 路径。
5. `tests/unit/knowledge/KnowledgeServiceFacadeDegradedModeTest.cpp`
   - 验证 degraded recall/rerank 能正确传播到最终 evidence。
6. `tests/unit/knowledge/KnowledgeServiceFacadeDeadlineBudgetTest.cpp`
   - 验证 budget 比例切分与 deadline exceeded fail-fast。

## 4. 验证计划

1. Build_CMakeTools：
   - `dasall_knowledge`
   - `dasall_knowledge_service_facade_smoke_unit_test`
   - `dasall_knowledge_service_facade_failure_path_unit_test`
   - `dasall_knowledge_service_facade_degraded_mode_unit_test`
   - `dasall_knowledge_service_facade_deadline_budget_unit_test`
2. build-ci 回退路径：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_knowledge dasall_knowledge_service_facade_smoke_unit_test dasall_knowledge_service_facade_failure_path_unit_test dasall_knowledge_service_facade_degraded_mode_unit_test dasall_knowledge_service_facade_deadline_budget_unit_test`
   - `ctest --test-dir build-ci -R "KnowledgeServiceFacade.*Test" --output-on-failure`

## 5. 完成判定

1. facade 骨架能完成 lifecycle 校验、deadline budget 传播、disabled/not-initialized/fail-closed 与 refresh busy guard。
2. lexical-only supporting chain 可被 facade 串起来并返回稳定 `KnowledgeRetrieveResult`。
3. 012 仍保持 function-seam 骨架，不承担 hybrid 完整编排、refresh worker 或恢复控制权。