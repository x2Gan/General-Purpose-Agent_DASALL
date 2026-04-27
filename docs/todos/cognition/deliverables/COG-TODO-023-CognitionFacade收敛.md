# COG-TODO-023 CognitionFacade 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready façade orchestration implementation

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.1 将 `CognitionFacade` 定义为 cognition 面向 runtime 的阶段协调入口，负责 decide / reflect 主链编排、统一错误出口和受控降级，而不是再造新的 orchestrator。
2. 同一章节与 ADR-008/ADR-007 边界共同约束 façade 的非职责：不得直接提交最终 `AgentResult`、不得自建 retry/circuit breaker、不得越权触发恢复或外部执行，只能返回语义级 suggestion/result。
3. `docs/todos/cognition/deliverables/COG-TODO-001-认知接口与生命周期边界收敛.md` 已冻结 cognition 公共口径为 `ICognitionEngine::decide()`、`ICognitionEngine::reflect()` 与独立 `IResponseBuilder::build()`，因此 023 的三入口闭环必须以 façade + response builder 组合验证，而不是把 response public API 回卷进 façade。
4. `docs/todos/cognition/deliverables/COG-TODO-020-CognitionLlmBridge收敛.md`、`COG-TODO-021-StageOutputValidator收敛.md`、`COG-TODO-022-CognitionTelemetry收敛.md` 已分别冻结 bridge supporting surface、validator fail-closed result surface 和 telemetry stage field owner，使 023 可以直接消费这些 private owners 组织主链。
5. `COG-TODO-013` ~ `019` 已把 `InputBoundaryValidator`、`PerceptionEngine`、`Planner`、`Reasoner`、`ReflectionEngine`、`BeliefUpdateSynthesizer`、`ResponseBuilder` 分别落盘，023 的最小实现目标因此收敛为“把已有 owner 串起来”，而不是再引入第二套认知逻辑。

## 2. 外部参考

1. Martin Fowler 对 Facade 模式的定义强调：Facade 负责为一组复杂子系统提供一致入口，隐藏编排细节，但不替代底层子系统本身的职责。023 中 `CognitionFacade` 只组织 perception/planning/reasoning/reflection/telemetry/validator 的调用，不复制这些组件逻辑：https://martinfowler.com/eaaCatalog/facade.html
2. OpenTelemetry trace 概念文档强调，跨阶段主链应该使用统一上下文字段记录开始/完成/失败事件；023 中 façade 对 `CognitionTelemetry` 的调用维持这一点，而不在主链里散落观测字段拼接：https://opentelemetry.io/docs/concepts/signals/traces/

## 3. 主结论

1. 更新 `cognition/src/CognitionFacade.cpp`，将 façade 从最小 stub 改为可执行 orchestration owner：
   - `decide()` 先跑 `InputBoundaryValidator`，再串联 `PerceptionEngine -> Planner -> StageOutputValidator(plan) -> Reasoner -> StageOutputValidator(action)`；
   - `reflect()` 先跑 `InputBoundaryValidator`，再串联 `ReflectionEngine -> BeliefUpdateSynthesizer`；
   - 两条主链都统一发射 telemetry started/completed/failed，并保持 fail-fast error surface。
2. façade 现在把 invalid-input missing fields 显式回写到 `ContextSufficiencySignal.missing_evidence_hints`，因此 013 建立的显式错误出口没有被 023 回归打断。
3. 新增的受控降级路径并不越权触发 runtime 行为：当 perception 无法生成安全输出且 `degraded_path_allowed=true` 时，façade 退化为 `AskClarification`，只返回 clarification suggestion、belief update hint 和 context reload 建议。
4. `CognitionFacadeFlowTest` 通过 `create_cognition_engine()` + `create_response_builder()` 组合验证模块级三入口闭环，保持 001 冻结的 public API 分层不变；023 没有把 response builder 重新并入 façade public surface。
5. `CognitionFacadeDegradedModeTest` 钉住 perception 无法路由时的 clarification degrade path；`CognitionFacadeInvalidInputTest` 继续保护显式 validation error 出口。

