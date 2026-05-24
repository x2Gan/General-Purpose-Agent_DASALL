# TUI-TODO-030 bare `dasall` 迁移门禁证据

状态：Done（Gate-TUI-08 仍保持 Blocked）
日期：2026-05-24
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只收敛 bare `dasall` 命令释放前的门禁证据、旧入口 inventory 与迁移矩阵，不修改 `apps/cli`、`apps/tui`、`debian/` 或 `scripts/packaging/` 的生产实现。
2. 本任务不提前执行 `TUI-TODO-031~034`，也不把 `BLK-TUI-006` 的 CJK/IME/resize 人工 gate 或 FTXUI Debian packaging review 伪装成已通过。
3. 本任务完成标准是：把 B.0 前置门禁逐项判定为 `Pass` 或 `Blocked`，列出当前仍占用 bare `dasall` 的仓内文件与命令面，并为 `TUI-TODO-031~034` 给出逐文件迁移动作。

## 2. 本地事实与设计依据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 附件 B.0 已冻结：释放 `/usr/bin/dasall` 前必须同时满足样品评审、daemon-backed projection 闭环、root/sudo-only operator 模型、`dasall-cli` 兼容矩阵与 FTXUI/IME/CJK 质量门五项前置条件。
2. 同一文档附件 B.4.4 已把命令迁移兼容矩阵固定为九个影响面：CMake target、install 规则、manpage、README.Debian/postinst、autopkgtest、packaging scripts、docs/examples、shell completion 与 release notes。
3. `docs/todos/tui/deliverables/TUI-TODO-001-启动身份与权限模型决策.md` 与 `docs/todos/tui/deliverables/TUI-TODO-024-启动降级与daemon-unavailable路径.md` 已冻结并验证：daemon-backed TUI v1 继续沿用 `/run/dasall/daemon.sock` + `0600 root/sudo-only` operator backend，ordinary-user full-function 仍是 future-only，`permission_denied` 必须保持独立 startup issue。
4. `docs/todos/tui/deliverables/TUI-TODO-023-daemon-data-source-contract基线.md`、`docs/todos/tui/deliverables/TUI-TODO-025-status-tool-recovery投影刷新.md`、`docs/todos/tui/deliverables/TUI-TODO-027-route-catalog-projection.md` 与 `docs/todos/tui/deliverables/TUI-TODO-029-next-preference提交回显.md` 已分别闭合 daemon-backed `open_session/submit_turn/poll_events/route_catalog/close_session`、status projection refresh、route catalog projection 字段与 next preference submit echo 的 focused evidence。
5. `docs/todos/tui/deliverables/TUI-TODO-020-fake-only-TuiApp小样基线.md`、`docs/todos/tui/deliverables/TUI-TODO-005-ftxui接入评审.md` 与 `docs/worklog/DASALL_开发执行记录.md` 仍明确：`BLK-TUI-006` 的 CJK/IME/resize manual gate 未闭合，FTXUI 的 Debian source/binary strategy 也尚未在命令迁移轮次通过 packaging review。
6. 当前仓内实际占用关系仍与附件 B.1 一致：`apps/cli/CMakeLists.txt` 把 `dasall-cli` target 产物名设为 `dasall`，`debian/dasall-cli.install` 仍把 `debian/tmp/usr/bin/dasall` 安装到 `usr/bin/`，而 `debian/dasall-daemon.postinst`、`debian/dasall-daemon.README.Debian`、`debian/dasall.1`、`debian/tests/pkg-smoke-local-control-plane` 与 `scripts/packaging/*.sh` 仍以 bare `dasall` 作为结构化控制面入口。
7. 当前仓内不存在 `docs/todos/tui/deliverables/TUI-TODO-035-交付证据回写.md`；因此 B.0 第 1 项要求的“样品评审采纳/延后/废弃清单已回写”仍缺单独收口件。

## 3. 外部参考

