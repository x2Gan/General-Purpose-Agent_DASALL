# COG-TODO-021 StageOutputValidator 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready validation implementation

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.4 已把 `StageOutputValidator` 定义为 cognition 内部统一校验器，负责 required fields、enum、numeric bounds、list constraints 与 stage-specific invariants 的 fail-closed 校验。
2. 同一章节明确 `StageOutputValidator` 的非职责：不直接调用 llm、不做 prompt/routing、不隐式修复输出、不直接向 Runtime 提交结果，也不重新暴露 provider 原始载荷。
3. `docs/todos/cognition/deliverables/COG-TODO-008-PlanGraph与ReplanResult对象收敛.md` 已把 DAG 合法性、节点上限、深度上限等约束留给后续 Planner / StageOutputValidator；021 正是把这些 planning invariants 变成可执行检查。
4. `docs/todos/cognition/deliverables/COG-TODO-009-ActionDecision与BudgetContext对象收敛.md` 已冻结 `ActionDecision`、`StageModelHint` 与 `BudgetContext` 字段，使 021 可以围绕 `decision_kind`、`selected_node_id`、`clarification_question`、`response_outline` 做不变量校验。
5. `docs/todos/cognition/deliverables/COG-TODO-019-ResponseBuilder收敛.md` 已把 response stage 输出面收敛到 `ResponseBuildResult` / `AgentResult`，使 021 可以直接校验 response envelope 一致性，而不必再定义第二套终态输出对象。
6. `docs/todos/cognition/deliverables/COG-TODO-020-CognitionLlmBridge收敛.md` 已提供 `StageLlmCallResult`，使 021 可以直接对 bridge 归一化后的 payload 做 schema-level fail-closed 校验。

## 2. 外部参考

1. JSON Schema object / array 参考明确要求：required properties 需要显式声明，enum / numeric / array length 约束都应通过显式 schema 规则判定是否有效，而不是依赖隐式默认值或自动修补；这与本轮 `StageOutputValidator` 对 required fields、enum、numeric bounds、list size 的 fail-closed 策略一致：https://json-schema.org/understanding-json-schema/reference/object https://json-schema.org/understanding-json-schema/reference/array

## 3. 主结论

1. 新增 `cognition/src/validation/StageOutputValidator.h`、`cognition/src/validation/StageOutputValidator.cpp`，把 validator 从设计卡片落为独立私有组件。
2. validator 的最小 supporting surface 收敛为：
   - `StageSchemaSpec`：承载 `required_fields`、`enum_constraints`、`numeric_bounds`、`list_constraints` 与 stage-specific invariant IDs；
   - `ValidationIssue` / `ValidationIssueSet`：承载字段路径、失败类型与原因；
   - `ValidationResult`：统一承载 `ok`、issue set、`ErrorInfo` 和 diagnostics。
3. `validate_stage_output()` 现在会对 bridge 归一化 payload 做 required / enum / numeric / list fail-closed 校验，不再允许“字段缺失但继续放行”的隐式成功。
4. `validate_plan_graph_invariants()` 会校验 plan_id、节点存在性、重复节点、边引用合法性、DAG 无环与 depth cap，从而把 planning supporting type 的结构假设落实为可执行门。
5. `validate_action_decision_invariants()` 与 `validate_response_envelope()` 分别把 `ActionDecision` 和 `ResponseBuildResult` 的关键语义约束落为可执行检查：
   - `ExecuteAction` 必须带 `selected_node_id` 与 `tool_intent_hint.tool_name`；
   - `DirectResponse` / `ConvergeSafe` 必须带 `response_outline.summary`；
   - response fallback 不能伪装成 fully completed，失败终态必须带 `error_info`。

