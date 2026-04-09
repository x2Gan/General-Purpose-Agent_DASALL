# CAP-TODO-023 ServiceConfigAdapter 与 ServicePolicyView 派生设计收敛

日期：2026-04-09
任务：CAP-TODO-023
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.2 / 6.9.1 / 6.9.2 已冻结 `ServiceConfigAdapter` 的职责是只消费 `RuntimePolicySnapshot`、`BuildProfileManifest` 与既有运行事实，在 services 模块内派生统一的 `ServicePolicyView`。
2. 同一设计文档明确禁止新增 `services.*` 顶层 schema，并要求 worker、timeout/circuit、TTL/stale-read、safe mode、高风险确认、审计粒度与 observability 开关都从既有 profile/runtime 语义收敛，而不是让各个 lane 自己发明配置分支。
3. [services/src/adapters/AdapterRouter.h](../../../../services/src/adapters/AdapterRouter.h) 中现有 `ServicePolicyView` 只是 035 为 Router 落下的最小字段；[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_开发执行记录.md) 也已明确 023 可以扩展其派生维度，但不能改变既有 fail-closed、envelope-first 和 no-client-override 语义。
4. 当前 [profiles/include/RuntimePolicySnapshot.h](../../../../profiles/include/RuntimePolicySnapshot.h) 尚未向 services 暴露 `runtime_budget.worker_threads` 对应的运行时并发提示，因此若不先补最小可读字段，023 无法按 6.9.1 验证 lane worker 派生公式。

## 2. 外部参考

1. Azure External Configuration Store pattern 强调配置应由中心化接口统一暴露，应用内部只派生本地视图，而不是复制 schema 或各自维护平行配置表；这支持本轮把 services 配置收口到单一 `ServiceConfigAdapter`，并明确禁止引入新的 `services.*` 顶层键。
2. 同一模式也强调配置接口应提供 typed access、版本化与错误缺省处理；这支持本轮让 `ServiceConfigAdapter` 直接消费已冻结的 `RuntimePolicySnapshot` / `BuildProfileManifest`，并在输入不一致时 fail-closed，而不是在 lane 内散落字符串解析逻辑。
3. 该模式对“本地缓存/派生视图而非重复 schema”的建议，也支持本轮把 `ServicePolicyView` 维持为 internal-only typed view，供 Router、execution/data/system 与 observability 共享，而不把它提升为公共 ABI。

## 3. Design 结论

1. 新增 internal `ServiceConfigAdapter`，唯一入口是 `derive_policy_view(RuntimePolicySnapshot, BuildProfileManifest)`；组件不直接读取 YAML，也不向 profiles 反向写回配置。
2. `ServicePolicyView` 从 Router 最小字段扩展为统一内部策略视图，显式承载 lane worker、deadline/timeout、circuit threshold、cache TTL、stale-read baseline、resync backoff、固定 overflow policy、safe mode / high-risk / audit / caller domain 约束，以及 observability 开关与 trace 采样率。
3. `RuntimePolicySnapshot` 补充 `worker_threads()` 只读访问面，以承接 `runtime_budget.worker_threads` 的现有 profile 资产，不改 shared contracts，也不引入新的顶层 schema。
4. `adapter_preference_order` 在当前阶段继续保持 services 内部固定 local-first 路由顺序：`platform_hal` 开启时为 `local_platform -> local_service -> remote_service`，否则为 `local_service -> remote_service`；live availability 仍由 Router/health 事实决定，而不是由 ConfigAdapter 越权裁定。
5. `command_queue_overflow_policy=reject` 与 `subscription_queue_overflow_policy=drop_oldest` 继续保持 6.9.2 的固定内策略，不上升为新的 profile 键。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 internal `ServiceConfigAdapter` 与派生结果对象 | services/src/ops/ServiceConfigAdapter.h、services/src/ops/ServiceConfigAdapter.cpp |
| 扩展 `ServicePolicyView` 为统一内部策略视图 | services/src/adapters/AdapterRouter.h、services/src/adapters/AdapterRouter.cpp |
| 向 services 暴露 `worker_threads` 只读访问面 | profiles/include/RuntimePolicySnapshot.h、profiles/src/RuntimePolicyProvider.cpp |
| 覆盖 worker/timeout/overflow/stale-read/platform/observability 派生 | tests/unit/services/ops/ServiceConfigAdapterTest.cpp |
| 新增 ops 单测子目录并接入 services/top-level unit 聚合 | tests/unit/services/ops/CMakeLists.txt、tests/unit/services/CMakeLists.txt、tests/unit/CMakeLists.txt |
| 将 config adapter 接入 services 构建图 | services/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/ops/ServiceConfigAdapter.h/.cpp`，实现 `RuntimePolicySnapshot / BuildProfileManifest -> ServicePolicyView` 的统一内部派生，并为 `RuntimePolicySnapshot` 补充 `worker_threads()` 只读访问面。
2. 测试目标：新增 `tests/unit/services/ops/ServiceConfigAdapterTest.cpp`，覆盖 worker、timeout、overflow、stale-read、platform/observability 开关与 fail-closed 输入校验。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -R ProfileRuntimePolicySchemaContractTest`

## 6. 风险与回退

1. 当前 `ServicePolicyView` 仍不直接承接 live adapter availability、route trust 或 fallback envelope；这些运行事实继续分别归属 Router、HealthProbe 与 Runtime，避免 ConfigAdapter 越权。
2. 本轮只为 services 暴露 `worker_threads()`，没有把 `infra.health.*` / `infra.metrics.*` 全量搬进 `RuntimePolicySnapshot`；若后续 observability/health 需要更细的导出策略字段，必须先确认 profiles 侧稳定暴露面，而不是在 services 自造旁路配置。
3. `adapter_preference_order` 当前仍采用固定 services 路由顺序而非 profile 可配顺序；若未来确需 profile 化 adapter locality / preference，必须先更新 profiles schema 与 contract gate，再扩张本组件。