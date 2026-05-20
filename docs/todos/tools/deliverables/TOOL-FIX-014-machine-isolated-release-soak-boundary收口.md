# TOOL-FIX-014 machine-isolated release / soak boundary 收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `TOOL-GAP-012`。
2. 本轮目标：纠正 `TOOL-GAP-012` 的 owner 边界，明确 Tools owner authoritative evidence 已稳定固定在本机 installed-package / release-runner local installed artifact；qemu/autopkgtest 与 long-session MCP/tool soak 继续保留为 packaging / release 环境复核。
3. 完成判定：`scripts/packaging/README.md`、`.github/workflows/release-package-gate.yml`、`pkg_smoke_install.sh --explicit-start-check` 与本轮总账 / deliverable / worklog 回写共同证明 Tools owner 不再缺 local authoritative evidence；本轮不执行 qemu / kvm，也不把更高层 release hardening gate 误写为 Tools 功能缺口。

## 2. 本地证据

1. `scripts/packaging/README.md` 已冻结 installed-package 功能矩阵：tools 行明确把 authoritative owner 固定为 `/usr/lib/dasall/dasall-tools-installed-proof --json` 与 `pkg_smoke_install.sh --explicit-start-check` 落盘的 `tools-installed-proof.json`，并明确 qemu / machine isolation 继续归 release-runner 环境复核。
2. `.github/workflows/release-package-gate.yml` 已在 qemu gate 前固定 `package-smoke` artifact 目录，并统一上传 release evidence；这说明 release-runner 对 tools 的 owner 输入是 local installed package smoke，而不是 guest-side qemu rerun。
3. `TOOL-FIX-009` 已建立 installed helper 与 package-smoke artifact contract，`TOOL-FIX-011` 已把 terminal visible / deny / allow proof 一并纳入 `tools-installed-proof.json`；因此本轮前 Tools owner 的 local authoritative evidence 已经齐备。
4. 本轮重新执行 `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-tool-gap-012-smoke bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`，确认 `/tmp/dasall-tool-gap-012-smoke/tools-installed-proof.json` 仍包含 `ok=true`、`route_kind=builtin`、`terminal_route_kind=builtin`、`agent_dataset_visible=true`、`agent_terminal_visible=true`、`terminal_confirmation_denied=true` 与 `terminal_invocation_succeeded=true`。
5. 本轮同时执行 `python3 scripts/packaging/validate_autopkgtest_metadata.py`，确认 qemu/autopkgtest discoverability 入口未回退；这用于证明 machine-isolated gate 仍有正式环境入口，但不构成当轮 qemu PASS。

## 3. 设计结论

### 3.1 根因收口

1. `TOOL-GAP-012` 的真实剩余项不是 Tools 产品实现未完成，而是 qemu/autopkgtest 与 long-session soak 被继续挂在 Tools gap 名下，导致环境级 release hardening follow-up 被误记为功能性 blocker。
2. 一旦 Tools 已具备独立 installed helper、package-smoke artifact owner 与 release-runner local artifact contract，就不应再要求 Tools owner 在当前轮次内额外给出 qemu guest-side rerun 才算“功能闭合”。
3. 正确收口方式不是补写第二套 Tools 产品代码，也不是在当前 no-qemu/kvm 约束下伪造 machine-isolated evidence，而是把 `TOOL-GAP-012` 改写为 owner boundary closeout。

### 3.2 本轮决定

1. Tools owner authoritative evidence 明确固定为 `/usr/lib/dasall/dasall-tools-installed-proof --json`、`pkg_smoke_install.sh --explicit-start-check` 与 release-runner package-smoke artifact。
2. qemu/autopkgtest 与 long-session MCP/tool soak 继续存在，但它们只属于 packaging / release 环境复核，不再作为 Tools owner gap blocker。
3. `validate_gate_int_10_installed_package_qemu.sh` 仍是 machine-isolated release 复核入口；其存在用于保持 handoff / discoverability，而不是要求本轮 Tools owner 在禁止 qemu/kvm 的约束下强行执行。

