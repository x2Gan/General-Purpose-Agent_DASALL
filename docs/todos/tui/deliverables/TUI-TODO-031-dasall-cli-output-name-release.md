# TUI-TODO-031 dasall-cli 产物名释放

状态：Done
日期：2026-05-24
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md；docs/todos/DASALL_子系统查漏补缺专项记录.md

## 1. 任务边界

1. 本任务只释放现有结构化 CLI target 的 build-tree 公开产物名：`dasall-cli` target 必须生成 `dasall-cli`，不再生成旧的 bare `dasall`。
2. 本任务不新增正式 TUI `dasall` target，不修改 Debian install / manpage / postinst / autopkgtest / packaging scripts；这些继续归属 `TUI-TODO-032~034`。
3. 本任务不提供旧 `dasall <subcommand>` 兼容别名；命令分流和 fail-closed 行为继续由后续正式 TUI target 与 command routing smoke 验证。

## 2. 本地证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 附件 B.4.1 要求先把 `dasall-cli` target 的公开产物名改回 `dasall-cli`，再由后续 TUI target 产出 bare `dasall`。
2. `docs/architecture/DASALL_TUI客户端设计方案.md` 附件 B.5 将 `CMD-REL-001` 定义为：调整 `apps/cli/CMakeLists.txt`，让现有结构化 CLI 产物名回到 `dasall-cli`，验收命令为 `cmake --build --preset vscode-linux-ninja --target dasall-cli`。
3. `docs/todos/tui/deliverables/TUI-TODO-030-command-release-gate-evidence.md` 已将 Gate-TUI-08 判定为 Pass，并把 `apps/cli/CMakeLists.txt` 的 `OUTPUT_NAME dasall` 列为 `TUI-TODO-031` 的唯一 Build 占用面。
4. `docs/todos/tui/deliverables/BLK-TUI-008-command-release-gate-recheck.md` 已关闭 command release gate blocker，明确 `TUI-TODO-031` 恢复为下一原子任务。
5. 当前 `apps/cli/CMakeLists.txt` 中 `dasall-cli` target 仍设置 `OUTPUT_NAME dasall`，这是本任务需要移除或改写的直接控制点。

## 3. 外部参考

1. CMake `OUTPUT_NAME` target property 文档说明：该属性设置 executable 或 library target 输出文件的 base name；如果未设置，则默认使用 logical target name。这支持本轮最小实现：删除 `OUTPUT_NAME dasall` 后，logical target `dasall-cli` 会回到 `dasall-cli` 输出名。
   - https://cmake.org/cmake/help/latest/prop_tgt/OUTPUT_NAME.html

## 4. Design 原子清单

| 项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定命令释放边界 | TUI 详设 B.4.1 / B.5、`TUI-TODO-030`、`BLK-TUI-008` | 本文件第 1~3 节 | 明确本轮只改 CLI build-tree 产物名 | 若发现 Gate-TUI-08 未 Pass，则停止并回到 blocker recovery |
| D2 | 锁定 Build 三件套 | TUI 专项 TODO 031 行、CMake `OUTPUT_NAME` 文档 | 本文件第 5 节 | 代码目标、测试目标、验收命令均可二值判定 | 若 build-tree 中仍生成旧 `apps/cli/dasall`，不得标记 Done |
| D3 | 锁定后继依赖 | TUI 详设 B.4.2~B.6、TUI 专项 TODO 032~034 | 本文件第 6 节 | Debian/script/TUI target 不混入本轮 | 若后继文件必须同轮修改才可构建，则标记 Scope blocker |

## 5. Design -> Build 映射

| Build 原子项 | 代码目标 | 测试目标 | 验收命令 | 正负例 |
|---|---|---|---|---|
| B1 | 删除或改写 `apps/cli/CMakeLists.txt` 中 `dasall-cli` 的 `OUTPUT_NAME dasall` | `CliControlPlaneCommandNameTest` 正例：`$<TARGET_FILE:dasall-cli>` basename 为 `dasall-cli` 且可执行 | `cmake --build --preset vscode-linux-ninja --target dasall-cli` | 正例：`apps/cli/dasall-cli` 存在；负例：旧 `apps/cli/dasall` 不存在 |
| B2 | 将 `CliControlPlaneCommandNameTest` 接入 CLI unit test CMake | `ctest -N` 可发现 `CliControlPlaneCommandNameTest` | `ctest --preset vscode-linux-ninja -N | rg "CliControlPlaneCommandNameTest"` | 正例：测试可发现；负例：测试未注册则命令无命中 |
| B3 | 回写 TODO / worklog / 本文件证据 | evidence consistency | `rg -n "TUI-TODO-031|CliControlPlaneCommandNameTest|dasall-cli" docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md docs/todos/DASALL_子系统查漏补缺专项记录.md docs/worklog/DASALL_开发执行记录.md docs/todos/tui/deliverables/TUI-TODO-031-dasall-cli-output-name-release.md` | 正例：任务状态和验证结果可追溯；负例：仍写作 Ready 或未记录验收结果 |

