# TUI-TODO-012 fake data source 场景回放基线

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只实现 `apps/tui/src/data/FakeScenarioCatalog.h`、`apps/tui/src/data/FakeTuiDataSource.h`、`apps/tui/src/data/FakeTuiDataSource.cpp` 与 focused unit tests；不实现 `TuiApp`、`FakeScenarioClock`、`DaemonTuiDataSource`、`TuiIpcController`、renderer 或 app loop。
2. 本任务只允许依赖 `apps/tui/src/data/ITuiDataSource.h`、`apps/tui/src/data/TuiProjectionTypes.h` 与 C++ 标准库；不引入 network/socket/daemon/runtime/provider/FTXUI 依赖。
3. 本任务完成标准是：fake 场景列表覆盖 `golden_ready`、`planning_tools`、`needs_confirm`、`recovering`、`route_switch`、`narrow_cjk`；同一 scenario 的 `open_session()` / `submit_turn()` / `poll_events()` / `route_catalog()` / `close_session()` replay 输出一致；focused build、single-test 与 discoverability 证据闭合。

## 2. 本地事实与证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 第 9.5.3 节已冻结 prototype 阶段的数据面策略：上层只消费 `ITuiDataSource`，prototype 只能从 deterministic scenario 取数，正式实现才经 `TuiIpcController` 访问 daemon projection。
2. `docs/todos/tui/DASALL_TUI小样快速实现专项TODO-2026-05-12.md` 第 4.3 节和 `TUI-PROTO-003` / `TUI-PROTO-006` 已冻结六个 fake scenario 名称与 coverage 目标：`golden_ready`、`planning_tools`、`needs_confirm`、`recovering`、`route_switch`、`narrow_cjk`，并要求“每个场景可 deterministic replay”。
3. `TUI-TODO-011` 已提供 `ITuiDataSource` 五个 operation、独立 request/result supporting object 与 `TuiDataSourceIssue` 的 machine-readable 失败语义，因此本轮不需要重新定义 fake/daemon 各自的数据 contract。
4. `apps/tui/src/main.cpp` 与 `apps/tui/CMakeLists.txt` 仍保持 fake-only/no-daemon prototype 基线；本轮 fake source 必须守住不访问 IPC、socket、runtime 或 provider 的边界。

## 3. 外部参考

1. Microsoft 的 Architectural Principles 文档在 Separation of Concerns、Dependency Inversion 与 Explicit Dependencies 原则中强调：上层逻辑应依赖抽象而不是实现细节，可替换实现应通过显式接口与纯数据依赖暴露。这支持本任务把 fake data source 收敛为 `ITuiDataSource` 的一个纯内存实现，并把 deterministic replay 冻结在 TUI module-local DTO 与 scenario catalog 里，而不是提前绑定 daemon transport 或 runtime owner。
   - 参考：https://learn.microsoft.com/en-us/dotnet/architecture/modern-web-apps-azure/architectural-principles

## 4. 冻结结论

### 4.1 fake scenario catalog 形状

1. `FakeScenarioCatalog::scenario_ids()` 冻结六个场景 ID：`golden_ready`、`planning_tools`、`needs_confirm`、`recovering`、`route_switch`、`narrow_cjk`。
2. `FakeScenarioCatalog::load()` 返回 `FakeScenarioLoadResult`，只允许二选一结果：成功时给出 `FakeScenario`，失败时给出 machine-readable `TuiDataSourceIssue`；未知场景统一落 `reason_domain=request`、`reason_code=validation_failed`。
3. `FakeScenario` 冻结为 fake replay 的最小数据单元，至少包含：`scenario_id`、`TuiSessionView`、`TuiRouteCatalogView`、`TuiSubmitTurnResult` 与按批次组织的 `std::vector<std::vector<TuiEventProjection>> event_batches`。

### 4.2 `FakeTuiDataSource` replay 语义

