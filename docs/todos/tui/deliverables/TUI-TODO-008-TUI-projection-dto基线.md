# TUI-TODO-008 TUI projection DTO 基线

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只定义 `apps/tui/src/data/TuiProjectionTypes.h` 中的 TUI module-local DTO，不实现 `ITuiDataSource`、`TuiIpcController`、`TuiReducer`、renderer 或任何 daemon/runtime 接线。
2. 本任务只冻结 DTO 的最小字段家族、supporting object、默认值策略、允许依赖与禁止依赖，并补一个 focused unit test 守住字段/边界；不把这些 DTO 上升到 `contracts`。
3. 本任务的完成标准是：DTO 头文件可被 unit test 单独编译，字段与详设/前序 seam 文档一致，且实现中不 include runtime/access/llm/provider 私有头。

## 2. 本地事实与证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 第 9.2 节已经给出 `TuiSessionView`、`TuiStatusProjection`、`TuiModelRouteProjection` 的对象草案，并明确这些对象属于 `apps/tui` module-local design surface。
2. 同一文档第 7.4 与 9.6 节已冻结投影边界：TUI 只消费受控 projection，`TuiSessionView`、`TuiTurnReceipt`、`TuiStatusProjection`、`TuiModelRouteProjection`、`TuiEventProjection` 继续保持 module-local，可跨 fake/daemon source 复用，但不因复用上升 shared contracts。
3. `docs/todos/tui/deliverables/TUI-TODO-003-daemon-projection-seam.md` 已为本任务提前冻结最小字段家族：`TuiSessionView`、`TuiTurnReceipt`、`TuiStatusProjection`、`TuiModelRouteProjection`、`TuiEventProjection`、`TuiToolSummaryView`、`TuiRouteCatalogView`，并明确 `route_catalog` 细字段仍后置到 `TUI-TODO-027`。
4. `docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md` 已把 `TUI-TODO-008` 锁定为单文件 DTO 头 + `TuiProjectionTypesTest`；其完成判定明确要求“字段与详设字段表一致，不 include runtime/access/llm/provider 私有头”。
5. `TUI-TODO-007` 已落下 non-installed `dasall_tui_prototype`，因此本任务可以把 DTO 定义成纯头文件，不需要引入额外库或 owner implementation 才能通过 focused test。

## 3. 外部参考

1. Microsoft Azure Architecture Center 的 CQRS 模式强调：presentation/read model 应按消费者需要定义独立 DTO，而不是直接暴露 domain/write model。TUI 作为 task-based UI，其 DTO 头文件应围绕 UI 消费面设计，而不是复用 runtime/access 内部对象。
   - 参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/cqrs
2. Google AIP-193 强调稳定 client contract 需要 machine-readable reason，而不是要求客户端解析自由文本 message。这支持本任务在 `TuiTurnReceipt`、`TuiEventProjection`、`TuiModelRouteProjection` 等 DTO 中显式保留 `reason_code` / `disabled_reasons` 一类稳定字段，而不是把 UI 分支绑到 message 文案上。
   - 参考：https://google.aip.dev/193

## 4. 冻结结论

### 4.1 DTO 家族与落位

1. `apps/tui/src/data/TuiProjectionTypes.h` 作为单一入口，冻结以下 TUI module-local DTO：`NextTurnPreference`、`TuiSessionView`、`TuiTurnReceipt`、`TuiStatusProjection`、`TuiModelRouteProjection`、`TuiToolSummaryView`、`TuiRouteCatalogEntry`、`TuiRouteCatalogView`、`TuiEventProjection`。
2. 这些对象仅服务 `apps/tui` 的 fake/daemon source、view model 与 reducer，不进入 `contracts`，也不复用 CLI private projection、raw daemon carrier 或 runtime internal object。
3. 本轮允许把 `route_catalog` 相关对象落成最小 supporting DTO，但只冻结 selector/fake source 当前必需字段；更细的 route allowlist/verification/health 字段扩展继续后置到 `TUI-TODO-027`。

### 4.2 依赖与字段规则

1. DTO 头文件只允许依赖 C++ 标准库头，如 `string`、`vector`、`optional`、`cstdint`；不得 include `access/`、`runtime/`、`llm/`、`profiles/`、provider 或 FTXUI 头。
2. 所有字段优先表达 user-visible summary、correlation id、reason code、timestamp、badge 和 selector affordance；禁止 raw exception、stack trace、secret、raw tool payload、完整 profile 文本、provider secret。
3. 对后续 producer 才能冻结的字段，一律保留为 `std::optional<...>` 或最小 supporting object，而不是引入 owner 私有类型占位。

### 4.3 `NextTurnPreference` 与 route DTO 规则

1. `NextTurnPreference` 作为 TUI module-local draft 先在本头文件中冻结，支持 `auto`、`prefer_depth`、`pin_model` 三种模式，并保留 `preferred_depth_tier`、`pinned_provider_id`、`pinned_model_id`、`user_visible_summary`、`source`、`applies_to_next_turn_only` 字段。
2. `TuiModelRouteProjection` 必须包含 `current_provider_id`、`current_model_id`、`current_depth_tier`、`disabled_reasons`、`next_preference`，让 fake selector 与后续 daemon 回显共享同一 UI-facing shape。
3. `TuiRouteCatalogView` 只提供当前 route 摘要、候选项列表和 disabled reason 所需最小字段；不得在本轮提前冻结完整 allowlist/profile 结构。

### 4.4 focused test 策略

1. `TuiProjectionTypesTest` 负责验证：DTO 字段族存在、默认值可判定、supporting object 关系成立，且 `apps/tui/src/data/TuiProjectionTypes.h` 未引入禁止依赖路径。
2. 本轮不做 owner 语义测试；focused test 只守住 compile-time / shape-level contract，避免在 `TUI-TODO-008` 提前越界实现 producer 行为。

## 5. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| projection DTO header | `apps/tui/src/data/TuiProjectionTypes.h` | `TuiProjectionTypesTest` | `ctest --preset vscode-linux-ninja -R "TuiProjectionTypes" --output-on-failure` |
| unit discoverability | `tests/unit/tui/CMakeLists.txt`、`tests/unit/tui/TuiProjectionTypesTest.cpp` | `TuiProjectionTypesTest` | `ctest --preset vscode-linux-ninja -N | rg "TuiProjectionTypes"` |

## 6. D Gate 结果

1. `TUI-TODO-008` 没有新的 blocker；其唯一前置 `TUI-TODO-007` 已完成，`BLK-TUI-003` / `BLK-TUI-004` 也已通过前序 deliverable 收口。
2. DTO 名称、字段家族、禁止依赖、测试目标和验收命令已经形成单一口径。
3. D Gate = PASS，可以进入最小 Build 落地：新增 `TuiProjectionTypes.h`、注册 `TuiProjectionTypesTest`，并用 focused unit test 验证 DTO shape 与 no-private-include boundary。