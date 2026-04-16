# TOOL-TODO-014 ToolRouteSelector 与 lane 选择骨架设计收敛

日期：2026-04-16  
任务：TOOL-TODO-014  
状态：D Gate PASS

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-014 的验收条件要求：ToolRouteSelector 必须实现 `select_route()`、`score_builtin_candidate()`、`score_mcp_candidate()`，并能对 builtin / workflow / mcp 选择、stale snapshot fallback、route unavailable 给出自动断言。
2. docs/architecture/DASALL_tools子系统详细设计.md 6.12.1 明确 RouteSelector 的职责边界是“在 descriptor、route hint、capability snapshot、health、profile switches 共同约束下做最终 lane 选择”，并要求 lane 隔离保持 module-local，不进入 shared contracts。
3. docs/architecture/DASALL_tools子系统详细设计.md 6.12.1 同时规定：workflow / agent delegation 优先识别；MCP 只有在存在可信 binding 且 profile 允许 stale read 时才能使用 stale snapshot；否则必须返回 `RouteUnavailable`。
4. TOOL-TODO-009、012、013 已分别提供稳定 descriptor / binding 目录、timeout/profile 投影视图与 fail-closed admission，因此 014 只负责“路由选择与 lane 骨架”，不提前接入真实 executor 或 MCP session 生命周期。

## 2. Design 结论

1. `ToolRouteDecision`、`ToolRouteHealthSnapshot`、`LaneReservation` 都保持 module-local，避免把 route supporting object 升格到 public ABI 或 shared contracts。
2. RouteSelector 的执行顺序固定为：先识别 workflow / agent delegation，再对 builtin 与 MCP 做评分，最后输出 route、lane key、reason code、selected server 与 stale snapshot 标记。
3. builtin 候选只有在 builtin lane enabled、lane 健康、descriptor 不是 workflow-like 且 timeout/max_tool_calls 有效时才可入选；MCP 候选只有在 mcp lane enabled、存在 binding、capability snapshot 可证明工具存在时才可入选。
4. stale snapshot 只在三条件同时满足时可用：snapshot freshness 为 stale、profile `stale_read_allowed=true`、snapshot 带可信 `trust_marker`；否则 MCP 分支直接失效，不做隐式放宽。
5. ExecutorLanePool 只负责把 route 决策映射到 builtin / workflow / server-scoped mcp lane reservation，保持 lane 隔离与并发预算语义，但不承担执行本体。

## 3. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| lane pool skeleton | tools/src/execution/ExecutorLanePool.h；tools/src/execution/ExecutorLanePool.cpp |
| route selector skeleton | tools/src/route/ToolRouteSelector.h；tools/src/route/ToolRouteSelector.cpp |
| route selector 单测 | tests/unit/tools/ToolRouteSelectorTest.cpp；tests/unit/tools/ToolRouteFallbackTest.cpp |
| tools/unit 构建接线 | tools/CMakeLists.txt；tests/unit/tools/CMakeLists.txt；tests/unit/CMakeLists.txt |

## 4. Build 三件套

1. 代码目标：让 `dasall_tools` 编译真实 ToolRouteSelector / ExecutorLanePool 源文件，并移除 route/execution placeholder 编译入口。
2. 测试目标：通过 route selector 与 fallback 两条 unit tests 覆盖 workflow/builtin/mcp 选择、stale snapshot fallback 和 `RouteUnavailable`。
3. 验收命令：
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`、`dasall_contract_tests`
   - RunCtest_CMakeTools 运行 `ToolRouteSelectorTest`、`ToolRouteFallbackTest`

## 5. 风险与回退

1. 当前 MCP 候选评分仍基于最小事实集：binding、capability snapshot、health、route hint；后续 CapabilityCache / ToolHealthProbe 落地后，应优先消费更正式的 freshness/health 证据，而不是在 RouteSelector 内继续扩临时逻辑。
2. `RouteUnavailable` 目前仍是 module-local reason code，不是 shared `ResultCode`；后续 ToolManager 接线时必须原样保留或做稳定映射，不能在不同阶段生成互相冲突的 route reason。
3. lane reservation 现在只收敛 lane key 与并发预算，不做真正池化调度；后续 executor 实现必须在不打破 lane 隔离的前提下扩成真实执行资源池。