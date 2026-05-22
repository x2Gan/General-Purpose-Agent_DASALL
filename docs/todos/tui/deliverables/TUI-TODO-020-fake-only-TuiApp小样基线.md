# TUI-TODO-020 fake-only TuiApp 小样基线

状态：Done
日期：2026-05-23
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只在 fake-only、no-daemon、non-install 边界内把 `probe -> fake source -> reducer -> renderer loop -> exit` 接成最小 `TuiApp` 小样；不推进 daemon projection、session open/close 真链路、route catalog 真消费、`dasall` 命令迁移或 packaging review。
2. 本任务完成标准是：`apps/tui/src/app/TuiApp.h/.cpp` 能装配 `TuiTerminalCapabilityProbe`、`FakeTuiDataSource`、`TuiReducer`、`FtxuiRendererAdapter`、`TuiComposer` 与 `TuiModelSelector`，`apps/tui/src/main.cpp` 能进入 fake-only prototype path，`TuiAppStartupTest` / `TuiPrototypeSmokeTest` 能以 focused integration 方式证明启动、回放、渲染与退出闭环。
3. `BLK-TUI-006` 仍然开放。本轮不宣称 CJK/IME/resize manual gate、real FTXUI interactive event loop、daemon attach、正式 session lifecycle、selector 真链路或 bare `dasall` release gate 已闭合。

## 2. 本地事实与外部参考

1. TUI 详设 9.5.1 已冻结 `TuiApp` 责任面：负责启动终端探测、装配 data source、创建 event loop、协调 renderer 与 reducer、处理退出路径；非职责是不解析 profile、不直接访问 runtime、不拥有 route owner。
2. TUI 详设 9.5.3、9.5.10、9.5.11 与主 TODO 已证明本轮前置件全部就绪：`FakeTuiDataSource` 已提供六个 deterministic scenario，`TuiTerminalCapabilityProbe` 已提供 `FullScreen/Narrow/Line/FailClosed` startup taxonomy，`FtxuiRendererAdapter` 已提供 deterministic screen 输出，`TuiComposer` / `TuiModelSelector` 已分别冻结输入状态机与 fake selector 交互。
3. 当前 `apps/tui/src/main.cpp` 仅打印 placeholder，`tests/integration/tui/TuiAppStartupTest` / `TuiPrototypeSmokeTest` 仍只是 discoverability 占位，因此真正缺口是 app orchestration，而不是 DTO、renderer、terminal probe 或 fake replay substrate。
4. FTXUI README 把 `component` 定义为用户交互与 main loop 层，把 `dom` 定义为随终端尺寸响应的布局层。这支持本轮把 orchestration 留在 `TuiApp`，而把布局继续留在 `FtxuiRendererAdapter` / `TuiLayoutMetrics`，避免在 app loop 内重写 renderer 规则。

## 3. 冻结结论

### 3.1 fake-only app loop

1. `TuiAppOptions` 冻结为 prototype-local 配置集：控制 fake scenario、注入式 terminal environment、bootstrap tick 数、初始 draft、selector preview mode、终端尺寸与输出流；不引入 daemon endpoint、profile 文件路径、runtime handle 或长期 session persistence。
2. `TuiApp::run()` 当前固定执行以下闭环：probe terminal -> select startup mode -> open fake session -> hydrate baseline status / route catalog -> replay bootstrap ticks -> 可选 selector preview -> render deterministic screen -> close fake session。
3. `dispatch_action()` 继续把状态变更收敛到 reducer；本轮只额外处理 startup/status/session/exit 这类 app-local request action，并明确把 `/clear` 降级成可见的 deferred banner，等待 `TUI-TODO-026` / `BLK-TUI-007` 处理真实 daemon session lifecycle。
4. `tick()` 当前只消费 `FakeTuiDataSource::poll_events()` 返回的 event batches，并把 event/status 变化同步回 reducer 与 composer busy state；不引入 background thread、blocking I/O 或未来 streaming attach/replay 语义。
5. `shutdown()` 只关闭 fake session 并返回可判定 exit code；close failure 会转成可见 banner 和非零退出码，但不会隐式重试、提权或启动系统 daemon。

### 3.2 prototype target 与测试接线

1. `apps/tui/CMakeLists.txt` 新增 `dasall_tui_prototype_core`，集中承载 `TuiApp` 与其 fake-only依赖源文件，供 `dasall_tui_prototype`、`TuiAppStartupTest`、`TuiPrototypeSmokeTest` 复用，避免在多个 target 重复拼接同一组 app sources。
2. prototype core 继续保持 `apps/tui/src` public include 与 optional private FTXUI backend 开关；不新增 install rule，不链接 `dasall_access` / `dasall_runtime` / `dasall_apps_runtime_support`。
3. `tests/integration/tui/CMakeLists.txt` 继续保留 `TuiTestTopologyDiscoverability` 与 `TuiPrototypeBuildSmokeTest`，同时把 `TuiAppStartupTest` / `TuiPrototypeSmokeTest` 从 topology 占位替换为真实 integration executable。

