# CLI-TODO-011 CLI 测试 topology 与 discoverability 接线

状态：Done
日期：2026-05-04
来源 TODO：docs/todos/cli/DASALL_cli本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只建立 CLI 自有 unit topology 和 contract/access 接入点，不提前实现 `CliJsonOutputContractTest`、`CliExitCodeContractTest` 的断言逻辑。
2. 本任务要解决的是 discoverability，而不是扩展 parser、formatter、exit decision 或 daemon 行为。
3. 本任务允许复用现有 `tests/unit/access` 下的 CLI 单测源文件，但 CMake 拓扑必须收敛到 `tests/unit/apps/cli`，避免后续继续把 CLI 测试挂靠在 access 名下。
4. 本任务同时复核 `CLI-TODO-003`、`CLI-TODO-004`、`CLI-TODO-005` 的已完成基线，确认现有代码、focused tests 与历史交付物一致后再进入 topology 落盘。

## 2. 基线复核

### 2.1 已完成任务继承核对

1. `CLI-TODO-003`：`apps/cli/CMakeLists.txt` 只链接 `dasall_access`、`dasall_contracts`、`dasall_infra`、`dasall_platform`，没有 direct link runtime；`apps/cli/src/main.cpp` 也只经 `UnixIpcProvider + CliIpcClient` 走 UDS 到 daemon/access。
2. `CLI-TODO-004`：`apps/cli/src/CliIpcClient.cpp` 已使用 `UdsRequestFrame/UdsResponseFrame` 编解码并把 `completed/accepted_async/rejected/not_ready` 投影到 `DaemonClientResponse`；`DaemonPingIntegrationTest` 已覆盖真实 ping roundtrip。
3. `CLI-TODO-005`：`access/include/daemon/DaemonEndpointDefaults.h` 已定义共享 `kDefaultDaemonSocketPath`；`main.cpp` 和 `CliDaemonSocketPathIntegrationTest.cpp` 已同时覆盖默认值与 `--socket-path` 覆盖路径。

### 2.2 历史交付与 focused tests

1. 交付物回链：`ACC-TODO-025`、`ACC-TODO-038`、`DMD-TODO-031`、`DMD-TODO-036` 均已落盘并有历史提交，对应 CLI-TODO-003~005 的代码边界。
2. focused tests：`CliIpcClientTest`、`CliIpcClientUnavailableTest`、`CliIpcClientResponseTest`、`CliDaemonCommandParserTest`、`CliDaemonOutputFormatterTest`、`DaemonPingIntegrationTest`、`CliDaemonSocketPathIntegrationTest` 当前全部通过，说明 003~005 不是“文档完成、代码缺位”。

## 3. 本轮设计结论

1. CLI 自有 unit topology 以 `tests/unit/apps/cli/CMakeLists.txt` 为唯一注册入口，测试名保持不变，但标签扩展为 `unit;access;cli`，保留 access 过滤能力同时显式标出 CLI 归属。
2. `tests/unit/access/CMakeLists.txt` 不再直接注册 CLI unit tests，避免同一测试名在 access/cli 两个拓扑重复声明。
3. `tests/contract/access/CMakeLists.txt` 先提供 `CliJsonOutputContractTest`、`CliExitCodeContractTest` 的 reserved entrypoint；为了防止 topology 任务冒充 contract 实现，保留点采用 fail-closed 占位，并且暂不携带 `contract` label。
4. `tests/contract/CMakeLists.txt` 只负责把 `tests/contract/access` 接到顶层 contract tree；真正的 JSON / exit code contract 断言仍由 `CLI-TODO-012` 替换这些保留点实现。

## 4. Design -> Build 映射

| 设计结论 | 直接解锁任务 | Build 落点 |
|---|---|---|
| CLI unit tests 必须拥有自有 topology | `CLI-TODO-006`、`CLI-TODO-007`、`CLI-TODO-008`、`CLI-TODO-010` | parser / client / formatter 的单测继续复用原文件，但统一由 `tests/unit/apps/cli` 接线 |
| contract/access 必须先提供稳定命名入口 | `CLI-TODO-009`、`CLI-TODO-010`、`CLI-TODO-012` | `CliJsonOutputContractTest`、`CliExitCodeContractTest` 名称现在已被 discoverability 锁定，后续只替换实现 |
| topology 不能冒充 contract 完成 | `CLI-TODO-012` | reserved entrypoint 故意 fail-closed，避免在没有 JSON/exit assertions 时误报 contract gate 通过 |

## 5. Validation

1. `Build_CMakeTools()`
2. `ctest --test-dir build/vscode-linux-ninja -N | rg "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|CliJsonOutputContractTest|CliExitCodeContractTest"`

结果摘要：

1. CMake 重新配置与编译通过后，现有 5 个 CLI unit tests 仍可被 `ctest -N` 发现，但注册入口已从 `tests/unit/access` 收敛到 `tests/unit/apps/cli`。
2. `CliJsonOutputContractTest` 与 `CliExitCodeContractTest` 已进入顶层 discoverability，但执行时会 fail-closed，明确提示 `CLI-TODO-012` 仍未完成真实 contract 断言。
3. `CLI-TODO-003`、`CLI-TODO-004`、`CLI-TODO-005` 的 focused tests 与历史提交复核均通过，本轮不需要回补基线代码。

## 6. 完成判定

CLI-TODO-011 完成的判定标准为：

1. `tests/unit/apps/cli` 与 `tests/contract/access` 两条拓扑都已落盘，并被顶层 `tests/unit/CMakeLists.txt` / `tests/contract/CMakeLists.txt` 接线。
2. `ctest -N` 可以直接发现现有 CLI unit tests 以及 `CliJsonOutputContractTest`、`CliExitCodeContractTest` 这两个稳定 contract 入口名。
3. 后续 CLI contract 实现不再需要继续挂靠 `tests/unit/access` 或临时发明新的 test 名称；`CLI-TODO-012` 只需替换 reserved entrypoint 为真实断言。
