# TUI-TODO-034 命令分流测试

状态：Done
日期：2026-05-25
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md；docs/todos/DASALL_子系统查漏补缺专项记录.md

## 1. 任务边界

1. 本任务只补 formal bare `dasall` 与 `dasall-cli` 的 command routing integration evidence：`apps/tui/src/main.cpp` 的最小参数守卫、`tests/integration/apps/tui/DasallCommandRoutingTest.cpp` 和 `tests/integration/apps/CMakeLists.txt` 的 test registration，以及 traceability 文档回写。
2. 本任务不回退 `TUI-TODO-031/032/033` 已完成的 output name、formal target、Debian/manpage/postinst/autopkgtest/package smoke 迁移，也不新增运行时兼容别名层。
3. 本任务完成标准是：证明 bare `dasall` 属于 TUI owner、`dasall-cli` 继续承接 structured control-plane surface，且旧 `dasall <subcommand>` 明确 fail-closed 并给出迁移提示。

## 2. 本地证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 附件 B.6 已固定 034 的验收口径：构建产物中同时存在 `dasall` 与 `dasall-cli`，bare `dasall` 归 TUI owner，`dasall-cli` 保留结构化控制面，旧 bare `dasall` 结构化调用必须 fail-closed 并给出清晰迁移说明。
2. `docs/todos/tui/deliverables/TUI-TODO-033-debian-command-migration.md` 已证明 Debian/package smoke 与 `scripts/packaging/*` 的结构化控制面命令全部迁移到 `dasall-cli`，同时把 bare `dasall` 的 non-TTY fail-closed redirect 文案固定为 `Use dasall-cli for non-interactive control-plane tasks.`。
3. 当前 formal bare `dasall` 入口在 `apps/tui/src/main.cpp` 中直接进入 `TuiApp::run()`，未显式区分旧结构化子命令；如果不补参数守卫，`dasall status` 这类旧调用只能偶然落到 non-TTY startup failure，而不能稳定给出迁移错误。
4. `tests/integration/apps/CMakeLists.txt` 在本轮前只聚合 `apps/cli` integration tests，缺少一条同时锚定 `dasall-tui` 与 `dasall-cli` 的 apps-level routing smoke。

## 3. 外部参考

1. Linux `isatty(3)` 说明 `isatty(fd)` 用于判断文件描述符是否指向终端；若不是终端，返回 0。这与 TUI 在 non-TTY 下 fail-closed、并把 non-interactive control-plane 任务导向 `dasall-cli` 的策略一致。
   - https://man7.org/linux/man-pages/man3/isatty.3.html
2. GNU Command Line Interfaces 建议程序至少支持 `--help` / `--version` 等标准选项，并为无效命令行语义提供清晰、一致的行为。034 因此为 bare `dasall` formal entrypoint 增加最小 `-h/--help` 分支，同时把旧 structured subcommand 显式判定为无效迁移路径，而不是静默吞掉参数。
   - https://www.gnu.org/prep/standards/html_node/Command_002dLine-Interfaces.html

## 4. Design 原子清单

| 项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 034 的 owner 边界 | TUI 详设附件 B.6、030/033 deliverable、031/032 结果 | 本文件第 1~3 节 | 明确 bare `dasall` 归 TUI，`dasall-cli` 归 structured CLI | 若发现 031~033 未闭合，则停止并回到 blocker recovery |
| D2 | 锁定最小 Build 三件套 | 034 任务行、现有 bare `dasall` / `dasall-cli` 行为样本 | 本文件第 5 节 | 代码目标、测试目标、验收命令可二值判定 | 若需要扩张到 Debian/script 迁移，则越界到 033，不在本轮处理 |
| D3 | 锁定回退验证口径 | 仓库现有 `RunCtest_CMakeTools` 已知泛化失败、既有 TUI/CLI 回退模式 | 本文件第 6~9 节 | 先走 CMake Tools，再在工具失败时回退到已构建二进制直跑 | 若连 direct binary 也无法执行，则本轮降级为 Validation blocker |

## 5. Design -> Build 映射

| Build 原子项 | 代码目标 | 测试目标 | 验收命令 | 正负例 |
|---|---|---|---|---|
| B1 | 更新 `apps/tui/src/main.cpp`，为 formal bare `dasall` 入口增加最小参数守卫：保留裸启动 TUI、支持 `-h/--help`、旧 structured subcommand fail-closed 并重定向到 `dasall-cli` | 运行期路由负例 | `Build_CMakeTools(buildTargets=["dasall-tui"])` | 正例：裸 `dasall` 仍进入 TUI startup path；负例：`dasall status` 不再悄悄落到旧 CLI 语义 |
| B2 | 新增 `tests/integration/apps/tui/DasallCommandRoutingTest.cpp` 并在 `tests/integration/apps/CMakeLists.txt` 注册 `DasallCommandRoutingTest` | apps command routing integration | `Build_CMakeTools(buildTargets=["dasall_apps_command_routing_integration_test"])` | 正例：`dasall-cli status --help` 保留 structured usage；负例：bare `dasall` non-TTY redirect 与 `dasall status` migration error |
| B3 | 回写 TUI TODO、总账、worklog 与本 deliverable | evidence consistency | `ctest --preset vscode-linux-ninja -R "DasallCommandRouting|CliControlPlane" --output-on-failure` | 正例：当前态能追溯 034 已闭合；负例：主账本不再保留 034 Ready |

