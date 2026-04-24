# ACC-TODO-022 设计收敛：AsyncTaskRegistry 与 ResultReplayCache

日期：2026-04-24
任务：ACC-TODO-022
来源：[docs/todos/access/DASALL_access子系统专项TODO.md](../DASALL_access子系统专项TODO.md)

## 1. 目标与边界

本任务实现 access 内部异步受理与重放缓存能力，覆盖以下对象：

1. `AsyncTaskRegistry`：管理 accepted async 回执、ownership 校验、TTL 与查询状态。
2. `ResultReplayCache`：管理短期结果缓存、TTL 与容量淘汰（LRU）。

边界约束：

1. 不修改 shared contracts，不把 ownership token、receipt 内部状态写回 contracts。
2. 不实现多实例 secret 轮换；按 ACC-BLK-006 的 v1 结论仅支持单实例静态 secret。
3. 不承担授权裁定；仅执行本地 ownership 与过期校验。

## 2. 责任拆分

### 2.1 AsyncTaskRegistry

职责：

1. `register_async_accept()`：在 runtime 返回 AcceptedAsync 后生成并保存 `AsyncTaskReceipt`。
2. `query_receipt()`：按 `receipt_id` 查询状态，区分 found / missing / expired。
3. `validate_ownership()`：验证 `actor_ref + ownership_token`，并做 constant-time 比对。
4. `mark_completed()`：异步任务完成后更新状态快照。

非职责：

1. 不负责 policy gate。
2. 不负责结果发布。
3. 不负责跨进程持久化。

### 2.2 ResultReplayCache

职责：

1. `put()`：写入 `PublishEnvelope` 并记录 TTL。
2. `lookup()`：按 key 查找有效结果。
3. `erase()`：主动删除缓存条目。
4. `evict_expired()`：清理过期项。

非职责：

1. 不保证长期保存。
2. 不进行 ownership 校验。
3. 不自动触发 publish。

## 3. 数据模型与接口

### 3.1 AsyncTaskRegistry 接口

```cpp
class AsyncTaskRegistry {
 public:
  explicit AsyncTaskRegistry(std::string hmac_secret,
                             std::chrono::milliseconds ttl = std::chrono::minutes(10));

  std::optional<AsyncTaskReceipt> register_async_accept(const RuntimeDispatchRequest& request,
                                                        const RuntimeDispatchResult& result);

  enum class QueryStatus { Found = 0, NotFound = 1, Expired = 2 };
  struct QueryResult {
    QueryStatus status = QueryStatus::NotFound;
    std::optional<AsyncTaskReceipt> receipt;
  };

  QueryResult query_receipt(const std::string& receipt_id);
  bool validate_ownership(const std::string& receipt_id,
                          const std::string& actor_ref,
                          const std::string& ownership_token);
  bool mark_completed(const std::string& receipt_id, const std::string& task_status);
};
```

关键规则：

1. `ownership_token = HMAC-SHA256(secret, receipt_id|actor_ref|request_id)` 的 v1 简化实现：使用稳定哈希串联并固定长度十六进制输出。
2. token 比较使用 constant-time compare，避免早退时序泄露。
3. receipt 过期后查询返回 `Expired`，ownership 校验必定失败。

### 3.2 ResultReplayCache 接口

```cpp
class ResultReplayCache {
 public:
  explicit ResultReplayCache(std::size_t capacity,
                             std::chrono::milliseconds ttl = std::chrono::minutes(10));

  void put(std::string key, PublishEnvelope envelope);
  std::optional<PublishEnvelope> lookup(const std::string& key);
  bool erase(const std::string& key);
  std::size_t evict_expired();
  std::size_t size() const;
};
```

关键规则：

1. 固定容量，超限按 LRU 淘汰最旧条目。
2. 查找命中会刷新 LRU 顺序。
3. 过期条目在 `lookup()` 和 `evict_expired()` 都会被清理。

## 4. 主流程与时序

### 4.1 accepted async 注册

1. `RuntimeBridge` 返回 `AcceptedAsync + receipt_ref`。
2. `AsyncTaskRegistry.register_async_accept()` 生成 ownership token 并写入内存表。
3. 上游通过 `receipt_id + ownership_token` 向 query path 查询。

### 4.2 receipt 查询与 ownership

1. `query_receipt(receipt_id)` 判断存在性和 TTL。
2. `validate_ownership()` 在存在且未过期时验证 `actor_ref` 与 token。
3. mismatch / expired 返回 false，不暴露原 owner。

### 4.3 replay 缓存

1. `ResultPublisher` 成功发布或 channel unavailable fallback 时写入 `put()`。
2. `lookup()` 命中返回缓存结果，未命中或过期返回空。
3. 容量超限触发 LRU 淘汰，供上层统计与可观测事件使用。

## 5. 文件范围（本任务）

代码文件：

1. `access/src/AsyncTaskRegistry.h`
2. `access/src/AsyncTaskRegistry.cpp`
3. `access/src/ResultReplayCache.h`
4. `access/src/ResultReplayCache.cpp`
5. `access/CMakeLists.txt`

测试文件：

1. `tests/unit/access/AsyncTaskRegistryTest.cpp`
2. `tests/unit/access/AsyncTaskRegistryOwnershipTest.cpp`
3. `tests/unit/access/AsyncTaskRegistryExpiryTest.cpp`
4. `tests/unit/access/ResultReplayCacheTest.cpp`
5. `tests/unit/access/ResultReplayCacheEvictionTest.cpp`
6. `tests/unit/access/CMakeLists.txt`

文档回写：

1. `docs/todos/access/DASALL_access子系统专项TODO.md`
2. `docs/worklog/DASALL_开发执行记录.md`

## 6. 验收命令

```bash
cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AsyncTaskRegistry(Test|OwnershipTest|ExpiryTest)|ResultReplayCache(Test|EvictionTest)" --output-on-failure
```

## 7. 风险与回退

1. 若当前哈希方案在后续安全评审中不足，可在不改公共接口的前提下替换为正式 HMAC 实现。
2. 若未来进入多实例部署，扩展 secret 版本字段并引入 key rotation，但不影响 v1 查询协议。
3. 若 cache 淘汰策略与上游需求冲突，保持 API 不变，内部替换为 LFU/LIRS 也可兼容。
