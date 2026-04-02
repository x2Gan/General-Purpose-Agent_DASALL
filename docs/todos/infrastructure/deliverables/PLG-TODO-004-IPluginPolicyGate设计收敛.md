# PLG-TODO-004 IPluginPolicyGate 设计收敛

日期：2026-04-01  
任务：PLG-TODO-004  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.3/6.6 明确 PluginPolicyGate 的职责是基于 PolicySnapshot 与 Profile 执行插件准入判定，并返回 PolicyDecisionRef。
2. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 4.2/5/6 明确 IPluginPolicyGate 达到 L2 可执行，但其输入仍写作 manifest/policy_snapshot/profile，尚未给出对象级最小承载形式。
3. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md 已标记 INF-TODO-017 Done，说明 PolicySnapshot、PolicyDecisionRef 与 ISecurityPolicyManager 边界已冻结，不再构成 004 的当前阻塞。
4. PluginManifest 仍受 INF-BLK-09 约束，当前不能在 004 中伪造完整 manifest 对象字段集合。

## 2. 外部参考

1. Microsoft MSDN Magazine《Writing, Loading, and Accessing Plug-Ins》建议将插件准入判定建模为“稳定输入引用 + 明确决策输出”的独立关口，而不是在接口阶段引入完整装载对象。本轮据此把策略判定输入收敛为 descriptor + manifest_ref + profile_id。

## 3. Blocker 修复与 Design 结论

阻塞结论：

1. 原始 004 任务的 evaluate(manifest, policy_snapshot, profile) 依赖尚未冻结的 PluginManifest；若直接写成完整 manifest 入参，会越界踩到 INF-BLK-09。
2. allowlist/trust policy 只在设计语义层存在，当前没有独立公共对象；如果现在强行单独冻结，会把 policy 规则细节从 PolicySnapshot 中拆散。

最小 blocker-fix：

1. 在 IPluginPolicyGate.h 中新增 PluginPolicyRequest，以已完成的 PluginDescriptor 承接治理字段，并用 manifest_ref/profile_id 两个 ref 锚点承接未解阻的 manifest 与 profile 细节。
2. evaluate 统一冻结为 evaluate(request, policy_snapshot)，输出继续复用已冻结的 PolicyDecisionRef，不新增 plugin 私有决策对象。

设计结论：

1. IPluginPolicyGate 只冻结一个 evaluate 入口，职责限定为插件准入判定，不承担策略快照获取、patch、rollback 或 runtime 恢复。
2. PluginPolicyRequest 只暴露 descriptor、manifest_ref、profile_id 三个边界字段，不持有完整 PluginManifest、PolicyBundle 或 ErrorInfo。
3. 返回值继续复用 policy::PolicyDecisionRef，保证 plugin 准入判定沿用现有 policy 决策与 evidence_ref 语义。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 IPluginPolicyGate evaluate 入口 | infra/include/plugin/IPluginPolicyGate.h |
| 冻结最小策略判定输入边界 | PluginPolicyRequest |
| 复用 PolicyDecisionRef 作为决策输出 | policy::PolicyDecisionRef |
| 验证接口签名与有效/无效输入行为 | PluginPolicyGateInterfaceCompileTest |
| 阻断 manifest/policy object ownership 越权 | PluginPolicyGateBoundaryContractTest |

## 5. Build 三件套

1. 代码目标：新增 infra/include/plugin/IPluginPolicyGate.h，并在 infra/CMakeLists.txt 中注册 public header。
2. 测试目标：
   - tests/unit/infra/plugin/PluginPolicyGateInterfaceTest.cpp：冻结 evaluate 签名、PluginPolicyRequest 结构和决策输出边界。
   - tests/contract/smoke/PluginPolicyGateBoundaryContractTest.cpp：验证 request 只持有 descriptor + ref，不拥有 blocked manifest 或 policy bundle 对象。
3. 验收命令：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci --target dasall_infra dasall_plugin_policy_gate_interface_unit_test dasall_contract_plugin_policy_gate_boundary_test
   - ctest --test-dir build-ci -N -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest"
   - ctest --test-dir build-ci --output-on-failure -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest"

## 6. 风险与回退

1. PluginPolicyRequest 当前只承接 descriptor + manifest_ref + profile_id；待 PluginManifest 解阻后，若需要直接传入 manifest 对象，应通过增量 overload 或桥接对象评审承接，而不是替换现有 request 结构。
2. evaluate 当前只消费现有 PolicySnapshot，不单独冻结 allowlist/trust_policy 对象；若后续策略引擎需要显式 query object，应优先复用 PolicyQueryContext/PolicyDecisionRef 现有边界。