# CAP-TODO-019 ExecutionDiagnoseService 诊断路径设计收敛

日期：2026-04-09
任务：CAP-TODO-019
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.3 / 6.6 已冻结 `ExecutionDiagnoseService` 的职责是返回执行目标 reachability、能力与最近故障摘要，且保持 internal-only。
2. 同一设计文档 6.7 / 10.2 已把 diagnose-only 路径归入 V1.1 的低风险读取范围，因此本轮实现必须复用只读 route/bridge 语义，不得引入副作用或补偿链路。
3. 设计文档 6.8 已给出 `AdapterUnavailable`、`RouteUnavailable`、`InvalidRequest` 等稳定错误映射；诊断路径需要输出 `target_reachable` / `report_json` 事实，而不是替代 infra diagnostics 导出器。
4. CAP-TODO-010 / 011 已提供 ServiceFacade 组合根与 unit discoverability，CAP-TODO-017 / 018 已为 query-only 与 subscription-only 路径落盘读侧组件，因此 019 可以直接按 internal execution service 继续推进。

## 2. 外部参考

1. Azure CQRS pattern 对读写职责分离的强调支持本轮把 diagnose-only 路径实现为纯读取组件，不与 `ExecutionCommandLane` 共享副作用语义。
2. Azure Bulkhead pattern 对低风险读取与高风险命令隔离的建议，也支持本轮把 diagnose-only 入口独立落在 execution 子域，而不是复用命令车道。
3. 诊断接口的常见做法是把 reachability、last_error、route facts 组合成序列化报告交给上层决策；这支持本轮把 `include_last_error` 作为只读请求负载透给 adapter，并保持 `report_json` 为事实快照而非控制命令。

## 3. Design 结论

1. `ExecutionDiagnoseService` 作为 internal-only 组件新增于 `services/src/execution/`，复用 `AdapterRouter`、`AdapterBridge` 与 `ResultMapper` 实现 diagnose 路径。
2. diagnose 路由固定以 query-style `diagnose` operation 进入既有 adapter 协议，不额外扩张 `IExecutionService` 公共面，也不修改 shared contracts。
3. `include_last_error` 被序列化为请求 JSON 负载透给 adapter；adapter 成功返回的 payload 直接作为 `report_json`，并把 `target_reachable` 置为 true。
4. 若 diagnose request 缺失 `capability_id` 或 `target_id`，组件立即 fail-closed 为 validation error；若 adapter timeout/unavailable，则返回 provider failure 且 `target_reachable=false`。
5. 若 diagnose receipt 夹带 `side_effects`，组件立即视为只读违约并返回 validation error，确保诊断路径不会隐式改变目标状态。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 internal `ExecutionDiagnoseService` 与依赖注入面 | services/src/execution/ExecutionDiagnoseService.h、services/src/execution/ExecutionDiagnoseService.cpp |
| query-style `diagnose` route request 与 include_last_error 透传 | services/src/execution/ExecutionDiagnoseService.cpp |
| adapter unavailable / invalid request / side_effect violation 错误映射 | services/src/execution/ExecutionDiagnoseService.cpp |
| 覆盖 success、invalid request、adapter unavailable、read-only violation 四类 unit 场景 | tests/unit/services/execution/ExecutionDiagnoseServiceTest.cpp |
| 将 diagnose service unit 接入 execution 与顶层 unit 聚合 | services/CMakeLists.txt、tests/unit/services/execution/CMakeLists.txt、tests/unit/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/execution/ExecutionDiagnoseService.h/.cpp`，实现 diagnose-only 路径、`target_reachable`、`report_json` 与只读错误收口。
2. 测试目标：新增 `tests/unit/services/execution/ExecutionDiagnoseServiceTest.cpp`，覆盖 success、invalid request、adapter unavailable 与 side_effect 违约场景。
3. 验收命令：
   - `cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 6. 风险与回退

1. 当前 diagnose 路径通过 query-style `diagnose` operation 复用既有 route/bridge 协议；若后续 adapter 需要专门 diagnose transport，必须先更新设计文档与 TODO，再扩张枚举和 bridge 协议。
2. `report_json` 当前直接透传 adapter payload，仍属于内部事实快照，不承诺成为新的共享 diagnostics schema；若需要跨模块稳定消费，必须走新的 interface admission review。
3. 019 已落盘 diagnose-only 代码入口，但 integration smoke、审计桥与 loopback fixture 仍未闭环；高风险命令分支依旧受 CAP-GATE-08 约束。