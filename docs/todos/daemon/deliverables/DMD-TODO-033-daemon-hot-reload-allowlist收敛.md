# DMD-TODO-033 daemon hot-reload allowlist 收敛

完成时间：2026-05-03

## 任务结论

DMD-TODO-033 已完成，SIGHUP 热重载已从“宽 allowlist”进一步收敛为“`daemon.diag_enabled` 单键 allowlist + 快照切换 + 拒绝审计”的可验证实现。

1. `DaemonConfigReloader` 当前只允许 `daemon.diag_enabled` 进入运行期 apply；`daemon.log_level`、`daemon.log_format`、`daemon.watchdog_enabled`、`daemon.receipt_ttl_sec`、`daemon.override_enabled` 已统一回到启动期语义。
2. 对非 allowlisted 键（包括 `daemon.socket_path`、`daemon.listen_backlog`、`daemon.startup_mode`、`daemon.dispatch_workers`、`daemon.log_format` 等）执行拒绝并保留 `last_known_good_snapshot`。
3. `main.cpp` 在收到 SIGHUP 时接入 reloader apply 路径，拒绝场景输出 `daemon.reload.denied` 审计日志字段。
4. 扩展 `AccessObservabilityBridge` 增加 `emit_reload_denied()`，并在 `DaemonObservabilityFieldSetTest` 固定事件字段集。

## 代码改动

1. 新增 `apps/daemon/src/DaemonConfigReloader.h`
2. 新增 `apps/daemon/src/DaemonConfigReloader.cpp`
3. 更新 `apps/daemon/src/main.cpp`
   - 接入 SIGHUP reload apply 逻辑与拒绝审计输出
4. 更新 `access/src/AccessObservabilityBridge.h`
   - 新增 `emit_reload_denied()`
5. 更新 `access/src/AccessObservabilityBridge.cpp`
   - 实现 `daemon.reload.denied` 事件
6. 更新 `tests/unit/apps/daemon/DaemonConfigReloadTest.cpp`
   - 正向断言仅 `daemon.diag_enabled` 可热更，反向断言 `log_format` / `watchdog_enabled` / `receipt_ttl_sec` / `override_enabled` 全部拒绝
7. 更新 `tests/unit/apps/daemon/CMakeLists.txt`
   - 注册 `DaemonConfigReloadTest`
8. 更新 `tests/unit/access/DaemonObservabilityFieldSetTest.cpp`
    - 覆盖 reload denied 审计字段
9. 更新 `apps/daemon/CMakeLists.txt`
    - 接入 `DaemonConfigReloader` 编译

## 验收

1. `Build_CMakeTools`：通过。
2. `RunCtest_CMakeTools(tests=["DaemonConfigReloadTest","DaemonSignalHandlerTest","DaemonObservabilityFieldSetTest"])`：通过（3/3）；`DaemonConfigReloadTest` 已证明只有 `daemon.diag_enabled` 保持 hot-reload allowlist 身份。

## 完成判定对照

1. SIGHUP 不重启 listener：已满足（reload 仅走快照 apply，不触达 bind/restart 路径）。
2. 不改 socket_path：已满足（`daemon.socket_path` 属于 restart-only，热更拒绝）。
3. `daemon.diag_enabled` 以外的 daemon 键不再假装 allowlisted：已满足（`log_format` / `watchdog_enabled` / `receipt_ttl_sec` / `override_enabled` 均已收窄为拒绝路径）。
4. 热更失败不污染运行快照：已满足（拒绝后保留 active 与 last-known-good）。
5. override enable 默认关闭：已满足（`DaemonBootstrapConfig.override_enabled=false` 默认值保留，且有测试覆盖）。
