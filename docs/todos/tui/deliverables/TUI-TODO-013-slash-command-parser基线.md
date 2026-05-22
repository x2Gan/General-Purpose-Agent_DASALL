# TUI-TODO-013 slash command parser 基线

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只实现 `apps/tui/src/command/TuiSlashCommandParser.h`、`apps/tui/src/command/TuiSlashCommandParser.cpp`、`tests/unit/tui/TuiSlashCommandParserTest.cpp` 与 focused CMake 注册；为承载 parser 输出，本轮允许最小化扩展 `apps/tui/src/model/TuiAction.h` 与 `apps/tui/src/model/TuiReducer.cpp` 的 request action 枚举/兜底分支。
2. 本任务不实现 `TuiComposer`、`TuiApp`、`DaemonTuiDataSource`、`TuiIpcController`、renderer、真实 session close/open/query，也不把 `/clear`、`/exit` 提前扩张成 daemon lifecycle integration。
3. 本任务完成标准是：`/help`、`/status`、`/session`、`/clear`、`/editor`、`/exit` 六个命令可二值解析；已知命令映射为本地 action 或 projection query action；未知命令与带参数命令 fail-closed 为本地错误 banner；`/clear` 不再返回 blocked 占位；focused build、single-test 与 discoverability 证据闭合。

## 2. 本地事实与证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 第 5.6 节已冻结首版最小 slash command 集合，仅允许 `/help`、`/status`、`/session`、`/clear`、`/editor`、`/exit`，并明确“不引入自由脚本命令、不绕过 access policy gate”。
2. 同一文档第 9.5.9 节已冻结 parser 职责和接口：`parse(std::string_view)`、`to_action()`、`help_entries()`，同时要求未知命令不提交 daemon、返回本地错误与建议。
3. `docs/todos/tui/deliverables/TUI-TODO-002-clear语义决策.md` 已冻结 `/clear` 为“留在当前进程、切到新的前台 session 语义、保留 input history”的 local action；`daemon close/open` 细节继续后置到 `TUI-TODO-026` / `BLK-TUI-007`。
4. `TUI-TODO-010` 已提供 `TuiAction` / `TuiReducer` typed action envelope，因此本轮可以把 slash command 收敛为显式 action，而不是回退到字符串分支或 blocked placeholder。
5. `TUI-TODO-012` 已提供 deterministic fake replay 基线，后续 `/status`、`/session` 的 projection query action 可以在不触碰 daemon/controller 的前提下复用 fake source 验证上层行为。

## 3. 外部参考

1. Aider 的 in-chat slash commands 文档把 `/help`、`/clear`、`/editor`、`/exit` 等命令收敛为显式前缀控制面，并与普通消息输入严格分离；未知功能不会隐式退化成 shell 执行。这支持本任务把 TUI slash command 设计为“单行前缀命令 -> 显式本地/查询动作”，而不是把任意 `/...` 文本当作可扩展脚本入口。
   - 参考：https://aider.chat/docs/usage/commands.html

## 4. 冻结结论

### 4.1 parser 数据形状

1. `TuiSlashCommandKind` 冻结为 `None`、`Help`、`Status`、`Session`、`Clear`、`Editor`、`Exit`、`Unknown`，只服务当前最小命令集合与 fail-closed 负例。
2. `TuiSlashCommand` 冻结三个核心事实：规范化后的 `verb`、是否为 local action、是否为 projection query；本轮不引入自由参数、shell payload 或 owner 私有 DTO。
3. `TuiSlashCommandParseResult` 冻结为 parser 输出壳层：`is_slash_command`、`accepted`、`normalized_input`、`reason_code`、`error_message`、`suggestion`，并由 `to_action()` 统一转换为 `TuiAction`。
4. `help_entries()` 冻结六条帮助元数据，并为每条命令保留 `owner_boundary`，避免帮助文案暗示 TUI 已越权拥有 daemon/runtime/profile owner 语义。

### 4.2 解析与映射语义

