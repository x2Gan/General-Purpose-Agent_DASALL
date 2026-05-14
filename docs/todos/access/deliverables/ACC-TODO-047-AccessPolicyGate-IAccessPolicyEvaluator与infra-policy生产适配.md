# ACC-TODO-047 设计收敛文档

## 1. 任务定义

将 Access 的 `AccessPolicyGate` 从本地 `PolicyBackendSnapshot` 布尔 stub 收敛为 `IAccessPolicyEvaluator` seam，并在 daemon / gateway production submit path 上接入 `infra/policy::ISecurityPolicyManager`。同时保留 snapshot backend 仅作为 unit fake，确保 policy input 完整投影 `subject/channel/environment/operation/target/fingerprint`，并在 policy backend unavailable 时保持 fail-closed。

## 2. 本地证据

1. 专项 TODO 将 `ACC-TODO-047` 定义为 P1-3 / R3 安全治理任务，要求 `AccessPolicyGate` 不再直接依赖本地 snapshot 布尔量，而是通过可替换 evaluator seam 接入真实 `infra/policy`。
2. `access/src/AccessPolicyGate.h/.cpp` 现已新增 `AccessPolicyQuery`、`IAccessPolicyEvaluator` 与 `make_infra_policy_evaluator(...)`；submit / diagnostics / override / task query 都可以通过统一 query builder 向 evaluator 投影 `actor/channel/operation/target/fingerprint`。
3. `access/include/AccessGatewayFactory.h` 与 `access/src/AccessGatewayFactory.cpp` 现已新增 `security_policy_manager` seam；daemon / gateway pipeline 会在有 manager 时构造 production evaluator，并把 `runtime_policy_snapshot` 与 bootstrap revision 投影为 `snapshot_fingerprint`。
4. `tests/unit/access/AccessPolicyInputAttributeTest.cpp` 已验证 seam query 携带 `subject/channel/environment/operation/target/fingerprint`；`tests/integration/access/AccessPolicyEvaluatorIntegrationTest.cpp` 已验证 production factory 会把请求投影给真实 policy manager；`tests/integration/access/AccessObservabilityMainChainIntegrationTest.cpp` 则用 unavailable manager 证明 fail-closed 路径仍会留下 observability denial 证据。
5. `tests/integration/access/CMakeLists.txt` 已将 `AccessPolicyEvaluatorIntegrationTest` 纳入 `gate-int-08 / access-v1-production-gate` 标签族，说明 047 不是局部对象单测，而是 production seam focused integration 证据。

## 3. 外部参考

1. OWASP Authorization Cheat Sheet 强调 deny by default / fail securely；本任务将该原则具体落为 policy manager unavailable 时拒绝请求，而不是回退为 allow：https://cheatsheetseries.owasp.org/cheatsheets/Authorization_Cheat_Sheet.html
2. NIST SP 800-162 对基于主体、客体、环境属性的授权判定做了正式化描述；本任务把 `subject/channel/environment/operation/target` 投影成 evaluator query，正是为了避免 Access 层继续靠布尔 stub 做失真的授权判定：https://csrc.nist.gov/pubs/sp/800/162/upd2/final
3. XACML PDP / PEP 分离强调 enforcement path 应通过决策点解释策略；本任务对应做法是让 `AccessPolicyGate` 保持 PEP 角色，而把真实策略解释委托给 `infra/policy` manager：https://docs.oasis-open.org/xacml/3.0/xacml-3.0-core-spec-os-en.html

## 4. 边界与职责

### 4.1 边界

1. 本任务只收敛 Access 到 `infra/policy` 的生产 evaluator seam，不扩写 app main、`RuntimeDependencySet` 或 `infra/policy` 公共 ABI。
2. 本任务不把 Access 自己升级成新的 policy owner；全局策略解释权仍属于 `infra/policy`，符合 DASALL ADR 边界。
3. 本任务不把 observability 全量 sink 接线写成已闭合结论；047 只要求 policy fail-closed 与 focused evidence 成立，更广日志/指标/追踪/审计矩阵留给 048。

### 4.2 职责

