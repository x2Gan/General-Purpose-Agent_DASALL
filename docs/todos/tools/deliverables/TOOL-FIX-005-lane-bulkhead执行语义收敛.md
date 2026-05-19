# TOOL-FIX-005 lane bulkhead 执行语义收敛

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `TOOL-FIX-005`。
2. 本轮目标：把 `ExecutorLanePool` 从仅输出 `lane_key + concurrency_budget` 的静态 reservation，收敛为真实执行期 bulkhead permit；显式补齐并发窗口扣减、overflow/backpressure reason code 与 lock-order 口径。
3. 完成判定：builtin / workflow / MCP 路径的 lane 隔离不再只是 route 元数据，而是执行期可判定的 permit 语义；超限路径返回稳定 backpressure reason code；MCP 饱和不影响 builtin lane 获取。

## 2. 本地证据

1. `docs/architecture/DASALL_tools子系统详细设计.md` 2.1 明确 `TOOL-C016`：若 tools 引入 lane pool，必须声明 `overflow_policy`、`backpressure` 与 `lock order`，且不得持锁执行网络 I/O。
2. 同文档 2.1 `TOOL-C021` 要求 builtin、workflow、MCP 至少隔离 lane 级资源池或并发窗口，避免单一路径故障扩大成全局工具执行面不可用。
3. `docs/ssot/InfraConcurrencyPolicy.md` 规定平台/控制面默认应显式暴露 backpressure，并要求 `reject/drop_oldest/timeout` 路径都具备可观测失败计数；L3 I/O 不得在持有 L2 锁时执行。
4. 当前实现中 `tools/src/execution/ExecutorLanePool.cpp` 只按 timeout/profile 开关返回 `LaneReservation{available, concurrency_budget}`，没有任何 inflight 扣减、释放或超限拒绝。
5. 当前 `tools/src/ToolManager.cpp` 只把 `route_decision.lane_key` 传入 `BuiltinExecutorLane` / `WorkflowEngine` / `MCPLane` 作为标签，不握有真实 permit，因此现有 tests 只能证明“选中了 lane”，不能证明“资源被隔离”。

## 3. 外部参考

1. Azure Architecture Center Bulkhead pattern 指出：应按依赖/消费者隔离资源池，避免某一路径的资源耗尽拖垮其它路径；当某一 service 的连接池或并发窗口被耗尽时，其他隔离池应继续可用。
2. 该模式特别强调 consumer 侧可使用独立连接池、thread pool 或 semaphore 做隔离，这与 DASALL tools 当前的 builtin / workflow / server-scoped MCP lane 模型一致。
3. 对本任务的直接启发：lane bulkhead 的最小闭环不是“给 route 打标签”，而是“执行前 acquire permit、执行后 release permit、超限时 fail-closed 并返回可判定 backpressure reason code”。

参考链接：<https://learn.microsoft.com/en-us/azure/architecture/patterns/bulkhead>

## 4. 设计结论

### 4.1 作用边界

1. `ExecutorLanePool` 继续保持 module-local，不上升到 shared contracts。
2. `ToolRouteSelector` 仍只负责 lane 选择，不承担执行与资源扣减。
3. `ToolManager` 负责在 route 已确定后获取 lane permit，并在执行结束后释放。
4. `BuiltinExecutorLane`、`WorkflowEngine`、`MCPLane` 不直接管理全局 lane pool，只消费 `lane_key` 与 route 事实。

### 4.2 并发窗口与 overflow_policy

1. builtin lane：单独维护 `builtin` permit 池，容量取 `timeout_view.max_tool_calls`。
2. workflow lane：单独维护 `workflow` permit 池，容量取 `timeout_view.max_tool_calls`。
3. MCP lane：按 `server_id` 维护 `mcp:<server_id>` permit 池，容量取 `timeout_view.max_tool_calls`，不同 server 互不影响。
4. overflow_policy：本轮固定为 `reject`，不引入排队或阻塞等待。
5. backpressure 行为：当 lane 无剩余 permit 时，调用直接 fail-closed，返回稳定 reason code，不等待、不重试、不借用其他 lane。

