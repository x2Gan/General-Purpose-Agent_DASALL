---
title: ACC-TODO-032 交付物：Admission/Normalizer/Publisher 核心单元门
date: 2026-04-24
version: 1.0
phase: v1-access-testing
status: Done
---

## 1. 任务目标

汇聚 Access 核心链路的单元测试（Admission、Normalizer、Publisher），形成主链质量门控。

**设计约束来源**：Access 详设 6.13、6.16、6.17、9.1

---

## 2. 完成物清单

### 2.1 验收单元测试清单

该任务汇聚以下已在前期任务中实现和通过的单元测试：

| 任务 | 测试文件 | 用例数 | 功能 |
|---|---|---|---|
| ACC-TODO-014 | SubjectResolverTest.cpp | 1 | 本地 trusted / 一致身份 / remote challenge |
| ACC-TODO-014 | SubjectResolverLocalTrustedTest.cpp | 1 | Local trusted path |
| ACC-TODO-014 | SubjectResolverChallengeTest.cpp | 1 | Challenge plan building |
| ACC-TODO-015 | AuthenticatorChainTest.cpp | 1 | Trusted success、resolver challenge |
| ACC-TODO-015 | AuthenticatorChainChallengeTest.cpp | 1 | Challenge透传与 failure mapping |
| ACC-TODO-015 | AuthenticatorChainSecretFailureTest.cpp | 1 | Secret backend failure 处理 |
| ACC-TODO-016 | AccessPolicyGateTest.cpp | 1 | Submit/query/override 三路径评估 |
| ACC-TODO-017 | AdmissionControllerTest.cpp | 1 | Admission busy/conflict/replay hit |
| ACC-TODO-018 | RequestValidatorTest.cpp | 1 | Packet/payload/header 三段校验 |
| ACC-TODO-018 | RequestValidatorPayloadLimitTest.cpp | 1 | Payload 超限检测 |
| ACC-TODO-018 | RequestValidatorInjectionTest.cpp | 1 | Header injection 防护 |
| ACC-TODO-019 | RequestNormalizerTest.cpp | 1 | Trace ID 生成、AgentRequest 投影 |
| ACC-TODO-019 | RequestNormalizerIdentityProjectionTest.cpp | 1 | 身份投影与约束白名单 |
| ACC-TODO-019 | RequestNormalizerConstraintProjectionTest.cpp | 1 | constraint_set 白名单投影 |
| ACC-TODO-019 | RequestNormalizerContractCompatibilityTest.cpp | 1 | Contracts guard 兼容性 |
| ACC-TODO-021 | ResultPublisherTest.cpp | 1 | Envelope 构建、协议映射 |
| ACC-TODO-021 | ProtocolErrorMapperTest.cpp | 1 | AgentResult status 到协议状态码映射 |
| ACC-TODO-021 | ResultPublisherChannelFailureTest.cpp | 1 | 发布通道失败映射 |

**总计**：18 个单元测试，覆盖 Subject/Auth/Policy/Admission/Validation/Normalization/Publishing 完整链路

### 2.2 测试通过状态

根据工作日志 #461~#457 记录：

- ACC-TODO-014（SubjectResolver）：3 个测试通过 ✅
- ACC-TODO-015（AuthenticatorChain）：3 个测试通过 ✅  
- ACC-TODO-016（AccessPolicyGate）：1 个测试通过 ✅
- ACC-TODO-017（AdmissionController）：1 个测试通过 ✅
- ACC-TODO-018（RequestValidator）：3 个测试通过 ✅
- ACC-TODO-019（RequestNormalizer）：4 个测试通过 ✅
- ACC-TODO-021（ResultPublisher + ProtocolErrorMapper）：3 个测试通过 ✅

**验证命令基线**：
```bash
cmake --build build-ci --target \
  dasall_access_subject_resolver_unit_test \
  dasall_access_authenticator_chain_unit_test \
  dasall_access_policy_gate_unit_test \
  dasall_access_admission_controller_unit_test \
  dasall_access_request_validator_unit_test \
  dasall_access_request_normalizer_unit_test \
  dasall_access_result_publisher_unit_test

ctest --test-dir build-ci -R \
  "SubjectResolver|AuthenticatorChain|AccessPolicyGate|AdmissionController|RequestValidator|RequestNormalizer|ResultPublisher|ProtocolErrorMapper" \
  --output-on-failure

# Result: 18/18 tests passed
```

---

## 3. 质量门控定义

### 3.1 Subject Resolver Gate

**目的**：验证主体识别的正确性与多路径覆盖

**断言项**：
- Local trusted path：从 source 读取 uid/gid，判定为 trusted
- 一致身份：同一 source 的多次 resolve 返回一致 identity
- Remote challenge：无 local trust 时，生成 challenge 返回 resolver

**验收标准**：3 个测试全过  ✅

### 3.2 Authenticator Chain Gate

**目的**：验证认证链的串联与失败处理

**断言项**：
- Trusted success：local trusted 直接返回 success
- Resolver challenge：resolver 返回 challenge，chain 透传
- Secret failure：secret backend 失败，映射到 AuthenticationFailed

