# INF-FIX-008 daemon/gateway Access ownership secret seam 注入

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `INF-FIX-008`。
2. 本轮目标：把 shared runtime composition 已保留的 `std::shared_ptr<ISecretManager>` 注入 daemon / gateway 的 Access ownership seam，让 accepted_async ownership HMAC 在 production app composition root 上真正接线，而不是继续只靠测试手动注入。
3. 用户附加约束：按 `project-implementation-cycle` 串行推进；逐文件落盘；若验收口径含 qemu / kvm 必须改成 build-tree 或本机真实安装态口径；本轮不得把 bootstrap writer、consumer matrix 或 package/qemu 证据混入 `INF-FIX-009`。

## 2. 本地证据

1. `access/src/AccessGatewayFactory.cpp` 已经在 `resolve_async_task_registry()` 中固定了 ownership HMAC 的 fail-closed 语义：仅当 `ownership_token_hmac_secret_ref` 存在、且 `ownership_secret_manager != nullptr` 时才尝试 materialize secret；materialize 失败则返回空 key，最终在 accepted_async 路径 fail-closed 为 `ownership_secret_unavailable`。
2. `apps/daemon/src/main.cpp` 与 `apps/gateway/src/main.cpp` 当前都会构造 `DaemonAccessPipelineOptions` / `GatewayAccessPipelineOptions`，但尚未把 `runtime_init_request.request.dependency_set->secret_manager` 赋给 `ownership_secret_manager`，因此 app binary owner 还停留在“保留 seam，但未消费”。
3. `INF-FIX-007` 已把 `RuntimeDependencySet::secret_manager` 与 owner-level `secret-manager-live-seam` evidence 收口为 shared runtime composition 事实；本轮缺口只剩 app composition root 到 Access pipeline options 的最后一跳。
4. `AccessBootstrapConfig` 已冻结 `ownership_token_hmac_secret_ref` 为启动事实字段；因此本轮不需要新增 profile key，也不需要改 Access factory 的 gating 规则，只要补 owner 注入即可。

## 3. 外部参考

1. OWASP Secrets Management Cheat Sheet 强调 secret consumption 应遵循 centralized / standardized access、least privilege、auditing，以及“由 consumer 在运行期取 secret，而不是在 CI/CD 或代码里扩散 secret”。这与当前 DASALL 的目标一致：由 infra 提供标准 `ISecretManager` seam，由 app owner 在运行时把 seam 注入给真实 consumer，而不是让 Access 自造第二条 secret 通道。

## 4. 设计结论

### 4.1 根因收口

1. 本轮根因不是 secret manager 缺实现，也不是 Access accepted_async 缺 HMAC 逻辑，而是 daemon / gateway owner 没把既有 `RuntimeDependencySet::secret_manager` 传给 `ownership_secret_manager`。
2. 因为 `AccessGatewayFactory` 已经用 `ownership_token_hmac_secret_ref + ownership_secret_manager` 做联合 gating，最小正确修复不是改 factory，而是让 app main 在 production composition root 上显式接线。
3. 该接线应保持 fail-closed：若 profile / bootstrap 没声明 `ownership_token_hmac_secret_ref`，accepted_async 继续走无 HMAC registry；若声明了但 secret 无法 materialize，仍由既有逻辑拒绝并返回 `ownership_secret_unavailable`。

### 4.2 owner 边界

1. `apps/runtime_support::compose_minimal_live_dependency_set()` 继续只负责生成 shared runtime dependency facts；它不直接碰 Access pipeline options。
2. daemon / gateway `main.cpp` 继续是 app-level composition owner，负责把 `dependency_set->secret_manager` 投影到 `DaemonAccessPipelineOptions::ownership_secret_manager` / `GatewayAccessPipelineOptions::ownership_secret_manager`。
3. `ISecretManager` ABI 不扩展 create/set；bootstrap 写入仍只走 `SecretBootstrapWriter` internal seam，不混入本轮。

### 4.3 验证边界

1. focused regression 需要同时覆盖：
   - Access factory 仍能在缺 secret 时 fail-closed。
   - daemon app composition 会把 live `secret_manager` 投影到 Access options。
   - gateway app composition 会把 live `secret_manager` 投影到 Access options。
2. 本轮 authoritative evidence 采用 build-tree focused build 与 direct `ctest`，不使用 qemu / kvm。

