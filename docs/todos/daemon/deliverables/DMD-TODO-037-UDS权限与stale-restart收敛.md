# DMD-TODO-037 UDS 权限与 stale restart 收敛

状态：Done
日期：2026-05-02
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只收敛真实 UDS socket mode 与 stale restart 恢复路径，不提前扩张到 profile/config loader 或 hot-reload snapshot source。
2. 本任务复用 DMD-TODO-032 的 socket policy 与 DMD-TODO-035 的真实 OS 级 `UnixIpcProvider` 基线，不重写 daemon endpoint policy 或 deployment contract。
3. 本任务不处理陈旧文档/历史 send-only smoke 清理；这些仍留给 DMD-TODO-040。

## 2. 根因与设计结论

### 2.1 回归根因

1. `DaemonSocketPolicy::try_cleanup_stale_socket()` 只允许清理 owner/group 匹配且 mode=0600 的 stale socket。
2. 真实 `UnixIpcProvider::listen()` 创建 filesystem socket 后没有主动收敛权限，导致实际文件 mode 受进程默认 umask 影响，当前实测为 0755。
3. 因此 daemon 正常启动虽然能工作，但若异常退出留下 stale socket，下一次 bind preflight 会因为 mode mismatch 拒绝清理，形成 restart-safe 回归。

### 2.2 本轮收敛结论

1. socket mode 必须在最靠近创建点的 `UnixIpcProvider::listen()` 上立即收紧，不能依赖外层 caller 或 deployment 假设补救。
2. 一旦真实 socket mode 与 policy 对齐，DMD-TODO-032 已冻结的 stale cleanup 规则即可直接恢复 restart-safe 行为，无需新增第二套清理逻辑。
3. focused gate 需要同时覆盖三层：platform mode/unlink、daemon runtime mode、daemon stale restart recovery。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| 真实 socket mode 必须在创建点收紧 | `platform/src/linux/UnixIpcProvider.cpp` | `UnixIpcProviderTest` 断言 mode=0600 |
| graceful close 必须 unlink filesystem socket | `UnixIpcProvider::close()` 的 listener close path | `UnixIpcProviderTest` / `DaemonSocketModeIntegrationTest` 断言 stop 后路径消失 |
| daemon 真实运行态必须创建 0600 socket | `tests/integration/access/DaemonSocketModeIntegrationTest.cpp` | daemon 启动后 `lstat()` mode=0600 且 ping 可用 |
| stale socket restart 必须恢复 | `tests/integration/access/DaemonStaleSocketRecoveryIntegrationTest.cpp` | owned stale socket 被清理后 daemon 可重新 ping |

## 4. 落盘结果

1. 更新 `platform/src/linux/UnixIpcProvider.cpp`：
   - 在非 abstract namespace UDS `bind()` 成功后立即对 socket path 执行 `chmod(..., 0600)`。
   - 若权限收紧失败，立即 `unlink + close` 并返回失败，避免把不安全 mode 的 socket 暴露给上层。
2. 更新 `tests/unit/platform/linux/UnixIpcProviderTest.cpp`：
   - 新增真实 socket mode 校验，断言 `listen()` 后 filesystem socket 的 mode 为 0600。
   - 新增 close/unlink 校验，断言 listener close 会移除 socket path。
3. 新增 `tests/integration/access/DaemonSocketIntegrationSupport.h`，提供 037 两条集成烟测共用的：
   - 受控临时目录；
   - native stale socket 构造 helper；
   - ping-only daemon pipeline options；
   - socket mode stat helper。
4. 新增 `tests/integration/access/DaemonSocketModeIntegrationTest.cpp`：
   - 启动真实 daemon fixture；
   - 断言运行中 socket mode=0600；
   - 断言 clean stop 后 socket path 被 unlink。
5. 新增 `tests/integration/access/DaemonStaleSocketRecoveryIntegrationTest.cpp`：
   - 先构造 owner/mode 匹配的 stale socket；
   - 再启动 daemon 验证 preflight cleanup + restart ping 恢复。
6. 更新 `tests/integration/access/CMakeLists.txt`，注册 `DaemonSocketModeIntegrationTest` 与 `DaemonStaleSocketRecoveryIntegrationTest` focused targets。

## 5. Validation

1. `cmake --build build-ci --target dasall_unix_ipc_provider_unit_test && ctest --test-dir build-ci -R "^UnixIpcProviderTest$" --output-on-failure`
2. `cmake --build build-ci --target dasall_daemon dasall_daemon_socket_policy_unit_test dasall_unix_ipc_provider_unit_test dasall_access_daemon_ping_integration_test dasall_access_daemon_socket_mode_integration_test dasall_access_daemon_stale_socket_recovery_integration_test && ctest --test-dir build-ci -R "DaemonSocketPolicyTest|UnixIpcProviderTest|DaemonSocketModeIntegrationTest|DaemonStaleSocketRecoveryIntegrationTest|DaemonPingIntegrationTest" --output-on-failure`

结果摘要：

1. `UnixIpcProviderTest` 通过，证明真实 listener socket 已在创建点收紧到 0600，且 close 会 unlink filesystem path。
2. `DaemonSocketPolicyTest` 继续通过，说明 032 的 stale cleanup 规则无需放宽即可兼容新的真实 socket mode。
3. `DaemonSocketModeIntegrationTest` 通过，证明 daemon 运行态创建的真实 socket 与 policy 对齐，且 clean stop 会清理 socket path。
4. `DaemonStaleSocketRecoveryIntegrationTest` 通过，证明 owner/mode 匹配的 stale socket 可以被清理，daemon restart 后 ping 恢复。
5. `DaemonPingIntegrationTest` 继续通过，说明 mode 收紧没有回归 daemon 正向 roundtrip。

## 6. 完成判定

DMD-TODO-037 已完成。判定依据：

1. daemon 实际创建的 filesystem socket mode 已与 policy 期望值 0600 对齐。
2. graceful close 与 clean stop 都会 unlink socket path，不再留下正常退出残留。
3. owner/mode 匹配的 stale socket 现在可被 daemon 启动前安全回收，restart smoke 已验证恢复路径。
4. 037 的 focused gate 已为 038/039 提供稳定运行面，不再让入口 loader/reload 复验落在错误的 socket 权限基线上。