# RT-FIX-004 deadline / cancellation closeout

来源任务：RT-FIX-004
完成日期：2026-05-21
关联缺口：RT-GAP-004
关联设计：`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/todos/runtime/deliverables/RT-TODO-006-RuntimeErrorCode与CancellationToken设计收敛.md`、`docs/todos/runtime/deliverables/RT-TODO-011-IScheduler与SchedulerTicket设计收敛.md`、`docs/todos/runtime/deliverables/RT-TODO-015-BudgetController设计收敛.md`、`docs/todos/runtime/deliverables/RT-TODO-027-RuntimeUnaryIntegration设计收敛.md`

## 1. 任务边界

1. 本轮只收口 runtime owner 的 deadline / cancellation hot path，不扩张到 observability、installed package、release runner 或 qemu 证据。
2. authoritative 问题定义固定为：`AgentRequest.timeout_ms` / `deadline_at` 与 `RuntimeBudget.max_latency_ms` 必须在 `AgentOrchestrator` 内收口为同一个 effective deadline，并贯通到 `LLMGenerateRequest`、`ToolRequest`、`ToolInvocationContext`、`SchedulerTicket`；外部调用返回晚到成功结果时，runtime 必须折叠为 timeout，而不能继续当作成功写入当前 turn。
3. 用户已明确禁止使用 qemu / kvm；本轮只使用 build-tree focused build 与 direct binary tests 作为权威证据。

## 2. 本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| effective deadline owner | `runtime/src/AgentOrchestrator.cpp` 新增 request/budget deadline helper，使用 `normalize_timeout_fields()` 归一化 `AgentRequest.timeout_ms` / `deadline_at`，并与 `RuntimeBudget.max_latency_ms` 取最早 deadline | runtime 不再把 request timeout、budget latency 和 scheduler token 拆成互不相干的三套时间语义 |
| direct LLM timeout propagation | `make_runtime_response_llm_request()` 现按 effective deadline 收紧 `LLMRequest.timeout_ms`；direct LLM path 在调用前后使用共享 `CancellationToken` 做 timeout 裁定，并把晚到成功折叠成 `RT_E_600_LLM_TIMEOUT` | LLM 慢调用不再因为 profile timeout 较大而越过请求 deadline；late success 不再污染当前 turn |
| tool timeout propagation | `make_tool_request()`、`make_tool_invocation_context()` 与 `tools/src/bridge/ToolServiceBridge.cpp` 现共同消费 request-level remaining timeout budget；`ServiceCallContext.deadline_ms` 会被 clamp 到 request live budget | tools/services lane 不再只看 ToolIR/profile timeout，runtime request deadline 已进入 tool bridge 的实际 deadline 计算 |
| worker / scheduler binding | live tool round 与 runtime-local assembly 的 `SchedulerTicketRequest` 现都绑定同一个 shared `CancellationToken`；tool round 在 invoke 前后都会检查 cancel/timeout，并在 invoke 返回后立即 release worker | worker hot path 不再持有独立的新 token；tool 返回晚到成功后也不会继续占着 worker 或走后续 waiting/reflection 写回 |
| focused regression | 新增 `tests/integration/agent_loop/RuntimeCancellationPropagationIntegrationTest.cpp`，并更新 `tests/integration/agent_loop/CMakeLists.txt`；同时更新 `tests/unit/tools/ToolInvocationContextSurfaceTest.cpp` 与 `tests/unit/tools/ToolServiceBridgeTest.cpp` | direct LLM/tool 两条主路径现在都有红灯回归锚点；tools surface 与 deadline clamp 也有单测锁定 |

## 3. 设计结论

