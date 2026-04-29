# DMD-TODO-015 Design Plan: 接线 accepted_async 到 AsyncTaskRegistry

**任务 ID**: DMD-TODO-015  
**目标**: 接线 accepted_async 到 AsyncTaskRegistry  
**状态**: Design 阶段  
**日期**: 2026-04-29  

## 1. 核心设计要点

### 1.1 问题陈述

当前 daemon 的 response path 已经能够识别 AcceptedAsync disposition，并在 UdsResponseFrame 中设置 receipt_ref（取值为 result_id）。但**缺失的是**：

1. receipt_ref 的生成没有经过 AsyncTaskRegistry 的注册
2. ownership_token 和 owner_ref 信息没有被记录
3. TTL 管理没有被集成
4. daemon accepted_async 响应没有与 Runtime 任务的映射

### 1.2 设计目标

复用 `access/src/AsyncTaskRegistry.*`，在 daemon response path 中集成 receipt 生成与管理，使得：

1. 每个 accepted_async 响应都经过 `AsyncTaskRegistry::register_async_accept()` 生成 receipt
2. receipt 信息（receipt_id、ownership_token、task_ref、TTL）被正确保存
3. 后续 status/cancel 命令可以通过 receipt 查询与验证
4. 不创建新的 ReceiptStore 平行实现

### 1.3 接线点分析

**当前 response path 流程**：
```
RuntimeBridge::dispatch()
  ↓
RuntimeDispatchResult (disposition=AcceptedAsync, receipt_ref=optional)
  ↓
ResultPublisher::build_envelope()
  ↓
PublishEnvelope (result_id, protocol_status_hint=202)
  ↓
DaemonProtocolAdapter::encode(envelope)
  ↓
build_response_frame()
  → UdsResponseFrame (receipt_ref=result_id, disposition=AcceptedAsync)
  → 编码发送到 client
```

**需要补充的环节**：
- 在 RuntimeBridge 返回 accepted_async 后，或在 ResultPublisher 投影后，**插入 AsyncTaskRegistry::register_async_accept() 的调用**
- 将生成的 receipt 信息流转到 response frame

### 1.4 集成方案选择

**方案 A**：在 ResultPublisher 中集成 AsyncTaskRegistry
- 优点：结果投影和 receipt 生成在同一地点
- 缺点：ResultPublisher 需要依赖 AsyncTaskRegistry；可能违反 ResultPublisher 的单一职责原则

**方案 B**：在 daemon 的 composition root（DaemonBootstrap 或 pipeline factory）中创建一个包装层
- 优点：保持 ResultPublisher 的独立性；适合 daemon 特定逻辑
- 缺点：增加间接层；需要在 pipeline factory 中处理 receipt 生成

**方案 C**（推荐）：在 RuntimeBridge 的映射层中处理 receipt 生成
- 优点：保持职责分离；receipt 生成与 runtime 结果映射在一起
- 缺点：RuntimeBridge 需要可选的 AsyncTaskRegistry 依赖

**选择**：**方案 B**，通过新增一个轻量的 DaemonResponseBuilderWithReceipt adapter 来包装 ResultPublisher，负责：
1. 调用 ResultPublisher::publish()
2. 如果 disposition == AcceptedAsync，调用 AsyncTaskRegistry::register_async_accept()
3. 将 receipt 信息注入 PublishEnvelope（新增字段或通过上下文传递）

## 2. Design->Build 映射表

| 设计项 | Build 对应项 | 代码目标 | 测试目标 |
|------|----------|--------|--------|
| DaemonResponseBuilderWithReceipt | 新增 adapter | `access/src/daemon/DaemonResponseBuilderWithReceipt.{h,cpp}` | `DaemonAcceptedAsyncReceiptTest` |
| PublishEnvelope 扩展 | 新增字段 | `AccessTypes.h` 中 PublishEnvelope 新增 `receipt` 字段 | - |
| daemon pipeline factory 整合 | 修改 factory | 更新 `apps/daemon/src/DaemonAccessPipelineFactory.cpp` 或 `DaemonBootstrap.cpp`，注入 AsyncTaskRegistry 和 DaemonResponseBuilderWithReceipt | `DaemonAccessPipelineFactoryTest` |
| UdsResponseFrame receipt 映射 | 更新 adapter | 更新 `DaemonProtocolAdapter::build_response_frame()` 从 PublishEnvelope 提取 receipt 信息 | `DaemonProtocolAdapterTest` |
| AsyncTaskRegistry 所有权验证 | 复用现有 | 无需修改，复用现有 `validate_ownership()`、`query_receipt()` 接口 | - |
| TTL 管理 | 复用现有 | 无需修改，复用现有 TTL 机制 | - |