## 6. D Gate

| 检查项 | 结果 |
|---|---|
| 是否仅选择一个任务 | Pass；本轮只执行 `TUI-TODO-034` |
| 前置依赖是否满足 | Pass；`TUI-TODO-032`、`TUI-TODO-033` 均已 Done，任务行无 blocker |
| 是否完成 Design -> Build 映射 | Pass；见第 5 节 |
| Build 三件套是否锁定 | Pass；代码目标、测试目标、验收命令已锁定 |
| 是否存在未解 blocker | Pass；当前未发现需要先拆的 BLOCK 任务 |

D Gate 结论：PASS。允许进入 `TUI-TODO-034` Build 阶段。

## 7. Build 结果

1. B1 已完成：`apps/tui/src/main.cpp` 现已为 formal bare `dasall` 入口增加最小参数守卫。裸启动仍进入 TUI app loop，`-h/--help` 返回简短 usage，旧 `help/version/config/ping/readiness/knowledge/run/status/cancel/diag` 结构化子命令统一 fail-closed，并给出 `dasall-cli <subcommand>` 的明确迁移提示。
2. B2 已完成：新增 `tests/integration/apps/tui/DasallCommandRoutingTest.cpp`，覆盖三条最小 routing 证据：
   - bare `dasall` 在 non-TTY 下返回 exit 1，并从 TUI startup path 输出 `Use dasall-cli for non-interactive control-plane tasks.`；
   - 旧 `dasall status` 返回 exit 1，并在 stderr 给出 `bare 'dasall status' is no longer supported. Use 'dasall-cli status'...` 的迁移错误；
   - `dasall-cli status --help` 返回 exit 0，并继续输出 `Usage: dasall-cli status`。
3. B2 同步完成 `tests/integration/apps/CMakeLists.txt` 接线：新增 `dasall_apps_command_routing_integration_test`，注入 `DASALL_TUI_BINARY_PATH`、`DASALL_CLI_BINARY_PATH` 与 `DASALL_REPOSITORY_ROOT`，并依赖 `dasall-tui` / `dasall-cli`。
4. B3 已完成：TUI 专项 TODO、子系统总账与 worklog 均已回写 034 Done 与当前命令释放链闭合结论。

## 8. Build 合规复核

| 检查项 | 结果 |
|---|---|
| 代码注释是否充分 | Pass；本轮新增的是单文件最小参数守卫与聚焦测试，控制流直接，自解释，不需要额外内联注释 |
| 是否具备正负例 | Pass；正例为 `dasall-cli status --help` 保留 structured control-plane usage，负例为 bare `dasall` non-TTY redirect 与旧 `dasall status` migration error |
| 是否验证测试发现性 | Pass；`ListTests_CMakeTools()` 已可发现 `DasallCommandRoutingTest` |
| TODO / deliverable / worklog 是否回写 | Pass；本文件、TUI 专项 TODO、子系统总账与 worklog 已同步更新 |
| 是否保持任务边界 | Pass；本轮只补 formal entrypoint guard、apps integration routing smoke 与 traceability，没有回滚 031~033 或扩张到 installed/qemu/release 证据 |

## 9. 验证

1. `Build_CMakeTools(buildTargets=["dasall-tui"])`
   - 结果：通过。
2. `Build_CMakeTools(buildTargets=["dasall_apps_command_routing_integration_test"])`
   - 结果：通过。
3. `ListTests_CMakeTools()`
   - 结果：通过；输出包含 `DasallCommandRoutingTest`。
4. `RunCtest_CMakeTools(tests=["DasallCommandRoutingTest","CliControlPlaneCommandNameTest"])`
   - 结果：工具仍返回仓库已知泛化 `生成失败`，未给出具体失败用例。
5. `./build/vscode-linux-ninja/tests/integration/apps/dasall_apps_command_routing_integration_test && ./build/vscode-linux-ninja/tests/unit/apps/cli/dasall-cli_control_plane_command_name_unit_test && echo routing-tests-ok`
   - 结果：通过；输出 `routing-tests-ok`。

## 10. 结果

1. `TUI-TODO-034` 已完成，bare `dasall` 与 `dasall-cli` 的 owner split 现在具备 apps integration 级证据。
2. bare `dasall` 现明确归 TUI owner：无参数时进入 TUI startup path，旧 structured subcommand 明确 fail-closed，并给出 `dasall-cli` 迁移提示。
3. `dasall-cli` 继续保留 structured control-plane surface；当前 TUI owner command-release 链 031~034 已全部闭合。
4. 后续若继续推进，应转 installed / qemu / release 环境复核；这已超出当前 TUI owner 原子任务范围。