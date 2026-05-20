# TOOL-FIX-011 runtime production 可见工具面收敛

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `TOOL-GAP-009`。
2. 本轮目标：把 runtime production live composition 的默认可见工具面与 builtin catalog 对齐，使 `agent.terminal` 与 `agent.dataset` 同时经由 governed `IToolManager -> builtin -> services` 路径可证；同时证明高风险 terminal action 在未确认时 fail-closed、确认后可走 builtin lane。
3. 完成判定：`compose_runtime_tool_manager()` 与 runtime live dependency set 已同时暴露 `agent.dataset` / `agent.terminal`；daemon / gateway app composition focused tests 已覆盖 terminal visible surface、unconfirmed deny 与 confirmed allow；`/usr/lib/dasall/dasall-tools-installed-proof --json` 与 `build/tool-fix-009-package-smoke/tools-installed-proof.json` 已返回 `agent_terminal_visible=true`、`terminal_confirmation_denied=true`、`terminal_invocation_succeeded=true` 与 `terminal_route_kind=builtin`；本轮不依赖 qemu / kvm，也不把本机 authoritative evidence 外推为 machine-isolated release-ready。

## 2. 本地证据

1. `tools/src/registry/BuiltinCatalog.cpp` 与 concrete builtin wrapper 已在上一轮固定暴露 `agent.dataset` / `agent.terminal` 两个 builtin descriptor，但 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 原先只向 runtime live path 注册 `agent.dataset`，并把 `visible_tools` 固定为 `{"agent.dataset"}`。
2. `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp` 与 `tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp` 在本轮前只能证明 dataset query builtin lane，可见工具面与 builtin catalog 不一致时，无法覆盖 terminal high-risk action 的 confirmation gate。
3. 本轮已更新 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`：`compose_runtime_tool_manager()` 现同时注册 `make_runtime_terminal_descriptor()` 与 `make_runtime_dataset_descriptor()`，`dependency_set->visible_tools` 收口为 `{"agent.dataset", "agent.terminal"}`。
4. 本轮已更新 `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp` 与 `tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp`：focused app composition 现在同时断言 `agent.terminal` 可见、未确认 action 返回 `policy.confirmation_required`、确认后的 terminal invocation 经 builtin/services live path 成功执行。
5. 本轮已更新 `apps/daemon/src/ToolsInstalledProofRunner.h/.cpp` 与 `apps/daemon/src/ToolsInstalledProofMain.cpp`：installed helper 现在把 terminal visible surface、confirmation gate、confirmed terminal invocation、terminal payload / route / projection 一并纳入 proof contract。
6. 本轮已更新 `tests/unit/apps/daemon/ToolsInstalledProofRunnerTest.cpp` 与 `scripts/packaging/pkg_smoke_install.sh`：unit fallback 与 package smoke 已把 terminal proof 变成 required acceptance，而不是仅检查 dataset query。
7. 本轮本机 authoritative installed evidence 已确认：`DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=build/tool-fix-009-package-smoke bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` 通过；`build/tool-fix-009-package-smoke/tools-installed-proof.json` 已包含 `visible_tools=["agent.dataset", "agent.terminal"]`、`agent_terminal_visible=true`、`terminal_confirmation_denied=true`、`terminal_invocation_succeeded=true`、`terminal_projection_present=true`、`terminal_route_kind=builtin` 与 `terminal_failure_reason_code=policy.confirmation_required`。

## 3. 设计结论

### 3.1 根因收口

1. `TOOL-GAP-009` 的根因不是 builtin terminal wrapper 缺失，也不是 `ToolManager` 主链没有 confirmation gate；真正缺口是 runtime production live composition 自己把 registry / visible surface 缩成了 dataset-only，导致 builtin catalog 与 runtime 可见面分叉。
2. 因为 runtime live surface 被缩面，build-tree 和 installed helper 都只能证明 dataset query 正向路径成立，无法证明 high-risk terminal action 在 production services backend 上既能 fail-closed 又能 confirmed allow。
3. 正确收口方式不是改写 `ToolManager` owner、也不是额外增加第二套 action path，而是把 runtime live composition 与既有 builtin catalog 对齐，再用 app composition test 与 installed helper 收口 terminal proof contract。

### 3.2 本轮决定

1. runtime production 默认可见工具面明确固定为 `agent.dataset` + `agent.terminal`。
2. `agent.terminal` 继续保持 high-risk action 语义：未确认调用必须返回 `policy.confirmation_required`，确认后的调用才允许命中 builtin lane 与 live services backend。
3. installed helper / package smoke 的 authoritative proof contract 继续由 packaging owner 持有，但 proof 内容从 dataset-only 扩大为 dataset + terminal 双路径。

### 3.3 边界与不外推项

1. 本轮不把 skill runtime 拉入 runtime production 主链；`TOOL-GAP-010` 继续独立保留。
2. 本轮不新增 qemu / autopkgtest / soak 结论；`TOOL-GAP-012` 继续保留 machine-isolated hardening 范围。
3. 本轮不把 terminal visible surface 收口误写为 ResultProjector complex payload regression 已完成；`TOOL-GAP-011` 继续独立保留。
4. 本轮不改变 Runtime / ToolManager owner 边界：Runtime 仍拥有 live composition 与 visible surface 裁定权，Tools 仍只负责 governed execution pipeline。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | runtime live composition 必须与 builtin catalog 对齐，默认可见 `agent.dataset` / `agent.terminal` | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` |
| D2 | terminal high-risk action 需在 app composition focused tests 中覆盖 deny / allow 双路径 | `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp` |
| D3 | installed helper 必须把 terminal proof 收口为 required contract | `apps/daemon/src/ToolsInstalledProofRunner.h`、`apps/daemon/src/ToolsInstalledProofRunner.cpp`、`apps/daemon/src/ToolsInstalledProofMain.cpp` |
| D4 | package smoke / installed authoritative artifact 必须扩展到 terminal 字段 | `tests/unit/apps/daemon/ToolsInstalledProofRunnerTest.cpp`、`scripts/packaging/pkg_smoke_install.sh`、`scripts/packaging/README.md` |
| D5 | gap closeout 结论必须回写 tools 总账、deliverable 索引与工作日志 | 本文档、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/todos/tools/deliverables/DELIVERABLES-INDEX.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 5. Build 三件套

