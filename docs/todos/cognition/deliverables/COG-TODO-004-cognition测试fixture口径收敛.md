# COG-TODO-004 cognition 测试 fixture 口径收敛

状态：Done  
日期：2026-04-24  
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md  
任务类型：前置补设计 / 评审门禁  

## 1. 本地证据

1. `tests/mocks/include/` 当前已有 `MockLLMAdapter.h`、`MockMemoryStore.h`、`MockTool.h`、`MockExecutionService.h` 等通用测试支撑，但没有 cognition-facing 的 `MockLLMManager` 或 `MockCognitionFixture`。
2. `tests/unit/runtime/RuntimeSmokeTest.cpp` 仍可用 `MockLLMAdapter + MockTool` 串联 legacy runtime smoke；该路径不能验证 Runtime 是否经 `ICognitionEngine` / `IResponseBuilder` 消费 cognition。
3. `docs/architecture/DASALL_cognition子系统详细设计.md` 已在目录建议中预留 `tests/mocks/include/MockLLMManager.h` 与 `MockCognitionFixture.h`，但 COG-D09 原先仍混有 `MockResponseBuilderSupport.h` 的旧 seam 表述。
4. COG-TODO-001 已冻结 cognition 公共入口为 `ICognitionEngine::decide()` / `reflect()` 与 `IResponseBuilder::build()`；COG-TODO-002 已冻结 canonical stage key；COG-TODO-003 已冻结 Runtime caller fixture 与 ActionDecision→FSM 第一跳。

## 2. 外部参考

1. Martin Fowler 的 "Mocks Aren't Stubs" 区分了 test double、stub、fake、spy、mock，并强调 mock 更偏向行为验证；本任务据此把 `MockLLMManager` 定义为可脚本化响应与交互记录的 cognition-facing double，而不是 provider adapter fake：https://martinfowler.com/articles/mocksArentStubs.html

## 3. 主结论

1. cognition 测试夹具必须围绕 cognition-facing seams 建立，不再以 `MockLLMAdapter + MockTool` 作为 cognition gate 的验收路径。
2. `MockLLMManager` 是 bridge / stage / profile / failure 测试的 LLM manager 级 double，关注 `StageModelHint`、canonical stage key、失败投影与 provider-private redaction。
3. `MockCognitionFixture` 是 runtime caller 与 cognition 公共入口的 fixture，关注合法 request 形状、`caller_domain=runtime.agent_orchestrator`、scripted `ActionDecision` / response builder 结果与 Runtime FSM 期望。
4. failure/profile smoke 样例先作为 `MockCognitionFixture` 内部 helper，共享同一套 stage key、caller shape 与 ErrorInfo 断言；不在测试私有逻辑中修正 production schema 或维护第二套 stage 映射。
5. COG-BLK-004 已完成设计侧解阻；真实 header、CMake 接线与 discoverability 仍由 COG-TODO-024 / 025 落地。

## 4. Fixture 职责表

| Fixture | 建议文件范围 | 职责 | 禁止事项 |
|---|---|---|---|
| `MockLLMManager` | `tests/mocks/include/MockLLMManager.h` | 按 `StageModelHint.stage_name`、`task_type`、budget hint、structured output 要求脚本化成功 / schema-invalid / unavailable / timeout / empty result；记录调用以验证 bridge projection 与 redaction | 不模拟 PromptRegistry、PromptComposer 或 provider adapter；不暴露 `reasoning_content` 等 provider-private 字段；不维护测试私有 stage mapping |
| `MockCognitionFixture` | `tests/mocks/include/MockCognitionFixture.h` | 生成 `CognitionStepRequest`、`ReflectionRequest`、`ResponseBuildRequest` 的最小合法样例；固定 Runtime caller；脚本化 `ActionDecision` 与 `ResponseBuildResult`；收集 FSM 第一跳期望 | 不替代 Runtime FSM；不构造 ToolRequest；不直接访问 MemoryStore / Tool / Knowledge 实现 |
| `FailureProfileScenario` | 首版作为 `MockCognitionFixture` helper | 复用 llm unavailable、schema violation、missing belief、contradictory observation、response fallback 与五档 profile smoke 样例 | 不把 retry / recovery / profile route 归一化藏进测试私有逻辑 |

## 5. 测试层映射

| 测试层 | 夹具口径 | 目标测试 |
|---|---|---|
| unit/cognition | 使用 `MockLLMManager` 验证 stage hint projection、error mapping、schema validation、telemetry redaction | `CognitionLlmBridgeProjectionTest`、`CognitionLlmBridgeErrorMappingTest`、`StageModelHintProjectionTest`、`CognitionTelemetryRedactionTest` |
| unit/runtime | 使用 `MockCognitionFixture` 验证 Runtime caller handoff 与 ActionDecision→FSM 第一跳 | `RuntimeCognitionLoopSmokeTest` |
| integration/cognition | 组合 `MockLLMManager` 与 `MockCognitionFixture`，覆盖 happy path、interaction contract、failure injection、profile compatibility | `CognitionRuntimeIntegrationTest`、`CognitionRuntimeInteractionContractTest`、`CognitionFailureInjectionIntegrationTest`、`CognitionProfileCompatibilityTest` |

## 6. Design -> Build 映射

| 设计结论 | 后续 Build 任务 | 验收点 |
|---|---|---|
| `MockLLMManager` 作为唯一 cognition-facing LLM manager double | COG-TODO-020、024、028、029 | bridge / failure / profile tests 不再依赖 `MockLLMAdapter` |
| `MockCognitionFixture` 作为 Runtime caller fixture | COG-TODO-024、026、027 | request 必含 caller_domain、goal/context/belief/observation/budget；ActionDecision→FSM 可断言 |
| failure/profile scenario helper 共享 fixture 语义 | COG-TODO-024、028、029 | 五类失败与五档 profile 不维护私有 stage mapping |
| `MockResponseBuilderSupport` 不作为首版独立 seam | COG-TODO-019、024、026 | response builder helper 先内聚在 `MockCognitionFixture`，待 streaming 演进再拆分 |

## 7. Build 三件套与验收

代码目标：更新 cognition 详设、专项 TODO 与本交付物，不新增生产代码。  
测试目标：文档一致性检索，确认 `MockLLMManager`、`MockCognitionFixture`、`tests/mocks/include` 的职责与后续 Build 映射可检索。  
验收命令：

```bash
rg -n "MockLLMManager|MockCognitionFixture|tests/mocks/include" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md
```

验收结论：PASS。检索可定位 fixture 职责表、目录建议、COG-D09 / COG-TODO-004 / COG-BLK-004 回写与后续 COG-TODO-024 落盘任务。

## 8. D Gate 与合规复核

| 检查项 | 结论 |
|---|---|
| D 文档已落盘 | PASS |
| Design->Build 映射完整 | PASS |
| Build 三件套已锁定 | PASS |
| 代码注释要求 | 不适用，本轮为文档门禁 |
| 正负例覆盖 | 正例：stage scripted success / Runtime caller success；负例：llm unavailable、schema violation、missing belief、response fallback |
| TODO / 交付物 / worklog 可追溯 | PASS |
| COG-BLK-004 | 设计侧已由本任务解阻；真实 mock header 与 discoverability 仍由 COG-TODO-024 / 025 关闭 |
