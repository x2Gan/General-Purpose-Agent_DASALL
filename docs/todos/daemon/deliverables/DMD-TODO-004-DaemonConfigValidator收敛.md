# DMD-TODO-004 DaemonConfigValidator 收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只实现 daemon 配置 validator 与最小 validate-only 入口，不引入完整 ConfigCenter/部署配置文件读取流程。
2. validate-only 的目标是“在 listener bind 之前给出结构化失败出口”，因此本轮不改造 `DaemonBootstrap::run()` 的内部 listener 细节。
3. validator 只冻结 v1 需要的三类检查：基础 config 合法性、flags/config conflict 拒绝、restart-only hot-reload key 拒绝。

## 2. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| 配置非法必须在 bind 前失败 | `apps/daemon/src/DaemonConfigValidator.{h,cpp}` | `DaemonConfigValidatorTest` 覆盖 socket_path 与 payload limit 失败路径 |
| validate-only 不能创建 listener 资源 | `DaemonConfigValidator::validate_only()` + `apps/daemon/src/main.cpp` | valid config validate-only 直接返回成功，不进入 `UnixIpcProvider::listen()` 路径 |
| flags 与配置文件重复键冲突必须拒绝 | `validate_conflicts()` | conflict test 返回稳定 `daemon.socket_path` 键路径 |
| restart-only 热更键必须显式拒绝 | `validate_reload_keys()` | `daemon.socket_path` 等键触发 `ReloadForbidden` |

## 3. 落盘结果

1. 新增 `apps/daemon/src/DaemonConfigValidator.h` 与 `apps/daemon/src/DaemonConfigValidator.cpp`，定义 `DaemonConfigValidationError`、`DaemonConfigValidationResult` 与 validator 四个入口：`validate_config()`、`validate_conflicts()`、`validate_reload_keys()`、`validate_only()`。
2. validator 复用 002 已冻结的 `DaemonBootstrapConfig` / `DaemonConfigConflict`，增加 1 MiB 默认值之上的绝对 payload 上限保护，以及 restart-only key 列表。
3. 更新 `apps/daemon/src/main.cpp`：
   - 增加最小命令行解析，只支持 `--validate-only` 和 `--socket-path`。
   - 在创建 `UnixIpcProvider` 之前先运行 config validation。
   - validate-only 成功时直接返回，不创建 listener。
   - 正常启动路径改为消费 `DaemonBootstrapConfig.socket_path` 与 `shutdown_grace_ms`，不再写死旧 socket 常量。
4. 更新 `apps/daemon/CMakeLists.txt` 与 `tests/unit/apps/daemon/CMakeLists.txt`，纳入 validator 源文件和 `DaemonConfigValidatorTest`。

## 4. Validation

1. `cmake -S . -B build-ci -G Ninja`
2. `cmake --build build-ci --target dasall_daemon dasall_daemon_config_validator_unit_test`
3. `ctest --test-dir build-ci -R "^DaemonConfigValidatorTest$" --output-on-failure`

结果摘要：

1. `dasall_daemon` 编译通过，证明 validate-only 入口和正常启动路径都已接入新 validator。
2. `DaemonConfigValidatorTest` 通过，覆盖默认成功、空 socket path、payload 上限、flags/config conflict、restart-only reload key 与 validate-only 成功路径。
3. daemon 现在可以在 listener bind 前失败，且 validate-only 不再依赖创建 IPC 资源来验证配置。

## 5. 完成判定

DMD-TODO-004 已完成。判定依据：

1. 配置非法会在 bind 前被 validator 拒绝。
2. validate-only 有独立入口，成功时不进入 listener 资源创建路径。
3. flags/config conflict 和 restart-only reload key 都有稳定错误出口，可直接服务后续 lifecycle 与 hot-reload 任务。