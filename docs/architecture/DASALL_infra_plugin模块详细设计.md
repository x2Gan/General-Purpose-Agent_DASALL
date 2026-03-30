# DASALL infra/plugin 组件详细设计

版本：v1.0  
日期：2026-03-25  
阶段：Detailed Design  
适用模块：infra/plugin（infra/src/plugin、infra/include/plugin）

## 1. 模块概览

### 1.1 模块定位

plugin 组件属于 Layer 1 Infrastructure Layer，在 DASALL 中承担“插件治理平面”职责，不承担业务调度与认知决策。

来源依据：
1. docs/architecture/DASSALL_Agent_architecture.md（3.4.7、5.10.1、5.10.2、8.8、8.9）
2. docs/architecture/DASALL_Engineering_Blueprint.md（3.12、4.2、4.3、5.1、5.2）
3. docs/architecture/DASALL_infrastructure子系统详细设计.md（6.2、6.3、6.4、6.5、6.6、6.8、6.9、6.10、7、8）
4. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md（INF-TODO-019、INF-BLK-09）

### 1.2 目标

1. 提供插件发现、校验、装载、启停、卸载与兼容管理能力。
2. 将签名校验、来源信任、ABI 兼容与 Profile 裁剪纳入统一治理链路。
3. 对外仅暴露稳定接口与治理结果，不泄露 loader 细节和动态链接实现细节。
4. 对失败路径提供可观测证据（日志/指标/追踪/审计），满足失败不可吞没。

### 1.3 上下游边界

上游消费者：runtime、apps、tools、services（通过 infra facade 间接调用）。  
同层依赖：SecurityPolicyManager、DiagnosticsService、AuditService、ConfigCenter、HealthMonitor。  
下游依赖：platform 动态库抽象、文件系统、签名验证库（如 OpenSSL）。

---

## 2. 约束清单

### 2.1 Must / Should / Must-Not 约束表

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| PLG-C001 | DASSALL_Agent_architecture.md 5.10.2 | Must | infra 必须包含 OTA/Plugin Manager 关键组件 | 子组件拆分 |
| PLG-C002 | DASSALL_Agent_architecture.md 3.7 + Blueprint 4.1 | Must | plugin 只能依赖下层抽象与 contracts，不反向依赖业务模块实现 | 依赖方向 |
| PLG-C003 | DASALL_Engineering_Blueprint.md 4.2 | Must-Not | infra 不得依赖 runtime/cognition/llm/tools/memory/knowledge/services/multi_agent 实现 | 头文件与链接依赖 |
| PLG-C004 | DASALL_Engineering_Blueprint.md 4.3 | Must | 跨模块只通过 contracts 稳定语义对象 | 接口语义 |
| PLG-C005 | ADR-005 | Must | 不可在 plugin 设计中反向修改主架构或 contracts 冻结结论 | 设计治理 |
| PLG-C006 | ADR-006 | Must-Not | plugin 仅记录上下文标识，不组装 ContextPacket、不渲染 Prompt | 职责边界 |
| PLG-C007 | ADR-007 | Must-Not | plugin 不做失败语义判定，仅返回执行结果与证据 | 异常处理 |
| PLG-C008 | ADR-008 | Must | plugin 不拥有调度权，只接受主控触发并返回治理结果 | 主控边界 |
| PLG-C009 | DASALL_infrastructure子系统详细设计.md 6.5 | Must | PluginDescriptor 仅保留 plugin_id/version/abi/trust_level/status 等治理字段 | 核心对象 |
| PLG-C010 | DASALL_infrastructure子系统详细设计.md 6.6 | Must | IPluginManager 需包含 discover/validate/load/unload 语义 | 接口定义 |
| PLG-C011 | DASALL_infrastructure子系统详细设计.md 6.8 + 6.10 | Must | 插件校验失败必须拒绝激活并写审计/兼容报告 | 失败兜底与观测 |
| PLG-C012 | DASALL_Engineering_Blueprint.md 5.1 + 5.2 | Must | Profile 仅裁剪能力，不绕过审计与主控链路 | 配置策略 |
| PLG-C013 | DASALL_工程协作与编码规范.md 3.6 | Must | 错误不可吞没，失败必须可观测 | 错误语义 |
| PLG-C014 | DASALL_工程协作与编码规范.md 3.7 | Should | 新增公共接口同步新增 unit/contract/integration 测试 | 测试门禁 |
| PLG-C015 | DASALL_infrastructure子系统专项TODO.md INF-BLK-09 | Must | plugin manifest、ABI 兼容矩阵、签名链路冻结前，不进入完整装载实现 | 实施节奏 |

