# PLG-TODO-006 plugin 审计适配器收敛

日期：2026-04-07
任务：PLG-TODO-006
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 将 PLG-TODO-006 定义为“新增 PluginAuditAdapter 适配器”，完成判定是 plugin load/unload/policy deny 三类高风险动作均有审计记录，且 evidence_ref 可导出追踪。
2. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.8/6.10 要求 plugin 在 validate/load/unload 等高风险路径上输出审计证据，最小字段必须覆盖 actor、action、target、outcome、evidence_ref、reason_code，并满足失败不可吞没。
3. 当前仓库已完成 INF-TODO-016 与 PLG-TODO-005，具备 audit::IAuditLogger、AuditService、现有 audit bridge 模式以及 plugin 私有实现的真实构建接线；本轮不需要回头扩写公共接口，只需补足 plugin 私有审计适配层与测试出口。

## 2. 研究学习结果

### 2.1 本地证据

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 已冻结 plugin 高风险动作的最小审计字段集，但 infra/src/plugin 下此前不存在独立的 PluginAuditAdapter，说明 006 的缺口是审计投影和 emit 入口未落盘，而不是字段语义未定义。
2. infra/include/audit/AuditService.h 已提供 init/start/write_audit/export_audit 边界，且可通过 audit::IAuditLogger 抽象消费，这使 006 可以保持 infra/plugin 对具体存储实现解耦。
3. infra/src/policy/PolicyAuditBridge.cpp 与 infra/src/ota/OTAAuditBridge.cpp 已证明，稳定的 audit 适配层应承担 action 命名、side_effects 序列化、AuditContext 投影与 write outcome 错误回传，而不是把这些细节散落到业务调用点。
4. tests/integration/infra/CMakeLists.txt 此前没有 plugin 子目录注册点，因此即使落了 adapter 代码，也无法证明事件真正经过 AuditService 写入与导出；这构成了 006 的测试出口缺口。

### 2.2 外部参考

1. OWASP Logging Cheat Sheet 指出，高风险功能使用与策略违例应始终进入 application logging；这直接支撑本轮把 plugin load/unload/policy deny 固化为必须可观测的审计动作，而不是作为可选 debug 日志。
2. 同一参考还要求每条事件至少可回答 when、where、who、what，以及 action、object、result、reason；这与本轮冻结 actor、action、target、outcome、evidence_ref、reason_code，并在 policy deny 时追加 result_code 的字段选择一致。

### 2.3 可落地启发

1. PluginAuditAdapter 的稳定边界应是 PluginAuditRecord -> AuditEvent/AuditContext -> AuditWriteOutcome，而不是直接暴露 AuditService 细节给 plugin 上游调用方。
2. plugin.load、plugin.unload、plugin.policy_deny 与 plugin:<plugin_id> 目标命名空间应作为冻结的导出过滤键，便于后续 011/012 在生命周期与失败注入场景中重用。
3. 缺失 audit sink、无效 record、write outcome 失败都必须显式回传 contracts::ResultCode 与 ErrorInfo，避免高风险动作的审计失败被静默吞掉。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 PluginAuditAdapter 的最小输入/输出边界 | plugin 详细设计 6.10；AuditService / IAuditLogger 边界 | infra/src/plugin/PluginAuditAdapter.h/.cpp | adapter 只承担事件投影、emit 状态与错误回传，不越权进入 lifecycle 编排 |
| D2 | 冻结 plugin 高风险动作的审计命名与 outcome 规则 | plugin 详细设计 6.8/6.10 | `plugin.load`、`plugin.unload`、`plugin.policy_deny` 映射与 side_effects 规则 | load/unload/policy deny 的 action、target、outcome、reason_code 稳定可判定 |
| D3 | 建立 006 的 unit + integration 验证出口 | tests/integration/infra 现状；AuditService 导出边界 | tests/unit/infra/plugin/PluginAuditAdapterTest.cpp；tests/integration/infra/plugin/PluginAuditTraceIntegrationTest.cpp | 既能验证字段/失败路径，也能验证写入与导出追踪 |
| D4 | 锁定 006 的 Build 三件套 | plugin 专项 TODO 006 | 本交付物 + TODO 回写 | 有代码目标、测试目标、验收命令 |

## 4. D Gate 结论

### 4.1 Blocker 修复与 Design -> Build 映射

阻塞结论：

1. 006 原任务行只给出宽泛的 `ctest -L "unit|integration" -R "PluginAudit"`，既没有 configure 步骤，也没有显式构建新增测试目标，不能证明新的 adapter unit/integration 出口可执行。
2. tests/integration/infra/CMakeLists.txt 缺少 plugin 子目录注册，因此 plugin 审计事件即使写入成功，也没有组件级 integration 入口证明导出链路成立。
3. 若 PluginAuditAdapter 直接耦合 AuditService 具体类型，会把 plugin 私有实现绑定到审计存储实现，破坏 infra 内部抽象边界，也会让后续 011 的状态机接线只能依赖 concrete service。

最小 blocker-fix：