## 3. 核心接口定义

### 3.1 DaemonResponseBuilderWithReceipt

```cpp
namespace dasall::access::daemon {

// 将 ResultPublisher 的发布结果增强为包含 receipt 信息
class DaemonResponseBuilderWithReceipt final {
 public:
  explicit DaemonResponseBuilderWithReceipt(
      std::shared_ptr<ResultPublisher> publisher,
      std::shared_ptr<AsyncTaskRegistry> registry);

  // 发布结果，如果是 accepted_async 则同时生成 receipt
  PublishAttemptResult publish_with_receipt(
      const RuntimeDispatchRequest& request,
      const dasall::contracts::AgentResult& agent_result);

 private:
  std::shared_ptr<ResultPublisher> publisher_;
  std::shared_ptr<AsyncTaskRegistry> registry_;
};

}
```

### 3.2 PublishEnvelope 扩展

```cpp
struct PublishEnvelope {
  // ... 现有字段 ...
  std::optional<AsyncTaskReceipt> receipt;  // 新增，仅当 disposition==AcceptedAsync 时有值
};
```

### 3.3 daemon pipeline factory 接线

```cpp
// 在 DaemonBootstrap 或 pipeline factory 中
auto registry = std::make_shared<AsyncTaskRegistry>(
    ownership_secret,  // 来自 config
    std::chrono::seconds(config.receipt_ttl_sec)
);

auto publisher = std::make_shared<ResultPublisher>(emit_backend);

auto response_builder = std::make_shared<DaemonResponseBuilderWithReceipt>(
    publisher,
    registry
);

// 在 daemon pipeline 中，所有 result publish 调用改用 response_builder
```

## 4. 测试策略

### 4.1 单元测试覆盖

**DaemonAcceptedAsyncReceiptTest**：
1. 正例：received accepted_async -> AsyncTaskRegistry::register_async_accept() 被调用 -> receipt 生成
2. 正例：receipt 包含正确的 request_id、actor_ref、ownership_token、task_ref、TTL
3. 正例：多个 accept 产生不同的 receipt_id
4. 负例：disposition != AcceptedAsync 时，不调用 registry
5. 负例：registry unavailable 时，publish 失败
6. 负例：AsyncTaskRegistry::register_async_accept() 返回 nullopt 时，graceful fallback

### 4.2 集成测试回归

- 现有 `AsyncTaskRegistryTest`、`AsyncTaskRegistryOwnershipTest`、`AsyncTaskRegistryExpiryTest` 须全量通过
- `DaemonAccessPipelineFactoryTest` 须验证 response_builder 被正确集成
- `DaemonUnaryRuntimeBridgeTest` 须验证 unary happy path 不受影响

## 5. 交付清单

1. **代码**：
   - `access/src/daemon/DaemonResponseBuilderWithReceipt.{h,cpp}`
   - 修改 `access/include/AccessTypes.h` 中 PublishEnvelope
   - 修改 `access/src/daemon/DaemonProtocolAdapter.cpp` build_response_frame()
   - 修改 daemon composition root（bootstrap 或 factory）

2. **测试**：
   - `tests/unit/access/DaemonAcceptedAsyncReceiptTest.cpp`

3. **文档**：
   - `docs/todos/daemon/deliverables/DMD-TODO-015-daemon-accepted-async-receipt收敛.md`

## 6. 验收命令

```bash
cmake -S . -B build-ci -G "Unix Makefiles" && \
cmake --build build-ci --target dasall_unit_tests && \
ctest --test-dir build-ci \
  -R "AsyncTaskRegistry(Test|OwnershipTest|ExpiryTest)|DaemonAcceptedAsyncReceiptTest" \
  --output-on-failure
```

## 7. D Gate 检查清单

- [ ] Design->Build 映射表完整（本表已提供）
- [ ] 核心接口与数据结构定义明确
- [ ] 测试策略覆盖正例与负例
- [ ] 未创建新的平行 ReceiptStore 实现（复用 AsyncTaskRegistry）
- [ ] 与前置任务（DMD-TODO-013/014）的接线点明确
- [ ] 与后续任务（DMD-TODO-016/017 status/cancel）的接线点明确

**D Gate 状态**: Ready to Build
