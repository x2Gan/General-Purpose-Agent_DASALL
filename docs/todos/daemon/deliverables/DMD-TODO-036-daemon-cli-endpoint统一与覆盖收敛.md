# DMD-TODO-036 daemon/CLI 默认 endpoint 统一与覆盖收敛

状态：Done
日期：2026-05-02
来源 TODO：docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只收敛 daemon 与 CLI 的默认 socket_path 来源，以及 CLI 对 daemon endpoint 的显式覆盖参数。
2. 本任务不提前修复真实 UDS socket mode 与 stale restart 策略；这些生产语义仍由 DMD-TODO-037 收口。
3. 本任务不提前把 profile/config snapshot loader 或 hot-reload fresh snapshot source 接入 binary 入口；这些仍由 DMD-TODO-038、039 处理。
4. 本任务只清理 deployment 文档里与 endpoint/CLI 覆盖直接相关的旧口径；专项 review report、陈旧 send-only smoke 清理留给 DMD-TODO-040。

## 2. 根因与设计结论

### 2.1 回归根因

1. daemon 默认值已经在 `apps/daemon/src/DaemonConfig.h` 收敛为 `/tmp/dasall/control.sock`，但 CLI 入口 `apps/cli/src/main.cpp` 仍硬编码旧值 `/tmp/dasall-daemon-control.sock`。
2. `CliCommandParser` 在 DMD-TODO-031 收敛了命令 taxonomy，但没有为 daemon endpoint 提供受控 override surface，导致 deployment 文档必须依赖“CLI 固定连接旧路径”的补充说明。
3. `docs/deploy/daemon/README.md` 与 `ACCEPTANCE_CHECKLIST.md` 仍保留“CLI 不消费 readiness / 只能验证 unavailable”这一历史口径，和当前 `CliIpcClient`/`CliOutputFormatter` 的真实能力不一致。

### 2.2 本轮收敛结论

1. daemon 与 CLI 需要共享同一个默认 endpoint 常量，避免在 `apps/daemon` private config 与 `apps/cli` 入口之间继续漂移。
2. CLI 的 endpoint 解析必须保持 fail-closed，同时支持受控 `--socket-path` 覆盖；默认值缺省时继续走共享常量，而不是在 parser 内复制默认字符串。
3. 部署 smoke 可以直接使用 CLI 的 `ping/readiness` 响应消费能力，不再需要把“原始 UDS Python 脚本”当成唯一正向验证入口。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| daemon/CLI 默认 endpoint 需要共用来源 | `access/include/daemon/DaemonEndpointDefaults.h`、`apps/daemon/src/DaemonConfig.h` | CLI 与 daemon 默认 socket_path 由同一常量导出 |
| CLI 需要 endpoint override surface | `apps/cli/src/CliCommandParser.*`、`apps/cli/src/main.cpp` | `CliDaemonCommandParserTest` 覆盖 `--socket-path` 正反例 |
| CLI endpoint 正向连通需要 focused smoke | `tests/integration/access/CliDaemonSocketPathIntegrationTest.cpp` | 默认 path 与显式 override 两条路径都能 ping daemon |
| 部署文档需要移除旧 CLI 口径 | `docs/deploy/daemon/README.md`、`docs/deploy/daemon/ACCEPTANCE_CHECKLIST.md` | 文档不再依赖“CLI 固定连接旧路径”或“CLI 不消费 readiness”的说明 |

## 4. 落盘结果

1. 新增 `access/include/daemon/DaemonEndpointDefaults.h`，冻结 `kDefaultDaemonSocketPath` 作为 daemon/CLI 共用默认值来源。
2. 更新 `apps/daemon/src/DaemonConfig.h`，让 `DaemonBootstrapConfig::socket_path` 改为直接复用共享常量，而不是在 daemon private config 中保留独立字符串字面量。
3. 更新 `apps/cli/src/CliCommandParser.h` / `CliCommandParser.cpp`：
   - `CliCommand` 新增 `socket_path` 可选字段。
   - parser 支持 `--socket-path <path>` 与 `--socket-path=<path>` 两种形式。
   - duplicate / missing value 等非法形式会 fail-closed 返回 `nullopt`。
4. 更新 `apps/cli/src/main.cpp`：
   - endpoint 默认回退到 `kDefaultDaemonSocketPath`。
   - 当 `CliCommand.socket_path` 存在时，CLI 显式连接 override path。
5. 更新 CLI focused tests：
   - `tests/unit/access/CliDaemonCommandParserTest.cpp` 新增 socket-path 正反例。
   - `CliIpcClientTest.cpp`、`CliIpcClientResponseTest.cpp`、`CliIpcClientUnavailableTest.cpp` 改为复用共享默认值，避免旧路径字面量继续漂移。
6. 新增 `tests/integration/access/CliDaemonSocketPathIntegrationTest.cpp` 并更新 `tests/integration/access/CMakeLists.txt`：
   - 默认 path 场景验证 CLI 在无 override 时走共享默认值。
   - override 场景验证 CLI 可通过 `--socket-path` 连到受控 custom endpoint。
   - 集成测试内部只做“当前用户 + 非活动 socket”级别的隔离清理，不修改生产 stale cleanup policy。
7. 更新 `docs/deploy/daemon/README.md` 与 `docs/deploy/daemon/ACCEPTANCE_CHECKLIST.md`：
   - 删除“CLI 固定连接旧路径”的补充说明。
   - 删除“CLI 尚未消费 readiness 响应”的旧口径。
   - 将 ping/readiness smoke 改为直接使用 `dasall-cli --socket-path ...`。

## 5. Validation

1. `cmake --build build-ci --target dasall-cli dasall-daemon dasall_access_cli_command_parser_unit_test dasall_access_cli_ipc_client_unit_test dasall_access_cli_ipc_client_response_unit_test dasall_access_cli_ipc_client_unavailable_unit_test dasall_access_daemon_ping_integration_test dasall_access_cli_daemon_socket_path_integration_test`
2. `ctest --test-dir build-ci -R "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonSocketPathIntegrationTest|DaemonPingIntegrationTest" --output-on-failure`

结果摘要：

1. `CliDaemonCommandParserTest` 通过，说明 CLI 已支持 `--socket-path` 正例、duplicate/missing-value 负例以及“默认值仍延后到共享常量解析”的语义。
2. `CliIpcClientTest`、`CliIpcClientResponseTest`、`CliIpcClientUnavailableTest` 通过，说明 CLI client 在切到共享默认 endpoint 后没有破坏现有 request/response 行为。
3. `DaemonPingIntegrationTest` 继续通过，说明 DMD-TODO-031 的真实 ping roundtrip 没被 endpoint 收敛改动回归。
4. `CliDaemonSocketPathIntegrationTest` 通过，说明 CLI 在默认值与显式 socket-path 两条路径下都能正向连到 daemon。

## 6. 完成判定

DMD-TODO-036 已完成。判定依据：

1. daemon 与 CLI 默认 socket_path 已来自同一处定义，不再分裂为 `/tmp/dasall/control.sock` 与旧的 `/tmp/dasall-daemon-control.sock`。
2. CLI 已具备受控 `--socket-path` 覆盖面，且 parser 对非法覆盖形式保持 fail-closed。
3. focused 自动化已覆盖默认 endpoint 与显式 override 两条正向路径。
4. deployment 文档不再依赖“CLI 固定连接旧路径”或“CLI 不消费 readiness”的补充说明，035 的 direct-bind smoke 口径可继续复用当前 CLI surface。