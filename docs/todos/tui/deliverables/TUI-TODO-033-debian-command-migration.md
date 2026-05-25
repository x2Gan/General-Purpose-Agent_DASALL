# TUI-TODO-033 Debian 命令迁移文件

状态：Done
日期：2026-05-25
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md；docs/todos/DASALL_子系统查漏补缺专项记录.md

## 1. 任务边界

1. 本任务只更新 Debian 命令迁移文件与 installed-package/package-smoke 脚本：`debian/dasall-cli.install`、`debian/dasall-cli.manpages`、`debian/dasall.1`、`debian/dasall-daemon.README.Debian`、`debian/dasall-daemon.postinst`、`debian/tests/*`、`debian/package-assets/*` 与 `scripts/packaging/*` 中的命令面。
2. 本任务只把结构化控制面命令迁移到 `dasall-cli`，并把 bare `dasall` 固定为正式 TUI 入口；不改 `apps/cli`、`apps/tui` 的 target 拓扑，不回退 `TUI-TODO-031/032` 已完成的输出名与 install rule。
3. 本任务不新增 `DasallCommandRoutingTest` 这类命令分流 integration test；旧 `dasall <subcommand>` 的 fail-closed / routing 证明继续归属 `TUI-TODO-034`。

## 2. 本地证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 附件 B.4.2~B.4.5 已把 033 的影响面锁定为 Debian install、manpage、README.Debian/postinst、autopkgtest 与 packaging scripts，同轮要求把控制面示例切到 `dasall-cli`，bare `dasall` 只描述 TUI。
2. `docs/todos/tui/deliverables/TUI-TODO-030-command-release-gate-evidence.md` 第 5~7 节已经列出 033 的逐文件迁移矩阵：`debian/dasall-cli.install` 需同时安装 `/usr/bin/dasall` 与 `/usr/bin/dasall-cli`，`debian/dasall.1` 要改写为 TUI manpage，`debian/dasall-daemon.README.Debian` / `postinst` / `debian/tests/pkg-smoke-local-control-plane` / `scripts/packaging/*.sh` 中的结构化命令都要切到 `dasall-cli`。
3. `TUI-TODO-031` 已释放 CLI build-tree 产物名，`TUI-TODO-032` 已新增正式 TUI `dasall` target 并把 non-TTY fail-closed 文案固定为 “Use dasall-cli for non-interactive control-plane tasks.”；033 因此前置依赖已满足。
4. 当前仓内命中证明 033 尚未落地：`debian/dasall-cli.install` 仍只安装 `debian/tmp/usr/bin/dasall`；`debian/dasall.1`、`debian/dasall-daemon.README.Debian`、`debian/dasall-daemon.postinst`、`debian/tests/pkg-smoke-local-control-plane` 及 `scripts/packaging/README.md` / `pkg_smoke_install.sh` / `knowledge_*` / `infra_release_soak_gate.sh` 仍把 bare `dasall` 用作 `config/ping/readiness/run/status/cancel/diag/knowledge` 的结构化控制面入口。

## 3. 外部参考

1. Debian Policy 12.1 规定每个程序都应提供对应 manual page，并随同同一包或其依赖一起安装；如果同一功能面拆成两个公开命令，则两个命令都应有各自 manpage 或可追溯的等价入口。因此 033 不能只改 `debian/dasall.1` 文案，必须同时为 `dasall-cli` 提供独立 manpage 安装项。
   - https://www.debian.org/doc/debian-policy/ch-docs.html#manual-pages
2. Debian Policy 6.2/6.3 规定 maintainer scripts 必须保持幂等，并且在没有 controlling terminal 时也要能非交互执行。因此 `postinst` 与 README.Debian 中的 first-install operator path 不能依赖交互式解释或混合命令语义，必须直接给出稳定的 `dasall-cli` 控制面命令。
   - https://www.debian.org/doc/debian-policy/ch-maintainerscripts.html
3. `autopkgtest(1)` 明确其职责是“在 testbed 中测试已安装的二进制 Debian 包”，测试内容来自 source package 的 `debian/tests/*`。因此 installed-package smoke 中的命令名属于安装态行为契约，033 必须同步把 autopkgtest / package smoke 改成双命令语义。
   - https://manpages.debian.org/testing/autopkgtest/autopkgtest.1.en.html

