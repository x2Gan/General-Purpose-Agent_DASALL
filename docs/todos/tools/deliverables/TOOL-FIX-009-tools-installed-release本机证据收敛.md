# TOOL-FIX-009 tools installed / release 本机证据收敛

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `TOOL-FIX-009`。
2. 本轮目标：为 tools 建立 deterministic installed-package 正向 proof owner，并把该 proof 收敛到 package smoke 与 release-runner local artifact 合同，而不是继续把 tools-positive 证据混进 `dasall run` 的 LLM 主链路。
3. 完成判定：`/usr/lib/dasall/dasall-tools-installed-proof --json` 已能在本机安装态返回 `ok=true`、`route_kind=builtin`、`agent_dataset_visible=true` 与 production bridge / observability evidence；`pkg_smoke_install.sh --explicit-start-check` 已落盘 `tools-installed-proof.json`；release-runner package-smoke artifact 目录保留同名证据；本轮不要求 qemu / kvm，也不把 local authoritative evidence 外推为 machine-isolated release-ready。

## 2. 本地证据

1. `scripts/packaging/README.md` 已冻结 installed-package 功能矩阵：tools 行要求 `/usr/lib/dasall/dasall-tools-installed-proof --json` 返回 `"ok": true`、`"route_kind": "builtin"`、`"agent_dataset_visible": true`，并要求 package-smoke artifact 目录落盘 `tools-installed-proof.json`。
2. `.github/workflows/release-package-gate.yml` 既有 `Run local installed package evidence` step 已固定 `dpkg-buildpackage -us -uc -b` 与 `bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` 为 release-runner local evidence owner；本轮只需把 tools proof 接到该 owner，而不是再发明第二套 runner。
3. 本轮前 build-tree live composition 已能证明 `agent.dataset` 经 `IToolManager -> builtin -> services` 正向链路成立，但 installed 态没有 deterministic tools-positive entrypoint；`dasall run` 同时承载 LLM 主链路，不适合作为 tools authoritative proof。
4. 本轮已新增 `apps/daemon/src/ToolsInstalledProofRunner.h/.cpp` 与 `apps/daemon/src/ToolsInstalledProofMain.cpp`：helper 通过 `DaemonEntryConfigLoader` 与 `compose_minimal_live_dependency_set()` 建立 daemon local-control-plane live dependency set，再调用 `IToolManager::invoke()` 执行 `agent.dataset`，并对 payload、observation digest、builtin route 与 external evidence markers 做二值断言。
5. 本轮已更新 `apps/daemon/CMakeLists.txt`、`tests/unit/apps/daemon/CMakeLists.txt` 与 `debian/dasall-daemon.install`：helper 现以 `dasall-tools-installed-proof` 目标构建，并安装到 `/usr/lib/dasall/dasall-tools-installed-proof`。
6. 本轮已更新 `scripts/packaging/pkg_smoke_install.sh`：package smoke 现调用 installed helper，断言 `ok`、`agent_dataset_visible`、`tool_invocation_succeeded`、`projection_present`、`route_citation_present`、`production_bridge_evidence_present`、`production_observability_evidence_present`，并落盘 `tools-installed-proof.json`。
7. 本轮本机安装态 smoke 实测输出已确认：`/usr/lib/dasall/dasall-tools-installed-proof` 存在；`build/tool-fix-009-package-smoke/tools-installed-proof.json` 包含 `effective_profile_id=desktop_full`、`route_kind=builtin`、`visible_tools=["agent.dataset"]`、`tool_invocation_succeeded=true`、`production_bridge_evidence_present=true` 与 `production_observability_evidence_present=true`。

## 3. 设计结论

### 3.1 根因收口

1. `TOOL-FIX-009` 的根因不是 runtime/tool path 缺实现，而是 installed-package 缺少独立 proof owner。build-tree tests 已证明 dataset builtin live path 成立，但没有安装态 helper 时，package smoke 无法给出稳定的 tools-positive authoritative evidence。
2. 正确收口方式不是扩大 runtime production 默认可见工具面，也不是重用 `dasall run`。`dasall run` 的 installed 正向路径用于 LLM 主链路；tools proof 应由独立 helper 通过 governed `IToolManager` 调用 `agent.dataset`，避免把 builtin dataset 投影误判为 LLM 主功能闭环。
3. release 侧本轮要闭合的是 local authoritative evidence contract：package build 产出 helper，package smoke 落盘 `tools-installed-proof.json`，release runner 复用既有 package-smoke artifact 目录归档该 JSON。qemu/autopkgtest 继续属于 machine-isolated hardening，不作为本轮前置。