### 3.3 smoke 语义

1. `TuiAppStartupTest` 负责守住两类最小二值结果：注入式 full-screen terminal 上 fake session 能启动、渲染并 clean shutdown；非 TTY terminal 会 fail-closed 并输出 startup blocker。
2. `TuiPrototypeSmokeTest` 负责守住 prototype 展示闭环：planning_tools fake scenario 的 transcript receipt、tool summary、status panel、busy composer 与 selector preview modal 都能在捕获帧中被验证。
3. smoke 断言冻结为“captured frames 可见 transcript/tool summary，final frame 可见 selector/composer/status”，不要求所有元素都在最后一帧共存，因为 modal overlay 本身会覆盖局部 transcript 画面。

## 4. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| fake-only app orchestration | apps/tui/src/app/TuiApp.h、apps/tui/src/app/TuiApp.cpp、apps/tui/src/main.cpp | TuiAppStartupTest、TuiPrototypeSmokeTest | Build_CMakeTools(buildTargets=["dasall_tui_prototype"]) |
| prototype source reuse | apps/tui/CMakeLists.txt | TuiPrototypeBuildSmokeTest | Build_CMakeTools(buildTargets=["dasall_tui_app_startup_integration_test","dasall_tui_prototype_smoke_integration_test"]) |
| focused integration registration | tests/integration/tui/CMakeLists.txt、tests/integration/tui/TuiAppStartupTest.cpp、tests/integration/tui/TuiPrototypeSmokeTest.cpp | TuiAppStartupTest、TuiPrototypeSmokeTest | ListTests_CMakeTools() |
| startup + smoke acceptance | tests/integration/tui/TuiAppStartupTest.cpp、tests/integration/tui/TuiPrototypeSmokeTest.cpp | TuiAppStartupTest、TuiPrototypeSmokeTest | ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(TuiAppStartupTest|TuiPrototypeSmokeTest)$' |

## 5. 结果

1. `apps/tui/src/app/TuiApp.h/.cpp` 已落 fake-only prototype app：能够探测 terminal capability、装配 fake data source、同步 baseline status / route / composer state、回放 bootstrap events、渲染 deterministic screen，并在退出时关闭 fake session。
2. `apps/tui/src/main.cpp` 已从 placeholder 切到真实 prototype app path，默认进入 `planning_tools` fake scenario，并展示 busy draft + selector preview 的小样画面。
3. `apps/tui/CMakeLists.txt` 已新增 `dasall_tui_prototype_core`，把 prototype executable 与 integration tests 统一绑定到同一组 fake-only app sources；FTXUI private dependency 仍只在 `apps/tui` 内部可见。
4. `tests/integration/tui/TuiAppStartupTest.cpp` 与 `tests/integration/tui/TuiPrototypeSmokeTest.cpp` 已落真实 focused integration tests；`tests/integration/tui/CMakeLists.txt` 已把 `TuiAppStartupTest` / `TuiPrototypeSmokeTest` 从 topology placeholder 切到真实 test executable。
5. `Build_CMakeTools(buildTargets=["dasall_tui_prototype"])` 通过。
6. `Build_CMakeTools(buildTargets=["dasall_tui_app_startup_integration_test","dasall_tui_prototype_smoke_integration_test"])` 首次失败于两个新测试缺失 `#include <iostream>`；同轮最小修复后重跑通过。
7. `ListTests_CMakeTools()` 可发现 `TuiAppStartupTest` 与 `TuiPrototypeSmokeTest`。
8. `RunCtest_CMakeTools(tests=["TuiAppStartupTest","TuiPrototypeSmokeTest"])` 仍命中仓库已知泛化 `生成失败`；已按 repo fallback 口径执行 `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(TuiAppStartupTest|TuiPrototypeSmokeTest)$'`，2/2 通过。
9. 当前 fake-only prototype 已能展示 fake transcript/status/selector/composer，并保持 non-install、no-daemon、no-command-release 边界；CJK/IME/resize manual gate 继续留给 `BLK-TUI-006`，daemon/status/route 真链路继续后置到 `TUI-TODO-021~029`。

结论：`TUI-TODO-020` 已闭合，fake-only `TuiApp` 小样可标记 Done；下一步转入 `TUI-TODO-021`，把本轮已稳定的 app 调用上下文沉淀为 daemon projection request/response mapping 与 header 草案。