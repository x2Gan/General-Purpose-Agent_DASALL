# WP-MEM-GAP-021 reflection feedback loop closeout

来源任务：WP-MEM-GAP-021
关联缺口：Reflection → ExperienceMemory 反馈闭环显性化（V2）
完成日期：2026-06-18

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-021`，不把 `WP-MEM-GAP-018` 的 soak gate、`WP-MEM-GAP-004` 的 installed gate 或新的 cognition structured-output schema 扩张混入同一轮。
2. authoritative 问题定义固定为：Memory 已具备 `ExperienceMemory` 持久化与按 stage 查询能力，但 cognition.reflect → runtime → memory 之间没有显式 lesson projection，也没有“被召回 lesson 已进入 `ContextPacket`”的端到端证据。
3. owner 边界保持不变：reflection lesson 的生成仍归 cognition，反思结果到 writeback request 的组装仍归 runtime，ExperienceMemory 的持久化、召回与 context 投影仍归 memory；不扩 `ContextPacket` frozen slots、不让 runtime 直接操作底层 store。

## 2. 研究与设计依据

1. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已将 `WP-MEM-GAP-021` 固定为“`reflection_lesson` 字段 + `ReflectionLessonProjection` + `WritebackCoordinatorReflectionLessonTest` + `MemoryReflectionFeedbackLoopIntegrationTest`”。
2. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §6.10.2 已冻结 Reflection 阶段的高优先级上下文应包含 `experience lessons` 与 confirmed facts；这说明缺口不在 store/query，而在 lesson 没有被显式投影进现有 `ContextPacket` 槽位。
3. 同一详设 §6.12.2 的 `ContextPacket` 槽位映射未提供新的 experience-only slot，因此本轮必须复用现有冻结面，而不是扩 shared `ContextPacket` contract。
4. LangChain / LangGraph memory 概念文档指出：long-term memory 中的 episodic memories 用于记住 past agent actions，reflection 可用于持续 refinement，而 memory collections 的价值在于把可复用信息显式保存并在未来交互中 grounding responses。这直接支撑 DASALL 在本轮把 self-reflection lesson 落成可检索 experience，并在下一轮通过现有 context projection 显式回灌，而不是把原始 reflection trace 塞回 prompt。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| cognition 与 runtime 之间需要一个 suggestion-only、可序列化、不会回退 ADR-007 的 lesson projection | [contracts/include/checkpoint/ReflectionLessonProjection.h](../../../../contracts/include/checkpoint/ReflectionLessonProjection.h)、[cognition/include/CognitionTypes.h](../../../../cognition/include/CognitionTypes.h) |
| reflection lesson 生成 owner 仍在 cognition，不要求本轮改 structured-output schema | [cognition/src/CognitionFacade.cpp](../../../../cognition/src/CognitionFacade.cpp)、[tests/integration/memory/MemoryReflectionFeedbackLoopIntegrationTest.cpp](../../../../tests/integration/memory/MemoryReflectionFeedbackLoopIntegrationTest.cpp) |
| runtime 需要把 reflection result 转成 MemoryWritebackRequest，但不直接触碰 store | [runtime/src/ReflectionLessonProjector.h](../../../../runtime/src/ReflectionLessonProjector.h)、[runtime/src/ReflectionLessonProjector.cpp](../../../../runtime/src/ReflectionLessonProjector.cpp)、[runtime/src/AgentOrchestrator.cpp](../../../../runtime/src/AgentOrchestrator.cpp) |
| memory 需要把 `reflection_lesson` 归一化为 self-reflection experience，并把召回的 lesson 投影回现有 `ContextPacket` 槽位 | [memory/include/writeback/MemoryWritebackRequest.h](../../../../memory/include/writeback/MemoryWritebackRequest.h)、[memory/src/writeback/WritebackCoordinator.cpp](../../../../memory/src/writeback/WritebackCoordinator.cpp)、[memory/src/context/ContextOrchestrator.cpp](../../../../memory/src/context/ContextOrchestrator.cpp) |
| 新链路需要同时有 focused unit gate 和 focused integration gate | [tests/unit/memory/WritebackCoordinatorReflectionLessonTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorReflectionLessonTest.cpp)、[tests/integration/memory/MemoryReflectionFeedbackLoopIntegrationTest.cpp](../../../../tests/integration/memory/MemoryReflectionFeedbackLoopIntegrationTest.cpp)、[tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp) |

## 4. 设计决策

1. `ReflectionLessonProjection` 保持 suggestion-only：只承载 `lesson_summary`、`trigger_condition`、`recommended_action`、`effectiveness_score`、`applicable_domains`、`tags`，不回扩执行控制字段，不替代 `ReflectionDecision` 或 `RecoveryOutcome`。
2. cognition 不新增新的 reflection structured-output schema；本轮只在已有 `reflection_decision` 成功产出后，合成一个 module-local lesson projection，避免把任务扩大成新的 bridge 协议轮次。
3. runtime 通过 `ReflectionLessonProjector` 生成单独的 best-effort `MemoryWritebackRequest`，语义与现有 `belief writeback` 对齐：writeback 失败只记 best-effort 细节，不改变 recovery owner 或主链 fail-closed 判定。
4. memory 不扩 `ContextPacket` slots，而是把 `CandidateSet.relevant_experiences` 规范化投影进现有 `retrieval_evidence` 文本槽位；这样既兑现“lesson 已影响 ContextPacket”，又不回退 ADR-006 的冻结面。
5. self-reflection experience 的 stage recall 依赖既有 `ExperienceQuery.stage` 过滤逻辑，因此本轮以 `reflection` / `stage:reflection` / `experience_kind:self_reflection` tags 固定召回与审计口径，而不新增 schema 列。

## 5. D Gate

1. 设计边界明确：不扩 shared `ContextPacket`、不新增 store schema、不把 reflection lesson owner 从 cognition 挪到 runtime / memory。
2. Build 三件套完整：代码目标、测试目标、验收命令均已锁定在 lesson projection、writeback normalization、next-round recall 与 focused unit/integration gates。
3. blocker 结论：原规划中的“cognition 需暴露 `lesson_learned` 字段”已被最小闭合方案吸收。本轮直接在 shared contracts 增加空心 lesson projection，并由 cognition 本地合成该字段，不需要额外拆出 blocker 任务。

## 6. 代码结果

1. 新增 [contracts/include/checkpoint/ReflectionLessonProjection.h](../../../../contracts/include/checkpoint/ReflectionLessonProjection.h)，冻结 suggestion-only `ReflectionLessonProjection` 与 `self_reflection` kind 常量。
2. 更新 [cognition/include/CognitionTypes.h](../../../../cognition/include/CognitionTypes.h) 与 [cognition/src/CognitionFacade.cpp](../../../../cognition/src/CognitionFacade.cpp)，为 `CognitionReflectionResult` 增加 `reflection_lesson` 字段，并在已有 `reflection_decision` 成功产出后，按 failed observation / decision kind 合成 lesson summary、trigger condition、recommended action、effectiveness score 与 reflection tags。
3. 更新 [memory/include/writeback/MemoryWritebackRequest.h](../../../../memory/include/writeback/MemoryWritebackRequest.h) 与 [memory/src/writeback/WritebackCoordinator.cpp](../../../../memory/src/writeback/WritebackCoordinator.cpp)，新增 `reflection_lesson` 写回入口；`WritebackCoordinator` 现会把该字段归一化为 self-reflection `ExperienceCandidate`，补齐 `reflection` / `stage:reflection` / `experience_kind:self_reflection` tags，并把 `experience_kind` 投影到 writeback observability fields。
4. 新增 [runtime/src/ReflectionLessonProjector.h](../../../../runtime/src/ReflectionLessonProjector.h) 与 [runtime/src/ReflectionLessonProjector.cpp](../../../../runtime/src/ReflectionLessonProjector.cpp)，并更新 [runtime/src/AgentOrchestrator.cpp](../../../../runtime/src/AgentOrchestrator.cpp) 与 [runtime/CMakeLists.txt](../../../../runtime/CMakeLists.txt)，让 runtime 在 reflection 成功后把 lesson 组装成单独的 best-effort writeback request。
5. 更新 [memory/src/context/ContextOrchestrator.cpp](../../../../memory/src/context/ContextOrchestrator.cpp)，把 `CandidateSet.relevant_experiences` 规范化投影到现有 `retrieval_evidence` 槽位，使下一轮 `prepare_context()` 能显式带回被召回的 reflection lesson。
6. 更新 [tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp)、[tests/unit/memory/CMakeLists.txt](../../../../tests/unit/memory/CMakeLists.txt) 与 [tests/integration/memory/CMakeLists.txt](../../../../tests/integration/memory/CMakeLists.txt)，把新 field、unit target 与 integration target 纳入 discoverability 与 public surface regression。
7. 新增 [tests/unit/memory/WritebackCoordinatorReflectionLessonTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorReflectionLessonTest.cpp) 与 [tests/integration/memory/MemoryReflectionFeedbackLoopIntegrationTest.cpp](../../../../tests/integration/memory/MemoryReflectionFeedbackLoopIntegrationTest.cpp)，分别锁定 self-reflection writeback 语义与 cognition→runtime→memory→ContextPacket 的 next-round recall 闭环。

## 7. 测试结果

1. `WritebackCoordinatorReflectionLessonTest` 证明 `reflection_lesson` 能被写回为一个 stage-filterable `ExperienceMemory`，并保留 `experience_kind:self_reflection` 与 `stage:reflection` tags。
2. `MemoryReflectionFeedbackLoopIntegrationTest` 使用真实 cognition reflection flow + runtime projector + sqlite-backed memory manager，证明 failed observation 产出的 lesson 会进入 `ExperienceMemory`，并在下一轮 `prepare_context(stage="reflection")` 中以 `retrieval_evidence` 形式显式回灌 `ContextPacket`。
3. `MemoryInterfaceCompileTest` 继续锁定新增 shared contract include 与 `MemoryWritebackRequest.reflection_lesson` public surface 不回退。

## 8. 验收证据

1. `ctest --test-dir build-ci -R "^WritebackCoordinatorReflectionLessonTest$" --output-on-failure`
   - 结果：通过，1/1。
2. `cmake --build build-ci --target dasall_memory_reflection_feedback_loop_integration_test && ctest --test-dir build-ci -R "^MemoryReflectionFeedbackLoopIntegrationTest$" --output-on-failure`
   - 结果：通过，1/1。
3. `ctest --test-dir build-ci -R "^(WritebackCoordinatorReflectionLessonTest|MemoryReflectionFeedbackLoopIntegrationTest|MemoryInterfaceCompileTest)$" --output-on-failure`
   - 结果：通过，3/3。

## 9. 结果

1. `WP-MEM-GAP-021` 已闭合；reflection 产出的 lesson 现在会沿 cognition → runtime → memory 显式落成 self-reflection `ExperienceMemory`，并在下一轮 context assembly 中被显性召回到 `ContextPacket`。
2. 本轮保持了 owner 边界：cognition 只生成 suggestion-only lesson，runtime 只做 request projection，memory 只做持久化/召回/上下文投影；没有新增执行控制字段，也没有回退 ADR-006 / ADR-007。
3. Memory 当前剩余 V2 / 运营焦点已从 `WP-MEM-GAP-021` 收敛为 `WP-MEM-GAP-018` 与更高层 installed / soak / release evidence；reflection feedback loop 不再是 open gap。
