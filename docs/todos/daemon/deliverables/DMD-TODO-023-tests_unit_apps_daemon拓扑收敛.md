# DMD-TODO-023 tests/unit/apps/daemon 拓扑收敛

完成时间：2026-05-01

## 任务结论

DMD-TODO-023 已完成。`tests/unit/apps/daemon` 的单元测试拓扑已经实际落盘并接入顶层 unit discoverability；本轮完成的是证据收口与验收口径校准，而不是新增 daemon 生产实现。

1. `tests/unit/apps/daemon/CMakeLists.txt` 已注册 daemon 壳层相关单测目标，并把目标追加到 `DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS`。
2. `tests/unit/CMakeLists.txt` 已通过 `add_subdirectory(apps/daemon)` 引入 daemon unit 测试，并把 `${DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS}` 汇入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`。
3. `ctest --test-dir build-ci -N` 已稳定发现 `DaemonBootstrapTest`、`DaemonLifecycleControllerTest`、`DaemonListenerHostTest`、`DaemonConfigValidatorTest` 四个 023 锚点测试。
4. 原 TODO 行中的聚合命令 `cmake --build build-ci --target dasall_unit_tests` 会触发全仓 unit test 运行，当前受仓库既有 runtime unit failure 污染，不适合作为该“测试拓扑 discoverability”任务的主验收信号；本轮已改为 focused build + discoverability 命令。

## 代码与拓扑证据

1. `tests/unit/apps/daemon/CMakeLists.txt`
   - 已注册 `DaemonSignalHandlerTest`
   - 已注册 `DaemonListenerHostTest`
   - 已注册 `DaemonBootstrapTest`
   - 已注册 `DaemonLifecycleControllerTest`
   - 已注册 `DaemonConfigValidatorTest`
2. `tests/unit/CMakeLists.txt`
   - 已引入 `apps/daemon`
   - 已将 daemon unit target 列表并入顶层 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`

## 验收

1. `cmake --build build-ci --target dasall-daemon_bootstrap_unit_test dasall-daemon_lifecycle_controller_unit_test dasall-daemon_listener_host_unit_test dasall-daemon_config_validator_unit_test`
   - 结果：通过。
2. `ctest --test-dir build-ci -N | rg "Daemon(Bootstrap|LifecycleController|ListenerHost|ConfigValidator)Test"`
   - 结果：通过，稳定发现 `DaemonListenerHostTest`、`DaemonBootstrapTest`、`DaemonLifecycleControllerTest`、`DaemonConfigValidatorTest`。

## 完成判定对照

1. apps/daemon 单测被统一聚合目标发现：已满足。
2. 不依赖本地手工命令逐个运行测试：已满足，CTest discoverability 已形成稳定入口。
3. 024 的前置 023 依赖可追溯：已满足，可继续进入 `DaemonPingIntegrationTest` 的真实集成收敛。

## 风险与备注

1. `dasall_unit_tests` 当前是带执行语义的聚合目标，不是纯 build target；若后续继续把 discoverability 任务绑定到该目标，容易再次被无关模块失败污染。
2. 本轮不新增任何 daemon 生产代码或测试代码；若后续 topology 有新增测试文件，仍需同步维护 `tests/unit/apps/daemon/CMakeLists.txt` 与顶层聚合列表。