### 3.2 边界与不外推项

1. 本轮不把 `agent.terminal` 加入 runtime production 默认可见工具面；`TOOL-GAP-009` 继续保持独立缺口。
2. 本轮不把 dataset helper 结果误写为 `run` / LLM 主链路通过；LLM 正向验收仍由 `dasall run ... --json` 负责。
3. 本轮不宣称 qemu/autopkgtest、release hardening 或 long-session soak 已完成；`TOOL-GAP-012` 仅从“完全缺失 installed evidence”收缩为“machine-isolated qemu / soak 仍缺”。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | installed tools-positive 证据需要独立 helper owner，而不是复用 `dasall run` | `apps/daemon/src/ToolsInstalledProofRunner.h/.cpp`、`apps/daemon/src/ToolsInstalledProofMain.cpp` |
| D2 | helper 必须进入 Debian 安装布局，确保 package smoke 与 installed host 可直接调用 | `apps/daemon/CMakeLists.txt`、`tests/unit/apps/daemon/CMakeLists.txt`、`debian/dasall-daemon.install` |
| D3 | package-smoke 必须把 helper 输出收口成 release-runner 可归档 artifact | `scripts/packaging/pkg_smoke_install.sh` |
| D4 | local authoritative evidence contract 继续由 packaging owner 文档与 release workflow 持有 | `scripts/packaging/README.md`、`.github/workflows/release-package-gate.yml` |
| D5 | 将完成结论回写到 tools 总账、交付索引与工作日志 | 本文档、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/todos/tools/deliverables/DELIVERABLES-INDEX.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 5. Build 三件套

1. 代码目标：新增 installed proof helper，并把 helper、unit test、Debian install layout 与 package smoke artifact 接线为同一条 local installed evidence 链。
2. 测试目标：focused build + direct-binary unit fallback 验证 helper，自机 installed-package smoke 生成 `tools-installed-proof.json` 并断言 builtin route / observation digest / production bridge / observability evidence。
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall-daemon_tools_installed_proof_runner_unit_test","dasall_tools_installed_proof_tool"])`
   - `build/vscode-linux-ninja/tests/unit/apps/daemon/dasall-daemon_tools_installed_proof_runner_unit_test`
   - `dpkg-buildpackage -us -uc -b`
   - `bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`
   - `sed -n '1,160p' build/tool-fix-009-package-smoke/tools-installed-proof.json`

## 6. Rollout Checklist

1. `/usr/lib/dasall/dasall-tools-installed-proof` 已随 `dasall-daemon` 安装布局落地，而不是只存在于 build tree。
2. `pkg_smoke_install.sh --explicit-start-check` 已落盘 `tools-installed-proof.json`，且 package smoke 完整退出成功。
3. `tools-installed-proof.json` 已包含 `ok=true`、`route_kind=builtin`、`agent_dataset_visible=true`、`tool_invocation_succeeded=true`、`production_bridge_evidence_present=true`、`production_observability_evidence_present=true`。
4. release-runner 继续复用 package-smoke artifact 目录，不另起新的 tools artifact owner。
5. 本轮不依赖 qemu / kvm；local authoritative evidence 与 machine-isolated gate 口径继续分离。

## 7. 风险与回退

1. 若继续用 `dasall run` 充当 tools-positive installed proof，会把 LLM 主链路与 builtin dataset 投影混成一个结论，后续 release note 会误报。
2. 若为了 installed proof 直接把 `agent.terminal` 暴露到 runtime production visible tools，会提前吞并 `TOOL-GAP-009`，并把高风险 action gate 改动混入本任务。
3. 若把本轮 local authoritative evidence 直接写成 qemu PASS / release-ready，会错误绕过 `TOOL-GAP-012` 的 machine-isolated hardening 要求。
4. package smoke 在 artifact 阶段仍依赖嵌入式 Python 小脚本；本轮已用 `sed 's/^  //' <<'PY' | python3 - ...` 收口 heredoc 缩进，后续同类脚本若继续保留 shell 级缩进，仍会出现运行期假失败。

## 8. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 helper、install layout、package smoke、release-runner local artifact 与文档回写。
3. Build 三件套已锁定为 focused build/unit/local installed smoke，不依赖 qemu / kvm。
4. 范围保持在 tools installed / release local evidence，不扩张到 runtime visible tool surface 或 machine-isolated qemu/soak gate。

结论：D Gate = PASS；`TOOL-FIX-009` 已可按本机 authoritative local evidence 与 release-runner package-smoke artifact 合同收口。