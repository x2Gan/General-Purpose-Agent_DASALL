# ACC-TODO-014 设计收敛文档

## 1. 任务定义

实现 SubjectResolver，在 access 主链中把入口包、peer 身份事实和入口 hints 收敛成稳定的主体解析结果，并在主体信息不完整或互相冲突时显式产出 challenge / reject 结论。

本任务范围：

1. 落盘 access/src/SubjectResolver.h 与 access/src/SubjectResolver.cpp。
2. 将 resolver 接入 dasall_access 静态库。
3. 新增 SubjectResolverTest.cpp、SubjectResolverLocalTrustedTest.cpp、SubjectResolverChallengeTest.cpp。
4. 回写 TODO 状态与证据，并完成提交推送。

## 2. 边界与职责

### 2.1 职责

1. 由 InboundPacket 派生稳定的 channel_ref。
2. 聚合 peer uid、证书 subject、JWT hint、token hint、simulator hint 等主体线索。
3. 在本地 CLI / daemon 入口满足 allowlist 与 peer identity 条件时产出 local trusted 主体。
4. 在远程入口缺少关键身份事实时返回 challenge plan，而不是生成低可信默认主体。
5. 在多源身份提示互相冲突时显式拒绝。

### 2.2 非职责

1. 不做最终 credential 校验，不替代 AuthenticatorChain。
2. 不做授权、Admission、限流或幂等判断。
3. 不把 internal supporting types 暴露到 access/include/ 公共 ABI。
4. 不因为缺少事实就默认匿名 allow。

## 3. 本地证据与外部参考

### 3.1 本地证据

1. Access 详设 6.7.2、6.14.7 明确 SubjectResolver 负责解析 SubjectIdentity、ChallengePlan 与 SubjectResolveOutcome，并要求 local trusted、remote challenge、identity conflict 三条分支可区分。
2. AccessTypes.h 已冻结 SubjectIdentity、LocalPeerUidFact、AccessAuthView 等 supporting surface，可直接承载 resolver 输出和治理输入。
3. DaemonProtocolAdapter 已把平台 describe_peer() 投影成 LocalPeerUidFact，说明本地 trusted 的事实输入已经具备，不需要把 peer identity 再塞回 shared contracts。

### 3.2 外部参考

1. OWASP Authentication Cheat Sheet 强调：认证失败必须 fail-closed；高风险或事实缺失场景应触发 re-authentication / challenge；认证错误响应需要避免把不同失败原因泄漏成默认放行路径。本任务据此采用“缺少关键信息 -> challenge 或 reject，而不是 anonymous allow”的规则。

## 4. 数据与接口说明

### 4.1 内部数据模型

1. PeerMetadata
   - 字段：certificate_subject、jwt_actor_ref、token_actor_ref、simulator_actor_ref、tenant_ref、device_ref、LocalPeerUidFact local_peer
   - 用途：承载入口适配器或平台侧提供的主体 hints。

2. ResolverView
   - 字段：trusted_local_subjects、strict_auth_required、allow_remote_challenge
   - 用途：表达 access auth 治理视图中 resolver 真正需要的只读子集。

3. ChallengePlan
   - 字段：challenge_type、reason_code、detail
   - 用途：把“需要补充认证”的结论传递给 AuthenticatorChain，不直接绑定协议层 challenge 载荷。

4. SubjectResolveOutcome
   - 字段：resolved、rejected、channel_ref、SubjectIdentity subject_identity、std::optional<ChallengePlan> challenge_plan、reject_reason
   - 用途：作为 resolver 的唯一输出，供后续认证链消费。

### 4.2 内部接口

1. resolve(const InboundPacket&, const PeerMetadata&, const ResolverView&)
   - 入口主函数，返回 SubjectResolveOutcome。

2. derive_channel_ref(const InboundPacket&)
   - 规则：稳定编码 entry_type 与 protocol_kind，为空时返回 unknown 占位片段，但不抛异常。

3. derive_local_subject(const InboundPacket&, const PeerMetadata&, const ResolverView&)
   - 只在本地入口和明确 allowlist 支撑下产出 local trusted 主体。

4. build_challenge_plan(const InboundPacket&, std::string_view reason_code)
   - 根据入口类型与协议形态生成协议无关 challenge 计划。

