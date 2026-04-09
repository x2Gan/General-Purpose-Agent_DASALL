# CAP-TODO-017 ExecutionQueryLane 只读查询车道设计收敛

日期：2026-04-09
任务：CAP-TODO-017
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.3 已冻结 `ExecutionQueryLane` 的职责是承接执行目标状态读取与只读查询，不产生副作用。
2. 同一设计文档 6.8.1 已把 `DataStale` 与 `AdapterUnavailable` 固定为查询路径需要稳定输出的错误分类，并要求 stale/read-only 语义可被二值判定。
3. 设计文档 6.6 / 6.9.1 / 10.2 已明确 `ExecutionQueryRequest.freshness` 只支持 `strict` / `allow_stale`，且 V1.1 query-only 路径是 CAP-GATE-08 之前允许落盘的只读灰度档位。
4. CAP-TODO-035~040 与 CAP-TODO-016 已提供稳定的 Router / Bridge / ResultMapper / CompensationCatalog 基础，因此 017 可以直接复用 route/receipt/result contract，而无需扩张 DataProjectionCache 终态实现。

## 2. 外部参考

1. Azure CQRS pattern 强调查询侧应保持 read-only、避免夹带写语义，并用独立模型优化读取路径；这支持本轮把 `ExecutionQueryLane` 与命令车道继续拆开，而不是复用 `ExecutionCommandLane` 的副作用语义。
2. Azure Cache-Aside pattern 强调 stale data 需要显式检测并按策略处理；这支持本轮在 `allow_stale` 下显式回落到 cached snapshot 并标记 `from_cache=true`，而在 `strict` 下返回 `DataStale`。
3. Azure Bulkhead pattern 对读写隔离的强调也支持本轮把 query-only 车道保持在独立 internal lane 中，避免高风险命令路径的错误和并发策略拖累只读查询。

## 3. Design 结论

1. `ExecutionQueryLane` 作为 internal-only 组件新增于 `services/src/execution/`，复用 `AdapterRouter`、`AdapterBridge` 与 `ResultMapper` 实现 `query_state()` 的只读路径。
2. 查询车道只接受 `capability_id`、`target_id`、`query_kind` 完整的请求；缺失字段时 fail-closed 为 validation error。
3. adapter receipt 若携带 `side_effects`，查询车道立即视为 read-only 违约并返回 validation failure，防止查询路径隐式写入或掩盖 provider 行为漂移。
4. 当 provider 返回 `data_stale` 且请求 freshness=`allow_stale` 时，车道可消费 injected cached snapshot 并返回 `from_cache=true` 的成功结果；若 freshness=`strict` 或没有可用 cached snapshot，则保持 `DataStale` 错误输出。
5. `AdapterUnavailable` / timeout 等 provider 失败仍通过 `ResultMapper` 收口为稳定的 provider failure，不被 stale fallback 掩盖。
6. 在 CAP-TODO-020 DataProjectionCache 落盘前，017 只保留 injected cached snapshot 接缝，不把临时缓存逻辑误写成终态 cache 组件。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 internal `ExecutionQueryLane` 组件 | services/src/execution/ExecutionQueryLane.h、services/src/execution/ExecutionQueryLane.cpp |
| strict / allow_stale freshness 分支 | services/src/execution/ExecutionQueryLane.cpp |
| 查询 receipt 的只读 side_effect 约束 | services/src/execution/ExecutionQueryLane.cpp |
| 覆盖 success、invalid request、strict stale、allow_stale cached、adapter unavailable、read-only violation 六类 unit 场景 | tests/unit/services/execution/ExecutionQueryLaneTest.cpp |
| 将 query lane unit 接入 execution/top-level unit 聚合 | services/CMakeLists.txt、tests/unit/services/execution/CMakeLists.txt、tests/unit/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/execution/ExecutionQueryLane.h/.cpp`，实现只读查询路径、freshness 分支与结构化错误输出。
2. 测试目标：新增 `tests/unit/services/execution/ExecutionQueryLaneTest.cpp`，覆盖 success / invalid / stale / adapter unavailable / read-only violation 场景。
3. 验收命令：
   - `cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 6. 风险与回退

1. 当前 cached snapshot 仍是 injected seam，不是 DataProjectionCache 终态；在 CAP-TODO-020 前不要把 query lane 内部 fallback 误当成完整 cache 子系统。
2. `allow_stale` 目前只覆盖 `DataStale` 场景，不掩盖 `AdapterUnavailable`；若后续要让缓存承接更广泛的 degraded read 语义，必须先回写设计与 TODO。
3. query-only 路径已落盘，但 CAP-GATE-08 仍需要 integration smoke 与审计桥证据才能放开高风险命令分支，这一点不能被 017 的完成误解为已解锁 V1.2/V1.3。