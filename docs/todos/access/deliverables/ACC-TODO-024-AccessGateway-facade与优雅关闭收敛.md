# ACC-TODO-024 设计收敛：AccessGateway facade 与优雅关闭

日期：2026-04-24
任务：ACC-TODO-024
来源：[docs/todos/access/DASALL_access子系统专项TODO.md](../DASALL_access子系统专项TODO.md)

## 1. 目标与边界

目标：实现 access 入口统一 facade，收敛 `init/submit/publish_result/shutdown` 生命周期，并提供可测试的 `run_submit_pipeline()` 主链执行点。

边界：

1. facade 只编排入口主链，不替代 runtime 裁定。
2. `shutdown()` 只处理 access 内 inflight 排空，不等待 runtime 业务完成。
3. 非 Ready 状态拒绝新 `submit()`，返回 `ShuttingDown` 语义。

## 2. 责任与非责任

职责：

1. 生命周期状态流转：`Uninitialized -> Initializing -> Ready -> Draining -> ShutDown`。
2. 在 `submit()` 内管理 inflight 计数，调用 `run_submit_pipeline()`。
3. `publish_result()` 在 `Ready/Draining` 可执行，在其他状态拒绝。
4. `shutdown(drain_timeout)` 在超时或 inflight 清零后终止。

非职责：

1. 不执行认证/授权细节。
2. 不改写 publisher/runtime 结果语义。
3. 不管理长期持久化状态。

## 3. 数据与接口

```cpp
class AccessGateway final : public IAccessGateway {
 public:
  using SubmitPipeline = std::function<RuntimeDispatchResult(const InboundPacket&)>;
  using PublishBackend = std::function<bool(const PublishEnvelope&)>;

  bool init() override;
  RuntimeDispatchResult submit(const InboundPacket& packet) override;
  bool publish_result(const PublishEnvelope& envelope) override;
  AccessGatewayState state() const override;
  bool is_ready() const override;
  void shutdown(std::chrono::milliseconds drain_timeout) override;

 private:
  RuntimeDispatchResult run_submit_pipeline(const InboundPacket& packet);
};
```

关键点：

1. submit 通过 RAII guard 维护 inflight 计数。
2. 非 Ready 状态返回 `AccessDisposition::Rejected + AccessErrorCode::ShuttingDown`。
3. publish backend 为可注入函数，便于单测和后续组合根注入。

## 4. 流程与时序

### 4.1 submit 正常链路

1. 状态检查为 Ready。
2. inflight +1。
3. 调用 `run_submit_pipeline()`。
4. inflight -1 并通知 drain 条件变量。

### 4.2 reject path

1. 状态不是 Ready（Draining/ShutDown/Uninitialized）。
2. 直接返回 rejected 结果，`error_code=ShuttingDown`。
3. 不执行 pipeline。

### 4.3 优雅关闭

1. `shutdown()` 设置状态 Draining。
2. 等待 inflight 清零，最长 `drain_timeout`。
3. 无论等待成功或超时，状态转 ShutDown。

## 5. 文件范围

代码：

1. `access/src/AccessGateway.h`
2. `access/src/AccessGateway.cpp`
3. `access/CMakeLists.txt`

测试：

1. `tests/unit/access/AccessGatewayFacadeTest.cpp`
2. `tests/unit/access/AccessGatewayRejectPathTest.cpp`
3. `tests/unit/access/AccessGatewayAsyncReceiptTest.cpp`
4. `tests/unit/access/AccessGatewayLifecycleTest.cpp`
5. `tests/unit/access/CMakeLists.txt`

文档：

1. `docs/todos/access/DASALL_access子系统专项TODO.md`
2. `docs/worklog/DASALL_开发执行记录.md`

## 6. 验收命令

```bash
cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AccessGateway(FacadeTest|RejectPathTest|AsyncReceiptTest|LifecycleTest)" --output-on-failure
```

## 7. 风险与回退

1. 当前 facade 使用函数注入 pipeline/backend，后续接组合根时保持接口兼容即可替换实现。
2. 若后续引入更复杂 shutdown 语义（例如阶段化 drain），可在不改 `IAccessGateway` 的前提下扩展内部状态机。
