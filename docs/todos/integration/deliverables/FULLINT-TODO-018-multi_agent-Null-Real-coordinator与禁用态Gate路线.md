# FULLINT-TODO-018 multi_agent Null/Real coordinator 与禁用态 Gate 路线

日期：2026-05-12  
来源任务：FULLINT-TODO-018  
关联阻塞项：FULLINT-BLK-004  
范围：`multi_agent/`、`runtime/`、`apps/runtime_support/`、`profiles/`、`tests/`、installed-package multi_agent disabled evidence

## 1. 研究结论

### 1.1 本地代码与安装态事实

本轮先以当前工作树和当前机器上的 installed package 为准，不采信历史 TODO 或历史 Gate 结论。

代码侧当前事实：

1. `multi_agent/include/MultiAgentTypes.h`、`multi_agent/include/IMultiAgentCoordinator.h`、`multi_agent/src/MultiAgentCoordinator.cpp` 与 `multi_agent/src/MultiAgentRuntimeFold.cpp` 已提供 `MultiAgentExecutionContext/Report/FoldResult`、Null/Real coordinator 工厂和 Runtime fold helper。
2. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已根据 `RuntimePolicySnapshot::multi_agent_enabled()` 注入 `multi_agent_coordinator`；`runtime/include/RuntimeDependencySet.h` 已新增 coordinator 依赖槽位。
3. `profiles/include/RuntimePolicySnapshot.h` 与 `profiles/src/RuntimePolicyProvider.cpp` 已提供 typed `enabled_modules.multi_agent` 投影，Runtime 可根据 snapshot 决定注入 Null/Real coordinator。
4. `runtime/src/AgentOrchestrator.cpp` 仍保持 Runtime owner；multi_agent 首版只输出 Observation、`compensation_hints` 与 `recovery_request` sidecar，不越权生成最终 `RecoveryOutcome`。
5. `tools/src/execution/CompensationLedger.cpp` 继续作为真实 `ToolCompensationHint` 来源；`MultiAgentRecoveryFoldIntegrationTest` 已证明 sidecar 最终仍经 `RecoveryManager::evaluate/execute/apply()` 裁定 `abort_safe`。

当前机器的 installed-package 事实：

1. `dasall --help` 未暴露 multi-agent 独立控制面命令；当前控制面仍只有 `help/version/config/ping/readiness/knowledge/run/status/cancel/diag`。
2. `/usr/share/dasall/profiles/desktop_full/runtime_policy.yaml` 与 `/usr/share/dasall/profiles/cloud_full/runtime_policy.yaml` 当前都为 `multi_agent: false`。
3. `sudo -n dasall ping --json` 返回 `disposition=completed`、`task_completed=true`、`readiness=READY`、`profile_id=desktop_full`。
4. `sudo -n dasall readiness --json` 返回 `disposition=completed`、`task_completed=true`、`state=READY`、`runtime_readiness=default-ready`、`bridge_reachable=true`。
5. `sudo -n dasall run '{"prompt":"如果当前运行路径支持工具调用，请调用 agent.dataset 并总结结果；如果当前路径不会进入工具调用，请明确说明。"}' --json --timeout-ms 120000` 当前返回 `disposition=completed`、`error.reason=task_not_completed`、无 `receipt_ref`。这说明 installed-package 现状必须以当轮实测为准，不能引用本轮较早的 `accepted_async` 观察或旧文档外推 multi_agent / tool ready。

### 1.2 外部参考

Anthropic 在 Building effective agents 中强调两点：

1. 应先采用简单、可组合、可验证的模式，再在有明确收益时增加 agent 复杂度。
2. orchestrator-workers 模式中，中心 orchestrator 负责动态拆解、委派和汇总，worker 不是第二个顶层主控。

映射到 DASALL：`multi_agent` 首版必须先实现一个受 Runtime 严格控制的协调 sidecar，并以禁用态 Gate 和可观测折叠结果为前提，而不是一次性引入完整分布式调度平面。

### 1.3 对 FULLINT-TODO-018 的可落地启发

1. 首版 Real coordinator 应该是最小可运行的 loopback coordinator，而不是一次性补齐详设中的全部子组件。
2. Runtime 必须只依赖 `IMultiAgentCoordinator` 和 typed `multi_agent_enabled()`，不能感知具体 Null/Real 类型。
3. Null coordinator 必须是显式对象，不允许继续用“没有实现所以不装配”来隐式表示禁用态。
4. `ToolCompensationHint` 和 `RecoveryOutcome` 的关系应保持为“coordinator 提供 sidecar 线索，Runtime/RecoveryManager 决定是否继续、降级或拒绝”。
5. installed-package 当前只能证明 disabled route 和主控制面未回退；Real coordinator 证据仍以 build-tree focused integration 为主，且当轮 installed 工具提示 `run` 只可记为 partial（`task_not_completed`），不能外推为 multi_agent/tool ready。

