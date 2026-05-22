# TUI-TODO-010 reducer 状态迁移基线

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只实现 `apps/tui/src/model/TuiReducer.h` 与 `apps/tui/src/model/TuiReducer.cpp` 中的纯 reducer 迁移逻辑，并补 `TuiReducerTransitionTest`；不实现 data source、slash command、composer 键位、selector、renderer 或 app loop。
2. 本任务只消费 `TuiAction.h` 与 `TuiScreenModel.h` 已冻结的 typed action/model 基线；除非 reducer 无法在当前 action 表面表达，否则不扩 action taxonomy。
3. 本任务完成标准是：`reduce(TuiScreenModel current, TuiAction action)` 可被 unit test 单独编译，能覆盖 submit、append event、focus switch、banner、unknown action no-op/fail-closed 路径，且实现中不引入 FTXUI、I/O、system time 或 owner 私有头。

## 2. 本地事实与证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 第 9.5.2 节已冻结 reducer 边界：`TuiReducer` 只响应 `TuiAction`、更新 `TuiScreenModel`；不包含 FTXUI 类型，不做 I/O，不调用 daemon，不读取系统时间。
2. 同一文档已明确失败语义：未知 action 保持 no-op 并记录 `debug_reason`；非法状态转换必须 fail-closed 到 banner/modal。
3. `docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md` 已把 `TUI-TODO-010` 锁定为 `apps/tui/src/model/TuiReducer.h`、`apps/tui/src/model/TuiReducer.cpp` 与 `TuiReducerTransitionTest`，完成判定明确要求覆盖 submit、append event、focus switch、banner、unknown action no-op/fail-closed 路径。
4. `TUI-TODO-009` 已落下 `TuiAction.h` 与 `TuiScreenModel.h`，当前 action 表面已经包含 `FocusChanged`、`BannerAdded`、`BannerCleared`、`ModalShown`、`ModalHidden`、`SessionHydrated`、`StatusUpdated`、`RouteUpdated`、`EventAppended`、`ComposerTextChanged`、`ComposerModeChanged`、`ComposerSubmitAvailabilityChanged`。
5. 详设 5.5 的 composer 状态枚举已明确包含 `submitting`，因此本轮可以把“submit 路径”收敛为 reducer 对 `ComposerModeChanged("submitting")` 的状态迁移，而不必提前为 014 新增独立 dispatcher/callback 机制。

## 3. 外部参考

1. Elm Architecture 强调交互式程序的 `Model`、`View`、`Update` 分离；`Update` 只基于消息纯函数地生成新状态，而不直接执行副作用。这支持本任务把 reducer 实现为纯 `reduce()`，并把实际 I/O 与 event source 继续留给后续 app/data source/composer 任务。
   - 参考：https://guide.elm-lang.org/architecture/

## 4. 冻结结论

### 4.1 reducer 接口与依赖边界

1. `apps/tui/src/model/TuiReducer.h` 只声明 `TuiScreenModel reduce(TuiScreenModel current, TuiAction action);`，不暴露 renderer helper、dispatcher、callback registry 或 side-effect seam。
2. `apps/tui/src/model/TuiReducer.cpp` 只允许依赖 C++ 标准库、`TuiAction.h`、`TuiScreenModel.h` 与 `TuiProjectionTypes.h`；不得 include `access/`、`runtime/`、`llm/`、`profiles/`、provider、FTXUI、terminal 句柄或 I/O 头。

### 4.2 action -> transition 基线

1. `FocusChanged`：存在 payload 且目标焦点合法时切换 focus；若请求 `Modal` 但当前 `modal.kind == None`，则保持原 focus，并以 error banner + `debug_reason` fail-closed。
2. `BannerAdded` / `BannerCleared`：新增 banner 或清空 banner 列表；banner 继续是纯 UI supporting object，不触发任何副作用。
3. `ModalShown` / `ModalHidden`：show 时写入 modal 并切到 `Modal` focus；hide 时清空 modal，若当前 focus 为 `Modal` 则回退到 `Composer`。
4. `SessionHydrated` / `StatusUpdated` / `RouteUpdated`：直接替换 screen model 中对应 projection slice，不做 owner 语义推导。
5. `EventAppended`：将 `TuiEventProjection` 映射成最小 `TuiMessageView` 并 append 到 transcript；若 `status_delta` 存在则同步刷新 `model.status`；若 `banner_reason` 存在则附加 warning banner。
6. `ComposerTextChanged` / `ComposerModeChanged` / `ComposerSubmitAvailabilityChanged`：只更新 composer state；其中 `ComposerModeChanged("submitting")` 视为本轮 submit 路径，需收敛到 `mode=submitting`、`can_submit=false`、`focus=Transcript`、`dirty=false`。
7. `Noop` 或未知 action：保持其余 model slice 不变，仅记录 `debug_reason`；未知 enum 值不允许崩溃或引入副作用。

### 4.3 fail-closed 规则

1. reducer 不抛异常来表达用户态非法状态；所有 fail-closed 结果都体现在新的 `TuiScreenModel` 上，至少包含 `debug_reason`，并优先附加 user-visible banner。
2. 本轮最小 fail-closed 场景固定为：`FocusChanged` 请求 `Modal` 但当前没有 active modal；测试通过即可，不提前为 future slash/daemon 语义扩展更多非法转换矩阵。

### 4.4 focused test 策略

1. `TuiReducerTransitionTest` 至少覆盖 5 条路径：submit、append event、focus switch、banner add/clear、unknown action no-op/fail-closed。
2. `TuiReducerTransitionTest` 继续是纯函数单测：不访问文件、网络、socket、系统时间或终端；只构造 `TuiScreenModel` + `TuiAction`，再断言 `reduce()` 返回结果。

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| reducer 接口与实现 | `apps/tui/src/model/TuiReducer.h`、`apps/tui/src/model/TuiReducer.cpp` | `TuiReducerTransitionTest` | `ctest --preset vscode-linux-ninja -R "TuiReducer" --output-on-failure` |
| unit discoverability | `tests/unit/tui/CMakeLists.txt`、`tests/unit/tui/TuiReducerTransitionTest.cpp` | `TuiReducerTransitionTest` | `ctest --preset vscode-linux-ninja -N | rg "TuiReducerTransitionTest"` |

## 6. D Gate 结果

1. `TUI-TODO-010` 没有新的 blocker；其唯一前置 `TUI-TODO-009` 已完成，screen model/action 基线可直接作为 reducer 输入。
2. reducer 接口、action->transition 语义、fail-closed 规则、测试目标和验收命令已经形成单一口径。
3. D Gate = PASS，可以进入最小 Build 落地：新增 `TuiReducer.h`、`TuiReducer.cpp`、`TuiReducerTransitionTest.cpp`，并注册真实 focused reducer test target。