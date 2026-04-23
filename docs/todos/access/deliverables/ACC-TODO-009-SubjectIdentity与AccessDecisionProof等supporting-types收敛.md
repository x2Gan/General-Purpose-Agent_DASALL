# ACC-TODO-009 设计收敛文档

## 1. 任务定义

定义 `SubjectIdentity`、`AccessDecisionProof` 与 `RuntimeDispatchRequest`/`RuntimeDispatchResult`、`PublishEnvelope`、`AsyncTaskReceipt` 等核心 supporting types，用以支持 Access Gateway 的认证、授权、异步处理等关键链路。

---

## 2. 设计边界与职责

### 2.1 模块边界

| 类型 | 所有权 | 地位 | 约束 |
|------|--------|------|------|
| `SubjectIdentity` | access module-local | sidecar | 不入 contracts；通过 constraint_set 投影 |
| `AccessDecisionProof` | access module-local | sidecar | 授权裁定私有；不可被 runtime 覆盖 |
| `RuntimeDispatchRequest` | RequestNormalizer 唯一 owner | 接缝 | shared AgentRequest + module-local sidecars |
| `RuntimeDispatchResult` | RuntimeBridge | 返回值 | disposition 由事实源决定 |
| `PublishEnvelope` | ResultPublisher | 投影 | AgentResult 权威；protocol metadata 派生 |
| `AsyncTaskReceipt` | AsyncTaskRegistry | 凭证 | ownership_token + TTL 管理 |

### 2.2 职责分配

| 类型 | 生成者 | 消费方 | 核心职责 |
|------|--------|--------|---------|
| `SubjectIdentity` | SubjectResolver | AuthenticatorChain / AccessPolicyGate | 验证用户身份与信任等级 |
| `AccessDecisionProof` | AccessPolicyGate | AdmissionController / observability | 记录授权决策与拒绝原因 |
| `RuntimeDispatchRequest` | RequestNormalizer | RuntimeBridge | 统一格式化 shared + sidecar 数据 |
| `RuntimeDispatchResult` | RuntimeBridge | 上层调用者 | 三语义结果转换 |
| `PublishEnvelope` | ResultPublisher | Protocol adapters | 无关发布计划 |
| `AsyncTaskReceipt` | AsyncTaskRegistry | 调用者 / TaskQueryHandler | 异步凭证与所有权保护 |

---

## 3. 数据结构详细定义

### 3.1 SubjectIdentity - 认证主体事实

```cpp
struct SubjectIdentity {
  // 调用者唯一标识
  // 格式：scheme://source/identifier
  // 示例：mTLS://gateway/client-cert-cn、JWT://local/user-id、token://daemon/peer-uid
  std::string actor_ref;

  // 主体类型：user/service/operator/diagnostic_endpoint
  std::string subject_type;

  // 认证方法：mTLS/JWT/token/local_trusted/diagnostic_key
  std::string auth_method;

  // 信任等级：public/authenticated/trusted/ops_privileged
  // 用于 access policy 评估和 constraint_set 投影
  std::string trust_level;

  // 租户引用（多租户隔离）
  // 空表示默认租户或全局作用域
  std::string tenant_ref;

  // 可选：认证链路的补充数据（如证书过期时间）
  std::optional<std::string> auth_metadata;
};
```

**语义**：
- 由 SubjectResolver 唯一生成
- Access 独占确定，Runtime 不重写
- trust_level 投影到 AgentRequest.constraint_set（作为 access_trust_level hint）
- actor_ref 在审计事件中作为不变锚点

### 3.2 AccessDecisionProof - 授权裁定证据

```cpp
struct AccessDecisionProof {
  // 授权决策：Allow / Deny / RequireConfirmation
  // 对齐 contracts::SharedPolicyDecisionSemantic
  std::string decision;

  // 策略决策引用
  // 格式：policy://policy-id/version/rule-index
  // 用于审计链接与策略调试
  std::string policy_decision_ref;

  // 拒绝原因码（仅当 decision == Deny）
  // 示例：INSUFFICIENT_PRIVILEGE、UNKNOWN_ACTOR、SERVICE_QUOTA_EXCEEDED
  std::string reason_code;

  // 拒绝原因描述（仅当 decision == Deny）
  std::optional<std::string> reason_description;

  // 策略评估时间戳（ISO 8601）
  std::optional<std::string> evaluated_at;
};
```

**语义**：
- 由 AccessPolicyGate 生成，基于 access action taxonomy
- decision 是最终值，不可被 runtime 业务结果覆盖
- admission 事实（是否进入 runtime）与 execution outcome（runtime 返回什么）独立记录
- reason_code 在 AdmissionController 阶段转换为 AccessErrorCode 并返回客户端

### 3.3 RuntimeDispatchRequest - Bridge 统一输入

