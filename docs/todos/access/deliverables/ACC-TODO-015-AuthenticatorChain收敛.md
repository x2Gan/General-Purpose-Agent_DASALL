# ACC-TODO-015 设计收敛文档

## 1. 任务定义

实现 AuthenticatorChain，在 access 主链中根据 SubjectResolver 的输出选择认证链、执行最小 credential 校验，并产出 authenticated / challenge / reject 三类显式结论。

本任务范围：

1. 落盘 access/src/AuthenticatorChain.h 与 access/src/AuthenticatorChain.cpp。
2. 将认证链接入 dasall_access 静态库。
3. 新增 AuthenticatorChainTest.cpp、AuthenticatorChainChallengeTest.cpp、AuthenticatorChainSecretFailureTest.cpp。
4. 回写 TODO 状态与证据，并完成提交推送。

## 2. 边界与职责

### 2.1 职责

1. 消费 SubjectResolveOutcome，选择与 auth_method / channel_ref 对应的认证链。
2. 对 local_trusted、JWT、token、mTLS、simulator_stub 五类 v1 认证方式执行最小 fail-closed 校验。
3. 在认证成功后补齐 SubjectIdentity 的最终信任属性。
4. 在需要补充认证时返回 challenge 结论，在 secret backend 或凭据校验失败时显式拒绝。

### 2.2 非职责

1. 不重复解析入口 hints，不替代 SubjectResolver。
2. 不做授权决策，不替代 AccessPolicyGate。
3. 不管理 secret 明文存储，也不直接实现协议层 challenge payload。
4. 不把认证链内部 supporting types 暴露到 access/include/ 公共 ABI。

## 3. 本地证据与外部参考

### 3.1 本地证据

1. Access 详设 6.3、6.14.7 已把 AuthenticatorChain 定义为 SubjectResolver 之后、PolicyGate 之前的认证执行器，并要求 challenge、reject、trusted success 三条路径可断言。
2. ACC-TODO-014 已冻结 SubjectResolveOutcome，允许 015 只聚焦链选择、校验和失败映射，不再处理入口 hints 合并。
3. AccessAuthView 已提供 trusted_local_subjects、auth_provider_ref 和 strict_auth_required，足以形成 v1 认证治理输入。

### 3.2 外部参考

1. OWASP Authentication Cheat Sheet 强调：认证链失败必须 fail-closed；高风险或事实不足场景应 step-up challenge；对 secret / credential 失败不能继续尝试低安全默认放行。本任务据此固定“secret backend failure / credential missing -> reject”，“resolver 已要求 challenge -> challenge 优先”的语义。

## 4. 数据与接口说明

### 4.1 内部数据模型

1. AuthChallenge
   - 字段：challenge_type、reason_code、detail
   - 用途：承接 resolver 的 challenge plan，并交给协议层做后续映射。

2. AuthenticationOutcome
   - 字段：authenticated、rejected、chain_ref、SubjectIdentity subject_identity、std::optional<AuthChallenge> challenge、failure_reason
   - 用途：作为认证链唯一输出对象，禁止 silent fallback。

### 4.2 内部接口

1. authenticate(const SubjectResolveOutcome&, const AccessAuthView&)
   - 主入口，根据 resolver 结论进入 success / challenge / reject 三路之一。

2. select_chain(const SubjectResolveOutcome&, const AccessAuthView&)
   - 将 auth_method 收口到 local_trusted / jwt / token / mtls / simulator_stub 五类链标识。

3. verify_credentials(const SubjectResolveOutcome&, const AccessAuthView&, std::string_view chain_ref)
   - 执行 v1 最小 credential 校验与 fail-closed 映射。

4. merge_subject_attributes(SubjectIdentity&, const AccessAuthView&, std::string_view chain_ref)
   - 成功后补齐 trust_level、tenant_ref 与 auth_metadata。

5. map_failure_reason(std::string_view reason_code)
   - 把内部失败码映射为稳定的拒绝原因。

## 5. 关键流程与时序

