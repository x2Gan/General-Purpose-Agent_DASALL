# COG-TODO-019 ResponseBuilder 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready terminal response construction implementation

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.3 已冻结 `ResponseBuilder` 的职责：在 Runtime 已判定进入终态输出路径后，把 `GoalContract`、`ContextPacket`、`BeliefState`、`latest Observation` 与 terminal decision 映射为共享 `AgentResult`，并保证模板降级和最小失败输出可用。
2. 同一章节明确边界：`ResponseBuilder` 不直接向用户通道提交结果，不替 Runtime 决定何时进入终态，不直接发起恢复，也不暴露 raw prompt、provider payload 或 `reasoning_content`。
3. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.14.1 已冻结 Runtime→Cognition 语义：`DirectResponse` / `ConvergeSafe` 由 Runtime 进入 `Responding` 并调用 `IResponseBuilder::build()` 构造 `AgentResult`，因此本轮只需要收口终态构造，不扩大到 Runtime 提交或外部执行。
4. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.14.2 明确 `ResponseBuilder` 的 stage canonical key 是 `response`，`task_type` 是 `final_response`；`CognitionLlmBridge` 尚由 COG-TODO-020 落地，因此本轮只能先把 response-mode 选择、模板降级、红线字段裁剪和 `AgentResult` 映射收口为独立组件，不提前越界到 bridge。
5. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 对 COG-TODO-019 的完成判定明确要求：llm 路径、模板降级和 redaction 三条路径都可验证，且不能引入 streaming 额外职责。
6. 代码现状显示 `ResponseBuilder` 原先内嵌在 `cognition/src/CognitionFacade.cpp`，只有 observation payload 成功路径和最小模板兜底，既没有独立 source owner，也没有 `ResponseBuilderAgentResultMappingTest`、`ResponseBuilderTemplateFallbackTest`、`ResponseBuilderRedactionTest` 三条 focused tests。

## 2. 外部参考

1. OWASP Logging Cheat Sheet 要求对敏感信息做 remove / mask / sanitize / pseudonymize，避免把 token、密码、连接串、原始秘密等高敏字段直接暴露在日志或输出中；同时强调失败信息不能靠静默吞掉来“降级成功”：https://cheatsheetseries.owasp.org/cheatsheets/Logging_Cheat_Sheet.html

本轮借鉴点：`ResponseBuilder` 对 `reasoning_content`、`raw_prompt`、`prompt_bundle`、`provider_payload`、`api_token`、`authorization`、`secret_key` 等字段执行显式裁剪，并用 `[REDACTED]` 与 `omitted_details` 记录被掩码内容，而不是静默丢失或把 provider-private 载荷直接穿透到 `AgentResult`。

## 3. 主结论

1. 新增独立私有源 `cognition/src/response/ResponseBuilder.cpp`，把 `ResponseBuilder` 从 `CognitionFacade.cpp` 中抽离出来，恢复 response stage 的独立 owner；`CognitionFacade` 只保留 `ICognitionEngine` 的 `decide()` / `reflect()` 入口。
2. `ResponseBuilder::build()` 现在明确分成三条可测试路径：
   - observation-backed `llm_projection` 路径：当 `latest_observation.payload` 存在且未显式要求模板时，构造 completed `AgentResult`；
   - `template_fallback` 路径：当 observation payload 缺失且允许模板回退时，用 `terminal_decision.response_outline.summary` 或当前 goal summary 生成 `PartiallyCompleted` 结果；
   - explicit error 路径：当 payload 缺失且模板回退被禁用时，返回 `ResponseBuildResult(ErrorInfo)`，不再伪造表面成功结果。
3. 组件内部补齐 `select_response_mode()`、`build_with_llm()`、`build_with_template()`、`redact_unsafe_fields()`、`clamp_output_size()`、`build_structured_payload()`，把 response mode、`fallback_used`、`omitted_details` 和结构化 envelope 统一投影到 `AgentResult.structured_payload`。
4. 为保持既有 runtime unary integration 语义兼容，completed 路径仍保留 `runtime unary integration completed:` 前缀；这使当前 true-port 集成断言不需要改测，同时把 response stage 独立 owner 先落地。
5. 本轮没有提前接入 `CognitionLlmBridge`、`StageOutputValidator` 或 streaming。当前 `build_with_llm()` 的职责是消费已归一化的 terminal observation payload，并为后续 COG-TODO-020 保留 response-mode seam，而不是自行引入第二套 llm 生命周期。

## 4. 边界与职责

