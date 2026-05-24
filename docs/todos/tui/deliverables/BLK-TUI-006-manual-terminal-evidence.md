# BLK-TUI-006 manual terminal evidence

日期：2026-05-24
状态：Ready for manual execution（待真实终端人工执行与签署；当前不关闭 BLK-TUI-006 / BLK-TUI-008）
签署基线 commit：038c2d44 `docs(tui): recheck command release gate blocker`
来源：`docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md`、`docs/todos/tui/deliverables/TUI-PROTO-017-样品评审证据.md`、`docs/todos/tui/deliverables/TUI-PROTO-017-formal-sample-signoff.md`、`docs/todos/tui/deliverables/BLK-TUI-008-command-release-gate-recheck.md`

## 1. 验收边界

1. 本文件只验收 `BLK-TUI-006`：真实终端中的 CJK / IME / live resize / composer interactive ergonomics。
2. 本文件不替代 formal sample sign-off；Product / Engineering 对样品采纳范围的签署已由 `TUI-PROTO-017-formal-sample-signoff.md` 完成。
3. 本文件不释放 bare `dasall` 命令，不新增 installed `dasall-tui`，不修改 CLI `OUTPUT_NAME`、Debian 文件或 packaging scripts。
4. 只有人工测试矩阵完成、结论为 Full pass 或 Degraded pass 且签署栏完成后，才允许把本文件状态从 Ready for manual execution 改为 Done / Closed via degraded path。

## 2. 验收环境记录

签署前请在真实终端执行并把结果保存到 artifacts 目录。建议路径：`docs/todos/tui/deliverables/artifacts/BLK-TUI-006-2026-05-24/`。

```bash
export ART=docs/todos/tui/deliverables/artifacts/BLK-TUI-006-2026-05-24
mkdir -p "$ART"

{
  date -Iseconds
  git rev-parse HEAD
  uname -a
  locale
  printf 'TERM=%s\n' "$TERM"
  stty size
  command -v fcitx5 || true
  command -v ibus-daemon || true
  command -v gnome-terminal || true
  command -v konsole || true
  command -v wezterm || true
  command -v alacritty || true
} | tee "$ART/environment.txt"
```

| 项 | 记录 |
|---|---|
| Tester | 待填写 |
| Terminal emulator | 待填写 |
| Shell | 待填写 |
| TERM | 待填写 |
| Locale | 待填写 |
| Font | 待填写 |
| IME / input method | 待填写 |
| Baseline commit | 038c2d44 |
| Artifact directory | `docs/todos/tui/deliverables/artifacts/BLK-TUI-006-2026-05-24/` |

## 3. 自动化前置检查

签署前请复跑以下命令，并把 stdout / stderr 保存到 artifact 目录。

```bash
cmake --build --preset vscode-linux-ninja --target dasall_tui_prototype | tee "$ART/build-dasall_tui_prototype.log"

ctest --test-dir build/vscode-linux-ninja --output-on-failure \
  -R '^(TuiComposerTest|TuiComposerHistoryTest|TuiMainLayoutSnapshotTest|TuiAppStartupTest|TuiPrototypeSmokeTest)$' \
  | tee "$ART/ctest-tui-manual-preflight.log"
```

| 检查项 | 命令 / 证据 | 结果 |
|---|---|---|
| prototype build | `$ART/build-dasall_tui_prototype.log` | 待填写 |
| composer + layout + app smoke tests | `$ART/ctest-tui-manual-preflight.log` | 待填写 |
| active binary | `build/vscode-linux-ninja/apps/tui/dasall_tui_prototype` | 待填写 |

## 4. 真实终端人工测试矩阵

执行对象：`build/vscode-linux-ninja/apps/tui/dasall_tui_prototype`。

若当前 prototype 只能输出 scripted final screen、不能接收真实键盘事件，必须在对应 case 写 `Blocked by non-interactive prototype`，并选择 Degraded pass 或 Fail；不得把 snapshot / unit test 证据冒充真实 IME / resize / composer 人工验收。

