# TUI-TODO-015 model selector fake 交互基线

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只实现 apps/tui/src/view/TuiModelSelector.h、apps/tui/src/view/TuiModelSelector.cpp、tests/unit/tui/TuiModelSelectorTest.cpp、tests/unit/tui/TuiRouteCatalogFilterTest.cpp 与 focused CMake 注册；不在本轮接入 reducer、renderer、TuiApp、DaemonTuiDataSource 或真实 next-turn carrier。
2. 本任务完成标准是：Auto、Prefer Depth、Pin Model 三种 fake selector 模式具备本地 draft/apply/cancel 语义；route catalog 中的 disabled candidate / disabled depth tier 能展示 reason；focused build、single-test 与 discoverability 证据闭合。
3. TUI-TODO-004 已冻结 `NextTurnPreference` 的真实 carrier 与 fail-closed 语义，因此本任务只负责 fake route catalog 下的 UI 草稿行为，不修改 profile、不宣称真实 provider/model pin 已 build 生效。

## 2. 本地事实与证据

1. docs/architecture/DASALL_TUI客户端设计方案.md 第 6.1~6.5 节与第 9.5.6 节已冻结 selector 边界：TUI 只拥有 next-turn preference 草稿，不拥有最终路由权；Auto 不加显式偏好，Prefer Depth 保持 advisory，Pin Model 在 disallowed/unavailable/not-supported 时 fail-closed。
2. apps/tui/src/data/TuiProjectionTypes.h 已落 `NextTurnPreference`、`TuiModelRouteProjection`、`TuiRouteCatalogView` 与 `TuiRouteCatalogEntry`，因此 selector 可直接消费 module-local projection DTO，而不必绑定 access/runtime/llm owner 对象。
3. apps/tui/src/data/FakeScenarioCatalog.h 已在 `route_switch` 场景提供可选与禁用混合的 fake route catalog：`provider-openai/gpt-4.1`、`provider-openai/gpt-4.1-mini` 可选；`provider-anthropic/claude-sonnet` 与 `provider-local/deep-reasoner` 分别带 `credentials_missing`、`verification_pending`、`allowlist_blocked` 等 disabled reason；当前 fake draft 预置为 `PreferDepth=deep`。
4. TUI-TODO-012 与 TUI-TODO-014 已分别冻结 deterministic fake replay 与 composer 状态机，因此本轮 selector 只需交付 local view/helper，不必越权实现 app loop、daemon attach 或 submit echo。

## 3. 外部参考

1. Textual Select 文档强调：选择控件应维护显式 options 列表、允许 blank/selected 状态分离、对非法 value 走显式拒绝而不是静默回退。该实践与本任务的 selector fake 交互一致：本轮保留明确 option 列表和 selected state，并让 disabled route/depth fail-closed，不引入任何第三方依赖。
   - 参考：https://textual.textualize.io/widgets/select/

## 4. 冻结结论

### 4.1 数据形状

1. `TuiModelSelectorOption` 冻结为 selector-local option view：`display_label`、`provider_id`、`model_id`、`depth_tier`、`selectable`、`selected`、`disabled_reasons`；足以覆盖 fake depth tier 聚合与 fake pin-model candidate 列表，不需要直接暴露 owner DTO。
2. `TuiModelSelector` 冻结为纯本地 helper：内部持有 `TuiRouteCatalogView` 快照、committed draft 与 pending draft，并提供 `open_selector()`、`choose_depth_tier()`、`choose_model()`、`apply_preference()`、`cancel_preference()`、`render_disabled_reason()`；它不读 transport，不持有 renderer，不依赖 reducer。

### 4.2 交互语义