## 5. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | app owner 必须把 shared runtime secret seam 注入 Access ownership seam | `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp` |
| D2 | 需要一个可复用的小型 app composition helper，避免 daemon/gateway 重复发明注入规则 | `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp` |
| D3 | Access factory 的 fail-closed 语义不能被回归打破 | `tests/unit/access/AsyncTaskRegistryMissingSecretFailClosedTest.cpp` |
| D4 | daemon / gateway 都要有 focused composition regression，证明 owner-level wiring 真的落到了 pipeline options | `tests/unit/access/DaemonAccessSecretCompositionTest.cpp`、`tests/unit/access/GatewayAccessSecretCompositionTest.cpp`、`tests/unit/access/CMakeLists.txt` |
| D5 | infrastructure 总账与执行记录要把 008 收口为 owner-level Access wiring done，并把 consumer matrix 继续留给 009 | `docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 6. Build 三件套

1. 代码目标：
   - 在 daemon / gateway app composition root 上把 `RuntimeDependencySet::secret_manager` 显式投影到 Access ownership seam。
   - 抽最小 helper，保证 daemon / gateway 共享同一条注入规则。
2. 测试目标：
   - `AsyncTaskRegistryMissingSecretFailClosedTest`
   - `DaemonAccessSecretCompositionTest`
   - `GatewayAccessSecretCompositionTest`
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_access_async_task_registry_missing_secret_fail_closed_unit_test","dasall_daemon_access_secret_composition_unit_test","dasall_gateway_access_secret_composition_unit_test"])`
   - `RunCtest_CMakeTools(tests=["AsyncTaskRegistryMissingSecretFailClosedTest","DaemonAccessSecretCompositionTest","GatewayAccessSecretCompositionTest"])`；若继续命中仓库既有泛化“生成失败”，按 fallback 直接执行 `build/vscode-linux-ninja/tests/unit/access/dasall_access_async_task_registry_missing_secret_fail_closed_unit_test`、`build/vscode-linux-ninja/tests/unit/access/dasall_daemon_access_secret_composition_unit_test`、`build/vscode-linux-ninja/tests/unit/access/dasall_gateway_access_secret_composition_unit_test`

## 7. 实施结果

1. 新增 header-only helper `apps/runtime_support/include/AccessOwnershipSecretWiring.h`，把“从 `RuntimeDependencySet` 投影 `secret_manager` 到 Access ownership seam”收敛成 app-level wiring 规则，而没有把 access 依赖反灌进 shared runtime composition 实现。
2. 更新 `apps/daemon/src/main.cpp` 与 `apps/gateway/src/main.cpp`：两处 app composition root 现在都显式调用该 helper，把 `runtime_init_request.request.dependency_set->secret_manager` 注入各自的 `ownership_secret_manager`。
3. 扩展 `tests/unit/access/AsyncTaskRegistryMissingSecretFailClosedTest.cpp`：新增“manager seam 存在但 secret record 缺失”负例，证明 owner-level 注入后仍旧 fail-closed 为 `ownership_secret_unavailable`。
4. 新增 `tests/unit/access/DaemonAccessSecretCompositionTest.cpp` 与 `tests/unit/access/GatewayAccessSecretCompositionTest.cpp`，并更新 `tests/unit/access/CMakeLists.txt`：focused 回归直接断言 daemon / gateway options 都会消费 runtime-composed secret seam，而不会在 dependency set 缺失时伪造 manager。

## 8. 验收结果

1. `Build_CMakeTools(buildTargets=["dasall_access_async_task_registry_missing_secret_fail_closed_unit_test","dasall_daemon_access_secret_composition_unit_test","dasall_gateway_access_secret_composition_unit_test"])`：通过。
2. `RunCtest_CMakeTools(tests=["AsyncTaskRegistryMissingSecretFailClosedTest","DaemonAccessSecretCompositionTest","GatewayAccessSecretCompositionTest"])`：仍命中仓库已知泛化“生成失败”，未提供 test-level 失败诊断。
3. fallback direct binaries：
   - `build/vscode-linux-ninja/tests/unit/access/dasall_access_async_task_registry_missing_secret_fail_closed_unit_test`
   - `build/vscode-linux-ninja/tests/unit/access/dasall_daemon_access_secret_composition_unit_test`
   - `build/vscode-linux-ninja/tests/unit/access/dasall_gateway_access_secret_composition_unit_test`
   - 结果：3 个 binary 全部退出 `0`。

## 9. 风险与回退

1. 若把注入逻辑放回 Access factory 内部，会模糊 app owner 与 access module 的边界，后续容易再次让 factory 偷读 runtime dependency facts。
2. 若让注入无条件启用而不复用现有 `ownership_token_hmac_secret_ref` gating，可能把“未声明 ownership secret 的 profile”误写成 mandatory secret 依赖。
3. 若本轮把 bootstrap writer / consumer matrix 一起改动，会把 `INF-FIX-008` 与 `INF-FIX-009` 职责重新混写。

## 10. Gate 结论

1. D Gate：PASS。根因、owner 边界、Design->Build 映射与非 qemu 验收口径已固定。
2. B Gate：PASS。app owner wiring、focused regression 与 fail-closed 回归都已落盘并通过 direct binary 验证。
3. 结论：`INF-FIX-008` 已完成；下一缺口继续前移到 `INF-FIX-009` secret consumer matrix 与 package evidence。