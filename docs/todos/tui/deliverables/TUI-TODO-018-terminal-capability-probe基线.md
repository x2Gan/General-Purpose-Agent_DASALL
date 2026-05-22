# TUI-TODO-018 terminal capability probe 基线

状态：Done
日期：2026-05-23
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只实现 apps/tui/src/terminal/TuiTerminalCapabilityProbe.h、apps/tui/src/terminal/TuiTerminalCapabilityProbe.cpp、tests/unit/tui/TuiTerminalCapabilityProbeTest.cpp 与 focused CMake 注册；不在本轮接线 TuiApp、daemon startup failure、FTXUI renderer、snapshot harness 或 installed/package startup path。
2. 本任务完成标准是：terminal probe 能稳定检测 TTY、TERM、尺寸、UTF-8、bracketed paste、resize 与 external editor，可把启动决策收敛为 `FullScreen`、`Narrow`、`Line`、`FailClosed` 四类 `TuiStartupMode`，并为非 TTY、TERM 异常、尺寸过小、editor 缺失等场景输出稳定 startup issue。
3. `TUI-TODO-001` 已冻结 root/sudo-only operator backend 与 ordinary-user fail-closed 口径，`TUI-TODO-024` 负责把 permission denied / daemon unavailable / profile missing 接到 app startup path，因此本轮只处理 terminal-local capability 与 mode taxonomy，不越权构造 daemon 错误。

## 2. 本地事实与证据

1. docs/architecture/DASALL_TUI客户端设计方案.md 第 5.7、9.5.10 节已冻结 terminal probe 边界：职责是检测 TTY、尺寸、TERM、UTF-8、粘贴、resize、外部编辑器和最低交互能力；非职责是不修改终端永久配置、不启动 daemon、不做 root/sudo 提权。
2. 同一设计文档已冻结 startup path 为 `full/narrow/line/fail-closed`，并明确非 TTY、尺寸过小、TERM 异常必须可判定；能力不足时允许降级到普通行输入或 `/editor`，不允许强行进入 full-screen alternate screen。
3. 当前 `apps/tui` 已具备 fake-only no-daemon substrate、DTO、reducer、slash parser、composer、selector、transcript 与 status panel 基线，terminal probe 只需要提供纯 terminal-local capability 与 startup mode helper，即可被后续 `TUI-TODO-019~020` 和 `TUI-TODO-024` 复用。
4. `tests/unit/tui/CMakeLists.txt` 已物化 TUI focused discoverability 拓扑，因此本轮只需补一个新的 unit target 和 test name，不需要重新设计测试聚合路径。

## 3. 外部参考

1. POSIX `isatty()` 规范明确：该接口用于判断文件描述符是否关联到终端设备；若不是终端则返回 0，常见错误条件包含 `ENOTTY`。这为本任务把 stdin/stdout/stderr 非 TTY 视为 fail-closed 提供了最小可依赖的行业约束。
   - 参考：https://pubs.opengroup.org/onlinepubs/9699919799/functions/isatty.html

## 4. 冻结结论

### 4.1 数据形状

1. `TuiStartupMode` 冻结为四态枚举：`FullScreen`、`Narrow`、`Line`、`FailClosed`。其中 `FullScreen` 与 `Narrow` 服务后续 renderer/app loop，`Line` 服务普通行输入或 `/editor` fallback，`FailClosed` 服务明确退出与 operator 引导。
2. `TuiStartupIssue` 冻结为最小 machine-readable startup issue：只包含 `reason_code`、`message` 与 `blocking`，供 app startup path、banner 或 exit formatter 复用，而不提前引入 daemon/access owner 的错误 carrier。
3. `TuiTerminalCapabilities` 冻结为 terminal probe 的局部快照：TTY 三通道、`term`、`locale`、`columns`、`rows`、UTF-8、paste、resize、external editor 与 `issues` 足以支撑本轮 startup mode 选择。
4. `TuiTerminalProbeEnvironment` 冻结为 focused test 的注入面：单测只喂环境快照，不依赖宿主终端状态；生产态 `probe()` 负责从进程环境读取实际值并归一化到同一结构。

### 4.2 Startup mode 与 issue 语义

