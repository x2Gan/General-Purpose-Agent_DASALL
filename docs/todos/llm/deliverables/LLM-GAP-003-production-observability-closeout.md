# LLM-GAP-003 production observability closeout

日期：2026-05-19
来源任务：LLM-GAP-003
状态：Done

## 1. 任务边界

1. 本轮只收口 `LLM-GAP-003`，不合并 release candidate rerun、installed proof 或 L6 soak。
2. 任务目标是确认 production metrics / trace / audit sink 已由 `LLM-FIX-003` 闭合，并形成面向 GAP 的独立 closeout 记录。
3. 本轮不新增 observability sink，不修改 shared contracts，不执行 qemu / kvm。

## 2. 本地证据

| 证据面 | 当前状态 | 判定 |
|---|---|---|
| factory options | `LLMProductionFactoryOptions` 持有 logger、metrics provider、tracer provider、audit logger | production manager 可接入 live observability bundle |
| factory composition | `LLMProductionFactory.cpp` 创建 `LLMMetricsBridge`、`LLMTraceBridge`、`LLMAuditBridge` 并注入 `LLMManager` | production factory 不再只返回 no-op manager |
| runtime composition | `RuntimeLiveDependencyComposition.cpp` 在创建 production LLM manager 时透传 `observability.logger`、`metrics_provider`、`tracer_provider`、`audit_logger` | runtime live path 与 production factory sink 接线一致 |
| hot path tests | `LLMProductionObservabilityIntegrationTest` 断言 production-composed manager 发出 log、metric、trace 和 reasoning strip audit event | observability 不再只停留在 fixture 手动调用 |

## 3. 外部参考

OpenTelemetry signals 文档把 telemetry 划分为 traces、metrics、logs 等信号：trace 描述请求路径，metric 描述运行时测量，log 描述事件记录。DASALL 在此基础上保留独立 audit bridge，用于治理事件和 provider-private reasoning strip 事实，避免把审计事实混入 shared `LLMResponse`。

## 4. Design -> Build 映射

| Design 判定 | Build 三件套 |
|---|---|
| production factory 必须接 live observability sinks | 代码目标：复用 `LLMProductionFactoryOptions`、`LLMMetricsBridge`、`LLMTraceBridge`、`LLMAuditBridge` 与 runtime live composition；本轮不新增产品代码 |
| manager hot path 必须自动发出 metrics / trace / audit，而不是只靠 fixture 手动调用 | 测试目标：`LLMProductionObservabilityIntegrationTest`、`LLMObservabilityFieldCompletenessTest`、`LLMAuditEventCoverageTest`、`LLMSubsystemSmokeIntegrationTest` |
| 关闭 GAP 时不得外推 installed / release / L6 soak | 验收命令：`RunCtest_CMakeTools(tests=["LLMProductionObservabilityIntegrationTest","LLMObservabilityFieldCompletenessTest","LLMAuditEventCoverageTest","LLMSubsystemSmokeIntegrationTest"])`；如 CMake Tools test generation 失败，则使用同一 build tree 的 direct CTest fallback 并记录限制 |

## 5. D Gate

结果：PASS。

1. 范围单一：只处理 `LLM-GAP-003`。
2. 设计边界清楚：production observability / audit sink closed；installed / release / soak evidence 不外推。
3. Build 三件套已锁定：代码目标、测试目标、验收命令均可二值判断。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_llm_production_observability_integration_test","dasall_llm_observability_field_completeness_unit_test","dasall_llm_audit_event_coverage_unit_test","dasall_llm_smoke_integration_test"])`
	- 结果：通过；四个 focused targets 构建成功。
2. `RunCtest_CMakeTools(tests=["LLMProductionObservabilityIntegrationTest","LLMObservabilityFieldCompletenessTest","LLMAuditEventCoverageTest","LLMSubsystemSmokeIntegrationTest"])`
	- 结果：工具在 generation 层失败，未进入测试执行；该结果不代表测试失败。
3. fallback：`ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(LLMProductionObservabilityIntegrationTest|LLMObservabilityFieldCompletenessTest|LLMAuditEventCoverageTest|LLMSubsystemSmokeIntegrationTest)$'`
	- 结果：通过；`100% tests passed, 0 tests failed out of 4`，`LLMObservabilityFieldCompletenessTest`、`LLMAuditEventCoverageTest`、`LLMSubsystemSmokeIntegrationTest`、`LLMProductionObservabilityIntegrationTest` 均通过。

## 7. 完成判定

`LLM-GAP-003` 已关闭。

1. production factory 与 runtime live composition 已把 logger、metrics provider、tracer provider 和 audit logger 注入 production-composed manager。
2. focused validation 证明 LLM observability field completeness、audit event coverage、subsystem smoke 与 production observability integration 未回退。
3. 本结论不外推为 installed package、release runner、external provider 长稳态或 L6 soak 证据。