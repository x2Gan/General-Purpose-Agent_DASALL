# CAP-TODO-015 ExecutionCommandLane 命令车道设计收敛

日期：2026-04-09  
任务：CAP-TODO-015  
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.2/6.3 已冻结 `ExecutionCommandLane` 的职责是承接副作用命令与补偿动作，维护串行化、幂等键、目标级忙碌保护和结构化错误输出。
2. 同一设计文档 6.7.1 / 6.8.1 已把命令路径时序和 `PartialSideEffect`、`TargetBusy`、`PolicyDenied`、`RouteUnavailable` 等错误分类收敛为 `AdapterRouter -> AdapterBridge -> ResultMapper` 的串联流程。
3. CAP-TODO-035~040 已提供稳定的 `AdapterSelection`、`AdapterReceipt`、三类 adapter 与 `ResultMapper` 基础，因此 015 可以直接落在 lane 层复用既有 route/receipt/result contract，而不需要再拼装第二套错误协议。
4. [docs/todos/services/DASALL_capability_services子系统专项TODO.md](../DASALL_capability_services子系统专项TODO.md) 7.3 / CAP-GATE-08 明确要求在 query-only/diagnose-only integration smoke 与 `ServiceAuditBridge` 未就绪前，不得提前落高风险命令分支代码，因此本轮只允许命令车道低风险基础落盘，高风险动作必须 fail-closed。

## 2. 外部参考

1. Azure CQRS pattern 强调命令与查询应拆分成不同模型，写侧承接验证和业务动作、读侧保持查询高效；这支持本轮把 `ExecutionCommandLane` 作为独立 command lane 落盘，而不是继续把命令语义堆进 `ServiceFacade`。
2. Azure Bulkhead pattern 强调关键依赖应隔离资源与并发影响，避免单一路径故障拖垮其他消费者；这支持本轮为关键动作建立 target/action 级串行化门和 busy fail-fast 语义。
3. Azure Compensating Transaction pattern 强调补偿步骤必须幂等、可恢复，且系统应记录足够的 undo 事实；这支持本轮把 `idempotency_key`、`source_execution_id` 和 `compensation_hints` 保持为 lane 输出事实，而不是让 services 越权裁定是否恢复。

## 3. Design 结论

1. `ExecutionCommandLane` 以 internal-only 组件新增于 `services/src/execution/`，只消费 `AdapterRouter`、`AdapterBridge` 与 `ResultMapper`，不扩张 shared contracts，也不改写 `InterfaceCatalog` readiness。
2. 命令车道复用既有 `AdapterRouteRequest` / `AdapterReceipt` / `ExecutionCommandResult` 语义，统一处理 execute 与 compensate 两类入口，但仅输出事实、side effects 与补偿提示，不裁定是否恢复。
3. 关键动作必须具备幂等键并通过 target/action 级串行化门；当同一关键动作重入时，车道返回 `target_busy` 结构化错误，而不是静默排队或隐式覆盖结果。
4. 同一幂等键的重复命令复用第一次已完成结果，不重复调用 adapter；这保持命令路径最小幂等语义，同时不给 runtime/tool 新增额外控制面。
5. 高风险动作在 CAP-GATE-08 未满足前显式 fail-closed 为 `policy_denied`，从而保证本轮不会提前落入 V1.2/V1.3 的高风险命令实现。
6. `PartialSideEffect` 路径只透出 provider 已返回的 `side_effects` / `evidence_refs`，补偿提示由注入式 hint lookup 提供，为 CAP-TODO-016 的静态 `CompensationCatalog` 预留稳定接缝。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 internal `ExecutionCommandLane` 组件并保持 execute/compensate 双入口 | services/src/execution/ExecutionCommandLane.h、services/src/execution/ExecutionCommandLane.cpp |
| 关键动作幂等键缓存与 target/action 串行化门 | services/src/execution/ExecutionCommandLane.cpp |
| 高风险动作在 CAP-GATE-08 前 fail-closed | services/src/execution/ExecutionCommandLane.cpp |
| 复用 Router / Bridge / ResultMapper 完成 route、receipt 与结构化错误收口 | services/src/execution/ExecutionCommandLane.cpp |
| 覆盖 success、invalid request、partial side effect、critical busy、high-risk gate 五类 unit 场景 | tests/unit/services/execution/ExecutionCommandLaneTest.cpp |
| 将 execution 子目录接入 services unit 聚合 | tests/unit/services/execution/CMakeLists.txt、tests/unit/services/CMakeLists.txt、tests/unit/CMakeLists.txt、services/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/execution/ExecutionCommandLane.h/.cpp`，实现 low-risk command lane、幂等缓存、关键动作串行化与高风险 gate fail-closed。
2. 测试目标：新增 `tests/unit/services/execution/ExecutionCommandLaneTest.cpp` 与 `tests/unit/services/execution/CMakeLists.txt`，覆盖 success / invalid request / partial side effect / target busy / high-risk gate。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest`

## 6. 风险与回退

1. 由于 contracts 当前没有 success result code 种子，015 继续保守复用既有 `ResultCode` 失败种子；后续若要细化成功码，必须先走 contracts/design 评审。
2. 本轮故意不放开高风险动作执行；若后续需要启用 `safe_mode.*` 或 require_confirmation 动作，必须先满足 CAP-GATE-08，再由 CAP-TODO-024/030 提供审计与 integration 证据。
3. 补偿提示目前通过 injected lookup 进入 lane，只为 CAP-TODO-016 预留接缝；在静态目录未落盘前，不应把临时 hint 生成逻辑扩张成恢复编排器。