# COG-TODO-022 CognitionTelemetry 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready observability implementation

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.4 已把 `CognitionTelemetry` 定义为 cognition 的统一观测桥，负责 decide / reflect / build_response 路径上的日志、指标、trace 与审计字段口径统一。
2. 同一章节明确 `CognitionTelemetry` 的非职责：不定义第二套观测协议、不记录 raw context / prompt / provider-private 字段、不替代 llm provider 指标、不替代 runtime 恢复准入审计、且不得阻塞主链结果返回。
3. 详设要求 `StageTelemetryContext` 至少显式承载 `request_id`、`goal_id`、`profile_id`、`stage`、`trace_id`、`model_hint_tier`、`fallback_used`、`result_code`；022 正是把这一组 fields 变成统一 owner surface。
4. `docs/todos/cognition/deliverables/COG-TODO-020-CognitionLlmBridge收敛.md` 已收口 canonical stage、warnings、diagnostics 与 provider-private 字段剥离，为 022 复用 stage/trace/redaction 口径提供直接上游支撑。
5. `docs/todos/cognition/deliverables/COG-TODO-021-StageOutputValidator收敛.md` 已把 schema / invariant fail-closed 校验收口为统一结果面，使 022 可以稳定记录 `result_code`、`stage failed` 与 degraded 路径，而不依赖各阶段零散拼接观测字段。
6. `docs/todos/cognition/deliverables/COG-TODO-024-cognition测试fixture实现收敛.md` 已冻结 cognition-specific test seam，使 022 可以在 module-local 范围内新增 telemetry sink mock，而不回退到旧的粗粒度 mock 观测路径。

## 2. 外部参考

1. OpenTelemetry trace 概念文档强调：trace/span 应通过统一 attributes 与 events 承载可关联的结构化元数据，`trace_id`/`span_id`/attributes 是跨操作关联分析的核心。022 中 `StageTelemetryContext` 和统一 event fields 的设计与这一点一致：https://opentelemetry.io/docs/concepts/signals/traces/
2. OpenTelemetry sensitive data 指南强调 telemetry 实现者必须负责 data minimization，避免采集不必要的敏感信息，并在必须保留观测价值时通过 redaction / delete / transform 处理敏感属性。022 中对 `raw_prompt`、`provider_payload`、`reasoning_trace` 等字段的 redaction 与 fail-safe 最小字段退化直接遵循这一原则：https://opentelemetry.io/docs/security/handling-sensitive-data/

## 3. 主结论

1. 新增 `cognition/src/observability/CognitionTelemetry.h`、`cognition/src/observability/CognitionTelemetry.cpp`，把 telemetry 从设计卡片落为独立私有 owner。
2. telemetry 的最小 supporting surface 收敛为：
   - `StageTelemetryContext`：统一 request/goal/profile/stage/trace/result 口径；
   - `DecisionTelemetryRecord`、`DegradeTelemetryRecord`、`AuditReferenceSet`：承载决策、降级和审计相关字段；
   - `TelemetryEvent`、`TelemetryMetric`、`TelemetryEmitResult`：统一多 sink 事件、指标与 fail-open 结果面；
   - `ICognitionTelemetrySink`：cognition 内部 multi-sink seam，便于后续 façade 集成与 focused tests。
3. `emit_stage_started()`、`emit_stage_completed()`、`emit_stage_failed()`、`emit_clarification_requested()`、`emit_response_degraded()` 已落盘，并会统一补齐 stage context fields，而不是让各阶段散落拼装字段。
4. telemetry 现在会对 event fields 中的 `raw_prompt`、`provider_payload`、`reasoning_trace`、`authorization`、`secret_key` 等敏感内容执行 redaction；若 redaction 过程中出现异常，则退化为最小安全输出，而不是放行未裁剪内容。
5. 任何 log / metric / trace / audit sink 的异常都被局部吞掉并转成 diagnostics，保持 fail-open，不会改变 cognition 主链结果对象。

