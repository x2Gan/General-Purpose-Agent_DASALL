# TUI-TODO-011 ITuiDataSource 接口基线

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只定义 `apps/tui/src/data/ITuiDataSource.h` 的 seam 接口、显式 request/result supporting object 与 contract-like 单测；不实现 `FakeTuiDataSource`、`DaemonTuiDataSource`、`TuiIpcController`、terminal probe 或 app loop。
2. 本任务只允许依赖 `apps/tui/src/data/TuiProjectionTypes.h` 与 C++ 标准库；不把 `TuiIpcController`、`AgentRequest`、`RuntimeDispatchRequest`、FTXUI、daemon/access/runtime 私有实现带进 data source 头文件。
3. 本任务完成标准是：`ITuiDataSource` 明确暴露 `open_session()`、`submit_turn()`、`poll_events()`、`route_catalog()`、`close_session()` 五个 operation；fake/daemon 实现可以共享同一组 request/result 语义；`ITuiDataSourceContractTest` 通过 focused build、single-test 与 discoverability 验证。

## 2. 本地事实与证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 第 9.5.3 节已冻结 `ITuiDataSource` 的职责、五个方法名和 fake/daemon 双实现方向：它只为 UI 提供 session、receipt、event、route projection 的统一来源，不拥有 UI 状态，不绕过 access policy。
2. 同一文档第 9.5.4、9.6 节与 `TUI-TODO-003` deliverable 已冻结 daemon-backed seam 的稳定 reason code 口径：`permission_denied`、`daemon_unavailable`、`timeout`、`schema_mismatch`、`malformed_response` 等必须保持 machine-readable，不允许依赖自由文本 message 猜测。
3. `TUI-TODO-008` 已落 `apps/tui/src/data/TuiProjectionTypes.h`，其中 `TuiSessionView`、`TuiTurnReceipt`、`TuiEventProjection`、`TuiRouteCatalogView` 与 `NextTurnPreference` 已可直接作为 interface payload；本轮不需要提前接入 daemon/runtime owner DTO。
4. `docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md` 已把 `TUI-TODO-011` 锁定为单个接口头 + contract-like 单测；前置只有 `TUI-TODO-008`，当前无额外 blocker。

## 3. 外部参考

1. Microsoft 的 Architectural Principles 文档在 Separation of Concerns、Dependency Inversion 与 Explicit Dependencies 原则中强调：上层逻辑应依赖抽象而不是实现细节，协作者需要通过显式接口暴露，从而允许替换实现并提升测试性。这支持本任务把 fake/daemon 数据面统一收敛到 `ITuiDataSource`，并把 request/result 事实显式建模，而不是让 `TuiApp` 直接绑定 daemon transport 或 future fake catalog 细节。
   - 参考：https://learn.microsoft.com/en-us/dotnet/architecture/modern-web-apps-azure/architectural-principles

## 4. 冻结结论

### 4.1 接口与 supporting object 形状

1. `ITuiDataSource` 维持纯虚接口，只暴露五个 operation：`open_session()`、`submit_turn()`、`poll_events()`、`route_catalog()`、`close_session()`。
2. 每个 operation 都使用独立 request/result supporting object，而不是裸参数列表或共享的变长 map：
   - `TuiOpenSessionRequest` / `TuiOpenSessionResult`
   - `TuiSubmitTurnRequest` / `TuiSubmitTurnResult`
   - `TuiPollEventsRequest` / `TuiPollEventsResult`
   - `TuiRouteCatalogRequest` / `TuiRouteCatalogResult`
   - `TuiCloseSessionRequest` / `TuiCloseSessionResult`
3. request object 负责显式暴露 `request_id`、`trace_id`、`session_id`、`user_input`、`NextTurnPreference`、`event_cursor`、`close_reason` 等调用事实，避免 future fake/daemon source 偷依赖隐式全局状态。

### 4.2 稳定失败语义

1. `TuiDataSourceIssue` 冻结为 data source seam 的最小错误 supporting object，至少包含 `reason_domain`、`reason_code`、`message`、`retryable`、`error_ref` 与公开 `metadata`。
2. data source failure 不能只有自由文本 message；`reason_domain` + `reason_code` 必须成对出现，才能被视为一致的 machine-readable issue。
3. 除 `poll_events()` 的成功空批次外，所有 result object 都必须拒绝“成功 payload 与 issue 同时存在”的歧义状态；`poll_events()` 的失败也不得同时携带 partial events 或 next cursor。
4. `close_session()` 的 v1 最小 contract 保持 `closed` + `issue` 二元语义，既允许 future daemon source 显式表达 `close_unavailable`，也不提前承诺 runtime close seam 已就绪。

### 4.3 分层与依赖边界

1. `apps/tui/src/data/ITuiDataSource.h` 只 include `data/TuiProjectionTypes.h` 与标准库头，不 include `access/`、`runtime/`、`llm/`、`profiles/`、`platform/`、FTXUI 或 `TuiIpcController`。
2. `ITuiDataSource` 不绑定 `AgentRequest`、`RuntimeDispatchRequest`、CLI projection 或 raw daemon carrier；daemon-specific mapping 继续后置到 `TUI-TODO-021~023`。
3. fake source 与 daemon source 共享 request/result/issue contract，但不共享 transport、socket policy、serialization 或 CLI envelope 细节。

### 4.4 focused test 策略

1. `tests/unit/tui/TuiDataSourceContractTest.cpp` 使用 test-local `StubTuiDataSource` 实现接口，验证五个 operation 可被 fake/daemon 统一满足。
2. focused test 至少覆盖：
   - 五个 operation 的 request/result 形状与显式字段透传
   - `TuiDataSourceIssue` 的 machine-readable 失败语义
   - payload-plus-error 歧义状态的拒绝规则
   - 头文件不引入 owner 私有依赖或 renderer/IPC 实现细节

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| data source seam 头文件 | `apps/tui/src/data/ITuiDataSource.h` | `ITuiDataSourceContractTest` | `ctest --preset vscode-linux-ninja -R "ITuiDataSource" --output-on-failure` |
| test registration / discoverability | `tests/unit/tui/CMakeLists.txt`、`tests/unit/tui/TuiDataSourceContractTest.cpp` | `ITuiDataSourceContractTest` | `ctest --preset vscode-linux-ninja -N | rg "ITuiDataSourceContractTest"` |

## 6. 结果

1. `TUI-TODO-011` 没有新的 blocker；`TUI-TODO-008` 已提供稳定 DTO，`TUI-TODO-003` 已提供 stable reason code 口径，因此接口头可以直接 Build。
2. 本轮已把五个 operation 的 request/result shape、machine-readable issue contract 与 no-private-include 边界冻结到代码和 focused test 上，足以作为 `TUI-TODO-012` fake source 与 `TUI-TODO-021~023` daemon source/controller 的共同前置。
3. 后续 `TUI-TODO-012` 只能在本接口上补 deterministic fake 行为，不能回头把 fake-only 字段、daemon transport 细节或 raw owner DTO 塞回 `ITuiDataSource`。

结论：TUI-TODO-011 D Gate = PASS；focused Build 与单测验证已闭合，可标记 Done。