1. Debian Policy 6.2 要求 maintainer scripts 保持幂等，6.3 要求 maintainer scripts 在没有 controlling terminal 时也能以 noninteractive 方式运行。这意味着 `postinst`、README.Debian 与 operator next steps 不能在命令迁移期依赖交互式兜底或半迁移状态；命令面必须与文档、脚本和安装后引导同步切换。
   - 参考：https://www.debian.org/doc/debian-policy/ch-maintainerscripts.html
2. `autopkgtest(1)` 明确其职责是“在 testbed 上测试已安装的二进制 Debian 包”，并以 source package 提供的 tests 为依据。这说明 `debian/tests/*` 与 package smoke 里的命令名不是可随意延后的文案问题，而是 installed-package 行为契约的一部分；若 bare `dasall` 与 `dasall-cli` 的角色要拆分，autopkgtest 与 package smoke 必须同轮改写。
   - 参考：https://manpages.debian.org/testing/autopkgtest/autopkgtest.1.en.html

## 4. B.0 前置门禁判定

| B.0 项 | 当前证据 | 结论 | 对 031~034 的影响 |
|---|---|---|---|
| 1. `dasall_tui_prototype` 样品评审已通过，采纳/延后/废弃清单已回写 | `TUI-TODO-020` deliverable 与 worklog 仍明确 `BLK-TUI-006` Open；仓内不存在 `TUI-TODO-035` 交付物 | Blocked | `TUI-TODO-031~034` 不能据此宣称命令释放已具 end-user ready 证据 |
| 2. `DaemonTuiDataSource`、session open/close/query、status projection、route projection 已具备最小可用闭环 | `TUI-TODO-023/024/025/026/027/028/029` deliverables 已闭合 open/submit/poll/route/close、startup failure、status refresh、session lifecycle、selector 消费与 submit echo | Pass | 030 可把 daemon-backed TUI ready 作为既成事实引用，不必再阻塞在 projection seam |
| 3. root/sudo-only operator 模型与普通用户 TUI 启动语义已冻结 | `TUI-TODO-001` 与 `TUI-TODO-024` 已冻结并验证 `root/sudo-only`、`permission_denied` 与 user-level daemon future-only | Pass | 命令迁移可明确把 operator 命令迁往 `dasall-cli`，但不能把 bare `dasall` 表述成普通用户默认 full-function 主路径 |
| 4. `dasall-cli` 结构化命令兼容矩阵已完成，仓内脚本和 Debian smoke 的迁移路径清晰 | 本文件第 5 节与第 6 节已把当前 inventory、目标命令归属与逐文件迁移动作收口为单一矩阵 | Pass（文档证据） | `TUI-TODO-031~033` 现在具备逐文件执行输入，但不等于改动已落地 |
| 5. FTXUI 依赖来源、Debian 打包策略、snapshot/IME/CJK 质量门已通过 | `TUI-TODO-005` 只冻结 resolver/pin 与 private dependency 规则，并明确 030~033 前还需复核 Debian source/binary strategy；`BLK-TUI-006` 仍未提供 CJK/IME/resize manual evidence | Blocked | Gate-TUI-08 不能通过；031~034 继续保持 Blocked |

结论：B.0 第 2、3、4 项现已具备可追溯证据；第 1、5 项仍未满足，因此 Gate-TUI-08 当前仍保持 Blocked，`TUI-TODO-031~034` 不得进入实现。

## 5. bare `dasall` 旧入口 inventory

### 5.1 Build 与 install 占用面

| 文件 | 当前事实 | 迁移动作 | 后续任务 |
|---|---|---|---|
| `apps/cli/CMakeLists.txt` | `set_target_properties(dasall-cli PROPERTIES OUTPUT_NAME dasall)`，结构化 CLI 仍直接产出 bare `dasall` | 先把 CLI 公开产物名改回 `dasall-cli` | `TUI-TODO-031` |
| `debian/dasall-cli.install` | 只安装 `debian/tmp/usr/bin/dasall usr/bin/` | CLI 改装到 `usr/bin/dasall-cli`，并让正式 TUI target 安装 `usr/bin/dasall` | `TUI-TODO-033` |

### 5.2 Debian 文档与安装后引导

