# TOOL-TODO-016 ToolServiceBridge 设计收敛

日期：2026-04-16  
任务：TOOL-TODO-016  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 6.2、6.2.2、6.12.2 已冻结 ToolServiceBridge 的职责边界：只负责 ToolIR 与 services facade request 的映射，不直接执行 side effect、route 决策或结果投影。
2. TOOL-TODO-011 已使 ToolValidator 能稳定产出带 `request_id`、`tool_call_id`、`tool_name`、`normalized_arguments`、`route`、`timeout_ms` 的 ToolIR，因此 016 可以直接消费 ToolIR，而不需要新增 shared contract supporting object。
3. services/include/ServiceTypes.h、IExecutionService.h、IDataService.h 已冻结 `ServiceCallContext`、`ExecutionCommandRequest`、`DataQueryRequest`、`ExecutionDiagnoseRequest` 的字段面，ToolServiceBridge 可以保持 module-local request mapper 角色。

## 2. Design 结论

1. 新增 module-local `ToolServiceBridge`，固定四个映射入口：`build_context()`、`build_action_request()`、`build_query_request()`、`build_diagnose_request()`。
2. `ServiceCallContext` 由 `ToolIR + ToolInvocationContext` 派生：优先透传 `request_id`、`tool_call_id`、`goal_id`、`session_id`、`trace_id`，缺失时按稳定规则补齐 fallback；`budget_guard` 直接复用 `RuntimePolicySnapshot.runtime_budget()`；`deadline_ms` 优先使用 `ToolIR.timeout_ms`，否则回落到 profile tool timeout，再保底到正值最小量级。
3. builtin 请求映射采用稳定最小规则：`tool_name` 直接映射为 `capability_id` / action / dataset，`target_id` 统一收敛为 `builtin:<tool_name>`，`normalized_arguments` 保持为 services request JSON 载荷，`idempotency_key` 原样透传。
4. query 路径的 freshness 直接跟随 profile stale-read 策略：当 `CapabilityCachePolicy.stale_read_allowed=true` 时输出 `allow_stale`，否则保持 `strict`。
5. ToolServiceBridge 仍是纯映射层：不调用 `IExecutionService` / `IDataService`，不构造 `ToolResult`，也不侵入 PolicyGate / RouteSelector / ResultProjector 的职责边界。

## 3. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| ToolIR -> services request mapper | tools/src/bridge/ToolServiceBridge.h、tools/src/bridge/ToolServiceBridge.cpp |
| action/query/diagnose 映射规则 | tests/unit/tools/ToolServiceBridgeTest.cpp |
| tools 内部 services 头可见性与 unit 注册 | tools/CMakeLists.txt、tests/unit/tools/CMakeLists.txt、tests/unit/CMakeLists.txt |
| 任务证据回写 | docs/todos/tools/DASALL_tools子系统专项TODO.md、docs/worklog/DASALL_开发执行记录.md |

## 4. Build 三件套

1. 代码目标：
   - tools/src/bridge/ToolServiceBridge.h
   - tools/src/bridge/ToolServiceBridge.cpp
   - tools/CMakeLists.txt
2. 测试目标：
   - tests/unit/tools/ToolServiceBridgeTest.cpp
   - full unit regression
3. 验收基线：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_tools dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 5. 本地验证

1. 定向构建：
   - Build_CMakeTools: `dasall_tool_service_bridge_unit_test`
2. 定向验证：
   - RunCtest_CMakeTools: `ToolServiceBridgeTest`
3. 聚合回归：
   - Build_CMakeTools: `dasall_tools`、`dasall_unit_tests`
   - `dasall_unit_tests` 构建期间自动执行 unit 集合，结果 `273/273 passed`
4. 结果摘要：
   - `ToolServiceBridgeTest` 通过，覆盖 action / query / diagnose 三条 request mapping 路径。
   - 全量 unit 回归通过，tools 标签下共 24 条 tests 保持全绿。
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，但未影响测试通过结论。

## 6. 风险与回退

1. 当前 action / dataset / target 的映射采用 v1 最小稳定规则，后续若 builtin descriptor 引入更细粒度 capability/target 元信息，应优先在 bridge 内收敛而不是扩 shared contracts。
2. ToolServiceBridge 目前只覆盖 request mapping；`ToolResult` 的统一错误与 side-effect 收口仍留给后续 `BuiltinExecutorLane` / `ResultProjector` 任务处理，避免在 016 提前把结果映射逻辑做重。