### 2.2 约束抽取结论

Must：组件存在性、依赖单向、签名与兼容检查、失败可观测、Profile 受控裁剪。  
Should：公共接口配套 unit/contract/integration 测试。  
Must-Not：越权调度、越权语义扩写、越界依赖业务实现。

---

## 3. 现状与缺口

### 3.1 现状识别

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| plugin 目录与实现骨架 | 缺失 | infra/src 尚无 plugin 子目录 | High | P0 |
| IPluginManager 接口 | 缺失 | infra/include 下无 plugin 接口头文件 | High | P0 |
| PluginDescriptor 对象 | 缺失 | 仅在子系统文档有字段，代码未落盘 | High | P0 |
| manifest/ABI/签名规范 | 缺失且被阻塞 | 专项 TODO 明确 INF-BLK-09 未冻结 | High | P0 |
| plugin 测试基线 | 缺失 | 无 PluginManagerTest/PluginManifestValidationTest | High | P0 |
| Plugin 与 Policy/Diagnostics 集成边界 | 设计有结论，代码无实现 | 缺乏接口级联动与证据导出模型 | Medium | P1 |
| Profile 裁剪策略映射 | 部分定义 | 有配置键，无插件级行为矩阵 | Medium | P1 |

证据：
1. docs/architecture/DASALL_infrastructure子系统详细设计.md（PluginManager 条目与阻塞）
2. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md（INF-TODO-019 Blocked，INF-BLK-09）
3. infra/src 当前目录无 plugin 子目录

### 3.2 现状-目标冲突

| 冲突类型 | 描述 | 影响 | 风险等级 |
|---|---|---|---|
| 边界冲突 | 可能把插件调度或业务路由放进 plugin manager | 破坏 ADR-008 主控边界 | High |
| 语义重复 | 可能把 Tool/Skill 语义复制到 PluginDescriptor | 破坏 contracts 冻结策略 | High |
| 依赖反转 | plugin 为便捷直接 include tools/runtime 实现头文件 | 违反蓝图 4.2 | High |
| 安全下沉不足 | 签名链路未冻结即实现 load | 产生供应链与回滚风险 | High |

---

## 4. 候选方案对比

### 4.1 候选方案

1. 方案 A：最小白名单插件（静态发现 + 文件签名 + ABI 主版本匹配）。
2. 方案 B：分层插件治理（Manifest Registry + Policy Gate + Sandbox Adapter + Lifecycle Manager）。
3. 方案 C：远程插件中心（在线仓库、远程签名校验、按需拉取与热更新）。

### 4.2 对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| A 最小白名单 | 中 | 高 | 低 | 治理能力不足、扩展性偏弱 | 作为 M1 过渡保留 |
| B 分层插件治理 | 高 | 高 | 中 | 需要先冻结 manifest/ABI/签名模型 | 采纳 |
| C 远程插件中心 | 中 | 中 | 高 | 引入网络供应链与运维复杂度，超出现阶段 | 淘汰（v2 预留） |

### 4.3 行业匹配结论

1. 基于签名链与兼容矩阵的本地治理模式，最匹配当前 infra 和 edge profile 条件。
2. 远程中心模式需补齐仓库信任、离线策略、分发审计，不适合当前冻结阶段。

---

## 5. 决策结论

### 5.1 最终选型

采纳方案 B：分层插件治理（Manifest Registry + Policy Gate + Lifecycle Manager + Compatibility Engine）。

### 5.2 选择依据

1. 与架构一致：plugin 属于 infra 治理能力，不承担业务决策。
2. 与 ADR 一致：不接管主控，不承担失败判定，不侵入上下文与提示词域。
3. 与 contracts 一致：只复用已有标识与错误语义，不扩写 Tool/Skill 契约。
4. 与专项 TODO 一致：先冻结对象与接口，再进入装载实现，符合 INF-TODO-019/INF-BLK-09。

