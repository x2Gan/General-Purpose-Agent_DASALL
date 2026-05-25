# TUI-TODO-032 正式 TUI dasall target

状态：Done
日期：2026-05-24
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md；docs/todos/DASALL_子系统查漏补缺专项记录.md

## 1. 任务边界

1. 本任务只新增正式 TUI executable target：logical target 为 `dasall-tui`，build-tree 与安装态公开产物名为 `dasall`。
2. 本任务接通正式入口的 daemon-backed 默认数据源，但不修改 Debian install 文件、manpage、postinst、autopkgtest 或 packaging smoke；这些继续归属 `TUI-TODO-033~034`。
3. 本任务不实现旧 `dasall <subcommand>` 兼容分流；命令分流与 fail-closed 旧入口验证继续由 `TUI-TODO-034` 覆盖。

## 2. 本地证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 9.1 将 `dasall-tui` 定义为正式 TUI target，产物名为 `dasall`，满足门禁后安装，并接 daemon projection。
2. `docs/architecture/DASALL_TUI客户端设计方案.md` 附件 B.4.1 要求在 `apps/tui` 新增独立 TUI target，由该 target 产出公开命令 `dasall`。
3. `docs/architecture/DASALL_TUI客户端设计方案.md` 附件 B.4.2 要求新 TUI target 单独安装到 `/usr/bin/dasall`，即 CMake install runtime destination 继续使用仓库的 `${DASALL_INSTALL_BINDIR}`。
4. `docs/architecture/DASALL_TUI客户端设计方案.md` 附件 B.5 将 `CMD-REL-002` 定义为：新增 bare `dasall` TUI 入口 target，并接通终端客户端主路径，验收命令为 `cmake --build --preset vscode-linux-ninja --target dasall-tui`。
5. `TUI-TODO-031` 已释放 `dasall-cli` build-tree 产物名，旧 bare CLI artifact 已清理，因此 `dasall-tui` 可以接管 bare `dasall` 输出名。

## 3. 外部参考

1. CMake `OUTPUT_NAME` target property 文档说明：该属性设置 executable 或 library target 输出文件的 base name；如果未设置，则默认使用 logical target name。本任务需要 logical target `dasall-tui` 输出 bare `dasall`，因此应显式设置 `OUTPUT_NAME dasall`。
   - https://cmake.org/cmake/help/latest/prop_tgt/OUTPUT_NAME.html
2. CMake `install(TARGETS)` 文档说明：`RUNTIME` artifact 覆盖 executable；使用相对 `DESTINATION` 时会相对 `CMAKE_INSTALL_PREFIX` 安装，并推荐使用 GNUInstallDirs 变量。本仓库已定义 `${DASALL_INSTALL_BINDIR}`，因此正式 TUI target 的 install rule 应沿用该变量。
   - https://cmake.org/cmake/help/latest/command/install.html

## 4. Design 原子清单

| 项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定正式 target 边界 | TUI 详设 9.1、B.4.1/B.4.2、B.5 CMD-REL-002 | 本文件第 1~3 节 | 明确本轮只新增正式 target，不修改 Debian/script | 若发现 031 未完成，则停止并回到 blocker recovery |
| D2 | 锁定入口默认数据源 | TUI 详设 9.1、021~023 daemon projection evidence | 本文件第 5 节 | `dasall-tui` 默认使用 `DaemonTuiDataSource`，prototype 仍 fake-only | 若 daemon source 链接无法最小闭合，则标记 Validation blocker |
| D3 | 锁定 Build 三件套 | TUI 专项 TODO 032 行、CMake 官方参考 | 本文件第 5 节 | 代码目标、测试目标、验收命令均可二值判定 | 若 build-tree 不能生成 `apps/tui/dasall`，不得标记 Done |

## 5. Design -> Build 映射

| Build 原子项 | 代码目标 | 测试目标 | 验收命令 | 正负例 |
|---|---|---|---|---|
| B1 | 在 `apps/tui/CMakeLists.txt` 新增 `dasall-tui` executable，设置 `OUTPUT_NAME dasall`，并安装到 `${DASALL_INSTALL_BINDIR}` | `DasallTuiEntrypointSmokeTest` 正例：`$<TARGET_FILE:dasall-tui>` basename 为 `dasall` 且可执行 | `cmake --build --preset vscode-linux-ninja --target dasall-tui` | 正例：`apps/tui/dasall` 存在；负例：logical target artifact `apps/tui/dasall-tui` 不存在 |
| B2 | 让正式 target 复用 `apps/tui/src/main.cpp` 并通过 compile definition 默认接入 `DaemonTuiDataSource` | `DasallTuiEntrypointSmokeTest` 检查 `main.cpp` 含 formal entrypoint 开关且 CMake 链接 daemon/ipc source | `ctest --preset vscode-linux-ninja -R "DasallTuiEntrypointSmokeTest" --output-on-failure` | 正例：正式 target 有 daemon source；负例：prototype target 仍不安装且保持 fake-only |
| B3 | 回写 TODO / worklog / 本文件证据 | evidence consistency | `rg -n "TUI-TODO-032|dasall-tui|DasallTuiEntrypointSmokeTest" docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md docs/todos/DASALL_子系统查漏补缺专项记录.md docs/worklog/DASALL_开发执行记录.md docs/todos/tui/deliverables/TUI-TODO-032-formal-dasall-tui-target.md` | 正例：任务状态和验证结果可追溯；负例：仍写作 Ready 或未记录验收结果 |

