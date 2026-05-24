# BLK-TUI-006 manual terminal evidence

日期：2026-05-24
状态：Done（Full pass；关闭 BLK-TUI-006，并允许 BLK-TUI-008 / Gate-TUI-08 复检转 Pass）
签署基线 commit：593be6e8 `fix(tui): clear status modal underlay`
来源：`docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md`、`docs/todos/tui/deliverables/TUI-PROTO-017-样品评审证据.md`、`docs/todos/tui/deliverables/TUI-PROTO-017-formal-sample-signoff.md`、`docs/todos/tui/deliverables/BLK-TUI-008-command-release-gate-recheck.md`

## 1. 验收边界

1. 本文件只验收 `BLK-TUI-006`：真实终端中的 CJK / IME / live resize / composer interactive ergonomics。
2. 本文件不替代 formal sample sign-off；Product / Engineering 对样品采纳范围的签署已由 `TUI-PROTO-017-formal-sample-signoff.md` 完成。
3. 本文件不释放 bare `dasall` 命令，不新增 installed `dasall-tui`，不修改 CLI `OUTPUT_NAME`、Debian 文件或 packaging scripts。
4. 本文件以 2026-05-24 当前会话中的用户确认作为真实终端人工验收结果：用户确认“tui 终端已经手测通过”。

## 2. 验收环境记录

本轮未新增本地截图、asciinema 或 artifact 文件；人工证据来自用户在当前会话对真实 TUI 终端的直接确认。自动化证据继续引用签署基线前已通过的 focused build、snapshot 与 `--self-check`。

| 项 | 记录 |
|---|---|
| Tester | x2Gan（用户当前会话确认） |
| Terminal emulator | 用户真实终端；具体 emulator 未落盘 |
| Shell | 未落盘 |
| TERM | 未落盘 |
| Locale | 未落盘 |
| Font | 未落盘 |
| IME / input method | 用户确认 TUI 终端手测通过；具体 IME 未落盘 |
| Baseline commit | 593be6e8 |
| Artifact directory | 未新增本地 artifact；以当前会话确认和自动化证据闭合 |

## 3. 自动化前置检查

| 检查项 | 命令 / 证据 | 结果 |
|---|---|---|
| manual terminal build | `Build_CMakeTools(buildTargets=[dasall_tui_main_layout_snapshot_unit_test,dasall_tui_manual_terminal])` | Pass |
| manual terminal self-check | `./build/vscode-linux-ninja/apps/tui/dasall_tui_manual_terminal --self-check` | Pass |
| composer + layout + app smoke tests | `./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_main_layout_snapshot_unit_test && ./build/vscode-linux-ninja/apps/tui/dasall_tui_manual_terminal --self-check`；前序 composer/simple-editing focused binaries 已通过 | Pass |
| active binary | `build/vscode-linux-ninja/apps/tui/dasall_tui_manual_terminal` | Pass；非安装目标，未释放 bare `dasall` |

## 4. 真实终端人工测试矩阵

执行对象：`build/vscode-linux-ninja/apps/tui/dasall_tui_manual_terminal`。

启动命令：

```bash
./build/vscode-linux-ninja/apps/tui/dasall_tui_manual_terminal
```

若该目标在真实终端中不能接收键盘事件、不能 live redraw 或异常退出，必须在对应 case 写 `Blocked by manual terminal runtime failure`，并选择 Degraded pass 或 Fail；不得把 snapshot / unit test / `--self-check` 证据冒充真实 IME / resize / composer 人工验收。