## 6. D Gate

| 检查项 | 结果 |
|---|---|
| 是否仅选择一个任务 | Pass；本轮只执行 `TUI-TODO-031` |
| 前置依赖是否满足 | Pass；`TUI-TODO-030` Done，`BLK-TUI-008` Closed，Gate-TUI-08 Pass |
| 是否完成 Design -> Build 映射 | Pass；见第 5 节 |
| Build 三件套是否锁定 | Pass；代码目标、测试目标、验收命令均已锁定 |
| 是否存在未解 blocker | Pass；当前未发现前置 blocker |

D Gate 结论：PASS。允许进入 `TUI-TODO-031` Build 阶段。

## 7. Build 结果

1. 已更新 `apps/cli/CMakeLists.txt`：移除 `dasall-cli` target 的 `OUTPUT_NAME dasall` 覆盖，让 CMake 使用 logical target name 生成 `apps/cli/dasall-cli`。
2. 同一 target 增加 post-build 清理旧 `apps/cli/dasall` 的动作，避免历史 build-tree 残留让 command release gate 误判为仍在产出 bare CLI。
3. 已新增 `tests/unit/apps/cli/CliControlPlaneCommandNameTest.cpp`，并在 `tests/unit/apps/cli/CMakeLists.txt` 注册 `CliControlPlaneCommandNameTest`。测试正例验证 `$<TARGET_FILE:dasall-cli>` basename 为 `dasall-cli` 且可执行；负例验证旧 `apps/cli/dasall` 不存在。
4. 验证结果：
   - `cmake --build --preset vscode-linux-ninja --target dasall-cli`：通过，链接生成 `apps/cli/dasall-cli`。
   - `Build_CMakeTools(buildTargets=["dasall-cli","dasall-cli_control_plane_command_name_unit_test"])`：通过。
   - `ListTests_CMakeTools()`：可发现 `CliControlPlaneCommandNameTest`。
   - `RunCtest_CMakeTools(tests=["CliControlPlaneCommandNameTest"])`：仍返回仓库已知泛化 `生成失败`，未给出具体失败用例。
   - 回退直接执行 `./build/vscode-linux-ninja/tests/unit/apps/cli/dasall-cli_control_plane_command_name_unit_test && test -x build/vscode-linux-ninja/apps/cli/dasall-cli && test ! -e build/vscode-linux-ninja/apps/cli/dasall && echo cli-command-name-test-ok`：通过。

## 8. Build 合规复核

1. 代码注释：本轮 CMake 与测试逻辑均为直接命名 / 路径断言，未新增额外注释；旧 artifact 清理动作与 `CliControlPlaneCommandNameTest` 共同说明实现意图。
2. 正负例：正例覆盖 `dasall-cli` target 文件名为 `dasall-cli` 且可执行；负例覆盖旧结构化 CLI 不再留下 `apps/cli/dasall` artifact。
3. 测试发现性：`ListTests_CMakeTools()` 输出包含 `CliControlPlaneCommandNameTest`。
4. TODO / worklog 回写：需同步更新 TUI 专项 TODO、子系统查漏补缺总账与 `docs/worklog/DASALL_开发执行记录.md`。
5. 提交前状态隔离：本轮允许提交范围限于 `apps/cli/CMakeLists.txt`、CLI focused test、TUI-TODO-031 deliverable、TUI 专项 TODO、子系统总账与 worklog。

## 9. 结果

`TUI-TODO-031` 已完成。build-tree 现在生成 `apps/cli/dasall-cli`，并在构建 `dasall-cli` target 后清理旧 `apps/cli/dasall` 残留；`TUI-TODO-032` 可作为下一原子任务继续新增正式 TUI `dasall` target。