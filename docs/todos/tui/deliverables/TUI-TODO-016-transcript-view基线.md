# TUI-TODO-016 transcript view 基线

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只实现 apps/tui/src/view/TuiTranscriptView.h、apps/tui/src/view/TuiTranscriptView.cpp、tests/unit/tui/TuiTranscriptViewTest.cpp 与 focused CMake 注册；不在本轮接入 FTXUI renderer、TuiApp、snapshot harness、daemon data source 或 session lifecycle。
2. 本任务完成标准是：当前前台 session transcript 能以受控摘要方式渲染 user/assistant/system/tool 行，支持 collapsible row 的折叠切换与 scroll-to-bottom 视口定位，并且 raw CoT、provider-private reasoning、secret、raw tool output 不出现在渲染输出中。
3. `TUI-TODO-012` 与 `TUI-TODO-010` 已冻结 fake replay 与 reducer 的消息投影基线，因此本轮 transcript view 只消费 `TuiMessageView`，不越权触碰 owner DTO、transport 或 renderer framework。

## 2. 本地事实与证据

1. docs/architecture/DASALL_TUI客户端设计方案.md 第 5.3、5.4、7.2、9.5.7 节已冻结 transcript 边界：TUI 只展示当前前台 session 的 summary、tool summary、折叠块和滚动状态；不持久化历史，不展示 raw Chain-of-Thought，不展示未清洗工具输出。
2. apps/tui/src/model/TuiScreenModel.h 已冻结 `TuiMessageView`：`role`、`content`、`timestamp`、`badges`、`collapsible`、`collapsed` 足以支撑 transcript 本地 render/collapse state，不需要额外引入 renderer-local DTO。
3. apps/tui/src/model/TuiReducer.cpp 已把 `TuiEventProjection` 收敛为受控 transcript row：turn receipt 只消费 `summary_text` / `disposition`，tool row 只消费 `observation_summary` / `risk_summary` / `tool_name`，而不是 raw tool payload。
4. apps/tui/src/data/FakeScenarioCatalog.h 已提供 `planning_tools`、`recovering` 等 deterministic fake scenario，可稳定产出 assistant/tool summary，足以支撑 transcript view 的 focused 单测与后续 fake-only app loop 接线。

## 3. 外部参考

1. Textual RichLog 文档强调：scrollable transcript/log viewer 需要把 `auto_scroll`、`wrap`、`max_lines` 作为局部视图行为处理，而不是把渲染框架状态泄漏到上游业务对象。该实践与本任务一致：本轮把 word-wrap、scroll-to-bottom 和可视行裁剪收敛在 `TuiTranscriptView` 内部，保持 reducer/model 不依赖 renderer。
   - 参考：https://textual.textualize.io/widgets/rich_log/

## 4. 冻结结论

### 4.1 数据形状

1. `TuiTranscriptLine` 冻结为 transcript render 输出的最小行对象：只保留 `message_index` 与 `text`，供后续 renderer/snapshot 直接消费，无需提前引入 FTXUI/Rich renderable 类型。
2. `TuiTranscriptRenderResult` 冻结为当前 transcript viewport 快照：`visible_lines`、`total_line_count`、`scroll_offset`、`at_top`、`at_bottom` 足以表达滚动窗口与后续布局裁剪结果。
3. `TuiTranscriptView` 冻结为纯本地 helper：持有 `TuiMessageView` 列表与局部 `scroll_offset_`，暴露 `render_transcript()`、`toggle_collapse()`、`scroll_to_bottom()` 与 `set_transcript()`；它不读 fake source、不持有 reducer、不触碰 renderer framework。

### 4.2 渲染与安全语义

1. `render_transcript()` 先把 transcript 扁平化为“header line + body lines”，再按 viewport 高度裁剪可见窗口；header 只显示角色、时间戳与 badge，body 只显示 `TuiMessageView.content` 的受控摘要文本。
2. collapsible 且 collapsed 的消息只展示单行 preview，并追加 `[collapsed]` 明示标记；展开后按 wrap width 输出完整摘要，但仍只消费受控 summary。
3. transcript view 额外做 fail-closed 文本净化：若消息内容或 badge 命中 `chain-of-thought`、`reasoning_content`、`provider-private reasoning`、`api_key`、`authorization:`、`bearer `、`sk-`、`raw tool output`、`stdout:`、`stderr:` 等 marker，则可见内容统一替换为 `[redacted unsafe transcript summary]`。
4. `toggle_collapse()` 对越界 index 或非 collapsible row 返回 false，不隐式篡改消息状态。
5. `scroll_to_bottom()` 以扁平化后的总行数为准计算最大 scroll offset，把 viewport 锚到最新几行；它不负责通用鼠标/键盘滚动事件，这些留给后续 renderer/app loop。

### 4.3 focused test 策略

1. tests/unit/tui/TuiTranscriptViewTest.cpp 守住三类正向语义：受控摘要渲染、collapsible row 的折叠切换、scroll-to-bottom 的最新窗口定位。
2. 同一测试文件守住两类负向语义：危险 marker 不泄漏到可见 transcript；非 collapsible / 越界 row 的 `toggle_collapse()` fail-closed。
3. tests/unit/tui/CMakeLists.txt 必须注册 `dasall_tui_transcript_view_unit_test` 与 `TuiTranscriptViewTest`，保证 focused build 与 discoverability 闭合。

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| transcript render helper | apps/tui/src/view/TuiTranscriptView.h、apps/tui/src/view/TuiTranscriptView.cpp | TuiTranscriptViewTest | ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiTranscriptViewTest$' |
| collapse / scroll state | apps/tui/src/view/TuiTranscriptView.cpp | TuiTranscriptViewTest | ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiTranscriptViewTest$' |
| focused registration | tests/unit/tui/CMakeLists.txt | TuiTranscriptViewTest | ctest --preset vscode-linux-ninja -N | rg 'TuiTranscriptViewTest' |

## 6. 结果

1. apps/tui/src/view/TuiTranscriptView.h/.cpp 已把 transcript 收敛为纯 view-local helper：支持受控摘要 render、collapsible row toggle、word-wrap、viewport 裁剪与 scroll-to-bottom。
2. tests/unit/tui/TuiTranscriptViewTest.cpp 已 focused 覆盖安全摘要渲染、collapse/expand、bottom anchor，以及 transcript view 文件的 no-private-include / no-renderer-dependency 边界。
3. `ListBuildTargets_CMakeTools()` + `ListTests_CMakeTools()` 通过；`dasall_tui_transcript_view_unit_test` 与 `TuiTranscriptViewTest` 已进入 VS Code CMake 发现图。
4. `Build_CMakeTools(buildTargets=["dasall_tui_transcript_view_unit_test"])` 通过。
5. `RunCtest_CMakeTools(tests=["TuiTranscriptViewTest"])` 仍命中仓库已知泛化“生成失败”；已按 repo fallback 口径执行 `ctest --preset vscode-linux-ninja -N | rg 'TuiTranscriptViewTest' && ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiTranscriptViewTest$'`，1/1 通过。
6. 本轮不宣称 snapshot harness、FTXUI renderer、fake-only `TuiApp` 或 real daemon transcript attach 已完成；这些继续后置到 `TUI-TODO-019~020` 与后续 gate。

结论：TUI-TODO-016 D Gate = PASS；transcript view 的局部 Build、focused test 与 discoverability 已闭合，可标记 Done。