| 文件 | 当前 bare `dasall` 用法 | 迁移动作 | 后续任务 |
|---|---|---|---|
| `debian/dasall-daemon.postinst` | first-install hint 使用 `sudo dasall config` | 改为 `sudo dasall-cli config`，并单独说明 bare `dasall` 进入 TUI | `TUI-TODO-033` |
| `debian/dasall-daemon.README.Debian` | README 明确 installed command name 是 `dasall`，并用 `sudo dasall config`、`sudo dasall ping --json` 作为 operator 路径 | 拆分为 TUI 入口说明 + `dasall-cli` operator 路径说明 | `TUI-TODO-033` |
| `debian/dasall.1` | manpage 把 `dasall` 记为 installed CLI，并列出 `ping/readiness/run/status/cancel` 等结构化子命令 | 拆成 `dasall.1`（TUI）与 `dasall-cli.1`（结构化 CLI），或等价更新双文档 | `TUI-TODO-033` |

### 5.3 autopkgtest 与 package smoke

| 文件 | 当前 bare `dasall` 用法 | 迁移动作 | 后续任务 |
|---|---|---|---|
| `debian/tests/control` | 当前只注册 `pkg-smoke-local-control-plane` | 保留 control-plane smoke，但切到 `dasall-cli`，并新增 bare `dasall` TUI smoke | `TUI-TODO-033~034` |
| `debian/tests/pkg-smoke-local-control-plane` | `config show/validate/plan/apply`、`ping`、`readiness`、`run`、`status`、`cancel`、`diag health` 全部直接调用 bare `dasall` | 结构化控制面命令统一切到 `dasall-cli`；bare `dasall` 只保留独立 TUI 启动/错误路径 smoke | `TUI-TODO-033~034` |

### 5.4 `scripts/packaging/` installed-package 证据入口

| 文件 | 当前 bare `dasall` 用法 | 迁移动作 | 后续任务 |
|---|---|---|---|
| `scripts/packaging/pkg_smoke_install.sh` | `ping/readiness/run/status/cancel/diag/knowledge *` 全部围绕 bare `dasall` 执行 | 把所有结构化控制面命令切到 `dasall-cli`；若要验证 bare `dasall`，单独增加 TUI smoke/exit path | `TUI-TODO-033` |
| `scripts/packaging/knowledge_local_installed_proof.sh` | `readiness`、`knowledge refresh/retrieve/health` 都经 bare `dasall` | 切换到 `dasall-cli knowledge ...` 与 `dasall-cli readiness` | `TUI-TODO-033` |
| `scripts/packaging/knowledge_failure_injection_installed_proof.sh` | `knowledge refresh/retrieve/health` 与 `readiness` 走 bare `dasall` | 切换到 `dasall-cli` | `TUI-TODO-033` |
| `scripts/packaging/knowledge_refresh_retrieve_soak.sh` | `readiness`、`knowledge refresh/retrieve/health` 走 bare `dasall` | 切换到 `dasall-cli` | `TUI-TODO-033` |
| `scripts/packaging/infra_release_soak_gate.sh` | `config apply`、`readiness`、`diag health` 走 bare `dasall` | 切换到 `dasall-cli` | `TUI-TODO-033` |
| `scripts/packaging/README.md` | installed-package 功能矩阵仍以 `dasall run`、`dasall knowledge`、`diag health` 等描述控制面 | 改为“人机入口示例=`dasall`，控制面示例=`dasall-cli`”的双命令矩阵 | `TUI-TODO-033` |

## 6. 命令迁移兼容矩阵落盘口径

