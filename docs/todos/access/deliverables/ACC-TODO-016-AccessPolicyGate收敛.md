# ACC-TODO-016 设计收敛文档

## 1. 任务定义

实现 AccessPolicyGate，在 access 主链中基于已认证主体、请求操作视图和敏感入口来源事实执行 fail-closed 授权判定，输出统一 AccessDecisionProof。

本任务范围：

1. 落盘 access/src/AccessPolicyGate.h 与 access/src/AccessPolicyGate.cpp。
2. 将策略门接入 dasall_access 静态库。
3. 新增 AccessPolicyGateTest.cpp、AccessPolicyOverrideGateTest.cpp、AccessPolicyBackendFailureTest.cpp。
4. 回写 TODO 状态与证据，并完成提交推送。

## 2. 边界与职责

### 2.1 职责

1. 从已认证请求构造 operation/target 视图并执行授权判定。
2. 对 submit、task query、override 三类路径输出 allow/deny/require_confirmation 语义。
3. 对 policy backend 不可用、上下文构造失败、来源事实不完整等场景执行 fail-closed 拒绝。
4. 生成结构稳定的 AccessDecisionProof，供后续 Admission/Normalizer 使用。

### 2.2 非职责

1. 不做身份认证，不替代 SubjectResolver/AuthenticatorChain。
2. 不执行限流、并发和幂等判定，不替代 AdmissionController。
3. 不实现 infra/policy 共享语义，不暴露 policy backend 内部对象。
4. 不执行 runtime override 或 diagnostics pull 具体动作，只做准入授权判定。

## 3. 本地证据与外部参考

### 3.1 本地证据

1. Access 详设 6.7.1、6.14.7 固定了 AccessPolicyGate 的 internal 边界与推荐接口：evaluate_submit、evaluate_task_query、evaluate_override_request。
2. ACC-TODO-015 已完成认证链，认证结论可以作为策略门唯一上游输入，不需要在 016 重新解析主体 hints。
3. TODO 明确 016 前置为 003、009、015，且 ACC-BLK-003 已解阻，可直接推进 override/diagnostics 入口准入判定。

### 3.2 外部参考

1. OWASP Authorization Cheat Sheet 强调 deny-by-default 和 fail-closed：当授权上下文不完整或策略后端不可用时必须拒绝。本任务据此将 backend unavailable、context malformed、override source invalid 统一映射为 deny。

## 4. 数据与接口说明

### 4.1 内部数据模型

1. OperationTargetView
   - 字段：operation、target_type、target_ref
   - 用途：表达 Access action taxonomy 的最小查询上下文。

2. OverrideSourceFact
   - 字段：source_type、has_config_patch_metadata、path_op_summary_complete、ttl_valid、target_ref_present
   - 用途：表达 runtime_override 来源与结构完整性事实。

3. PolicyBackendSnapshot
   - 字段：backend_available、allow_submit、allow_task_query、allow_override、require_confirmation_for_override、decision_ref
   - 用途：作为 v1 policy backend 抽象结果输入，隔离真正 backend 实现。

4. AccessPolicyEvaluationInput / AccessPolicyEvaluationResult
   - 输入由 AuthenticationOutcome + InboundPacket 组成。
   - 输出包含 allowed / requires_confirmation / AccessDecisionProof / reject_reason。

### 4.2 内部接口

1. evaluate_submit(const AccessPolicyEvaluationInput&, const PolicyBackendSnapshot&)
2. evaluate_task_query(const AccessPolicyEvaluationInput&, std::string_view task_ref, const PolicyBackendSnapshot&)
3. evaluate_override_request(const AccessPolicyEvaluationInput&, const OverrideSourceFact&, const PolicyBackendSnapshot&)
4. build_query_context(...)
5. map_policy_result(...)

## 5. 关键流程与时序

### 5.1 submit 路径

1. 校验 authenticated=true。
2. 构造 operation=submit、target_type=entry。
3. 调用 map_policy_result。
4. 输出 allow 或 deny 的 AccessDecisionProof。

### 5.2 task query 路径

1. 校验 authenticated=true 和 task_ref 非空。
2. 构造 operation=task_query、target_type=async_task。
3. backend 不可用或 proof 不完整时 deny。

### 5.3 override 路径

1. 先校验 OverrideSourceFact 的来源类型和结构完整性。
2. 通过后构造 operation=runtime_override、target_type=runtime_policy_patch。
3. backend 返回 require confirmation 时输出 requires_confirmation，不得当作 allow。

## 6. 决策规则

1. 未认证请求统一拒绝，reason=authentication_required。
2. backend_available=false 统一拒绝，reason=policy_backend_unavailable。
3. override 来源不在 allowlist 或结构不完整时拒绝，reason=override_source_invalid。
4. require_confirmation 只作为独立结论输出，不降级为 allow。

## 7. Design -> Build 映射

| 设计项 | Build 落点 |
|---|---|
| internal policy gate types | access/src/AccessPolicyGate.h |
| submit/query/override fail-closed 实现 | access/src/AccessPolicyGate.cpp |
| access 库接线 | access/CMakeLists.txt |
| submit 授权路径 | tests/unit/access/AccessPolicyGateTest.cpp |
| override 来源与确认路径 | tests/unit/access/AccessPolicyOverrideGateTest.cpp |
| backend failure 路径 | tests/unit/access/AccessPolicyBackendFailureTest.cpp |
| 测试注册 | tests/unit/access/CMakeLists.txt |

## 8. 文件范围

1. access/src/AccessPolicyGate.h
2. access/src/AccessPolicyGate.cpp
3. access/CMakeLists.txt
4. tests/unit/access/AccessPolicyGateTest.cpp
5. tests/unit/access/AccessPolicyOverrideGateTest.cpp
6. tests/unit/access/AccessPolicyBackendFailureTest.cpp
7. tests/unit/access/CMakeLists.txt
8. docs/todos/access/DASALL_access子系统专项TODO.md
9. 本文档

## 9. 验收三件套

### 9.1 代码目标

1. 实现 module-local AccessPolicyGate。
2. 完成 submit/query/override 三路径 fail-closed 授权判定。

### 9.2 测试目标

1. AccessPolicyGateTest：submit allow/deny 路径。
2. AccessPolicyOverrideGateTest：override source invalid 与 require confirmation。
3. AccessPolicyBackendFailureTest：backend unavailable 拒绝路径。

### 9.3 验收命令

```bash
cmake --build build-ci --target \
  dasall_access_policy_gate_unit_test \
  dasall_access_policy_override_gate_unit_test \
  dasall_access_policy_backend_failure_unit_test && \
ctest --test-dir build/vscode-linux-ninja -R "AccessPolicy(GateTest|OverrideGateTest|BackendFailureTest)" --output-on-failure
```

说明：当前仓库全量 dasall_unit_tests 仍受 knowledge 既有编译问题影响，本任务采用定向构建与定向 ctest 验收。

## 10. 风险与回退

1. 当前以 PolicyBackendSnapshot 表达 backend 结果，是 v1 的最小抽象；后续对接真实 infra/policy 时应保持 evaluate_* 接口稳定。
2. override/diagnostics 仅实现准入判定，不下沉执行细节，避免 016 越权扩张。
3. 若后续 action taxonomy 扩展，应优先扩 build_query_context，不直接散落到各路径分支。