1. 只有“单行 + 首个非空字符为 `/`”的输入才进入 slash parser；普通文本或多行输入保持 `Noop`，不被误判为 slash command。
2. parser 会做 ASCII 小写规范化，因此 `/EDITOR` 与 `/editor` 进入同一命令分支；但首版仍不接受额外参数。
3. `to_action()` 的冻结映射如下：

| 命令 | 输出 action | 说明 |
|---|---|---|
| `/help` | `ModalShown` + `TuiModalKind::Help` | 本地 help/keymap modal，文案中保留 `/clear` 的“保留 input history”语义 |
| `/status` | `StatusQueryRequested` | projection query action；后续由 app loop / data source 消费 |
| `/session` | `SessionQueryRequested` | projection query action；后续由 app loop / data source 消费 |
| `/clear` | `ForegroundSessionClearRequested` | 明确 local action，不再返回 blocked 占位 |
| `/editor` | `ComposerModeChanged("external-editor")` | 本地切换到 external-editor composer mode |
| `/exit` | `ExitRequested` | 显式退出请求；真实 close 行为继续后置到 `TUI-TODO-026` |

4. 为承载 `/status`、`/session`、`/clear`、`/exit` 的显式 action，本轮允许在 `TuiActionType` 新增 `StatusQueryRequested`、`SessionQueryRequested`、`ForegroundSessionClearRequested`、`ExitRequested`；`TuiReducer` 对这些 request action 仅做显式 no-op 保底，等待后续 app loop / data source 处理，不在本轮偷渡 session lifecycle 语义。

### 4.3 fail-closed 语义

1. 未知命令统一返回本地 `BannerAdded` 错误 banner，`reason_code=unknown_slash_command`，并建议用户改用 `/help`；不得回落为普通消息提交，也不得隐式执行 shell。
2. 首版所有 slash command 均不接受参数；带参数输入统一返回本地 `BannerAdded` 错误 banner，`reason_code=slash_command_arguments_not_supported`。
3. 裸 `/` 统一返回本地 `BannerAdded` 错误 banner，`reason_code=empty_slash_command`。

### 4.4 focused test 策略

1. `tests/unit/tui/TuiSlashCommandParserTest.cpp` 负责守住六个已知命令映射、unknown/argument-bearing fail-closed、single-line gating、help metadata 与 parser 文件的 no-private-include boundary。
2. `tests/unit/tui/CMakeLists.txt` 必须注册 `dasall_tui_slash_command_parser_unit_test` 与 `TuiSlashCommandParserTest`，并让 discoverability 可直接命中该测试名。

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| slash parser surface | `apps/tui/src/command/TuiSlashCommandParser.h`、`apps/tui/src/command/TuiSlashCommandParser.cpp` | `TuiSlashCommandParserTest` | `ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiSlashCommandParserTest$'` |
| request action 承载 | `apps/tui/src/model/TuiAction.h`、`apps/tui/src/model/TuiReducer.cpp` | `TuiSlashCommandParserTest` | `ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiSlashCommandParserTest$'` |
| discoverability | `tests/unit/tui/CMakeLists.txt`、`tests/unit/tui/TuiSlashCommandParserTest.cpp` | `TuiSlashCommandParserTest` | `ctest --preset vscode-linux-ninja -N | rg 'TuiSlashCommandParserTest'` |

## 6. 结果

1. 本轮没有新的 blocker；`TUI-TODO-002` 已经解除了 `/clear` 的语义阻塞，`TUI-TODO-010` / `TUI-TODO-012` 已提供足够的 action/fake baseline 支撑 parser 落盘。
2. `TuiSlashCommandParser` 已把六个最小命令集合、help metadata、single-line gating、argument rejection 与本地 error banner 统一收敛到 typed parser/result/action 语义。
3. `TUI-TODO-013` 不宣称 `/status` / `/session` 已具备真实 projection 查询能力，也不宣称 `/clear` / `/exit` 已闭合 session lifecycle；这些 owner 行为继续后置到 `TUI-TODO-021~026`。

结论：TUI-TODO-013 D Gate = PASS；focused Build、single-test 与 discoverability 已闭合，可标记 Done。