## 6. D Gate

| 检查项 | 结果 |
|---|---|
| 是否仅选择一个任务 | Pass；本轮只执行 `TUI-TODO-032` |
| 前置依赖是否满足 | Pass；`TUI-TODO-023`、`TUI-TODO-024`、`TUI-TODO-030`、`TUI-TODO-031` 均已 Done，任务行无阻塞项 |
| 是否完成 Design -> Build 映射 | Pass；见第 5 节 |
| Build 三件套是否锁定 | Pass；代码目标、测试目标、验收命令均已锁定 |
| 是否存在未解 blocker | Pass；当前未发现前置 blocker |

D Gate 结论：PASS。允许进入 `TUI-TODO-032` Build 阶段。

## 7. Build 结果

1. 已更新 `apps/tui/CMakeLists.txt`：新增正式 executable target `dasall-tui`，复用 `apps/tui/src/main.cpp`，编译 `DaemonTuiDataSource.cpp` 与 `TuiIpcController.cpp`，链接 `dasall_tui_prototype_core` 与 `dasall_platform`。
2. 已为 `dasall-tui` 设置 `OUTPUT_NAME dasall`，build-tree 生成 `build/vscode-linux-ninja/apps/tui/dasall`，并增加 `install(TARGETS dasall-tui RUNTIME DESTINATION ${DASALL_INSTALL_BINDIR})`。
3. 已更新 `apps/tui/src/main.cpp`：在 `DASALL_TUI_FORMAL_ENTRYPOINT=1` 下默认使用 `DaemonTuiDataSource`；prototype target 未设置该编译定义，继续使用 `planning_tools` fake 场景、bootstrap ticks 与 selector preview。
4. 已新增 `tests/integration/tui/DasallTuiEntrypointSmokeTest.cpp`，并在 `tests/integration/tui/CMakeLists.txt` 注册 `DasallTuiEntrypointSmokeTest`；同时更新 `TuiIntegrationTopologySmokeTest` 的 discoverability 断言。
5. 验证结果：
   - `Build_CMakeTools(buildTargets=["dasall-tui"])`：通过，链接生成 `apps/tui/dasall`。
   - `Build_CMakeTools(buildTargets=["dasall-tui","dasall_tui_entrypoint_smoke_integration_test","dasall_tui_integration_topology_smoke_integration_test"])`：通过。
   - `ListTests_CMakeTools()`：可发现 `DasallTuiEntrypointSmokeTest` 与 `TuiTestTopologyDiscoverability`。
   - `RunCtest_CMakeTools(tests=["DasallTuiEntrypointSmokeTest","TuiTestTopologyDiscoverability"])`：仍返回仓库已知泛化 `生成失败`，未给出具体失败用例。
   - 回退直接执行 `./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_entrypoint_smoke_integration_test && ./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_integration_topology_smoke_integration_test && test -x build/vscode-linux-ninja/apps/tui/dasall && test ! -e build/vscode-linux-ninja/apps/tui/dasall-tui && echo dasall-tui-entrypoint-ok`：通过。

## 8. Build 合规复核

1. 代码注释：本轮新增 CMake target、compile definition 与入口分支均由命名表达目的；未添加额外注释。`DASALL_TUI_FORMAL_ENTRYPOINT` 明确区分正式 daemon-backed 入口与 prototype fake-only 入口。
2. 正负例：正例覆盖 `dasall-tui` target 文件名为 `dasall` 且可执行、CMake 安装规则存在、正式入口默认接 `DaemonTuiDataSource`；负例覆盖 build-tree 不产生 `apps/tui/dasall-tui` runtime artifact，且 prototype target 仍无 install rule、仍保留 fake scenario path。
3. 测试发现性：`ListTests_CMakeTools()` 输出包含 `DasallTuiEntrypointSmokeTest`；`TuiIntegrationTopologySmokeTest` 也同步断言该 test name 被本地 integration CMake 注册。
4. TODO / worklog 回写：需同步更新 TUI 专项 TODO、子系统查漏补缺总账与 `docs/worklog/DASALL_开发执行记录.md`。
5. 提交前状态隔离：本轮允许提交范围限于 `apps/tui/CMakeLists.txt`、`apps/tui/src/main.cpp`、TUI integration smoke/test registration、TUI-TODO-032 deliverable、TUI 专项 TODO、子系统总账与 worklog。

## 9. 结果

`TUI-TODO-032` 已完成。build-tree 现在由正式 target `dasall-tui` 生成 `apps/tui/dasall`，该 target 安装规则指向 `${DASALL_INSTALL_BINDIR}`，并在正式入口下默认使用 daemon-backed data source；`dasall_tui_prototype` 继续保持 non-installed fake-only 小样。下一步可进入 `TUI-TODO-033` 更新 Debian 命令迁移文件。