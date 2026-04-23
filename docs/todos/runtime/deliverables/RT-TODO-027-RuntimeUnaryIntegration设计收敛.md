# RT-TODO-027 RuntimeUnaryIntegration 设计收敛

日期：2026-04-23  
任务：RT-TODO-027  
状态：已完成

## 1. 本地证据

1. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 将 027 定义为 runtime 专项的 true cross-module unary gate；在 026 完成后，runtime 仍只证明了 subsystem-local ready，Gate-RT-11 继续受 RT-BLK-01 约束。
2. `runtime/src/AgentOrchestrator.cpp` 在本轮前只有 runtime-local stub/fixture 路径，没有一条真正消费 `IMemoryManager -> ICognitionEngine -> IToolManager -> IResponseBuilder` 的 live unary 控制链，因此即便相邻模块各自存在实现，也无法证明 runtime 真端口主成功链成立。
3. `cognition/` 在本轮前仍停留在 placeholder-only 状态；缺口不在“cognition 全量能力未完成”，而在“runtime-facing public seam 不存在”，导致 runtime 无法只依赖稳定接口来接入真实决策与响应构建。
4. memory 与 tools 已具备可复用的接口和 concrete test assembly，因此 027 的最小判别点不是补齐整个 cognition TODO，而是验证“最薄 cognition public seam + runtime live wiring + 最小真端口装配”是否足以打通 `AgentRequest -> ContextPacket -> cognition -> tools -> AgentResult`。
5. 聚焦验证已经通过：
   - `cmake --build build/vscode-linux-ninja --target dasall_runtime_unary_integration_test`
   - `ctest --test-dir build/vscode-linux-ninja -R "^RuntimeUnaryIntegrationTest$" --output-on-failure`
   - `ctest --test-dir build/vscode-linux-ninja -R "^MainFlowContractE2ETest$" --output-on-failure`
   - `ctest --test-dir build/vscode-linux-ninja -N | rg "RuntimeUnaryIntegrationTest|MainFlowContractE2ETest"`

## 2. 设计结论

1. 027 的真实解阻不需要补齐整个 cognition 子系统，只需要新增最小 runtime-facing public seam：`ICognitionEngine`、`IResponseBuilder`、`CognitionTypes`、`ActionDecision`、`BeliefUpdateHint`，并提供默认 `CognitionFacade` / `ResponseBuilder` 实现。
2. runtime production 代码仍然只消费 public interface：`IMemoryManager`、`ICognitionEngine`、`IToolManager`、`IResponseBuilder`。sqlite memory manager、builtin executor lane、registry descriptor 等 concrete implementation 只存在于 integration 装配层，不泄漏进 runtime 生产依赖面。
3. `RuntimeDependencySet` 需要从“只支持 runtime-local stub”扩展为“双轨 seam 容器”：
   - 保留既有 local stub / seeded waiting session / checkpoint 路径；
   - 仅当 `memory_manager`、`cognition_engine`、`response_builder`、`tool_manager` 齐备时，才允许 `AgentOrchestrator` 进入 live unary path。
4. live unary path 必须复用既有合法 FSM 边，不新增临时状态边：`Planning -> Reasoning -> ToolCalling -> WaitingExternal -> Reflecting -> Reasoning -> Responding -> Auditing -> Persisting -> Completed`。
5. knowledge / llm 在 027 中只保留 seam 和可见性位置，不是 Gate-RT-11 的通过前提；它们属于后续 live-path 扩展项，而不是当前 true unary integration 的最小闭环条件。

## 3. 边界 / 职责

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `AgentOrchestrator` | 串起 live unary success path，并在 runtime 内完成审计、持久化与终态收口 | 不直连 concrete memory/tool/cognition 实现 |
| `RuntimeDependencySet` | 提供 runtime-local 与 live unary 双轨 seam，并暴露 `has_live_unary_ports()` 判定 | 不承担业务决策或策略解释 |
| `ICognitionEngine` / `CognitionFacade` | 基于 `ContextPacket` 做最小 action 决策，并在 tool observation 后完成 reflect | 不持有 runtime 控制器状态 |
| `IResponseBuilder` / `ResponseBuilder` | 将 `Observation` / reflection 输出收口为 `AgentResult` | 不负责工具调用或 session 持久化 |
| `RuntimeUnaryIntegrationTest` | 组装 sqlite memory、builtin tool registry 与 cognition factories，验证 true integration gate | 不把 concrete 装配逻辑渗透到 runtime production 代码 |

## 4. 数据 / 接口说明

1. 新增的 cognition public seam：
   - `ICognitionEngine::decide(const CognitionStepRequest&)`
   - `ICognitionEngine::reflect(const ReflectionRequest&)`
   - `IResponseBuilder::build(const ResponseBuildRequest&)`