| 对象 | 职责 | 非职责 |
|---|---|---|
| `AccessPolicyGate` | 规范化 policy query、执行 fail-closed 输入校验、解释 evaluator 结果到 Access decision | 不自己拥有策略快照来源，不直接决定全局 policy owner |
| `IAccessPolicyEvaluator` | 为 Access 提供可替换 policy evaluation seam | 不暴露 `infra/policy` 之外的新公共治理接口 |
| `InfraPolicyEvaluator` | 将 `AccessPolicyQuery` 映射为 `PolicyQueryContext` 并调用 `ISecurityPolicyManager` | 不负责 app main 注入或 readiness 聚合 |
| `AccessGatewayFactory` | 在 daemon / gateway pipeline 中选择 production evaluator 或 legacy snapshot fake，并附带 snapshot fingerprint | 不负责扩张到 runtime dependency set 或修改 app composition root |

## 5. 数据与接口说明

1. `access/src/AccessPolicyGate.h`
   - 新增 `AccessPolicyQuery`，显式承载 `subject_identity`、`operation_target`、`entry_type`、`protocol_kind`、`request_id`、`session_id`、`trace_id`、`snapshot_fingerprint`、`sensitive_request`。
   - 新增 `IAccessPolicyEvaluator` seam 与 `make_infra_policy_evaluator(...)` 声明。
   - `AccessPolicyEvaluationInput` 新增 `snapshot_fingerprint`，供生产 path 把治理版本投影带入 evaluator。
2. `access/src/AccessPolicyGate.cpp`
   - 新增 `InfraPolicyEvaluator`，把 Access query 转换为 `infra::policy::PolicyQueryContext`。
   - 新增 policy decision mapping helper，把 `Allow/Deny/RequireConfirmation` 与 snapshot/generation/evidence 统一投影回 `AccessPolicyEvaluationResult`。
   - `backend unavailable` 统一映射为 fail-closed deny，不再使用 allow fallback。
3. `access/include/AccessGatewayFactory.h`
   - `DaemonAccessPipelineOptions` 与 `GatewayAccessPipelineOptions` 新增 `security_policy_manager`。
4. `access/src/AccessGatewayFactory.cpp`
   - 新增 `build_policy_snapshot_fingerprint(...)`，将 bootstrap revision 与 runtime snapshot generation / profile id 归一化为 `SnapshotVersionFingerprint`。
   - daemon / gateway submit path 与 daemon diagnostics path 现在都优先走 production evaluator；仅在无 manager 注入时才回到 `PolicyBackendSnapshot` fake 路径。
5. `tests/integration/access/AccessPolicyEvaluatorIntegrationTest.cpp`
   - 使用 fake `ISecurityPolicyManager` 证明 production factory 会把 `module=request access.gateway.http_unary`、`operation=submit`、`target=entry/gateway`、`request/session/trace/actor` 正确投影到 policy manager。
6. `tests/integration/access/AccessObservabilityMainChainIntegrationTest.cpp`
   - 将 backend unavailable 场景从旧的 `options.policy_backend_available = false` 改为真实 unavailable manager，证明 fail-closed 证据已从 stub 语义切换到 production seam。

## 6. 流程与时序

1. daemon / gateway pipeline 构建时，如果 options 中提供 `security_policy_manager`，factory 就调用 `make_infra_policy_evaluator(...)` 构造 production evaluator。
2. submit / diagnostics 请求进入 `AccessPolicyGate` 前，factory 先补齐 `policy_input.snapshot_fingerprint`。
3. `AccessPolicyGate` 将 `authentication + packet + operation_target + fingerprint` 归一化为 `AccessPolicyQuery`。
4. `InfraPolicyEvaluator` 把 query 转成 `PolicyQueryContext` 并调用 `ISecurityPolicyManager::evaluate(...)`。
5. 若 manager 返回允许，则主链继续进入 Admission / Runtime；若 manager 不可用、返回 deny 或 malformed query，则 Access 立即 fail-closed。
6. backend unavailable 的 focused integration 继续通过 observability main-chain test 验证 denied event 与 runtime skip 结果。

## 7. Design -> Build 映射

| 设计项 | Build 落点 | 完成判定 |
|---|---|---|
| `IAccessPolicyEvaluator` seam | `access/src/AccessPolicyGate.h/.cpp` | Access 不再把 production policy path 写死为本地 snapshot 布尔 stub |
| production `infra/policy` 适配 | `access/src/AccessPolicyGate.cpp`、`access/src/AccessGatewayFactory.cpp` | factory 注入 manager 时走真实 evaluator，manager unavailable 时 fail-closed |
| policy query 完整属性投影 | `access/src/AccessPolicyGate.cpp`、`tests/unit/access/AccessPolicyInputAttributeTest.cpp` | subject/channel/environment/operation/target/fingerprint 可自动断言 |
| production seam integration | `tests/integration/access/AccessPolicyEvaluatorIntegrationTest.cpp` | factory + gateway submit 会把 query 投影给真实 manager fake |
| unavailable fail-closed 回归 | `tests/integration/access/AccessObservabilityMainChainIntegrationTest.cpp` | runtime 不会被调用，observability denial 事件仍成立 |

