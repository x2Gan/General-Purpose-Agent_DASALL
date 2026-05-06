# SingleAgentRuntimePortMatrix (Single Source of Truth)

状态：Frozen
Owner：runtime / knowledge / llm
关联任务：INT-TODO-001
关联阻塞：INT-BLK-01
关联 Gate：Gate-INT-01、Gate-INT-06

## 1. 目的

本文件冻结 default single-agent unary 主链对 runtime 外部端口的 required / optional 口径，避免 `RuntimeDependencySet`、runtime 详设、集成 Gate 与后续 Build 任务各自解释“默认 ready”和“允许降级”的含义。

## 2. 范围与术语

适用范围：

1. `AgentFacade::init()` 完成后，由 `RuntimeDependencySet` 提供给 `AgentOrchestrator` 的 default single-agent unary 依赖面。
2. 仅覆盖 `memory`、`cognition`、`tools`、`knowledge`、`llm` 五类对外端口。
3. 不覆盖 streaming、multi_agent、services self-check、module-local supporting objects。

术语约定：

1. `required`：该端口缺失或未就绪时，runtime 不得以 default unary ready 名义接单。
2. `optional`：该端口缺失时，runtime 仅可在显式 `degraded` 语义下继续运行；不得把缺口伪装成 default ready。
3. `fail-closed`：在 init / preflight 阶段直接拒绝，或返回 capability unavailable；不得以空实现伪造成功路径。
4. `degraded`：允许接单但必须携带清晰的 readiness tag、审计字段和 Gate 归属；`degraded` 不满足 `Gate-INT-01` 的 default-ready 含义。

## 3. 决策摘要

1. `memory`、`cognition`、`tools` 是 default unary 的核心 required ports；缺任一项都必须 fail-closed。
2. `knowledge`、`llm` 对“系统可否继续运行”是 optional，但对“是否可以声明 default unary ready”是 required；缺失时只能进入显式 degraded unary，而不能继续宣称 default-ready。
3. `RuntimeDependencySet::has_live_unary_ports()` 当前只表达最小核心端口存活，不足以表达 default-ready 与 degraded-ready 的差异；后续 Build 必须提升为更细粒度 readiness surface。
4. required / optional 的判定以产品模式为准，而不是以是否存在原始裸指针为准；任何 profile-specialized `null adapter` 都必须由 runtime 明示其语义归属。

## 4. Default Unary Port Matrix

| 端口域 | `RuntimeDependencySet` 字段 | 主要消费者 | 端口分类 | default unary ready 要求 | 缺失或未就绪时语义 | 允许的 degraded 语义 | 归属 Gate |
|---|---|---|---|---|---|---|---|
| memory | `memory_manager` | `AgentOrchestrator` 上下文装配、session/checkpoint 收敛 | `required` | 必须为 live runtime-facing seam | init / preflight 直接 fail-closed；不得绕过 ADR-006 以局部拼装替代 | 不允许 | `Gate-INT-01`、`Gate-INT-06` |
| cognition | `cognition_engine`、`response_builder` | 决策、响应拼装、最终 `AgentResult` 生成 | `required` | 必须同时具备 cognition 决策与 response build 能力 | init / preflight 直接 fail-closed；不得以 observation fallback 冒充完整 cognition path | 不允许 | `Gate-INT-01`、`Gate-INT-03` |
| tools | `tool_manager` | tool round、service bridge、result semantics | `required` | default unary profile 必须具备 runtime-owned tool seam | 原始端口缺失时 fail-closed；不得把空指针当成“当前请求未用到工具” | 仅允许非默认 profile 用受控 `null adapter` 关闭 tool lane；该模式不等于 default-ready | `Gate-INT-01`、`Gate-INT-06` |
| knowledge | `knowledge_service` | retrieval handoff、evidence projection、memory/cognition 证据消费 | `optional` | 若要声明 default unary ready，必须为 live retrieve seam | 不得继续宣称 default-ready；若 profile 未允许降级，则 fail-closed | 允许进入 `degraded`，并输出 `knowledge_unavailable` 或等价审计标签 | `Gate-INT-01`、`Gate-INT-06` |
| llm | `llm_manager` | cognition llm bridge、response synthesis、fallback 策略 | `optional` | 若要声明 default unary ready，必须为 live model path | 不得继续宣称 default-ready；若 profile 未允许 fallback/degraded，则 fail-closed | 允许进入 `degraded`，并输出 `llm_fallback` / `llm_unavailable` 或等价标签 | `Gate-INT-01`、`Gate-INT-03`、`Gate-INT-06` |

## 5. Runtime Readiness 解释规则

1. `has_live_unary_ports()` 只可继续作为“核心 required ports 已具备”的最小布尔检查，不得继续被解释为“系统已经 default unary ready”。
2. default unary ready 至少还需要额外声明 `knowledge_service` 与 `llm_manager` 的 live 状态，并把结果纳入 readiness / telemetry / diagnostics 输出口。
3. 任何 fixture gate 若依赖 `knowledge` 或 `llm` 缺失路径，只能证明 degraded 行为可复现，不能外推为 true integration ready。
4. `tools` 被 profile 显式关闭时，必须通过 runtime-owned `null adapter` 保持 seam 完整；调用方不得自行以 `nullptr` 绕过组合根。

## 6. Design -> Build 映射

| 设计决策 | 后续 Build 任务 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| `has_live_unary_ports()` 不能再单独代表 default-ready | `INT-TODO-010` | `runtime/include/RuntimeDependencySet.h` | `RuntimeDependencySetReadinessTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R RuntimeDependencySetReadinessTest --output-on-failure` |
| `knowledge` / `llm` 缺失只允许走显式 degraded path | `INT-TODO-014` | `runtime/src/AgentFacade.cpp`、`runtime/src/AgentOrchestrator.cpp`、`cognition/src/CognitionFacade.cpp` | `RuntimeRequiredOptionalPortsIntegrationTest`、`RuntimeProfileCompatibilityTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "RuntimeRequiredOptionalPortsIntegrationTest|RuntimeProfileCompatibilityTest" --output-on-failure` |
| Gate 需要把 default-ready 与 degraded-ready 分层记录 | `INT-TODO-021` | `tests/integration/agent_loop/RuntimeRequiredOptionalPortsIntegrationTest.cpp`、`tests/integration/agent_loop/RuntimeProfileCompatibilityTest.cpp` | `RuntimeRequiredOptionalPortsIntegrationTest`、`RuntimeProfileCompatibilityTest`、`LLMSubsystemSmokeIntegrationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "RuntimeRequiredOptionalPortsIntegrationTest|RuntimeProfileCompatibilityTest|LLMSubsystemSmokeIntegrationTest" --output-on-failure` |

## 7. 验证锚点

```bash
rg -n "required|optional|fail-closed|degraded|knowledge_service|llm_manager" \
  docs/ssot/SingleAgentRuntimePortMatrix.md \
  docs/architecture/DASALL_runtime子系统详细设计.md \
  docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md
```

## 8. 结论

1. `INT-OQ-01` 在本文件中被采纳：`knowledge` / `llm` 不是“完全可有可无”的增强项，而是 default-ready 声明所必需、对 degraded 运行可选的端口。
2. `INT-BLK-01` 的设计冻结出口是：矩阵已给出每个端口的模式、消费者、失败语义和 Gate 归属；后续 Build 只负责把该矩阵落实到 readiness surface、runtime 行为与 integration gate。