# PLG-TODO-012 plugin 失败注入与可观测性测试收敛

日期：2026-04-07
任务：PLG-TODO-012
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 将 PLG-TODO-012 定义为“编写 plugin 失败注入与可观测性测试”，完成判定是关键失败路径至少覆盖签名失败、兼容失败、load 超时三类，并为每条路径提供稳定的可观测证据。
2. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.8/6.10/9.1/9.2 要求准入失败直接拒绝激活、输出 report 或审计证据，并把 Failure Injection 作为质量 Gate 的独立维度。
3. 当前仓库已完成 PLG-TODO-005、PLG-TODO-006、PLG-TODO-011，具备 PluginValidationPipeline 的 stage callback 注入点、PluginLifecycleManager 的 runtime callback 注入点，以及 PluginAuditAdapter/AuditService 的审计写入与导出链路。

## 2. 研究学习结果

### 2.1 本地证据

1. infra/src/plugin/PluginValidationPipeline.h/.cpp 已提供 signature/compatibility stage callback 注入点，说明 012 不缺 failure injection 入口，真正缺口是 validation failure 尚未把审计适配器接到拒绝路径上。
2. infra/src/plugin/PluginAuditAdapter.h/.cpp 在本轮前只冻结了 plugin.load、plugin.unload、plugin.policy_deny 三类动作，无法为 signature fail / compatibility fail 提供统一 action 命名与导出过滤键。
3. infra/src/plugin/PluginLifecycleManager.h/.cpp 已具备 runtime load callback 注入点与 load failure audit 接线，因此 load timeout 的最小 failure injection 可以在不引入真实 PluginRuntimeBridge 的前提下稳定复现。
4. tests/integration/infra/plugin/CMakeLists.txt 在本轮前只注册了 PluginAuditTraceIntegrationTest，说明 012 的 discoverability 缺口不在 tests 顶层，而在 plugin 子目录下还没有 failure-observability 入口。

### 2.2 外部参考

1. OWASP Logging Cheat Sheet 明确建议把 input validation failures、authorization failures 和高风险功能使用纳入应用级 logging/audit，同时要求事件至少回答 when/where/who/what，以及 action、object、result、reason。
2. 同一参考还要求在应用测试中显式验证 logging failure / external sink failure 不应阻断主业务链，这与本轮保持 PluginValidationPipeline/PluginLifecycleManager 主结果边界稳定、只追加可选审计出口的做法一致。

### 2.3 可落地启发

1. 012 的根因修复不应该再造新的 validation public interface，而应把 signature fail / compatibility fail 审计接线限制在 PluginValidationPipeline 私有实现中。
2. load timeout 的可观测性验证可以直接复用 011 已冻结的 runtime callback 注入点和 006 的 PluginAuditAdapter，而不必等待真实平台装载器。
3. 由于本轮修改了 plugin integration 注册入口，必须同时回填 discoverability 证据，而不仅是跑通单条 ctest 命令。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 012 的 failure injection 注入面 | PluginValidationPipeline / PluginLifecycleManager 现状 | 复用 stage callbacks + runtime callbacks | 不新增任何 public plugin interface |
| D2 | 补足 validation failure 的统一审计动作 | plugin 详细设计 6.8/6.10 | PluginAuditAdapter 新增 signature_fail / compatibility_fail 动作 | validate rejection 可按 action 导出审计事件 |
| D3 | 建立 012 的 integration observability 出口 | tests/integration/infra/plugin 现状 | PluginFailureObservabilityIntegrationTest | signature fail、compatibility fail、load timeout 三条路径均可二值判定 |
| D4 | 锁定 012 的 Build 三件套 | plugin 专项 TODO 012 | 本交付物 + TODO 回写 | 有代码目标、测试目标、验收命令与 discoverability 命令 |

## 4. D Gate 结论

### 4.1 Blocker 修复与 Design -> Build 映射

阻塞结论：

1. plugin 详细设计要求 signature fail 与 compatibility fail 提供可观测证据，但当前代码只有 load/unload/policy deny 审计动作，validation failure 的审计出口实际缺失。
2. tests/integration/infra/plugin/ 当前只有 PluginAuditTraceIntegrationTest，意味着 012 即使补了失败场景，也没有对应的 integration 发现性入口。
3. 本轮需要同时验证新增 integration 测试被 CTest 图发现，否则 Build 合规复核不完整。

最小 blocker-fix：

1. 在 PluginAuditAdapter 中新增 `plugin.signature_fail` 与 `plugin.compatibility_fail` 两个私有审计动作，并保持结果仍映射到既有 contracts::ResultCode/ErrorInfo 类型。
2. 在 PluginValidationPipeline 中增加可选 PluginAuditAdapter 注入，仅对 policy deny / signature fail / compatibility fail 三类 validation rejection 发射审计事件，不修改任何 public plugin request/result 对象。
3. 在 tests/integration/infra/plugin/ 下新增 PluginFailureObservabilityIntegrationTest，并补齐 plugin 子目录 registration，使 012 具备稳定 discoverability。

