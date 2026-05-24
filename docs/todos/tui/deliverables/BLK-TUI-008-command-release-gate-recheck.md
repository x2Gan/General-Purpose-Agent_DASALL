# BLK-TUI-008 bare `dasall` command release gate recheck

日期：2026-05-24
来源任务：BLK-TUI-008
来源 TODO：docs/todos/DASALL_子系统查漏补缺专项记录.md、docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md
状态：Done（复检完成；BLK-TUI-008 Closed，Gate-TUI-08 Pass）

## 1. 本轮任务选择

project-implementation-cycle 本轮从总账中的 TUI client 残余缺口进入，目标任务为 `BLK-TUI-008`：bare `dasall` 命令迁移前置门禁未满足。

任务选择结论：`BLK-TUI-008` 不是可直接 Build 的代码任务，而是依赖阻塞。它影响 `TUI-TODO-031~034`，但这些后续任务只有在 Gate-TUI-08 转 Pass 后才能推进。因此本轮切换到 blocker recovery，最小动作是复检并固化 Gate-TUI-08 的剩余阻塞条件，防止后续误改 `apps/cli` 的 `OUTPUT_NAME` 或提前安装 TUI 为 `/usr/bin/dasall`。

## 2. Blocker 分类

| 项 | 结论 |
|---|---|
| blocker 类型 | Dependency blocker |
| 根因 | 已解除；formal product/engineering sign-off 已完成，`BLK-TUI-006` 真实终端 manual evidence 已由用户确认 Full pass 并签署 |
| 可自动修复性 | 已通过用户确认 + 自动化证据完成关闭回写；本文件不伪造额外 artifact，也不代替后续命令迁移实现 |
| 原任务是否恢复可执行 | 是。`TUI-TODO-031` 恢复为下一原子任务；`TUI-TODO-032~034` 仍按 031/032/033 的后继依赖顺序执行 |
| 本轮提交性质 | 文档化 blocker closeout 与 Gate-TUI-08 复检；不包含生产代码、CMake、Debian 或 packaging 脚本变更 |

## 3. 本地证据

1. `docs/todos/tui/deliverables/TUI-TODO-030-command-release-gate-evidence.md` 已证明 B.0 第 2、3、4 项具备可追溯证据：daemon-backed projection、root/sudo-only operator 模型、`dasall-cli` 兼容矩阵与旧入口 inventory 已收口。
2. `docs/todos/tui/deliverables/TUI-PROTO-017-样品评审证据.md` 已补齐样品采纳 / 延后 / 废弃清单，`docs/todos/tui/deliverables/TUI-PROTO-017-formal-sample-signoff.md` 已完成 Product / Engineering formal sign-off。
3. `docs/todos/tui/deliverables/TUI-TODO-036-ftxui-installed-path-packaging-review.md` 已冻结正式 Debian release path 使用 package-managed FTXUI / `libftxui-dev`，拒绝长期 vendored installed path；但该 review 明确不提供 IME / resize / composer human gate。
4. `docs/todos/tui/deliverables/BLK-TUI-006-manual-terminal-evidence.md` 已由用户确认真实 TUI 终端手测通过并选择 Full pass，关闭 `BLK-TUI-006`。
5. `docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md` 将随本轮回写：`TUI-TODO-031` 恢复为下一原子任务，`TUI-TODO-032~034` 继续等待前序迁移任务完成。
6. `docs/architecture/DASALL_TUI客户端设计方案.md` 8.3/8.4 要求 CJK、IME、resize、composer 与 snapshot gate 在正式命令迁移前完成；9.1 也要求 `dasall-cli` 与正式 `dasall-tui` 分阶段治理。

## 4. 外部参考

1. Debian Policy 6.2 / 6.3 要求 maintainer scripts 保持幂等，并且在没有 controlling terminal 时能 noninteractive fallback。这支持当前结论：`postinst`、README.Debian 和 operator next steps 不能依赖半迁移命令面或交互式补救；命令释放必须等 gate 全部满足后同批切换。
   - https://www.debian.org/doc/debian-policy/ch-maintainerscripts.html
2. `autopkgtest(1)` 定义其目标是测试已安装的 Debian binary package，并从 source package tests 驱动 installed package 行为。这支持当前结论：`debian/tests/*` 与 package smoke 中的命令名是 installed 行为契约，不能在 `dasall` / `dasall-cli` 角色未分流前提前改一半。
   - https://manpages.debian.org/testing/autopkgtest/autopkgtest.1.en.html

## 5. Gate-TUI-08 复检

| B.0 条件 | 当前状态 | 复检结论 |
|---|---|---|
| 样品评审通过并回写采纳 / 延后 / 废弃清单 | 清单已由 `TUI-PROTO-017` 回写；formal sign-off 已由 `TUI-PROTO-017-formal-sample-signoff.md` 完成 | Pass |
| daemon-backed projection、status、route、session lifecycle 可验证 | `TUI-TODO-023~029` 已闭合 focused evidence | Pass |
| root/sudo-only operator 模型与 ordinary-user fail-closed 已冻结 | `TUI-TODO-001`、`TUI-TODO-024` 已闭合 | Pass |
| `dasall-cli` 兼容矩阵与旧入口 inventory 已收口 | `TUI-TODO-030` 已闭合 | Pass |
| FTXUI 依赖来源、Debian 打包策略、snapshot/IME/CJK 质量门通过 | package-managed FTXUI review 已由 `TUI-TODO-036` 通过；`BLK-TUI-006` manual evidence 已由用户确认 Full pass 并签署 | Pass |

