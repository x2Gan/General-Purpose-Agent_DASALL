# DMD-TODO-001 daemon 命令 taxonomy 与 frame 类型收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只冻结 daemon v1 的命令 taxonomy 与 module-local UDS frame 类型，不改写 `DaemonProtocolAdapter` 的现有解析路径。
2. 类型定义保持在 `access/include/daemon`，不进入 `contracts/`，以满足 daemon 详设 6.4.1 与专项 TODO 中的边界约束。
3. 为避免后续 adapter/CLI 改造返工，本轮同步保留 `submit` -> `run`、`diagnostics` -> `diag` 的兼容别名映射，但不提前扩展新的命令族。

## 2. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| `UdsRequestFrame` / `UdsResponseFrame` 作为 daemon private schema 先独立落型 | `access/include/daemon/DaemonProtocolTypes.h` | `DaemonProtocolTypesTest` 验证默认值与字段承载 |
| daemon command taxonomy 必须先于 adapter codec 冻结 | `DaemonCommandKind` + `classify_daemon_command()` | `ping/run/status/cancel/diag/unknown` 分类可二值断言 |
| v1 读写权限语义需可被后续 policy/health 任务复用 | `is_read_only_daemon_command()` / `is_mutating_daemon_command()` | 读写分类断言通过 |
| 类型头必须进入 access 公共 surface，但不扩散到 contracts | `access/CMakeLists.txt` public header 注册 | focused build 能直接消费新增 header |

## 3. 落盘结果

1. 新增 `access/include/daemon/DaemonProtocolTypes.h`，定义 `DaemonCommandKind`、`DaemonAsyncPreference`、`UdsResponseDisposition`、`DaemonFrameDecodeError`、`UdsRequestFrame` 与 `UdsResponseFrame`。
2. 新增 `classify_daemon_command()`，将 `ping`、`run`、`status`、`cancel`、`readiness`、`diag` 冻结为 v1 taxonomy，并保留 `submit`/`diagnostics` 兼容别名。
3. 更新 `access/CMakeLists.txt`，把新类型头加入 `dasall_access` public headers。
4. 新增 `tests/unit/access/DaemonProtocolTypesTest.cpp` 并注册 `DaemonProtocolTypesTest`，覆盖 schema_version、async preference、response disposition、命令分类与 unknown 路径。

## 4. Validation

1. `cmake -S . -B build-ci -G Ninja`
2. `cmake --build build-ci --target dasall_access_daemon_protocol_types_unit_test`
3. `ctest --test-dir build-ci -R "^DaemonProtocolTypesTest$" --output-on-failure`

结果摘要：

1. 新增头文件已被 `dasall_access` public header set 正确消费。
2. `DaemonProtocolTypesTest` 通过，schema_version 默认值、命令 taxonomy、读写分类和兼容别名都具备稳定断言。
3. 验收阶段发现 `build-ci` 现存缓存使用 `Ninja`，因此按仓库构建记忆切换为等价的 `Ninja` focused validation，而没有重置目录或混入无关构建失败。

## 5. 完成判定

DMD-TODO-001 已完成。判定依据：

1. `ping/run/status/cancel/diag/unknown` 命令分类已可二值断言。
2. `UdsRequestFrame` 与 `UdsResponseFrame` 已在 access daemon surface 落型，且未进入 `contracts/`。
3. 新类型头和单测入口都已接入仓库当前的 `build-ci` 构建链。