Design -> Build 映射：

| Design 结论 | Build 落地 |
|---|---|
| validation failure 只在私有骨架内补审计出口 | infra/src/plugin/PluginValidationPipeline.h/.cpp |
| signature/compatibility failure 需要稳定 action 名称 | infra/src/plugin/PluginAuditAdapter.h/.cpp |
| 新动作必须保留 unit 守卫 | tests/unit/infra/plugin/PluginAuditAdapterTest.cpp |
| 012 需要真实 integration failure-observability 证据 | tests/integration/infra/plugin/PluginFailureObservabilityIntegrationTest.cpp |
| plugin integration discoverability 需要组件级注册 | tests/integration/infra/plugin/CMakeLists.txt |

### 4.2 Build 三件套

1. 代码目标：infra/src/plugin/PluginAuditAdapter.h、infra/src/plugin/PluginAuditAdapter.cpp、infra/src/plugin/PluginValidationPipeline.h、infra/src/plugin/PluginValidationPipeline.cpp、tests/integration/infra/plugin/CMakeLists.txt。
2. 测试目标：
   - tests/unit/infra/plugin/PluginAuditAdapterTest.cpp
   - tests/integration/infra/plugin/PluginFailureObservabilityIntegrationTest.cpp
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_infra dasall_plugin_audit_adapter_unit_test dasall_plugin_failure_observability_integration_test
   - ctest --test-dir build-ci -N -L integration | grep -E "Plugin(AuditTraceIntegrationTest|FailureObservabilityIntegrationTest)"
   - ctest --test-dir build-ci --output-on-failure -R "PluginAuditAdapterTest|PluginFailureObservabilityIntegrationTest"

### 4.3 D Gate

结论：PASS。

理由：

1. 012 的改动严格收敛在 plugin 私有实现和 tests，不新增 public plugin interface，也不反向扩写 contracts 共享语义。
2. 关键失败路径与 discoverability 都具备明确的二值出口，允许继续进入 Build 并独立提交。

## 5. Build 落地结果

1. 更新 infra/src/plugin/PluginAuditAdapter.h/.cpp，新增 `plugin.signature_fail`、`plugin.compatibility_fail` 两个私有审计动作及对应 emit API，并保持 rejected outcome 与 result_code side_effect 语义稳定。
2. 更新 infra/src/plugin/PluginValidationPipeline.h/.cpp，引入可选 PluginAuditAdapter 注入，把 policy deny、signature fail、compatibility fail 三类 validation rejection 接入统一审计出口。
3. 更新 tests/unit/infra/plugin/PluginAuditAdapterTest.cpp，把 validation failure 动作纳入既有 unit 守卫，验证 action、outcome、side_effects 与 context 投影。
4. 更新 tests/integration/infra/plugin/CMakeLists.txt，注册 `PluginFailureObservabilityIntegrationTest`。
5. 新增 tests/integration/infra/plugin/PluginFailureObservabilityIntegrationTest.cpp，分别验证 signature fail、compatibility fail、load timeout 三条 failure injection 路径的 report/audit 证据链。

## 6. Build 合规复核

1. 边界：本轮没有修改 IPluginManager、PluginValidationRequest/Result、PluginLoadResult/UnloadResult 等 public 边界；新的审计动作只存在于 plugin 私有适配层。
2. 根因处理：修复的是 validation failure 缺少统一审计出口和 integration 发现性缺失的根因，而不是在测试里绕过真实骨架直接断言字符串。
3. 正负例覆盖：unit 覆盖新增 validation-failure audit 动作的正例；integration 覆盖 signature fail、compatibility fail、load timeout 三类失败路径，均断言 report_ref/evidence_ref 或 AuditEvent 导出事实。
4. 测试发现性：新增 integration 用例已通过 `ctest -N -L integration` 被 CTest 图发现，不是仅靠显式 target 执行才能跑通。
5. 兼容性：所有新增失败动作仍映射到既有 contracts::ResultCode/ErrorInfo 范围，未引入新的公共错误域。

## 7. 验证结果

1. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
2. cmake --build build-ci --target dasall_infra dasall_plugin_audit_adapter_unit_test dasall_plugin_failure_observability_integration_test：通过。
3. ctest --test-dir build-ci -N -L integration | grep -E "Plugin(AuditTraceIntegrationTest|FailureObservabilityIntegrationTest)"：通过，plugin integration 子集可发现 2 个用例。
4. ctest --test-dir build-ci --output-on-failure -R "PluginAuditAdapterTest|PluginFailureObservabilityIntegrationTest"：通过，2/2 tests passed。

## 8. 结论

1. PLG-TODO-012 已完成，plugin 现在对 signature fail、compatibility fail、load timeout 三条关键失败路径都具备稳定的 report 或 audit 证据链。
2. 本轮把 validation failure observability 补齐到 plugin 私有骨架中，并保持 load timeout 继续复用 011 的 lifecycle failure audit，为后续 013 的 profile 行为矩阵验证提供可持续回归的 failure baseline。