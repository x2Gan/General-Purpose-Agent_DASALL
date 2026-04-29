# DMD-TODO-021 daemon observability / audit 收敛

完成时间：2026-04-29

## 任务结论

已为 daemon 本地控制面补齐结构化观测字段接线，并把对应字段集合固定到 access 单测：

1. daemon request 路径补齐 `request_id`、`session_id`、`trace_id`、`daemon_state`、`connection_ref`
2. accepted_async receipt 路径补齐 `receipt_ref`
3. peer identity deny 路径补齐 daemon 状态与连接引用
4. graceful shutdown 后续复用的 abandoned audit 事件已预留并完成字段定义
5. 认证秘密未进入 daemon 观测事件字段

## 代码变更

1. 更新 `access/src/AccessObservabilityBridge.h`
   - 新增 daemon 专用观测接口声明
2. 更新 `access/src/AccessObservabilityBridge.cpp`
   - 实现 `emit_daemon_request_fact()`
   - 实现 `emit_receipt_event()`
   - 实现 `emit_peer_identity_denied()`
   - 实现 `emit_shutdown_abandoned()`
3. 更新 `access/src/AccessGatewayFactory.cpp`
   - 在 daemon command router 入口发出 request fact
   - 在 peer identity fail-closed 路径发出 deny 事件
   - 在 accepted_async receipt 路径发出 receipt 事件
4. 更新 `tests/unit/access/AccessObservabilityFieldSetTest.cpp`
   - 为 daemon 专用事件补齐字段回归断言
5. 新增 `tests/unit/access/DaemonObservabilityFieldSetTest.cpp`
   - 单独固定 daemon request / receipt / deny / shutdown 四类事件字段集合
6. 更新 `tests/unit/access/CMakeLists.txt`
   - 注册 `DaemonObservabilityFieldSetTest`

## 验收

1. 构建：`Build_CMakeTools`（通过）
2. 定向测试：`RunCtest_CMakeTools(tests=["DaemonObservabilityFieldSetTest","AccessObservabilityFieldSetTest","AccessObservabilityBridgeTest"])`（通过）
3. 邻近回归：`RunCtest_CMakeTools(tests=["DaemonPeerIdentityFailClosedTest","DaemonAcceptedAsyncReceiptTest","AccessGatewayAsyncReceiptTest"])`（通过）

## 完成判定对照

1. `request_id/session_id/trace_id/daemon_state/connection_ref/receipt_ref` 在对应路径可断言：已满足
2. 认证秘密不进入日志：已满足，daemon 事件字段集中未写入 secret 类字段
3. shutdown abandoned 审计事实可复用：已满足，本轮已定义专用事件并固定字段集合，供 DMD-TODO-022 接线

## 边界说明

1. 本轮聚焦 daemon access 观测字段与事件接线，不扩张到 infra concrete logging/audit backend 装配
2. `emit_shutdown_abandoned()` 已就位，但真正的 draining/abandoned 触发仍由 DMD-TODO-022 收口