| 影响面 | 当前 owner / 命令面 | 目标 owner / 命令面 | 迁移口径 |
|---|---|---|---|
| CMake target | `dasall-cli` target 产物名=`dasall` | CLI target 产物名=`dasall-cli`；正式 TUI target 产物名=`dasall` | 先执行 031，再执行 032 |
| install 规则 | CLI 安装到 `/usr/bin/dasall` | CLI 安装到 `/usr/bin/dasall-cli`，TUI 安装到 `/usr/bin/dasall` | 033 同轮落 Debian install |
| manpage | `debian/dasall.1` 描述旧 CLI | `dasall.1` 描述 TUI，`dasall-cli.1` 描述结构化 CLI | 033 同步文档与安装规则 |
| README.Debian / postinst | operator next steps 使用 `sudo dasall config` | operator next steps 改为 `sudo dasall-cli config`，bare `dasall` 单列为 TUI 入口 | 033 同步迁移 |
| autopkgtest | installed control-plane smoke 直接执行 bare `dasall <subcommand>` | 结构化 smoke 全部切到 `dasall-cli`，另加 bare `dasall` TUI smoke | 033 更新脚本，034 增加命令分流测试 |
| packaging scripts | package smoke 与 installed proof helper 把 bare `dasall` 视为结构化 CLI | 结构化 helper 全切 `dasall-cli`，bare `dasall` 只保留人机入口 smoke | 033 |
| docs/examples | 控制面与人机入口混用 bare `dasall` | 文档示例强制区分：人机=`dasall`，控制面=`dasall-cli` | 033/035 |
| shell completion | 当前未见已冻结双命令 completion | 若后续提供 completion，`dasall-cli` 承接结构化 completion，`dasall` 仅保留 TUI/options | 033 后复核 |
| release notes | 当前无 bare `dasall` breaking change 迁移说明 | 明确 `dasall`/`dasall-cli` 角色切换与旧 `dasall <subcommand>` fail-closed | 033/035 |

## 7. Design -> Build 映射

| 后续任务 | 锁定代码/文档目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| `BLK-TUI-006` / `TUI-TODO-035` | 回写样品评审采纳/延后/废弃清单与 CJK/IME/resize manual evidence | evidence consistency | `rg -n "采纳|延后|废弃|CJK|IME|resize" docs/todos/tui/deliverables docs/worklog/DASALL_开发执行记录.md` |
| `TUI-TODO-031` | `apps/cli/CMakeLists.txt` 释放 CLI 产物名 | `CliControlPlaneCommandNameTest` | `cmake --build --preset vscode-linux-ninja --target dasall-cli` |
| `TUI-TODO-032` | `apps/tui/CMakeLists.txt`、`apps/tui/src/main.cpp` 正式 TUI `dasall` target 与 install rule | `DasallTuiEntrypointSmokeTest` | `cmake --build --preset vscode-linux-ninja --target dasall-tui` |
| `TUI-TODO-033` | `debian/`、`scripts/packaging/`、README/manpage/postinst/autopkgtest 切换到双命令语义 | package smoke / docs consistency | `rg -n "dasall (config|ping|readiness|run|status|cancel|diag|knowledge|version)" debian scripts` |
| `TUI-TODO-034` | `tests/integration/apps/tui/DasallCommandRoutingTest.cpp` 与相关 CMake 接线 | `DasallCommandRoutingTest`、`CliControlPlaneCommandNameTest` | `ctest --preset vscode-linux-ninja -R "DasallCommandRouting|CliControlPlane" --output-on-failure` |

## 8. 结果

1. `TUI-TODO-030` 已完成其自身职责：B.0 门禁证据、旧入口 inventory 与命令迁移兼容矩阵现在已集中落盘，不再散落在 TUI 详设、专项 TODO、Debian 文件和 packaging scripts 之间。
2. Gate-TUI-08 当前仍保持 Blocked，阻塞项明确收敛为两条：
   - `BLK-TUI-006` 尚未提供样品评审采纳/延后/废弃清单与 CJK/IME/resize manual evidence。
   - FTXUI 的 Debian source/binary strategy 与 installed-path packaging review 尚未通过；`TUI-TODO-005` 已明确 030~033 前必须单独复核。
3. 因此本轮不会推进 `TUI-TODO-031~034`。下一步最小解阻动作应优先转入 `TUI-TODO-035`，把样品评审与阶段证据收口到专门 deliverable；只有在 `BLK-TUI-006` 与 FTXUI packaging review 闭合后，Gate-TUI-08 才允许转为 Pass。