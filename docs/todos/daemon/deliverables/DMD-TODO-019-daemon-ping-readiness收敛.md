# DMD-TODO-019 daemon ping/readiness 收敛

完成时间：2026-04-29

## 任务结论

已将 daemon `ping` / `readiness` 从 `DaemonBootstrap` 静态特判迁移到统一 daemon command router，经 `create_daemon_access_gateway()` 的 submit pipeline 处理，并复用 `DaemonHealthService` 生成 readiness 摘要。

## 代码变更

1. 更新 `access/include/AccessGatewayFactory.h`
   - 为 daemon pipeline 增加 health 元数据输入
2. 更新 `access/src/AccessGatewayFactory.cpp`
   - 接入 `DaemonHealthService`
   - 新增 `make_ping_dispatch_result()` / `make_readiness_dispatch_result()`
   - 在 daemon command router 中处理 `ping` 与 `readiness`
3. 更新 `apps/daemon/src/main.cpp`
   - 为 daemon pipeline 注入 profile/version/diag/bridge 健康元数据
4. 更新 `apps/daemon/src/DaemonBootstrap.cpp`
   - 删除 bootstrap 内部静态 ping 特判，统一走 `gateway_->submit()`
5. 更新 `apps/daemon/src/DaemonBootstrap.h`
   - 删除过时的 `make_ping_response()` 声明
6. 新增测试：
   - `tests/unit/access/DaemonPingCommandTest.cpp`
   - `tests/unit/access/DaemonReadinessCommandTest.cpp`
   - `tests/unit/access/DaemonPingDoesNotBypassRouterTest.cpp`
7. 更新测试注册：
   - `tests/unit/access/CMakeLists.txt`
   - `tests/unit/apps/daemon/CMakeLists.txt`

## 验收

1. 构建：`Build_CMakeTools`（通过）
2. 定向测试：`RunCtest_CMakeTools(tests=["DaemonPingCommandTest","DaemonReadinessCommandTest","DaemonPingDoesNotBypassRouterTest"])`（通过）

## 完成判定对照

1. `ping` 返回 version/schema/profile/readiness 摘要：已满足
2. `readiness` 在 bridge unavailable 时返回 `NOT_READY`：已满足
3. ping 不再绕过统一 daemon command router：已满足，且 `DaemonPingDoesNotBypassRouterTest` 断言未进入 runtime backend

## 说明

1. 集成级真实 daemon ping 验收仍由 `DMD-TODO-024` 收口；本任务聚焦 command router 与响应编码闭环。
