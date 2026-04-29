# DMD-TODO-017 daemon cancel 命令 routing 收敛

## 1. 任务结论

- 任务 ID：DMD-TODO-017
- 完成日期：2026-04-29
- 状态：Done
- 范围：v1 仅实现 registry-only + runtime cancel forward，不扩展 runtime live status/query surface。

## 2. 关键实现

1. 扩展 daemon 查询处理器，新增 cancel 路由语义：
   - `access/include/daemon/DaemonTaskQueryHandler.h`
   - `access/src/daemon/DaemonTaskQueryHandler.cpp`
2. `handle_cancel(receipt_ref, owner, cancel_backend)` 行为：
   - 先复用 `handle_status()` 做 receipt 存在性/过期/owner 校验
   - owner 匹配后调用 cancel backend（`RuntimeBridge::cancel`）
   - cancel backend 成功后执行 `AsyncTaskRegistry::mark_completed(..., "cancelled")`
3. 在 daemon access pipeline 接入 cancel 命令路由：
   - `access/src/AccessGatewayFactory.cpp`
   - cancel 分支位于 auth + policy gate 之后，确保不绕过 `AccessPolicyGate`
4. 扩展 daemon protocol adapter：
   - `access/src/daemon/DaemonProtocolAdapter.cpp`
   - 支持 `cancel` 命令从 args 投影 `receipt_ref/ownership_token` 到 payload。

## 3. 语义与错误映射

- owner 匹配 + cancel forward 成功 -> `cancelled`（Completed/200）
- missing -> `cancel_missing`（Rejected/404）
- expired -> `cancel_expired`（Rejected/410）
- owner mismatch -> `cancel_owner_mismatch`（Rejected/403）
- cancel forward 失败 -> `cancel_forward_failed`（Rejected/503）

## 4. 测试与验收证据

- 构建：`Build_CMakeTools`（result code 0）
- 定向测试：`RunCtest_CMakeTools`
  - `DaemonCancelCommandTest` passed
  - `RuntimeBridgeRejectMappingTest` passed
  - `AccessTaskQueryHandlerTest` passed

## 5. 变更文件

- `access/include/daemon/DaemonTaskQueryHandler.h`
- `access/src/daemon/DaemonTaskQueryHandler.cpp`
- `access/src/AccessGatewayFactory.cpp`
- `access/src/daemon/DaemonProtocolAdapter.cpp`
- `tests/unit/access/DaemonCancelCommandTest.cpp`
- `tests/unit/access/CMakeLists.txt`
- `docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md`
- `docs/todos/daemon/deliverables/DMD-TODO-017-daemon-cancel-routing收敛.md`
