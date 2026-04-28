# DMD-TODO-032 UDS endpoint 权限与 stale socket 策略收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务收敛 daemon UDS endpoint 的 socket_path 权限、父目录安全、owner/group/mode 校验、stale socket 检测与 bind 前清理策略。
2. 只覆盖 pathname sockets，不支持 abstract namespace。
3. 只做 bind 前静态校验与一次性 stale socket 清理，不扩展到 runtime 动态权限变更或 SELinux/label 策略。

## 2. 设计与实现

### 2.1 权限与安全依据

- 参考 [unix(7) man page](https://man7.org/linux/man-pages/man7/unix.7.html)：
  - Pathname sockets 的权限由父目录决定，必须有写和执行权限。
  - 连接到 stream socket 需要对 socket 文件有写权限。
  - socket 文件的 owner/group 由创建进程决定，权限受 umask 影响，后续可用 chmod/chown 调整。
  - stale socket 必须在 bind 前由 daemon 主动检测并安全清理，不能误删非本进程 owner 的活动 socket。
  - 见 man7.org unix(7) “Pathname socket ownership and permissions” 节。
- Docker 等安全基线建议本地 daemon UDS 必须限制在 root-only 或专用 service 账户目录下，避免 world-writable 目录和 0666 权限。

### 2.2 策略实现

1. 新增 `apps/daemon/src/DaemonSocketPolicy.{h,cpp}`，定义 `DaemonSocketPolicy`、`validate_socket_path()`、`preflight_bind_endpoint()`、`try_cleanup_stale_socket()`，实现如下：
   - socket_path 必须为绝对路径，且不得包含 `.` 或 `..` 路径穿越分量。
   - 父目录必须为当前进程 owner/group，且 mode 至少 0700，不得 world-writable。
   - socket 文件必须为 0600 且 owner/group 匹配，stale socket 仅在 liveness probe 失败且 owner/mode 匹配时清理。
   - 活动 socket（能 connect 成功）不得被清理，直接报 AddressInUse。
2. validator 入口接入 validate_socket_path，listener bind 入口接入 preflight_bind_endpoint 和 try_cleanup_stale_socket。
3. 单测覆盖 helper 规则、bind conflict、stale socket 清理、父目录权限等正负例。

## 3. 验证

1. `DaemonSocketPolicyTest` 覆盖路径校验、父目录权限、stale socket 清理、mode/owner 拒绝等。
2. `DaemonListenerHostBindConflictTest` 覆盖活动 socket 拒绝与 stale socket 清理后重试。
3. `DaemonConfigValidatorTest` 间接覆盖 socket_path 校验。
4. `DaemonListenerHostTest` 回归通过，证明 listener 主流程未被破坏。
5. `dasall_daemon` 编译通过。

## 4. 参考资料

- [unix(7) man page](https://man7.org/linux/man-pages/man7/unix.7.html)
- Docker 官方安全建议：https://docs.docker.com/engine/security/#docker-daemon-attack-surface

## 5. 完成判定

1. daemon UDS endpoint 权限、父目录安全、stale socket 清理策略已收敛，所有正负例均有单测覆盖。
2. 只要 socket_path 不再依赖硬编码 /tmp，且 bind conflict/stale cleanup/permission denied 都有稳定错误与审计事实，即可判定本轮完成。