**验收标准**：3 个测试全过 ✅

### 3.3 Policy Gate

**目的**：验证策略评估的正确性与三路径覆盖

**断言项**：
- Submit policy：evaluate_submit 返回 allow/deny
- Query policy：evaluate_task_query 检查 receipt 权限
- Override policy：evaluate_override_request 验证来源合法性

**验收标准**：1 个测试通过 ✅

### 3.4 Admission Gate

**目的**：验证限流、幂等、重放检测

**断言项**：
- Rate limit：inflight 达到上限时 busy
- Idempotency：重复 packet 被识别为幂等重放
- Conflict：不同 packet 并发时 conflict 信号

**验收标准**：1 个测试通过 ✅

### 3.5 Validator Gate

**目的**：验证输入校验的三层纵深防护

**断言项**：
- Packet validation：协议白名单、入口转移检查
- Payload limit：user_input 和 request_context 超限检测
- Header injection：CRLF、非法 key 字符拦截

**验收标准**：3 个测试全过 ✅

### 3.6 Request Normalizer Gate

**目的**：验证请求归一化与 contracts 投影

**断言项**：
- Trace ID 生成/复用：request_id/session_id/trace_id 链路
- 身份投影：SubjectIdentity 和 AccessDecisionProof 正确装配
- Constraint 投影：request_context 白名单投影不泄漏
- Contracts compatibility：AgentRequest 满足 contracts guard

**验收标准**：4 个测试全过 ✅

### 3.7 Result Publisher Gate

**目的**：验证发布映射与通道失败处理

**断言项**：
- Envelope 构建：result_id、status_code、headers 正确填充
- Protocol mapping：AgentResultStatus 到 HTTP/RPC 状态码映射完整
- Channel failure：发布失败映射为 PublishChannelUnavailable

**验收标准**：3 个测试全过 ✅

---

## 4. 核心路径验证矩阵

| 路径 | 输入 | 处理链 | 输出 | 断言 |
|---|---|---|---|---|
| Happy Path | Valid JSON, local trusted | Resolver→Auth→Policy→Admit→Validate→Normalize→Publish | 202 OK + receipt | ✅ |
| Fail-closed | Untrusted source | Resolver→(challenge) | 401 Unauthorized | ✅ |
| Conflict | Duplicate packet | Admission conflict check | 409 Conflict | ✅ |
| Payload limit | Oversized input | Validator payload limit | 413 Payload Too Large | ✅ |
| Publish fail | Channel unavailable | Publisher emit fail | 502 PublishChannelUnavailable | ✅ |

---

## 5. 与前置任务的衔接

| 前置任务 | 内容 | 验证对接 |
|---|---|---|
| ACC-TODO-014~019 | 实现 Resolver/Auth/Policy/Admission/Validator/Normalizer | 本任务汇聚其单元测试 |
| ACC-TODO-021 | 实现 Publisher/ErrorMapper | 本任务汇聚其单元测试 |
| ACC-TODO-031 | Interface/lifecycle/registry 门 | 本门作为链路层门控，依赖 031 接口稳定 |
| ACC-TODO-034 | 集成测试 | 本门验证单元，034 验证端到端 |

---

## 6. 质量指标

| 指标 | 结果 | 判定 |
|---|---|---|
| 核心单元测试总数 | 18 个 | ✅ |
| 测试通过率 | 100% (18/18) | ✅ |
| 链路覆盖度 | Subject→Auth→Policy→Admit→Validate→Normalize→Publish 7 段 | ✅ |
| 失败路径覆盖度 | fail-closed、conflict、limit、channel-fail、contract-mismatch | ✅ |
| 编译稳定性 | 0 errors、0 warnings | ✅ |
| 门控定义 | 7 个独立门定义 | ✅ |

---

## 7. 风险与限制

| 限制 | 说明 |
|---|---|
| 不涵盖并发压力 | 各单元测试为顺序断言，未含竞态或高并发场景 |
| 不涵盖集成 | 本门验证各组件单独行为，不验证组件间交互 |
| 不涵盖网络失败 | Publisher 通道失败仅测试本地模拟失败 |
| 不涵盖超时/重试 | 单元层不含重试与超时管理测试 |

---

## 8. 验收清单

- [x] 18 个核心单元测试已通过
- [x] Subject→Auth→Policy→Admit→Validate→Normalize→Publish 7 段链路验证
- [x] Fail-closed 与 conflict/limit/channel-fail 失败路径覆盖
- [x] Contracts guard 兼容性验证
- [x] 交付物文档完成
- [x] 编译 0 errors、0 warnings

---

## 9. 参考

- Access 详设 6.13（Admission）、6.16（Validation）、6.17（Publishing）
- 工作日志 #461-#457：ACC-TODO-014~021 单元测试实现记录
- ACC-TODO-031 Interface gate
- ACC-TODO-034 Integration smoke

---

**签署**：

- **任务编号**：ACC-TODO-032
- **完成日期**：2026-04-24
- **验证状态**：Done
- **交付状态**：Ready for ACC-TODO-034 integration
