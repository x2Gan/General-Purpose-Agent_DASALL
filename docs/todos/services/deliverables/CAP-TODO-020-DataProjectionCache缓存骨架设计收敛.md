# CAP-TODO-020 DataProjectionCache 缓存骨架设计收敛

日期：2026-04-09
任务：CAP-TODO-020
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.2 / 6.3 已冻结 `DataProjectionCache` 的职责是缓存只读投影视图、降低重复查询成本，并保持 internal-only。
2. 同一设计文档 6.9.1 已给出 `capability_cache_policy.expire_after_ms -> data_cache_ttl_ms` 与 `capability_cache_policy.stale_read_allowed -> default_stale_read_policy` 的固定派生关系，因此 020 必须把 TTL、stale-read 与 `from_cache` 语义显式化。
3. 设计文档 6.8 / 9.3 已要求 stale 事实不能被隐藏：strict 模式下应暴露 `DataStale`，allow_stale 模式下允许返回缓存数据但必须标记 `from_cache=true`。
4. CAP-TODO-011 已提供 services unit discoverability，CAP-TODO-017 的 ExecutionQueryLane 也已验证读侧 freshness 分支模式，因此 020 可以先把 cache 组件独立落盘，再由 021 直接消费。

## 2. 外部参考

1. Azure Cache-Aside pattern 强调缓存只应服务读路径、写路径成功后再决定是否刷新；这支持本轮把 `DataProjectionCache` 限定为 query-only internal component，而不是缓存命令结果。
2. 同一模式也强调 stale data 必须显式按策略处理；这支持本轮把 lookup 结果分成 miss / hit_fresh / hit_stale，并显式区分 strict 与 allow_stale 的 `from_cache` 判定位。
3. Azure Bulkhead pattern 对资源隔离的强调也支持本轮先把缓存作为独立 data 组件实现，避免把 TTL 与 stale 分支耦合进 execution 车道或 facade 组合根。

## 3. Design 结论

1. `DataProjectionCache` 作为 internal-only 组件新增于 `services/src/data/`，维护 `dataset + filters_json + projection` 组合键到 cached rows snapshot 的映射。
2. cache 组件通过 injected `now_ms` 时间源计算 snapshot age，并以 `ProjectionCacheState` 显式区分 `miss`、`hit_fresh` 与 `hit_stale`。
3. stale + strict 返回 `hit_stale` 但 `from_cache=false`，让上层 `DataQueryLane` 继续转成 `DataStale`；stale + allow_stale 返回 `hit_stale` 且 `from_cache=true`，允许直接使用过期缓存。
4. 缓存条目记录 `rows_json`、`cache_ref`、`cached_at_ms` 与 `age_ms` 事实，供后续 DataQueryLane 在 stale/error 路径复用。
5. 组件只服务只读数据路径，不缓存任何 execution command 结果，也不引入 profile schema 或主动失效机制。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 internal `DataProjectionCache` 与 lookup 结果结构 | services/src/data/DataProjectionCache.h、services/src/data/DataProjectionCache.cpp |
| TTL / stale / from_cache 判定位与 injected time source | services/src/data/DataProjectionCache.cpp |
| 覆盖 miss、fresh hit、stale + strict、stale + allow_stale 四类 unit 场景 | tests/unit/services/data/DataProjectionCacheTest.cpp |
| 新增 data 单测子目录并接入 services/top-level unit 聚合 | tests/unit/services/data/CMakeLists.txt、tests/unit/services/CMakeLists.txt、tests/unit/CMakeLists.txt |
| 将 cache 组件纳入 services 构建图 | services/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/data/DataProjectionCache.h/.cpp`，实现 cache key、TTL、stale 状态与 `from_cache` 判定位。
2. 测试目标：新增 `tests/unit/services/data/DataProjectionCacheTest.cpp`，覆盖 miss、fresh hit、stale + strict、stale + allow_stale 场景。
3. 验收命令：
   - `cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 6. 风险与回退

1. 当前 cache key 直接使用 `dataset|filters_json|projection` 原始串拼接，尚未做语义级 JSON 规范化；若后续需要 filters 顺序无关的 key 等价，必须先更新设计文档与 TODO。
2. 020 只提供 cache skeleton，不负责 DataStale 错误对象构造或直连 adapter 查询；这些语义将在 CAP-TODO-021 DataQueryLane 中完成。
3. 当前组件不提供主动失效和容量回收策略；若未来 profile 需要 LRU、bounded size 或 refresh-after-write，必须在 ServicePolicyView / design 中先补约束，再扩张实现。