### 5.1 成功路径

1. authenticate() 先处理 resolver 已 reject / challenge 的前置结论。
2. 若 resolver 已 resolved，则调用 select_chain() 确定认证链。
3. verify_credentials() 校验当前链。
4. 校验成功后通过 merge_subject_attributes() 回填最终主体属性，并输出 authenticated=true。

### 5.2 Challenge 路径

1. 若 resolver 已给出 challenge plan，则认证链直接返回 challenge。
2. 不允许在 resolver 已要求 challenge 的情况下再退回匿名成功。

### 5.3 Reject 路径

1. resolver 已 reject 时，直接映射 failure_reason。
2. secret backend 不可用、凭据缺失、allowlist 不匹配、未知 auth_method 时，认证链必须 fail-closed。
3. 认证失败后不得继续尝试更低安全级别的默认放行链。

## 6. 决策规则

1. local_trusted 只在 trusted_local_subjects 包含 actor_ref 时通过。
2. 当 auth_provider_ref 为 secret://unavailable 时，依赖外部 secret backend 的链统一返回 secret_backend_unavailable。
3. JWT / token / mTLS / simulator_stub 在 actor_ref 或 auth_method 缺失时统一拒绝 credential_missing。
4. map_failure_reason() 对外只暴露稳定失败语义，不泄漏实现细节。

## 7. Design -> Build 映射

| 设计项 | Build 落点 |
|---|---|
| internal authentication outcome types | access/src/AuthenticatorChain.h |
| chain selection / verification / failure mapping | access/src/AuthenticatorChain.cpp |
| access 库接线 | access/CMakeLists.txt |
| trusted success 验证 | tests/unit/access/AuthenticatorChainTest.cpp |
| challenge 验证 | tests/unit/access/AuthenticatorChainChallengeTest.cpp |
| secret backend failure 验证 | tests/unit/access/AuthenticatorChainSecretFailureTest.cpp |
| 测试注册 | tests/unit/access/CMakeLists.txt |

## 8. 文件范围

1. access/src/AuthenticatorChain.h
2. access/src/AuthenticatorChain.cpp
3. access/CMakeLists.txt
4. tests/unit/access/AuthenticatorChainTest.cpp
5. tests/unit/access/AuthenticatorChainChallengeTest.cpp
6. tests/unit/access/AuthenticatorChainSecretFailureTest.cpp
7. tests/unit/access/CMakeLists.txt
8. docs/todos/access/DASALL_access子系统专项TODO.md
9. 本文档

## 9. 验收三件套

### 9.1 代码目标

1. 实现 module-local AuthenticatorChain 与 supporting outcome types。
2. 形成 credential success、challenge、secret backend failure 的稳定判定。

### 9.2 测试目标

1. AuthenticatorChainTest：验证 trusted success 与最终主体属性补齐。
2. AuthenticatorChainChallengeTest：验证 resolver challenge 可被认证链原样承接。
3. AuthenticatorChainSecretFailureTest：验证 secret backend 故障显式 fail-closed。

### 9.3 验收命令

```bash
cmake --build build-ci --target \
  dasall_access_authenticator_chain_unit_test \
  dasall_access_authenticator_chain_challenge_unit_test \
  dasall_access_authenticator_chain_secret_failure_unit_test && \
ctest --test-dir build/vscode-linux-ninja -R "AuthenticatorChain(Test|ChallengeTest|SecretFailureTest)" --output-on-failure
```

说明：当前仓库全量 dasall_unit_tests 仍受 knowledge 既有编译问题影响，本任务使用定向目标验收。

## 10. 风险与回退

1. 当前 v1 只实现最小认证链选择与 fail-closed 语义，不引入真实 JWT 签名校验或证书链验证细节。
2. 若后续引入真实 secret backend 或多步 challenge，可继续扩 verify_credentials() 与 AuthChallenge，但保持 authenticate() 主签名不变。
3. 任何后续扩展都不得把 internal auth supporting types 抬升到公共 ABI。
