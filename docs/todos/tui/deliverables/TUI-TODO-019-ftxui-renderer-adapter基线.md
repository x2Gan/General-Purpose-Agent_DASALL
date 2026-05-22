# TUI-TODO-019 FTXUI renderer adapter 基线

状态：Done
日期：2026-05-23
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只实现 apps/tui/src/view/TuiDesignTokens.h、apps/tui/src/view/TuiLayoutMetrics.h、apps/tui/src/terminal/FtxuiRendererAdapter.h、apps/tui/src/terminal/FtxuiRendererAdapter.cpp、tests/unit/tui/TuiDesignTokensTest.cpp、tests/unit/tui/TuiMainLayoutSnapshotTest.cpp 与 focused CMake 注册；不在本轮接线 `TuiApp` event loop、daemon attach、installed `dasall` 命令迁移或 packaging review。
2. 本任务完成标准是：renderer adapter 能把已落盘的 screen model、transcript view、status panel 与 composer state 组合成稳定的 root layout；`TuiDesignTokens` 与 `TuiLayoutMetrics` 能冻结 40x12 / 80x24 / 120x36 断点、spacing、status panel 比例与 modal footprint；focused tests 能覆盖 design tokens 与主布局 snapshot。
3. `TUI-TODO-005` 已冻结 FTXUI 的 default-off resolver、pin 与 `apps/tui` private dependency 规则，`TUI-TODO-018` 已冻结 terminal capability probe 与 `FullScreen/Narrow/Line/FailClosed` startup mode；因此本轮只负责 renderer baseline，不越权宣称 CJK/IME/resize manual gate、full-screen app loop 或 bare `dasall` release gate 已闭合。

## 2. 本地事实与证据

1. docs/architecture/DASALL_TUI客户端设计方案.md 第 8.4、9.5.11 节已冻结 renderer adapter 边界：职责是把 view model 转为 renderer output、集中治理颜色/间距/badge/布局断点与 snapshot 输出；非职责是不持有业务状态、不调用 data source、不把 FTXUI 类型泄漏到 reducer/model。
2. 同一设计文档已要求 snapshot harness 至少覆盖 80x24、120x36、narrow CJK、selector modal 与 busy draft，并明确布局不足时必须进入 narrow/fallback，而不是让 transcript/status/composer 在窄屏上互相重叠。
3. 当前 `apps/tui` 已具备 projection DTO、reducer、fake scenario replay、slash parser、composer、selector、transcript、status panel 与 terminal probe 基线，renderer adapter 只需要消费这些已冻结 helper，而不需要重新定义 state、projection 或 startup taxonomy。
4. `tests/unit/tui/CMakeLists.txt` 已有稳定 discoverability 拓扑，本轮只需新增 focused unit/snapshot target 并把 `TuiUnitTopologySmokeTest` 独立成单独 test name，不需要重做测试目录结构。

## 3. 冻结结论

### 3.1 Tokens 与布局断点

1. `TuiDesignTokens` 冻结为 renderer-local token 集：集中定义 RGB palette、outer padding、section gap、panel padding、transcript indent、composer min/max lines、badge/focus token 与终端断点。
2. `TuiBreakpointTokens` 当前冻结三层阈值：line floor `40x12`、narrow snapshot `80x24`、full snapshot `120x36`；同时冻结 full-screen status column 宽度 `34` 与 narrow status panel 目标高度 `8`。
3. `TuiLayoutMetrics` 冻结为纯计算对象，不依赖 renderer backend：
   - `FullScreen`：transcript 与 status panel 同行分栏，status panel 走侧栏。
   - `Narrow`：status panel 改为 stacked layout，避免 80x24 侧栏挤压 transcript。
   - `Line`：去掉 selector strip、status panel 与 modal overlay，只保留最小 transcript/composer 布局。
4. 窄屏 status panel 当前允许 priority-based sampling：当 80x24 高度不足以同时保留完整八行 status 文本时，优先保留 budget、stage、decision 与 health 关键信息，保证 narrow snapshot 不因硬性保八行而丢失预算摘要。

### 3.2 Renderer adapter 与 snapshot 语义

1. `FtxuiRendererAdapter.h` 只暴露 `TuiRenderFrame`、`render_root()`、`render_to_screen()` 与 `apply_layout_metrics()`；header 不暴露任何 FTXUI 类型，继续守住 TUI-DES-004 的 no-leak boundary。
2. `render_root()` 当前冻结为 canonical frame builder：输入 `TuiScreenModel`，输出 header/transcript/status/composer/footer/modal 六组已裁剪文本行与一份 `TuiLayoutMetrics`。
3. `render_to_screen()` 当前冻结为 dual-path renderer：
   - 当 `DASALL_TUI_RENDERER_USE_FTXUI=1` 且 private dependency 已可解析时，允许走 FTXUI backend 把 canonical frame 输出到 `ftxui::Screen`。
   - 在当前默认 preset 与 focused tests 中，走 deterministic ASCII snapshot backend，确保 default-off resolver 下仍可稳定验证布局语义与 discoverability。
