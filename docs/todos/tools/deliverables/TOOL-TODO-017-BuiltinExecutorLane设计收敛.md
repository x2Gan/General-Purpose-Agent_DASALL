# TOOL-TODO-017 BuiltinExecutorLane 设计收敛

日期：2026-04-16  
任务：TOOL-TODO-017  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 6.12.2 已固定 BuiltinExecutorLane 的职责：承接 builtin route、本地调用 `IExecutionService` / `IDataService`、把 services 回执收敛为统一 `ToolResult`，但不重解释 policy、route 或 digest。
2. TOOL-TODO-016 已提供 `ToolServiceBridge`，可以稳定把 `ToolIR + ToolInvocationContext` 映射为 action / query / diagnose 三类 services request，因此 017 的唯一主目标是 lane 分发与 `ToolResult` 收口。
3. TOOL-TODO-015b 已把 ToolManager 主链接通，但默认 executor 仍是 module-local fallback；017 可以在不触碰 ResultProjector 的前提下，把 builtin route 默认执行替换成真实 lane。

## 2. Design 结论

1. 新增 module-local `BuiltinExecutorLane`，内部依赖 `ToolRegistry`、`ToolServiceBridge`、`IExecutionService`、`IDataService` 与时钟函数，暴露 `execute()`、`dispatch_action()`、`dispatch_query()`、`dispatch_diagnose()`、`map_service_result()` 最小面。
2. lane 在执行入口重新基于 `tool_name` 解析 descriptor category，并按 `Action -> execute`、`Information -> query`、`Diagnostic -> diagnose` 分发到 services facade；`Workflow` / `AgentDelegation` 在 builtin lane 内明确 fail-closed。
3. services 回执到 `ToolResult` 的映射采用 v1 最小规则：成功语义由 `error` 是否为空决定，`payload_json / rows_json / report_json` 分别进入 `ToolResult.payload`，`side_effects` 在 action 错误路径下原样保留，query cache hit 通过 tags 标注，不在 lane 内伪造 compensation plan。
4. ToolManager 的默认 executor 现已对 builtin route 接到 `BuiltinExecutorLane`；workflow / MCP 仍保留既有 fallback executor，避免 017 越权扩展到其它执行域。
5. lane 默认依赖提供了最小 loopback services stub，使默认 ToolManager 在 builtin 路径上不再依赖 015b 阶段的字符串 fallback executor；真正的 digest/projection 替换仍留给 018。

## 3. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| builtin lane 本体 | tools/src/execution/BuiltinExecutorLane.h、tools/src/execution/BuiltinExecutorLane.cpp |
| ToolManager builtin 默认接线 | tools/src/ToolManager.cpp |
| action / query / diagnose / partial side effect 行为测试 | tests/unit/tools/BuiltinExecutorLaneTest.cpp |
| timeout 错误收口测试 | tests/unit/tools/BuiltinExecutorLaneTimeoutTest.cpp |
| unit 注册与证据回写 | tools/CMakeLists.txt、tests/unit/tools/CMakeLists.txt、tests/unit/CMakeLists.txt、docs/todos/tools/DASALL_tools子系统专项TODO.md、docs/worklog/DASALL_开发执行记录.md |

## 4. Build 三件套

1. 代码目标：
   - tools/src/execution/BuiltinExecutorLane.h
   - tools/src/execution/BuiltinExecutorLane.cpp
   - tools/src/ToolManager.cpp
2. 测试目标：
   - `BuiltinExecutorLaneTest`
   - `BuiltinExecutorLaneTimeoutTest`
   - `ToolManagerSkeletonTest`
   - `ToolManagerBatchInvokeTest`
   - `ToolManagerPipelineTest`
   - `ToolManagerFailurePathTest`
   - full unit regression
3. 验收基线：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_tools dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 5. 本地验证

1. 定向构建：
   - Build_CMakeTools: `dasall_builtin_executor_lane_unit_test`、`dasall_builtin_executor_lane_timeout_unit_test`
   - Build_CMakeTools: `dasall_tool_manager_skeleton_unit_test`、`dasall_tool_manager_batch_invoke_unit_test`、`dasall_tool_manager_pipeline_unit_test`、`dasall_tool_manager_failure_path_unit_test`
2. 定向验证：
   - RunCtest_CMakeTools: `BuiltinExecutorLaneTest`、`BuiltinExecutorLaneTimeoutTest`、`ToolManagerSkeletonTest`、`ToolManagerBatchInvokeTest`、`ToolManagerPipelineTest`、`ToolManagerFailurePathTest`
3. 聚合回归：
   - Build_CMakeTools: `dasall_tools`、`dasall_unit_tests`
   - `dasall_unit_tests` 构建期间自动执行 unit 集合，结果 `275/275 passed`
4. 结果摘要：
   - BuiltinExecutorLane 的 action / query / diagnose / timeout / partial side effect 行为全部通过。
   - ToolManager 既有 skeleton / batch / pipeline / failure tests 在 builtin 默认接线切换后无回归。
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，但未影响测试通过结论。

## 6. 风险与回退

1. 当前 lane 只收敛到 `ToolResult`，services 的 `compensation_hints` 与独立 evidence refs 尚未进入统一 envelope；后续 018 / workflow / compensation 任务需要继续收口这条链。
2. query / diagnose 的 payload 仍以 services 回执 JSON 直接进入 `ToolResult.payload`；真正的规则化压缩与 digest 安全边界仍依赖 018 的 `ResultProjector`，在其完成前不应把 builtin 最小闭环误判为 digest 完整闭环。