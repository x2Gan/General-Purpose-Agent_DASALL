# DMD-TODO-011 DaemonProtocolAdapter frame 收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务仅收敛 `DaemonProtocolAdapter` 的 frame decode/encode 路径，确保优先委托 `DaemonFrameCodec`。
2. 不扩张到 status/cancel/diag 命令路由与 daemon pipeline 组合。
3. 目标是让 malformed frame 在 adapter 侧 fail-closed，并让 response frame 稳定承载 disposition 与 receipt/error/result 字段。

## 2. 本地证据

1. `DMD-TODO-030` 已完成，`DaemonFrameCodec` 已提供 `decode_request_frame()` 与 `encode_response_frame()`。
2. `DMD-TODO-011` 当前要求 adapter 补齐 `decode()/encode()` 并通过单测覆盖 request_id/trace_id/session_hint/idempotency_key/command/args/payload 与 malformed 场景。
3. 现有 `DaemonProtocolAdapter` 在本轮前已接线 codec，但缺少任务要求的显式 parse/build 收敛接口，且 `decode_request_frame()` 传参使用 `active_payload_.size()`，无法体现稳定上限策略。

## 3. 设计结论

1. 在 adapter 内新增私有收敛函数：`parse_uds_request_frame()` 与 `build_uds_response_frame()`，把 decode/encode 的 frame 语义显式固化。
2. `decode()` 统一通过 `parse_uds_request_frame()` 调用 codec；解码失败直接返回空 `InboundPacket`，确保 malformed frame 不进入后续 Runtime 路径。
3. `decode_request_frame()` 使用固定上限 `1MiB`，避免把“当前 payload 长度”误当上限导致边界失效。
4. `encode()` 统一通过 `build_uds_response_frame()` 构造响应，再调用 codec 编码并发送。

## 4. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| decode/encode 显式收敛到 parse/build | `access/include/daemon/DaemonProtocolAdapter.h`、`access/src/daemon/DaemonProtocolAdapter.cpp` | adapter 不再在 decode/encode 内直接散落 frame 组装逻辑 |
| decode 固定 payload 上限并 fail-closed | `DaemonProtocolAdapter::parse_uds_request_frame()` | malformed 输入返回空 packet，不进入 Runtime |
| response frame 编码稳定 | `DaemonProtocolAdapter::build_uds_response_frame()` + codec | accepted_async 响应包含 disposition 与 receipt_ref |
| 单测覆盖字段与负例 | `tests/unit/access/DaemonProtocolAdapterTest.cpp` | request/trace/session/idempotency/args/payload/async 与 malformed/encode 路径可断言 |

## 5. 落盘结果

1. 更新 `access/include/daemon/DaemonProtocolAdapter.h`：
   - 新增私有函数声明 `parse_uds_request_frame()`；
   - 新增私有函数声明 `build_uds_response_frame()`；
   - 引入 `DaemonProtocolTypes.h` 以承载 `UdsResponseFrame`。
2. 更新 `access/src/daemon/DaemonProtocolAdapter.cpp`：
   - 新增 `kMaxDaemonFramePayloadBytes = 1MiB`；
   - `decode()` 改为统一调用 `parse_uds_request_frame()`；
   - `parse_uds_request_frame()` 通过 `decode_request_frame(payload_view, 1MiB)` 解析；
   - `encode()` 改为 `build_uds_response_frame()` + `encode_response_frame()`。
3. 更新 `tests/unit/access/DaemonProtocolAdapterTest.cpp`：
   - 扩展 submit frame 样例，覆盖 `request_id/trace_id/session_hint/idempotency_key/command/args/payload/async_preference`；
   - 保留 malformed frame fail-closed 断言；
   - 新增 encode 断言，验证 `accepted_async`、`receipt_ref`、`request_id`、`trace_id` 等字段稳定输出。

## 6. Validation

1. `Build_CMakeTools(buildTargets=["dasall_access_daemon_protocol_adapter_unit_test"])`
   - 结果：通过。
2. `Build_CMakeTools(buildTargets=["dasall_access_daemon_protocol_adapter_local_trusted_unit_test"])`
   - 结果：通过。
3. `RunCtest_CMakeTools(tests=["DaemonProtocolAdapterTest","DaemonProtocolAdapterLocalTrustedTest"])`
   - 结果：通过（2/2）。
   - 备注：CTest 仍打印仓库基线的 `DartConfiguration.tcl` 提示，但返回码为 0。

## 7. 完成判定

1. adapter decode/encode 已显式收敛为 `parse_uds_request_frame()` 与 `build_uds_response_frame()`。
2. malformed frame 在 adapter 层 fail-closed，不进入 Runtime。
3. response frame 可稳定承载 disposition、receipt_ref、agent_result/error_ref（agent_result/error_ref 仍沿 codec 既有路径承载）。
4. 单测已覆盖 DMD-TODO-011 要求的核心字段与畸形帧路径，任务可标记 Done。