1. effective deadline 的 authoritative owner 继续固定在 runtime，而不是把 `CancellationToken` 或 absolute deadline 直接泄漏进 tools public surface。tools 只接收 runtime 投影出来的 remaining timeout budget，依赖方向保持从 runtime 到 tools 的单向收口。
2. `CancellationToken` 现在既表达 request timeout/deadline，也表达 runtime latency budget 的最早截止点；SchedulerTicket、direct LLM path 与 tool hot path 共享同一个 token state，因此“请求已过期”不再需要在各处各算一遍。
3. late result 语义与 cognition 的 stage-timeout 口径对齐：一旦 effective deadline 已过，runtime 就以 timeout 结果 fail-closed，而不是接受迟到的成功值继续推进 waiting checkpoint、session 绑定或 response folding。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 落点 |
|---|---|
| 在 runtime owner 内收口 request timeout/deadline 与 latency budget | `runtime/src/AgentOrchestrator.cpp` |
| 将 effective deadline 投影到 direct LLM request | `runtime/src/AgentOrchestrator.cpp`、`tests/integration/agent_loop/RuntimeCancellationPropagationIntegrationTest.cpp` |
| 将 remaining timeout budget 投影到 ToolInvocationContext / ServiceCallContext | `tools/include/ToolInvocationContext.h`、`tools/src/bridge/ToolServiceBridge.cpp`、`tests/unit/tools/ToolInvocationContextSurfaceTest.cpp`、`tests/unit/tools/ToolServiceBridgeTest.cpp` |
| 将 shared cancellation token 绑定 scheduler ticket 与 tool round pre/post checks | `runtime/src/AgentOrchestrator.cpp` |
| 锁定 direct LLM/tool late-result timeout regression | `tests/integration/agent_loop/RuntimeCancellationPropagationIntegrationTest.cpp` |
| 保持 runtime 现有 required/optional port gate 不回退 | `tests/integration/agent_loop/RuntimeRequiredOptionalPortsIntegrationTest.cpp` |

## 5. D Gate

1. 范围单一：只处理 `RT-FIX-004` / `RT-GAP-004`。
2. 本轮不扩张到 runtime waiting-state 新策略、installed package / app-binary / release-runner 级证据，也不把当前 build-tree 结论外推为 `RT-GAP-008` 已关闭。
3. 本轮不使用 qemu / kvm；更高层环境证据继续留给后续 runtime / packaging 任务。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_runtime_cancellation_propagation_integration_test","dasall_tool_invocation_context_surface_unit_test","dasall_tool_service_bridge_unit_test"])`：通过。
2. `RunCtest_CMakeTools(...)`：本仓库当前仍返回已知泛化 `生成失败`，未提供 test-level 失败诊断，因此继续以 direct binaries 作为权威 test evidence。
3. `./build/vscode-linux-ninja/tests/unit/tools/dasall_tool_invocation_context_surface_unit_test`：通过。
4. `./build/vscode-linux-ninja/tests/unit/tools/dasall_tool_service_bridge_unit_test`：通过。
5. `./build/vscode-linux-ninja/tests/unit/runtime/dasall_runtime_cancellation_token_unit_test`：通过。
6. `./build/vscode-linux-ninja/tests/integration/agent_loop/dasall_runtime_cancellation_propagation_integration_test`：通过。
7. `./build/vscode-linux-ninja/tests/integration/agent_loop/dasall_runtime_required_optional_ports_integration_test`：通过。

## 7. 完成判定

1. `RT-GAP-004` 已在当前树关闭：runtime 现已把 effective deadline / cancellation 贯通到 direct LLM、tool bridge 与 scheduler ticket hot path。
2. `RuntimeCancellationPropagationIntegrationTest` 证明 direct LLM 与 tool round 的晚到成功结果都会被折叠为 timeout，并分别返回 `RT_E_600_LLM_TIMEOUT` / `RT_E_601_TOOL_TIMEOUT`。
3. `ToolInvocationContextSurfaceTest` 与 `ToolServiceBridgeTest` 已锁定 request-level remaining timeout budget surface，不允许 tools bridge 再悄悄回到只看 ToolIR/profile timeout 的旧行为。
4. `RuntimeRequiredOptionalPortsIntegrationTest` 继续保持绿色，说明本轮 deadline/cancellation 接线没有打坏 runtime 既有 required/optional port gate。