## 2. Design 原子清单

| 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 Runtime -> multi_agent 最小公共接口 | `multi_agent` 占位状态、ADR-008、MultiAgent 详设 6.2/6.6 | `IMultiAgentCoordinator`、`MultiAgentExecutionContext`、`MultiAgentExecutionReport` | Runtime 能依赖稳定接口，不再直连 placeholder |
| D2 | 冻结禁用态 Gate 路径 | source/installed profile 均为 `multi_agent: false` | typed enablement + `NullMultiAgentCoordinator` | Runtime 在禁用态下可显式装配 Null object |
| D3 | 冻结首版 Real coordinator 责任面 | `RecoveryManager` 与 `CompensationLedger` 当前真实代码 | loopback `MultiAgentCoordinator` + fold result helper | 协同结果能回到 Observation / recovery advisory 路径 |
| D4 | 冻结 focused integration 与 installed evidence 边界 | 当前 installed 工具提示 `run` 实测为 `disposition=completed` + `error.reason=task_not_completed` | focused tests + package/manual probes | 不把 build-tree 结果冒充 installed-package full ready |

D Gate 判定条件：

1. 对外公共接口、禁用态 Gate、首版 Real coordinator 和验证层级已明确。
2. Build 三件套已经锁定：代码目标、测试目标、验收命令。
3. 范围保持在 FULLINT-TODO-018，不扩张到完整多 Agent 调度子系统。

## 3. Design 决策

### 3.1 最小公共接口面

本轮只冻结以下 module-local public surface：

1. `IMultiAgentCoordinator`：Runtime 唯一依赖入口。
2. `MultiAgentExecutionContext`：承接 Runtime 传入的 trace、policy、允许域、父 checkpoint 引用和当前 Observation。
3. `MultiAgentExecutionReport`：承接 `multi_agent_result`、折叠后的 `Observation`、`compensation_hints`、局部 `recovery_request` sidecar、审计引用和 `disabled` 标记。
4. `MultiAgentRuntimeFoldResult`：用于 Runtime focused tests 校验 Observation / compensation / recovery 折叠语义，不把折叠逻辑散在 `AgentOrchestrator` 中。

### 3.2 禁用态与启用态路径

1. `profiles::RuntimePolicySnapshot` 新增 typed `multi_agent_enabled()`，由 `RuntimePolicyProvider` 直接解析 `enabled_modules.multi_agent`。
2. `apps/runtime_support::compose_minimal_live_dependency_set()` 根据 `multi_agent_enabled()` 注入 `NullMultiAgentCoordinator` 或 `MultiAgentCoordinator`。
3. `RuntimeDependencySet` 新增 coordinator 槽位，但不要求本轮把 `AgentOrchestrator` 主循环强制切到 multi-agent；首版先证明装配、fold helper 和恢复裁定边界成立。

### 3.3 首版 Real coordinator 的最小职责

1. 输入必须是 `MultiAgentRequest`，输出必须是 `MultiAgentExecutionReport`。
2. 首版只实现 loopback 协同：把 request 规范化为单个 collaboration observation，回填 `MultiAgentResult`，并在存在工具 side-effect 证据时生成 `compensation_hints` sidecar。
3. Real coordinator 不直接调用 `RecoveryManager`，只生成供 Runtime 消费的恢复建议材料；最终 `RecoveryOutcome` 仍由 Runtime owner 裁定。
4. 首版不引入 AgentRegistry、LeaseManager、DispatchScheduler、WorkerExecutionBridge 的完整实现，只保留后续可扩展接口和 focused test seam。

### 3.4 Observation / Recovery 折叠策略

1. Real coordinator 产生的协同结果必须折叠为 `contracts::Observation`，source 标识为 multi_agent collaboration，而不是直接写最终 `AgentResult`。
2. 若 `compensation_hints` 非空，只能进入 fold result sidecar，由 Runtime/RecoveryManager 再决定是否进入降级或拒绝路径。
3. 若协同结果建议 `replan` / `abort_safe` / `retry_step`，首版通过 `MultiAgentRuntimeFoldResult` 产生可被 `RecoveryManager` 接受的恢复请求材料，由 focused integration test 证明 owner 边界未越权。

