# TUI-TODO-017 status panel fake 展示基线

状态：Done
日期：2026-05-23
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只实现 apps/tui/src/view/TuiStatusPanel.h、apps/tui/src/view/TuiStatusPanel.cpp、tests/unit/tui/TuiStatusPanelTest.cpp 与 focused CMake 注册；不在本轮接入 FTXUI renderer、TuiApp、daemon status projection producer、status integration test 或 snapshot harness。
2. 本任务完成标准是：status panel 能在 fake projection 下稳定展示 stage、tool、pending、budget、recovery、health、safe mode 与 decision summary 八类文本状态；缺字段时显式降级为 unknown/degraded，而不是留空或只依赖颜色。
3. `TUI-TODO-003` 已冻结 `TuiStatusProjection` 仅包含 `stage`、`current_tool`、`pending_interaction`、`budget_summary`、`recovery_summary`、`health_summary`、`safe_mode_summary` 七个字段，因此本轮不扩 DTO，不引入 only-fake `decision_summary` 字段；decision summary 只允许在 view 层由既有 projection 派生。

## 2. 本地事实与证据

1. docs/architecture/DASALL_TUI客户端设计方案.md 第 7.1、7.4、9.5.8 节已冻结 status panel 边界：它只消费受控 projection，职责是展示 stage、tool、pending、budget、recovery、health、safe mode、decision summary；非职责是裁定 recovery、执行 tool、计算 budget 或读取 runtime/access 内部对象。
2. docs/todos/tui/deliverables/TUI-TODO-003-daemon-projection-seam.md 已冻结 `TuiStatusProjection` 字段边界，并明确 status panel 只能消费该 DTO；同一 deliverable 还要求 fake/status 场景可复用同结构，不得增加 only-fake 字段。
3. apps/tui/src/data/FakeScenarioCatalog.h 已提供 `planning_tools`、`needs_confirm`、`recovering`、`route_switch`、`narrow_cjk` 等 deterministic fake status 场景，足以覆盖健康、等待交互、恢复摘要、guarded safe mode 与窄屏文本回放。
4. `TUI-TODO-015` 与 `TUI-TODO-016` 已分别冻结 selector fake 交互与 transcript view 基线，因此本轮 status panel 只需要解决本地文本 badge/fallback 语义，不必越权处理 route/renderer/app loop 状态。

## 3. 外部参考

1. W3C WCAG 2.1 Understanding SC 1.4.1 Use of Color 明确要求：颜色不能作为传达信息、提示动作或区分状态的唯一视觉手段，必须辅以文本或其他可见线索。该原则直接约束本任务中的 status badge/health badge 设计：stage、health、safe mode、degraded 等语义必须出现在文本中，不能指望后续 renderer 仅靠颜色表达。
   - 参考：https://www.w3.org/WAI/WCAG21/Understanding/use-of-color.html

## 4. 冻结结论

### 4.1 数据形状

1. `TuiStatusPanelLine` 冻结为 status panel 的最小渲染行对象：只保留 `text` 与 `degraded`，供后续 renderer/snapshot 直接消费，而不提前引入 FTXUI/DOM 类型。
2. `TuiStatusPanelRenderResult` 冻结为当前 status panel 的局部快照：`lines`、`stage_badge`、`health_summary`、`decision_summary`、`narrow_layout`、`degraded` 足以表达文本 badge、窄屏标签和整体降级状态。
3. `TuiStatusPanel` 冻结为纯 view-local helper：持有 `TuiStatusProjection`，暴露 `render_status_panel()`、`format_stage_badge()`、`format_health_summary()` 与局部 `decision_summary` 派生逻辑；它不读 fake source、不依赖 reducer、不触碰 renderer framework。

### 4.2 渲染与降级语义

1. `format_stage_badge()` 负责把 `stage` 与 `safe_mode_summary` 归一化为显式文本 badge；例如 `tool_calling` 渲染为 `[tool calling]`，guarded safe mode 渲染为 `[reflecting | guarded]`，缺字段时 fail-closed 为 `[unknown stage]`。
2. `format_health_summary()` 负责把 `health_summary` 与 `safe_mode_summary` 合成为纯文本健康摘要；健康态输出 `healthy`，降级态输出 `degraded: ...`，缺字段输出 `degraded: unknown health`，确保后续 renderer 不会只靠颜色暗示健康状态。
3. `render_status_panel()` 固定输出八行文本：stage、tool、pending、budget、recovery、health、safe mode、decision。窄屏模式只缩短标签，不缩减状态语义。
4. decision summary 不新增 DTO 字段，而是按 `pending_interaction -> recovery_summary -> safe_mode_summary -> health_summary -> current_tool -> stage` 的优先级派生用户可见摘要；缺字段时降级为 `degraded: decision summary unavailable`。
5. `current_tool`、`pending_interaction`、`recovery_summary` 等空字段会显式呈现为 `none` 或 `unknown/degraded`，不允许空白占位；`budget_summary` 与 `safe_mode_summary` 缺失时分别降级为 `degraded: unknown budget`、`degraded: unknown safe mode`。

### 4.3 focused test 策略

1. tests/unit/tui/TuiStatusPanelTest.cpp 守住三类正向语义：健康 fake status 的文本 badge 渲染、等待交互的 pending/decision 文本、恢复摘要与 guarded safe mode 的文本呈现。
2. 同一测试文件守住两类负向语义：空 projection 的 unknown/degraded fail-closed 路径，以及 status panel 生产文件的 no-private-include / no-renderer-dependency 边界。
3. tests/unit/tui/CMakeLists.txt 必须注册 `dasall_tui_status_panel_unit_test` 与 `TuiStatusPanelTest`，保证 focused build 与 discoverability 闭合。

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| status panel helper | apps/tui/src/view/TuiStatusPanel.h、apps/tui/src/view/TuiStatusPanel.cpp | TuiStatusPanelTest | ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiStatusPanelTest$' |
| badge/fallback 语义 | apps/tui/src/view/TuiStatusPanel.cpp | TuiStatusPanelTest | ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiStatusPanelTest$' |
| focused registration | tests/unit/tui/CMakeLists.txt | TuiStatusPanelTest | ListTests_CMakeTools() |

## 6. 结果

1. apps/tui/src/view/TuiStatusPanel.h/.cpp 已把 status panel 收敛为纯 view-local helper：支持 stage badge、health summary、decision summary 派生、窄屏标签缩写，以及 unknown/degraded fail-closed 文本兜底。
2. tests/unit/tui/TuiStatusPanelTest.cpp 已 focused 覆盖健康 fake status、pending interaction、recovery guarded 状态、空 projection 降级路径，以及 status panel 文件的 no-private-include / no-renderer-dependency 边界。
3. `ListBuildTargets_CMakeTools()` + `ListTests_CMakeTools()` 通过；`dasall_tui_status_panel_unit_test` 与 `TuiStatusPanelTest` 已进入 VS Code CMake 发现图。
4. `Build_CMakeTools(buildTargets=["dasall_tui_status_panel_unit_test"])` 通过。
5. `RunCtest_CMakeTools(tests=["TuiStatusPanelTest"])` 仍命中仓库已知泛化“生成失败”；已按 repo fallback 口径执行 `ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiStatusPanelTest$'`，1/1 通过。
6. 本轮不宣称 daemon/access status producer、status integration test、renderer/snapshot 或 `TuiApp` status wiring 已完成；这些继续后置到 `TUI-TODO-019~020` 与 `TUI-TODO-025`。

结论：TUI-TODO-017 D Gate = PASS；status panel fake 展示的局部 Build、focused test 与 discoverability 已闭合，可标记 Done。