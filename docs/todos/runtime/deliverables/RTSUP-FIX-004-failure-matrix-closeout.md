# RTSUP-FIX-004 owner / fail-closed / marker regression matrix closeout

来源任务：RTSUP-FIX-004
完成日期：2026-05-27
关联缺口：RTSUP-GAP-004
关联设计：`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/ssot/SystemIntegrationGateMatrix.md`、`docs/todos/runtime/DASALL_runtime_support组件专项TODO.md`

## 1. 任务边界

1. 本轮只收口 runtime_support shared helper 的 owner / fail-closed / marker regression matrix traceability，不扩张到 package smoke、qemu 或 release-preflight gate。
2. authoritative 实现来源固定为 `RTSUP-TODO-008` 已落盘 focused tests 与 discoverability；当前 round 的目标是把系统总记录中的旧 `Todo` 状态升级为与专项 TODO 一致的完成态。
3. 本轮不新增 helper 行为，只确认 downstream contract 已被 focused tests、daemon/gateway symmetry 与 discoverability 绑定住。

## 2. 本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| owner symmetry | `tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp` 同时以 `daemon.local-control-plane` 与 `gateway.http-unary` 作为 composition owner fixture | helper contract 不再只在 daemon 正例成立，gateway 也被拉进同一 regression matrix |
| marker stratification | `RuntimeLiveCompositionFailureMatrixTest.cpp` 已对 `knowledge-degraded:` marker 做 retained behavior 断言 | knowledge optional path 的 degraded marker 不会在后续修改中被 ready/fatal 语义混写 |
| required fail-closed | 专项 TODO 已把 required ports missing、daemon/gateway symmetry、knowledge unavailable marker 与 multi-agent disabled seam 列为同一 gate | helper 修改后若回退为 silent fallback 或 owner 不清，会直接打爆 focused matrix |
| dedicated scope separation | `RuntimeKnowledgeHybridCanaryIntegrationTest.cpp` 已单独覆盖 positive hybrid canary path；本轮把 `RuntimeLiveCompositionFailureMatrixTest.cpp` 收回 004 原始的 owner/fail-closed/marker 范围 | 004 的回归门不再被后续 knowledge hybrid/automation 能力误绑，matrix 重新只证明本任务宣称的 contract |
| discoverability | `tests/integration/access/CMakeLists.txt` 已注册 `RuntimeLiveCompositionFailureMatrixTest`，并与 daemon/gateway composition tests 一起进入同一 focused topology | regression matrix 不只是孤立测试文件，而是可被 CMake / gate discoverability 找到的正式入口 |

## 3. Design -> Build 映射

| Design 目标 | Build / Test 落点 |
|---|---|
| 固定 daemon / gateway owner symmetry | `RuntimeLiveCompositionFailureMatrixTest`、`DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest` |
| 固定 required-missing fail-closed 语义 | `RuntimeLiveCompositionFailureMatrixTest` |
| 固定 knowledge degraded marker stratification | `RuntimeLiveCompositionFailureMatrixTest`、daemon/gateway composition tests |
| 固定 multi-agent disabled seam 不回退 | `MultiAgentDisabledByProfileIntegrationTest` |
| 固定 focused discoverability | `tests/integration/access/CMakeLists.txt` |

## 4. 验证

1. `RunCtest_CMakeTools(tests=["DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest","MultiAgentDisabledByProfileIntegrationTest","RuntimeLiveCompositionFailureMatrixTest"])`
	- 结果：继续命中仓库已知泛化 `生成失败`，未返回 test-level 诊断；不将其判定为功能红灯。
2. `Build_CMakeTools(buildTargets=["dasall_access_daemon_runtime_live_dependency_composition_integration_test","dasall_access_gateway_runtime_live_dependency_composition_integration_test","dasall_access_runtime_live_composition_failure_matrix_integration_test","dasall_multi_agent_disabled_by_profile_integration_test"])`
	- 结果：通过；本轮额外重建 `dasall_access_runtime_live_composition_failure_matrix_integration_test`，确认 matrix scope 收窄后的 target 可正常链接。
3. `./build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_runtime_live_dependency_composition_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_access_gateway_runtime_live_dependency_composition_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_access_runtime_live_composition_failure_matrix_integration_test && ./build/vscode-linux-ninja/tests/integration/multi_agent/dasall_multi_agent_disabled_by_profile_integration_test && printf '%s\n' PASS`
	- 结果：`PASS`。

## 5. 完成判定

满足以下条件时，`RTSUP-FIX-004` 可在系统总记录中标记为 Done：

1. 当前树仍能发现并执行 owner / fail-closed / marker regression matrix 相关 focused tests。
2. daemon/gateway symmetry、required-missing fail-closed 与 knowledge degraded marker stratification 继续保持在同一 regression gate 内。
3. 结论保持在 build-tree focused evidence，不越级外推为 installed / qemu / release-ready。

本轮结论：`RTSUP-FIX-004` 可在系统总记录中升级为 Done。`RuntimeLiveCompositionFailureMatrixTest` 已重新收敛到 004 原始宣称的 owner / fail-closed / marker contract；positive hybrid canary 与 automation 子面继续留在各自的专门 knowledge focused tests，不再绑住 004 的回归门。