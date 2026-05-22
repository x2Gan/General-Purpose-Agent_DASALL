# TUI-TODO-014 composer 状态机基线

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只实现 `apps/tui/src/view/TuiComposer.h`、`apps/tui/src/view/TuiComposer.cpp`、`apps/tui/src/view/TuiInputHistory.h`、`tests/unit/tui/TuiComposerTest.cpp`、`tests/unit/tui/TuiComposerHistoryTest.cpp` 与 focused CMake 注册；不要求本轮接入 renderer、`TuiApp`、`DaemonTuiDataSource` 或真实 external editor I/O。
2. 本任务完成标准是：`ready`、`editing`、`history-recall`、`reverse-search`、`external-editor`、`submitting`、`pending-interaction` 七个 composer 状态具备局部状态机；`Enter`、`Alt+Enter`、`Ctrl+J`、`Up`、`Down`、`Ctrl+R` 与 busy draft 行为可二值断言；focused build、single-test 与 discoverability 证据闭合。
3. `BLK-TUI-006` 继续保留在 IME/CJK/resize 的人工样品 gate；本任务只负责“自动测试先覆盖状态机”，不越权宣称终端样品已经通过。

## 2. 本地事实与证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 第 5.5 节已冻结 composer 的产品语义：`Enter` 发送、`Alt+Enter` / `Ctrl+J` 换行、`Up` / `Down` 边界 recall、`Ctrl+R` 反向搜索、`/editor` 外部编辑器与 assistant-busy draft。
2. 同一文档第 9.5.5 节已冻结 `TuiComposer` 的 owner 边界：只负责多行输入、历史、反向搜索、外部编辑器与 busy draft；不负责 daemon submit、不负责全局快捷键、不持久化长期历史。
3. `TUI-TODO-010` 已提供 `TuiComposerState` 与 reducer/action baseline，本轮可直接复用现有 `text`、`mode`、`history_query`、`can_submit`、`dirty` 五个字段，而不必引入 FTXUI 或 owner 私有 DTO。
4. `TUI-TODO-012` 与 `TUI-TODO-013` 已分别提供 deterministic fake replay 与最小 slash parser 基线，因此本轮 composer 只需输出局部 `SubmitRequested` / `OpenExternalEditorRequested` 意图，不需要提前接入 daemon 或字符串分支。

## 3. 外部参考

1. prompt_toolkit 的 prompt 文档把 multiline input、history、reverse search 与 external-editor 风格的终端交互分层到独立输入组件中；本任务只借鉴其“多行输入 + 历史/反向搜索 + 编辑器降级”的成熟终端交互语义，不引入其实现依赖。
   - 参考：https://python-prompt-toolkit.readthedocs.io/en/stable/pages/asking_for_input.html

## 4. 冻结结论

### 4.1 数据形状

1. `TuiComposerKey` / `TuiComposerKeyEvent` 冻结为最小键盘输入壳层：`TextChanged`、`Enter`、`AltEnter`、`CtrlJ`、`Up`、`Down`、`CtrlR`，并允许 caller 显式传入 `cursor_at_boundary` 与 `draft_unmodified`，避免本轮过早把终端光标细节下沉到 model。
2. `TuiComposerActionType` / `TuiComposerAction` / `TuiComposerUpdate` 冻结为 composer-local side effect 语义：本轮只允许 `SubmitRequested` 与 `OpenExternalEditorRequested` 两个显式结果，不引入 daemon request、shell payload 或 owner 私有 carrier。
3. `TuiInputHistory` 冻结为纯内存输入历史 supporting object：忽略空白草稿、提供 `older()` / `newer()` 边界 recall 与 `latest_match()` 反向搜索，不持久化磁盘历史，不接触 transport。
4. `TuiComposer` 继续直接复用 `model::TuiComposerState`，保持 `apps/tui/src/view/` 仅依赖 TUI model 与标准库，不引入 FTXUI、`access/`、`runtime/`、`llm/`、`profiles/` 私有依赖。

### 4.2 状态迁移语义

