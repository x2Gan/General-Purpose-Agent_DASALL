# ACC-TODO-017 设计收敛文档

## 1. 任务定义

实现 AdmissionController（含 RateLimit/Idempotency 协作逻辑），在 runtime dispatch 之前收敛并发配额、幂等冲突和 replay hit 判定，输出统一 AccessAdmissionResult。

本任务范围：

1. 落盘 access/src/AdmissionController.h 与 access/src/AdmissionController.cpp。
2. 将 AdmissionController 接入 dasall_access 静态库。
3. 新增 AdmissionControllerTest.cpp、RateLimitGateTest.cpp、IdempotencyGuardTest.cpp、AdmissionReplayHitTest.cpp。
4. 回写 TODO 状态与证据，并完成提交推送。

## 2. 边界与职责

### 2.1 职责

1. 在请求进入 RuntimeBridge 之前执行并发配额判定。
2. 生成并维护幂等签名，区分 conflict 与 replay hit。
3. 发放/释放 inflight ticket，维护 admission 计数一致性。
4. 在请求完成后记录 completion，以支持后续 replay 命中。

### 2.2 非职责

1. 不做认证和授权，不替代 SubjectResolver/AuthenticatorChain/AccessPolicyGate。
2. 不构造 AgentRequest，不替代 RequestNormalizer。
3. 不直接调用 runtime，不替代 RuntimeBridge。
4. 不做长期结果存储，replay 只保留幂等窗口内最小记录。

## 3. 本地证据与外部参考

### 3.1 本地证据

1. Access 详设 6.13、6.14.7 明确 AdmissionController 负责 busy/conflict/replay-hit/admit 四类出口，且必须 fail-closed。
2. IAdmissionController 公共接口已在 ACC-TODO-010 冻结，017 只需提供 module-local concrete implementation。
3. TODO 指定 017 的目标函数与测试出口为 admit/release_ticket/record_completion + 四个单测，可直接映射到实现。

### 3.2 外部参考

1. IETF Idempotency-Key draft 强调同一幂等键在处理中的冲突与已完成结果重放应被显式区分。本任务据此将 in-flight 命中映射为 conflict_hit，将 completed 命中映射为 replay_hit。

## 4. 数据与接口说明

### 4.1 内部数据模型

1. InflightTicket
   - 字段：ticket_ref、signature、issued_at
   - 用途：表示当前占用并发配额的请求。

2. IdempotencyRecord
   - 字段：signature、inflight_ticket_ref、completed、replay_receipt_ref、expires_at
   - 用途：维护幂等窗口内处理态与已完成态。

3. AdmissionStateSnapshot
   - 字段：inflight_count、tracked_signatures
   - 用途：测试和调试时观察内部状态（仅 internal）。

### 4.2 接口

1. admit(const RuntimeDispatchRequest&)
2. release_ticket(const std::string& ticket_ref)
3. record_completion(const std::string& ticket_ref, const RuntimeDispatchResult&)
4. acquire_inflight_ticket(...)
5. check_idempotency(...)
6. release_inflight_ticket(...)

## 5. 关键流程与时序

### 5.1 normal admit

1. 清理幂等窗口过期记录。
2. 判定并发上限。
3. 构造签名并检查 idempotency 记录。
4. 无冲突时发放 ticket，返回 admitted=true。

### 5.2 conflict

1. 同签名请求在 inflight 状态再次进入。
2. 返回 conflict_hit=true，reject_reason=idempotency_conflict。

### 5.3 replay hit

1. 同签名请求命中 completed 记录。
2. 返回 replay_hit=true，并携带 replay_receipt_ref。

### 5.4 completion

1. record_completion() 根据 ticket_ref 回写 completed 记录。
2. 清理 inflight 占用，保留 replay 信息直到窗口过期。

## 6. 决策规则

1. inflight 数达到 max_inflight_requests 时拒绝，reason=concurrency_limit_exceeded。
2. in-flight 幂等签名重复时返回 conflict_hit，禁止再次 dispatch。
3. completed 幂等签名重复时返回 replay_hit，禁止再次 dispatch。
4. 任意状态不一致（未知 ticket、异常签名）都按 fail-closed 最小语义处理，不 silent loss。

## 7. Design -> Build 映射

| 设计项 | Build 落点 |
|---|---|
| admission internal types 与实现 | access/src/AdmissionController.h / .cpp |
| access 库接线 | access/CMakeLists.txt |
| normal admit 路径 | tests/unit/access/AdmissionControllerTest.cpp |
| 并发配额拒绝路径 | tests/unit/access/RateLimitGateTest.cpp |
| 幂等冲突路径 | tests/unit/access/IdempotencyGuardTest.cpp |
| replay hit 路径 | tests/unit/access/AdmissionReplayHitTest.cpp |
| 测试注册 | tests/unit/access/CMakeLists.txt |

## 8. 文件范围

1. access/src/AdmissionController.h
2. access/src/AdmissionController.cpp
3. access/CMakeLists.txt
4. tests/unit/access/AdmissionControllerTest.cpp
5. tests/unit/access/RateLimitGateTest.cpp
6. tests/unit/access/IdempotencyGuardTest.cpp
7. tests/unit/access/AdmissionReplayHitTest.cpp
8. tests/unit/access/CMakeLists.txt
9. docs/todos/access/DASALL_access子系统专项TODO.md
10. 本文档

## 9. 验收三件套

### 9.1 代码目标

1. 实现 AdmissionController concrete class（实现 IAdmissionController）。
2. 收敛 busy/conflict/replay-hit/admit 四类准入语义。

### 9.2 测试目标

1. AdmissionControllerTest：normal admit。
2. RateLimitGateTest：并发配额拒绝。
3. IdempotencyGuardTest：inflight conflict。
4. AdmissionReplayHitTest：completion 后 replay 命中。

### 9.3 验收命令

```bash
cmake --build build-ci --target \
  dasall_access_admission_controller_core_unit_test \
  dasall_access_rate_limit_gate_unit_test \
  dasall_access_idempotency_guard_unit_test \
  dasall_access_admission_replay_hit_unit_test && \
ctest --test-dir build/vscode-linux-ninja -R "AdmissionControllerTest|RateLimitGateTest|IdempotencyGuardTest|AdmissionReplayHitTest" --output-on-failure
```

说明：当前仓库全量 dasall_unit_tests 仍受 knowledge 既有编译问题影响，本任务采用定向构建与定向 ctest 验收。

## 10. 风险与回退

1. 当前 replay 记录是内存态窗口实现，后续扩展分布式场景时应保持 admit/release/record_completion 语义不变。
2. 如后续引入更细粒度速率策略，应在 AdmissionController 内部扩展，不拆裂为入口侧散落逻辑。
3. 未知 ticket 的 completion 回调按 no-op 处理，避免破坏 inflight 状态一致性。
