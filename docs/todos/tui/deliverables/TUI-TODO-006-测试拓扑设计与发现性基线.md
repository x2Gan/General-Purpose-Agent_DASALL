# TUI-TODO-006 测试拓扑设计与发现性基线

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只注册 TUI 的测试拓扑，不提前实现 `TuiScreenModel`、`TuiReducer`、`TuiComposer`、`TuiApp` 或 FTXUI renderer 本体。
2. 本任务只落 `tests/unit/tui`、`tests/integration/tui`、`tests/fixtures/tui/golden` 的最小 skeleton，并把它们接入现有 unit/integration 聚合变量。
3. 本任务的目标是让 focused TUI 测试入口先可发现、可打标签、可被后续任务复用；不把 topology skeleton 冒充为功能已完成或 release-ready。

## 2. 本地事实与证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 已在 9.5.1、9.5.2、9.5.5、9.5.11 与第 12 章明确给出 `TuiAppStartupTest`、`TuiPrototypeSmokeTest`、`TuiScreenModelTest`、`TuiReducerTransitionTest`、`TuiComposerTest`、snapshot/golden 的测试出口，以及 `tests/unit/tui/`、`tests/integration/tui/`、`tests/fixtures/tui/` 的目录落位。
2. `docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md` 的 TUI-TODO-006 已锁定代码目标、测试目标、验收命令和聚合变量名：`DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS`、`DASALL_APPS_TUI_INTEGRATION_TEST_EXECUTABLE_TARGETS`。
3. 当前仓库 `tests/unit/CMakeLists.txt` 尚未 `add_subdirectory(tui)`，`tests/integration/CMakeLists.txt` 也尚未 `add_subdirectory(tui)` 或聚合任何 TUI 测试目标，因此 `ctest -N` 还不能发现 TUI focused tests。
4. 当前 `tests/fixtures/` 仅有 `apps/`、`infra/`、`runtime/`，缺少 `tests/fixtures/tui/golden`，这会让未来 snapshot/golden 测试缺少受控落点。
5. 现有 memory/knowledge 等子系统已经证明：测试拓扑任务的完成标准不是“编译通过”，而是“目录物化 + 顶层 CMake 聚合 + ctest 可发现 + placeholder-only 回归被 smoke test 阻断”。

## 3. 外部参考

1. CMake 官方 `add_test(NAME ...)` 文档明确：测试可通过可执行 target 注册进 CTest，测试名与命令解耦，适合先注册稳定的 discoverability 入口，再在后续任务逐步替换为真实测试实现。
   - 参考：https://cmake.org/cmake/help/latest/command/add_test.html
2. CMake 官方 `set_tests_properties()` 文档明确：测试属性应在创建测试的目录作用域中设置，`LABELS` 是 CTest discoverability 与 gate 过滤的稳定机制。这支持本任务在 `tests/unit/tui` 与 `tests/integration/tui` 内部分别冻结 `unit` / `integration` / `snapshot` 标签，而不是把标签逻辑散落到顶层。
   - 参考：https://cmake.org/cmake/help/latest/command/set_tests_properties.html

## 4. 冻结结论

### 4.1 目录与变量边界

1. TUI unit 测试入口固定落在 `tests/unit/tui/`，并通过 `DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS` 汇总到 `tests/unit/CMakeLists.txt`。
2. TUI integration 测试入口固定落在 `tests/integration/tui/`，并通过 `DASALL_APPS_TUI_INTEGRATION_TEST_EXECUTABLE_TARGETS` 汇总到 `tests/integration/CMakeLists.txt`。
3. snapshot/golden 基线固定落在 `tests/fixtures/tui/golden/`；本轮只要求目录与占位说明物化，不提前提交真实 golden 截图。

### 4.2 发现性与标签规则

1. 本轮至少要让以下测试名可被 `ctest -N` 发现：`TuiScreenModelTest`、`TuiReducerTransitionTest`、`TuiComposerTest`、`TuiPrototypeSmokeTest`。
2. `TuiMainLayoutSnapshotTest` 可以先作为 snapshot skeleton 注册，但必须显式带 `snapshot` 标签，避免后续 snapshot gate 与普通 unit gate 混写。
3. `TuiTestTopologyDiscoverability` 作为 topology smoke 入口保留，用来守住“顶层 CMake 已注册 TUI 子树、fixtures 已物化、placeholder-only 回归被阻断”的最小不变式。
4. 标签冻结如下：
   - unit skeleton：`unit;tui`
   - integration skeleton：`integration;tui`
   - snapshot skeleton：`unit;snapshot;tui`

### 4.3 最小实现策略

1. topology 任务允许使用最小 smoke executable 复用多个 CTest 名称，只要测试名、标签、聚合变量与目录边界已经稳定。
2. topology smoke 的职责是验证：
   - `tests/unit/CMakeLists.txt` 与 `tests/integration/CMakeLists.txt` 已注册 TUI 子树。
   - 顶层聚合列表已包含 TUI target family。
   - `tests/fixtures/tui/golden` 已物化。
   - 不再退回到 `placeholder.cpp` 之类的空树布局。
3. 真正的功能断言仍由 `TUI-TODO-008~020` 替换或扩展；本轮 topology 测试不能声称 `TuiScreenModel`、`TuiReducer`、`TuiComposer`、`TuiApp` 已具备业务语义。

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| unit topology | `tests/unit/tui/CMakeLists.txt`、`tests/unit/tui/*`、`tests/unit/CMakeLists.txt` | `TuiScreenModelTest`、`TuiReducerTransitionTest`、`TuiComposerTest`、`TuiMainLayoutSnapshotTest` | `ctest --preset vscode-linux-ninja -N | rg "Tui(ScreenModel|Reducer|Composer)"` |
| integration topology | `tests/integration/tui/CMakeLists.txt`、`tests/integration/tui/*`、`tests/integration/CMakeLists.txt` | `TuiAppStartupTest`、`TuiPrototypeSmokeTest`、`TuiTestTopologyDiscoverability` | `ctest --preset vscode-linux-ninja -N | rg "TuiPrototypeSmoke"` |
| fixture topology | `tests/fixtures/tui/golden/*` | snapshot discoverability / future golden-ready layout | `rg -n "tests/fixtures/tui/golden|snapshot" tests/integration/tui tests/unit/tui` |

## 6. D Gate 结果

1. 目录落位、聚合变量名、测试名、标签与验收命令已经形成单一口径。
2. 本任务不依赖 `BLK-TUI-006`、`BLK-TUI-007` 或 `BLK-TUI-008`；这些 blocker 约束的是 composer manual gate、session seam 与命令迁移，不阻塞 topology 注册。
3. D Gate = PASS，可以进入最小 Build 落地：创建 topology smoke 源文件、fixtures 占位文件，并接线顶层 CMake 聚合。