1. 让 PluginAuditAdapter 只依赖 `std::shared_ptr<audit::IAuditLogger>`，把 AuditService 限定在 integration 测试中作为 concrete sink 使用。
2. 在 tests/integration/infra 下新增 plugin 子目录与 PluginAuditTraceIntegrationTest，验证事件可经过 AuditService 写入并按 action 过滤导出。
3. 将 006 的验收命令升级为显式 configure、显式构建 `dasall_plugin_audit_adapter_unit_test` 与 `dasall_plugin_audit_trace_integration_test`，再定向执行对应 ctest 子集。

Design -> Build 映射：

| Design 结论 | Build 落地 |
|---|---|
| 审计适配器只负责事件投影与状态回传 | infra/src/plugin/PluginAuditAdapter.h/.cpp |
| 高风险动作的 action/outcome/side_effects 必须冻结 | PluginAuditAdapter.cpp |
| 字段缺失与缺少 logger 必须显式失败 | tests/unit/infra/plugin/PluginAuditAdapterTest.cpp |
| 写入与导出追踪必须有 integration 证据 | tests/integration/infra/plugin/PluginAuditTraceIntegrationTest.cpp |
| plugin integration 入口必须组件级注册 | tests/integration/infra/CMakeLists.txt、tests/integration/infra/plugin/CMakeLists.txt |

### 4.2 Build 三件套

1. 代码目标：infra/src/plugin/PluginAuditAdapter.h、infra/src/plugin/PluginAuditAdapter.cpp、infra/CMakeLists.txt、tests/integration/infra/CMakeLists.txt、tests/integration/infra/plugin/CMakeLists.txt。
2. 测试目标：
   - tests/unit/infra/plugin/PluginAuditAdapterTest.cpp
   - tests/integration/infra/plugin/PluginAuditTraceIntegrationTest.cpp
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_infra dasall_plugin_audit_adapter_unit_test dasall_plugin_audit_trace_integration_test
   - ctest --test-dir build-ci --output-on-failure -R "PluginAuditAdapterTest|PluginAuditTraceIntegrationTest"

### 4.3 D Gate

结论：PASS。

理由：

1. 006 的改动严格停留在 plugin 私有审计适配层、unit/integration 注册与验证，不提前进入 lifecycle 状态机、runtime 主控或 platform 动态加载细节。
2. 高风险动作的命名、失败回传与导出追踪都已有明确的二值验证出口，且不需要扩写任何 plugin 公共接口或 contracts 共享语义。

## 5. Build 落地结果

1. 新增 infra/src/plugin/PluginAuditAdapter.h 与 PluginAuditAdapter.cpp，提供 PluginAuditRecord、PluginAuditEmitResult、PluginAuditAdapterStatus 以及 load/unload/policy deny 三类高风险动作的稳定 emit 入口。
2. 更新 infra/CMakeLists.txt，把 PluginAuditAdapter.cpp 与 PluginAuditAdapter.h 纳入 plugin 私有源/头清单，确保 006 的新适配器真实进入 dasall_infra 构建图。
3. 更新 tests/unit/infra/plugin/CMakeLists.txt，注册 `dasall_plugin_audit_adapter_unit_test` 目标。
4. 新增 tests/unit/infra/plugin/PluginAuditAdapterTest.cpp，覆盖 load/unload/policy deny 成功路径、invalid record 拒绝路径、缺失 audit logger 失败路径。
5. 更新 tests/integration/infra/CMakeLists.txt，新增 `add_subdirectory(plugin)`；同时新增 tests/integration/infra/plugin/CMakeLists.txt，注册 `dasall_plugin_audit_trace_integration_test`。
6. 新增 tests/integration/infra/plugin/PluginAuditTraceIntegrationTest.cpp，使用 AuditService 验证 plugin 审计事件可写入、可导出、可按 `plugin.policy_deny` 动作过滤。

## 6. Build 合规复核

1. 边界：本轮 adapter 只依赖 audit::IAuditLogger 抽象；AuditService 仅在 integration 测试中作为 concrete sink 出现，没有把 plugin 私有实现反向绑死到审计存储实现。
2. 根因处理：修复的是 plugin 审计适配层缺失与 integration 注册点缺失的根因，而不是在未来调用点里散落拼装 AuditEvent 的临时逻辑。
3. 测试出口：unit 验证动作命名、字段投影、失败关闭；integration 验证 AuditService 写入与导出过滤，可直接证明 evidence_ref 与 reason_code 能形成可追踪链路。
4. 兼容性：IPluginManager、IPluginPolicyGate 与 contracts 公共对象均未修改；011 只需在 lifecycle skeleton 中调用现有 adapter 接口即可复用冻结的审计语义。

## 7. 验证结果

1. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
2. cmake --build build-ci --target dasall_infra dasall_plugin_audit_adapter_unit_test dasall_plugin_audit_trace_integration_test：通过。
3. ctest --test-dir build-ci --output-on-failure -R "PluginAuditAdapterTest|PluginAuditTraceIntegrationTest"：通过，2/2 tests passed。

## 8. 结论

1. PLG-TODO-006 已完成，plugin load/unload/policy deny 三类高风险动作现在具备稳定的审计事件投影与导出追踪证据。
2. 本轮把 plugin 审计字段、action 命名与失败回传冻结在私有适配层中，为后续 011 的 lifecycle 状态机接线和 012 的失败注入测试提供了可复用的可观测性基线。