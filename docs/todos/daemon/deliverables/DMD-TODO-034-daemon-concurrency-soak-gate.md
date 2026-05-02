# DMD-TODO-034 daemon concurrency / soak gate 收敛

## 1. 任务范围

- 任务 ID：DMD-TODO-034
- 目标：验证 daemon 并发背压、长期运行 soak、receipt TTL cleanup 与 draining gate。
- 设计锚点：daemon 详设 6.5.2、6.11、6.12。

## 2. 实施结果

### 2.1 受控 worker 与资源计数

1. `DaemonBootstrap` 从单连接同步处理收敛为受控 dispatch worker 队列，accept loop 负责接入，worker 负责连接处理。
2. 新增 `active_connection_count()`，统一统计 pending channel 与 processing channel，作为 soak/backpressure 的资源回落证据。
3. `DaemonListenerHost` 的 connection handler 语义改为可接管 channel 生命周期，避免 worker 模式下 listener 过早关闭连接。

### 2.2 receipt TTL cleanup 与 publish failure seam

1. `AsyncTaskRegistry` 新增 `prune_expired()`，并在计数/ownership/mark_completed 路径对齐过期清理行为。
2. `create_daemon_access_gateway(...)` 新增可注入 `publish_backend` seam，用于 deterministic 验证 publish failure。
3. `DaemonInProcessFixture` 统一暴露 `receipt_active_count()`、`prune_expired_receipts()` 与 `active_connection_count()`，供三条 034 gate 共享。

### 2.3 focused integration gates

1. `DaemonBackpressureIntegrationTest`
   - 为每个请求生成唯一 `request_id/idempotency_key`，避免幂等冲突掩盖并发背压。
   - 验证前两条请求阻塞期间，overflow 请求经 Admission 拒绝，资源计数最终回落到基线。
2. `DaemonSoakIntegrationTest`
   - repeated parallel waves 下交替注入 happy path 与 publish failure，验证 parseable response 与 `active_connection_count()` 回落。
   - draining 场景拆分 inflight/late request 的 deadline，验证 stop 期间新请求被拒绝，已在途请求可在排空窗口内完成。
3. `DaemonReceiptTtlCleanupIntegrationTest`
   - accepted_async 生成两个独立 receipt。
   - status 查询暴露 `status_expired`，显式 cleanup 后 `receipt_active_count()` 回落到 0。

## 3. 修改文件

1. `access/include/AccessGatewayFactory.h`
2. `access/src/AccessGatewayFactory.cpp`
3. `access/src/AsyncTaskRegistry.h`
4. `access/src/AsyncTaskRegistry.cpp`
5. `apps/daemon/src/DaemonBootstrap.h`
6. `apps/daemon/src/DaemonBootstrap.cpp`
7. `apps/daemon/src/DaemonListenerHost.h`
8. `apps/daemon/src/DaemonListenerHost.cpp`
9. `tests/integration/access/DaemonIntegrationHarness.h`
10. `tests/integration/access/DaemonInProcessFixture.h`
11. `tests/integration/access/DaemonLoadScenario.h`
12. `tests/integration/access/DaemonBackpressureIntegrationTest.cpp`
13. `tests/integration/access/DaemonSoakIntegrationTest.cpp`
14. `tests/integration/access/DaemonReceiptTtlCleanupIntegrationTest.cpp`
15. `tests/integration/access/CMakeLists.txt`
16. `docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md`

## 4. 验证证据

执行命令：

```text
Build_CMakeTools(buildTargets=["dasall_access_daemon_backpressure_integration_test","dasall_access_daemon_soak_integration_test","dasall_access_daemon_receipt_ttl_cleanup_integration_test"])
```

直接执行已构建测试二进制：

```text
build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_backpressure_integration_test
build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_soak_integration_test
build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_receipt_ttl_cleanup_integration_test
```

结果：

1. `DaemonBackpressureIntegrationTest`：退出码 0。
2. `DaemonSoakIntegrationTest`：退出码 0。
3. `DaemonReceiptTtlCleanupIntegrationTest`：退出码 0。

说明：当前环境中 `RunCtest_CMakeTools` 存在卡住现象，本轮按用户要求改用直接执行已构建测试二进制作为 focused 证据；三条 gate 均完成验证。

## 5. 约束符合性

1. daemon 仍只承担接入、背压、发布与排空，不持有业务 scheduler，不越过 Runtime/Recovery/Orchestrator 边界。
2. `dispatch_workers` 从配置项落到真实受控 worker，但只处理入口连接，不扩张成第二业务调度层。
3. TTL cleanup 只清理 receipt 映射与计数，不在持锁路径执行 publish 或外部 side effect。