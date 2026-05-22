# INF-FIX-007 SecretManager live composition seam 收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `INF-FIX-007`。
2. 本轮目标：冻结 `ISecretManager` 的 live composition seam，在不扩展 `ISecretManager` ABI、且不使用 qemu / kvm 的前提下，为 daemon / gateway shared runtime composition 提供最小 production builder，并保留标准 unavailable/provider-timeout 语义。
3. 用户附加约束：按 `project-implementation-cycle` 串行推进；如存在前置 blocker 先解阻；逐文件落盘；完成后按仓库规范提交推送；若验收口径写成 qemu，则改成真实本机 build-tree / local 口径再执行；本轮不得越界提前实现 `INF-FIX-008` 的 Access ownership seam 注入。

## 2. 本地证据

1. `ISecretManager`、`SecretManagerFacade`、`FileSecretBackend` 与 `SecretBootstrapWriter` 读写边界在 infra 内已经存在；缺口不在 secret 读链本身，而在 app-level runtime live composition 没有 authoritative public seam。
2. 首轮 focused build 直接暴露出边界缺陷：`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 若尝试 include `secret/SecretManagerFacade.h` / `secret/backends/FileSecretBackend.h` 等 infra internal concrete headers，会在 runtime_support 编译面报 `fatal error: secret/SecretManagerFacade.h: No such file or directory`。这证明 007 的根因不是“不会组 secret”，而是“缺少 infra public builder seam”。
3. `InstallLayout` 已提供 packaged `state_root=/var/lib/dasall` 与 `DASALL_STATE_ROOT` override；`FileSecretBackend` 在 secret root 缺失时会报告 backend unavailable，`SecretManagerFacade` 再把该状态映射为标准 `ProviderTimeout`。因此 live builder 的最小正确语义不是直接返回 `nullptr`，而是返回可用的 `ISecretManager` seam object，让缺根路径继续经公共 ABI fail-closed。
4. `DaemonAccessPipelineOptions::ownership_secret_manager` / `GatewayAccessPipelineOptions::ownership_secret_manager` seam 已存在，但这是 `INF-FIX-008` 的职责；本轮只冻结 shared runtime composition seam，不越权把 secret 注入 Access ownership path。

## 3. 设计结论

### 3.1 根因收口

1. `INF-FIX-007` 的根因不是 SecretManager 缺实现，而是 runtime_support 无法合法依赖 infra secret concrete implementation，导致 daemon / gateway shared composition 只能停留在“理论可接”而没有 public live seam。
2. 若把 `FileSecretBackend + SecretManagerFacade` 直接塞进 runtime_support，会破坏 infra public/private 边界；若为此扩展 `ISecretManager` 新增 bootstrap create/set，又会污染 consumer-facing ABI。
3. 因此最小有效修复是：由 infra 提供 public live builder seam，runtime_support 只消费这个 seam，并把 resulting `std::shared_ptr<ISecretManager>` 保留在 shared runtime dependency surface 上。

### 3.2 seam 边界

1. 新增 `infra/include/secret/SecretManagerLiveComposition.h` 与 `infra/src/secret/SecretManagerLiveComposition.cpp`，对外暴露 `compose_live_secret_manager(secret_backend_type, options)`；当前只支持 profile-selected `file` backend。
2. builder 内部基于 install layout / `state_root_override` 组合 `FileSecretBackendOptions`，创建 `FileSecretBackend + SecretManagerFacade` 并返回 `std::shared_ptr<ISecretManager>`；`ISecretManager` ABI 未新增 create/set，bootstrap 写入继续只走 `SecretBootstrapWriter` internal seam。
3. `RuntimeDependencySet` 新增 `secret_manager` retention 面，`compose_minimal_live_dependency_set()` 现会在 daemon / gateway shared runtime composition 中保留 live secret manager，并写入 owner-level `secret-manager-live-seam` evidence。
4. 缺 secret root 时 builder 仍返回 seam object；实际 unavailable / provider-timeout 语义通过标准 `ISecretManager` 调用路径暴露，不 silent fallback 成 ready。
5. Access ownership seam 注入继续由 `INF-FIX-008` 持有；本轮不修改 daemon / gateway main 的 `ownership_secret_manager` wiring。

### 3.3 验证边界

1. 本轮 authoritative focused evidence 由 build-tree focused build 与 direct binary fallback 组成；未使用 qemu / kvm。
2. `RunCtest_CMakeTools` 对本仓库相关 tests 仍返回已知泛化“生成失败”，因此本轮继续沿用仓库既有 fallback：先 `Build_CMakeTools`，再直接执行已构建 binary 获取行为证据。
3. 行为证据不仅覆盖新 unit test，也覆盖 daemon / gateway shared runtime composition 对 secret seam retention 与 owner-level evidence 的回归。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | `ISecretManager` live builder 必须归 infra public seam，不能让 runtime_support include infra internal concrete headers | `infra/include/secret/SecretManagerLiveComposition.h`、`infra/src/secret/SecretManagerLiveComposition.cpp`、`infra/CMakeLists.txt` |
| D2 | shared runtime composition 必须保留 live secret manager seam，但不越权做 Access ownership 注入 | `runtime/include/RuntimeDependencySet.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` |
| D3 | missing root 语义必须沿标准 `ISecretManager` unavailable/provider-timeout 路径暴露 | `tests/unit/infra/secret/SecretManagerLiveCompositionTest.cpp` |
| D4 | daemon / gateway owner-level runtime composition 必须可断言 secret seam retention 与 evidence | `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp` |
| D5 | infrastructure 总账必须把 007 收口为 live composition seam 已闭合，并把 Access ownership 注入保留给 008 | `docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 5. Build 三件套

