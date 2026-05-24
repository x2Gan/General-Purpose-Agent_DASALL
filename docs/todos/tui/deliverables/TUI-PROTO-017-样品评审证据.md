# TUI-PROTO-017 样品评审证据

状态：Done（样品评审清单、formal sign-off 与 BLK-TUI-006 manual terminal Full pass 均已回写）
日期：2026-05-24
来源 TODO：docs/todos/tui/DASALL_TUI小样快速实现专项TODO-2026-05-12.md

## 1. 任务边界

1. 本任务只把 `TUI-TODO-014`、`TUI-TODO-015`、`TUI-TODO-019`、`TUI-TODO-020`、`TUI-TODO-029` 与 `TUI-TODO-035` 已落盘的 fake-only prototype 证据收敛成 `TUI-PROTO-017` 等价样品评审资产，形成采纳 / 延后 / 废弃清单，并明确 `Gate-TUI-PROT-06` 的清单回写与 `BLK-TUI-006` 人工 gate 边界。
2. 本任务不修改 `apps/tui`、`apps/cli`、`debian/` 或 `scripts/packaging/` 的生产实现，不把样品评审误写成 installed-path `dasall` ready，也不替代 FTXUI installed-path Debian source/binary strategy review。
3. 本任务对命令迁移 gate 的作用限于闭合 B.0 第 1 项中的“采纳 / 延后 / 废弃清单已回写”证据缺口；formal product/engineering sign-off 已由 `TUI-PROTO-017-formal-sample-signoff.md` 完成，CJK/IME/resize 的真实终端人工 gate 已由 `BLK-TUI-006-manual-terminal-evidence.md` 以用户确认 Full pass 关闭。

## 2. 评审输入

1. `docs/todos/tui/deliverables/TUI-TODO-020-fake-only-TuiApp小样基线.md` 已证明 fake-only `TuiApp` 能把 `probe -> fake source -> reducer -> renderer loop -> exit` 接成统一小样，并用 `TuiAppStartupTest` / `TuiPrototypeSmokeTest` 守住启动、回放、渲染与退出闭环。
2. `docs/todos/tui/deliverables/TUI-TODO-019-ftxui-renderer-adapter基线.md` 已冻结 `TuiDesignTokens`、`TuiLayoutMetrics`、`FtxuiRendererAdapter` 与 120x36 / 80x24 / selector modal / busy draft snapshot 基线。
3. `docs/todos/tui/deliverables/TUI-TODO-014-composer状态机基线.md`、`TUI-TODO-015-model-selector-fake交互基线.md`、`TUI-TODO-017-status-panel-fake展示基线.md` 与 `TUI-TODO-018-terminal-capability-probe基线.md` 已分别闭合 composer、selector、status panel 与 terminal startup taxonomy 的 focused 证据。
4. `docs/todos/tui/deliverables/TUI-TODO-029-next-preference提交回显.md` 已补齐 selector draft -> `submit_turn` payload -> owner route/receipt echo 的 focused integration 证据，说明样品中的 selector 不是纯视觉占位。
5. `docs/todos/tui/deliverables/TUI-TODO-035-交付证据回写.md` 已明确：此前缺的不是 prototype baseline 本身，而是长期样品评审资产；本任务就是把该资产补齐。
6. `docs/todos/tui/DASALL_TUI小样快速实现专项TODO-2026-05-12.md` 第 4.4 节已冻结样品评审决策面：主布局比例、transcript 表达、composer 行为、selector 表达、状态栏表达、色彩与密度。

## 3. 样品事实与当前证据

