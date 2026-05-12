# FULLINT-TODO-016 tools/services runtime production caller 边界验证

日期：2026-05-12
来源任务：FULLINT-TODO-016
范围：runtime caller -> ToolManager -> BuiltinExecutorLane -> services call context -> ToolInvocationEnvelope；installed-package 手工探针只用于记录当前 L4 边界，不外推 package-ready

## 1. Phase -1 任务确认

本轮只推进 `FULLINT-TODO-016`。

可执行性判定：PASS。

1. 前置 `FULLINT-TODO-012`、`FULLINT-TODO-015` 已完成，当前工作树在本轮启动前为干净状态。
2. 本轮按真实代码现状先核对 runtime / tools / services 调用面，再以 build-tree focused 验证和实际 installed-package 运行结果收口，不采信既有 TODO 或测试绿灯本身作为完成证据。
3. 研究中发现一个同轮最小代码缺口：`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 宣称 `visible_tools = {"agent.dataset"}`，但 production composition 直接构造默认 `ToolManager()`，其默认 registry 为空，无法保证 visible tool surface 与真正注册进治理链的 builtin descriptor 一致。
4. `runtime/src/AgentOrchestrator.cpp` 的 `make_tool_invocation_context()` 与 `make_tool_request()` 已经能稳定生成 `request_id/session_id/trace_id/tool_call_id/goal_id/runtime_budget/timeout`，因此本轮不需要扩改 runtime caller 语义，只需把 production composition 与 focused 验证对齐到这条既有边界。

## 2. 研究输入

### 2.1 本地证据

| 输入 | 本轮采用方式 |
|---|---|
| `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | 确认 production composition 当前注入了 `visible_tools={"agent.dataset"}`，但未显式注册 builtin descriptor，也未显式装配 builtin lane + service bridge。 |
| `apps/runtime_support/CMakeLists.txt` | 确认 runtime_support 目标此前没有显式暴露 tools->services compile boundary。 |
| `runtime/src/AgentOrchestrator.cpp` | 核对 `make_tool_invocation_context()`、`make_tool_request()`：`caller_domain=runtime.agent_orchestrator`，并保留 `request_id/session_id/trace_id/tool_call_id/goal_id/runtime_budget/timeout`。 |
| `tools/src/ToolManager.cpp` | 确认默认 `ToolManager::default_dependencies()` 会创建空 registry；若调用方不主动注册 builtin descriptor，则 `descriptor_missing` 是真实缺口。 |
| `tools/src/execution/BuiltinExecutorLane.cpp` / `tools/src/bridge/ToolServiceBridge.cpp` | 确认 builtin query/action/diagnose 最终通过 `IDataService` / `IExecutionService`，并由 `ToolServiceBridge` 负责生成 `ServiceCallContext`。 |
| `tests/integration/agent_loop/RuntimeUnaryIntegrationTest.cpp` | 原测试只证明 runtime unary true-port path 能 completed，但没有断言 caller context 是否真的进入 `IDataService`。 |
| `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp` | 原测试只验证 readiness / optional ports，不验证 production composition 下的 `tool_manager->invoke()` 是否真的可用。 |
| installed package probes | 用 `dpkg-buildpackage`、`pkg_smoke_install.sh --explicit-start-check`、`systemctl start dasall-daemon.service`、`dasall ping/run/status` 记录当前安装态行为；这些结果只用于当前边界，不替代 L2 focused 验收。 |

### 2.2 外部参考

| 参考 | 对本任务的约束 |
|---|---|
| OpenTelemetry Context Propagation | 调用方跨服务边界时必须显式保留 trace id / parent relationship，接收方才能把下游操作关联回同一调用链；这与本轮在 `RuntimeUnaryIntegrationTest` 中锁定 `request_id/session_id/trace_id/tool_call_id/goal_id/budget_guard/deadline_ms` 的 caller context 投影一致。 |

## 3. Design 原子项

| 原子项 | 设计目标 | 输入依据 | 完成判定 | 风险与回退 |
|---|---|---|---|---|
| D1 | 收敛 production composition 的 visible tool surface | `RuntimeLiveDependencyComposition.cpp`、`ToolManager.cpp` | `visible_tools` 中出现的 runtime builtin 必须真实注册进 ToolRegistry，再交给 ToolManager | 若 registry 仍为空，只能记录 blocker，不宣称 production caller boundary 已闭合 |
| D2 | 显式保留 tools->services 装配边界 | `BuiltinExecutorLane.cpp`、`ToolServiceBridge.cpp` | production composition 中的 builtin path 必须经 builtin lane + service bridge，而不是继续依赖隐式空默认装配 | 若 runtime_support 目标无法看见 services public headers，则补最小 CMake 依赖而不扩面 |
| D3 | 把 runtime caller context 变成可验证断言 | `AgentOrchestrator.cpp`、`RuntimeUnaryIntegrationTest.cpp` | `RuntimeUnaryIntegrationTest` 必须证明 `request_id/session_id/trace_id/tool_call_id/goal_id/budget_guard/deadline_ms` 进入 `IDataService` | 不新增新的 caller abstraction，直接复用现有 runtime caller helper |
| D4 | 区分 L2 closeout 与 L4 installed evidence | actual package probes | build-tree focused 成功后可关闭本任务的 L2 owner；installed-package 若仍只给 `task_not_completed` / `accepted_async` / `status_missing`，必须显式写为 partial | 不用 package partial 结果反推本轮失败，也不把 package smoke 绿灯写成 tools runtime caller 已经 L4 ready |