## 4. Design -> Build 映射

| 设计结论 | Build 落点 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| Runtime 必须有 typed enablement 依据 | `profiles/include/RuntimePolicySnapshot.h`、`profiles/src/RuntimePolicyProvider.cpp` | 新增 `multi_agent_enabled()` | `MultiAgentDisabledByProfileIntegrationTest` | `Build_CMakeTools(buildTargets=["dasall_multi_agent_focus_integration_tests"])` |
| Runtime 只依赖统一 coordinator 接口 | `multi_agent/include/`、`runtime/include/RuntimeDependencySet.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | 注入 Null/Real coordinator | runtime composition + discoverability | `Build_CMakeTools(buildTargets=["dasall_multi_agent_focus_integration_tests","dasall_full_business_chain_discoverability"])` |
| 禁用态必须显式 Gate | `multi_agent/src/MultiAgentCoordinator.cpp`、`tests/integration/multi_agent/` | disabled report | `MultiAgentDisabledByProfileIntegrationTest` | `Build_CMakeTools(buildTargets=["dasall_multi_agent_focus_integration_tests"])` |
| Real coordinator 必须回到 Observation / recovery advisory | `multi_agent/src/MultiAgentCoordinator.cpp`、`multi_agent/src/MultiAgentRuntimeFold.cpp` | `MultiAgentExecutionReport`、fold helper | `MultiAgentCoordinatorPipelineTest`、`MultiAgentRecoveryFoldIntegrationTest` | `Build_CMakeTools(buildTargets=["dasall_multi_agent_focus_integration_tests"])` |
| 安装态只能证明 disabled/control-plane 边界 | installed profiles + `dasall` probes | `multi_agent: false`、无 multi-agent surface、tool prompt partial | installed smoke summary | `rg -n '^\s*multi_agent:' /usr/share/dasall/profiles/desktop_full/runtime_policy.yaml /usr/share/dasall/profiles/cloud_full/runtime_policy.yaml`；`dasall --help | rg -n 'multi|run|status|cancel|knowledge|tools' || true`；`sudo -n dasall ping --json`；`sudo -n dasall readiness --json`；`sudo -n dasall run '{...}' --json --timeout-ms 120000` |

## 5. Build 清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| B1 | 新增 `multi_agent/include/*` 与 Null/Real coordinator 最小实现 | disabled/positive/recovery 三条 focused tests | `Build_CMakeTools(buildTargets=["dasall_multi_agent_focus_integration_tests"])` -> PASS |
| B2 | 给 `RuntimePolicySnapshot` 补 typed `multi_agent_enabled()` 并接入 live composition | runtime composition focused integration + BC-17 discoverability | `Build_CMakeTools(buildTargets=["dasall_multi_agent_focus_integration_tests","dasall_full_business_chain_discoverability"])` -> PASS |
| B3 | 新增 Runtime fold helper，覆盖 Observation / compensation / recovery advisory | `MultiAgentRecoveryFoldIntegrationTest` | 由 `dasall_multi_agent_focus_integration_tests` acceptance 覆盖并通过 |
| B4 | 回写 deliverable、TODO、worklog，并复跑 installed-package disabled probes | package/manual probes | `rg -n '^\s*multi_agent:' ...`、`dasall --help | rg -n 'multi|run|status|cancel|knowledge|tools' || true`、`sudo -n dasall ping/readiness/run ...` |

## 6. 结果

判定：D Gate PASS，B Gate PASS。

原因：

1. 本轮已基于真实代码和 installed-package 实测完成最小 sidecar 实现，不再停留在 placeholder / design-only 结论。
2. `Build_CMakeTools(buildTargets=["dasall_multi_agent_focus_integration_tests"])` 当轮通过，discoverability 与 acceptance 同时覆盖 `MultiAgentDisabledByProfileIntegrationTest`、`MultiAgentCoordinatorPipelineTest`、`MultiAgentRecoveryFoldIntegrationTest`。
3. `Build_CMakeTools(buildTargets=["dasall_full_business_chain_discoverability"])` 当轮通过，BC-17 已从 explicit missing gate 切换为真实 discoverability 入口。
4. installed-package 当轮只证明 disabled/control-plane 边界：profile 仍为 `multi_agent: false`、CLI 无 multi-agent surface、工具提示 `run` 仅为 `task_not_completed` partial；因此不把 build-tree 结果冒充 installed-package multi_agent ready。