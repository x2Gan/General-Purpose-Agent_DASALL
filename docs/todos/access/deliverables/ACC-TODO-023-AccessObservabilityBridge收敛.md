# ACC-TODO-023 设计收敛：AccessObservabilityBridge

日期：2026-04-24
任务：ACC-TODO-023
来源：[docs/todos/access/DASALL_access子系统专项TODO.md](../DASALL_access子系统专项TODO.md)

## 1. 目标与边界

目标：在 access 模块内提供统一事件桥接层，收敛请求接收、认证失败、授权拒绝、dispatch 结果与发布失败的字段口径。

边界：

1. 只负责事件组装与发送，不改变业务判定。
2. 不定义新的业务语义对象，不污染 contracts。
3. 发送失败只返回 false，不抛异常、不反向影响主链流程。

## 2. 职责与非职责

职责：

1. `emit_request_received()`：输出 access.request.received 事件。
2. `emit_auth_failed()`：输出 access.auth.failed 事件。
3. `emit_policy_denied()`：输出 access.policy.denied 事件。
4. `emit_dispatch_result()`：输出 access.runtime.dispatched 事件。
5. `emit_publish_failed()`：输出 access.publish.failed 事件。

非职责：

1. 不调用 runtime。
2. 不做策略判定。
3. 不做日志存储实现，仅通过可注入 sink 回调发送。

## 3. 数据模型与接口

```cpp
struct AccessObservabilityEvent {
  std::string name;
  std::map<std::string, std::string> fields;
};

class AccessObservabilityBridge {
 public:
  using EmitBackend = std::function<bool(const AccessObservabilityEvent&)>;

  explicit AccessObservabilityBridge(EmitBackend backend = {});

  bool emit_request_received(const InboundPacket&, std::string_view request_id,
                             std::string_view session_id, std::string_view trace_id,
                             std::optional<std::string_view> actor_ref = std::nullopt) const;
  bool emit_auth_failed(const InboundPacket&, std::string_view request_id,
                        std::string_view trace_id, std::string_view reason_code,
                        std::optional<std::string_view> actor_ref = std::nullopt) const;
  bool emit_policy_denied(const RuntimeDispatchRequest&, std::string_view reason_code) const;
  bool emit_dispatch_result(const RuntimeDispatchRequest&, const RuntimeDispatchResult&,
                            std::int64_t latency_ms) const;
  bool emit_publish_failed(const PublishEnvelope&, AccessErrorCode, std::string_view detail) const;
};
```

字段最低锚点：

1. request_id
2. session_id
3. trace_id
4. actor_ref（可选）

## 4. 流程与时序

1. 接收请求后调用 `emit_request_received()`，记录入口和协议事实。
2. 认证失败路径调用 `emit_auth_failed()`，记录 auth_method/reason_code。
3. policy deny 路径调用 `emit_policy_denied()`，记录 operation/decision/ref。
4. dispatch 返回后调用 `emit_dispatch_result()`，记录 disposition/latency。
5. 发布失败调用 `emit_publish_failed()`，记录 error_code/detail。

失败处理：

1. backend 未配置或发送失败返回 false。
2. 调用方必须将返回值视为“观测写出状态”，不能据此改变 accept/deny 结论。

## 5. 文件范围

代码：

1. `access/src/AccessObservabilityBridge.h`
2. `access/src/AccessObservabilityBridge.cpp`
3. `access/CMakeLists.txt`

测试：

1. `tests/unit/access/AccessObservabilityBridgeTest.cpp`
2. `tests/unit/access/AccessObservabilityFieldSetTest.cpp`
3. `tests/unit/access/CMakeLists.txt`

文档：

1. `docs/todos/access/DASALL_access子系统专项TODO.md`
2. `docs/worklog/DASALL_开发执行记录.md`

## 6. 验收命令

```bash
cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AccessObservabilityBridgeTest|AccessObservabilityFieldSetTest" --output-on-failure
```

## 7. 风险与回退

1. 当前 sink 为可注入回调，后续对接 infra logging/metrics/tracing 时保持接口不变。
2. 若字段集合调整，仅在 bridge 内集中变更，避免上游调用点散落字段拼装逻辑。