## 4. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| façade 只做阶段协调，不复制 owner 逻辑 | `cognition/src/CognitionFacade.cpp` | decide / reflect 主链都只编排现有 owner |
| invalid input 必须 fail-fast 且回传缺失字段 | `apply_invalid_decide_result()`、`CognitionFacadeInvalidInputTest.cpp` | `missing_evidence_hints` 保持显式字段名 |
| degrade 只能返回 suggestion，不越权执行 | `make_clarification_fallback()`、`CognitionFacadeDegradedModeTest.cpp` | perception routing gap 时退化为 `AskClarification` |
| response 入口继续保持独立 public owner | `CognitionFacadeFlowTest.cpp` | façade + response builder 组合通过模块级闭环验证 |
| telemetry / validator 必须被 façade 直接消费 | `CognitionFacade.cpp` 中的 telemetry / validator 调用 | stage started/completed/failed 与 invariant gate 全部存在 |

## 5. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 把 façade 从 stub 改为 decide/reflect orchestration owner | invalid input、happy path、degraded path 都有明确出口 | `Build_CMakeTools(buildTargets=["dasall_cognition_facade_invalid_input_unit_test","dasall_cognition_facade_flow_unit_test","dasall_cognition_facade_degraded_mode_unit_test"])` | 若首轮编译失败，只修 façade 与 tests 的局部接口对齐，不扩到其他 owner |
| B2 | 新增 flow / degraded façade tests 并更新 cognition unit CMake | 模块级三入口闭环与 clarification degrade path 可稳定断言 | `RunCtest_CMakeTools(tests=["CognitionFacadeInvalidInputTest","CognitionFacadeFlowTest","CognitionFacadeDegradedModeTest"])` | 若继续命中仓库 CTest 工具噪声，按基线回退显式二进制 |
| B3 | 回归 invalid-input compatibility 并重跑同一切片 | `missing_evidence_hints` 不丢失，happy/degraded 结果不被回归打断 | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_invalid_input_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_flow_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_degraded_mode_unit_test` | 若 invalid-input 回归失败，只回补 façade 的 validation projection，不改 validator owner |

## 6. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_cognition_facade_invalid_input_unit_test","dasall_cognition_facade_flow_unit_test","dasall_cognition_facade_degraded_mode_unit_test"])`
   - 第一次结果：失败；暴露的都是 023 本地接口对齐问题，包括 `GoalContract.goal_id`/`ReflectionDecision.decision_kind`/`AgentResult.status` 的 optional 形状、`ErrorInfo.source_ref` 的真实字段、`PlanningRequest`/`ReasoningRequest` 的命名空间，以及 façade 对 telemetry `[[nodiscard]]` 返回值的忽略。
   - 同一 slice 修正上述接口偏差后第二次结果：仍失败；`ValidationResult.ok` 为字段而不是方法。
   - 同一 slice 再修正 `ValidationResult.ok` 访问后第三次结果：通过；三个 façade-focused targets 全部编译链接成功。
2. `RunCtest_CMakeTools(tests=["CognitionFacadeInvalidInputTest","CognitionFacadeFlowTest","CognitionFacadeDegradedModeTest"])`
   - 结果：失败，工具返回仓库既有通用错误 `生成失败`；不作为代码失败证据。
3. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_invalid_input_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_flow_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_degraded_mode_unit_test`
   - 第一次结果：失败；`CognitionFacadeInvalidInputTest` 暴露 023 回归，`apply_invalid_decide_result()` 未把 validator `missing_fields` 回写到 `missing_evidence_hints`。
   - 同一 slice 回补 missing field projection 后复跑：通过；三条 façade-focused test binaries 全部零输出退出。

## 7. 完成判定与边界

1. COG-TODO-023 已完成：façade 不再是 stub，decide / reflect orchestration、显式错误出口和 clarification degrade path 已可执行并通过 focused tests。
2. 023 没有破坏 001 冻结的 public API 边界；`IResponseBuilder::build()` 继续独立存在，response 闭环通过模块级组合测试验证，而不是把 public response 入口重新并回 façade。
3. 023 仍未越权进入 runtime 恢复或结果提交：没有自建 retry/circuit breaker，没有直接提交 `AgentResult`，也没有绕过 runtime 去触发外部执行。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| façade orchestration | PASS：`decide()` / `reflect()` 已串联现有 owner，不再返回硬编码 stub 结果 |
| invalid input fail-fast | PASS：validator missing fields 继续显式投影到 `missing_evidence_hints` |
| controlled degrade | PASS：perception 无法安全路由时退化为 `AskClarification`，不越权执行 |
| public API 边界 | PASS：response builder 继续保持独立 public owner，023 只做模块级组合验证 |
| focused validation | PASS：Build_CMakeTools 通过，RunCtest_CMakeTools 继续命中既有工具噪声，显式二进制执行全部通过 |