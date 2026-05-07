# DMD-TODO-016 daemon status 命令 routing 收敛

## 1. 任务结论

- 任务 ID：DMD-TODO-016
- 完成日期：2026-04-29
- 结论：Done
- 范围冻结：status 首版保持 `registry-only`，不引入 Runtime live status/query surface。

## 2. 实现摘要

1. 新增 daemon 侧任务状态查询处理器：
   - `access/include/daemon/DaemonTaskQueryHandler.h`
   - `access/src/daemon/DaemonTaskQueryHandler.cpp`
2. 在 daemon access submit pipeline 中接入 `status` 命令路由：
   - `access/src/AccessGatewayFactory.cpp`
   - 路由路径：`packet.packet_id == "status"` -> `DaemonTaskQueryHandler::handle_status()`
3. 保持 owner 校验 fail-closed：
   - 先 `query_receipt()` 区分 `missing/expired`
   - 再 `validate_ownership()` 判断 `mismatch`
4. 补齐 status 查询所需的 payload 投影：
   - `access/src/daemon/DaemonProtocolAdapter.cpp`
   - 对 `status` 命令从 `args` 投影 `receipt_ref/actor_ref/ownership_token` 到 packet payload。

## 3. 语义映射

- `Found + pending` -> `active`
- `Found + completed` -> `completed`
- `Found + cancelled` -> `cancelled`
- `NotFound` -> `missing`
- `Expired` -> `expired`
- `Ownership validate failed` -> `mismatch`

该映射满足 DMD-TODO-016 完成判定中的 `active/expired/missing/mismatch` 稳定响应要求，并确保不暴露其他 owner 信息。

## 4. 测试与验收证据

- 构建：`Build_CMakeTools`（result code 0）
- 定向测试：`RunCtest_CMakeTools(tests=["DaemonTaskQueryHandlerTest","AccessTaskQueryHandlerTest"])`
- 结果：
  - `DaemonTaskQueryHandlerTest` passed
  - `AccessTaskQueryHandlerTest` passed

## 5. 变更文件

- `access/include/daemon/DaemonTaskQueryHandler.h`
- `access/src/daemon/DaemonTaskQueryHandler.cpp`
- `access/src/AccessGatewayFactory.cpp`
- `access/src/daemon/DaemonProtocolAdapter.cpp`
- `access/CMakeLists.txt`
- `tests/unit/access/DaemonTaskQueryHandlerTest.cpp`
- `tests/unit/access/CMakeLists.txt`
- `docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md`