### 3.3 边界与不外推项

1. 本轮不新增任何 Tools 产品代码；若 `tools-installed-proof.json` 回退，应回到 `TOOL-FIX-009` / `TOOL-FIX-011` 的实现面修复，而不是重新打开 boundary closeout 本身。
2. 本轮不宣称当轮 qemu rerun 通过、release-ready 或 long-session soak 已完成。
3. 本轮不改写 `TOOL-FIX-012` 与 `TOOL-FIX-013` 的技术结论；它们的功能性 closeout 继续独立成立。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build / 文档落点 |
|---|---|---|
| D1 | Tools owner authoritative evidence 应固定在 installed helper 与 package-smoke artifact，而不是 qemu guest-side rerun | `scripts/packaging/README.md`、`scripts/packaging/pkg_smoke_install.sh` |
| D2 | release-runner 应复用 package-smoke artifact 目录作为 Tools local installed owner | `.github/workflows/release-package-gate.yml` |
| D3 | qemu/autopkgtest 与 long-session MCP/tool soak 只能保留为 packaging / release 环境复核 | `docs/todos/DASALL_子系统查漏补缺专项记录.md`、本文档 |
| D4 | `TOOL-GAP-012` closeout 必须回写 deliverable 索引与 worklog | `docs/todos/tools/deliverables/DELIVERABLES-INDEX.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 5. Build 三件套

1. 代码目标：无新增产品代码；仅回写总账 / deliverable / worklog / deliverable 索引，明确 Tools owner 与 packaging / release owner 的边界。
2. 测试目标：复验 local installed authoritative proof 未回退，并验证 qemu/autopkgtest metadata discoverability 入口保持可用。
3. 验收命令：
   - `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-tool-gap-012-smoke bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`
   - `sed -n '1,200p' /tmp/dasall-tool-gap-012-smoke/tools-installed-proof.json`
   - `python3 scripts/packaging/validate_autopkgtest_metadata.py`
   - `rg -n "TOOL-GAP-012|TOOL-FIX-014" docs/todos/DASALL_子系统查漏补缺专项记录.md docs/todos/tools/deliverables/TOOL-FIX-014-machine-isolated-release-soak-boundary收口.md docs/todos/tools/deliverables/DELIVERABLES-INDEX.md docs/worklog/DASALL_开发执行记录.md`

## 6. Rollout Checklist

1. `TOOL-GAP-012` 已在总账改为“已闭合 / Medium”。
2. deliverable 索引已登记 `TOOL-FIX-014`。
3. worklog 已记录 boundary closeout 与复验结果。
4. `tools-installed-proof.json` 继续证明 Tools owner local authoritative evidence 未回退。
5. 本轮未使用 qemu / kvm，且未把 local evidence 外推为 machine-isolated PASS。

## 7. 风险与回退

1. 若后续 `pkg_smoke_install.sh --explicit-start-check` 不再生成 `tools-installed-proof.json` 或字段回退，说明是 installed proof contract 失效，应重新打开 `TOOL-FIX-009` / `TOOL-FIX-011` 的实现任务，而不是继续把 `TOOL-GAP-012` 当成环境缺口。
2. 若 release-runner workflow 不再归档 `package-smoke` 目录，Tools local evidence owner 会重新漂移；此时应修复 workflow / packaging 文档，而不是扩张 Tools 产品职责。
3. 若把本轮结论误写为 qemu PASS / release-ready，会错误吞并 packaging / release 环境复核的剩余工作。

## 8. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 packaging README、release workflow、总账、deliverable 索引与 worklog。
3. Build 三件套已锁定为 local installed smoke + metadata validator + 文档一致性回写，不依赖 qemu / kvm。
4. 范围保持在 owner boundary closeout，不扩张为 machine-isolated evidence 的当轮执行。

结论：D Gate = PASS；`TOOL-GAP-012` 已按 Tools owner authoritative evidence / packaging-release boundary 收口。 