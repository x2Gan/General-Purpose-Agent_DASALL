# FULLINT-TODO-009 startup diagnostics stage 覆盖

日期：2026-05-11

范围：daemon/gateway startup diagnostics、app-binary failure stage、Gate-INT-10 release-preflight support

## 1. 结论

`FULLINT-TODO-009` 已完成。daemon/gateway startup diagnostics 不再只覆盖 daemon config-validation 与 gateway runtime-policy-load 两个早期失败点；当前 app-binary diagnostics 已覆盖 runtime dependency composition、runtime init、AccessGateway init、gateway listen 与 daemon listener-bind 失败分支。

本次补丁保留已有真实 config/profile 负路径，并新增两类稳定 fixture：

1. 明确的 diagnostics forced-stage 环境变量，用于在真实 daemon/gateway app binary 中触发 runtime composition / runtime init / AccessGateway init stage 输出，默认未设置时无行为。
2. 真实资源冲突 fixture：gateway 通过占用 TCP port 触发 `listen`；daemon 通过占用 Unix domain socket 触发 `listener-bind`。

## 2. 代码落点

| 文件 | 变化 |
|---|---|
| `apps/daemon/src/main.cpp` | 新增 `DASALL_DAEMON_STARTUP_DIAGNOSTICS_FORCE_STAGE` forced-stage fixture；当 `DaemonBootstrap::run()` 返回失败时输出统一 `stage=listener-bind`、`error_code=DAEMON_E_LISTENER_BIND_FAILED` |
| `apps/gateway/src/main.cpp` | 新增 `DASALL_GATEWAY_STARTUP_DIAGNOSTICS_FORCE_STAGE` forced-stage fixture，覆盖 `runtime-dependency-composition`、`runtime-init`、`access-gateway-init` |
| `tests/integration/access/DaemonStartupDiagnosticsTest.cpp` | 增加 runtime composition/init/AccessGateway init forced-stage 覆盖；增加 active Unix socket listener-bind 真实冲突覆盖；统一断言 startup prefix、stage、error_code、trace_id、assets/profile 路径字段 |
| `tests/integration/access/GatewayStartupDiagnosticsTest.cpp` | 增加 runtime composition/init/AccessGateway init forced-stage 覆盖；增加 occupied TCP port listen 真实冲突覆盖；统一断言 startup prefix、stage、error_code、trace_id、assets/profile 路径字段 |
| `docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md` | `FULLINT-TODO-009` 标记 Done；`FULLINT-BLK-005` 标记已解阻；BC-13 缺口改由后续 017 承接更广 hardening 负路径 |
| `docs/worklog/DASALL_开发执行记录.md` | 记录 #629 回写验证证据 |

## 3. Stage 覆盖矩阵

| Binary | Stage | Trigger | Expected error code | 关键断言 |
|---|---|---|---|---|
| daemon | `config-validation` | invalid socket parent | `DAEMON_E_CONFIG_VALIDATION_FAILED` | `trace_id=startup:daemon:config-validation`、`socket_path=`、config detail |
| daemon | `runtime-dependency-composition` | `DASALL_DAEMON_STARTUP_DIAGNOSTICS_FORCE_STAGE=runtime-dependency-composition` | `DAEMON_E_RUNTIME_COMPOSITION_FAILED` | startup prefix、stage、error_code、trace_id、asset/profile fields |
| daemon | `runtime-init` | `DASALL_DAEMON_STARTUP_DIAGNOSTICS_FORCE_STAGE=runtime-init` | `DAEMON_E_RUNTIME_INIT_FAILED` | startup prefix、stage、error_code、trace_id、forced diagnostics detail |
| daemon | `access-gateway-init` | `DASALL_DAEMON_STARTUP_DIAGNOSTICS_FORCE_STAGE=access-gateway-init` | `DAEMON_E_ACCESS_GATEWAY_INIT_FAILED` | startup prefix、stage、error_code、trace_id、asset/profile fields |
| daemon | `listener-bind` | active Unix domain socket on requested path | `DAEMON_E_LISTENER_BIND_FAILED` | `trace_id=startup:daemon:listener-bind`、active socket bind-preflight detail |
| gateway | `runtime-policy-load` | empty `--profile-id=` | `PRF_E_SCHEMA_INVALID` | `trace_id=startup:gateway:runtime-policy-load`、requested profile slot、listen port |
| gateway | `runtime-dependency-composition` | `DASALL_GATEWAY_STARTUP_DIAGNOSTICS_FORCE_STAGE=runtime-dependency-composition` | `GATEWAY_E_RUNTIME_COMPOSITION_FAILED` | startup prefix、stage、error_code、trace_id、asset/profile fields |
| gateway | `runtime-init` | `DASALL_GATEWAY_STARTUP_DIAGNOSTICS_FORCE_STAGE=runtime-init` | `GATEWAY_E_RUNTIME_INIT_FAILED` | startup prefix、stage、error_code、trace_id、forced diagnostics detail |
| gateway | `access-gateway-init` | `DASALL_GATEWAY_STARTUP_DIAGNOSTICS_FORCE_STAGE=access-gateway-init` | `GATEWAY_E_ACCESS_GATEWAY_INIT_FAILED` | startup prefix、stage、error_code、trace_id、asset/profile fields |
| gateway | `listen` | occupied TCP port | `GATEWAY_E_LISTEN_FAILED` | `trace_id=startup:gateway:listen`、`listen_port=`、listen failure detail |

## 4. 验收证据

1. `Build_CMakeTools(buildTargets=["dasall_access_daemon_startup_diagnostics_test","dasall_access_gateway_startup_diagnostics_test"])`
   - 结果：通过。
   - 证据：daemon/gateway startup diagnostics test binaries 均完成构建并执行通过。
2. `RunCtest_CMakeTools(tests=["DaemonStartupDiagnosticsTest","GatewayStartupDiagnosticsTest"])`
   - 结果：通过；`DaemonStartupDiagnosticsTest` 与 `GatewayStartupDiagnosticsTest` 均 passed。
3. `get_errors` on changed C++ files
   - 结果：`apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`、`tests/integration/access/DaemonStartupDiagnosticsTest.cpp`、`tests/integration/access/GatewayStartupDiagnosticsTest.cpp` 均无 VS Code diagnostics。

## 5. 边界

本任务证明的是 build-tree app-binary / focused diagnostics 层面的 failure-stage 可观测性，不外推为 installed-package、qemu 或 production release-ready。更广 Access / Infra release hardening 负路径仍由 `FULLINT-TODO-017` 承接。

forced-stage 环境变量只用于显式 diagnostics fixture；默认环境下 daemon/gateway 启动行为不变。listen/bind 分支使用真实端口和 Unix socket 冲突验证，避免只靠文档或历史测试结论。