| 评审面 | 当前仓内证据 | 评审结论 |
|---|---|---|
| 主布局比例 | `TuiMainLayoutSnapshotTest` 已覆盖 120x36 full-screen、80x24 narrow CJK、selector modal 与 busy draft；`TUI-TODO-020` 已把这些布局接到真实 prototype app loop | 证据充分，允许冻结当前 full-screen + narrow fallback 布局 |
| Transcript 表达 | `TUI-TODO-016` 已冻结 summary-only transcript、collapse/scroll 语义；`TUI-TODO-020` smoke 已展示 transcript/tool summary 与 selector/composer 同屏 | 证据充分，允许采纳“只展示受控摘要，不展示 raw CoT / provider-private dump” |
| Composer 行为 | `TuiComposerTest` / `TuiComposerHistoryTest` 已守住 multiline、history recall、reverse-search、external editor、busy draft；`TUI-TODO-020` 已把 composer 接入 prototype；`BLK-TUI-006` 已补用户确认 Full pass | prototype 级证据和真实终端人工证据均已闭合 |
| Selector 表达 | `TuiModelSelectorDaemonTest` 与 `TuiNextPreferenceIntegrationTest` 已守住 Auto / PreferDepth / PinModel、disabled reason、effective route echo 与 fail-closed | 证据充分，允许采纳 modal selector 与 next-turn-only preference 语义 |
| 状态栏表达 | `TUI-TODO-017` 与 `TUI-TODO-025` 已守住 stage / budget / recovery / health / decision summary 的 helper + integration evidence | 证据充分，允许采纳当前 status panel 信息优先级 |
| CJK / 80x24 | `narrow_cjk` fake scenario、`TuiMainLayoutSnapshotTest` 与 `TuiAppStartupFailureTest` 已覆盖 80x24 + 中文压力与 narrow fallback；`BLK-TUI-006` 已补用户真实终端确认 | 样品评审输入与真实终端人工确认均已通过 |
| IME / live resize / composer interactive ergonomics | `BLK-TUI-006-manual-terminal-evidence.md` 记录用户确认 TUI 终端手测通过；自动化 self-check/snapshot 作为边界证据 | Full pass；不再保留为阻塞项 |

## 4. 采纳 / 延后 / 废弃清单

### 4.1 采纳

1. 采纳 `dasall_tui_prototype` 作为当前阶段唯一被接受的样品基线：允许以 fake-only、non-install、no-daemon 的方式承接 TUI 交互、布局与术语决策，但不外推为 installed `dasall` 入口。
2. 采纳 120x36 下的主布局结构：transcript 主列 + status side rail + composer 底部输入区；80x24 下改为 stacked status fallback，而不是继续并栏挤压 transcript。
3. 采纳 transcript 的 summary-only 表达：assistant/tool/system 只展示受控摘要、badge、timestamp 与 collapse affordance，不展示 raw CoT、provider-private reasoning 或原始 tool payload。
4. 采纳 composer 的 prototype 行为基线：multiline draft、history recall、reverse-search、external editor 与 busy draft 保持可见，并继续作为后续正式 TUI composer 的基线输入。
5. 采纳 selector 的 next-turn-only 语义与 modal 交互：Auto / PreferDepth / PinModel 三模式、disabled reason、apply/cancel 反馈与 owner echo 现在已有稳定证据，不再视为 purely fake decoration。
6. 采纳 `planning_tools` 作为标准展示场景，`golden_ready` 作为空闲基线，`narrow_cjk` 作为 80x24 + 中文压力场景；后续样品对比、文档截图与 regression 都应优先围绕这三类场景组织。

### 4.2 延后

1. 不再延后 IME 多阶段输入、候选词弹出与复杂粘贴交互的当前 release gate 验收；`BLK-TUI-006` 已以用户确认 Full pass 关闭。未来若要扩大终端矩阵或补录 artifact，另开兼容性增强任务。
2. 不再延后 live resize 交互的人机验收；`BLK-TUI-006` 已以用户确认 Full pass 关闭。未来若要覆盖更多 emulator，另开兼容性增强任务。
3. 延后 installed-path `dasall` 命令迁移、Debian manpage/README.Debian/postinst/autopkgtest 改写与 package smoke 切换；这些不属于小样评审直接采纳面，继续后置到 `TUI-TODO-031~034`。
4. 延后 ordinary-user full-function TUI 与 user-level daemon 路径；当前样品只接受 root/sudo-only operator backend + ordinary-user fail-closed 的启动语义。

