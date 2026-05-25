# TUI-TODO-042 installed package TUI daemon-backed smoke

状态：Done
日期：2026-05-25
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只闭合 installed/package 层证据：必须使用 Debian 包重建后的已安装 `dasall` / `dasall-daemon`，并在 `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR` 下落盘可审计 artifact。
2. 本任务不把 installed smoke 外推成完整 release-ready，也不覆盖 formal/prototype purity 与最终 closeout；这些继续留给 `TUI-TODO-043~044`。
3. 本任务必须同时证明两条语义不回退：bare `dasall` 仍为交互式 TTY-only 入口，`dasall-cli` 仍保留非交互 structured control-plane 入口。

## 2. 局部判别与实现决策

1. 首次 authoritative smoke 证明 041 的 build-tree true E2E 不能直接外推到 installed daemon：安装态 scripted smoke 返回 `status_stage:"ready"`，同时出现 `submit_turn rejected by access gateway`，说明默认 installed daemon 约束与 041 harness 不一致。
2. 邻近代码排查表明：041 harness 显式放行 `tui_ipc.v1` 且注入 `AsyncTaskRegistry`；而 installed daemon 默认 pipeline allowlist 仅含 `ipc_uds`，accepted_async ownership receipt 也只在 proof 模式下启用。
3. 因此本任务的根修复不是放宽 smoke 预期，而是：
   - 在 `apps/daemon/src/main.cpp` 中把 installed daemon 默认 `allowed_protocols` 扩到包含 `tui_ipc.v1`
   - 在 `scripts/packaging/pkg_smoke_install.sh` 与 `debian/tests/tui-daemon-backed` 中复用已存在的 installed async-receipt proof daemon / 临时 socket 路径，而不是错误假设默认 system daemon 必然产出 accepted_async receipt
   - 把 non-TTY 断言从固定 `stdin` 收敛为当前正式 fail-closed 的 stdio blocker 文案，避免把实现细节绑死到单个文件描述符

## 3. 落盘结果

1. `apps/daemon/src/main.cpp` 已补齐 installed daemon 默认 `tui_ipc.v1` allowlist，formal bare `dasall` 走 installed daemon 时不再被 protocol allowlist 直接拦截。
2. `scripts/packaging/pkg_smoke_install.sh` 已把 `--tui-daemon-backed-check` 收敛到 proof daemon / 临时 socket 的 installed authoritative path，并在 `/tmp/dasall-tui-042-smoke/` 下落盘：
   - `tui-daemon-backed-proof.json`
   - `tui-noninteractive.txt`
3. `debian/tests/tui-daemon-backed` 已与 package smoke 对齐：同样走 proof daemon / 临时 socket 路径，并接受当前正式 stdio blocker 文案。
4. 本 deliverable、TUI 专项 TODO、专项总账与 worklog 已同步回写，`TUI-TODO-042` 从 package smoke 设计缺口转为已闭合证据项。

## 4. 验证

1. `Build_CMakeTools(buildTargets=["dasall-daemon","dasall-tui"])`
   - 结果：通过。
2. `sh -n scripts/packaging/pkg_smoke_install.sh && sh -n debian/tests/tui-daemon-backed && echo PASS`
   - 结果：通过；输出 `PASS`。
3. authoritative Debian rebuild
   - 证据：`/tmp/tui-042-rerun-rebuild-deb.log` 已完成 `dh_builddeb`，`/tmp/dasall-tui-042-packbuild-rerun/` 与 `/home/gangan/` 均产出 `dasall_0.1.0-1_all.deb`、`dasall-cli_0.1.0-1_amd64.deb`、`dasall-daemon_0.1.0-1_amd64.deb`、`dasall-common_0.1.0-1_all.deb`。
4. `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-tui-042-smoke bash scripts/packaging/pkg_smoke_install.sh --tui-daemon-backed-check`
   - 结果：通过；输出 `install smoke passed`。
   - artifact：`/tmp/dasall-tui-042-smoke/tui-daemon-backed-proof.json` 与 `/tmp/dasall-tui-042-smoke/tui-noninteractive.txt` 已落盘。

## 5. 结果与剩余边界

1. installed bare `dasall` 现已具备一条可审计的 daemon-backed smoke：`tui-daemon-backed-proof.json` 记录了 `status_stage:"accepted_async"`、`status_current_tool:"access.submit"`、`rendered_screen_contains_receipt:true`、`rendered_screen_contains_route:true` 与 `latest_banner_title:"Turn submitted"`。
2. `tui-noninteractive.txt` 同时证明 bare `dasall` 在非 TTY 下继续 fail-closed，并把非交互 usage redirect 到 `dasall-cli`，没有把 control-plane 语义误并回 formal TUI。
3. `TUI-TODO-042` 已完成，但 TUI 仍不能宣称 installed release-ready：`TUI-TODO-043` 仍需 formal/prototype binary purity gate，`TUI-TODO-044` 仍需 closeout evidence 与最终口径回写。