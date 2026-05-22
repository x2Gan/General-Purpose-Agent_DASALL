# TUI-TODO-007 no-daemon prototype target 基线

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只创建 `dasall_tui_prototype` 的最小 build substrate，不提前实现 `TuiApp`、`TuiScreenModel`、`ITuiDataSource`、terminal probe 或 renderer 语义。
2. 本任务只允许修改 `apps/tui/CMakeLists.txt`、`apps/tui/src/main.cpp`、`apps/CMakeLists.txt`，以及与 build/discoverability 直接相关的测试接线；不触碰 bare `dasall` 命令、Debian 安装态和 daemon/access/runtime/provider 真实链路。
3. 本任务的完成标准是：prototype target 可显式构建、可被 focused test 名稳定引用、且继续满足 fake-only、不安装、不改变 bare `dasall`、不链接 daemon/runtime/provider implementation。

## 2. 本地事实与证据

1. `apps/tui/CMakeLists.txt` 当前只保留 `dasall_tui_apply_ftxui_private_dependency(target)` helper，没有任何 `add_executable(dasall_tui_prototype ...)`，因此 `cmake --build --target dasall_tui_prototype` 还没有落点。
2. `apps/CMakeLists.txt` 当前未 `add_subdirectory(tui)`，说明即使 `apps/tui` 存在目录，也没有进入 apps 层构建图。
3. TUI 详设第 5.1、8.4、9.1、9.5.1 和第 10 章已经固定：prototype 阶段必须使用 `dasall_tui_prototype` 名称，保持 fake-only、不安装、不改变 bare `dasall`，并允许在 FTXUI 未解锁时先走 mock/no-renderer 路径。
4. `TUI-TODO-006` 已让 `tests/integration/tui` 进入 discoverability，因此本任务可以直接在既有 integration 拓扑内新增 `TuiPrototypeBuildSmokeTest`，而不必重新搭测试骨架。
5. `TUI-TODO-005` 已冻结 FTXUI 为 default-off private dependency，这意味着 prototype target 不能把 FTXUI 作为硬前置；最稳妥的最小实现是“若 FTXUI target 已解析则私有链接，否则保持 no-renderer main 仍可编译”。

## 3. 外部参考

1. CMake 官方 `add_executable()` 文档明确：普通可执行 target 通过 `add_executable(<name> <sources>...)` 进入构建图，目标名必须在工程内全局唯一；这支持本任务直接引入 `dasall_tui_prototype` 作为独立逻辑 target，而不是复用 `dasall-cli` 或提前占用 bare `dasall`。
   - 参考：https://cmake.org/cmake/help/latest/command/add_executable.html
2. CMake 官方 `install()` 文档明确：可执行产物只有在显式写入 `install(TARGETS ...)` 规则后才会进入安装脚本；因此 prototype 的“不安装”约束可以通过“完全不声明 install 规则”来满足，而不是引入额外 packaging 分支。
   - 参考：https://cmake.org/cmake/help/latest/command/install.html

## 4. 冻结结论

### 4.1 target 与目录边界

1. `apps/CMakeLists.txt` 必须新增 `add_subdirectory(tui)`，使 `apps/tui` 进入 apps 层构建图。
2. `apps/tui/CMakeLists.txt` 必须新增 `add_executable(dasall_tui_prototype ...)`，并把 `apps/tui/src/main.cpp` 作为当前唯一源码入口。
3. prototype target 的逻辑名和构建产物名都保持 `dasall_tui_prototype`；本轮不设置 `OUTPUT_NAME dasall`，也不创建正式 `dasall-tui` target。

### 4.2 依赖与边界规则

1. prototype target 只允许依赖 C++ 标准库，以及在 FTXUI target 已存在时复用 `dasall_tui_apply_ftxui_private_dependency()` 进行局部 `PRIVATE` 链接。
2. prototype target 不得链接 `dasall_access`、`dasall_runtime`、`dasall_apps_runtime_support`、`dasall_memory`、`dasall_knowledge`、任何 provider implementation，或任何 daemon/IPC 实现。
3. prototype target 不得声明 `install(TARGETS dasall_tui_prototype ...)`，也不得改写现有 `dasall-cli` 的 `OUTPUT_NAME dasall` 口径。

### 4.3 行为与验证策略

1. `apps/tui/src/main.cpp` 只提供 no-daemon placeholder 行为：输出当前是 prototype/fake-only/no-renderer 或 FTXUI-available 的启动提示，然后返回成功退出码。
2. 本任务新增 `TuiPrototypeBuildSmokeTest` 作为 focused test 名，用于守住 prototype target 的 discoverability 和构建入口；它可以通过显式 `cmake --build <build-dir> --target dasall_tui_prototype` 完成 smoke 验证。
3. 既有 `TuiTestTopologyDiscoverability` 需要同步感知 `apps/CMakeLists.txt` 与 `apps/tui/CMakeLists.txt` 的接线，防止 prototype target 日后再次退回“目录存在但未纳入构建图”的空壳状态。

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| prototype target wiring | `apps/CMakeLists.txt`、`apps/tui/CMakeLists.txt`、`apps/tui/src/main.cpp` | `TuiPrototypeBuildSmokeTest` | `cmake --build --preset vscode-linux-ninja --target dasall_tui_prototype` |
| topology regression guard | `tests/integration/tui/CMakeLists.txt`、`tests/integration/tui/TuiIntegrationTopologySmokeTest.cpp` | `TuiTestTopologyDiscoverability` | `ctest --preset vscode-linux-ninja -R "^(TuiTestTopologyDiscoverability|TuiPrototypeBuildSmokeTest)$" --output-on-failure` |

## 6. D Gate 结果

1. prototype target 名称、代码目标、测试目标和验收命令已经形成单一口径。
2. 本任务不依赖新的外部 blocker；`TUI-TODO-006` 已满足测试拓扑前置，`TUI-TODO-005` 已满足 FTXUI private dependency 前置。
3. D Gate = PASS，可以进入最小 Build 落地：接线 `apps/tui` target、补 `main.cpp`、注册 `TuiPrototypeBuildSmokeTest`，并用 focused build/test 验证 no-daemon boundary 仍成立。