2. live unary path 的主要数据流：
   - 输入：`AgentInitRequest`、`AgentRequest`、`RuntimePolicySnapshot`、`RuntimeDependencySet`
   - 中间态：`ContextPacket`、`ActionDecision`、`ToolInvocationContext`、`Observation`
   - 输出：`AgentResult`、runtime-owned session/checkpoint anchors
3. tool policy 的关键细节不是 `ToolInvocationContext.caller_domain`，而是 `ToolManager.cpp` 内部推导出的 `requested_domain`；因此 builtin information query 在 true integration test 中必须显式放行：
   - `allowed_tool_domains = { builtin }`
   - `tool_visibility_rules = { builtin:agent.dataset }`
4. memory context 组装依赖已存在的 writeback/seed 数据；若不先写入最小 session fact，`ContextPacket` 会停在 degraded / non-ready，无法进入 live unary 主链。

## 5. 流程 / 时序

1. integration fixture 构造 runtime dependency set：sqlite-backed `IMemoryManager`、默认 cognition engine / response builder、registry-backed `IToolManager`、以及既有 runtime controllers。
2. `AgentFacade::init(...)` 建立 session 后，`AgentFacade::run(...)` 进入 `AgentOrchestrator::run_once()`。
3. orchestrator 通过 `IMemoryManager` 拉取 `ContextPacket`，检测 live unary ports 齐备后进入真端口分支。
4. `ICognitionEngine::decide(...)` 根据上下文产出 `ExecuteAction(agent.dataset)`；`IToolManager` 调用 builtin executor lane 返回 `Observation`。
5. `ICognitionEngine::reflect(...)` 与 `IResponseBuilder::build(...)` 共同收口最终 `AgentResult`。
6. runtime 内部继续沿既有审计、持久化、completed 终态路径收口，并由 `MainFlowContractE2ETest` 交叉验证共享 contract 没有被这条真链路破坏。

## 6. 文件范围

1. `cognition/CMakeLists.txt`
2. `cognition/include/CognitionConfig.h`
3. `cognition/include/CognitionTypes.h`
4. `cognition/include/ICognitionEngine.h`
5. `cognition/include/IResponseBuilder.h`
6. `cognition/include/belief/BeliefUpdateHint.h`
7. `cognition/include/decision/ActionDecision.h`
8. `cognition/src/CognitionFacade.cpp`
9. `runtime/CMakeLists.txt`
10. `runtime/include/RuntimeDependencySet.h`
11. `runtime/src/AgentOrchestrator.cpp`
12. `tests/integration/agent_loop/CMakeLists.txt`
13. `tests/integration/agent_loop/RuntimeUnaryIntegrationTest.cpp`

## 7. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| cognition 最小 public seam | `cognition/include/*`、`cognition/src/CognitionFacade.cpp` |
| runtime live unary wiring | `runtime/include/RuntimeDependencySet.h`、`runtime/src/AgentOrchestrator.cpp` |
| true integration fixture assembly | `tests/integration/agent_loop/RuntimeUnaryIntegrationTest.cpp` |
| true integration discoverability | `tests/integration/agent_loop/CMakeLists.txt` |

## 8. Build 三件套

1. 代码目标：新增 cognition 最小 public seam，扩展 runtime live unary path，并注册 `RuntimeUnaryIntegrationTest`。
2. 测试目标：`RuntimeUnaryIntegrationTest`、`MainFlowContractE2ETest`。
3. 验收命令：
   - `cmake --build build/vscode-linux-ninja --target dasall_runtime_unary_integration_test`
   - `ctest --test-dir build/vscode-linux-ninja -R "^RuntimeUnaryIntegrationTest$" --output-on-failure`
   - `ctest --test-dir build/vscode-linux-ninja -R "^MainFlowContractE2ETest$" --output-on-failure`
   - `ctest --test-dir build/vscode-linux-ninja -N | rg "RuntimeUnaryIntegrationTest|MainFlowContractE2ETest"`

## 9. 风险与回退

1. 如果后续把 runtime 直接绑定到 cognition / memory / tools 的 concrete implementation，而不是继续只依赖公共接口，027 的 seam 隔离会被破坏，未来 profile 或平台裁剪会重新失效。
2. 027 只证明 unary success path 的 true integration ready；true-port session persist、dependency unavailable live path 等更宽范围的真端口回归，仍应作为后续独立任务处理。
3. 当前工作区内 `RunCtest_CMakeTools` 生成失败，因此本轮 gate 证据统一使用 `build/vscode-linux-ninja` 上的直接 `ctest --test-dir ...` 结果；在 CMake Tools runner 恢复前，不应把该工具的失败误记为 runtime gate 失败。