1. `probe()` 的生产态实现只读取当前进程 terminal/environment 基线：`isatty(STDIN_FILENO/STDOUT_FILENO/STDERR_FILENO)`、`TERM`、`LC_ALL` / `LC_CTYPE` / `LANG`、`VISUAL` / `EDITOR`、`ioctl(TIOCGWINSZ)` 与 `COLUMNS` / `LINES` fallback；它不修改 termios 状态，也不写终端 escape sequence。
2. `select_startup_mode()` 的当前冻结语义如下：
   - `FullScreen`：满足 full-screen 尺寸下限 `120x36`，且 UTF-8、bracketed paste、resize 均可用。
   - `Narrow`：满足 fallback snapshot 下限 `80x24`，且 UTF-8、bracketed paste、resize 均可用。
   - `Line`：terminal 是可交互 TTY，未触发 blocking issue，但尺寸只够普通行输入，或 UTF-8 / paste / resize 等高级输入能力不足。
   - `FailClosed`：任一 blocking issue 成立，包括非 TTY、TERM 为空/`dumb`/`unknown`，以及尺寸低于 line-mode 最小地板 `40x12`。
3. `40x12` 只作为 line-mode 最小地板，不等同于 full-screen 布局目标；它的作用是确保 startup error、help/banners 和最小行输入仍有可判定文本空间，避免在极小终端上把错误路径也渲染坏。
4. external editor 缺失不阻断 startup mode：它只产出 `external_editor_unavailable` issue，表示 `/editor` 将保持禁用；真正决定 `Line` 或 `FailClosed` 的是 TTY、TERM、尺寸和高级输入能力是否满足最低要求。
5. `format_startup_error()` 只为 `FailClosed` 输出文本：当前格式直接串接 blocking issues，使非 TTY、TERM 异常与尺寸过小都能给出稳定 operator 提示；`Line` 和 `Narrow` 不视为 startup error。

### 4.3 focused test 策略

1. tests/unit/tui/TuiTerminalCapabilityProbeTest.cpp 以注入环境快照覆盖四类正负路径：`FullScreen`、`Narrow`、`Line`、`FailClosed`。
2. 同一测试文件额外守住两条细分语义：external editor 缺失必须产出 `external_editor_unavailable` issue 但不阻断 startup；`invalid_term` 与 `terminal_too_small` 必须都进入稳定 startup error 文本。
3. tests/unit/tui/CMakeLists.txt 必须注册 `dasall_tui_terminal_capability_probe_unit_test` 与 `TuiTerminalCapabilityProbeTest`，把 terminal probe 接入 TUI unit discoverability 拓扑。

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| terminal probe data + environment seam | apps/tui/src/terminal/TuiTerminalCapabilityProbe.h | TuiTerminalCapabilityProbeTest | Build_CMakeTools(buildTargets=["dasall_tui_terminal_capability_probe_unit_test"]) |
| terminal capability classification | apps/tui/src/terminal/TuiTerminalCapabilityProbe.cpp | TuiTerminalCapabilityProbeTest | ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiTerminalCapabilityProbeTest$' |
| focused registration | tests/unit/tui/CMakeLists.txt | TuiTerminalCapabilityProbeTest | ListTests_CMakeTools() |

## 6. 结果

1. apps/tui/src/terminal/TuiTerminalCapabilityProbe.h/.cpp 已把 terminal probe 收敛为纯 terminal-local helper：支持真实环境读取、注入式测试环境、stable startup issues 与 `FullScreen/Narrow/Line/FailClosed` 四态 `TuiStartupMode`。
2. tests/unit/tui/TuiTerminalCapabilityProbeTest.cpp 已 focused 覆盖大终端 full-screen、80x24 narrow、UTF-8/paste/resize 缺失时的 line fallback、external editor 缺失 issue、non-TTY fail-closed、TERM 异常与尺寸过小 fail-closed，以及 production 文件的 no-private-include / no-renderer-dependency 边界。
3. `ListTests_CMakeTools()` 可发现 `TuiTerminalCapabilityProbeTest`。
4. `Build_CMakeTools(buildTargets=["dasall_tui_terminal_capability_probe_unit_test"])` 通过。
5. `RunCtest_CMakeTools(tests=["TuiTerminalCapabilityProbeTest"])` 仍命中仓库已知泛化 `生成失败`；已按 repo fallback 口径执行 `ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiTerminalCapabilityProbeTest$'`，1/1 通过。
6. 本轮不宣称 permission denied、daemon unavailable、profile missing 或 `TuiApp` startup path 已闭合；这些继续后置到 `TUI-TODO-024` 与 `TUI-TODO-023`。

结论：TUI-TODO-018 D Gate = PASS；terminal capability probe 的局部 Build、focused test 与 discoverability 已闭合，可标记 Done。