1. 代码目标：runtime live composition 注册 terminal descriptor 并公开 terminal visible surface；installed helper / package smoke 同步升级 terminal proof contract。
2. 测试目标：focused app composition tests 覆盖 terminal visible + confirmation gate；installed helper unit fallback 与本机 installed package smoke 覆盖 terminal artifact 字段。
3. 验收命令：
   - `cmake --build build/vscode-linux-ninja --target dasall_access_daemon_runtime_live_dependency_composition_integration_test dasall_access_gateway_runtime_live_dependency_composition_integration_test dasall-daemon_tools_installed_proof_runner_unit_test -j4`
   - `build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_runtime_live_dependency_composition_integration_test`
   - `build/vscode-linux-ninja/tests/integration/access/dasall_access_gateway_runtime_live_dependency_composition_integration_test`
   - `build/vscode-linux-ninja/tests/unit/apps/daemon/dasall-daemon_tools_installed_proof_runner_unit_test`
   - `cmake --build obj-x86_64-linux-gnu --target dasall_tools_installed_proof_tool -j4`
   - `dpkg-buildpackage -us -uc -b`
   - `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=build/tool-fix-009-package-smoke bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`
   - `sed -n '1,160p' build/tool-fix-009-package-smoke/tools-installed-proof.json`

## 6. Rollout Checklist

1. runtime live dependency set 已同时暴露 `agent.dataset` / `agent.terminal`，而不是继续停留在 dataset-only。
2. daemon / gateway focused integration 已覆盖 terminal unconfirmed deny 与 confirmed allow。
3. `/usr/lib/dasall/dasall-tools-installed-proof --json` 已在本机安装态返回 terminal visible / deny / allow / builtin route 字段。
4. `build/tool-fix-009-package-smoke/tools-installed-proof.json` 已保留 terminal proof 字段，release-runner package-smoke artifact contract 无需新增第二套 owner。
5. 本轮未使用 qemu / kvm，也未把 local installed authoritative evidence 外推为 machine-isolated PASS。

## 7. 风险与回退

1. 若只在 builtin catalog 层宣称 terminal 已存在、但 runtime live composition 不同步更新，installed/app evidence 会继续停留在 dataset-only，`TOOL-GAP-009` 会被假性关闭。
2. 若把 terminal visible surface 打通却不验证 unconfirmed deny / confirmed allow 双路径，后续可能把高风险 action 当成普通 query 误放行。
3. 若把本轮结论外推为 qemu / autopkgtest / soak 已完成，会错误吞并 `TOOL-GAP-012` 的 machine-isolated hardening 范围。

## 8. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 runtime composition、app composition tests、installed helper、package smoke 与文档回写。
3. Build 三件套已在本机完成，且 installed authoritative evidence 已落盘。
4. 范围保持在 runtime production visible tools surface，不扩张到 skill runtime 主链、payload golden regression 或 qemu/soak。

结论：D Gate = PASS；`TOOL-GAP-009` 已按 runtime production 可见工具面、confirmation gate 与本机 installed authoritative evidence 收口。