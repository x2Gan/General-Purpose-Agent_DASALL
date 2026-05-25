# TUI-TODO-044 review closeout evidence

状态：Done
日期：2026-05-25
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务不新增代码路径，只把 `TUI-TODO-037~043` 已经完成的协议冻结、focused integration、true daemon-backed E2E、installed smoke 与 formal binary purity 证据收口成一份 review closeout 口径。
2. 本任务必须明确区分 `focused/scripted IPC`、`true daemon-backed E2E`、`installed local smoke`、`release-ready` 四层概念，避免把低层或本机证据继续误写成更高层结论。
3. 本任务完成后，TUI review follow-up 原子任务可全部关闭；但 `qemu/autopkgtest 实际运行`、`release runner`、`soak/chaos` 仍属于更高层环境证据，不由本轮 closeout 直接宣称完成。

## 2. 037~043 证据矩阵

| 原子任务 | 证据层级 | 已闭合内容 | 代表性命令 / 证据 | 明确禁止外推 |
|---|---|---|---|---|
| `TUI-TODO-037` | L1 design / evidence consistency | 冻结 `tui_ipc.v1` owner、五个 operation dispatch、错误 taxonomy、temp socket E2E 策略，以及 `037~044` Design->Build 映射 | `rg -n "true daemon-backed" docs/todos/tui/deliverables/TUI-TODO-037-true-integration-protocol-convergence.md && rg -n "tui_ipc.v1" docs/todos/tui/deliverables/TUI-TODO-037-true-integration-protocol-convergence.md` | 不得把历史 scripted IPC / focused integration 写成 true daemon-backed |
| `TUI-TODO-038` | L2 focused server evidence | daemon/access `tui_ipc.v1` server handler、五个 operation dispatch、server-side error taxonomy 已实现并回归 | `Build_CMakeTools(buildTargets=["dasall_tui_ipc_protocol_adapter_unit_test","dasall-daemon"])` + `./build/vscode-linux-ninja/tests/unit/access/dasall_tui_ipc_protocol_adapter_unit_test` | 不得外推为 formal client 已跑通真实 roundtrip |
| `TUI-TODO-039` | L2 focused / test seam | formal `dasall` 增加 `DASALL_TUI_DAEMON_SOCKET` seam，真实测试可指向临时 socket，生产默认仍指向 `/run/dasall/daemon.sock` | `Build_CMakeTools(buildTargets=["dasall-tui","dasall_tui_socket_override_integration_test","dasall_tui_integration_topology_smoke_integration_test"])` + 直接执行两个 integration binary | 不得外推为 daemon-backed submit/receipt 真链路已闭合 |
| `TUI-TODO-040` | L2 focused app integration | formal composer submit 已真实接到 `ITuiDataSource::submit_turn()`，receipt / banner / status 能回投 screen model | `Build_CMakeTools(buildTargets=["dasall_tui_submit_turn_integration_test","dasall_tui_integration_topology_smoke_integration_test"])` + 直接执行两个 integration binary | 不得外推为真实 daemon/access socket 已参与 |
| `TUI-TODO-041` | L2 true daemon-backed E2E | formal TUI 已在 build tree 上通过真实 daemon/access 临时 socket 跑通 `open -> route -> submit -> poll -> close`，并断言 accepted_async receipt、route/status/event projection | `Build_CMakeTools(buildTargets=["dasall_tui_daemon_backed_e2e_integration_test","dasall_tui_integration_topology_smoke_integration_test"])` + `./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_daemon_backed_e2e_integration_test` | 不得外推为 installed package / release-ready |
| `TUI-TODO-042` | L4 installed-package local evidence | installed bare `dasall` + installed daemon 已在本机 authoritative smoke 下产出 `tui-daemon-backed-proof.json` 与 `tui-noninteractive.txt`，证明 accepted_async receipt 与非 TTY fail-closed / `dasall-cli` redirect 语义 | `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-tui-042-smoke bash scripts/packaging/pkg_smoke_install.sh --tui-daemon-backed-check` | 不得外推为 qemu/autopkgtest 已通过，亦不得外推为 release-ready |
| `TUI-TODO-043` | L3/L4 release-binary purity gate | formal `dasall` 已改连 fake-free `dasall_tui_core`；prototype fake path 保留但不再污染 formal binary | 初始 `nm -C build/vscode-linux-ninja/apps/tui/dasall | rg 'FakeTuiDataSource|FakeScenario'` 命中问题；随后 `Build_CMakeTools(buildTargets=["dasall-tui","dasall_tui_entrypoint_purity_integration_test"])` 与 `./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_entrypoint_purity_integration_test` 通过 | 不得外推为 installed package / qemu / release runner 全链路 ready |

## 3. closeout 口径

1. `TUI-TODO-037~044` review follow-up 已全部闭合；TUI owner 现在可以稳定陈述四个结论：
   - `037~040` 建立并冻结了 protocol / seam / submit focused evidence
   - `041` 提供了 build-tree true daemon-backed E2E
   - `042` 提供了 local installed-package authoritative smoke
   - `043` 提供了 formal binary purity gate
2. `TUI-TODO-001~036` 的历史 closeout 仍然有效，但它们只覆盖 prototype、focused component、command release gate、packaging review 等先前范围；044 明确禁止再把这些历史项误写成 true daemon-backed 或 installed ready。
3. 当前 TUI owner-authoritative 的最高可信结论是：formal TUI 的协议冻结、真实 build-tree daemon-backed E2E、本机 installed smoke 与 formal binary purity 都已具备；这是一组 `L1/L2/L4 local` closeout 证据，而不是 `qemu/release-runner/soak` 级 release-ready 宣言。

## 4. blocking 状态与后续外部证据

1. 本专项 review follow-up 不再有 owner-local blocker；`BLK-TUI-009`、`BLK-TUI-010` 与 `TUI-TODO-037~044` 均已关闭或完成。
2. 仍未被本轮证据覆盖、因此不得被偷换成“已发布 ready”的外部项包括：
   - `debian/tests/tui-daemon-backed` 的真实 qemu/autopkgtest 运行结果
   - release runner / packaging pipeline 的更高层 installed 复验
   - soak / chaos / production 长稳态结论
3. 因而 044 的最终 closeout 口径是：TUI review follow-up 已完成，但 release-ready 若要成立，仍需更高层环境继续提供 `L5/L6` 证据；这不再作为 TUI owner 本轮 TODO 阻塞项，而是交给 packaging / release 环境处理。

## 5. 验证

1. `rg -n "TUI-TODO-037" docs/todos/tui/deliverables/TUI-TODO-044-review-closeout-evidence.md`
   - 预期：命中本 deliverable 的 037~043 证据矩阵与 closeout 口径。
2. `rg -n "true daemon-backed" docs/worklog/DASALL_开发执行记录.md`
   - 预期：命中 037~044 相关 worklog 条目，证明 worklog 已明确采用分层口径而不是混写成 single ready claim。

结论：`TUI-TODO-044` = Done。`TUI-TODO-037~044` review closeout 已全部收口；TUI owner 仍保持对 `release-ready` 的保守口径，不把 local installed smoke / purity 证据外推为更高层环境结论。