## 4. Design 原子清单

| 项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 Debian 安装面命令归属 | TUI 详设 B.4.2~B.4.5、030 迁移矩阵、031/032 结果 | 本文件第 1~3 节 | 明确 `dasall-cli` 承接结构化控制面，bare `dasall` 只保留 TUI | 若发现 031/032 未闭合，则停止并回到 blocker recovery |
| D2 | 锁定 033 的最小实现范围 | 033 任务行、现有 `debian/` 与 `scripts/packaging/` 命中 | 本文件第 5 节 | 仅修改 install/manpage/README/postinst/autopkgtest/package smoke，不扩张到 routing integration test | 若需要新增运行时兼容分流，升级到 034，不在本轮暗中处理 |
| D3 | 锁定 Build 三件套 | 033 任务行、TUI non-TTY fail-closed 文案、autopkgtest/package smoke 角色 | 本文件第 5 节 | 代码目标、测试目标、验收命令均可二值判定 | 若改完后 `debian/`/`scripts/packaging/` 仍命中 bare control-plane 命令，则不得标记 Done |

## 5. Design -> Build 映射

| Build 原子项 | 代码目标 | 测试目标 | 验收命令 | 正负例 |
|---|---|---|---|---|
| B1 | 更新 `debian/dasall-cli.install` 与 `debian/dasall-cli.manpages`，让包同时安装 `dasall`/`dasall-cli` 和双 manpage；把 `debian/dasall.1` 改写为 TUI 入口并新增 `debian/dasall-cli.1` | 文档/安装面一致性检查 | `rg -n "debian/tmp/usr/bin/dasall-cli|debian/dasall-cli.1|dasall-cli" debian/dasall-cli.install debian/dasall-cli.manpages debian/dasall.1 debian/dasall-cli.1` | 正例：CLI/TUI 各自有安装项与 manpage；负例：`debian/dasall.1` 不再把 bare `dasall` 描述为结构化 CLI |
| B2 | 更新 `debian/dasall-daemon.README.Debian`、`debian/dasall-daemon.postinst`、`debian/package-assets/dasall-daemon/etc/default/dasall-daemon`，把 operator/control-plane 示例切到 `dasall-cli`，并单列 bare `dasall` 为 TUI | maintainer-script/doc consistency | `rg -n "sudo dasall-cli config|Use dasall-cli|bare `dasall`|TUI" debian/dasall-daemon.README.Debian debian/dasall-daemon.postinst debian/package-assets/dasall-daemon/etc/default/dasall-daemon` | 正例：README/postinst/default 注释全部指向 `dasall-cli` operator path；负例：不再出现 `sudo dasall config` |
| B3 | 更新 `debian/tests/pkg-smoke-local-control-plane` 与 `scripts/packaging/*`，把所有结构化控制面命令切到 `dasall-cli`，并为 bare `dasall` 增加 non-TTY TUI fail-closed smoke | shell 语法检查 + dual-command smoke 文案检查 | `rg -n "dasall (config|ping|readiness|run|status|cancel|diag|knowledge)" debian scripts; test $? -eq 1` | 正例：结构化控制面只经 `dasall-cli` 执行；负例：bare `dasall` 不再承载这些子命令 |
| B4 | 回写 TODO / 总账 / worklog / 本文件证据 | evidence consistency | `rg -n "TUI-TODO-033|dasall-cli|pkg-smoke-local-control-plane|pkg_smoke_install.sh|non-interactive control-plane" docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md docs/todos/DASALL_子系统查漏补缺专项记录.md docs/worklog/DASALL_开发执行记录.md docs/todos/tui/deliverables/TUI-TODO-033-debian-command-migration.md` | 正例：033 状态和验证结果可追溯；负例：主账本仍写 033 Ready 或 operator path 仍指向 bare `dasall` |

## 6. D Gate

| 检查项 | 结果 |
|---|---|
| 是否仅选择一个任务 | Pass；本轮只执行 `TUI-TODO-033` |
| 前置依赖是否满足 | Pass；`TUI-TODO-030`、`TUI-TODO-031`、`TUI-TODO-032` 均已 Done，任务行无 blocker |
| 是否完成 Design -> Build 映射 | Pass；见第 5 节 |
| Build 三件套是否锁定 | Pass；代码目标、测试目标、验收命令已锁定 |
| 是否存在未解 blocker | Pass；当前未发现需要先拆的 BLOCK 任务 |