### 5.3 放弃其他方案理由

1. 方案 A 作为落地过渡可行，但长期缺少治理分层和可扩展性，不宜作为终态。
2. 方案 C 依赖外部供应链与线上治理，现阶段会放大安全和运维不确定性。

---

## 6. 详细设计

### 6.1 职责边界

plugin 组件职责：
1. 发现插件包与清单。
2. 校验签名、来源、ABI 兼容和 Profile allowlist。
3. 执行装载/启停/卸载生命周期。
4. 输出治理结果（Catalog、LoadResult、CompatibilityReport）并完成审计。

plugin 组件非职责：
1. 不决定是否进入多 Agent 或业务流程分支。
2. 不修改 contracts 共享对象结构。
3. 不自行执行恢复判定，仅报告失败并交由 runtime 恢复链路处理。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| PluginRegistry | 扫描插件目录，解析 Manifest，生成 PluginCatalog |
| PluginPolicyGate | 基于 SecurityPolicySnapshot + Profile 策略执行准入判定 |
| PluginSignatureVerifier | 校验插件包签名、证书链、来源信任级别 |
| PluginCompatibilityEngine | 校验 ABI/API 版本、依赖约束、平台标签 |
| PluginLifecycleManager | 执行 load/enable/disable/unload 生命周期与资源回收 |
| PluginRuntimeBridge | 与 platform 动态装载抽象交互，隔离 dlopen/dlsym 等细节 |
| PluginAuditAdapter | 生成并写入 plugin 相关审计事件 |
| PluginDiagnosticsAdapter | 输出故障证据与兼容报告引用 |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| PluginRegistry | 插件目录、Manifest 文件、Profile 配置 | PluginCatalog | catalog 仅表达发现结果，不含激活结论 |
| PluginPolicyGate | PolicySnapshot、allowlist、trust policy | PolicyDecisionRef | 决策可追溯，可审计 |
| PluginSignatureVerifier | PluginPackage、证书链、信任锚 | SignatureReport | 未通过则禁止 load |
| PluginCompatibilityEngine | PluginDescriptor、Host ABI、依赖矩阵 | CompatibilityReport | 需明确 fail reason code |
| PluginLifecycleManager | load/unload 请求、前置校验结果 | LoadResult/UnloadResult | 生命周期状态可重放 |
| PluginRuntimeBridge | binary path、entry symbol、sandbox hint | Handle / ErrorInfo | 屏蔽平台差异 |
| PluginAuditAdapter | 风险动作上下文 | AuditEvent 引用 | plugin load/unload/policy deny 强制审计 |
| PluginDiagnosticsAdapter | 失败上下文、报告对象 | evidence_ref | 供 DiagnosticsService 导出 |

### 6.4 子组件依赖关系

1. IPluginManager -> PluginRegistry、PluginPolicyGate、PluginSignatureVerifier、PluginCompatibilityEngine、PluginLifecycleManager。
2. PluginPolicyGate 依赖 ISecurityPolicyManager.snapshot()。
3. PluginLifecycleManager 依赖 PluginRuntimeBridge，不直接触达 OS 动态加载 API。
4. PluginLifecycleManager 通过 PluginAuditAdapter 产出审计事件。
5. PluginCompatibilityEngine 与 PluginDiagnosticsAdapter 协作输出 CompatibilityReport 与 evidence_ref。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | contracts 对齐关系 |
|---|---|---|---|
| PluginDescriptor | plugin_id, version, abi, trust_level, status, source | 必填字段不可为空；status 仅在治理状态机内推进 | 仅复用 request_id/task_id/trace_id 等标识语义 |
| PluginManifest | schema_version, plugin_id, version, entry, required_abi, capabilities, signature_ref | schema_version 必须版本化；capabilities 受 allowlist 约束 | 不扩写 ToolDescriptor/SkillSpec 语义 |
| PluginCatalog | discovered_plugins[], rejected_plugins[] | 发现与拒绝原因必须可解释 | 与 Observation evidence_ref 引用对齐 |
| SignatureReport | verified, signer, chain_status, reason | 验签失败禁止进入 load | 映射 ErrorInfo，不新增 contracts 错误对象 |
| CompatibilityReport | abi_ok, api_ok, dependency_ok, reasons[] | 任一检查失败则 reject | 以 ResultCode + ErrorInfo 对外暴露 |
| LoadResult | plugin_id, phase, result, handle_ref, evidence_ref | 失败必须含 reason/evidence_ref | 与 AgentResult 分层，不混用 |