## 4. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 通过条件 |
|---|---|---|
| runtime visible tool 必须有真实 descriptor | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | production composition 生成的 `tool_manager->invoke(agent.dataset)` 不再命中空 registry |
| runtime_support 显式装配 tools->services compile boundary | `apps/runtime_support/CMakeLists.txt` | runtime_support 目标能编译 `BuiltinExecutorLane` / `ToolServiceBridge`，并与 `dasall_services` 一致链接 |
| runtime caller context 必须落入 `ServiceCallContext` | `tests/integration/agent_loop/RuntimeUnaryIntegrationTest.cpp` | `IDataService` 捕获到 `request_id/session_id/trace_id/tool_call_id/goal_id/budget_guard/deadline_ms` |
| production composition 不能只验证 readiness | `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/access/CMakeLists.txt` | ready baseline 之外，还要断言 `tool_manager->invoke()` 成功产出 `ToolInvocationEnvelope + Observation + ObservationDigest` |
| gate 结果与 package 事实分层记录 | 本交付物、专项 TODO、worklog | `ToolServicesSmokeIntegrationTest` / `RuntimeUnaryIntegrationTest` / production composition test 为 L2；package 手工运行结果只记录 partial |

## 5. D Gate

| Gate | 判定 | 证据 |
|---|---|---|
| 范围单一 | PASS | 只处理 `FULLINT-TODO-016` 和它的同轮最小 production composition 缺口。 |
| 前置依赖 | PASS | `FULLINT-TODO-012`、`015` 已完成。 |
| Build 三件套 | PASS | 代码目标、测试目标、验收命令都已在 §4 锁定。 |
| 不外推 installed-package | PASS | package 结果只记录本轮真实边界，不冒充 L2 build-tree closeout。 |

## 6. B 阶段执行结果

### 6.1 代码落点

| Build 项 | 文件 | 结果 |
|---|---|---|
| production tool manager assembly | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | 新增 runtime builtin descriptor `agent.dataset`，production composition 改为显式装配 `ToolRegistry + BuiltinExecutorLane + ToolServiceBridge + ToolManager`；`visible_tools` 与 registry 对齐，并补 `tool-services-caller-ready` evidence tag。 |
| runtime_support compile boundary | `apps/runtime_support/CMakeLists.txt` | 显式加入 `services/include` 与 `dasall_services`，避免 runtime_support 继续通过隐式 include path 使用 tools->services 装配。 |
| runtime caller projection test | `tests/integration/agent_loop/RuntimeUnaryIntegrationTest.cpp` | 新增 `CaptureDataService`，把 runtime unary fixture 的 builtin query 真正落到 `IDataService`，并断言 `ServiceCallContext` 字段完整保留。 |
| production composition caller verification | `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp` | 在 readiness 断言之外，新增 `tool_manager->invoke(agent.dataset)` 成功路径，证明 production composition 产出的 tool manager 真正经过 route + projection。 |
| tools public surface consumption | `tests/integration/access/CMakeLists.txt` | 为 daemon composition integration test 显式链接 `dasall_tools`，使用 `IToolManager` / `ToolInvocationContext` 公开头。 |

### 6.2 focused build / test

| 命令 | 结果 |
|---|---|
| `Build_CMakeTools(buildTargets=["dasall_access_daemon_runtime_live_dependency_composition_test"])` | PASS；修复 include / services link 后，runtime_support 和 daemon composition integration target 构建通过。 |
| `RunCtest_CMakeTools(tests=["DaemonRuntimeLiveDependencyCompositionTest"])` | PASS；新增 production `tool_manager->invoke()` 断言通过。 |
| `Build_CMakeTools(buildTargets=["dasall_runtime_unary_integration_test"])` | PASS。 |
| `RunCtest_CMakeTools(tests=["RuntimeUnaryIntegrationTest"])` | PASS；`CaptureDataService` 证明 runtime caller context 进入 `IDataService`。 |
| `Build_CMakeTools(buildTargets=["dasall_gate_int_07","dasall_runtime_unary_integration_test","dasall_access_daemon_runtime_live_dependency_composition_test"])` | PASS；`dasall_gate_int_07` 当轮执行并通过。 |
| `RunCtest_CMakeTools(tests=["ToolServicesSmokeIntegrationTest","RuntimeUnaryIntegrationTest","DaemonRuntimeLiveDependencyCompositionTest"])` | PASS；3/3 passed。 |