| Case | 操作步骤 | Pass 标准 | 结果 | Artifact | 备注 |
|---|---|---|---|---|---|
| 120x36 full-screen | 将终端调整到 120x36，运行 prototype，记录截图或 asciinema | transcript、status、selector、composer 可见且无重叠 | 待填写 | 待填写 |  |
| 80x24 narrow fallback | 将终端调整到 80x24，运行 prototype，记录截图或 asciinema | narrow fallback 生效，status 不挤压 transcript，composer 不被遮挡 | 待填写 | 待填写 |  |
| CJK display | 使用包含中文 transcript / status / composer 的 `narrow_cjk` 或等价场景观察输出 | 中文宽字符不破坏边框、对齐、截断或 footer | 待填写 | 待填写 |  |
| CJK input | 在 composer 输入中文短句并提交或保留 draft | 字符不乱码、不丢字、不破坏 cursor / draft 显示 | 待填写 | 待填写 |  |
| IME composition | 使用真实 IME 输入候选词、选择候选、取消候选 | composition 不导致崩溃；提交文本与预期一致；候选态不污染 draft | 待填写 | 待填写 |  |
| multiline composer | 验证 `Enter`、`Alt+Enter`、`Ctrl+J` | `Enter` 提交；`Alt+Enter` / `Ctrl+J` 换行；多行不遮挡 transcript | 待填写 | 待填写 |  |
| history recall | 提交三条 prompt 后用 `Up` / `Down` 召回 | 只在边界召回；未提交 draft 可恢复；不会覆盖正在编辑文本 | 待填写 | 待填写 |  |
| reverse search | 提交包含关键词的历史后按 `Ctrl+R` 搜索 | 能命中历史项；普通编辑退出 search；不会进入不可恢复状态 | 待填写 | 待填写 |  |
| external editor | 触发 `/editor` 或等价外部编辑器入口，分别保存和取消 | 保存替换 draft；取消保留原 draft；失败有可理解降级 | 待填写 | 待填写 |  |
| live resize shrink | 运行中从 120x36 缩到 80x24 | 不崩溃、不丢 draft；布局切换到 narrow fallback | 待填写 | 待填写 |  |
| live resize expand | 运行中从 80x24 放大到 120x36 | 不崩溃、不丢 draft；布局恢复 full-screen 信息密度 | 待填写 | 待填写 |  |
| non-ideal terminal | 在不支持 resize / paste / UTF-8 完整能力的终端或降级环境中运行 | fail-closed 或降级为 line input + `/editor`，错误文案稳定 | 待填写 | 待填写 |  |

## 5. 结论选择

签署时必须只选择一个结论。

| 结论 | 选择 | 含义 | 对 Gate-TUI-08 的影响 |
|---|---|---|---|
| Full pass | 待选择 | CJK display/input、IME composition、live resize、multiline/history/reverse-search/external-editor 均通过 | `BLK-TUI-006` 可关闭；formal sign-off 已完成，Gate-TUI-08 可进入复检 |
| Degraded pass | 待选择 | 复杂 composer / IME / resize 存在限制，但 Product + Engineering 接受 release 口径降级为 line input + `/editor` | `BLK-TUI-006` 可按降级路径关闭；命令迁移文档必须写明不承诺复杂 composer |
| Fail / keep Open | 待选择 | 任一关键路径失败且不接受降级 | `BLK-TUI-006` 继续 Open；`TUI-TODO-031~034` 继续 Blocked |

## 6. 降级口径

如果选择 Degraded pass，必须填写以下内容：

| 项 | 内容 |
|---|---|
| 降级范围 | 待填写 |
| 用户可见文案 | 待填写 |
| release note 限制 | 待填写 |
| 后续补救任务 | 待填写 |
| 是否允许进入 Gate-TUI-08 复检 | 待填写 |

默认可接受的降级方向只能是：保留 line input + `/editor`，不承诺复杂 composer、IME candidate overlay 或 live resize 完整体验。若要采用其他降级口径，必须新增 Product / Engineering 评审说明。

## 7. 签署栏

| 角色 | 签署结论 | 签署人 | 日期 | 限制 / 备注 |
|---|---|---|---|---|
| Manual tester | 待签署 |  |  |  |
| Product reviewer | 待签署 |  |  |  |
| Engineering reviewer | 待签署 |  |  |  |

若单人同时承担多个角色，必须在备注中写明 `single-maintainer multi-role acceptance`，并分别确认人工操作、产品降级范围与工程风险均已检查。

## 8. 签署后回写要求

1. Full pass 或 Degraded pass 签署完成后，更新本文件状态为 `Done` 或 `Closed via degraded path`。
2. 同步更新 `TUI-PROTO-017-样品评审证据.md`、`TUI-TODO-035-交付证据回写.md`、TUI 专项 TODO、子系统总账与 worklog。
3. 若选择 Degraded pass，必须同步更新命令迁移相关文档，明确 `dasall` release 不承诺复杂 composer / IME / resize 完整体验。
4. 若选择 Fail / keep Open，只回写失败证据和补救任务，不得恢复 `TUI-TODO-031~034`。

## 9. 验收命令

```bash
rg -n "Ready for manual execution|120x36|80x24|CJK|IME|resize|composer|Full pass|Degraded pass|Fail|single-maintainer" docs/todos/tui/deliverables/BLK-TUI-006-manual-terminal-evidence.md
```