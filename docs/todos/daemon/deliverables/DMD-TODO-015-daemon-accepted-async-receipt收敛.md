# DMD-TODO-015 daemon accepted_async receipt 收敛

最近更新时间：2026-04-29
任务状态：Done
关联任务：DMD-TODO-015

## 1. 任务目标

将 daemon 的 accepted_async 响应接线到 `AsyncTaskRegistry`，确保回执（receipt）具备：

1. `receipt_ref/receipt_id` 可追踪。
2. `ownership_token` 与 owner 事实可用于后续校验。
3. TTL 生命周期由既有 `AsyncTaskRegistry` 管理。
4. 不引入并行任务系统或新的 `ReceiptStore`。

## 2. 代码变更

1. `access/include/AccessTypes.h`
- 将 `AsyncTaskReceipt` 前移到 `PublishEnvelope` 之前。
- 在 `PublishEnvelope` 新增 `std::optional<AsyncTaskReceipt> receipt` 字段。

2. `access/include/daemon/DaemonResponseBuilderWithReceipt.h`
- 新增 `DaemonResponseBuilderWithReceipt`，封装 accepted_async 的回执注册入口。
- 对外提供 `register_and_build_receipt()`。

3. `access/src/daemon/DaemonResponseBuilderWithReceipt.cpp`
- 实现 accepted_async 分支：调用 `AsyncTaskRegistry::register_async_accept()`。
- 将 `optional` 回执安全转换为 `shared_ptr` 返回，非 accepted_async 或 registry 不可用时 graceful fallback。

4. `access/src/daemon/DaemonProtocolAdapter.cpp`
- `AcceptedAsync` 响应优先使用 `envelope.receipt->receipt_id`。
- 缺失时回退 `result_id`，保持兼容。

5. `access/CMakeLists.txt`
- 注册 `DaemonResponseBuilderWithReceipt` 头文件与源文件。

6. `tests/unit/access/DaemonAcceptedAsyncReceiptTest.cpp`
- 新增 7 个用例，覆盖回执生成、唯一性、owner/token 事实、非 async/空 registry 回退等。

7. `tests/unit/access/CMakeLists.txt`
- 注册 `dasall_access_daemon_accepted_async_receipt_unit_test`。

## 3. 验证证据

1. 构建验证：
- `cmake --build build-ci`
- 结果：构建完成（通过）。

2. 新增测试执行：
- `./build-ci/tests/unit/access/dasall_access_daemon_accepted_async_receipt_unit_test`
- 结果：退出码 0（通过）。

## 4. 边界符合性检查

1. 未新增平行 `ReceiptStore`：是。
2. 复用 `access/src/AsyncTaskRegistry.*`：是。
3. accepted_async 回执包含 owner/token/TTL 事实：是（由 `AsyncTaskRegistry` 持有并验证）。
4. 保持 daemon 与 runtime 主控边界（ADR-008）：是。

## 5. 结论

DMD-TODO-015 已完成，daemon accepted_async 到 AsyncTaskRegistry 的接线已收敛，代码、测试、构建与文档回写均已落盘。