D Gate 结论：PASS。允许进入 `TUI-TODO-033` Build 阶段。

## 7. Build 结果

1. B1 已完成：`debian/dasall-cli.install` 现同时安装 `/usr/bin/dasall` 与 `/usr/bin/dasall-cli`，`debian/dasall-cli.manpages` 同时注册 `debian/dasall.1` 与新增的 `debian/dasall-cli.1`；`debian/dasall.1` 已改写为 installed TUI manpage，明确 interactive TTY required，非交互式 control-plane 任务应改用 `dasall-cli(1)`。
2. B2 已完成：`debian/dasall-daemon.README.Debian`、`debian/dasall-daemon.postinst` 与 `debian/package-assets/dasall-daemon/etc/default/dasall-daemon` 的 operator/config 示例已统一切到 `dasall-cli`，同时单列 bare `dasall` 为 human-facing TUI entrypoint。
3. B3 已完成：`debian/tests/pkg-smoke-local-control-plane`、`scripts/packaging/pkg_smoke_install.sh`、`scripts/packaging/knowledge_local_installed_proof.sh`、`scripts/packaging/knowledge_failure_injection_installed_proof.sh`、`scripts/packaging/knowledge_refresh_retrieve_soak.sh`、`scripts/packaging/infra_release_soak_gate.sh` 与 `scripts/packaging/README.md` 的结构化控制面命令已统一迁移到 `dasall-cli`；其中 autopkgtest 与 package smoke 额外补了 bare `dasall` non-TTY fail-closed smoke，固定验证文案为 `stdin is not attached to a TTY` 与 `Use dasall-cli for non-interactive control-plane tasks.`。
4. B4 已完成：`docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md`、`docs/todos/DASALL_子系统查漏补缺专项记录.md` 与 `docs/worklog/DASALL_开发执行记录.md` 已回写 033 完成结论，并把下一原子任务推进到 `TUI-TODO-034`。

## 8. Build 合规复核

| 检查项 | 结果 |
|---|---|
| 代码注释是否充分 | Pass；本轮仅做命令名与文案迁移，shell/roff 变更均为直接语义替换，无新增复杂控制流，代码自解释，不需要新增注释 |
| 是否具备正负例 | Pass；正例为 `dasall-cli` 的 installed control-plane `config/ping/readiness/run/status/cancel/diag/knowledge` 路径继续保留，负例为 bare `dasall` 在 non-TTY 环境下必须 fail-closed 并输出 redirect 文案 |
| 是否验证语法/命令面 | Pass；已执行 `sh -n debian/tests/pkg-smoke-local-control-plane`、`sh -n scripts/packaging/pkg_smoke_install.sh`、`sh -n scripts/packaging/knowledge_local_installed_proof.sh`、`sh -n scripts/packaging/knowledge_failure_injection_installed_proof.sh`、`sh -n scripts/packaging/knowledge_refresh_retrieve_soak.sh`、`sh -n scripts/packaging/infra_release_soak_gate.sh`；并执行 `rg -n "dasall (config|ping|readiness|run|status|cancel|diag|knowledge)" debian scripts`，结果零命中 |
| TODO / deliverable / worklog 是否回写 | Pass；033 deliverable、TUI 专项 TODO、子系统总账与 worklog 均已补齐状态、验证与下一任务 |
| 是否保持任务边界 | Pass；本轮只更新 Debian/package smoke/README 与 traceability 文档，没有提前进入 `TUI-TODO-034` 的 routing integration test |

## 9. 结果

1. `TUI-TODO-033` 已完成，installed-package/public docs/package smoke 现统一采用双命令语义：bare `dasall` 仅代表 TUI，`dasall-cli` 承接结构化 control-plane。
2. Debian install/manpage/operator docs/autopkgtest/package smoke 与 packaging proof 脚本之间的命令面已一致；033 的最小 TUI 证据由 non-TTY fail-closed smoke 提供，不需要在本轮提前实现 routing integration test。
3. 下一原子任务：`TUI-TODO-034`，补 `dasall` vs `dasall-cli` 的 command routing integration smoke。