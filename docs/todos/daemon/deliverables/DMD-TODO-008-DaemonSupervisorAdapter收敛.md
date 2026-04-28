# DMD-TODO-008 DaemonSupervisorAdapter 收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只冻结 daemon v1 的最小 supervisor notify surface：`notify_ready()`、`notify_stopping()`、`tick_watchdog()`。
2. v1 只交付两种语义：无 supervisor 时的 no-op 成功路径，以及有 `IWatchdogService` 时的最小 watchdog bridge；不交付 systemd 专用 fd/notify、socket activation import 或 daemon 自行恢复裁定。
3. 本轮不把 adapter 接进 `DaemonBootstrap`；009 再决定组合根如何装配 supervisor 依赖，035 再收敛部署/运维契约。

## 2. 研究与设计结论

### 2.1 本地证据

1. DMD-BLK-002 的核心缺口不是 watchdog 组件不存在，而是 daemon 侧没有冻结“最小通知面”，导致 008/035 在 systemd 与 infra bridge 之间长期悬空。
2. `infra/include/watchdog/IWatchdogService.h` 已经冻结 `register_entity(...)`、`unregister_entity(...)`、`heartbeat(...)` 等 public seam，这足以支撑 daemon v1 的 watchdog bridge，而无需引入新的跨模块接口。
3. 008 的完成判定要求两件事：no-op 路径不能阻塞 daemon 启动，以及 watchdog 失败不能由 daemon 自行恢复裁定。因此 adapter 应只负责显式通知和失败上抛，不做重试、降级或恢复决策。

### 2.2 设计结论

1. `DaemonSupervisorAdapter` 默认走 no-op；只有在 `watchdog_enabled=true` 且注入 `IWatchdogService` 时，才桥接到 watchdog service。
2. `notify_ready()` 负责注册 daemon watched entity，`tick_watchdog()` 负责发送 heartbeat，`notify_stopping()` 负责注销该 entity。
3. bridge 失败必须原样通过 `WatchdogOperationResult` 返回，daemon 不在 adapter 内做隐藏重试或本地恢复裁定。
4. `systemd notify` / `sd_notify()` / `socket activation` 明确继续留在 v2，不混入 008 的 v1 范围。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| v1 默认 no-op，不要求 supervisor 存在 | `apps/daemon/src/DaemonSupervisorAdapter.{h,cpp}` | `DaemonSupervisorAdapterTest` 断言无 watchdog 配置时三类 notify 均成功 |
| watchdog bridge 复用 `IWatchdogService` public seam | `DaemonSupervisorAdapter::notify_ready/notify_stopping/tick_watchdog` | unit test 能观察 register/unregister/heartbeat 被正确调用 |
| failure 只上抛，不自行恢复 | `tick_watchdog()` / `notify_ready()` 直接返回 `WatchdogOperationResult` | failure surfacing test 断言无隐藏 retry / deactivation |
| daemon build 图纳入 supervisor adapter | `apps/daemon/CMakeLists.txt`、`tests/unit/apps/daemon/CMakeLists.txt` | `dasall_daemon` 与 `DaemonSupervisorAdapterTest` target 均可构建 |

## 4. 落盘结果

1. 新增 `apps/daemon/src/DaemonSupervisorAdapter.h` 与 `apps/daemon/src/DaemonSupervisorAdapter.cpp`：
   - `DaemonSupervisorAdapterOptions` 冻结 v1 `watchdog_enabled`、`watchdog_entity_id`、`watchdog_timeout_ms`、`watchdog_grace_ms`。
   - `notify_ready()` 在 watchdog bridge 启用时注册 daemon watched entity。
   - `tick_watchdog()` 在 bridge active 时发送 heartbeat，并维护显式 heartbeat sequence。
   - `notify_stopping()` 注销 daemon watched entity。
   - 默认无 watchdog service 或 `watchdog_enabled=false` 时，所有入口均返回 no-op success。
2. 更新 `apps/daemon/CMakeLists.txt`，把 supervisor adapter 纳入 `dasall_daemon`。
3. 新增 `tests/unit/apps/daemon/DaemonSupervisorAdapterTest.cpp`，覆盖：
   - no-op path
   - watchdog bridge ready/tick/stopping 主链
   - failure surfacing 且无内部恢复
4. 更新 `tests/unit/apps/daemon/CMakeLists.txt`，注册 `DaemonSupervisorAdapterTest`。

## 5. Validation

1. `Build_CMakeTools(buildTargets=["dasall_daemon"])`
2. `Build_CMakeTools(buildTargets=["dasall_daemon_supervisor_adapter_unit_test","dasall_watchdog_service_interface_unit_test"])`
3. `RunCtest_CMakeTools(tests=["DaemonSupervisorAdapterTest","WatchdogServiceInterfaceTest"])`

结果摘要：

1. `dasall_daemon` 编译通过，说明新增 supervisor adapter 未破坏 daemon 现有构建图。
2. `DaemonSupervisorAdapterTest` 通过，证明 v1 no-op path、watchdog bridge 和 failure surfacing 语义已冻结。
3. `WatchdogServiceInterfaceTest` 回归通过，说明 008 复用的是已冻结的 infra watchdog public seam，而不是新造 daemon 专用接口。
4. CTest stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按仓库基线计为有效证据。

## 6. 完成判定

DMD-TODO-008 已完成。判定依据：

1. daemon v1 已具备 no-op + `IWatchdogService` bridge 的最小 supervisor notify surface。
2. watchdog 失败现在只通过 `WatchdogOperationResult` 对外暴露，不由 daemon adapter 自行恢复裁定。
3. DMD-BLK-002 已清除，035 后续可以在该最小 seam 之上继续收敛部署与运维契约。