1. `open_session()` 只从已加载场景复制本地 `TuiSessionView`，并重置 event batch cursor；它不访问 daemon、socket、runtime 或 provider。
2. `submit_turn()` 复用场景冻结的 `TuiSubmitTurnResult`，同时把调用方提供的 `request_id` / `trace_id` 回写到 receipt，保证同一 scenario + 同一请求输入的 replay 输出保持一致。
3. `poll_events()` 以固定顺序回放 `event_batches`，并在 cursor 缺失或不匹配时返回 machine-readable `validation_failed`；session 不匹配时返回 `session_not_found`。
4. `route_catalog()` 只回放场景内置 `TuiRouteCatalogView`；`close_session()` 只重置 fake source 的本地 session state，不触发任何 transport 或外部副作用。

### 4.3 六个场景覆盖面

1. `golden_ready`：标准空闲主界面，覆盖 ready session、profile、当前 route 与 idle receipt。
2. `planning_tools`：执行中状态，覆盖 planning -> tool_calling 的双批次 timeline、budget 变化、tool summary 与 busy draft banner。
3. `needs_confirm`：等待用户交互，覆盖 `pending_interaction=confirm_external_tool` 与 confirmation banner。
4. `recovering`：恢复摘要展示，覆盖 recovery summary、degraded health 与 recovery-oriented tool summary。
5. `route_switch`：LLM selector 展开与切换，覆盖 `PreferDepth` next preference、多候选 route 与 disabled reason。
6. `narrow_cjk`：80x24 + 中文压力，覆盖中文 receipt/tool summary/banner 文案与 narrow startup mode。

### 4.4 focused test 策略

1. `tests/unit/tui/TuiFakeScenarioCatalogTest.cpp` 负责验证六个场景完整性、重复 `load()` 的 deterministic 输出，以及 catalog 头文件不引入 owner 私有依赖或 transport 调用。
2. `tests/unit/tui/FakeTuiDataSourceTest.cpp` 负责验证 `planning_tools` 场景的 deterministic open/submit/poll 序列、machine-readable 错误语义，以及 fake source 头/实现不触碰 socket/IPC/remote endpoint。
3. `tests/unit/tui/CMakeLists.txt` 必须把 `TuiFakeScenarioCatalogTest` 与 `TuiFakeDataSourceTest` 注册进 focused build/test/discoverability 路径，避免后续 `TUI-TODO-013~020` 重复补 test entry。

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| fake scenario catalog | `apps/tui/src/data/FakeScenarioCatalog.h` | `TuiFakeScenarioCatalogTest` | `ctest --preset vscode-linux-ninja -R '^TuiFakeScenarioCatalogTest$' --output-on-failure` |
| fake data source replay | `apps/tui/src/data/FakeTuiDataSource.h`、`apps/tui/src/data/FakeTuiDataSource.cpp` | `TuiFakeDataSourceTest` | `ctest --preset vscode-linux-ninja -R '^TuiFakeDataSourceTest$' --output-on-failure` |
| test registration / discoverability | `tests/unit/tui/CMakeLists.txt`、`tests/unit/tui/TuiFakeScenarioCatalogTest.cpp`、`tests/unit/tui/FakeTuiDataSourceTest.cpp` | `TuiFakeScenarioCatalogTest`、`TuiFakeDataSourceTest` | `ctest --preset vscode-linux-ninja -N | rg 'TuiFake(ScenarioCatalog|DataSource)Test'` |

## 6. 结果

1. `TUI-TODO-012` 没有新的 blocker；`ITuiDataSource` seam、projection DTO、typed reducer 和 prototype target 已足够承载一个纯内存 deterministic fake replay 层。
2. 本轮已把六个场景、`FakeScenarioLoadResult` 的 machine-readable 失败 contract、`FakeTuiDataSource` 的 session/cursor/replay 语义，以及 focused tests 一并落盘到代码与 CMake。
3. 后续 `TUI-TODO-013~020` 可以直接复用 fake source 作为 composer、selector、status panel、transcript 与 app loop 的统一 prototype 数据入口；正式 daemon attach 继续后置到 `TUI-TODO-021~023`。

结论：TUI-TODO-012 D Gate = PASS；focused Build 与单测验证已闭合，可标记 Done。