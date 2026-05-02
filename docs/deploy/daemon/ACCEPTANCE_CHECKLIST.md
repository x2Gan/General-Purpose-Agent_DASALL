# DASALL daemon 部署验收清单

## 1. 适用范围

本清单用于 direct-bind v1 本地控制面交付验收。通过条件不是“systemd 已接入所有高级能力”，而是本地运维者可以按文档独立完成 validate、start、ping/readiness、daemon unavailable 与 graceful stop。

## 2. 前置条件

1. 已在仓库根目录完成构建：

```bash
cmake --build build-ci --target dasall_daemon dasall_cli
```

2. 已准备安全 socket 目录：

```bash
mkdir -p /tmp/dasall-dmd035
chmod 700 /tmp/dasall-dmd035
```

## 3. 验收步骤

### 3.1 validate-only

```bash
./build-ci/apps/daemon/dasall_daemon \
  --validate-only \
  --socket-path /tmp/dasall-dmd035/control.sock
```

通过条件：返回 0，且输出 `[dasall_daemon] config validation passed without creating listener resources`。

### 3.2 daemon unavailable

```bash
./build-ci/apps/cli/dasall_cli ping
```

通过条件：daemon 未启动时返回非 0，并输出 `[dasall_cli] daemon ping: FAILED — daemon unavailable or timeout`。

### 3.3 start

```bash
./build-ci/apps/daemon/dasall_daemon \
  --socket-path /tmp/dasall-dmd035/control.sock
```

通过条件：控制台输出 `[dasall_daemon] starting on /tmp/dasall-dmd035/control.sock`，进程持续存活。

### 3.4 ping + readiness

```bash
./build-ci/apps/cli/dasall_cli \
  --socket-path /tmp/dasall-dmd035/control.sock \
  ping

./build-ci/apps/cli/dasall_cli \
  --socket-path /tmp/dasall-dmd035/control.sock \
  readiness
```

通过条件：

1. `ping` 输出包含 `daemon_version`、`schema_version`、`profile_id`。
2. `readiness` 输出包含 `state` 与 `listener_ready`、`gateway_ready`、`bridge_reachable`。

### 3.5 graceful stop

```bash
kill -TERM <daemon-pid>
```

通过条件：daemon 输出 `[dasall_daemon] stopped (run=ok)`；若有 inflight 请求，遵守 Draining 排空或输出 abandoned 审计事实。

## 4. 契约检查项

1. service 样例必须使用 `Type=simple`，不得伪装 `Type=notify`。
2. 文档必须明确 socket activation 为 v2 非交付项。
3. 配置样例必须与 `DaemonBootstrapConfig` 键集合对齐。
4. README 必须明确：当前二进制不直接消费 YAML/JSON 配置文件。
5. readiness smoke 必须通过 CLI 消费真实 UDS 响应验证，而不是只验证 send 成功。