### 6.6 核心接口语义定义

建议头文件：infra/include/plugin/

1. IPluginManager
- discover(profile): 返回 PluginCatalog。
- validate(manifest, package): 返回 ValidationResult（聚合 SignatureReport + CompatibilityReport + PolicyDecision）。
- load(plugin_id, load_options): 返回 LoadResult。
- unload(plugin_id): 返回 UnloadResult。
- list_active(): 返回 ActivePluginSet。

2. IPluginPolicyGate
- evaluate(manifest, policy_snapshot, profile): 返回 PolicyDecisionRef。

3. IPluginSignatureVerifier
- verify(package, trust_store): 返回 SignatureReport。

4. IPluginCompatibilityEngine
- check(manifest, host_abi, deps): 返回 CompatibilityReport。

错误语义（映射 infra 错误码域）：
1. INF_E_PLUGIN_VALIDATE_FAIL
2. INF_E_PLUGIN_LOAD_FAIL
3. INF_E_POLICY_INVALID（策略快照不可用）
4. INF_E_DIAG_EXPORT_FAIL（证据导出失败）

前置条件：discover/validate/load 均要求 infra 已完成 init/start。  
后置条件：load/unload 必须产生日志、指标、审计三类观测记录。

### 6.7 主流程时序（正常）

1. InfraServiceFacade.start() 触发 plugin 子组件初始化。
2. PluginRegistry 扫描目录并解析 Manifest，生成 PluginCatalog。
3. PluginPolicyGate 基于 Profile + PolicySnapshot 过滤候选插件。
4. PluginSignatureVerifier 校验包签名与来源信任。
5. PluginCompatibilityEngine 校验 ABI/API/依赖兼容。
6. PluginLifecycleManager 执行 load 并更新 active set。
7. PluginAuditAdapter 写入插件装载审计事件。
8. 对外返回 LoadResult（含 evidence_ref）。

### 6.8 异常与恢复时序

异常分类：
1. 准入类失败：白名单不匹配、策略拒绝、签名失败、兼容失败。
2. 装载类失败：入口符号缺失、初始化超时、资源占用冲突。
3. 运行类失败：心跳失败、实例崩溃、卸载失败。

恢复动作：
1. 准入失败：直接拒绝激活，输出 CompatibilityReport + 审计。
2. 装载失败：回滚到未激活状态，释放 handle，记录 LoadResult(result=failed)。
3. 运行失败：标记 degraded，触发 HealthMonitor 事件并建议 runtime 执行恢复策略。
4. 连续 N 次失败：触发 plugin_safe_mode（禁用动态加载，仅保留静态白名单插件）。

失败兜底：
1. 绝不静默跳过失败插件。
2. 无法导出证据时至少保留本地审计与错误码。

### 6.9 配置项与默认策略

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| infra.plugin.enabled | true | Profile/部署 | 是否启用插件治理 |
| infra.plugin.allowlist | [] | Profile/部署 | 允许激活插件集合 |
| infra.plugin.search_paths | ["./plugins"] | Profile/部署 | 插件扫描路径 |
| infra.plugin.load_timeout_ms | 3000 | Profile/部署 | 单插件装载超时 |
| infra.plugin.max_active | 16 | Profile/部署 | 最大激活插件数 |
| infra.plugin.signature.required | true | Profile/部署 | 是否强制签名校验 |
| infra.plugin.trust.min_level | internal | Profile/部署 | 最低信任等级 |
| infra.plugin.abi.strict_mode | true | Profile/部署 | 严格 ABI 兼容检查 |
| infra.plugin.remote_fetch.enabled | false | Profile/部署 | 是否允许远程拉取（默认禁用） |
| infra.plugin.safe_mode.fail_threshold | 3 | Profile/部署 | 连续失败阈值 |

### 6.10 可观测性设计

