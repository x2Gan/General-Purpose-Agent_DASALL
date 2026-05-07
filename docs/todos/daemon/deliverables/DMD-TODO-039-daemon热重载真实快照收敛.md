# DMD-TODO-039 daemon 热重载真实快照收敛

状态：Done
日期：2026-05-03
来源 TODO：docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只收敛 SIGHUP 的 fresh snapshot source 与最小可观测 allowlisted reload，不扩张到更多 daemon mutable key 的 live rebind。
2. 本任务复用 DMD-TODO-038 的 `DaemonEntryConfigLoader` 作为 reload candidate source，不重新发明第二套 config file/profile 解析链。
3. 本任务把 daemon live-reload surface 明确收敛为 `diag_enabled` 单键；`log_format`、`watchdog_enabled`、`receipt_ttl_sec`、`override_enabled` 等键不再保留“名义 allowlisted、运行态无消费者”的模糊状态。

## 2. 根因与设计结论

### 2.1 回归根因

1. `main.cpp` 在收到 `SIGHUP` 时一直重复 `apply_reload_snapshot(initial bootstrap config)`，没有 fresh snapshot source，因此外部修改 `--config-file` 根本不会进入进程。
2. access gateway 把 `daemon_diagnostics_enabled` 按值 capture 到 `DaemonDiagnosticsHandler` 与 ping/readiness 路径里，导致即便 reload 之后 `DaemonConfigReloader` 的内部快照更新，运行中的 daemon 行为也不可见。
3. 因此 DMD-TODO-033 只证明了 allowlist helper/reloader 本身有效，没有证明真实 daemon process 能通过相同 source 观察到热更结果。

### 2.2 本轮收敛结论

1. `SIGHUP` 必须复用 DMD-TODO-038 的同一个 `DaemonEntryConfigLoadRequest`，重新读取 profile/config snapshot，而不是重复 apply 初始值。
2. allowlisted key 至少需要一条真实可观测路径；本轮选 `diag_enabled`，通过 access gateway 的共享状态接缝让 running daemon 在 reload 后立即改变 diagnostics gate。
3. restart-only key 继续由 `DaemonConfigReloader` 拒绝，并保留 `reload_rejected_restart_only_keys` 这一稳定 reason code。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| SIGHUP 需要读取 fresh snapshot | `apps/daemon/src/main.cpp` | reload 路径重新调用 `DaemonEntryConfigLoader`，不再重复 apply 初始 config |
| allowlisted key 需要真实运行态可观察效果 | `access/include/AccessGatewayFactory.h`、`access/src/AccessGatewayFactory.cpp`、`access/src/daemon/DaemonDiagnosticsHandler.*` | `diag_enabled` reload 后 diagnostics command 从 `diag_disabled` 变为 completed |
| 非 `daemon.diag_enabled` 键继续拒绝并审计 | `apps/daemon/src/DaemonConfigReloader.cpp` + main reload audit projection | `DaemonHotReloadIntegrationTest` 断言 `daemon.log_format` 与 `daemon.socket_path` 都会被拒绝且 audit reason 稳定 |
| 文档需要反映 fresh reload source | `docs/deploy/daemon/README.md`、`ACCEPTANCE_CHECKLIST.md` | README/验收清单明确 `SIGHUP` 会重读当前 profile/config snapshot |

## 4. 落盘结果

1. 更新 `apps/daemon/src/main.cpp`：
   - 固化单一 `DaemonEntryConfigLoadRequest`；
   - 初始启动与 `SIGHUP` reload 共用同一 entry loader；
   - reload 时若 fresh candidate 不可用或出现 config conflict，会输出稳定的 `daemon.reload.denied` 审计事实；
   - reload 成功后同步更新运行中 daemon 的 diagnostics gate 状态。
2. 更新 `access/include/AccessGatewayFactory.h`，新增 `daemon_diagnostics_enabled_state` 共享状态接缝。
3. 更新 `access/src/AccessGatewayFactory.cpp` 与 `access/src/daemon/DaemonDiagnosticsHandler.*`：
   - diagnostics handler 不再只看 construction-time bool；
   - ping/readiness 与 diagnostics command 都会读取当前共享 diagnostics gate 状态。
4. 更新 `tests/integration/access/DaemonHotReloadIntegrationTest.cpp`：
   - 通过同一个 `DaemonEntryConfigLoadRequest` 反复读取 YAML config file；
   - 先验证 `diag_enabled=false` 时 diagnostics command 被 `diag_disabled` 拒绝；
   - 再修改 config file 为 `diag_enabled=true`，验证 allowlisted reload 后 diagnostics command 立即成功；
   - 再修改 `log_format`，验证 formerly-allowlisted key 已收窄为拒绝路径；
   - 最后修改 `socket_path`，验证 restart-only key 继续被拒绝并保留稳定 audit reason。
5. 更新 `tests/integration/access/CMakeLists.txt` 与 `tests/unit/apps/daemon/CMakeLists.txt`，补齐 `DaemonHotReloadIntegrationTest` 与 `DaemonConfigReloadTest` 的编译接线。
6. 更新 `docs/deploy/daemon/README.md` 与 `ACCEPTANCE_CHECKLIST.md`，同步 fresh reload snapshot 语义。

## 5. Validation

1. `Build_CMakeTools(buildTargets=["dasall-daemon","dasall-daemon_signal_handler_unit_test","dasall-daemon_config_reload_unit_test","dasall_access_daemon_hot_reload_integration_test","dasall_access_daemon_observability_field_set_unit_test"])`
2. `ctest --test-dir build-ci -R "DaemonConfigReloadTest|DaemonHotReloadIntegrationTest|DaemonSignalHandlerTest|DaemonObservabilityFieldSetTest" --output-on-failure`

结果摘要：

1. `DaemonConfigReloadTest` 继续通过，说明 allowlist / restart-only / last-known-good 规则保持稳定。
2. `DaemonHotReloadIntegrationTest` 通过，证明 daemon 现在会通过同一 entry request 读取 fresh snapshot，且 `diag_enabled` reload 后可被运行中的 gateway 立即观察到；`daemon.log_format`、`daemon.socket_path` 则会继续被拒绝。
3. `DaemonSignalHandlerTest` 继续通过，说明 `SIGTERM` / `SIGHUP` 意图采集没有回归。
4. `DaemonObservabilityFieldSetTest` 继续通过，说明 reload denied 的字段集与 `reload_rejected_restart_only_keys` 等稳定 reason code 没有漂移。

## 6. 完成判定

DMD-TODO-039 已完成。判定依据：

1. `SIGHUP` 不再重复 apply 初始 config，而是重新读取当前 profile/config snapshot。
2. `diag_enabled` 已成为真实运行态可观察的唯一 allowlisted key，证明 reload 结果不再停留在 helper 内部快照。
3. `daemon.log_format`、`daemon.socket_path` 等非 allowlisted key 继续拒绝，并保留稳定审计 reason。
4. 039 已为 040 的文档/证据复验提供真实 hot-reload gate，不再依赖“helper 有能力但 process 不可观察”的模糊口径。