## 5. 关键流程与时序

### 5.1 正常路径

1. resolve() 先调用 derive_channel_ref() 固定 channel_ref。
2. 若请求来自 CLI / daemon 且 LocalPeerUidFact 明确表明 eligible_for_local_trusted=true，则走 derive_local_subject()。
3. 否则解析证书、JWT、token、simulator 等 hints。
4. 若只有一个一致身份提示，则输出 resolved=true 的 SubjectIdentity。

### 5.2 Challenge 路径

1. 若远程入口没有足够身份事实，但 allow_remote_challenge=true，则调用 build_challenge_plan()。
2. 输出 resolved=false、rejected=false，并带 challenge plan。

### 5.3 Reject 路径

1. 若多个 hints 给出不同 actor_ref，判定为 identity conflict。
2. 若本地 trusted 推断缺少 allowlist 或 peer identity 不可信，则拒绝。
3. 若远程入口禁用 challenge 且关键信息缺失，则拒绝。

## 6. 决策规则

1. local trusted 仅允许 entry_type in {"cli", "daemon"} 且 eligible_for_local_trusted=true。
2. trusted_local_subjects 为空时，不得把任何本地 peer 默认视为 trusted。
3. 证书 / JWT / token / simulator 提示中，只要出现多个不同 actor_ref，必须 reject，避免跨源身份拼装。
4. 远程入口缺失身份事实时，优先 challenge；若治理视图禁止 challenge，则 reject。
5. resolver 产出的 SubjectIdentity.auth_method 只反映来源事实，不代表最终认证已经完成。

## 7. Design -> Build 映射

| 设计项 | Build 落点 |
|---|---|
| internal resolver supporting types | access/src/SubjectResolver.h |
| channel_ref / local trusted / conflict / challenge 实现 | access/src/SubjectResolver.cpp |
| access 库接线 | access/CMakeLists.txt |
| 一致身份与冲突验证 | tests/unit/access/SubjectResolverTest.cpp |
| local trusted 验证 | tests/unit/access/SubjectResolverLocalTrustedTest.cpp |
| remote challenge 验证 | tests/unit/access/SubjectResolverChallengeTest.cpp |
| 测试注册 | tests/unit/access/CMakeLists.txt |

## 8. 文件范围

1. access/src/SubjectResolver.h
2. access/src/SubjectResolver.cpp
3. access/CMakeLists.txt
4. tests/unit/access/SubjectResolverTest.cpp
5. tests/unit/access/SubjectResolverLocalTrustedTest.cpp
6. tests/unit/access/SubjectResolverChallengeTest.cpp
7. tests/unit/access/CMakeLists.txt
8. docs/todos/access/DASALL_access子系统专项TODO.md
9. 本文档

## 9. 验收三件套

### 9.1 代码目标

1. 实现 module-local SubjectResolver 与 supporting types。
2. 形成 local trusted、identity conflict、remote challenge 的稳定判定。

### 9.2 测试目标

1. SubjectResolverTest：验证一致身份提示与 identity conflict。
2. SubjectResolverLocalTrustedTest：验证 local trusted 正向与 allowlist 约束。
3. SubjectResolverChallengeTest：验证远程缺失身份事实时的 challenge 结论。

### 9.3 验收命令

```bash
cmake --build build-ci --target \
  dasall_access_subject_resolver_unit_test \
  dasall_access_subject_resolver_local_trusted_unit_test \
  dasall_access_subject_resolver_challenge_unit_test && \
ctest --test-dir build-ci -R "SubjectResolver(Test|LocalTrustedTest|ChallengeTest)" --output-on-failure
```

说明：当前仓库全量 dasall_unit_tests 仍受 knowledge 既有编译问题影响，本任务使用定向目标验收。

## 10. 风险与回退

1. 若后续入口类型扩展到 HTTP / WS / simulator 以外的新入口，优先扩 PeerMetadata 与 ResolverView，不要把 internal supporting types 抬升到公共 ABI。
2. challenge 只生成协议无关计划，不在本任务引入 HTTP header、CLI prompt 等协议细节。
3. 如果后续需要更细粒度的 risk-based challenge，可以在不改变 resolve() 主签名的前提下扩展 ChallengePlan 字段。