## 8. 文件范围

1. `access/include/AccessGatewayFactory.h`
2. `access/src/AccessGatewayFactory.cpp`
3. `access/src/AccessPolicyGate.h`
4. `access/src/AccessPolicyGate.cpp`
5. `tests/unit/access/AccessPolicyInputAttributeTest.cpp`
6. `tests/unit/access/AccessPolicyOverrideGateTest.cpp`
7. `tests/unit/access/CMakeLists.txt`
8. `tests/integration/access/CMakeLists.txt`
9. `tests/integration/access/AccessPolicyEvaluatorIntegrationTest.cpp`
10. `tests/integration/access/AccessObservabilityMainChainIntegrationTest.cpp`
11. `docs/todos/access/DASALL_access子系统专项TODO.md`
12. `docs/todos/access/deliverables/ACC-TODO-047-AccessPolicyGate-IAccessPolicyEvaluator与infra-policy生产适配.md`
13. `docs/worklog/DASALL_开发执行记录.md`

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| B1 | 在 `AccessPolicyGate` 内引入 evaluator seam，并保留 snapshot backend 仅作 unit fake | `AccessPolicyGateTest`、`AccessPolicyBackendFailureTest`、`AccessPolicyOverrideGateTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessPolicyGateTest|AccessPolicyBackendFailureTest|AccessPolicyOverrideGateTest" --output-on-failure` |
| B2 | 将 policy input 投影为包含 fingerprint 的结构化 query | `AccessPolicyInputAttributeTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessPolicyInputAttributeTest" --output-on-failure` |
| B3 | 让 daemon / gateway factory 优先走 `infra/policy` production evaluator，并保持 unavailable fail-closed | `AccessPolicyEvaluatorIntegrationTest`、`AccessPolicyBackendUnavailableIntegrationTest`、`AccessObservabilityMainChainIntegrationTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessPolicyEvaluatorIntegrationTest|AccessPolicyBackendUnavailableIntegrationTest|AccessObservabilityMainChainIntegrationTest" --output-on-failure` |

## 10. 验收结果

1. `Build_CMakeTools(buildTargets=["dasall_access_policy_evaluator_integration_test","dasall_access_observability_main_chain_integration_test","dasall_access_policy_input_attribute_unit_test","dasall_access_policy_gate_unit_test","dasall_access_policy_backend_failure_unit_test","dasall_access_policy_override_gate_unit_test"])`
   - 结果：通过。
   - 说明：重编了 `dasall_access`、新的 production evaluator integration target、backend unavailable observability integration target，以及受影响的 policy gate unit targets。
2. `RunCtest_CMakeTools(tests=["AccessPolicyInputAttributeTest","AccessPolicyGateTest","AccessPolicyBackendFailureTest","AccessPolicyOverrideGateTest","AccessPolicyEvaluatorIntegrationTest","AccessPolicyBackendUnavailableIntegrationTest","AccessObservabilityMainChainIntegrationTest"])`
   - 结果：通过，7/7 passed。
3. 额外观察
   - `AccessPolicyBackendUnavailableIntegrationTest` 仍是 alias，但其底层场景已经从旧布尔 stub 切换为真实 unavailable policy manager。
   - 本轮没有扩写 `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp` 或 `RuntimeDependencySet`，因此 047 的作用域仍严格限制在 Access 内部 seam 与 focused integration 证据。

## 11. D Gate 结果

Gate = PASS。

1. `AccessPolicyGate` 已从本地 snapshot stub 收敛为 `IAccessPolicyEvaluator` seam，production path 可以通过 `infra/policy` manager 执行 per-request authorization。
2. policy input 的 subject / channel / environment / operation / target / fingerprint 已具 focused unit 证据，backend unavailable 也具 production integration fail-closed 证据。
3. 本轮仍未把 observability 全量 sink 接线写成已闭合；更广安全治理矩阵继续留给 048 / 051，不外推为全部安全可交付。