### 4.3 reason code 与可观测面

1. builtin 超限：`lane.builtin.backpressure`
2. workflow 超限：`lane.workflow.backpressure`
3. MCP 超限：`lane.mcp.backpressure`
4. unavailable 继续保留既有 route reason code；只有在 route 已可用但 permit 获取失败时才落 backpressure reason code。
5. `ToolManager` 失败 envelope、trace、metrics 与 result projection 应继续使用该稳定 reason code，保证超限路径可追溯。

### 4.4 lock order

1. `ExecutorLanePool` 只引入 lane-state 互斥量，属于 L2。
2. permit acquire/release 只在 L2 内更新 inflight 计数和 availability，不执行 builtin/service/MCP I/O。
3. `ToolManager` 必须在完成 acquire 后释放 `ExecutorLanePool` 内部锁，再进入 `BuiltinExecutorLane`、`WorkflowEngine` 或 `MCPLane`。
4. 任何 MCP `ensure_session()`、adapter invoke、services 调用都发生在无 lane-state 锁状态下，满足“不持 L2 锁执行 I/O”。

## 5. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | lane pool 要有真实 permit 而非静态 reservation | `tools/src/execution/ExecutorLanePool.h`、`tools/src/execution/ExecutorLanePool.cpp` |
| D2 | route 后、执行前 acquire；结束后 release | `tools/src/ToolManager.h`、`tools/src/ToolManager.cpp` |
| D3 | 超限 reason code 稳定且按 lane 区分 | `tools/src/execution/ExecutorLanePool.*`、`tools/src/ToolManager.cpp` |
| D4 | 并发窗口与 bulkhead 测试可二值化 | `tests/unit/tools/ExecutorLanePoolConcurrencyTest.cpp`、`tests/integration/tools/ToolLaneBackpressureIntegrationTest.cpp` |
| D5 | focused tests 可发现并接入门禁 | `tests/unit/tools/CMakeLists.txt`、`tests/integration/tools/CMakeLists.txt` |

## 6. Build 三件套

1. 代码目标：在 `ToolManager` 执行主链接入 lane permit acquire/release，并让 `ExecutorLanePool` 维护 per-lane inflight 窗口与 backpressure reason code。
2. 测试目标：新增单测覆盖 permit 扣减/释放与 server-scoped MCP 隔离；新增 integration 覆盖 builtin lane 饱和返回稳定 reason code，以及 MCP lane 饱和不影响 builtin。
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_executor_lane_pool_concurrency_unit_test","dasall_tool_route_selector_unit_test","dasall_tool_lane_backpressure_integration_test"])`
   - `RunCtest_CMakeTools(tests=["ToolRouteSelectorTest","ExecutorLanePoolConcurrencyTest","ToolLaneBackpressureIntegrationTest"])`

## 7. 风险与回退

1. 若 permit 生命周期接错，可能造成 lane 泄漏，表现为后续调用永久 backpressure；因此测试必须覆盖 release 后窗口恢复。
2. 若把 acquire 放到执行器内部，workflow / builtin / MCP 三条路径会各自复制 bulkhead 逻辑，破坏治理链集中性；本轮不采用。
3. 若尝试引入阻塞队列或后台 worker，会越过 `TOOL-FIX-005` 的最小范围，并引入额外 lock-order 风险；本轮固定为 `reject` 不排队。

## 8. D Gate

1. 设计产物已落盘。
2. Design->Build 映射已明确到代码与测试文件。
3. Build 三件套已锁定。
4. 范围保持在 lane bulkhead permit 语义，不扩张到 generic MCP、observability sink 或 installed 证据。

结论：D Gate = PASS，可进入 `TOOL-FIX-005` Build 阶段。