1. `open_selector()` 无显式 mode 时沿用当前 committed draft；显式切换到 Auto / Prefer Depth / Pin Model 时，只重建本地 pending draft，不触碰 profile 或 runtime owner。
2. Auto 会清空 depth/provider/model hint，并输出稳定 `user_visible_summary="auto"`。
3. Prefer Depth 会把 fake route catalog 折叠为唯一 depth tier 列表；当某个 depth tier 仅由 disabled candidate 构成时，仍保留该 tier，但标记为 `selectable=false` 并聚合 disabled reasons。
4. Pin Model 会保留所有 fake candidate route，包括 disabled candidate；disabled route 只展示 reason，不允许被选择。
5. `apply_preference()` 只提交 next-turn-only fake draft，并把 `source` 固定为 `tui_model_selector`；`cancel_preference()` 必须恢复上一次 committed draft。

### 4.3 fail-closed 语义

1. disabled depth tier 调用 `choose_depth_tier()` 时必须返回 false，不允许静默把深度偏好写入 draft。
2. disabled provider/model 调用 `choose_model()` 时必须返回 false，不允许退回到其它 selectable route 冒充 “pin 已生效”。
3. `render_disabled_reason()` 只做人类可读的 reason code 归一化（下划线转空格、去重、拼接），不自行推导 owner 级错误语义。

### 4.4 focused test 策略

1. tests/unit/tui/TuiModelSelectorTest.cpp 守住 fake draft 初始化、Prefer Depth 本地提交、Pin Model 本地提交、cancel rollback 与 selector 文件的 no-private-include / no-renderer-dependency 边界。
2. tests/unit/tui/TuiRouteCatalogFilterTest.cpp 守住 fake route catalog 的 depth 聚合、disabled reason 合并、disabled candidate fail-closed 与 selected option 刷新。
3. tests/unit/tui/CMakeLists.txt 必须注册 `dasall_tui_model_selector_unit_test`、`dasall_tui_route_catalog_filter_unit_test`、`TuiModelSelectorTest` 与 `TuiRouteCatalogFilterTest`，保证 focused build 与 discoverability 闭合。

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| selector local state | apps/tui/src/view/TuiModelSelector.h、apps/tui/src/view/TuiModelSelector.cpp | TuiModelSelectorTest | ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiModelSelectorTest$' |
| route catalog filtering | apps/tui/src/view/TuiModelSelector.cpp | TuiRouteCatalogFilterTest | ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiRouteCatalogFilterTest$' |
| focused registration | tests/unit/tui/CMakeLists.txt | TuiModelSelectorTest、TuiRouteCatalogFilterTest | ctest --preset vscode-linux-ninja --output-on-failure -R '^(TuiModelSelectorTest|TuiRouteCatalogFilterTest)$' |

## 6. 结果

1. apps/tui/src/view/TuiModelSelector.h/.cpp 已把 fake selector 收敛为纯 view-local helper：支持 Auto、Prefer Depth、Pin Model 三模式，本地 draft/apply/cancel 流程，以及 disabled reason 的人类可读渲染。
2. tests/unit/tui/TuiModelSelectorTest.cpp 与 tests/unit/tui/TuiRouteCatalogFilterTest.cpp 已 focused 覆盖 fake draft 提交、cancel rollback、depth tier 聚合、disabled candidate fail-closed 和 no-private-include boundary。
3. Build_CMakeTools(buildTargets=["dasall_tui_model_selector_unit_test","dasall_tui_route_catalog_filter_unit_test"]) 通过。
4. RunCtest_CMakeTools(tests=["TuiModelSelectorTest","TuiRouteCatalogFilterTest"]) 仍命中仓库已知泛化“生成失败”；已按仓库 fallback 口径执行 `ctest --preset vscode-linux-ninja --output-on-failure -R '^(TuiModelSelectorTest|TuiRouteCatalogFilterTest)$'`，2/2 通过。
5. 本轮不宣称真实 route catalog projection、daemon selector attach、submit echo 或 ModelRouter effective route 回显已完成；这些继续后置到 TUI-TODO-027~029。

结论：TUI-TODO-015 D Gate = PASS；fake selector 的局部 Build、focused tests 与 discoverability 已闭合，可标记 Done。