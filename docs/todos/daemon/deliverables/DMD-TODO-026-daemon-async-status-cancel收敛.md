# DMD-TODO-026 daemon async / status / cancel 收敛

## 1. 任务范围

- 任务 ID：DMD-TODO-026
- 目标：验证 daemon accepted_async、status、cancel 的集成闭环，并覆盖 owner mismatch 与 TTL expired。
- 设计锚点：daemon 详设 6.8、9。

## 2. 实施结果

### 2.1 receipt registry 测试注入 seam

更新 `access/include/AccessGatewayFactory.h` 与 `access/src/AccessGatewayFactory.cpp`：

1. 为 `DaemonAccessPipelineOptions` 增加可选 `async_task_registry` 注入项。
2. daemon access pipeline 优先复用外部注入的 `AsyncTaskRegistry`，仅在未注入时回退到默认 registry。
3. 该 seam 让集成测试能够控制 receipt TTL，并显式驱动 `mark_completed()` 状态迁移，而不修改 daemon/runtime 主链职责。

### 2.2 status / cancel wire 投影修正

更新 `access/src/AccessGatewayFactory.cpp` 中的 `make_status_dispatch_result()` / `make_cancel_dispatch_result()`：

1. `Active` / `Completed` / `Cancelled` 状态现在会把 `task_status` 投影到 `agent_result.response_text`，让 daemon wire response 可被 CLI/client 读取。
2. rejected 分支不再把 `task_status` 误写到 wire `error_ref`，而是统一输出稳定错误键，如 `status_owner_mismatch`、`cancel_owner_mismatch`、`status_expired`。

### 2.3 receipt flow 集成测试

新增 `tests/integration/access/DaemonReceiptFlowIntegrationTest.cpp`，覆盖：

1. `accepted_async` 返回 `receipt_ref`。
2. owner 查询可观察 `active -> completed` 状态。
3. 非 owner `status` 返回 `status_owner_mismatch`。
4. 非 owner `cancel` 返回 `cancel_owner_mismatch`，且不触发 runtime cancel backend。
5. owner `cancel` 会转发到 runtime cancel backend，并让后续 `status` 返回 `cancelled`。
6. TTL 到期后 `status` 返回 `status_expired`。

## 3. 修改文件

1. `access/include/AccessGatewayFactory.h`
2. `access/src/AccessGatewayFactory.cpp`
3. `tests/integration/access/CMakeLists.txt`
4. `tests/integration/access/DaemonReceiptFlowIntegrationTest.cpp`
5. `docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md`

## 4. 验证证据

执行命令：

```text
Build_CMakeTools(buildTargets=["dasall_access_daemon_receipt_flow_integration_test","dasall_access_daemon_task_query_handler_unit_test","dasall_access_daemon_cancel_command_unit_test"])
RunCtest_CMakeTools(tests=["DaemonReceiptFlowIntegrationTest","DaemonTaskQueryHandlerTest","DaemonCancelCommandTest"])
```

结果：

1. `DaemonReceiptFlowIntegrationTest`：Passed。
2. `DaemonTaskQueryHandlerTest`：Passed。
3. `DaemonCancelCommandTest`：Passed。
4. `RunCtest_CMakeTools` stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按当前仓库基线计为有效 focused gate 证据。

## 5. 约束符合性

1. 未引入 runtime live status/query surface；status 仍保持 DMD-BLK-005 冻结的 registry-only scope。
2. cancel 仍通过 owner 校验与 `RuntimeBridge::cancel()` 转发，不形成 daemon 第二任务系统。
3. 仅新增测试注入 seam 与 wire 投影修正，没有改变 daemon/runtime 边界或 contracts 面。