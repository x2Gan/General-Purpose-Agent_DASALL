# DASALL daemon v1 部署与 supervisor 交付契约

更新时间：2026-05-02
适用范围：DASALL 本地控制面 direct-bind v1 交付

## 1. 目的与范围

本目录冻结 DASALL daemon 作为本地控制面的 v1 部署契约。目标不是新增 daemon 能力，而是把当前仓库已经实现并验证过的运行约束、systemd/supervisor 接入方式、socket 权限面、validate-only 操作和基本 smoke 流程收敛为可执行运维文档。

本契约只覆盖以下 v1 交付范围：

1. direct-bind Unix Domain Socket 本地监听。
2. `--validate-only`、`--socket-path`、`--profile-id` 与 `--config-file` 四个受控入口参数。
3. `SIGTERM` 优雅关闭与 `SIGHUP` allowlist hot-reload intent。
4. supervisor 最小契约：ready/stopping/watchdog 语义在 daemon 内部已冻结为 no-op 或 `IWatchdogService` bridge，但不对外承诺 `sd_notify()`、`Type=notify` 或 socket activation fd import。

以下内容明确不属于 v1 交付：

1. systemd socket activation。
2. `sd_notify()` readiness 通知。
4. 远程 TCP/HTTP 控制面。

## 2. 本地证据与契约来源

### 2.1 仓库内证据

1. [apps/daemon/src/main.cpp](../../../apps/daemon/src/main.cpp) 已通过 `DaemonEntryConfigLoader` 接入默认 `desktop_full` profile、`--profile-id`、`--config-file` 与 `--socket-path` override，并在 validate-only 成功时直接退出，不创建 listener。
2. [apps/daemon/src/DaemonConfigValidator.cpp](../../../apps/daemon/src/DaemonConfigValidator.cpp) 已冻结 socket path、payload 上限、flags/config conflict 与 reload key 校验语义。
3. [apps/daemon/src/DaemonBootstrap.cpp](../../../apps/daemon/src/DaemonBootstrap.cpp) 已实现 direct-bind listener、ready 后 accept loop、`SIGTERM` 停机排空与 supervisor adapter 接线。
4. [apps/daemon/src/DaemonSupervisorAdapter.cpp](../../../apps/daemon/src/DaemonSupervisorAdapter.cpp) 已冻结 v1 supervisor seam：`notify_ready()`、`notify_stopping()`、`tick_watchdog()`。
5. [tests/integration/access/DaemonPingIntegrationTest.cpp](../../../tests/integration/access/DaemonPingIntegrationTest.cpp) 已证明 daemon fixture 可真实消费 ping frame 并返回 completed response。
6. [docs/architecture/DASALL_daemon本地控制面详细设计.md](../../../docs/architecture/DASALL_daemon本地控制面详细设计.md) 6.9.3、10、11 已把 supervisor/watchdog/socket activation 演进边界写清。

### 2.2 外部参考

1. Docker daemon 官方文档强调“配置文件优先、flags 仅作显式覆盖，同一键重复出现时应拒绝启动”，本契约沿用该治理原则；当前 DASALL v1 已落地 `--validate-only`、`--socket-path`、`--profile-id` 与 `--config-file` 四个最小入口 surface。
2. 行业守护进程部署惯例要求 service 文件显式声明启动、停止、重载和运行目录权限；DASALL v1 采用 `Type=simple` direct-bind 模式，不虚构 `notify` 语义。

## 3. 运行契约总表

| 项目 | v1 契约 | 说明 |
|---|---|---|
| 启动模式 | direct-bind only | `DaemonStartupMode::SocketActivated` 仅保留类型枚举，不是当前交付模式 |
| 监听面 | UDS 本地 socket | 默认值见 [access/include/daemon/DaemonEndpointDefaults.h](../../../access/include/daemon/DaemonEndpointDefaults.h) 与 [apps/daemon/src/DaemonConfig.h](../../../apps/daemon/src/DaemonConfig.h)，部署建议改为 `/run/dasall/control.sock` |
| 参数 | `--validate-only`、`--socket-path`、`--profile-id`、`--config-file` | 默认 profile 为 `desktop_full`；CLI 只暴露入口选择与显式 override，不把全部 daemon 键摊平成 flags |
| 配置文件 | 通过 `--config-file` 受控读取 YAML/JSON deployment snapshot | flags 与 config file 同键冲突时拒绝启动，而不是静默取其一 |
| readiness | 通过 daemon command router 返回 JSON 响应 | CLI 现已消费 ping/readiness 响应；可走默认 socket_path 或 `--socket-path` 覆盖 |
| graceful stop | `SIGTERM` | daemon 进入 Draining，拒绝新请求并排空 inflight |
| reload | `SIGHUP` | 重新读取当前 `--profile-id` / `--config-file` 对应的 fresh snapshot；只允许 allowlist 键热更，restart-only key 保持拒绝并审计 |
| watchdog | no-op 或内部 watchdog bridge | 不承诺 systemd `WatchdogSec=` 对接 |
| socket activation | v2 范围外 | 当前不交付 fd import，也不建议配置 `.socket` 单元 |

## 4. 目录内交付物

1. [docs/deploy/daemon/dasall-daemon.service](dasall-daemon.service)：direct-bind v1 的 systemd service 示例。
2. [docs/deploy/daemon/daemon.example.json](daemon.example.json)：部署视角 JSON 样例。
3. [docs/deploy/daemon/daemon.example.yaml](daemon.example.yaml)：部署视角 YAML 样例。
4. [docs/deploy/daemon/ACCEPTANCE_CHECKLIST.md](ACCEPTANCE_CHECKLIST.md)：本地运维验收清单。

