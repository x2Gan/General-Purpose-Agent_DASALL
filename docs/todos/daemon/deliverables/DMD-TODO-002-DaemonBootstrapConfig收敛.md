# DMD-TODO-002 DaemonBootstrapConfig 收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只定义 daemon 配置与进程上下文类型，不把 `main.cpp` 或 `DaemonBootstrap` 立即切换到新配置对象。
2. v1 daemon 详设 6.10.2 中列出的配置键全部在 `DaemonBootstrapConfig` 中落型，避免后续 profile 投影和 validator 继续依赖散落常量。
3. 为支撑本轮 focused unit test，只补了一条最小 `tests/unit/apps/daemon` 单测接线；这不是 DMD-TODO-023 的 discoverability 闭环，不提前宣称其完成。

## 2. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| daemon v1 配置键必须先有统一类型承载 | `apps/daemon/src/DaemonConfig.h` | `DaemonBootstrapConfigTest` 验证默认值与一致性断言 |
| validate-only、profile 投影、listener bind 后续都依赖冲突模型 | `DaemonConfigConflict` + `DaemonConfigSource` | 冲突对象可承载 key/source/value 事实 |
| daemon build 成功后需要明确的 process context 容器 | `DaemonProcessContext` | 进程上下文字段可承载 config/profile/runtime handles |
| apps/daemon 当前缺少单测宿主，需要最小接线承载配置测试 | `tests/unit/apps/daemon/CMakeLists.txt` + `tests/unit/CMakeLists.txt` | focused build 能发现 `DaemonBootstrapConfigTest` |

## 3. 落盘结果

1. 新增 `apps/daemon/src/DaemonConfig.h`，定义 `DaemonStartupMode`、`DaemonConfigSource`、`DaemonConfigConflict`、`DaemonBootstrapConfig` 与 `DaemonProcessContext`。
2. `DaemonBootstrapConfig` 吸收详设 6.10.2 的 v1 配置键：`socket_path`、`listen_backlog`、`max_payload_bytes`、`dispatch_timeout_ms`、`shutdown_grace_ms`、`receipt_ttl_sec`、`accept_workers`、`dispatch_workers`、`diag_enabled`、`override_enabled`、`watchdog_enabled`、`log_format`、`startup_mode`。
3. 在同一头文件中新增 `has_consistent_values()`，为非法 socket path、worker 数量和窗口值提供轻量结构化断言。
4. 更新 `apps/daemon/CMakeLists.txt`，把 `DaemonConfig.h` 纳入 daemon target 源列表。
5. 新增 `tests/unit/apps/daemon/CMakeLists.txt` 与 `tests/unit/apps/daemon/DaemonBootstrapConfigTest.cpp`，并在 `tests/unit/CMakeLists.txt` 中接线最小 apps/daemon unit test 宿主。

## 4. Validation

1. `cmake -S . -B build-ci -G Ninja`
2. `cmake --build build-ci --target dasall-daemon_bootstrap_config_unit_test`
3. `ctest --test-dir build-ci -R "^DaemonBootstrapConfigTest$" --output-on-failure`

结果摘要：

1. `DaemonBootstrapConfigTest` 通过，默认值与非法 socket path / worker / TTL / grace window 断言均可稳定二值判定。
2. `DaemonProcessContext` 与 `DaemonConfigConflict` 已具备最小结构定义，可被后续 DMD-TODO-003 / 004 直接复用。
3. apps/daemon 单测当前只引入配置测试的最小接线，后续 daemon topology discoverability 仍由 DMD-TODO-023 统一收口。

## 5. 完成判定

DMD-TODO-002 已完成。判定依据：

1. 详设 6.10.2 的 v1 配置键已经全部有类型承载。
2. 非法 socket_path、worker 数、TTL 与 graceful shutdown window 已具备结构化断言。
3. `DaemonProcessContext`、`DaemonStartupMode` 与 `DaemonConfigConflict` 已落型，为后续 profile 投影与 validator 提供稳定接口面。