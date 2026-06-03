# WP-MEM-GAP-007 external evidence projection closeout

来源任务：WP-MEM-GAP-007
关联缺口：GAP-P1-C / MEM-B06
完成日期：2026-06-03

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-007 / GAP-P1-C`，不把 ProductionLogging 字段断言补强、installed gate 绿色记录或后续 scoring / retention 演进项混入同一轮。
2. authoritative 问题定义固定为：runtime 是否已成为 knowledge structured evidence -> `MemoryContextRequest.external_evidence` 文本投影的唯一 owner，并在同一处保留 `retrieval_evidence_refs` 的最小结构化并行视图。
3. owner 边界保持不变：knowledge 继续拥有 `EvidenceBundle` / `EvidenceSlice` 作为结构化事实源；runtime 负责跨模块投影到 memory；memory 继续只消费 `external_evidence` 与 `retrieval_evidence_refs`，不反向解释 knowledge source-of-truth。

## 2. 本轮代码结果

| 目标 | 落盘结果 | 对 closeout 的意义 |
|---|---|---|
| runtime owner 收口 | 新增 [runtime/src/KnowledgeEvidenceProjector.h](../../../runtime/src/KnowledgeEvidenceProjector.h) 与 [runtime/src/KnowledgeEvidenceProjector.cpp](../../../runtime/src/KnowledgeEvidenceProjector.cpp)，并更新 [runtime/src/AgentOrchestrator.cpp](../../../runtime/src/AgentOrchestrator.cpp)、[runtime/CMakeLists.txt](../../../runtime/CMakeLists.txt) | knowledge -> memory 文本 / refs 投影不再散落在 `AgentOrchestrator` 内联循环里；runtime 现有单一 projector owner 承担 dedupe、invalid ref 过滤与最小共享投影 |
| focused unit gate | 新增 [tests/unit/runtime/KnowledgeEvidenceProjectorTest.cpp](../../../tests/unit/runtime/KnowledgeEvidenceProjectorTest.cpp)，并更新 [tests/unit/runtime/CMakeLists.txt](../../../tests/unit/runtime/CMakeLists.txt) | `KnowledgeEvidenceProjectorTest` 现锁定 baseline evidence 保留、`context_projection` 去重、invalid ref 过滤与结构化 freshness 保留 |
| boundary integration gate | 新增 [tests/integration/memory/MemoryExternalEvidenceProjectionEndToEndTest.cpp](../../../tests/integration/memory/MemoryExternalEvidenceProjectionEndToEndTest.cpp)，并更新 [tests/integration/memory/CMakeLists.txt](../../../tests/integration/memory/CMakeLists.txt) | 通过 recording memory manager 直接证明 runtime 在 `memory_manager->prepare_context()` 边界会同时保留 runtime baseline evidence、knowledge 文本投影与 `retrieval_evidence_refs` |

## 3. 设计与关联依据

1. [docs/ssot/CrossModuleDataProjectionMatrix.md](../../../docs/ssot/CrossModuleDataProjectionMatrix.md) §4 已冻结 `knowledge -> runtime -> memory` 的 projection matrix：`external_evidence` 继续承载文本视图，`retrieval_evidence_refs` 只保留最小结构化共享字段。
2. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已把 `WP-MEM-GAP-007` 固定为 runtime-side `KnowledgeEvidenceProjector` + end-to-end evidence projection focused tests。
3. [docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md](../../../docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md) 已指出 external evidence / memory prepare_context 是 runtime 主链的一部分；本轮只把该投影 owner 收口为独立 projector，不改 runtime 主控权归属。

## 4. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| runtime 必须成为 knowledge -> memory 投影唯一 owner | `KnowledgeEvidenceProjectorTest` |
| `external_evidence` 与 `retrieval_evidence_refs` 必须并行保留 | `KnowledgeEvidenceProjectorTest`、`MemoryExternalEvidenceProjectionEndToEndTest` |
| 验收应锁在 `memory_manager->prepare_context()` 边界，而不是更宽的 unary terminal state | `MemoryExternalEvidenceProjectionEndToEndTest` |

## 5. D Gate

1. owner 单一：knowledge 仍输出 `EvidenceBundle` / `EvidenceSlice`，runtime 统一把它们投影到 `MemoryContextRequest`，memory 只消费 shared projection。
2. shared contracts 不扩张：本轮没有把 `EvidenceSlice` / `EvidenceBundle` 上抬进 contracts，只沿既有 `external_evidence` 与 `retrieval_evidence_refs` 承接。
3. focused acceptance：端到端验收锁在 `prepare_context()` 请求已收到正确 evidence 载荷，避免把不属于本任务的终态响应噪声混入 blocker 判定。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_runtime_knowledge_evidence_projector_unit_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["KnowledgeEvidenceProjectorTest"])`
   - 结果：通过，1/1。
3. `Build_CMakeTools(buildTargets=["dasall_memory_external_evidence_projection_integration_test"])`
   - 结果：通过。
4. `RunCtest_CMakeTools(tests=["MemoryExternalEvidenceProjectionEndToEndTest"])`
   - 结果：通过，1/1。
5. `Build_CMakeTools(buildTargets=["dasall_runtime_knowledge_evidence_projector_unit_test","dasall_memory_external_evidence_projection_integration_test"])` + `RunCtest_CMakeTools(tests=["KnowledgeEvidenceProjectorTest","MemoryExternalEvidenceProjectionEndToEndTest"])`
   - 结果：通过，2/2。

## 7. 完成判定

1. `WP-MEM-GAP-007 / GAP-P1-C / MEM-B06` 已闭合。
2. runtime 现以 `KnowledgeEvidenceProjector` 统一承接 knowledge structured evidence 到 memory text projection 的 owner 责任，并保持 `retrieval_evidence_refs` 最小结构化并行视图。
3. Memory 当前剩余 P1 焦点收敛为 `WP-MEM-GAP-008` 与更高层 installed / GA 绿色记录；external evidence projection 不再是本轮后的高优先级 blocker。