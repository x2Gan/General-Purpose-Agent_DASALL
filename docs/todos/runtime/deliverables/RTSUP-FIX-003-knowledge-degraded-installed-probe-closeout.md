# RTSUP-FIX-003 knowledge degraded semantics / installed positive probe closeout

来源任务：RTSUP-FIX-003
完成日期：2026-05-27
关联缺口：RTSUP-GAP-003
关联设计：`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/todos/runtime/DASALL_runtime_support组件专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md`

## 1. 任务边界

1. 本轮只收口 `apps/runtime_support` 对 knowledge optional port 的 ready / degraded / unavailable 语义与 installed positive probe traceability，不扩张到 hybrid canary、dense rollout 或 qemu / installed-package release gate。
2. authoritative 实现来源固定为 `RTSUP-TODO-007` 已落盘 helper 行为、focused tests 与 worklog 记录 #648；当前 round 的目标是把系统总记录中的旧 `Todo` 状态升级为与专项 TODO 一致的完成态。
3. owner 边界不变：runtime_support 只消费 knowledge public seam 与 installed asset probe 结果，不把 knowledge service owner 回流到 daemon/gateway entry 或 runtime control plane。

## 2. 本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| helper marker 语义 | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已写出 `:knowledge-installed-assets-ready` 与 `:knowledge-degraded:<reason>` marker | optional knowledge port 的 positive / degraded 路径已在 app live composition surface 明确分层，而不是把 factory 成功直接偷换成 ready |
| positive probe terminal state | helper 现按 `health_snapshot.last_refresh_status == Completed` 认定 installed positive probe 成功 | runtime_support 不再把 admission `Accepted` 或 refresh in-flight 误判为 ready |
| direct installed probe | `tests/integration/knowledge/KnowledgeInstalledAssetProbeIntegrationTest.cpp` 已断言 `last_refresh_status=Completed` 与 active snapshot 存在 | build-tree focused gate 已能直接证明 installed asset knowledge factory 的正向路径 |
| app composition retention | `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp` 与 `tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp` 已同时断言 `knowledge-installed-assets-ready` 正例与 `knowledge-degraded:` 负例 | daemon / gateway 对 optional knowledge marker 的 retained behavior 已进入对称 focused regression |
| authoritative traceability | `docs/todos/runtime/DASALL_runtime_support组件专项TODO.md` 的 `RTSUP-TODO-007` 与 `docs/worklog/DASALL_开发执行记录.md` 的记录 #648 已将该实现面记为 Done | 本轮不重复实现 knowledge path，而是把系统总记录与专项 TODO / worklog 对齐 |

## 3. Design -> Build 映射

| Design 目标 | Build / Test 落点 |
|---|---|
| 用 installed positive probe 区分 ready 与 degraded | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` |
| 只在 terminal success 时暴露 positive ready marker | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`KnowledgeInstalledAssetProbeIntegrationTest` |
| 在 daemon/gateway composition 保留 ready/degraded marker stratification | `DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest` |

## 4. 验证

1. `RunCtest_CMakeTools(tests=["KnowledgeInstalledAssetProbeIntegrationTest","DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest"])`
	- 结果：继续命中仓库已知泛化 `生成失败`，未返回 test-level 诊断；不将其判定为功能红灯。
2. `./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_installed_asset_probe_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_runtime_live_dependency_composition_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_access_gateway_runtime_live_dependency_composition_integration_test && printf '%s\n' PASS`
	- 结果：`PASS`。

## 5. 完成判定

满足以下条件时，`RTSUP-FIX-003` 可在系统总记录中标记为 Done：

1. 当前树仍能发现并执行 installed positive probe 与 daemon/gateway composition focused tests。
2. helper surface 保持 `knowledge-installed-assets-ready` 与 `knowledge-degraded:<reason>` 的显式分层。
3. 结论保持在 build-tree focused evidence，不越级外推为 installed / qemu / release-ready。

本轮结论：`RTSUP-FIX-003` 可在系统总记录中升级为 Done。knowledge optional port 的 ready / degraded / unavailable 语义已由 helper marker、installed positive probe 与 daemon/gateway composition focused 复验共同闭合；当前剩余工作已转向 regression matrix 与 release/qemu 证据，而不是 optional semantics 仍漂移。