| 组件 | 职责 | 非职责 |
|---|---|---|
| `ResponseBuilder` | 把终态 observation / outline 投影为 `AgentResult` | 不决定何时终态；不提交用户通道；不管理 llm provider lifecycle |
| `ResponseEnvelope` | 收口 response mode、summary、required sections、omitted details、fallback flag | 不等于 shared contract；不承载 Runtime FSM 或 Recovery 语义 |
| Runtime | 决定进入 `Responding`、提交结果、写 checkpoint | 不回卷 response stage 组装逻辑 |
| `CognitionLlmBridge`（后续） | 负责真正的 response stage llm 调用映射 | 本轮不落地，不扩张到 COG-TODO-020 |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| ResponseBuilder 必须拥有独立 source owner | `cognition/src/response/ResponseBuilder.cpp`、`cognition/CMakeLists.txt`、`cognition/src/CognitionFacade.cpp` | response 逻辑不再内嵌在 façade 中，`dasall_cognition` 可单独编译通过 |
| completed 路径必须可映射为 `AgentResult` | `ResponseBuilder.cpp` + `ResponseBuilderAgentResultMappingTest.cpp` | observation payload 可投影为 `Completed` 结果，且保留既有 integration 前缀 |
| observation 缺失时要显式模板降级，不允许静默成功 | `ResponseBuilder.cpp` + `ResponseBuilderTemplateFallbackTest.cpp` | `fallback_used=true`、`PartiallyCompleted` 与 fallback-disabled 错误路径可二值断言 |
| provider-private / secret-bearing 字段不得泄露 | `ResponseBuilder.cpp` + `ResponseBuilderRedactionTest.cpp` | `reasoning_content` / `raw_prompt` / `api_token` 被替换为 `[REDACTED]`，并保留 `omitted_details` |
| response unit topology 必须可发现 | `tests/unit/cognition/CMakeLists.txt` | 三条 ResponseBuilder focused tests 均可独立编译与执行 |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 抽离独立 `ResponseBuilder.cpp` 并把 response owner 从 façade 中移出 | `dasall_cognition` 最小编译验证 | `Build_CMakeTools(buildTargets=["dasall_cognition"])` | 若抽离破坏现有工厂或 linkage，只回修 response source 与 façade wiring |
| B2 | 补 completed / template / error 三态映射、structured payload 和 clamping | `ResponseBuilderAgentResultMappingTest`、`ResponseBuilderTemplateFallbackTest` | `cmake --build build/vscode-linux-ninja --target dasall_response_builder_agent_result_mapping_unit_test dasall_response_builder_template_fallback_unit_test` | 若 completed 语义回归，优先保持 integration 兼容前缀与 status 映射 |
| B3 | 落红线字段裁剪与 `omitted_details` | `ResponseBuilderRedactionTest` | `cmake --build build/vscode-linux-ninja --target dasall_response_builder_redaction_unit_test` | 若 redaction 规则过宽，先收窄到已冻结的 provider-private / secret-bearing keys |
| B4 | 注册三条 focused unit tests | discoverability 与直接执行成立 | `cmake --build build/vscode-linux-ninja --target dasall_response_builder_agent_result_mapping_unit_test dasall_response_builder_template_fallback_unit_test dasall_response_builder_redaction_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_response_builder_agent_result_mapping_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_response_builder_template_fallback_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_response_builder_redaction_unit_test` | 若 CMake Tools 测试生成继续抖动，按仓库基线回退到显式目标构建与二进制执行 |

## 7. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_cognition"])`
   - 第一次结果：通过，但暴露 `CognitionFacade.cpp` 遗留未使用 helper 与 `ResponseBuilder.cpp` 聚合初始化告警。
   - 修补同一 slice 后复跑：通过，`dasall_cognition` 干净编译。
2. `Build_CMakeTools(buildTargets=["dasall_response_builder_agent_result_mapping_unit_test"])`
   - 结果：通过。
3. `RunCtest_CMakeTools(tests=["ResponseBuilderAgentResultMappingTest"])`
   - 结果：失败，工具返回通用错误 `生成失败`；根据仓库已有验证基线，不将其误判为代码失败。
4. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_response_builder_agent_result_mapping_unit_test`
   - 结果：通过；零输出退出。
5. `cmake --build build/vscode-linux-ninja --target dasall_response_builder_template_fallback_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_response_builder_template_fallback_unit_test`
   - 结果：通过；模板降级正例与 fallback-disabled 负例均零输出退出。
6. `cmake --build build/vscode-linux-ninja --target dasall_response_builder_redaction_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_response_builder_redaction_unit_test`
   - 结果：通过；默认 redaction 与显式关闭 redaction 两条分支均零输出退出。
7. `cmake --build build/vscode-linux-ninja --target dasall_response_builder_agent_result_mapping_unit_test dasall_response_builder_template_fallback_unit_test dasall_response_builder_redaction_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_response_builder_agent_result_mapping_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_response_builder_template_fallback_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_response_builder_redaction_unit_test`
   - 结果：通过；最终 focused acceptance run 成功，Ninja 报告 `no work to do` 后三条二进制串行零输出退出。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 范围控制 | PASS：只收敛 response source、focused unit tests、最小 CMake 接线与 TODO/worklog 证据 |
| 终态输出边界 | PASS：`ResponseBuilder` 仍不决定终态时机、不直接 publish 结果、不触发恢复或外部执行 |
| llm / template / error 三态 | PASS：completed projection、template fallback、fallback-disabled error 均可二值断言 |
| redaction 规则 | PASS：provider-private / secret-bearing keys 被显式掩码，并保留 `omitted_details` 证据 |
| streaming 边界 | PASS：未引入 streaming、多响应或 bridge 生命周期；仍保持 v1 单次响应 + 模板降级 |
| 正负例覆盖 | PASS：completed 正例、template 正例、fallback-disabled 负例、redaction on/off 分支均已覆盖 |
| 工具态异常处理 | PASS：`RunCtest_CMakeTools` 的通用失败已按仓库基线回退到显式目标构建与二进制执行，并保留证据 |