## 5. socket 与权限约定

1. 生产部署建议使用 `/run/dasall/control.sock` 或同级受控运行目录，不建议继续使用 `/tmp` 默认值。
2. socket 父目录必须由受控用户/组持有，且不得 world-writable；daemon validator 已对不安全目录 fail-closed。
3. systemd service 建议通过 `RuntimeDirectory=dasall` 与 `RuntimeDirectoryMode=0750` 创建运行目录，并配合 `UMask=007` 收敛 socket 权限。
4. stale socket 清理只允许发生在当前 daemon 有权判断为安全可回收的路径上；不得人工删除不明活动 socket。

## 6. supervisor 交付契约

### 6.1 v1 支持内容

1. service manager 负责拉起、停止与重启 daemon 进程。
2. daemon 内部提供 `notify_ready()`、`notify_stopping()`、`tick_watchdog()` 的最小 adapter seam。
3. 若部署侧注入 `IWatchdogService`，watchdog 事件由内部 bridge 记录；若未注入，则所有 supervisor 调用均为 no-op success。

### 6.2 v1 不支持内容

1. 不使用 `Type=notify`。
2. 不发送 `sd_notify(READY=1|STOPPING=1|WATCHDOG=1)`。
3. 不配置 `.socket` 单元，也不依赖 socket activation。

### 6.3 推荐 service 语义

1. 使用 `Type=simple`。
2. 使用 `ExecStartPre=... --validate-only ...` 做启动前校验。
3. 使用 `ExecReload=/bin/kill -HUP $MAINPID` 暴露 allowlist reload。
4. 使用 `ExecStop=/bin/kill -TERM $MAINPID` 触发 graceful shutdown。
5. 使用 `Restart=on-failure`，不要把 watchdog 恢复裁定下沉到 daemon 内部。

## 7. 运维 smoke 命令

以下命令假定项目根目录为 `/home/gangan/DASALL`，并已完成构建：

```bash
cmake -S . -B build-ci -G "Unix Makefiles"
cmake --build build-ci --target dasall_daemon dasall_cli
```

### 7.1 validate-only

```bash
mkdir -p /tmp/dasall-dmd035
chmod 700 /tmp/dasall-dmd035

./build-ci/apps/daemon/dasall_daemon \
  --validate-only \
  --socket-path /tmp/dasall-dmd035/control.sock
```

期望结果：输出 `[dasall_daemon] config validation passed without creating listener resources` 并返回 0。

### 7.2 daemon unavailable

```bash
./build-ci/apps/cli/dasall_cli ping
```

期望结果：在 daemon 未启动时返回非 0，并输出 `[dasall_cli] daemon ping: FAILED — daemon unavailable or timeout`。

### 7.3 start

```bash
mkdir -p /tmp/dasall-dmd035
chmod 700 /tmp/dasall-dmd035

./build-ci/apps/daemon/dasall_daemon \
  --socket-path /tmp/dasall-dmd035/control.sock
```

期望结果：输出 `[dasall_daemon] starting on /tmp/dasall-dmd035/control.sock`，进程持续运行直到收到 `SIGTERM`。

### 7.4 ping / readiness

CLI 现已消费 daemon `ping` / `readiness` 响应，可直接使用与部署 socket_path 对齐的入口参数：

```bash
./build-ci/apps/cli/dasall_cli \
  --socket-path /tmp/dasall-dmd035/control.sock \
  ping

./build-ci/apps/cli/dasall_cli \
  --socket-path /tmp/dasall-dmd035/control.sock \
  readiness
```

期望结果：

1. `ping` 输出包含 `daemon_version`、`schema_version`、`profile_id`、`request_id`、`readiness`。
2. `readiness` 输出包含 `state`，并带 `lifecycle_ready`、`listener_ready`、`gateway_ready`、`bridge_reachable` 等字段。

### 7.5 graceful stop

```bash
kill -TERM <daemon-pid>
```

期望结果：daemon 输出 `[dasall_daemon] stopped (run=ok)` 或在超时场景留下 abandoned 审计事实后退出；不再接受新请求。

## 8. 配置样例使用原则

1. [docs/deploy/daemon/daemon.example.json](daemon.example.json) 与 [docs/deploy/daemon/daemon.example.yaml](daemon.example.yaml) 是 deployment contract projection，同时可通过 `--config-file` 作为当前入口 loader 的输入样例。
2. 当前 daemon 二进制默认使用 `desktop_full` profile；若需要切换 profile，可通过 `--profile-id <id>` 显式选择。
3. `--socket-path` 这类 flags 仍只用于入口选择与局部 override；若与 config file 中的同键值冲突，daemon 会拒绝启动。
4. 后续若要扩展更多 daemon 键的 runtime loader surface，仍必须继续遵守“配置文件优先、重复键冲突即拒绝启动”的现有 validator 语义。

## 9. 回退与演进边界

1. 若部署环境要求 `Type=notify`、`WatchdogSec=` 或 `.socket` 单元，则属于 v2 演进，不应在 v1 service 样例上做局部补丁冒充支持。
2. CLI 的默认 endpoint 已与 daemon 默认 socket_path 对齐；若部署路径与默认值不同，继续通过 `--socket-path` 做显式覆盖，而不是在部署层扩展私有脚本协议。
3. 真实 daemon ping 自动化已由 DMD-TODO-024 与 DMD-TODO-036 提供；若后续需要 unary/async/failure/profile 级 daemon E2E，继续推进 DMD-TODO-025、026、027。