日志点：
1. 插件扫描开始/结束与发现数量。
2. 策略拒绝、签名失败、兼容失败原因。
3. load/unload 生命周期关键阶段。
4. safe_mode 触发与恢复。

指标：
1. infra_plugin_discover_total。
2. infra_plugin_validate_total。
3. infra_plugin_validate_fail_total。
4. infra_plugin_load_total。
5. infra_plugin_load_fail_total。
6. infra_plugin_active_count。
7. infra_plugin_safe_mode_total。

追踪：
1. discover/validate/load/unload 各流程建立 span。
2. 关键日志附 trace_id/span_id/plugin_id。

审计：
1. plugin load/unload、policy deny、signature fail 强制审计。
2. 审计字段最小集合：actor、action、target、outcome、evidence_ref、reason_code。

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 冻结插件接口边界 | 新增 IPluginManager 与子接口骨架 | 先完成 L2 冻结，避免实现漂移 | infra/include/plugin/IPluginManager.h, IPluginPolicyGate.h, IPluginSignatureVerifier.h, IPluginCompatibilityEngine.h | unit: PluginInterfaceCompileTest; contract: PluginContractBoundaryTest | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "PluginInterfaceCompileTest|PluginContractBoundaryTest" | 阻塞：INF-BLK-09 |
| 冻结核心对象模型 | 新增 PluginDescriptor/Manifest/Catalog/Report 对象 | 先让输入输出可测与可审查 | infra/include/plugin/PluginDescriptor.h; PluginManifest.h; PluginCatalog.h; PluginReports.h | unit: PluginManifestSchemaTest; contract: PluginObjectBoundaryTest | ctest --test-dir build-ci -R "PluginManifestSchemaTest|PluginObjectBoundaryTest" | 阻塞：manifest 字段冻结 |
| 建立校验管线 | 新增 validate 聚合流程骨架 | 把 policy/signature/compat 三检收敛到同一出口 | infra/src/plugin/PluginValidationPipeline.cpp | unit: PluginValidationPipelineTest | ctest --test-dir build-ci -R PluginValidationPipelineTest | 依赖 SecurityPolicy snapshot |
| 建立生命周期骨架 | 新增 PluginLifecycleManager skeleton | 提供 load/unload 状态机最小闭环 | infra/src/plugin/PluginLifecycleManager.cpp | unit: PluginLifecycleStateTest; integration: InfraPluginLifecycleTest | ctest --test-dir build-ci -R "PluginLifecycleStateTest|InfraPluginLifecycleTest" | 阻塞：runtime bridge 细节 |
| 建立可观测与审计接线 | 新增 plugin 日志/指标/审计适配器 | 满足失败可观测与合规留痕 | infra/src/plugin/PluginAuditAdapter.cpp; PluginMetricsAdapter.cpp | integration: PluginAuditTraceIntegrationTest | ctest --test-dir build-ci -R PluginAuditTraceIntegrationTest | 依赖 AuditService 可用 |
| 远程插件能力预留 | 不实现 remote fetch，仅保留开关和接口空实现 | 对齐当前阶段风险控制 | infra/include/plugin/IPluginPackageSource.h | unit: PluginRemoteFetchDisabledTest | ctest --test-dir build-ci -R PluginRemoteFetchDisabledTest | 默认禁用 |

不可立即映射项：
1. 远程插件仓库同步与在线信任链：当前阶段不纳入（缺少供应链治理基线）。
2. 动态沙箱隔离（seccomp/namespace）完整实现：先预留 PluginRuntimeBridge 抽象。

---

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

建议新增：
1. infra/include/plugin/
2. infra/src/plugin/
3. tests/unit/infra/plugin/
4. tests/contract/infra/plugin/
5. tests/integration/infra/plugin/

### 8.2 分阶段实施与完成判定