### 6.3 installed-package 实际运行结果

| 探针 | 命令 | 实际结果 | 判定 |
|---|---|---|---|
| package rebuild | `dpkg-buildpackage -us -uc -b -nc` | PASS；日志显示生成 `../dasall-common_0.1.0-1_all.deb`、`../dasall_0.1.0-1_all.deb`、`../dasall-cli_0.1.0-1_amd64.deb`、`../dasall-daemon_0.1.0-1_amd64.deb` | PASS |
| fresh install smoke | `bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` | PASS；脚本报告 `install smoke passed` | PASS |
| manual daemon start + ping | `sudo -n systemctl start dasall-daemon.service && sudo -n dasall ping --json` | PASS；`disposition=completed`，payload 含 `readiness":"READY"`，daemon log 显示 `runtime readiness=default-ready` | PASS |
| manual simple run | `sudo -n dasall run '{"prompt":"请用LLM回答：2+3等于几？只给出简短答案。"}' --json --timeout-ms 120000` | `disposition=completed` 但 `error.reason=task_not_completed`、`exit_code=5` | L4 partial；不能作为 tools/services runtime caller 成功证据 |
| manual tool prompt | `sudo -n dasall run '{"prompt":"如果当前运行路径支持工具调用，请调用 agent.dataset 并总结结果；如果当前路径不会进入工具调用，请明确说明。"}' --json --timeout-ms 120000` | `disposition=accepted_async`、`receipt_ref=receipt-for-ticket-1`、`exit_code=0` | 记录当前行为，不宣称完成 |
| receipt follow-up | `sudo -n dasall status --receipt receipt-for-ticket-1 --json` | `disposition=rejected`、`error.reason=status_missing`、`access_error_domain=receipt`、`exit_code=5` | 证明安装态当前 async/tool receipt surface 仍是 partial，不可外推 L4 ready |

结论：本轮 `FULLINT-TODO-016` 的完成归属仍是 L2 build-tree closeout。installed-package 当前只证明 package smoke 没有因为本轮改动回退；手工 `run` / async receipt 行为仍不足以把 tools/services runtime caller 升级为 L4 ready。

## 7. Build 合规复核

| 检查项 | 结果 |
|---|---|
| 代码注释 | 本轮未新增叙事性注释；新增 helper 和测试断言均通过类型/命名自解释。 |
| 正负例覆盖 | 正例：runtime unary caller context 命中 `IDataService`、production composition `tool_manager->invoke()` 成功；负例：installed-package `status --receipt` 返回 `status_missing`，明确只记 partial。 |
| 测试发现性 / gate | `RuntimeUnaryIntegrationTest`、`ToolServicesSmokeIntegrationTest`、`DaemonRuntimeLiveDependencyCompositionTest` 均可被 CTest 发现并通过；`dasall_gate_int_07` 当轮通过。 |
| TODO / 交付物 / worklog 回写 | 本交付物、专项 TODO 与 worklog 同步回写 L2 closeout 与 L4 partial 边界。 |
| 无关改动隔离 | 本轮只修改 runtime_support、对应 focused tests 与文档回写，不扩到 knowledge/memory/packaging 逻辑。 |

## 8. Gate 判定

| Gate | 判定 | 证据 |
|---|---|---|
| D Gate | PASS | 见 §5。 |
| B Gate | PASS | build-tree focused build / tests 全部通过，production composition 和 runtime unary caller boundary 都有真实代码证据。 |
| installed-package L4 | PARTIAL | package smoke 通过，但手工 `run` / async receipt 仍不足以证明 runtime production caller 已在 installed-package 闭合。 |

## 9. 结果与后继任务

1. `FULLINT-TODO-016` 已完成：build-tree 现在能同时证明 runtime unary tool path fixture 与 production composition 都仍经 `registry -> validator -> policy -> route -> services -> projection`，没有绕过治理。
2. 本轮没有修改 `runtime/src/AgentOrchestrator.cpp` 或 `services/src/` 实现，因为 caller context 构造和 services interface seam 已存在；真实缺口在于 production composition 没有把 advertised visible tool surface 接到真正注册的 ToolManager。
3. installed-package 当前仍只可写为 partial：manual simple `run` 返回 `task_not_completed`，tool prompt 进入 `accepted_async` 后 receipt 查询落入 `status_missing`；这说明 L4 package runtime caller 还不能写成 ready。
4. 后续若要提升到 package / release evidence，应由后继任务补 installed-package async/tool receipt owner 或单独冻结 installed tool prompt 策略，而不是拿本轮 L2 focused 结果外推。