## 4. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| validator 是统一的 stage fail-closed owner | `cognition/src/validation/StageOutputValidator.h`、`StageOutputValidator.cpp` | 021 不改 façade，只收口校验逻辑 |
| required / enum / numeric / list 约束必须显式声明 | `StageSchemaSpec`、`StageOutputValidatorSchemaTest.cpp` | 非法 payload 直接失败，不靠隐式默认值通过 |
| planning invariants 必须可执行验证 | `validate_plan_graph_invariants()`、`StageOutputValidatorPlanGraphInvariantTest.cpp` | depth cap、DAG 和 edge reference 约束可直接断言 |
| response envelope 必须与 fallback/error 语义一致 | `validate_response_envelope()`、`StageOutputValidatorResponseEnvelopeTest.cpp` | fallback 结果不会伪装成 `Completed` |
| validator 不做 llm/prompt/runtime 决策 | 仅消费 `StageLlmCallResult` / `PlanGraph` / `ActionDecision` / `ResponseBuildResult` | 021 不新增 llm 调用或门面串联逻辑 |

## 5. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增 validator private owner 与 issue/result supporting structs | 让三类校验都能复用统一 issue surface | `Build_CMakeTools(buildTargets=["dasall_stage_output_validator_schema_unit_test","dasall_stage_output_validator_plan_graph_invariant_unit_test","dasall_stage_output_validator_response_envelope_unit_test"])` | 若 include/linking 接线错误，只回修 validator tests 依赖，不扩到 022 / 023 |
| B2 | 实现 schema-level fail-closed 校验 | required / enum / numeric / list 负例都应失败 | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_output_validator_schema_unit_test` | 若 payload 解析过宽，先收窄到当前 focused tests 使用的 top-level JSON 形状 |
| B3 | 实现 plan / response invariants | DAG / depth cap / fallback envelope 必须可断言 | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_output_validator_plan_graph_invariant_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_output_validator_response_envelope_unit_test` | 若 invariant 规则误伤现有 supporting types，只回修 validator 规则本身 |

## 6. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_stage_output_validator_schema_unit_test","dasall_stage_output_validator_plan_graph_invariant_unit_test","dasall_stage_output_validator_response_envelope_unit_test"])`
   - 第一次结果：失败；`StageOutputValidator` 私有头转而包含 `CognitionLlmBridge.h`，但 plan/response 两个 validator tests 未链接 `dasall_llm`，无法解析 `ILLMManager.h`。
   - 同一 slice 补齐两个测试目标的 `dasall_llm` 依赖后复跑：通过，三条目标全部完成编译链接。
2. `RunCtest_CMakeTools(tests=["StageOutputValidatorSchemaTest","StageOutputValidatorPlanGraphInvariantTest","StageOutputValidatorResponseEnvelopeTest"])`
   - 结果：失败，工具返回仓库既有通用错误 `生成失败`；不作为代码失败证据。
3. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_output_validator_schema_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_output_validator_plan_graph_invariant_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_output_validator_response_envelope_unit_test`
   - 结果：通过；三条 validator-focused tests 二进制全部零输出退出。

## 7. 完成判定与边界

1. COG-TODO-021 已完成：schema-level fail-closed 校验、plan graph invariant 校验与 response envelope invariant 校验均已落盘并通过 focused tests。
2. 本轮没有提前实现 telemetry 字段收口或 façade orchestration；022、023 继续分别收口观测 fail-open 和三入口主链串联。
3. validator 仍不做 llm 调用、prompt/routing、降级裁定或 Runtime 结果提交，继续保持纯校验 owner 边界。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| fail-closed 原则 | PASS：required / enum / numeric / list / invariant 失败都会生成 issue 与 `ErrorInfo` |
| planning invariant | PASS：DAG、edge reference、node cap、depth cap 进入可执行校验 |
| response envelope 一致性 | PASS：fallback/failed 路径不会伪装成 fully completed 成功结果 |
| owner 边界 | PASS：validator 只消费归一化输出和 supporting types，不新增 llm/runtime 控制逻辑 |
| focused validation | PASS：三条 validator tests 构建通过，显式二进制执行全部通过 |