1. 代码目标：
   - 新增 infra public seam `SecretManagerLiveComposition`，按 install layout 与 profile-selected `secret_backend_type` 组合 `FileSecretBackend + SecretManagerFacade`。
   - 在 `RuntimeDependencySet` 与 `compose_minimal_live_dependency_set()` 中保留 live `ISecretManager` seam 和 owner-level evidence。
   - 新增 focused unit test，并扩展 daemon / gateway runtime composition tests 验证 seam retention。
2. 测试目标：
   - `dasall_secret_manager_live_composition_unit_test`
   - `dasall_access_daemon_runtime_live_dependency_composition_integration_test`
   - `dasall_access_gateway_runtime_live_dependency_composition_integration_test`
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_secret_manager_live_composition_unit_test","dasall_access_daemon_runtime_live_dependency_composition_integration_test","dasall_access_gateway_runtime_live_dependency_composition_integration_test"])`
   - `RunCtest_CMakeTools(tests=["SecretManagerLiveCompositionTest","DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest"])` 仍返回仓库已知泛化“生成失败”，故按 fallback 直接执行 `build/vscode-linux-ninja/tests/unit/infra/dasall_secret_manager_live_composition_unit_test`、`build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_runtime_live_dependency_composition_integration_test`、`build/vscode-linux-ninja/tests/integration/access/dasall_access_gateway_runtime_live_dependency_composition_integration_test`

## 6. Rollout Checklist

1. live builder 已固定在 infra public seam，而不是 runtime_support private helper。
2. `ISecretManager` ABI 未新增 create/set；bootstrap 写入仍停留在 `SecretBootstrapWriter` internal seam。
3. missing root 路径已由 focused unit test 验证为 unavailable/provider-timeout 语义，而非 builder 直接返回空指针。
4. daemon / gateway shared runtime composition 已能保留 `secret_manager` 与 owner-level `secret-manager-live-seam` evidence。
5. Access ownership seam 注入、secret consumer matrix 与 installed/package evidence 未被提前混入本轮。
6. 本轮未使用 qemu / kvm，也未把 build-tree focused evidence 外推为 installed/package ready。

## 7. 风险与回退

1. 若后续再次让 runtime_support include infra secret concrete headers，会重新破坏 public/private boundary，并把 007 回滚成编译期耦合缺口。
2. 若后续把 missing root 改成 builder 直接返回 `nullptr` 或 ready fallback，会破坏现有 `ISecretManager` unavailable/provider-timeout 语义，对 Access/LLM 等 consumer 形成误导。
3. 若后续把 `INF-FIX-008` Access ownership 注入重新混写进 007 文档，会模糊 live seam 与 consumer wiring 的职责边界。

## 8. D Gate

1. focused build 已通过：`dasall_secret_manager_live_composition_unit_test`、daemon runtime composition test、gateway runtime composition test 全部成功链接。
2. `RunCtest_CMakeTools` 仍命中仓库既有泛化“生成失败”；按仓库既有 fallback 直接执行三项 binary 后均退出 `0`。
3. `INF-FIX-007` 已把 SecretManager live composition seam 收口为 infra public builder、runtime dependency retention 与 owner-level focused evidence；`INF-FIX-008` / `INF-FIX-009` 继续持有 Access ownership wiring 与 consumer matrix。

结论：D Gate = PASS；`INF-FIX-007` 已以 non-qemu 的 build-tree focused evidence 收口。