| 阶段 | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| PLG-M1 对象与接口冻结 | Not Started | 冻结 manifest/descriptor/report 与 IPluginManager 语义 | 子系统详细设计 + INF-TODO-019 | infra/include/plugin/* | unit + contract | cmake --build build-ci --target dasall_infra | 头文件齐备且边界测试通过 |
| PLG-M2 校验链路骨架 | Not Started | 新增 policy/signature/compat 校验管线骨架 | ADR/Blueprint/Policy 设计 | infra/src/plugin/PluginValidationPipeline.cpp | unit | ctest --test-dir build-ci -R PluginValidationPipelineTest | 校验失败可稳定拒绝 |
| PLG-M3 生命周期最小闭环 | Not Started | 新增 load/unload 状态机与运行桥接 | infra 详细设计 6.8 | infra/src/plugin/PluginLifecycleManager.cpp | unit + integration | ctest --test-dir build-ci -R "PluginLifecycleStateTest|InfraPluginLifecycleTest" | load/unload 结果可审计可观测 |
| PLG-M4 门禁与Profile对齐 | Not Started | 接入 profile 行为矩阵与 gate 脚本 | Blueprint 5.1/5.2 | profiles/* + tests/* + scripts/ci/* | compatibility + contract | ctest --test-dir build-ci -L "contract|integration" | 三档 profile 行为一致性通过 |

### 8.3 原子实施任务（建议级）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| PLG-T001 | Not Started | 新增 PluginDescriptor 对象并冻结字段 | infra 详细设计 6.5 | infra/include/plugin/PluginDescriptor.h | PluginDescriptorFieldTest | ctest --test-dir build-ci -R PluginDescriptorFieldTest | 字段与约束一致 |
| PLG-T002 | Not Started | 新增 PluginManifest 对象并定义 schema_version | INF-BLK-09 解阻要求 | infra/include/plugin/PluginManifest.h | PluginManifestSchemaTest | ctest --test-dir build-ci -R PluginManifestSchemaTest | schema 可版本化 |
| PLG-T003 | Not Started | 新增 IPluginManager 接口骨架 | infra 详细设计 6.6 | infra/include/plugin/IPluginManager.h | PluginInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 接口编译通过 |
| PLG-T004 | Not Started | 新增 validate 聚合流程骨架 | 详细设计 6.3/6.8 | infra/src/plugin/PluginValidationPipeline.cpp | PluginValidationPipelineTest | ctest --test-dir build-ci -R PluginValidationPipelineTest | 三检流程失败可区分 |
| PLG-T005 | Not Started | 新增 load/unload 生命周期状态机 | 详细设计 6.7/6.8 | infra/src/plugin/PluginLifecycleManager.cpp | PluginLifecycleStateTest | ctest --test-dir build-ci -R PluginLifecycleStateTest | 状态转移可判定 |
| PLG-T006 | Not Started | 新增插件审计适配器 | 详细设计 6.10 | infra/src/plugin/PluginAuditAdapter.cpp | PluginAuditAdapterTest | ctest --test-dir build-ci -R PluginAuditAdapterTest | 高风险动作有审计 |
| PLG-T007 | Not Started | 新增 plugin 合约边界测试 | contracts 冻结策略 | tests/contract/infra/plugin/* | PluginContractBoundaryTest | ctest --test-dir build-ci -R PluginContractBoundaryTest | 无越权字段 |
| PLG-T008 | Not Started | 新增 profile 插件治理行为矩阵测试 | Blueprint 5.1 | tests/integration/infra/plugin/ProfilePluginMatrixTest.cpp | ProfilePluginMatrixTest | ctest --test-dir build-ci -R ProfilePluginMatrixTest | 档位差异符合预期 |

---

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试层 | 覆盖对象 | 关键用例 | 通过标准 |
|---|---|---|---|
| Unit | Manifest/Descriptor/Validation/Lifecycle | schema 解析、签名失败、ABI 不兼容、状态机转移 | 全断言通过，错误码可判定 |
| Contract | plugin 与 contracts 边界 | 标识字段复用、ResultCode/ErrorInfo 映射稳定 | 无语义越权 |
| Integration | plugin 与 policy/diagnostics/audit/health | reject->audit->diagnostics 证据链闭环 | 关键链路可重复 |
| Failure Injection | 签名伪造、ABI 冲突、load 超时、卸载失败 | 拒绝激活、回滚、告警、证据输出 | 每条失败有观测证据 |
| Compatibility | profile 档位差异 | desktop_full/edge_balanced/edge_minimal 插件行为矩阵 | 不出现 breaking 偏差 |

### 9.2 质量 Gate 建议

| Gate ID | 检查项 | 失败判定 |
|---|---|---|
| PLG-G1 | plugin 单元测试全绿 | 任一 unit 失败即阻断 |
| PLG-G2 | plugin 合约边界测试全绿 | 出现越权字段或语义漂移即阻断 |
| PLG-G3 | 失败注入关键路径全绿 | 任一失败无兜底动作即阻断 |
| PLG-G4 | 审计链路覆盖 | plugin load/unload 或 policy deny 无审计即阻断 |
| PLG-G5 | Profile 兼容检查 | 任一 profile 出现未声明行为差异即阻断 |

---

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low（新增对象/接口） | infra 内部消费者与 runtime 间接调用方 | v1 接口先落地，新增字段 optional，避免删除旧字段 | 先 desktop_full，再 edge_balanced，最后 edge_minimal | 预留 IPluginPackageSource 与 sandbox hint |
| Medium（接口签名调整） | 所有 plugin 调用方 | v1/v2 双接口并存 + 适配器过渡 | 双写审计与指标，稳定后切流 | 预留 remote fetch 与证书轮换策略 |
| High（ABI 规则变更） | 已发布插件包与运维流程 | 主版本升级 + 兼容窗口 + 迁移工具 | 先灰度白名单插件，失败即回滚旧 ABI 规则 | 预留 ABI compatibility profile |

演进原则：
1. 默认向后兼容，新增优于修改，废弃优于删除。
2. 接口签名变更必须经过 breaking review gate。
3. profile 差异通过配置与注册表落地，不在主流程散落条件分支。

---

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| B-PLG-01 manifest 字段未冻结 | PLG-T002~PLG-T005 | 冻结最小 manifest schema（id/version/entry/abi/signature_ref） | 先定义 v1 schema 并加版本号 | 禁止 load，只保留 discover + validate dry-run |
| B-PLG-02 ABI 兼容矩阵未冻结 | PLG-T004~PLG-T005 | 明确 host ABI 与插件 ABI 匹配规则 | 先支持主版本严格匹配 | 禁用跨 ABI 激活 |
| B-PLG-03 签名信任链未冻结 | PLG-T004~PLG-T006 | 明确信任锚、证书轮换、失败码映射 | 先本地 trust store + 离线验签 | 禁用 remote plugin |
| B-PLG-04 runtime bridge 细节未定 | PLG-T005 | 固定最小桥接接口（load_symbol/unload） | 先提供 mock bridge 跑通状态机测试 | 暂停真实动态库加载 |
| B-PLG-05 integration 拓扑不稳定 | PLG-T008 | tests/integration 注册稳定、依赖 mock 完整 | 先跑 unit+contract 门禁 | 延后 integration gate 到下一迭代 |

### 11.2 风险清单

| 风险 | 等级 | 触发条件 | 缓解动作 |
|---|---|---|---|
| 供应链风险 | High | 未验签直接装载插件 | 强制 signature.required=true |
| 边界漂移 | High | PluginDescriptor 扩写 Tool/Skill 语义 | contract 边界测试阻断 |
| 长稳风险 | Medium | 插件加载失败循环重试 | fail_threshold + safe_mode |
| 兼容性风险 | Medium | profile 配置冲突导致行为不一致 | ProfilePluginMatrixTest + gate |

---

## 12. 未决问题与后续任务

### 12.1 未决问题

1. PluginManifest 的最终字段集合和扩展字段命名空间策略。
2. ABI 兼容矩阵是否采用“主版本严格 + 次版本可配置”模式。
3. 签名信任根来源（内置、部署注入、硬件信任区）及轮换窗口。
4. PluginRuntimeBridge 是否需要强制沙箱能力声明。
5. 远程插件能力的供应链审计最小闭环何时纳入。

### 12.2 后续任务建议

1. 在 docs/todos 下新增 plugin 组件专项 TODO，承接 PLG-T001~PLG-T008。
2. 优先解 INF-BLK-09，再从 Blocked 迁移 INF-TODO-019 到 Not Started。
3. 与 security policy 组件联合冻结 policy->plugin 准入决策对象。
4. 先完成 unit/contract 门禁，再打开 integration 与 profile 兼容门禁。
