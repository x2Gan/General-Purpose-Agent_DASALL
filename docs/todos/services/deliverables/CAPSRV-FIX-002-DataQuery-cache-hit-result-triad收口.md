# CAPSRV-FIX-002 DataQuery cache hit result triad 收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `CAPSRV-GAP-002` / `CAPSRV-FIX-002`。
2. 本轮目标：修正 `DataQueryLane` cache hit 成功路径的 result triad，使 success path 保持 `code=nullopt,error=nullopt`，同时为 fresh cache hit 与 allow_stale cache hit 冻结 focused regression。
3. 完成判定：`services/src/data/DataQueryLane.cpp` 的 cache hit 分支已不再写入失败 code；`tests/unit/services/data/DataQueryLaneTest.cpp` 已覆盖 cache hit `has_consistent_values()` / `succeeded()`；相邻 `ServiceResultSemanticsContractTest` 与 `BuiltinExecutorLaneResultCodeTest` 通过；本轮不依赖 qemu / kvm，也不把结果外推为 installed / release / soak 证据。

## 2. 本地证据

1. `services/include/ServiceTypes.h` 当前把成功语义固定为 `code=nullopt,error=nullopt`：`service_result_has_consistent_triad()` 要求 `code` 与 `error` 同步出现或同步缺失，`service_result_succeeded()` 要求两者都为空。
2. `docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md` 已明确指出 tools -> services 链路存在“成功 payload 混入失败 code 会污染跨层状态语义”的问题，并建议在 tools / capability services contract tests 中同时断言 success、error、code 三元一致性。
3. `docs/architecture/DASALL_capability_services子系统详细设计.md` 已将 `DataQueryResult` 定义为 data query 统一结果对象，并要求 `from_cache` 只表达缓存来源，不应掩盖真实 stale / success 事实。
4. 变更前 `services/src/data/DataQueryLane.cpp` 的 cache hit 分支手工返回 `.code = contracts::ResultCode::ToolExecutionFailed`、`.error = std::nullopt`，与 `ServiceTypes.h` 的 triad 规则冲突。
5. 变更前 `tests/unit/services/data/DataQueryLaneTest.cpp` 只断言 cache 命中复用与 payload，不校验 success triad，因此公共 triad gate 虽然存在，但 cache hit 快路径没有被 focused regression 命中。
6. 本轮已更新 `services/src/data/DataQueryLane.cpp` 与 `tests/unit/services/data/DataQueryLaneTest.cpp`，并用 direct-binary 方式复跑 data / contract / tools 三个聚焦测试二进制，结果均通过。

## 3. 外部参考

1. Abseil `Status User Guide` 说明：成功路径应返回 `OkStatus()`，非成功路径才携带 canonical error code 与 message；调用方应以 `ok()` 区分 success 与 error，而不是容忍“成功值 + 失败码”混写。本轮 triad 修复遵循同一条语义原则：cache hit 属于成功读取，不应附带失败 code。

## 4. 设计结论

### 4.1 根因收口

1. `CAPSRV-GAP-002` 的根因不是公共 triad contract 缺失，而是 `DataQueryLane` cache hit 快路径绕过 `ResultMapper` 后手工拼装了不一致的成功结果。
2. 跨层 guard 已经存在：`ServiceResultSemanticsContractTest` 固定 services 结果三元一致性，`BuiltinExecutorLaneResultCodeTest` 固定 tools 消费侧会拒绝“failure code without error”。
3. 真正漏掉的是 cache hit focused regression，因此旧实现虽然能复用缓存，却会让 `succeeded()` / `has_consistent_values()` 在命中路径上产生错误结论。

### 4.2 本轮决定

1. cache hit 与 allow_stale cache hit 都按成功路径处理：`code=nullopt,error=nullopt,from_cache=true`，保留 `rows_json` 不变。
2. triad 修复保持最小化，只改 cache hit 分支，不改 `ResultMapper`、router、bridge、metrics 或公共 `ServiceTypes` 语义。
3. focused regression 固定在 `DataQueryLaneTest`，分别覆盖 fresh cache hit 与 allow_stale cache hit 的 `has_consistent_values()` / `succeeded()`。

### 4.3 边界与不外推项

1. 本轮不改 `DataProjectionCache` 的 TTL / stale 策略，也不扩张到 dynamic registry、subscription trace 或 production observability。
2. 本轮不引入 qemu / kvm、installed package 或 external backend 证据；closeout 只覆盖 build-tree focused triad 语义。
3. 本轮不改变 `IDataService` / `ServiceTypes` 的 owner boundary；只是让 `DataQueryLane` 的 cache hit 实现重新服从既有公共 contract。

## 5. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | success triad 必须保持 `code` / `error` 同步为空 | `services/src/data/DataQueryLane.cpp` |
| D2 | fresh cache hit 需要 focused triad regression | `tests/unit/services/data/DataQueryLaneTest.cpp` |
| D3 | allow_stale cache hit 也需要 focused triad regression | `tests/unit/services/data/DataQueryLaneTest.cpp` |
| D4 | 相邻 triad gate 不能回退 | `tests/contract/tool/ServiceResultSemanticsContractTest.cpp`、`tests/unit/tools/BuiltinExecutorLaneResultCodeTest.cpp` |
| D5 | closeout 结论需回写总账、交付物与工作日志 | 本文档、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 6. Build 三件套

1. 代码目标：`DataQueryLane` cache hit 命中不再写入失败 code，成功 cache read 与公共 triad 语义一致。
2. 测试目标：`DataQueryLaneTest` 锁定 fresh / allow_stale cache hit 的 triad；相邻 `ServiceResultSemanticsContractTest` 与 `BuiltinExecutorLaneResultCodeTest` 继续守住跨层 gate。
3. 验收命令：
   - `cmake --build build/vscode-linux-ninja --target dasall_data_query_lane_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/services/data/dasall_data_query_lane_unit_test`
   - `ninja -C build/vscode-linux-ninja dasall_contract_service_result_semantics_test dasall_builtin_executor_lane_result_code_unit_test`
   - `./build/vscode-linux-ninja/tests/contract/dasall_contract_service_result_semantics_test`
   - `./build/vscode-linux-ninja/tests/unit/tools/dasall_builtin_executor_lane_result_code_unit_test`

## 7. Rollout Checklist

1. fresh cache hit 与 allow_stale cache hit 均已显式断言 `has_consistent_values()` 与 `succeeded()`。
2. 相邻 services contract / tools consumer triad gate 复跑通过。
3. 本轮没有把 cache hit triad closeout 外推为 services production backend、installed package 或 qemu gate 已完成。

## 8. 风险与回退

1. 若后续新增其他手工构造 `DataQueryResult` 的 cache / fallback 分支，仍可能再次引入“success payload + failure code”混写；后续实现必须复用同一 triad 约束。
2. 若未来 `service_result_succeeded()` 或 `service_result_has_consistent_triad()` 的 contract 变化，必须同步更新 `DataQueryLaneTest` 与相邻 triad contract tests，而不是只改单个 lane。
3. 本轮没有覆盖 installed / release / soak 层证据；若后续上层 gate 失败，不应回退为本轮 triad 修复无效。

## 9. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 cache hit success triad、focused regression、相邻 triad gate 与文档回写。
3. Build 三件套已在本机 build tree 完成，且未使用 qemu / kvm。
4. 范围保持在 `CAPSRV-FIX-002`，未扩张到其他 services gap。

结论：D Gate = PASS；`CAPSRV-FIX-002` 已按 DataQuery cache hit success triad、focused regression 与相邻 triad gate evidence 收口。