复检结论：Gate-TUI-08 当前转为 Pass。`TUI-TODO-031` 可作为下一原子任务释放 `dasall-cli` 产物名；本轮仅关闭 blocker，不修改 `apps/cli/CMakeLists.txt`、Debian 文件或 packaging scripts。

## 6. 最小解阻动作

`BLK-TUI-008` 的剩余解阻动作已完成：

1. 已完成项：Formal sample sign-off 已由 Product + Engineering 在 `TUI-PROTO-017-formal-sample-signoff.md` 中确认样品采纳结论，包含评审人、日期、采纳范围、延后范围与不允许进入安装态的残余限制。
2. 已完成项：`BLK-TUI-006-manual-terminal-evidence.md` 已记录用户确认的真实终端 Full pass，并保留自动化 build/self-check/snapshot 证据边界。

因此 Gate-TUI-08 复检结果从 Blocked 改为 Pass，下一步进入 `TUI-TODO-031`。

## 7. 本轮未执行的后续动作

1. 本轮不改 `apps/cli/CMakeLists.txt` 中 `dasall-cli` 的 `OUTPUT_NAME dasall`；该动作属于 `TUI-TODO-031`。
2. 本轮不新增正式 installed `dasall-tui` / `/usr/bin/dasall` 安装规则；该动作属于 `TUI-TODO-032`。
3. 本轮不改 `debian/` 或 `scripts/packaging/` 的 bare `dasall` 命令面；该动作属于 `TUI-TODO-033`。
4. 本轮不新增 `DasallCommandRoutingTest`；该动作属于 `TUI-TODO-034`。
5. 本轮只关闭 blocker，不宣称 `TUI-TODO-031~034` 的代码、安装、文档和 smoke 迁移已经完成。

## 8. Design -> Build 映射

| 后续任务 | 进入条件 | 代码 / 文档目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| Formal sign-off closeout | 已完成；Product + Engineering 已确认样品采纳范围 | `TUI-PROTO-017-formal-sample-signoff.md` 已从 Ready for signature 改为 Done | evidence consistency | `rg -n "状态：Done|Product reviewer|Engineering reviewer|同意|采纳|延后|废弃|Gate-TUI-08" docs/todos/tui/deliverables/TUI-PROTO-017-formal-sample-signoff.md` |
| `BLK-TUI-006` manual gate | 已完成 | `BLK-TUI-006-manual-terminal-evidence.md` 已签署为 Done / Full pass | 80x24 / 120x36 / CJK / IME / resize / composer checklist | `rg -n "状态：Done|Full pass|120x36|80x24|CJK|IME|resize|composer|x2Gan" docs/todos/tui/deliverables/BLK-TUI-006-manual-terminal-evidence.md` |
| `TUI-TODO-031` | Gate-TUI-08 已转 Pass | `apps/cli/CMakeLists.txt` 释放 CLI 产物名 | `CliControlPlaneCommandNameTest` | `cmake --build --preset vscode-linux-ninja --target dasall-cli` |
| `TUI-TODO-032~034` | 031 已落地且 formal target / packaging matrix 可执行 | 正式 TUI target、Debian/script 双命令迁移、command routing tests | package smoke / command routing | `ctest --preset vscode-linux-ninja -R "DasallCommandRouting|CliControlPlane" --output-on-failure` |

## 9. Build 合规复核

1. 代码注释：本轮无生产代码改动，无需新增代码注释。
2. 正负例：文档验证包含正向证据（Pass 项与已完成 deliverables）和负向约束（不得改 `OUTPUT_NAME`、不得安装 TUI 为 `dasall`、不得代签 formal sign-off）。
3. 测试发现性：本轮未触及 CMake / test registration；验收采用 focused `rg` 检查 deliverable、专项 TODO、总账与 worklog 的 gate 口径一致性。
4. TODO / worklog 回写：本交付物需要同步回写 TUI 专项 TODO、子系统总账和 worklog，说明 BLK-TUI-008 已复检并关闭，`TUI-TODO-031` 恢复为下一原子任务。
5. 提交前状态隔离：本轮只允许提交本交付物与对应文档回写，不包含 `apps/cli`、`apps/tui`、`debian/`、`scripts/packaging/`。

## 10. 结果

1. 本轮完成 `BLK-TUI-008` 的 blocker recovery 复检与出口固化。
2. `BLK-TUI-008` Closed；Gate-TUI-08 Pass；`TUI-TODO-031` 恢复为下一原子任务。
3. 下一步最小可执行动作是 `TUI-TODO-031`：释放 `dasall-cli` 产物名；`TUI-TODO-032~034` 仍需按后继依赖逐项执行。
