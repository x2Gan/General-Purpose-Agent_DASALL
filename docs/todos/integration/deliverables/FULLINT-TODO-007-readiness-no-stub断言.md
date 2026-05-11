# FULLINT-TODO-007 readiness no-stub 断言

日期：2026-05-11

范围：daemon/gateway readiness 投影、Gate-INT-10 app-binary no-stub、build-tree secret permission fail-closed

## 1. 结论

`FULLINT-TODO-007` 已完成。daemon/gateway 不再把 runtime entrypoint `accepted` 直接偷换为对外 ready；`default-ready`、`degraded-ready`、`stub-ready` 已经在 health/readiness 与 app-binary smoke 中分层可观测。

本轮同时修复 Gate-INT-10 暴露的真实 blocker：普通用户执行 build-tree app-binary 时，live LLM manager 访问 `/var/lib/dasall/secrets` 可能遇到 permission denied。`FileSecretBackend` 已改为 error-code based path probing，权限受限时返回 backend unavailable / not found 失败结果，daemon/gateway 不崩溃、不回退 stub。

## 2. 代码落点

| 层级 | 文件 | 变化 |
|---|---|---|
| Access pipeline | `access/include/AccessGatewayFactory.h`、`access/src/AccessGatewayFactory.cpp` | `DaemonAccessPipelineOptions` 增加 `daemon_runtime_readiness_label`；readiness payload 输出 `runtime_readiness`；`stub-ready` 令 bridge unreachable；`degraded-ready` 显式记录 degraded reason |
| Daemon health | `access/include/daemon/DaemonHealthTypes.h`、`access/src/daemon/DaemonHealthService.*` | `DaemonHealthInput` / `DaemonReadinessSnapshot` 保留 `runtime_readiness_label` |
| App roots | `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp` | daemon/gateway 从 `AgentInitResult::readiness_label()` 投影 readiness；`stub_ready()` 不再对外 ready |
| Gateway probe | `apps/gateway/src/HealthProbeHandler.*` | `/health/ready` 可输出 `runtime_readiness=<label>` detail |
| Secret backend | `infra/src/secret/backends/FileSecretBackend.cpp` | root/path probing 改为 `std::error_code` 路径，权限错误不抛 `std::filesystem::filesystem_error` |
| Tests | `tests/unit/access/*`、`tests/integration/access/*BinaryUnarySmokeTest.cpp` | 覆盖 degraded readiness、no-stub 日志/输出、build-tree secret unavailable fail-closed |

## 3. 行为矩阵

| 场景 | 预期 |
|---|---|
| runtime `default-ready` | daemon/gateway 可以对外 ready，并保留 `runtime_readiness=default-ready` |
| runtime `degraded-ready` | daemon readiness 返回 DEGRADED / 200；gateway `/health/ready` 返回 READY 且带 `runtime_readiness=degraded-ready`；不得宣称 production default-ready |
| runtime `stub-ready` | bridge 不可达，health/readiness 不得伪装 ready；binary smoke 不得出现 `stub-ready` / `stub_runtime_path` |
| build-tree 无 `/var/lib/dasall/secrets` 权限 | daemon CLI run fail-closed 为 `task_not_completed`；gateway submit 保持受控响应；进程不崩溃；不回退 `agent.dataset` stub |

## 4. 验收证据

1. `Build_CMakeTools(buildTargets=["dasall_gate_int_10"])`
   - 结果：通过。
   - 证据：expected tests discoverability 覆盖 `CliDaemonSocketPathIntegrationTest`、`DaemonBinaryUnarySmokeTest`、`GatewayBinaryUnarySmokeTest`、`GatewayBinaryMissingBackendRegressionTest`；`gate-int-10` / `release-preflight-gate` label discoverability 通过；acceptance passed。
2. `Build_CMakeTools(buildTargets=["dasall_runtime_agent_init_result_readiness_unit_test","dasall_gate_int_06","dasall_gate_int_10","dasall_file_secret_backend_unit_test"])`
   - 结果：通过。
   - 证据：Gate-INT-06 的 `RuntimeRequiredOptionalPortsIntegrationTest`、`RuntimeProfileCompatibilityTest`、`LLMSubsystemSmokeIntegrationTest` 均通过；Gate-INT-10 acceptance 通过。
3. `RunCtest_CMakeTools(tests=["AgentInitResultReadinessTest","FileSecretBackendTest","DaemonHealthServiceTest","DaemonReadinessCommandTest"])`
   - 结果：通过。

## 5. 边界

本交付物证明 build-tree app-binary readiness/no-stub 与权限受限 secret backend 的 fail-closed 行为，不替代 installed-package rootful LLM smoke、qemu `autopkgtest` 或 release runner secret injection 证据。发布级结论仍需由后续 release runner 任务复跑并归档。