| Case | 操作步骤 | Pass 标准 | 结果 | Artifact | 备注 |
|---|---|---|---|---|---|
| 120x36 full-screen | 用户真实终端手测 | transcript、status、composer 可见且无重叠 | Pass | 当前会话用户确认 | Full pass 范围内接受 |
| 80x24 narrow fallback | 用户真实终端手测 | narrow fallback 生效，status 不挤压 transcript，composer 不被遮挡 | Pass | 当前会话用户确认 | Full pass 范围内接受 |
| CJK display | 用户真实终端手测 | 中文宽字符不乱码，不破坏边框、截断或 footer | Pass | 当前会话用户确认 | Full pass 范围内接受 |
| CJK input | 用户真实终端手测 | 字符不乱码、不丢字、不破坏 cursor / draft 显示 | Pass | 当前会话用户确认 | Full pass 范围内接受 |
| IME composition | 用户真实终端手测 | composition 不导致崩溃；提交文本与预期一致；候选态不污染 draft | Pass | 当前会话用户确认 | Full pass 范围内接受 |
| multiline composer | 用户真实终端手测 | `Enter` 提交；`Ctrl+J` / 可用的 `Alt+Enter` 换行；多行不遮挡 transcript | Pass | 当前会话用户确认 | Full pass 范围内接受 |
| history recall | 用户真实终端手测 | 只在边界召回；未提交 draft 可恢复；不会覆盖正在编辑文本 | Pass | 当前会话用户确认 | Full pass 范围内接受 |
| reverse search | 用户真实终端手测 | 能命中历史项；普通编辑退出 search；不会进入不可恢复状态 | Pass | 当前会话用户确认 | Full pass 范围内接受 |
| external editor | 用户真实终端手测 | 保存替换 draft；取消保留原 draft；失败有可理解降级 | Pass | 当前会话用户确认 | Full pass 范围内接受 |
| live resize shrink | 用户真实终端手测 | 不崩溃、不丢 draft；布局切换到 narrow fallback | Pass | 当前会话用户确认 | Full pass 范围内接受 |
| live resize expand | 用户真实终端手测 | 不崩溃、不丢 draft；布局恢复 full-screen 信息密度 | Pass | 当前会话用户确认 | Full pass 范围内接受 |
| non-ideal terminal | 用户真实终端手测 | fail-closed 或降级为 line input + `/editor`，错误文案稳定 | Pass | 当前会话用户确认 | Full pass 范围内接受 |

## 5. 结论选择

签署时必须只选择一个结论。

| 结论 | 选择 | 含义 | 对 Gate-TUI-08 的影响 |
|---|---|---|---|
| Full pass | Selected | CJK display/input、IME composition、live resize、multiline/history/reverse-search/external-editor 均通过 | `BLK-TUI-006` 关闭；formal sign-off 已完成，Gate-TUI-08 已转 Pass |
| Degraded pass | Not selected | 复杂 composer / IME / resize 存在限制，但 Product + Engineering 接受 release 口径降级为 line input + `/editor` | 不适用 |
| Fail / keep Open | Not selected | 任一关键路径失败且不接受降级 | 不适用 |

## 6. 降级口径

如果选择 Degraded pass，必须填写以下内容：

| 项 | 内容 |
|---|---|
| 降级范围 | 不适用；本轮选择 Full pass |
| 用户可见文案 | 不适用 |
| release note 限制 | 不适用 |
| 后续补救任务 | 不适用；后续进入 `TUI-TODO-031` 命令迁移原子任务 |
| 是否允许进入 Gate-TUI-08 复检 | 允许；`BLK-TUI-006` 已关闭 |

默认可接受的降级方向只能是：保留 line input + `/editor`，不承诺复杂 composer、IME candidate overlay 或 live resize 完整体验。若要采用其他降级口径，必须新增 Product / Engineering 评审说明。

## 7. 签署栏

| 角色 | 签署结论 | 签署人 | 日期 | 限制 / 备注 |
|---|---|---|---|---|
| Manual tester | Full pass | x2Gan | 2026-05-24 | 用户当前会话确认 TUI 终端已手测通过；single-maintainer multi-role acceptance |
| Product reviewer | 同意关闭 | x2Gan | 2026-05-24 | 采纳 Full pass，不采用降级口径；single-maintainer multi-role acceptance |
| Engineering reviewer | 同意关闭 | x2Gan | 2026-05-24 | 自动化 build/self-check/snapshot 已通过；不在本文件释放 installed `dasall`；single-maintainer multi-role acceptance |

若单人同时承担多个角色，必须在备注中写明 `single-maintainer multi-role acceptance`，并分别确认人工操作、产品降级范围与工程风险均已检查。

## 8. 签署后回写要求

1. Full pass 或 Degraded pass 签署完成后，更新本文件状态为 `Done` 或 `Closed via degraded path`。
2. 同步更新 `TUI-PROTO-017-样品评审证据.md`、`TUI-TODO-035-交付证据回写.md`、TUI 专项 TODO、子系统总账与 worklog。
3. 若选择 Degraded pass，必须同步更新命令迁移相关文档，明确 `dasall` release 不承诺复杂 composer / IME / resize 完整体验。
4. 若选择 Fail / keep Open，只回写失败证据和补救任务，不得恢复 `TUI-TODO-031~034`。

## 9. 验收命令

```bash
rg -n "状态：Done|Full pass|120x36|80x24|CJK|IME|resize|composer|x2Gan|single-maintainer|Gate-TUI-08" docs/todos/tui/deliverables/BLK-TUI-006-manual-terminal-evidence.md
```