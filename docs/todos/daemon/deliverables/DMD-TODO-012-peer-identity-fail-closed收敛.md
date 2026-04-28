# DMD-TODO-012 peer identity fail-closed 收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只收敛 daemon 本地 peer identity 缺失时的 fail-closed 路径。
2. 覆盖范围限定在 `DaemonProtocolAdapter`、`SubjectResolver`、`AccessPolicyGate` 与对应单测。
3. 不扩张到 status/cancel/diag 命令路由与 pipeline 组装任务。

## 2. 设计与实现结论

1. `DaemonProtocolAdapter::describe_local_peer_uid_fact()` 在 `describe_peer` 失败时保持 fail-closed：
   - 保留 `actor_ref`；
   - `is_local_socket_peer=false`；
   - `eligible_for_local_trusted=false`。
2. `SubjectResolver::resolve()` 新增 daemon 专属 fail-closed：
   - 对 `entry_type=daemon`，若缺失可验证的本地 peer identity（`is_local_socket_peer=false` 或 `eligible_for_local_trusted=false`），直接拒绝并返回 `missing_local_peer_identity`。
3. `AccessPolicyGate::evaluate_override_request()` 新增 daemon privileged deny 路径：
   - daemon 入口的 privileged override 仅允许 `local_trusted` 且 `actor_ref` 形如 `local://uid/*`；
   - 不满足时拒绝并返回 `daemon_peer_identity_required`。

## 3. 代码落盘

1. 更新 `access/src/SubjectResolver.cpp`：
   - 增加 daemon entry 类型判断；
   - 在 local trusted 推导失败后，对 daemon 入口执行 fail-closed 拒绝。
2. 更新 `access/src/AccessPolicyGate.cpp`：
   - 增加 daemon privileged 操作的 local trusted 身份前置校验；
   - 在 override 路径中显式拒绝缺失本地 peer 身份的请求。
3. 更新 `tests/unit/access/DaemonProtocolAdapterLocalTrustedTest.cpp`：
   - 新增 `describe_peer` failure 场景，验证 adapter 不会错误授予 local trusted。
4. 更新 `tests/unit/access/SubjectResolverLocalTrustedTest.cpp`：
   - 新增 daemon 入口缺失 peer identity 场景，验证 reject reason 为 `missing_local_peer_identity`。
5. 新增 `tests/unit/access/DaemonPeerIdentityFailClosedTest.cpp`：
   - 覆盖 daemon privileged override 在 JWT/空 actor_ref 下拒绝；
   - 覆盖 local_trusted + local actor_ref 允许路径。
6. 更新 `tests/unit/access/CMakeLists.txt`：
   - 注册 `DaemonPeerIdentityFailClosedTest`。

## 4. 验收命令与结果

1. `Build_CMakeTools(buildTargets=["dasall_access_daemon_protocol_adapter_local_trusted_unit_test"])`：通过。
2. `Build_CMakeTools(buildTargets=["dasall_access_subject_resolver_local_trusted_unit_test"])`：通过。
3. `Build_CMakeTools(buildTargets=["dasall_access_daemon_peer_identity_fail_closed_unit_test"])`：通过。
4. `Build_CMakeTools(buildTargets=["dasall_unix_ipc_provider_peer_identity_unit_test"])`：通过。
5. `RunCtest_CMakeTools(tests=["DaemonProtocolAdapterLocalTrustedTest","SubjectResolverLocalTrustedTest","DaemonPeerIdentityFailClosedTest","UnixIpcProviderPeerIdentityTest"])`：4/4 通过。

备注：CTest stderr 中存在仓库基线 `DartConfiguration.tcl` 提示，但返回码为 0，不影响本任务验收。

## 5. 完成判定

满足 DMD-TODO-012 完成条件：

1. local trusted、remote/untrusted、describe_peer failure 三类路径均有明确 Allow/Deny 结果。
2. daemon privileged command 在缺失 peer identity 时显式拒绝（fail-closed）。