```cpp
struct RuntimeDispatchRequest {
  // 共享数据：经 Access 归一化的业务请求
  // 包含：agent_ref, action, parameters, deadline, trace_context 等
  contracts::AgentRequest agent_request;

  // === 以下为 module-local sidecars ===

  // 认证主体事实
  SubjectIdentity subject_identity;

  // 授权裁定证据
  AccessDecisionProof decision_proof;

  // 客户端能力视图
  // 用于 runtime 在多模式执行时判断结果能否返回给客户端
  std::string client_capability_view;

  // 发布模式标志
  bool async_allowed = false;     // 可异步响应
  bool stream_requested = false;  // 请求流式订阅

  // 请求头扩展（protocol-specific 信息）
  std::map<std::string, std::string> request_context;

  // deadline（内部使用，不暴露给 runtime 任务）
  std::optional<std::string> access_deadline;
};
```

**语义**：
- RequestNormalizer 的唯一所有者
- AgentRequest 是 shared，sidecars 是 module-local
- dispatch_deadline 不传给 runtime（runtime 只看 agent_request 的 deadline）
- client_capability_view 用于 stream attach/reconnect 时判断是否可恢复订阅

### 3.4 RuntimeDispatchResult - Bridge 返回结果

```cpp
enum class AccessDisposition {
  Rejected = 0,           // access 拒绝（admission gate 失败）
  Completed = 1,          // 同步完成（runtime 返回 AgentResult）
  AcceptedAsync = 2,      // 异步受理（return receipt + poll/callback）
  StreamAttached = 3,     // 流式订阅（return subscription_ref）
};

struct RuntimeDispatchResult {
  // 三语义结果
  AccessDisposition disposition = AccessDisposition::Rejected;

  // 业务结果（仅当 disposition == Completed）
  // 包含 status / result_code / output / metadata 等
  std::optional<contracts::AgentResult> agent_result;

  // 错误信息（当 disposition == Rejected 或异常）
  std::optional<AccessError> error_info;

  // 异步凭证 ID（仅当 disposition == AcceptedAsync）
  // 调用者用此 ID + ownership_token 查询结果
  std::optional<std::string> receipt_ref;

  // 流式订阅引用（仅当 disposition == StreamAttached）
  std::optional<std::string> subscription_ref;

  // 响应上下文（protocol adapters 使用）
  std::map<std::string, std::string> response_context;
};
```

**语义**：
- `AcceptedAsync` **只表示 access 完成受理**，不表示 runtime 业务完成
- disposition 由最靠近事实的阶段决定
- 异步时不含 AgentResult，只含 receipt_ref 供后续查询

### 3.5 PublishEnvelope - 协议无关发布计划

```cpp
struct PublishEnvelope {
  // 跟踪 ID
  std::string request_id;      // 原始请求 ID
  std::string result_id;       // 结果 ID（与 request_id 可能相同或派生）
  std::string session_id;      // 会话 ID
  std::string trace_id;        // 链路追踪 ID

  // === 以下为 module-local ===

  // 通道引用（内部路由使用）
  std::string channel_ref;

  // 协议种类：http / uds / ipc / mqtt / websocket
  std::string protocol_kind;

  // 权威结果
  contracts::AgentResult agent_result;

  // 协议映射提示（adapters 使用）
  // 示例：HTTP → "200 OK", "400 Bad Request", "429 Too Many Requests"
  std::string protocol_status_hint;

  // 协议元数据（protocol-specific header/frame/metadata）
  std::string protocol_metadata;

  // 是否为最终响应（vs. 流式中间帧）
  bool is_final = true;
};
```

**语义**：
- AgentResult 是权威事实源
- protocol_hint 与 protocol_metadata 仅为派生结果
- ResultPublisher 不允许修改 result_code / status / task_completed 等 agent_result 字段

### 3.6 AsyncTaskReceipt - 异步受理凭证

```cpp
struct AsyncTaskReceipt {
  // 凭证唯一 ID（UUID 或 nanoid）
  std::string receipt_id;

  // 原始请求 ID（追踪用）
  std::string request_id;

  // 原始会话 ID（多请求关联用）
  std::string session_id;

  // 原始调用者引用（所有权验证用）
  std::string actor_ref;

  // Runtime 任务引用（结果查询用）
  std::string task_ref;

  // TTL 过期时间（steady_clock）
  // 建议 TTL 5 分钟 ~ 24 小时（可配置）
  std::chrono::steady_clock::time_point expires_at;

  // 所有权验证令牌
  // 计算方法：HMAC-SHA256(server_secret, receipt_id || actor_ref || request_id)
  // 调用者需提供此令牌才能查询结果
  std::string ownership_token;

  // 可选：任务状态快照（不可变）
  std::optional<std::string> initial_status;
};
```

**所有权验证规则**：

