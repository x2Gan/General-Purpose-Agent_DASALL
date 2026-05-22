# TUI-TODO-009 screen model 与 action 基线

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只定义 `apps/tui/src/model/TuiAction.h` 与 `apps/tui/src/model/TuiScreenModel.h` 中的 MVU model/action 基线，不实现 reducer、data source、slash command、composer 行为或 renderer。
2. 本任务只冻结 screen model 的最小字段族、supporting object、默认值策略、允许依赖与禁止依赖，并补一个 focused unit test 守住默认 shape 与 no-FTXUI/no-I/O 边界。
3. 本任务完成标准是：screen model 与 action 头文件可被 unit test 单独编译，字段与详设 9.2/9.5.2 一致，且实现中不引入 FTXUI、I/O、系统时间或 owner 私有头。

## 2. 本地事实与证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 第 9.2 节已经给出 `TuiScreenModel` 草案，确认该对象消费 `TuiSessionView`、`TuiStatusProjection`、`TuiModelRouteProjection` 与 `TuiComposerState`，属于 `apps/tui` 的 module-local view model。
2. 同一文档第 9.5.2 节已冻结 `TuiScreenModel` / `TuiReducer` 的职责边界：model/action 必须保持纯数据，不包含 FTXUI 类型、不做 I/O、不调用 daemon、不读取系统时间；未知 action 的 no-op/fail-closed 语义留给后续 reducer 实现。
3. `docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md` 已把 `TUI-TODO-009` 锁定为 `apps/tui/src/model/TuiAction.h`、`apps/tui/src/model/TuiScreenModel.h` 与 `TuiScreenModelTest`，其完成判定明确要求“model 不包含 FTXUI 类型、不做 I/O、不读取系统时间”。
4. `TUI-TODO-008` 已落下 `apps/tui/src/data/TuiProjectionTypes.h` 与 `TuiProjectionTypesTest`，因此本任务可以直接复用 module-local DTO 作为 screen model 的数据表面，而不需要提前接入 daemon/runtime 实现。
5. `tests/unit/tui/CMakeLists.txt` 已具备稳定 discoverability 入口；本任务只需追加一个 focused unit target 即可进入现有 `ctest` 拓扑。

## 3. 外部参考

1. Elm Guide 对 The Elm Architecture 的总结强调：交互式程序应拆成 `Model`、`View`、`Update` 三部分，`Model` 表示状态、`Update` 负责基于消息更新状态。这支持本任务将 `TuiAction` 与 `TuiScreenModel` 明确拆分，并把 reducer 留在后续 `TUI-TODO-010` 单独实现，而不是让 renderer 或 data source 直接改 model。
   - 参考：https://guide.elm-lang.org/architecture/

## 4. 冻结结论

### 4.1 文件与对象落位

1. `apps/tui/src/model/TuiAction.h` 作为 action/focus/banner/modal 的单一入口，冻结 `TuiAction`、`TuiActionType`、`TuiFocusState`、`TuiBanner`、`TuiModalState` 及其 supporting enum。
2. `apps/tui/src/model/TuiScreenModel.h` 作为 screen model 的单一入口，冻结 `TuiMessageView`、`TuiComposerState`、`TuiScreenModel`。
3. 这些对象只服务 `apps/tui` 的 reducer、view、app loop 和 fake/daemon source 组合，不进入 `contracts`，也不复用 CLI private object、daemon raw carrier 或 runtime internal state。

### 4.2 依赖与字段规则

1. 头文件只允许依赖 C++ 标准库和 `apps/tui/src/data/TuiProjectionTypes.h`；不得 include `access/`、`runtime/`、`llm/`、`profiles/`、provider、FTXUI 头。
2. model/action 只表达 UI 状态与事件，不做 I/O，不读取系统时间，不持有 socket、filesystem path、terminal handle、thread、promise 或 renderer object。
3. `TuiScreenModel` 对投影对象的引用全部使用 TUI module-local DTO；不得把 `AgentRequest`、`RuntimeDispatchRequest`、CLI JSON envelope 或 raw daemon response 直接放进 model。

### 4.3 `TuiScreenModel` 规则

1. `TuiScreenModel` 继续消费 `TuiSessionView`、`TuiStatusProjection`、`TuiModelRouteProjection` 与 `TuiComposerState`，并补齐 typed `TuiFocusState`、`TuiBanner`、`TuiModalState`，不再使用裸 `std::string focused_region` / `std::vector<std::string> banners` 作为长期基线。
2. `TuiMessageView` 继续作为 transcript 渲染前的 module-local view data，最小字段保留 `role`、`content`、`timestamp`、`badges`、`collapsible`、`collapsed`。
3. `TuiComposerState` 只保留 draft text、mode、history query、submit 可用性与 dirty bit；复杂键位和 IME 语义后置到 `TUI-TODO-014`。

### 4.4 `TuiAction` 规则

1. `TuiAction` 必须是纯数据 envelope，包含 `TuiActionType` 和后续 reducer 所需的最小 optional payload，不直接绑定 renderer event 或 daemon callback 类型。
2. 首轮冻结的 action 家族覆盖：focus 切换、banner raise/clear、modal show/hide、session/status/route hydrate、event append、composer text/mode/submit-availability 更新。
3. 本轮不引入 reducer helper、dispatcher、callback registry；未知 action 的 no-op/fail-closed 语义由 `TUI-TODO-010` 的 reducer 测试负责收口。

### 4.5 focused test 策略

1. `TuiScreenModelTest` 负责验证：action/model 默认 shape、typed focus/banner/modal 默认值、`TuiScreenModel` 与 `TuiProjectionTypes` 的组合关系成立，以及两个头文件文本不引入 FTXUI/I/O/system-time/private-owner 依赖。
2. 本轮不做 reducer 迁移测试；focused test 只守住 compile-time / shape-level contract，避免在 `TUI-TODO-009` 提前越界实现状态流转。

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| screen model 与 action 头文件 | `apps/tui/src/model/TuiAction.h`、`apps/tui/src/model/TuiScreenModel.h` | `TuiScreenModelTest` | `ctest --preset vscode-linux-ninja -R "TuiScreenModel" --output-on-failure` |
| unit discoverability | `tests/unit/tui/CMakeLists.txt`、`tests/unit/tui/TuiScreenModelTest.cpp` | `TuiScreenModelTest` | `ctest --preset vscode-linux-ninja -N | rg "TuiScreenModelTest"` |

## 6. D Gate 结果

1. `TUI-TODO-009` 没有新的 blocker；其唯一前置 `TUI-TODO-008` 已完成，`apps/tui/src/data/TuiProjectionTypes.h` 可直接作为 model/action 的 DTO 依赖。
2. screen model、action 家族、禁止依赖、测试目标和验收命令已经形成单一口径。
3. D Gate = PASS，可以进入最小 Build 落地：新增 `TuiAction.h`、`TuiScreenModel.h`、`TuiScreenModelTest.cpp`，并注册 focused unit test。