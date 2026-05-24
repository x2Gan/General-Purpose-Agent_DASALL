# TUI-PROTO-017 formal sample sign-off

日期：2026-05-24
状态：Done
签署基线 commit：038c2d44 `docs(tui): recheck command release gate blocker`
来源：`docs/todos/tui/deliverables/TUI-PROTO-017-样品评审证据.md`、`docs/todos/tui/deliverables/BLK-TUI-008-command-release-gate-recheck.md`

## 1. 签署边界

1. 本文件只承接 formal sample sign-off：由 Product 与 Engineering 确认当前 TUI prototype 的采纳 / 延后 / 废弃结论是否可作为后续正式 TUI 的产品与工程输入。
2. 本文件不替代 `BLK-TUI-006` 的真实终端人工验收；CJK、IME、live resize 与 composer interactive ergonomics 必须由单独 manual evidence 文件记录。
3. 本文件不释放 bare `dasall` 命令，不修改 `apps/cli`、`apps/tui`、`debian/` 或 `scripts/packaging/`，也不把 `TUI-TODO-031~034` 改为可执行。
4. Product 与 Engineering 两列都签署为同意后，本文件才可从 Ready for signature 改为 Done；本文件已完成，Gate-TUI-08 的剩余 manual gate 已由 `BLK-TUI-006-manual-terminal-evidence.md` 后续关闭。

## 2. 评审输入

| 输入 | 作用 | 当前结论 |
|---|---|---|
| `TUI-PROTO-017-样品评审证据.md` | 样品采纳 / 延后 / 废弃清单 | Formal sign-off 已由 x2Gan 于 2026-05-24 完成 |
| `TUI-TODO-020-fake-only-TuiApp小样基线.md` | fake-only prototype app loop | 已证明 prototype 可启动、回放、渲染与退出 |
| `TUI-TODO-014-composer状态机基线.md` | composer 状态机 | 已自动化覆盖 multiline、submit、history、reverse-search、external-editor、busy draft |
| `TUI-TODO-019-ftxui-renderer-adapter基线.md` | renderer / layout / snapshot baseline | 已覆盖 120x36、80x24、selector modal、busy draft snapshot |
| `TUI-TODO-029-next-preference提交回显.md` | selector next-turn preference | 已证明 Auto / PreferDepth / PinModel 的 submit echo 与 fail-closed 口径 |
| `TUI-TODO-036-ftxui-installed-path-packaging-review.md` | FTXUI release-path packaging strategy | package-managed FTXUI 被采纳，vendored installed path 被拒绝 |
| `BLK-TUI-008-command-release-gate-recheck.md` | 命令迁移前置门禁复检 | 本 sign-off 已完成；`BLK-TUI-006` 已 Full pass，Gate-TUI-08 已转 Pass |

## 3. Product / Engineering 待确认事项

| 决策面 | 建议结论 | 签署选择 | 限制条件 |
|---|---|---|---|
| fake-only prototype 定位 | 采纳为当前阶段唯一样品基线 | 同意 | 不外推为 installed `/usr/bin/dasall` |
| 120x36 主布局 | 采纳 transcript 主列 + status side rail + composer bottom | 同意 | 以后续真实终端验收结果为准 |
| 80x24 fallback | 采纳 stacked/narrow fallback，废弃强制并栏 | 同意 | 若真实终端出现重叠，必须回退 layout metrics |
| transcript 表达 | 采纳 summary-only，不展示 raw CoT / provider-private dump | 同意 | 安全边界不得因 UI 需求放宽 |
| composer 行为基线 | 采纳 multiline、history、reverse-search、external-editor、busy draft 作为正式输入基线 | 同意 | IME / resize / external editor 往返已由 `BLK-TUI-006` Full pass 关闭 |
| selector 行为 | 采纳 next-turn-only preference；selector 不写 profile、不拥有 route | 同意 | 真实 route 仍归 profiles/llm/ModelRouter owner |
| installed command migration | 延后 | 同意 | Gate-TUI-08 已转 Pass；`TUI-TODO-031` 可作为下一原子任务推进，032~034 按前序依赖执行 |

## 4. 采纳清单

签署后表示同意以下内容进入后续正式 TUI 设计输入：

1. 采纳 `dasall_tui_prototype` 作为 fake-only、non-install、no-daemon 的样品基线。
2. 采纳 120x36 full-screen 布局与 80x24 narrow fallback 的方向。
3. 采纳 transcript summary-only 展示，不展示 raw CoT、secret、provider-private reasoning 或 raw tool payload。
4. 采纳 composer prototype 行为基线：multiline draft、history recall、reverse-search、external editor 与 busy draft。
5. 采纳 selector 的 next-turn-only preference 语义：Auto / PreferDepth / PinModel、disabled reason、apply/cancel 与 owner echo。
6. 采纳 `planning_tools`、`golden_ready`、`narrow_cjk` 作为后续样品对比与 regression 优先场景。

## 5. 延后清单

签署后仍然延后以下内容，不得据此释放命令迁移：

1. IME 多阶段输入、候选词弹出、复杂粘贴与真实终端 external editor 往返。
2. live resize 的人机验收与不同终端模拟器差异处理。
3. installed-path `dasall` 命令迁移、Debian manpage / README.Debian / postinst / autopkgtest / package smoke 切换。
4. ordinary-user full-function TUI 与 user-level daemon 路径。

## 6. 废弃清单

签署后明确废弃以下方向：

1. fake-only 阶段抢占 `/usr/bin/dasall`。
2. 80x24 下继续强制 transcript/status 并栏并造成挤压。
3. selector 写 profile、充当 route owner 或在 `PinModel` 失败后 silent fallback。
4. FTXUI 依赖泄漏到 `apps/tui` 之外，或正式 installed path 长期使用 vendored FTXUI。

## 7. 签署栏

| 角色 | 签署结论 | 签署人 | 日期 | 限制 / 备注 |
|---|---|---|---|---|
| Product reviewer | 同意 | x2Gan | 2026-05-24 | single-maintainer dual-role sign-off |
| Engineering reviewer | 同意 | x2Gan | 2026-05-24 | single-maintainer dual-role sign-off |

若单人同时承担 Product 与 Engineering 角色，必须在备注中写明 `single-maintainer dual-role sign-off`，并显式确认已按两个角色分别检查产品采纳范围与工程风险。

## 8. 签署后回写要求

1. 两个签署结论均为同意时，将本文件状态改为 `Done`，并在 `TUI-PROTO-017-样品评审证据.md`、TUI 专项 TODO、总账与 worklog 中回写 formal sign-off 已完成。
2. 若签署带限制条件，限制条件必须进入 `TUI-PROTO-017` 延后清单或 `BLK-TUI-006` 降级路径，不能只留在签字栏。
3. 本文件签署完成后，Gate-TUI-08 的剩余 manual gate 由 `BLK-TUI-006-manual-terminal-evidence.md` 关闭；当前 `BLK-TUI-006` 已 Full pass，Gate-TUI-08 已转 Pass。

## 9. 验收命令

```bash
rg -n "状态：Done|Product reviewer|Engineering reviewer|同意|采纳|延后|废弃|Gate-TUI-08|BLK-TUI-006|single-maintainer" docs/todos/tui/deliverables/TUI-PROTO-017-formal-sample-signoff.md
```