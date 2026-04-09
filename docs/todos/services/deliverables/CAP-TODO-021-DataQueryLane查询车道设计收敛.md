# CAP-TODO-021 DataQueryLane 查询车道设计收敛

日期：2026-04-09
任务：CAP-TODO-021
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.2 / 6.3 已冻结 `DataQueryLane` 的职责是承接业务数据、状态数据与目录数据的只读查询，并按 freshness 决定是否允许返回缓存。
2. 同一设计文档 6.8 / 9.3 已要求 stale 事实不能被隐藏：strict 模式下应返回 `DataStale`，allow_stale 模式下允许返回缓存结果但必须显式标记 `from_cache=true`。
3. 设计文档 6.6 / 6.9.1 已把 `DataQueryRequest`、`DataCatalogRequest`、`DataQueryResult.from_cache` 与 cache TTL/stale 策略固定下来，因此 021 可以直接消费 CAP-TODO-020 的 `DataProjectionCache`。
4. CAP-TODO-020 已提供 lookup/stale/from_cache 骨架，CAP-TODO-035~040 已提供稳定的 Router / Bridge / ResultMapper，因此 021 只需把 data query 与 catalog listing 的只读路径收口到统一 internal lane。

## 2. 外部参考

1. Azure Cache-Aside pattern 强调 cache hit/miss/stale 分支应在读取路径中显式处理；这支持本轮把 `DataQueryLane` 设计成“先查 cache，再决定是否 live query”的车道，而不是把缓存隐藏在 adapter 内部。
2. Azure CQRS pattern 对查询侧只读语义的要求，也支持本轮把 `query()` 与 `list_capabilities()` 限定为 read-only 操作，并对 `side_effects` 违约做 fail-closed 处理。
3. Azure Bulkhead pattern 对数据读路径和命令写路径隔离的建议，支持本轮把 `DataQueryLane` 保持在 data 子域独立组件内，而不是复用 execution 车道。

## 3. Design 结论

1. `DataQueryLane` 作为 internal-only 组件新增于 `services/src/data/`，复用 `AdapterRouter`、`AdapterBridge`、`ResultMapper` 与 `DataProjectionCache` 实现 `query()` / `list_capabilities()` 两条只读入口。
2. `query()` 在 cache fresh hit 或 allow_stale stale hit 时直接返回缓存结果并标记 `from_cache=true`；在 strict stale 时返回 `DataStale` 错误事实，不把过期数据伪装成成功结果。
3. cache miss 时，车道按 projection/filter 透传 live adapter request；live 查询成功后把 rows payload 回写 `DataProjectionCache`，为后续 query 复用。
4. `list_capabilities()` 固定以 query-style `catalog.list` operation 承接目录 discoverability，不消费缓存，也不扩张执行语义。
5. 若 data query 或 catalog receipt 夹带 `side_effects`，车道立即返回 validation error，确保读路径不会隐式写入。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 internal `DataQueryLane` 与依赖注入面 | services/src/data/DataQueryLane.h、services/src/data/DataQueryLane.cpp |
| cache hit/miss/stale 与 live query 分支 | services/src/data/DataQueryLane.cpp |
| query-style `catalog.list` discoverability 路径 | services/src/data/DataQueryLane.cpp |
| 覆盖 cache miss/hit、strict stale、allow_stale、side_effect 违约、catalog listing 五类 unit 场景 | tests/unit/services/data/DataQueryLaneTest.cpp |
| 将 query lane unit 接入 data 与顶层 unit 聚合 | tests/unit/services/data/CMakeLists.txt、tests/unit/CMakeLists.txt、services/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/data/DataQueryLane.h/.cpp`，实现 data query / catalog listing、cache 集成与只读错误收口。
2. 测试目标：新增 `tests/unit/services/data/DataQueryLaneTest.cpp`，覆盖 cache miss/hit、strict stale、allow_stale、query side_effect 违约与 catalog listing 场景。
3. 验收命令：
   - `cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 6. 风险与回退

1. 当前 lane 把 `dataset` / `target_class` 直接映射为 adapter route 的 `capability_id` / `target_id`，这是 V1 的最小路由约定；若后续需要 dataset 与 capability registry 解耦，必须先更新设计文档与 TODO。
2. 021 仍未引入独立 read store 或物化视图，只是在既有 adapter path 上增加 cache；若后续需要 projection worker/read model，必须在 data 子域另开任务。
3. data query 和 catalog listing 已落盘，但 integration smoke、metrics/health 桥和 loopback fixture 仍未闭环；D2 的系统子域与 observability 任务仍需继续推进。