1. `TextChanged` 会替换当前草稿，并清除 history recall / reverse-search 的游标状态；在非 busy 时进入 `editing`，在 busy 时保持 `pending-interaction`。
2. `Enter` 仅在当前草稿存在可见内容且 `can_submit=true` 时生效：记录输入历史、发出 `SubmitRequested`、清空本地草稿、进入 `submitting`，并关闭重复发送。
3. `Alt+Enter` 与 `Ctrl+J` 只追加换行，不触发 submit；这两条路径必须对 slash command 与普通消息一视同仁，因为 parser 仍由后续 app loop 决定是否消费。
4. `Up` 仅在输入为空，或 caller 显式声明“光标在 recall 边界且草稿未改动”时进入 `history-recall`；`Down` 只在 `history-recall` 模式下继续向新条目移动，越过最新条目后恢复 recall seed draft。
5. `Ctrl+R` 会把当前 draft 作为 `history_query` 进入 `reverse-search`，并按匹配结果向更旧的 prompt 循环；普通编辑会清空 `history_query` 并回到 `editing` / `pending-interaction`。
6. `set_busy(true)` 会把 composer 切到 `pending-interaction` 并禁止 submit，但允许继续编辑新草稿；`set_busy(false)` 则按当前 draft 恢复到 `ready` 或 `editing`。
7. `open_external_editor()` 只发出显式 `OpenExternalEditorRequested` 意图并进入 `external-editor`；`apply_external_editor_result(nullopt)` 必须恢复原草稿，`apply_external_editor_result(text)` 才替换本地 draft。

### 4.3 fail-closed 语义

1. `submitting` 与 `external-editor` 状态下不接受会污染当前局部语义的键盘动作；`Enter` 对空白输入始终 no-op。
2. 外部编辑器取消或失败时，原草稿必须保持不变，不允许吞掉用户输入。
3. `BLK-TUI-006` 继续保留为人工 gate：本轮不承诺 IME、CJK、resize、真实终端外部编辑器往返都已达标，只承诺状态机与本地失败语义已自动化覆盖。

### 4.4 focused test 策略

1. `tests/unit/tui/TuiComposerTest.cpp` 守住多行输入、submit、busy draft、external-editor request/cancel/success 与 production-file no-private-include boundary。
2. `tests/unit/tui/TuiComposerHistoryTest.cpp` 守住 `TuiInputHistory` 的 non-blank 记录、history recall 边界、seed draft 恢复、reverse-search 循环，以及 input history header 的 no-private-include boundary。
3. `tests/unit/tui/CMakeLists.txt` 必须把占位的 `TuiComposerTest` 切换为真实 `dasall_tui_composer_unit_test`，并新增 `dasall_tui_composer_history_unit_test` / `TuiComposerHistoryTest`，保持 discoverability 与 unit 标签闭合。

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| composer state machine | `apps/tui/src/view/TuiComposer.h`、`apps/tui/src/view/TuiComposer.cpp` | `TuiComposerTest` | `ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiComposerTest$'` |
| input history + reverse search | `apps/tui/src/view/TuiInputHistory.h`、`apps/tui/src/view/TuiComposer.cpp` | `TuiComposerHistoryTest` | `ctest --preset vscode-linux-ninja --output-on-failure -R '^TuiComposerHistoryTest$'` |
| focused registration | `tests/unit/tui/CMakeLists.txt` | `TuiComposerTest`、`TuiComposerHistoryTest` | `ctest --preset vscode-linux-ninja --output-on-failure -R '^(TuiComposerTest|TuiComposerHistoryTest)$'` |

## 6. 结果

1. 本轮没有新增 owner blocker；`BLK-TUI-006` 被明确留在 IME/CJK/resize 人工 gate，不阻断先落本地 composer 状态机。
2. `apps/tui/src/view/TuiComposer.h/.cpp` 与 `TuiInputHistory.h` 已把多行输入、submit、busy draft、history recall、reverse-search 与 external-editor 降级路径收敛为可编译、可发现、无 owner 私有依赖的局部状态机。
3. `cmake --build --preset vscode-linux-ninja --target dasall_tui_composer_unit_test dasall_tui_composer_history_unit_test` 与 `ctest --preset vscode-linux-ninja --output-on-failure -R '^(TuiComposerTest|TuiComposerHistoryTest)$'` 均已通过。
4. 本轮不宣称 renderer、`TuiApp`、真实 external editor I/O、IME/CJK 终端样品或 daemon submit 已完成；这些继续后置到 `TUI-TODO-015~020` 与 `BLK-TUI-006`。

结论：TUI-TODO-014 D Gate = PASS；composer 状态机的 focused Build、single-test 与 discoverability 已闭合，可标记 Done。