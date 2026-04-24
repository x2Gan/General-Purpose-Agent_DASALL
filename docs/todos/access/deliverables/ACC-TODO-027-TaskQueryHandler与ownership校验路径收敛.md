# ACC-TODO-027 交付物：TaskQueryHandler 与 ownership 校验路径收敛

最近更新时间：2026-04-24
状态：Done

## 任务来源

- TODO：docs/todos/access/DASALL_access子系统专项TODO.md ACC-TODO-027
- 前置：ACC-TODO-022（AsyncTaskRegistry ownership 能力）、ACC-TODO-026（gateway 组合根）

## 代码目标

| 文件 | 说明 |
|---|---|
| apps/gateway/src/TaskQueryHandler.h | 定义 QueryHandlerResult 与 TaskQueryHandler 接口（query/cancel） |
| apps/gateway/src/TaskQueryHandler.cpp | 实现 ownership 校验路径：先 query 再 validate，保证 pending/completed/expired 语义稳定；cancel 走 registry 标记 cancelled |
| apps/gateway/CMakeLists.txt | gateway 目标纳入 TaskQueryHandler.cpp |
| tests/unit/access/AccessTaskQueryHandlerTest.cpp | 新增 TaskQueryHandler 单元测试：owner query/cancel、non-owner reject、expired 语义 |
| tests/unit/access/CMakeLists.txt | 注册 dasall_access_task_query_handler_unit_test / AccessTaskQueryHandlerTest |

## 行为收敛说明

1. 查询路径 `handle_query(receipt_id, actor_ref, ownership_token)`：
   - 先 `query_receipt` 判定 NotFound/Expired。
   - 再 `validate_ownership` 判定 OwnerMismatch。
   - ownership 通过后返回 task_status（pending/completed/cancelled）。
2. 取消路径 `handle_cancel(...)`：
   - 同样先判定 NotFound/Expired。
   - ownership 通过后调用 `mark_completed(receipt_id, "cancelled")`。
   - 返回 `Cancelled` 状态。
3. 非 owner query/cancel 均 fail-closed 返回 `OwnerMismatch`。
4. owner query 对同一 receipt 可观测到明确状态演进：pending -> completed -> expired（TTL 后）。

## 验收命令与结果

```bash
cmake --build /home/gangan/DASALL/build-ci \
  --target dasall_access_task_query_handler_unit_test dasall_gateway
# -> 构建通过（gateway 仅保留 third_party/cpp-httplib 既有 warning）

ctest --test-dir /home/gangan/DASALL/build-ci \
  -R "AccessTaskQueryHandlerTest|AsyncTaskRegistryOwnershipTest" \
  --output-on-failure
# -> 2/2 passed
```

## 约束与边界

1. ownership 双因子仍由 AsyncTaskRegistry 提供：actor_ref + ownership_token。
2. 本任务不扩展 runtime cancel 转发语义；v1 cancel 在 registry 侧标记为 cancelled，避免越过 ADR-007/008 权限边界。
3. 未引入 shared contracts 变更，保持 module-local sidecar 边界。