## 4. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| telemetry 是统一观测 owner | `cognition/src/observability/CognitionTelemetry.h`、`CognitionTelemetry.cpp` | 022 不提前串 façade，只收口观测逻辑 |
| stage fields 必须统一 | `StageTelemetryContext`、`CognitionTelemetryFieldsTest.cpp` | request_id / trace_id / stage / model_hint_tier / decision_kind 全部可断言 |
| sensitive fields 必须 redaction | `redact_value()`、`CognitionTelemetryRedactionTest.cpp` | raw prompt / provider payload 不会泄露到 event fields |
| sink 故障必须 fail-open | `TelemetryEmitResult`、`CognitionTelemetryFailureIsolationTest.cpp` | 单个 sink throw 后其他 sink 仍继续发射 |
| 暂不绑定具体 infra provider | `ICognitionTelemetrySink` internal seam | 后续 023 只消费 owner，不需要在 022 就绑定 provider 细节 |

## 5. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增 telemetry private owner 与 internal multi-sink seam | stage started/completed/failed/degraded 路径可以统一出字段 | `Build_CMakeTools(buildTargets=["dasall_cognition_telemetry_fields_unit_test","dasall_cognition_telemetry_redaction_unit_test","dasall_cognition_telemetry_failure_isolation_unit_test"])` | 若 contracts 类型面有误，先修 telemetry owner，不扩到 façade |
| B2 | 落地 telemetry sink mock 与 fields tests | 验证上下文字段和 decision fields 贯通 | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_telemetry_fields_unit_test` | 若字段遗漏，只补 owner field projector，不改主链组件 |
| B3 | 落地 redaction / failure isolation | 验证敏感字段裁剪与 sink fail-open | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_telemetry_redaction_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_telemetry_failure_isolation_unit_test` | 若 redaction 或 fail-open 误伤非敏感字段，只回修 telemetry helper |

## 6. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_cognition_telemetry_fields_unit_test","dasall_cognition_telemetry_redaction_unit_test","dasall_cognition_telemetry_failure_isolation_unit_test"])`
   - 第一次结果：失败；`ErrorInfo` 真实 surface 中 `details.code`、`retryable`、`safe_to_replan` 为 optional，且 failure type 应来自 `ResultCodeCategory`，首版 telemetry failure path 和 test 对类型假设过宽。
   - 同一 slice 修正 optional 访问和 failure type 映射后复跑：通过；三条 telemetry-focused targets 全部编译链接成功。
2. `RunCtest_CMakeTools(tests=["CognitionTelemetryFieldsTest","CognitionTelemetryRedactionTest","CognitionTelemetryFailureIsolationTest"])`
   - 结果：失败，工具返回仓库既有通用错误 `生成失败`；不作为代码失败证据。
3. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_telemetry_fields_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_telemetry_redaction_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_telemetry_failure_isolation_unit_test`
   - 结果：通过；三条 telemetry-focused tests 二进制全部零输出退出。

## 7. 完成判定与边界

1. COG-TODO-022 已完成：统一 stage telemetry owner、敏感字段 redaction 与 sink failure fail-open 均已落盘并通过 focused tests。
2. 本轮没有提前把 telemetry 绑死到具体 infra provider，也没有提前落 runtime smoke 或 façade orchestration；023 继续负责三入口主链与 telemetry/validator/bridge 的整合。
3. telemetry 继续只做观测，不负责 llm 调用、schema 校验、降级裁定或 runtime 结果提交。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 语义字段完整 | PASS：request/goal/profile/stage/trace/model_hint/result/decision 字段进入统一 owner |
| sensitive data redaction | PASS：`raw_prompt`、`provider_payload`、`reasoning_trace` 等字段不会透出原值 |
| sink failure fail-open | PASS：单个 sink throw 不会阻断其他 sink 发射，也不会抛回调用方 |
| owner 边界 | PASS：022 未提前绑定具体 infra provider，也未越权进入 façade 主链 |
| focused validation | PASS：三条 telemetry tests 构建通过，显式二进制执行全部通过 |