```
验证流程：
1. 按 receipt_id 查找 Receipt
   ❌ 未找到 → ReceiptNotFound(404)
   
2. 检查 actor_ref 匹配
   ❌ 不匹配 → ReceiptOwnerMismatch (403, 不暴露原始 actor)
   
3. Constant-time 比较 ownership_token
   ❌ 不一致 → AccessDenied (403)
   
4. 检查 expires_at TTL
   ❌ 过期 → ReceiptExpired(410)
   
5. 全通过 → 返回任务状态或结果
   ✅ 返回 (task_status, result_reference)
```

---

## 4. 与 Contracts 的边界说明

### 4.1 Shared vs Module-Local 划分

| 类型 | 地位 | 理由 | 约束 |
|------|------|------|------|
| `AgentRequest` / `AgentResult` | ✅ SHARED | runtime 主链统一入出 | 不扩字段；add via constraint_set hint |
| `SubjectIdentity` | 🔐 MODULE-LOCAL | 认证实现细节 | 不入 contracts；需投影才能跨边界 |
| `AccessDecisionProof` | 🔐 MODULE-LOCAL | 授权实现细节 | 不入 contracts；仅留 reason_code 投影 |
| `AsyncTaskReceipt` | 🔐 MODULE-LOCAL | 异步语义未冻结 | 保护细节；不暴露 ownership_token |
| `PublishEnvelope` | 🔐 MODULE-LOCAL | 协议适配细节 | protocol_hint/metadata 私有 |

### 4.2 投影规则

| From | To | 投影字段 | 方式 |
|-----|----|----|------|
| `SubjectIdentity.trust_level` | `AgentRequest.constraint_set["access_trust_level"]` | hint 字符串 | RequestNormalizer |
| `SubjectIdentity.subject_type` | `AgentRequest.constraint_set["access_subject_type"]` | hint 字符串 | RequestNormalizer |
| `AccessDecisionProof.reason_code` | `AccessError.code` | 转换为 AccessErrorCode | AdmissionController |
| `AsyncTaskReceipt.receipt_id` | observability event | 作为 correlation_id | AccessObservabilityBridge |

---

## 5. 文件范围

### 5.1 新增/修改文件

| 文件 | 操作 | 内容 |
|------|------|------|
| `access/include/AccessTypes.h` | 新增 6 个 struct/enum | SubjectIdentity、AccessDecisionProof、RuntimeDispatchRequest/Result、PublishEnvelope、AsyncTaskReceipt |

### 5.2 测试文件

| 文件 | 用途 |
|------|------|
| `tests/unit/access/AccessSupportingTypesTest.cpp` | 验证 SubjectIdentity、AccessDecisionProof 定义与约束 |
| `tests/unit/access/PublishEnvelopeTypesTest.cpp` | 验证 PublishEnvelope 与 AgentResult 边界 |
| `tests/unit/access/AsyncTaskReceiptTypesTest.cpp` | 验证 AsyncTaskReceipt ownership 规则与 TTL |

---

## 6. 约束与设计决策

1. **module-local owner**：sidecar 数据仅在 Access 内部流转，不跨 shared contracts 边界
2. **ownership token 保护**：AsyncTaskReceipt 使用 HMAC 防止伪造与所有权绕过
3. **TTL 管理**：receipt 设定明确过期时间，避免无限期副本积累
4. **三语义简化**：disposition 固定为 4 值，避免漂移（不支持第 5 种 runtime 补充状态）
5. **投影而非暴露**：sidecar 信息通过 constraint_set 或 observability bridge 投影，不直接扩字段
6. **事实源唯一**：AgentResult 权威，protocol adapters 从 protocol_hint/metadata 派生，不构造新 status

---

## 7. Design Gate 检查清单

- [x] 6 个核心类型定义符合详设 6.6、6.7、6.19
- [x] 与 shared contracts 的边界明确且可防检
- [x] ownership_token 与 TTL 约束完整  
- [x] 投影规则与事实源关系清晰
- [x] 无依赖外部冻结接缝的内容
- [x] 测试出口与验收命令可二值判定

**D Gate 结论**：✅ **PASS** - 设计充分、约束明确、可直接进入 Build

---

## 8. 映射到 Build 任务

| Design 结论 | 对应 Build 任务 | Build 责任 |
|---|---|---|
| SubjectIdentity 6 字段定义 | ACC-TODO-009-B | 新增 struct + 字段验证 |
| AccessDecisionProof 4 字段定义 | ACC-TODO-009-B | 新增 struct + 语义验证 |
| RuntimeDispatchRequest 扩展 | ACC-TODO-009-B | 新增 sidecar 字段 + 类型验证 |
| RuntimeDispatchResult 修改 | ACC-TODO-009-B | 扩展 disposition enum + 字段映射 |
| PublishEnvelope 定义 | ACC-TODO-009-B | 新增 struct + 权威性验证 |
| AsyncTaskReceipt 定义与所有权规则 | ACC-TODO-009-B | 新增 struct + ownership HMAC 模拟 |
| 与 AgentRequest/AgentResult 边界 | ACC-TODO-009-B | 单测验证无字段污染 |