### 4.3 废弃

1. 废弃 fake-only 阶段抢占 installed bare `dasall` 的做法；Gate-TUI-08 已转 Pass，但 installed 命令面仍必须由 `TUI-TODO-031~034` 按序落地。
2. 废弃 80x24 终端下继续强制保留 transcript/status 并栏的布局方案；该方案与当前 `TuiLayoutMetrics` 的 narrow fallback 冻结结论冲突，且会重新引入内容重叠风险。
3. 废弃把 selector 写成 profile writer、route owner 或 silent fallback controller 的方向；selector 只表达 next-turn preference，最终 route 仍归 profiles/llm/ModelRouter 裁定。
4. 废弃把 FTXUI 依赖公开泄漏到 `apps/tui` 之外的方案；renderer 依赖继续保持 `apps/tui` private boundary。

## 5. Gate 与 blocker 回写

| Gate / Blocker | 当前状态 | 结论 |
|---|---|---|
| `Gate-TUI-PROT-06` | Pass | 本文件已形成长期样品评审资产，`采纳 / 延后 / 废弃` 清单不再只存在于零散 deliverable 与 TODO 口径里；formal product/engineering sign-off 已由 `TUI-PROTO-017-formal-sample-signoff.md` 完成。 |
| `Gate-TUI-05` | Pass | 样品评审清单、formal sign-off 与 CJK/IME/resize/composer 真实终端 Full pass 均已回写。 |
| `BLK-TUI-006` | Closed | `BLK-TUI-006` 已由 `BLK-TUI-006-manual-terminal-evidence.md` 关闭，证据边界为用户确认的真实终端 Full pass + 自动化 build/self-check/snapshot。 |
| Gate-TUI-08 B.0 第 1 项 | Pass | 命令迁移前置门禁中的“采纳 / 延后 / 废弃清单已回写”与 formal sample sign-off 均具备可追溯证据。 |

## 6. Design -> Build 映射

| 评审结论 | 锁定证据 | 锁定验收命令 |
|---|---|---|
| 主布局与窄屏 fallback 采纳 | `TUI-TODO-019`、`TUI-TODO-020` | `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(TuiDesignTokensTest|TuiMainLayoutSnapshotTest|TuiAppStartupTest|TuiPrototypeSmokeTest)$'` |
| composer / selector / status prototype 基线采纳 | `TUI-TODO-014`、`015`、`017`、`029` | `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(TuiComposerTest|TuiComposerHistoryTest|TuiModelSelectorTest|TuiModelSelectorDaemonTest|TuiNextPreferenceIntegrationTest|TuiStatusPanelTest)$'` |
| IME / resize 人工 gate 关闭 | `BLK-TUI-006` | `rg -n "BLK-TUI-006|IME|resize|manual|Full pass|Closed" docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md docs/todos/tui/deliverables/TUI-PROTO-017-样品评审证据.md` |

## 7. 结果

1. `TUI-PROTO-017` 现在已有长期样品评审资产；主布局、transcript、composer、selector、status panel 与场景基线的采纳 / 延后 / 废弃口径不再缺失。
2. `Gate-TUI-PROT-06` 当前已 Pass：评审资产已回写，formal product/engineering sign-off 已完成；`Gate-TUI-05` 已因 `BLK-TUI-006` Full pass 转为 Pass。
3. `BLK-TUI-006` 已关闭，真实终端 IME / resize / composer human gate 不再阻塞 Gate-TUI-08。
4. 本文件不会把 installed-path `dasall` 命令迁移写成已实现；`TUI-TODO-031~034` 仍需在 Gate-TUI-08 Pass 后按原子任务顺序落地。