4. ASCII snapshot backend 不是对正式 full-screen app loop 的替代；它只负责在 `TUI-TODO-020` 之前为主布局、modal overlay、busy draft 与 narrow CJK 提供可回归的 deterministic screen 输出。

### 3.3 Focused test 策略

1. `TuiDesignTokensTest` 负责冻结 token 与 metrics 基线：断点、spacing、status column width、line/narrow/full 布局模式切换，以及 design token header 的 no-private-include / no-renderer-leak 边界。
2. `TuiMainLayoutSnapshotTest` 负责冻结主布局 snapshot：
   - 120x36 full-screen ready shell
   - 80x24 narrow CJK stacked layout
   - selector modal overlay
   - busy draft banner + locked composer
3. `tests/unit/tui/CMakeLists.txt` 必须注册 `dasall_tui_design_tokens_unit_test`、`dasall_tui_main_layout_snapshot_unit_test`、`TuiDesignTokensTest`、`TuiMainLayoutSnapshotTest` 与独立的 `TuiUnitTopologySmokeTest`，并在 FTXUI targets 不可见时把 snapshot target compile definition 固定为 `DASALL_TUI_RENDERER_USE_FTXUI=0`。

## 4. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| design tokens + layout breakpoints | apps/tui/src/view/TuiDesignTokens.h、apps/tui/src/view/TuiLayoutMetrics.h | TuiDesignTokensTest | Build_CMakeTools(buildTargets=["dasall_tui_design_tokens_unit_test"]) |
| renderer frame + snapshot backend | apps/tui/src/terminal/FtxuiRendererAdapter.h、apps/tui/src/terminal/FtxuiRendererAdapter.cpp | TuiMainLayoutSnapshotTest | Build_CMakeTools(buildTargets=["dasall_tui_main_layout_snapshot_unit_test"]) |
| focused registration | tests/unit/tui/CMakeLists.txt、tests/unit/tui/TuiUnitTopologySmokeTest.cpp | TuiUnitTopologySmokeTest | ListTests_CMakeTools() |
| narrow/full snapshot acceptance | tests/unit/tui/TuiMainLayoutSnapshotTest.cpp | TuiMainLayoutSnapshotTest | ctest --preset vscode-linux-ninja --output-on-failure -R '^(TuiDesignTokensTest|TuiMainLayoutSnapshotTest)$' |

## 5. 结果

1. apps/tui/src/view/TuiDesignTokens.h 与 apps/tui/src/view/TuiLayoutMetrics.h 已冻结 renderer-local palette、spacing、badge/focus token、40x12 / 80x24 / 120x36 断点，以及 FullScreen/Narrow/Line 三种 layout metrics 计算规则。
2. apps/tui/src/terminal/FtxuiRendererAdapter.h/.cpp 已落地 canonical frame builder 与 deterministic screen renderer：可组合 session header、route strip、transcript、status panel、composer、footer 与 modal overlay，并保持 header 无 FTXUI 泄漏。
3. tests/unit/tui/TuiDesignTokensTest.cpp 已覆盖 token defaults、layout mode 切换与 no-private-include/no-renderer-leak 边界；tests/unit/tui/TuiMainLayoutSnapshotTest.cpp 已覆盖 full-screen ready、narrow CJK、selector modal 与 busy draft snapshot。
4. tests/unit/tui/CMakeLists.txt 已注册 `dasall_tui_design_tokens_unit_test`、`dasall_tui_main_layout_snapshot_unit_test`、`TuiDesignTokensTest`、`TuiMainLayoutSnapshotTest` 与 `TuiUnitTopologySmokeTest`，并把 snapshot target 接到 optional FTXUI private dependency 开关上。
5. `Build_CMakeTools(buildTargets=["dasall_tui_design_tokens_unit_test"])` 通过。
6. `Build_CMakeTools(buildTargets=["dasall_tui_main_layout_snapshot_unit_test"])` 通过。
7. `RunCtest_CMakeTools(tests=["TuiDesignTokensTest"])` 与 `RunCtest_CMakeTools(tests=["TuiMainLayoutSnapshotTest"])` 仍命中仓库已知泛化 `生成失败`；已按 repo fallback 口径执行 `ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiDesignTokensTest$'` 与 `ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiMainLayoutSnapshotTest$'`，均 1/1 通过。
8. 本轮不宣称 real FTXUI interactive loop、CJK/IME/resize manual evidence、`TuiRenderSnapshotTest` golden 更新策略、fake-only `TuiApp` 或 bare `dasall` 命令迁移已闭合；这些继续后置到 `BLK-TUI-006`、`TUI-TODO-020` 与后续 gate。

结论：TUI-TODO-019 D Gate = PASS；renderer adapter、design tokens、layout metrics 与主布局 snapshot baseline 已闭合，可标记 Done。