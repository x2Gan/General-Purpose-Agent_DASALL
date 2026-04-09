# CAP-TODO-032 Capability Services profile integration 设计收敛

日期：2026-04-09
任务：CAP-TODO-032
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 的 9.1 和 10.2 已冻结 services profile integration 的最小口径：profile 差异必须通过 `CapabilityServicesProfileIntegrationTest` 证明 `desktop_full` 与 `edge_balanced` 在 route、timeout 和 cache 行为上的差异，而不是新增 `services.*` profile 键。
2. [services/src/ops/ServiceConfigAdapter.cpp](../../../../services/src/ops/ServiceConfigAdapter.cpp) 已冻结 services-facing 派生规则：`ServicePolicyView` 只能消费 `RuntimePolicySnapshot` / `BuildProfileManifest`，并从中导出 `local_platform_route_enabled`、request/workflow timeout、data cache TTL 与 `default_allow_stale_reads`。这决定了 032 只能走真实 profile 资产加载路径，而不能手搓一份脱离仓库基线的测试数据。
3. [profiles/desktop_full/runtime_policy.yaml](../../../../profiles/desktop_full/runtime_policy.yaml) 与 [profiles/edge_balanced/runtime_policy.yaml](../../../../profiles/edge_balanced/runtime_policy.yaml) 已给出本轮需要回归的真实差异：desktop_full 关闭 `platform_hal`、`tool.timeout_ms=2500`、`workflow.timeout_ms=5000`、`expire_after_ms=180000`、`stale_read_allowed=false`；edge_balanced 打开 `platform_hal`、`tool.timeout_ms=1800`、`workflow.timeout_ms=4000`、`expire_after_ms=120000`、`stale_read_allowed=true`。
4. [services/src/data/DataProjectionCache.cpp](../../../../services/src/data/DataProjectionCache.cpp) 已冻结缓存新鲜度边界：stale 判定是 `age_ms > ttl_ms`，而不是 `>=`。因此 032 的 desktop_full 过期断言必须显式越过 180000ms 边界，不能停在 TTL 等值点上。
5. [tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp](../../../../tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp) 仍是 profile schema 不回退的 contract gate；032 必须证明新增 services profile integration 不会绕开该 contract，而是与其并行成立。

## 2. 外部参考

1. CTest 官方手册说明 `ctest -R` 可用于定向执行单用例，`ctest -L` 可用于标签过滤执行；这支持 032 同时验证 `CapabilityServicesProfileIntegrationTest` 单入口稳定性、`profile` 标签 discoverability，以及它对既有 integration 聚合链的兼容性。参考：https://cmake.org/cmake/help/latest/manual/ctest.1.html

## 3. Design 结论

1. 继续保持 services profile integration 为 tests-side 收敛，不修改任何 services public ABI，也不在 profile schema 中增补 `services.*` 顶层配置；032 只新增一个 `integration;profile` 测试入口，消费仓库中已经冻结的真实 profile 资产。
2. 新增 `CapabilityServicesProfileIntegrationTest`，通过 `ProfileCatalog -> BuildProfileResolver -> RuntimePolicyProvider -> ServiceConfigAdapter` 加载真实 `desktop_full` / `edge_balanced` 资产，稳定断言三类差异：
   - route 差异：desktop_full 只有 `local_service -> remote_service`，edge_balanced 在前面追加 `local_platform`
   - timeout 差异：desktop_full 的 request/workflow timeout 为 8000/5000ms，edge_balanced 为 7000/4000ms
   - cache 差异：desktop_full TTL 更长且默认 strict，edge_balanced TTL 更短且默认允许 stale
3. cache 差异证明不能只看 `default_allow_stale_reads` 布尔值，还必须结合 `DataProjectionCache` 的真实 TTL 行为和严格的 `age_ms > ttl_ms` 边界；因此本轮使用注入时钟分别验证 150s 中间态和 182s 过期态，避免把等值边界误判成 stale。
4. 032 完成后，services integration 矩阵已经覆盖 smoke + failure + profile + audit + metrics + trace + health 七类入口，剩余专项风险从“实现缺口”转为 admission 与 Gate 证据收口。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 为 services integration 增加 profile 标签入口 | tests/integration/services/CMakeLists.txt |
| 用真实 profile 资产回归 route / timeout / cache 差异 | tests/integration/services/CapabilityServicesProfileIntegrationTest.cpp |
| 回写 032 状态、验证证据与剩余 Gate 说明 | docs/todos/services/DASALL_capability_services子系统专项TODO.md、docs/worklog/DASALL_开发执行记录.md |

## 5. Build 三件套

1. 代码目标：新增 `CapabilityServicesProfileIntegrationTest`，并将其注册为 `integration;profile` 测试入口，直接消费 `ProfileCatalog`、`BuildProfileResolver`、`RuntimePolicyProvider`、`ServiceConfigAdapter` 与 `DataProjectionCache` 的真实组合路径。
2. 测试目标：至少稳定覆盖 desktop_full / edge_balanced 的 route、request/workflow timeout、cache TTL 与默认 stale-read 基线差异，并保证 `ProfileRuntimePolicySchemaContractTest` 与 integration 聚合链不回退。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_services_profile_integration_test`
   - `ctest --test-dir build-ci --output-on-failure -R CapabilityServicesProfileIntegrationTest`
   - `ctest --test-dir build-ci --output-on-failure -L profile`
   - `ctest --test-dir build-ci --output-on-failure -L integration`
   - `ctest --test-dir build-ci --output-on-failure -R ProfileRuntimePolicySchemaContractTest`
   - `ctest --test-dir build-ci -N`

## 6. 风险与回退

1. 当前 profile integration 断言直接依赖真实 `desktop_full` / `edge_balanced` 资产值；若 profile 资产未来有意调整 platform_hal、timeout 或 cache policy，应先更新 profile 文档/专项 TODO，再同步调整测试，而不是把服务层差异重新塞回私有 schema。
2. 缓存过期断言严格绑定 `DataProjectionCache` 的 `age_ms > ttl_ms` 语义；若后续 freshness 规则变化，必须同步调整 032 的时钟边界，否则容易产生假回归。
3. VS Code CMake Tools 在当前工作区仍存在“空 target/test 列表 + 无法配置项目”的工具态问题；在该问题修复前，032 的回归验证应继续以显式 `build-ci` 命令链为准，避免误把 IDE 配置失败当作代码回归。