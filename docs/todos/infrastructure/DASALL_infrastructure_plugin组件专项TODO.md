# DASALL infra/plugin 组件专项 TODO

最近更新时间：2026-03-25  
阶段：Detailed Design -> Special TODO  
适用范围：infra/plugin（infra/src/plugin、infra/include/plugin）  
当前结论：**可进入部分执行，最细可安全落到 L2（数据结构/接口级），暂不允许进入完整装载实现，必须先解除 INF-BLK-09 阻塞**

---

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_infra_plugin模块详细设计.md（v1.0）
2. docs/architecture/DASALL_infrastructure子系统详细设计.md（6.2、6.3、6.4、6.5、6.6、6.8、6.9、6.10）
3. docs/architecture/DASSALL_Agent_architecture.md（3.4.7、5.10.1、5.10.2、8.8、8.9）
4. docs/architecture/DASALL_Engineering_Blueprint.md（3.12、4.1、4.2、4.3、5.1、5.2）
5. docs/adr/ADR-005、ADR-006、ADR-007、ADR-008
6. docs/plans/DASALL_工程落地实现步骤指引.md
7. docs/development/DASALL_工程协作与编码规范.md
8. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md（INF-TODO-019、INF-BLK-09）
9. 当前仓库代码现状：infra/ 无 plugin 子目录，infra/include/ 无 plugin 接口头文件

本文档目的是将 plugin 详细设计转化为：

1. 约束与设计缺口清单
2. 粒度可行性分级
3. Design -> TODO 映射
4. 最小原子任务清单
5. 执行顺序与门禁表
6. 解阻条件与回退策略

---

## 2. plugin 组件目标与范围

### 2.1 组件目标

根据详细设计 1.2、infrastructure 子系统详细设计 6.2、架构文档 5.10.2，plugin 组件目标固定为：

1. 提供插件发现、校验、装载、启停、卸载与兼容管理能力。
2. 将签名校验、来源信任、ABI 兼容与 Profile 裁剪纳入统一治理链路。
3. 对外仅暴露稳定接口与治理结果，不泄露 loader 细节和动态链接实现。
4. 对失败路径提供可观测证据（日志/指标/追踪/审计），满足失败不可吞没。

### 2.2 范围边界

**纳入本专项 TODO** 的对象：

1. IPluginManager、IPluginPolicyGate、IPluginSignatureVerifier、IPluginCompatibilityEngine 接口定义与骨架实现。
2. PluginDescriptor、PluginManifest、PluginCatalog、Plugin*Report 对象定义与映射关系。
3. PluginValidationPipeline、PluginLifecycleManager、Plugin*Adapter 组件骨架。
4. CMake 落盤点、测试注册点、contracts 边界校验点。
5. 根据设计缺口阻塞的前置补设计任务。

**不纳入本专项 TODO** 的对象：

1. RuntimeOrchestrator 主控逻辑与调度权。
2. SecurityPolicyManager 与 DiagnosticsService 的内部实现（仅作为依赖方）。
3. PluginRuntimeBridge 动态库加载细节（platform 层职责）。
4. 远程插件仓库、供应链中心、网络同步（v2 演进）。
5. contracts 公共对象的反向扩写。

---

## 3. 输入依据与约束清单

### 3.1 设计约束抽取表

| 约束 ID | 来源文档 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| PLG-C001 | 架构 5.10.2；infra 详设 6.2 | Must | infra 必须包含 Plugin Manager 关键组件 | 必须生成 IPluginManager 接口与组件任务 |
| PLG-C002 | 架构 3.7；蓝图 4.1 | Must | plugin 只依赖下层抽象与 contracts，不反向依赖业务模块 | 禁止出现 runtime/cognition/llm/tools 头文件依赖 |
| PLG-C003 | 蓝图 4.2 | Must-Not | infra 不得依赖业务模块实现 | 代码目标仅在 infra/、tests/、docs/ 等 infra 属域 |
| PLG-C004 | 蓝图 4.3；架构 3.8 | Must | 跨模块调用仅通过 contracts 稳定接口 | 错误码映射、标识字段只消费现有 contracts 语义 |
| PLG-C005 | ADR-005 | Must | contracts 与关键边界冻结前，不反向改写主架构 | 设计缺口必须标记 Blocked，禁伪造实现 |
| PLG-C006 | ADR-006 | Must-Not | plugin 仅记录上下文标识，不组装 ContextPacket | 日志/审计不生成业务语义上下文 |
| PLG-C007 | ADR-007 | Must-Not | plugin 不做失败语义判定，仅返回结果与证据 | load 失败仅返回 LoadResult+evidence_ref，不裁定恢复 |
| PLG-C008 | ADR-008 | Must | plugin 不拥有调度权，仅接受主控触发 | PluginLifecycleManager 不推进 runtime 状态机 |
| PLG-C009 | infra 详设 6.5 | Must | PluginDescriptor 仅治理字段，不扩写 Tool/Skill | 字段集合仅含 plugin_id/version/abi/trust_level/status/source |
| PLG-C010 | infra 详设 6.6 | Must | IPluginManager 包含 discover/validate/load/unload | 接口名与方法语义必须严格遵循详设 6.6 |
| PLG-C011 | infra 详设 6.8/6.10 | Must | 校验失败必须拒绝激活且写审计 | validate/load 失败路径必须覆盖审计与兼容报告 |
| PLG-C012 | 蓝图 5.1/5.2 | Must | Profile 仅裁剪能力，不绕过审计与主控 | plugin 配置项受 ProfilePluginMatrixTest 约束 |
| PLG-C013 | 编码规范 3.6 | Must | 失败不可吞没，必须可观测 | 错误码、审计、指标三类观测必须有对应测试出口 |
| PLG-C014 | 编码规范 3.7 | Should | 公共接口同步新增 unit/contract/integration 测试 | 任务需附测试目标与验收命令 |
| PLG-C015 | infra 子系统 TODO INF-BLK-09 | Must | manifest/ABI/签名链路冻结前，仅进接口与对象级 | 不进实现装载、不进完整验签链这样的函数级任务 |

### 3.2 现状与缺口证据

| 缺口对象 | 当前状态 | 影响等级 | 修复优先级 |
|---|---|---|---|
| infra/src/plugin 子目录 | 缺失 | High | P0 |
| infra/include/plugin 接口头文件 | 缺失 | High | P0 |
| PluginDescriptor/Manifest/Catalog 对象 | 仅在设计文档有字段，无代码落盘 | High | P0 |
| IPluginManager 接口 | 无对应头文件 | High | P0 |
| PluginManifest schema_version 与字段冻结 | INF-BLK-09 阻塞 | High | Blocked |
| ABI 兼容矩阵规则 | INF-BLK-09 阻塞 | High | Blocked |
| 签名校验链路规范 | INF-BLK-09 阻塞 | High | Blocked |
| Plugin 单元测试基线 | 无 tests/unit/infra/plugin | Medium | P1 |
| PluginValidationPipeline 实现 | 需要依赖 INF-TODO-017（SecurityPolicyManager） | Medium | P1 |
| PluginAuditAdapter 实现 | 需要依赖 INF-TODO-016（AuditService） | Medium | P1 |

---

## 4. 粒度可行性评估

### 4.1 总体结论

**结论**：当前 plugin 详细设计整体达到 **L2（数据结构/接口级）**，局部未达 **L3（函数/方法级）**。

**理由**：

1. ✓ 已具备核心接口清单：IPluginManager、IPluginPolicyGate、IPluginSignatureVerifier、IPluginCompatibilityEngine、IPluginRuntimeBridge。
2. ✓ 已具备核心对象字段：PluginDescriptor、PluginManifest、PluginCatalog、Plugin*Report。
3. ✓ 已具备主流程与异常流程说明：6.7 主流程时序、6.8 异常与恢复。
4. ✓ 已具备错误码域清单与映射：六个 INF_E_PLUGIN_* 错误码。
5. ✓ 已具备目录与测试矩阵建议：8.1 目录建议、9.1 测试矩阵。
6. ✗ **缺失关键对象冻结**：PluginManifest 字段集合未最终冻结，允许扩展字段未定义。
7. ✗ **缺失签名校验规范**：trust_store 信任锚、证书链验签、来源信任等级未冻结。
8. ✗ **缺失 ABI 兼容矩阵**：主版本/次版本/API 兼容规则未确定，严格模式配置项存在。
9. ✗ **缺失 PluginRuntimeBridge 入口**：platform 层交互接口约定不完整。

### 4.2 粒度可行性分级表

| 设计对象 | 设计锚点 | 粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| PluginDescriptor | 详设 6.5 | L2 | 字段清单、必填约束、治理状态机、contracts 映射 | 序列化格式 | 直接拆数据结构任务 |
| PluginManifest | 详设 6.5；INF-BLK-09 | L1 | 字段样例、schema_version 版本化建议、capabilities 约束 | 最终字段集、扩展命名空间、序列化中间格式 | 先补设计，再拆任务；当前 Blocked |
| PluginCatalog | 详设 6.3、6.5 | L2 | 结构字段、rejected_plugins 与拒绝原因链接 | 大小限制、导出限制 | 直接拆数据结构任务 |
| SignatureReport | 详设 6.5、6.8 | L2 | 字段清单、验签失败禁止 load、链状态说明 | trust_store 来源与链验规则 | 直接拆数据结构，阻塞依赖 INF-BLK-09 |
| CompatibilityReport | 详设 6.5、6.8 | L2 | 字段清单、三检结果聚合、reason codes | ABI 兼容规则详尽定义 | 直接拆数据结构，阻塞依赖 INF-BLK-09 |
| IPluginManager | 详设 6.6；infra 系统 6.6 | L2 | 方法名、输入输出对象、主流程调用关系 | 内部错误重试策略、timeout 约定、exception 映射 | 直接拆接口级任务，实现保持 skeleton |
| IPluginPolicyGate | 详设 6.3、6.6 | L2 | 方法名、输入（PolicySnapshot、allowlist）、输出 | 裁定优先级冲突时的详细规则 | 直接拆接口级任务，依赖 INF-TODO-017 |
| IPluginSignatureVerifier | 详设 6.3、6.6 | L1 | 方法名、输入输出、验签失败禁止 load | trust_store 结构、链验细节、失败码详尽定义 | 先补设计，当前 Blocked（INF-BLK-09） |
| IPluginCompatibilityEngine | 详设 6.3、6.6 | L1 | 方法名、check 方法、三检输出 | ABI/API 匹配规则表、主次版本定义、Host ABI 识别 | 先补设计，当前 Blocked（INF-BLK-09） |
| PluginValidationPipeline | 详设 6.3、6.7/6.8 | L2 | 触发时机、三检顺序、失败枝条、输出聚合对象 | 并行 vs 串行执行约束、timeout 编排、回滚能力 | 直接拆流程任务，依赖接口与 INF-TODO-017 |
| PluginLifecycleManager | 详设 6.7/6.8 | L1 | 状态机图、load/unload/enable/disable 转移、故障恢复 | safe_mode 触发逻辑、handle 释放时机、signal 处理 | 先补设计，当前不拆实现任务，仅接口骨架 |
| PluginAuditAdapter | 详设 6.8、6.10 | L2 | 高风险动作清单、审计字段最小集、强制审计条件 | 审计导出格式、脱敏规则 | 直接拆适配器任务，依赖 INF-TODO-016 |
| infra CMake/plugin 注册 | 详设 8.1；当前代码现状 | L2 | 目录建议、文件列表、test 子目录 | integration 顶层 CMake 接入细节 | 直接拆注册任务 |

---

## 5. Design -> TODO 映射表

| Design 项 | 设计锚点 | TODO 类型 | 优先级 | 状态 | 对应任务 ID 范围 | 映射说明 |
|---|---|---|---|---|---|---|
| PluginDescriptor 字段与约束冻结 | 详设 6.5；infra 系统 6.5 | 数据结构 | P0 | Done | PLG-TODO-001 | 该对象字段清单齐备，可直接落 L2 |
| PluginCatalog 结构定义 | 详设 6.5、6.3 | 数据结构 | P0 | Done | PLG-TODO-002 | discovered_plugins[] + rejected_plugins[] 清晰，可直接拆 |
| IPluginManager 接口冻结与骨架 | 详设 6.6；infra 系统 6.6 | 接口 | P0 | Done | PLG-TODO-003 | 已通过最小 request/result 边界收敛修复签名缺口，接口与 skeleton 可直接编译验证 |
| IPluginPolicyGate 接口定义 | 详设 6.6；infra 系统 6.6 | 接口 | P0 | Done | PLG-TODO-004 | 已通过最小 PluginPolicyRequest 收敛 manifest/profile 缺口，并复用已冻结的 PolicySnapshot/PolicyDecisionRef 边界 |
| SignatureReport 对象冻结 | 详设 6.5、6.8 | 数据结构 | P1 | Blocked | PLG-BLK-01 | verified/signer/chain_status/reason 可定义，但 trust_store 细节阻塞 |
| CompatibilityReport 对象冻结 | 详设 6.5、6.8 | 数据结构 | P1 | Blocked | PLG-BLK-02 | abi_ok/api_ok/dependency_ok 可定义，但 ABI 规则阻塞 |
| IPluginSignatureVerifier 接口 | 详设 6.6 | 接口 | P1 | Blocked | PLG-BLK-03 | 接口名存在，但实现依赖 trust_store 与链验规范冻结 |
| IPluginCompatibilityEngine 接口 | 详设 6.6 | 接口 | P1 | Blocked | PLG-BLK-04 | 接口名存在，但实现依赖 ABI 兼容矩阵冻结 |
| PluginManifest 对象冻结 | 详设 6.5；INF-BLK-09 | 数据结构 | P0 | Blocked | PLG-BLK-05 | schema_version 版本化，但字段集与扩展模型未冻结 → INF-BLK-09 |
| PluginValidationPipeline 建立 | 详设 6.3、6.7/6.8 | 流程 | P0 | Not Started | PLG-TODO-005 | 三检顺序明确，可先拆骨架，不进完整实现 |
| PluginAuditAdapter 建立 | 详设 6.10；infra 系统 6.10 | 适配器 | P0 | Not Started | PLG-TODO-006 | 审计字段列表清晰，但依赖 INF-TODO-016 AuditService |
| plugin 私有错误码域 | 详设 6.6、9.1 | 错误码 | P0 | Done | PLG-TODO-007 | 六个错误码已列，可直接定义 |
| plugin 构建入口接线 | 详设 8.1、8.2 | 构建/测试 | P0 | Done | PLG-TODO-008 | plugin source/public header 已按组件收敛到 infra CMake 入口 |
| plugin 单元测试入口注册 | 详设 9.1；编码规范 3.7 | 测试 | P0 | Done | PLG-TODO-009 | plugin unit 注册逻辑已下沉到 tests/unit/infra/plugin 子目录，并以组件级列表接入顶层聚合 |
| plugin 合约边界测试入口注册 | 蓝图 4.3；contracts 冻结策略；编码规范 3.7 | 测试 | P0 | Not Started | PLG-TODO-010 | tests/contract 已有 plugin 边界用例，仍需收敛 helper 与 discoverability |
| PluginLifecycleManager 状态机 | 详设 6.7/6.8 | 流程 | P1 | Not Started | PLG-TODO-011 | 状态机与故障恢复图存在，仅提供骨架与状态转移测试 |
| 失败注入测试与指标验证 | 详设 9.1、9.2 | 测试 | P1 | Not Started | PLG-TODO-012 | 关键失败场景可测，但需要实现才能注入 |
| Profile 插件治理行为矩阵测试 | 蓝图 5.1/5.2；详设 6.9 | 测试 | P1 | Not Started | PLG-TODO-013 | 配置项与默认值已列表，可写行为组合测试 |

---

## 6. 原子任务清单

### 6.1 Build-ready 任务（可直接推进）

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度 | 代码目标 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PLG-TODO-001 | Done | 定义 PluginDescriptor 数据结构 | 详设 6.5；infra 系统 6.5 | 详设 6.5 核心对象与 contracts 对齐 | L2 | infra/include/plugin/PluginDescriptor.h，承载 plugin_id、version、abi、trust_level、status、source 字段 | unit：字段默认值与 unknown 兜底；contract：不越权扩写标识语义 | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_descriptor_unit_test dasall_contract_plugin_descriptor_boundary_test && ctest --test-dir build-ci -N -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest"` | 无 | 无 | 无 | 数据结构头文件、基础单测、字段说明文档 | 已完成（2026-04-01）：PluginDescriptor.h、PluginDescriptorFieldTest、PluginDescriptorBoundaryContractTest 落盘；空值统一收敛为 unknown，ctest 发现 2 个用例并全部通过 |
| PLG-TODO-002 | Done | 定义 PluginCatalog 数据结构 | 详设 6.5、6.3 | 详设 6.5 对象、6.3 发现结果语义 | L2 | infra/include/plugin/PluginCatalog.h，承载 discovered_plugins[]、rejected_plugins[] 字段，拒绝原因可追溯 | unit：空 catalog、全发现、全拒绝三种状态；contract：与 Observation evidence_ref 关联性测试 | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_catalog_unit_test dasall_contract_plugin_catalog_boundary_test && ctest --test-dir build-ci -N -R "PluginCatalogTest|PluginCatalogBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginCatalogTest|PluginCatalogBoundaryContractTest"` | PLG-TODO-001 | 无 | 无 | 数据结构头文件、单测与合约测试 | 已完成（2026-04-01）：PluginCatalog.h、PluginCatalogTest、PluginCatalogBoundaryContractTest 落盘；空 catalog/全发现/全拒绝和 evidence_ref 边界均已覆盖，ctest 发现 2 个用例并全部通过 |
| PLG-TODO-003 | Done | 新增 IPluginManager 接口与骨架实现 | 详设 6.6；infra 系统 6.6 | 详设 6.6 核心接口、6.7 主流程时序 | L2 | infra/include/plugin/IPluginManager.h 与 infra/src/plugin/PluginManager.cpp 骨架，包含 discover()、validate()、load()、unload()、list_active() 五个方法 | unit：接口编译与返回对象类型；contract：ResultCode 引用不越权 | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_manager_interface_unit_test dasall_contract_plugin_manager_boundary_test && ctest --test-dir build-ci -N -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest"` | PLG-TODO-001、PLG-TODO-002 | 无 | 无 | 接口头文件、空壳实现、编译与单测证据 | 已完成（2026-04-01）：IPluginManager.h、PluginManager.cpp、PluginManagerInterfaceCompileTest、PluginManagerBoundaryContractTest 落盘；最小 request/result 边界已在同轮收敛，ctest 发现 2 个用例并全部通过 |
| PLG-TODO-004 | Done | 新增 IPluginPolicyGate 接口 | 详设 6.6；infra 系统 6.6 | 详设 6.6 IPluginPolicyGate、6.3 策略决策 | L2 | infra/include/plugin/IPluginPolicyGate.h，包含 evaluate(manifest, policy_snapshot, profile) 方法，返回 PolicyDecisionRef | unit：接口编译；contract：不越权扩写策略对象 | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_policy_gate_interface_unit_test dasall_contract_plugin_policy_gate_boundary_test && ctest --test-dir build-ci -N -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest"` | PLG-TODO-001、INF-TODO-017（SecurityPolicyManager） | 无 | 无 | 接口头文件、编译证据 | 已完成（2026-04-01）：IPluginPolicyGate.h、PluginPolicyGateInterfaceCompileTest、PluginPolicyGateBoundaryContractTest 落盘；最小 PluginPolicyRequest 已在同轮收敛，ctest 发现 2 个用例并全部通过 |
| PLG-TODO-005 | Not Started | 建立 PluginValidationPipeline 骨架与三检流程 | 详设 6.3、6.7、6.8 | 详设 6.7 主流程、6.8 异常流程、6.3 子组件依赖 | L2 | infra/src/plugin/PluginValidationPipeline.cpp（or PluginValidatorImpl.cpp），骨架实现 policy->signature->compat 三检流程，失败时返回对应 Report | unit：三检失败分支可区分；contract：聚合 Report 语义稳定 | `cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci --output-on-failure -R "PluginValidationPipelineTest"` | PLG-TODO-003、PLG-TODO-004、PLG-TODO-001、PLG-TODO-002（以及 INF-TODO-017） | 无 | 无 | pipeline 实现、单测 | 仅当三检流程失败可判定（拒绝可溯源）且编译通过时完成 |
| PLG-TODO-006 | Not Started | 新增 PluginAuditAdapter 适配器 | 详设 6.10；infra 系统 6.10 | 详设 6.10 审计点与字段清单 | L2 | infra/src/plugin/PluginAuditAdapter.cpp，负责生成 plugin load/unload 与 policy deny 的 AuditEvent，含 actor、action、target、outcome、evidence_ref、reason_code | unit：AuditEvent 必填字段校验；integration：审计写入与导出可追踪 | `cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci --output-on-failure -L "unit|integration" -R "PluginAudit"` | PLG-TODO-001、INF-TODO-016（AuditService） | 无 | 无 | adapter 实现、单测与集成测试 | 仅当高风险动作（load/unload/policy deny）有审计记录，且 evidence_ref 可解引时完成 |
| PLG-TODO-007 | Done | 定义 plugin 私有错误码域 | 详设 6.6、9.1 | 详设 6.6 错误语义、9.1 failure injection 场景 | L2 | infra/include/InfraErrorCode.h（or 新增 plugin/PluginErrorCode.h），定义六个 INF_E_PLUGIN_* 错误码并在 infra facade 侧建立映射入口 | unit：码值可判定；contract：映射 contracts::ResultCode 时不新增共享语义 | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_error_code_unit_test dasall_contract_plugin_error_code_boundary_test && ctest --test-dir build-ci -N -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest"` | 无 | 无 | 无 | 错误码头文件、映射说明、单测与合约测试 | 已完成（2026-04-01）：PluginErrorCode.h、PluginErrorCodeTest、PluginErrorCodeBoundaryContractTest 落盘；六个 `INF_E_PLUGIN_*` 码名已在设计收敛文档中冻结并全部通过 unit/contract 验证 |
| PLG-TODO-008 | Done | 接线 infra/src/plugin 与 infra/include/plugin CMake 目标 | 详设 8.1；当前 CMake 现状 | 详设 8.1 目录落盘建议、8.2 分阶段实施 | L2 | 更新 infra/CMakeLists.txt，新增 plugin 子目录与源文件列表（允许仅添加头文件与空实现入口） | build：dasall_infra 目标可编译且包含 plugin 真实头文件 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra && rg -n "DASALL_INFRA_PLUGIN_(SOURCES|PUBLIC_HEADERS)" infra/CMakeLists.txt` | PLG-TODO-001、PLG-TODO-002、PLG-TODO-003、PLG-TODO-004、PLG-TODO-007 | 无 | 无 | infra/CMakeLists.txt 改动、构建输出 | 已完成（2026-04-07）：infra/CMakeLists.txt 新增 `DASALL_INFRA_PLUGIN_SOURCES` 与 `DASALL_INFRA_PLUGIN_PUBLIC_HEADERS`，plugin 源与公开头不再散落在全局清单中，`dasall_infra` 构建通过 |
| PLG-TODO-009 | Done | 注册 tests/unit/infra/plugin 单元测试入口 | 详设 9.1；编码规范 3.7 | 详设 9.1 测试矩阵、编码规范 3.7 | L2 | 新增 tests/unit/infra/plugin/ 目录与 PluginDescriptorTest.cpp/PluginCatalogTest.cpp 等，在 tests/unit/CMakeLists.txt 中注册 plugin 子目录 | unit：用例被 ctest -L unit 发现 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_plugin_descriptor_unit_test dasall_plugin_catalog_unit_test dasall_plugin_error_code_unit_test dasall_plugin_manager_interface_unit_test dasall_plugin_policy_gate_interface_unit_test && ctest --test-dir build-ci -N -L unit \| grep -i plugin && ctest --test-dir build-ci --output-on-failure -L plugin` | PLG-TODO-001、PLG-TODO-002、PLG-TODO-003、PLG-TODO-004、PLG-TODO-007、PLG-TODO-008 | 无 | 无 | tests/unit/infra/plugin 目录与 CMakeLists.txt、ctest 发现证据 | 已完成（2026-04-07）：新增 tests/unit/infra/plugin/CMakeLists.txt，并把 plugin unit 注册从父级 CMake 下沉到子目录入口；`ctest -N -L unit` 可发现 5 个 plugin 用例，`ctest -L plugin` 5/5 通过 |
| PLG-TODO-010 | Not Started | 注册 tests/contract/infra/plugin 合约边界测试入口 | 蓝图 4.3；contracts 冻结策略；编码规范 3.7 | 详设 6.5 contracts 对齐关系、9.1 contract 覆盖目标 | L2 | 在 tests/contract/ 现有注册机制下新增 plugin 边界用例，例如 PluginObjectBoundaryTest 检查标识字段不越权、ErrorCodeMappingTest 检查码映射稳定 | contract：标识字段、错误码、AuditEvent 引用不越权 | `cmake -S . -B build-ci -G "Unix Makefiles" && ctest --test-dir build-ci -L contract --output-on-failure -R "Plugin"` | PLG-TODO-001、PLG-TODO-002、PLG-TODO-003、PLG-TODO-004、PLG-TODO-007、PLG-TODO-008、PLG-TODO-009 | 无 | 无 | tests/contract/plugin/ 源文件、contract 执行记录 | 仅当新增 contract 测试被发现且能阻止 contracts 语义越权时完成 |
| PLG-TODO-011 | Not Started | 新增 PluginLifecycleManager 状态机与转移测试 | 详设 6.7、6.8 | 详设 6.7 主流程、6.8 异常与恢复时序 | L2 | infra/src/plugin/PluginLifecycleManager.cpp（skeleton），包含 load/unload/enable/disable 四个方法与内部状态转移，不进入实际 dlopen/dlsym 实现 | unit：状态机转移枚举测试（loaded->active、loaded->disabled、failed->cleanup）；contract：LoadResult/UnloadResult 返回值稳定 | `ctest --test-dir build-ci --output-on-failure -R "PluginLifecycleStateTest"` | PLG-TODO-003、PLG-TODO-006 | 无 | 无 | lifecycle 骨架实现、状态转移单测 | 仅当状态机转移可预测、失败路径可审计时完成 |
| PLG-TODO-012 | Not Started | 编写 plugin 失败注入与可观测性测试 | 详设 9.1、9.2 | 详设 9.1 failure injection 场景、9.2 quality gates | L2 | tests/ 下新增 PluginSignatureFailureTest、PluginCompatibilityFailureTest、PluginLoadTimeoutTest 等，验证失败路径有日志/审计/指标 | integration：当 validate fail 时有 AuditEvent、当 load fail 时有 CompatibilityReport | `ctest --test-dir build-ci -L integration --output-on-failure -R "PluginFailure\|PluginObservability"` | PLG-TODO-005、PLG-TODO-006、PLG-TODO-011 | 无 | 无 | 失败注入测试源文件、执行记录 | 仅当关键失败路径（签名失败、兼容失败、load 超时）均有可观测证据时完成 |
| PLG-TODO-013 | Not Started | 编写 Profile 插件治理行为矩阵测试 | 蓝图 5.1、5.2；详设 6.9 | 详设 6.9 配置项与默认策略、蓝图 3.13 profile 机制 | L2 | tests/ 下新增 ProfilePluginMatrixTest.cpp，覆盖 desktop_full/edge_balanced/edge_minimal 三档 profile 下 plugin 配置项的行为差异验证 | compatibility：三档 profile 下 plugin.allowlist、infra.plugin.signature.required、infra.plugin.abi.strict_mode 等行为一致性检查 | `ctest --test-dir build-ci --output-on-failure -R "ProfilePluginMatrix"` | 详设 9.1 里程碑 M4（all 标准对象与接口冻结） | 无 | 无 | profile 矩阵测试源文件、行为表 | 仅当三档 profile 下 plugin 行为无意外偏差时完成 |

### 6.2 Blocked 阻塞任务（需先解除前置）

| ID | 状态 | 任务标题 | 来源依据 | 阻塞原因 | 设计锚点 | 解阻条件 | 最小解阻动作 | 解阻后任务号 |
|---|---|---|---|---|---|---|---|---|
| PLG-TODO-014 | Blocked | 定义 PluginManifest 对象与 schema 冻结 | 详设 6.5；INF-BLK-09 | manifest 字段集与扩展字段命名空间未冻结 → INF-BLK-09 | 详设 6.5；6.9 | 确定最终 manifest 字段集合（schema_version、plugin_id、version、entry、required_abi、capabilities、signature_ref 是否完整），定义可选字段与扩展命名空间策略 | 新增"PluginManifest Schema 对象冻结"文档，列出 v1.0 字段与版本迭代规则 | 将从 Blocked 迁移到 Not Started，后续拆 PLG-TODO-014-A（对象定义）、PLG-TODO-014-B（单元测试） |
| PLG-TODO-015 | Blocked | 定义 IPluginSignatureVerifier 与签名链路规范 | 详设 6.6；INF-BLK-09 | trust_store 来源、证书链验证规范、信任等级定义未冻结 → INF-BLK-09 | 详设 6.6、6.8、9.1 | 冻结 trust_store 结构、链验算法、签名失败码映射、信任等级（untrusted/external/vendor/internal）枚举 | 新增"Plugin 签名与信任管理规范"文档，含 trust_store 初始化方式、轮换策略、信任等级判定规则 | 将从 Blocked 迁移到 Not Started，后续拆 PLG-TODO-015-A（接口定义）、PLG-TODO-015-B（验证流程骨架） |
| PLG-TODO-016 | Blocked | 定义 IPluginCompatibilityEngine 与 ABI 兼容矩阵 | 详设 6.6；INF-BLK-09 | ABI 主次版本定义、兼容规则（严格/可配置）、Host ABI 识别机制未冻结 → INF-BLK-09 | 详设 6.6、6.8、6.9 | 冻结 ABI 版本格式（MAJOR.MINOR.PATCH）、兼容检查规则表、platform tag 语义（x86_64-linux-gnu/aarch64-linux-gnu 等）、infra.plugin.abi.strict_mode 影响范围 | 新增"Plugin ABI 兼容性政策文档"，含兼容矩阵表、strict mode 对应行为、跨 ABI 激活禁止条件 | 将从 Blocked 迁移到 Not Started，后续拆 PLG-TODO-016-A（接口定义）、PLG-TODO-016-B（兼容检查实现骨架） |
| PLG-TODO-017 | Blocked | 定义 SignatureReport 与 CompatibilityReport 对象 | 详设 6.5、6.8；INF-BLK-09 | 上游三个 manifest/signature/abi 阻塞解决后，才能最终确定报告对象格式 | 详设 6.5、6.8 | 冻结 SignatureReport(verified、signer、chain_status、reason)、CompatibilityReport(abi_ok、api_ok、dependency_ok、reasons[])、报告聚合与优先落地 | 在 PLG-TODO-014/015/016 解阻后，补齐两个 report 对象的精确字段与错误码映射 | 将从 Blocked 迁移到 Not Started → PLG-TODO-017（report 对象定义与单测） |

---

## 7. 执行顺序建议与门禁表

### 7.1 任务执行顺序（含并行段）

| 顺序段 | 执行任务 | 并行性 | 说明 |
|---|---|---|---|
| **Phase 1：基础对象冻结** | PLG-TODO-001、002、007 | ✓ 并行可行 | 三个对象互相弱依赖（仅逻辑约束，无代码依赖），可并行推进 |
| **Phase 2：核心接口冻结** | PLG-TODO-003、004 | ✓ 并行可行（以 Phase 1 完成为前置） | 两个接口定义，依赖 Phase 1 对象落盘 |
| **Phase 3：构建与测试入口** | PLG-TODO-008、009、010 | ✓ 并行可行（以 Phase 1-2 完成为前置） | CMake 与测试注册，不互相依赖 |
| **Phase 4：流程骨架与观测适配** | PLG-TODO-005、006、011 | ✓ 并行可行（以 Phase 2-3 完成为前置） | 三个流程骨架可独立推进，仅逻辑约束 |
| **Phase 5：测试完善与兼容性验证** | PLG-TODO-012、013 | ✓ 并行可行（以 Phase 4 完成为前置） | 失败注入与 profile 矩阵测试可并行 |
| **解阻准备** | PLG-TODO-014、015、016、017 | ✗ 依赖 INF-BLK-09 解除 | 阻塞项需外部一体化协调（manifest/signature/abi 三项同步冻结） |

### 7.2 关键行为路径（Critical Path）

```
PLG-TODO-001 → PLG-TODO-003 → PLG-TODO-005 → PLG-TODO-012
  ↓                ↓              ↓              ↓
PLG-TODO-002     PLG-TODO-004  PLG-TODO-006   PLG-TODO-013
  ↓
PLG-TODO-008 → PLG-TODO-009 → PLG-TODO-010
  ↓
PLG-TODO-011
```

### 7.3 必过质量门表

| Gate ID | 门禁项 | 触发时机 | 通过标准 | 不通过后动作 |
|---|---|---|---|---|
| PLG-GATE-01 | 接口冻结门 | 完成 PLG-TODO-003、004 后 | IPluginManager 与 IPluginPolicyGate 头文件齐备，方法名与详设 6.6 完全一致，无业务模块头文件依赖 | 退回接口定义，修复后再过门 |
| PLG-GATE-02 | contracts 边界门 | 完成 PLG-TODO-001、002、007 后 | 新增 plugin 对象仅消费既有 contracts 标识语义（request_id/trace_id 等），contract 边界测试通过 | 审查对象字段定义，移除越权字段 |
| PLG-GATE-03 | 构建门 | 完成 PLG-TODO-008 后 | `cmake --build build-ci --target dasall_infra` 无错误 | 修复 CMake 配置或源文件问题 |
| PLG-GATE-04 | unit 发现门 | 完成 PLG-TODO-009 后 | `ctest --test-dir build-ci -N -L unit \| grep -i plugin` 能列出至少 5 个 plugin 相关用例 | 检查 tests/unit/infra/plugin/CMakeLists.txt 注册 |
| PLG-GATE-05 | contract 边界门 | 完成 PLG-TODO-010 后 | `ctest --test-dir build-ci -L contract --output-on-failure -R Plugin` 全绿 | 修复越权字段或语义漂移 |
| PLG-GATE-06 | 失败兜底门 | 完成 PLG-TODO-012 后 | 每条关键失败路径（validate fail、load fail）都有审计或 report 证据 | 补充失败路径的观测点 |
| PLG-GATE-07 | 解阻协调门 | 推进 PLG-TODO-014+/015+/016+/017+ 前 | INF-BLK-09 已通过 infra 子系统协调解除（manifest/signature/abi 三项同步冻结） | 等待上游依赖解阻 |
| PLG-GATE-08 | breaking 评审门 | 任意接口签名或 contracts 映射变更前 | 评审明确 breaking 风险等级、迁移窗口、回退方案 | 未评审禁止推进 |

---

## 8. 阻塞项管理与解阻路径

### 8.1 关键阻塞项详表

| 阻塞 ID | 阻塞描述 | 根本原因 | 影响范围 | 解阻所有者 | 解阻优先级 | 最小解阻 milestone | 解阻后下一步 |
|---|---|---|---|---|---|---|---|
| **INF-BLK-09（上游）** | plugin manifest、ABI 兼容矩阵、签名链路三项未同步冻结 | 跨子域协调（plugin 自身需等待 signature verification 与 compatibility engine 共同冻结） | PLG-TODO-014/015/016/017、后续装载实现、测试完善 | infra 系统设计评审（涉及多个子域） | P0（critical path） | M3 里程碑（校验链路骨架完成） | 解除后立即启动 PLG-TODO-014+ 系列 |
| **PLG-BLK-01** | manifest schema 最终字段集与扩展命名空间未定 | 依赖 INF-BLK-09 同步冻结 + 与 tools/skill 侧协商防止语义重复 | PLG-TODO-014 | plugin 组件负责人 + tools 组件负责人 | P0 | M2 | 拆分 PLG-TODO-014-A（对象定义） + PLG-TODO-014-B（单元测试） |
| **PLG-BLK-02** | ABI 兼容矩阵与 Host ABI 识别机制未定 | 依赖 INF-BLK-09 同步冻结 + 需确认 platform 层的 ABI 能力 | PLG-TODO-016 | plugin 组件负责人 + platform 组件负责人 | P0 | M2 | 拆分 PLG-TODO-016-A（接口定义） + PLG-TODO-016-B（兼容检查框架） |
| **PLG-BLK-03** | 签名校验与信任链规范未定 | 依赖 INF-BLK-09 同步冻结 + 安全策略与审计链路对齐 | PLG-TODO-015 | plugin 组件负责人 + security policy 组件负责人 | P0 | M2 | 拆分 PLG-TODO-015-A（接口定义） + PLG-TODO-015-B（验证流程） |
| **PLG-BLK-04** | PluginRuntimeBridge 与平台动态库接口的约定不完整 | platform 层 HAL 与 plugin 侧的契约需联合冻结 | PLG-TODO-011（PluginLifecycleManager）、后续装载实现 | platform 组件负责人 + plugin 组件负责人 | P1（可先用 mock bridge） | M3 | 先用 mock bridge 跑通单元与失败注入，后补平台集成 |
| **PLG-BLK-05** | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；plugin integration/failure 是否可执行改由组件自身落盘负责 | 仓库级 tests integration 拓扑已稳定，当前缺口转为 plugin 组件具体用例与观测链落盘 | PLG-TODO-012/013 | infra 系统负责人 + tests 架构负责人 | P1 | M4 | 后续按组件落盘 plugin failure/profile/integration 用例并执行 gate |

### 8.2 与上游依赖关系（INF-BLK-09 解阻路径）

```plaintext
infra 子系统详细设计（已完成）
        ↓
INF-TODO-017（SecurityPolicyManager 冻结）─→ PLG-TODO-004 可进实现
        ↓
INF-TODO-016（AuditService 冻结）────────→ PLG-TODO-006 可进实现
        ↓
INF-BLK-09 解阻（三项同步冻结）───────────┐
        ├─→ manifest 字段冻结 ─────→ PLG-TODO-014
        ├─→ ABI 兼容矩阵冻结 ──────→ PLG-TODO-016
        └─→ 签名规范冻结 ─────────→ PLG-TODO-015
        ↓
PLG-TODO-014/015/016/017 进展
        ↓
完整 PluginValidationPipeline 与 PluginLifecycleManager 实现（M3）
```

---

## 9. 风险与回退策略

### 9.1 风险清单

| 风险 | 等级 | 触发条件 | 缓解动作 | 回退策略 |
|---|---|---|---|---|
| 误把 L2 设计当成 L3 实现 | High | 直接开始写 load()、unload() 完整实现前未冻结 manifest/ABI 规则 | Phase 1-3 是严格的对象与接口冻结，不进实现细节 | 禁止实现细节，回到接口/对象定义，先补设计 |
| plugin 越权依赖 runtime/tools | High | 为便利包含 runtime/tools 实现头文件或直接调用它们 | contract 边界测试必须阻断这类 include | 触发 breaking review gate，立即回滚 |
| 错误码与审计语义漂移 | High | 在 PLG-TODO-007 后直接修改私有错误码而不映射 contracts::ResultCode | contract 测试与 gate 把守映射稳定性 | 回退码映射，严格按 infra 子系统统一规则 |
| 供应链风险（未验签直接装载） | High | 跳过 SignatureVerifier 步骤或设 signature.required=false 上线 | 单元与失败注入测试覆盖签名失败拒绝 | 禁止在 edge_minimal 等受限 profile 中设为 false，必须灰度审核 |
| Profile 行为不一致导致升级失败 | Medium | 三档 profile 的 plugin 策略不通过 ProfilePluginMatrixTest | ProfilePluginMatrixTest 作为门禁阶段强制过 | 回滚到上一版本，重新评审配置策略，再发布 rc 版本 |
| 向后兼容破坏 | Medium | manifest schema 或 ABI 规则在不通知用户情况下变更 | 接口版本化、manifest schema_version 强制版本号、breaking review gate | 发布兼容性公告，提供迁移工具，保留旧版本规则窗口 |
| 长稳循环失败无兜底 | Medium | 插件反复加载失败且无 safe_mode 自动转移 | 失败注入测试验证 safe_mode 触发条件与行为 | 手工干预或回滚 plugin allowlist |

### 9.2 回退与恢复级别

| Level | 触发条件 | 回滚对象 | 恢复时间 | 责任方 |
|---|---|---|---|---|
| L0 本地回退 | 单个 plugin 加载失败 3 次 | 从 active plugins 中移除该 plugin，进入 safe_mode | 秒级 | PluginLifecycleManager + safe_mode 检查 |
| L1 版本下沉 | manifest schema 变更无法解析 | 降级到上一 plugin schema version；允许旧 manifest 格式共存 | 分钟级 | 清单版本控制机制 + runtime 灰度策略 |
| L2 功能降级 | ABI 不兼容导致大量 plugin 拒绝激活 | 关闭 infra.plugin.abi.strict_mode，允许宽容兼容模式 | 小时级 | 配置中心 + 部署运维 |
| L3 系统隔离 | plugin safe_mode 无法稳定恢复 | 禁用所有动态插件，仅保留硬编码白名单能力 | 小时级 | infra.plugin.enabled=false + 重新部署 |

---

## 10. 验收与质量门

### 10.1 分阶段验收标准

| Phase | 交付物 | 验收命令 | 通过标准 | 证据回写 |
|---|---|---|---|---|
| Phase 1 | PluginDescriptor、PluginCatalog、errcode 头文件 | `cmake --build build-ci --target dasall_infra` | 编译无误 | build 日志 |
| Phase 2 | IPluginManager、IPluginPolicyGate 头文件 | `cmake --build build-ci --target dasall_infra && ctest -L unit -R "PluginManager\|PluginPolicyGate"` | unit 全绿 | ctest 结果摘要 |
| Phase 3 | CMakeLists.txt、test 注册 | `ctest --test-dir build-ci -N -L unit \| grep -c plugin` | 找到 ≥5 个 plugin 用例 | ctest -N 输出 |
| Phase 4 | ValidationPipeline、LifecycleManager、AuditAdapter 骨架 | `ctest --test-dir build-ci -L "unit\|contract" --output-on-failure -R "Plugin"` | 全部通过 | ctest 报告 |
| Phase 5 | 失败注入与 profile 兼容测试 | `ctest --test-dir build-ci -L "integration" --output-on-failure -R "PluginFailure\|ProfilePlugin"` | 全部通过 | ctest 报告 + coverage 评估 |

### 10.2 最终交付完成标准（All Gates Green）

仅当以下条件同时满足，方可标记为"完成"：

1. ✓ **接口冻结门（PLG-GATE-01）**：IPluginManager、IPluginPolicyGate 头文件落盘，方法名与详设 6.6 完全一致。
2. ✓ **contracts 边界门（PLG-GATE-02）**：contract 边界测试通过，无越权字段或语义漂移。
3. ✓ **构建门（PLG-GATE-03）**：`cmake --build build-ci --target dasall_infra` 成功。
4. ✓ **unit 发现门（PLG-GATE-04）**：`ctest -L unit` 能找到并执行 ≥5 个 plugin 相关测试。
5. ✓ **contract 执行门（PLG-GATE-05）**：`ctest -L contract` plugin 用例全绿。
6. ✓ **失败兜底门（PLG-GATE-06）**：每条关键失败路径（validate fail/load fail）都有日志、审计或 report 证据可验证。
7. ✓ **解阻协调门（PLG-GATE-07）**：若推进 PLG-TODO-014+ 系列，需 INF-BLK-09 已解除。
8. ✓ **breaking 评审门（PLG-GATE-08）**：若接口签名或 contracts 映射变更，需评审纪录。

---

## 11. 可行性结论

### 11.1 结论：**可进入部分执行，不可全面实施**

#### 可直接推进的范围

当前 plugin 详细设计满足 **L2 粒度（数据结构/接口级）** 要求，以下任务可直接执行：

1. **Phase 1-3**：基础对象冻结、核心接口定义、构建与测试注册（PLG-TODO-001~010）
   - 不涉及实现细节，仅定义接口与对象
   - 可与 INF-TODO-017（SecurityPolicyManager）并行推进
   - 预计 2-3 周完成

2. **Phase 4**：流程骨架与观测适配（PLG-TODO-011~013）
   - 提供状态机与审计适配器骨架，不进完整装载逻辑
   - 依赖 Phase 1-3 完成
   - 预计 2 周完成

#### 不可推进的范围（当前阻塞）

以下任务当前无法推进，必须先解除 **INF-BLK-09**：

1. **PLG-TODO-014**（PluginManifest 对象冻结）
   - **阻塞原因**：manifest 字段集合与扩展字段命名空间未冻结
   - **解阻条件**：plugin 设计评审需确定最终 v1.0 schema；与 tools/skill 侧协商防止语义重复
   - **估计解阻时间**：1-2 周（需跨模块评审）

2. **PLG-TODO-015+016**（签名与 ABI 规范）
   - **阻塞原因**：trust_store、链验规则、ABI 兼容矩阵三项关联，需同步冻结
   - **解阻条件**：涉及 security policy、platform 等多个组件，需一体化协调
   - **估计解阻时间**：2-3 周

3. **完整装载实现与运行时集成**
   - **阻塞原因**：PLG-TODO-014~016 须先完成；PluginRuntimeBridge 平台细节未定
   - **估计解阻时间**：3-4 周（依赖平台层交付）

### 11.2 当前最小可执行粒度

| 对象级别 | 可执行范围 | 例子 |
|---|---|---|
| **L2 数据结构** | ✓ 已具备 | PluginDescriptor、PluginCatalog、错误码枚举 |
| **L2 接口定义** | ✓ 已具备 | IPluginManager、IPluginPolicyGate 接口头文件 |
| **L2 流程骨架** | ✓ 已具备 | ValidationPipeline、LifecycleManager 状态机架构 |
| **L3 函数实现** | ✗ 不可 | load()、unload() 完整实现（依赖 L2 约束冻结） |
| **L3 条件分支** | ✗ 不可 | 验签失败降级、ABI 兼容回退等具体逻辑 |
| **集成与联调** | ✗ 不可 | plugin 与 runtime 协同、PluginRuntimeBridge 平台接入 |

### 11.3 与上游依赖的关系

| 上游组件 | 当前状态 | 对 plugin 的影响 | 预期 Ready 时间 |
|---|---|---|---|
| INF-TODO-017（SecurityPolicyManager） | Not Started | PLG-TODO-004 依赖此接口可用 | 1-2 周 |
| INF-TODO-016（AuditService） | Not Started | PLG-TODO-006 依赖此接口可用 | 1-2 周 |
| INF-BLK-09（manifest/ABI/signature 三项冻结） | Blocked | PLG-TODO-014/015/016/017 全部卡此 | 2-3 周 |
| platform PluginRuntimeBridge | 未启动 | PLG-TODO-011 后续装载实现取决此 | 3-4 周 |

### 11.4 下一步建议

#### 立即启动（无前置）

1. ✓ 推进 PLG-TODO-001~013（Phase 1-5）
2. ✓ 与 INF-TODO-017/016 并行推进，协调 SecurityPolicyManager 与 AuditService 接口提前冻结
3. ✓ 启动"PluginManifest Schema v1.0"补设计工作，与 tools 组件协商防止语义重复

#### 等待解阻（INF-BLK-09）后启动

1. ○ PLG-TODO-014（manifest 对象），需补设计评审
2. ○ PLG-TODO-015（签名规范），需补设计与安全评审
3. ○ PLG-TODO-016（ABI 兼容矩阵），需补设计与平台评审
4. ○ 完整装载实现与 runtime/platform 集成测试

#### 平行推进（不互相依赖）

1. ✓ 论证下阶段供应链规范与远程 plugin 能力（v2 演进侧）
2. ✓ 准备 sandbox/seccomp 沙箱能力预留（作为 PluginRuntimeBridge 扩展点）

---

## 12. 总体交付清单与里程碑

### 12.1 交付清单（按优先级）

| Priority | Deliverable | 涉及任务 | Ready 时间 | 风险 |
|---|---|---|---|---|
| P0-M1 | 基础对象与接口头文件（PluginDescriptor/Catalog/IPluginManager） | PLG-TODO-001~004、007 | Week 1 | 无 |
| P0-M1 | CMake 与单元测试注册 | PLG-TODO-008~010 | Week 2 | tests 顶层 integration 未接入 |
| P0-M2 | 校验与生命周期流程骨架 | PLG-TODO-005、011~013 | Week 3 | PluginRuntimeBridge 平台接口未定 |
| P0-M3 | manifest/ABI/signature 三项补设计 | PLG-TODO-014/015/016 | Week 3-4（解阻后） | INF-BLK-09 依赖外部评审 |
| P1-M4 | 完整装载与运行时集成 | 后续任务 | Week 5+ | 平台层交付与性能验证 |

### 12.2 关键里程碑定义

- **M1（Week 1-2）**：所有 L2 对象与接口冻结，ctest 发现 plugin 用例数 ≥5
- **M2（Week 2-3）**：Phase 4 完成，PluginValidationPipeline 与 PluginLifecycleManager 骨架可编译可测
- **M3（Week 3-4）**：manifest/ABI/signature 补设计完成，INF-BLK-09 解除，PLG-TODO-014~017 从 Blocked 转 Not Started
- **M4（Week 4-5）**：完整装载实现完成，unit/contract/failure injection/profile matrix 全部 gates 通过
- **M5（Week 5+）**：与 runtime/platform 集成联调完成，端到端 plugin 加载链路冒烟测试通过

---

## 13. 常见问题与澄清

### Q1：为什么"不进入完整装载实现"？

**A**：当前 INF-BLK-09 处于阻塞状态，manifest 字段、ABI 兼容规则、签名链路三项未同步冻结。直接实现 load() 会导致后续 breaking change 风险。遵循"先冻结再实现"的工程原则，避免返工。

### Q2：PluginRuntimeBridge 的缺失是否阻塞当前任务？

**A**：否。当前任务（PLG-TODO-001~013）仅涉及接口定义与骨架实现，用 mock bridge 可验证状态机逻辑。真实平台接入是后续里程碑 M4-5 的事。

### Q3：如何处理 infra 与 runtime 的界限？

**A**：严格遵循 ADR-008：
- **plugin 职责**：发现、校验、装载、卸载、输出治理结果
- **runtime 职责**：根据 LoadResult 决策、调度、故障恢复
- plugin 不拥有调度权，只是被 runtime 的"守门员"。

### Q4：为什么 PluginManifest 是 Blocked 的？

**A**：因系其与 PluginABI、SignatureReport 在概念上强耦合。为避免后续大改动，需三项同步冻结。这是 INF-BLK-09 的一部分，由 infra 子系统体系协调负责。

### Q5：test/integration 尚未接入，是否影响当前任务完成？

**A**：否。当前任务目标在于接口与对象冻结（unit/contract gates），不包含 integration 门禁的完整执行。PLG-GATE-06 可先用 unit/contract 降级验证，integration 在 M4 后期补齐。

---

## 14. 本轮执行记录（2026-04-01 / PLG-TODO-001）

### 14.1 选中任务

1. 本轮任务：PLG-TODO-001。
2. 可执行性依据：该任务无前置依赖，字段边界已由详细设计 6.5 与专项 TODO 的 PLG-C009 明确冻结，且可在单轮内完成“头文件 + 定向 unit/contract 证据 + 文档回写”闭环。

### 14.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5 冻结 PluginDescriptor 六个治理字段：plugin_id、version、abi、trust_level、status、source。
2. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.5 明确 PluginDescriptor 不反向扩写 Tool/Skill 契约。
3. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 中 PLG-C009 与 PLG-TODO-001 明确本轮只做对象冻结与 unknown 兜底，不进入 manifest、签名或 lifecycle 细节。
4. infra/include/InfraContext.h、infra/include/policy/PolicyDecisionRef.h、infra/include/ota/OTATypes.h 已提供当前仓库的数据结构冻结风格参考：header-only、显式默认值、最小一致性检查出口。

外部参考：

1. Microsoft MSDN Magazine《Writing, Loading, and Accessing Plug-Ins》强调插件体系应先冻结最小稳定的元数据/接口面，再逐步进入加载与隔离细节。本轮据此保持 PluginDescriptor 为纯治理元数据对象，不提前引入装载句柄、入口符号或业务能力描述。

D 结论：

1. 新增 docs/todos/infrastructure/PLG-TODO-001-PluginDescriptor设计收敛.md，明确 PluginTrustLevel、PluginStatus、unknown 归一化与 Design->Build 映射。
2. Build 三件套锁定为：
        - 代码目标：infra/include/plugin/PluginDescriptor.h。
        - 测试目标：PluginDescriptorFieldTest、PluginDescriptorBoundaryContractTest。
        - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_descriptor_unit_test dasall_contract_plugin_descriptor_boundary_test && ctest --test-dir build-ci -N -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest"。
3. D Gate：PASS。

### 14.3 Build 交付与证据

交付物：

1. infra/include/plugin/PluginDescriptor.h：冻结 plugin_id/version/abi/source 四个字符串治理字段及 trust_level/status 两个 plugin 私有枚举，新增 normalize()、uses_unknown_defaults() 与 is_governance_ready()。
2. tests/unit/infra/plugin/PluginDescriptorTest.cpp：覆盖默认 unknown、空值归一化负例和完整字段正例。
3. tests/contract/smoke/PluginDescriptorBoundaryContractTest.cpp：覆盖“不扩写 request_id/trace_id/task_id”等 contracts 标识语义的边界约束。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件与定向测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_descriptor_unit_test dasall_contract_plugin_descriptor_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest"`：通过，发现 2 个测试。
4. `ctest --test-dir build-ci --output-on-failure -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：新增对象和枚举命名已直接表达治理语义，无需额外冗余注释。
2. 正负例覆盖：unit 覆盖默认态正例、完整字段正例与空值归一化负例；contract 覆盖 contracts 标识语义未越权扩写的负例守卫。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/contract 用例进入 CTest 图。
4. TODO 证据回写：已完成任务状态、验收命令与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 PluginDescriptor 对象、测试、CMake 注册、设计收敛文档与专项 TODO/工作日志证据。

阻塞修复：

1. 原任务行的验收命令只构建 `dasall_infra`，无法保证 CTest 执行前生成新增测试可执行文件；本轮已将验收命令修正为显式构建对应 unit/contract 目标，作为最小 validation blocker fix。

---

## 15. 本轮执行记录（2026-04-01 / PLG-TODO-002）

### 15.1 选中任务

1. 本轮任务：PLG-TODO-002。
2. 可执行性依据：PLG-TODO-001 已完成，PluginCatalog 的 discovered/rejected 双集合边界在详细设计 6.5/6.3 中已冻结，且本轮可在单次提交内完成“对象头文件 + 定向 unit/contract + 文档证据”闭环。

### 15.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5 明确 PluginCatalog 固定为 discovered_plugins[] 与 rejected_plugins[]，并要求“发现与拒绝原因必须可解释”。
2. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.3/6.7 明确 catalog 只表达 discovery result，不含激活结论。
3. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 中 PLG-TODO-002 明确 rejected_plugins 需与 Observation evidence_ref 引用语义对齐。
4. infra/include/policy/PolicyDecisionRef.h 与 infra/include/diagnostics/DiagnosticsTypes.h 已提供 reason_code/evidence_ref 的现有 infra 侧引用模式，可直接复用为 rejected_plugins 的最小追踪锚点。

外部参考：

1. Microsoft MSDN Magazine《Writing, Loading, and Accessing Plug-Ins》将插件管理器描述为“扫描目录、收集候选插件并维护结果列表”的独立阶段。本轮据此把 PluginCatalog 收敛为 discovery result 对象，不引入 load handle、执行结果或 runtime 调度语义。

D 结论：

1. 新增 docs/todos/infrastructure/PLG-TODO-002-PluginCatalog设计收敛.md，冻结 RejectedPluginRecord、evidence_ref 对齐策略与 Design->Build 映射。
2. Build 三件套锁定为：
        - 代码目标：infra/include/plugin/PluginCatalog.h。
        - 测试目标：PluginCatalogTest、PluginCatalogBoundaryContractTest。
        - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_catalog_unit_test dasall_contract_plugin_catalog_boundary_test && ctest --test-dir build-ci -N -R "PluginCatalogTest|PluginCatalogBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginCatalogTest|PluginCatalogBoundaryContractTest"。
3. D Gate：PASS。

### 15.3 Build 交付与证据

交付物：

1. infra/include/plugin/PluginCatalog.h：新增 PluginCatalog 与 RejectedPluginRecord，冻结 discovered_plugins/rejected_plugins 两个结果集合、reason_code/evidence_ref 锚点，以及 empty()/has_traceable_rejections()/has_consistent_entries() 三个判定出口。
2. tests/unit/infra/plugin/PluginCatalogTest.cpp：覆盖空 catalog、全发现、全拒绝和不带 evidence_ref/重复 plugin_id 的负例。
3. tests/contract/smoke/PluginCatalogBoundaryContractTest.cpp：覆盖 rejected_plugins 只复用 reason_code/evidence_ref，不直接持有 Observation/ErrorInfo 的边界约束。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件与定向测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_catalog_unit_test dasall_contract_plugin_catalog_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "PluginCatalogTest|PluginCatalogBoundaryContractTest"`：通过，发现 2 个测试。
4. `ctest --test-dir build-ci --output-on-failure -R "PluginCatalogTest|PluginCatalogBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：新增对象和方法命名已直接表达 discovery/result/evidence 语义，无需额外冗余注释。
2. 正负例覆盖：unit 覆盖空 catalog、全发现、全拒绝正例，以及 evidence_ref 缺失和 plugin_id 重复负例；contract 覆盖 Observation/ErrorInfo ownership 禁区。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/contract 用例进入 CTest 图。
4. TODO 证据回写：已完成任务状态、验收命令与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 PluginCatalog 对象、测试、CMake 注册、设计收敛文档与专项 TODO/工作日志证据。

阻塞修复：

1. 原任务行的验收命令未显式构建新增测试目标，无法稳定支撑 ctest 执行；本轮已将验收命令修正为显式构建对应 unit/contract 目标，作为最小 validation blocker fix。

---

## 16. 本轮执行记录（2026-04-01 / PLG-TODO-007）

### 16.1 选中任务

1. 本轮任务：PLG-TODO-007。
2. 可执行性依据：该任务无代码前置依赖，目标限定在 plugin 私有错误码枚举与映射规则；唯一风险是“六个 INF_E_PLUGIN_*”在原始设计中未显式完整列名，属于可在本轮内通过设计收敛修复的 context blocker。

### 16.2 Blocker 修复与 Design 结论

阻塞证据：

1. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 声称“六个 INF_E_PLUGIN_* 错误码已列”，但 docs/architecture/DASALL_infra_plugin模块详细设计.md 6.6 只显式给出 `INF_E_PLUGIN_VALIDATE_FAIL` 与 `INF_E_PLUGIN_LOAD_FAIL` 两个名字。
2. 同一详细设计 6.8/9.1 虽已冻结策略拒绝、签名失败、兼容失败、卸载失败等失败类别，但未把这四类失败收敛成显式 `INF_E_PLUGIN_*` 名称，导致 007 无法直接满足“六个私有错误码均可追溯到详设”的完成判定。

最小 blocker-fix：

1. 新增 docs/todos/infrastructure/PLG-TODO-007-PluginErrorCode设计收敛.md，把 6.6 的 validate/load 锚点与 6.8/9.1 的失败类别收敛为六个冻结码名：`INF_E_PLUGIN_VALIDATE_FAIL`、`INF_E_PLUGIN_POLICY_DENIED`、`INF_E_PLUGIN_SIGNATURE_FAIL`、`INF_E_PLUGIN_COMPATIBILITY_FAIL`、`INF_E_PLUGIN_LOAD_FAIL`、`INF_E_PLUGIN_UNLOAD_FAIL`。

D 结论：

1. PluginErrorCode 采用 plugin 私有 header-only 错误码定义，不把实现细节提前耦合进 InfraErrorCode 顶层枚举。
2. 映射规则只允许落入 contracts 已冻结的 Validation / Policy / Runtime 三类 ResultCode，不新增共享错误语义。
3. Build 三件套锁定为：
        - 代码目标：infra/include/plugin/PluginErrorCode.h。
        - 测试目标：PluginErrorCodeTest、PluginErrorCodeBoundaryContractTest。
        - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_error_code_unit_test dasall_contract_plugin_error_code_boundary_test && ctest --test-dir build-ci -N -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest"。
4. D Gate：PASS。

### 16.3 Build 交付与证据

交付物：

1. infra/include/plugin/PluginErrorCode.h：新增 PluginErrorCode、PluginErrorMapping、plugin_error_code_name() 与 map_plugin_error_code()，冻结六个 plugin 私有错误码及其一级 contracts 映射。
2. tests/unit/infra/plugin/PluginErrorCodeTest.cpp：覆盖六个名字稳定性与 Validation/Policy/Runtime 映射。
3. tests/contract/smoke/PluginErrorCodeBoundaryContractTest.cpp：覆盖“只映射到现有 contracts ResultCode”和“名字保持在 `INF_E_PLUGIN_*` 私有命名空间”的边界约束。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件与定向测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_error_code_unit_test dasall_contract_plugin_error_code_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest"`：通过，发现 2 个测试。
4. `ctest --test-dir build-ci --output-on-failure -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：新增 enum 和映射函数命名已直接表达阶段与失败域语义，无需额外冗余注释。
2. 正负例覆盖：unit 覆盖名字稳定性和一级映射正例；contract 覆盖共享 ResultCode 不越权与命名空间边界守卫。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/contract 用例进入 CTest 图。
4. TODO 证据回写：已完成 blocker 说明、任务状态、验收命令与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 PluginErrorCode 对象、测试、CMake 注册、设计收敛文档与专项 TODO/工作日志证据。

阻塞修复：

1. 原任务的设计锚点未显式完整列出六个 `INF_E_PLUGIN_*` 名字；本轮先通过设计收敛文档完成最小 blocker-fix，再落盘代码与测试。
2. 原任务行的验收命令未显式构建新增测试目标，无法稳定支撑 ctest 执行；本轮已同步修正为显式构建对应 unit/contract 目标。

---

## 17. 本轮执行记录（2026-04-01 / PLG-TODO-003）

### 17.1 选中任务

1. 本轮任务：PLG-TODO-003。
2. 可执行性依据：PLG-TODO-001/002 已完成，IPluginManager 的五个入口在详细设计 6.6 中已冻结到接口级；缺口集中在 validate/load/unload/list_active 的最小 request/result 边界，可在本轮内修复后继续落盘。

### 17.2 Blocker 修复与 Design 结论

阻塞证据：

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.6 给出了 IPluginManager 五个方法名，但没有把 ValidationResult、LoadOptions、UnloadResult、ActivePluginSet 落到对象级字段表，直接实现会把接口边界暗含进实现层。
2. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6 与 plugin 模块详细设计 6.6 在 discover/profile 和 load/load_options 两处存在签名粒度不一致，需要先做同轮收敛，避免后续 breaking。
3. validate 语义仍受 INF-BLK-09 的 Manifest/Signature/Compatibility 对象阻塞影响，不能在本轮伪造完整 Manifest 或 Report 对象。

最小 blocker-fix：

1. 新增 docs/todos/infrastructure/PLG-TODO-003-IPluginManager设计收敛.md，把 request/result 缺口收敛为 PluginValidationRequest、PluginLoadOptions、PluginValidationResult、PluginLoadResult、PluginUnloadResult、ActivePluginSet 六个最小边界对象。
2. 对仍未解阻的 Manifest/SignatureReport/CompatibilityReport 不定义完整对象，只保留 manifest_ref、signature_report_ref、compatibility_report_ref 三个 ref 锚点。
3. discover 统一冻结为 discover(profile_id)，load 统一冻结为 load(plugin_id, load_options)，以对齐 plugin 模块详细设计 6.3/6.6 与 6.9 配置项中的 profile/load_timeout/audit 约束。

D 结论：

1. IPluginManager 在 infra/include/plugin/IPluginManager.h 中冻结五个治理入口，discover 返回既有 PluginCatalog，list_active 返回只含 PluginDescriptor 集合的 ActivePluginSet。
2. validate 通过 PluginValidationResult 聚合 PolicyDecisionRef 与 signature/compatibility 的 ref，不越界到被阻塞的完整报告对象。
3. load/unload 只冻结 phase/result/evidence/handle_ref 级别的最小输出；PluginManager.cpp 仅提供 not-implemented skeleton，不进入 lifecycle/runtime bridge 实现。
4. Build 三件套锁定为：
        - 代码目标：infra/include/plugin/IPluginManager.h、infra/src/plugin/PluginManager.cpp。
        - 测试目标：PluginManagerInterfaceCompileTest、PluginManagerBoundaryContractTest。
        - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_manager_interface_unit_test dasall_contract_plugin_manager_boundary_test && ctest --test-dir build-ci -N -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest"。
5. D Gate：PASS。

### 17.3 Build 交付与证据

交付物：

1. infra/include/plugin/IPluginManager.h：新增 IPluginManager 与六个最小边界对象，冻结 discover/validate/load/unload/list_active 五个治理入口。
2. infra/src/plugin/PluginManager.cpp：新增 not-implemented skeleton，实现五个入口的最小失败兜底，不进入 runtime bridge 或 lifecycle 细节。
3. tests/unit/infra/plugin/PluginManagerInterfaceTest.cpp：覆盖接口签名、request/result 类型、成功路径与默认占位请求失败路径。
4. tests/contract/smoke/PluginManagerBoundaryContractTest.cpp：覆盖 ResultCode/ErrorInfo 不越权，以及 signature/compatibility 仅以 ref 暴露的边界约束。
5. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成 header/source 与定向 unit/contract 测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_manager_interface_unit_test dasall_contract_plugin_manager_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest"`：通过，发现 2 个测试。
4. `ctest --test-dir build-ci --output-on-failure -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：接口、request/result 和 skeleton 命名已直接表达治理阶段与边界语义，无需额外冗余注释。
2. 正负例覆盖：unit 覆盖接口签名与成功/失败路径；contract 覆盖 ResultCode/ErrorInfo 类型边界及 report 仅以 ref 暴露。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/contract 用例进入 CTest 图。
4. TODO 证据回写：已完成 blocker 说明、任务状态、验收命令与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 IPluginManager 接口、最小边界对象、skeleton、定向测试、设计收敛文档与专项 TODO/工作日志证据。

阻塞修复：

1. 原任务缺少 validate/load/unload/list_active 的最小 request/result 对象；本轮先补接口边界收敛文档与同头文件最小对象，再落接口与测试。
2. 设计文档在 discover/profile、load/load_options 上存在签名粒度差异；本轮已统一到 profile-aware discover 与 load_options-aware load。
3. 原任务行的验收命令只构建 `dasall_infra`，无法保证新增测试目标可执行；本轮已同步修正为显式构建 unit/contract 目标。

---

## 18. 本轮执行记录（2026-04-01 / PLG-TODO-004）

### 18.1 选中任务

1. 本轮任务：PLG-TODO-004。
2. 可执行性依据：INF-TODO-017 已完成，PolicySnapshot/PolicyDecisionRef/ISecurityPolicyManager 边界均已冻结；当前唯一缺口是 manifest 仍被 INF-BLK-09 阻塞，但可以在本轮内通过最小 request 收敛修复。

### 18.2 Blocker 修复与 Design 结论

阻塞证据：

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.6 将 IPluginPolicyGate 描述为 evaluate(manifest, policy_snapshot, profile)，但 PluginManifest 仍处于 INF-BLK-09 阻塞链上，不能在本轮伪造完整对象。
2. 同一设计 6.3 说明 PluginPolicyGate 的真实输入语义是 PolicySnapshot、allowlist 和 trust policy；当前这些细节并没有独立公共对象，若强拆会和已冻结的 PolicySnapshot 边界重复。
3. PLG-TODO-004 本身是 L2 接口任务，要求的是接口冻结和编译/边界验证，而不是策略引擎实现。

最小 blocker-fix：

1. 新增 docs/todos/infrastructure/PLG-TODO-004-IPluginPolicyGate设计收敛.md，定义 PluginPolicyRequest，以已冻结的 PluginDescriptor 承接治理字段，并用 manifest_ref/profile_id 两个 ref 锚点承接未解阻的 manifest 与 profile 细节。
2. evaluate 统一冻结为 evaluate(request, policy_snapshot)，输出继续复用已冻结的 PolicyDecisionRef，不新增 plugin 私有决策对象。

D 结论：

1. IPluginPolicyGate 只冻结一个 evaluate 入口，职责限定为插件准入判定，不承担策略快照获取、patch、rollback 或 runtime 恢复。
2. PluginPolicyRequest 只暴露 descriptor、manifest_ref、profile_id 三个边界字段，不持有完整 PluginManifest、PolicyBundle 或 ErrorInfo。
3. Build 三件套锁定为：
        - 代码目标：infra/include/plugin/IPluginPolicyGate.h。
        - 测试目标：PluginPolicyGateInterfaceCompileTest、PluginPolicyGateBoundaryContractTest。
        - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_policy_gate_interface_unit_test dasall_contract_plugin_policy_gate_boundary_test && ctest --test-dir build-ci -N -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest"。
4. D Gate：PASS。

### 18.3 Build 交付与证据

交付物：

1. infra/include/plugin/IPluginPolicyGate.h：新增 IPluginPolicyGate 与 PluginPolicyRequest，冻结策略判定入口及最小输入边界。
2. tests/unit/infra/plugin/PluginPolicyGateInterfaceTest.cpp：覆盖 evaluate 签名、有效输入 allow 决策与无效输入 deny 决策。
3. tests/contract/smoke/PluginPolicyGateBoundaryContractTest.cpp：覆盖 request 仅持有 PluginDescriptor + ref，不拥有 blocked manifest 或 policy bundle 对象的边界约束。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成 public header 与定向 unit/contract 测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_policy_gate_interface_unit_test dasall_contract_plugin_policy_gate_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest"`：通过，发现 2 个测试。
4. `ctest --test-dir build-ci --output-on-failure -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：接口和 request 命名已直接表达准入判定边界，无需额外冗余注释。
2. 正负例覆盖：unit 覆盖 evaluate 签名与 allow/deny 两条输入路径；contract 覆盖 request 不越权拥有 blocked manifest/policy 对象。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/contract 用例进入 CTest 图。
4. TODO 证据回写：已完成 blocker 说明、任务状态、验收命令与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 IPluginPolicyGate 接口、PluginPolicyRequest、定向 unit/contract 测试、设计收敛文档与专项 TODO/工作日志证据。

阻塞修复：

1. 原任务直接依赖未冻结的 PluginManifest；本轮先用 PluginPolicyRequest 的 manifest_ref/profile_id 收敛接口边界，再冻结 evaluate 入口。
2. allowlist/trust policy 当前没有独立对象；本轮保持它们继续内聚在已冻结的 PolicySnapshot 语义内，不额外拆散边界。
3. 原任务行的验收命令只构建 `dasall_infra`，无法保证新增测试目标可执行；本轮已同步修正为显式构建 unit/contract 目标。

---

## 19. 本轮执行记录（2026-04-07 / PLG-TODO-008）

### 19.1 选中任务

1. 本轮任务：PLG-TODO-008。
2. 可执行性依据：PLG-TODO-001/002/003/004/007 已完成，Phase 7.1 与关键路径明确把 008 放在 Phase 3 的起点；当前缺口仅剩 plugin 构建入口尚未按组件级列表收口，且可在单轮内完成“CMake 接线 + 定向 build 验证 + TODO/工作日志回写”闭环。

### 19.2 研究与 Design 结论

本地证据：

1. infra/CMakeLists.txt 已接入 src/plugin/PluginManager.cpp 与五个 plugin 公共头，但它们散落在 DASALL_INFRA_CORE_SOURCES / DASALL_INFRA_PUBLIC_HEADERS 中，尚未形成 plugin 专属构建入口。
2. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 的 Phase 7.1、关键路径和 Gate 03 都表明 008 属于 Phase 3 构建接线任务；但任务表中 008/009 的前置依赖写成了“PLG-TODO-001 至 007 / 005”，与阶段划分不一致，会阻塞后续自动选择。
3. dasall_infra 当前通过 target_sources + PUBLIC_HEADER 聚合源与公开头，说明 008 的正确修复方式是“组件级列表收口”，而不是新增新的子库或导出机制。

外部参考：

1. CMake 官方 target_sources 文档说明，同一 target 的 sources 可按作用域和调用顺序分组追加；本轮据此把 plugin 源与公开头收敛到独立变量，再统一挂接到 dasall_infra。
2. CMake 官方 add_subdirectory 文档说明，子目录会立即并入当前构建图；本轮据此把 009 的子目录注册收口保留为下一轮，不在 008 中提前扩张。

D 结论：

1. 新增 docs/todos/infrastructure/deliverables/PLG-TODO-008-plugin构建入口接线收敛.md，冻结 plugin 构建入口的收口方式、最小 blocker-fix 与 Design->Build 映射。
2. Build 三件套锁定为：
        - 代码目标：infra/CMakeLists.txt。
        - 测试目标：dasall_infra 构建成功，且 infra/CMakeLists.txt 可检索到 plugin 专属入口变量。
        - 验收命令：cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra && rg -n "DASALL_INFRA_PLUGIN_(SOURCES|PUBLIC_HEADERS)" infra/CMakeLists.txt。
3. D Gate：PASS。

### 19.3 Build 交付与证据

交付物：

1. infra/CMakeLists.txt：新增 DASALL_INFRA_PLUGIN_SOURCES 与 DASALL_INFRA_PLUGIN_PUBLIC_HEADERS，并让 DASALL_INFRA_CORE_SOURCES / DASALL_INFRA_PUBLIC_HEADERS 引用这两个组件级列表。
2. docs/todos/infrastructure/deliverables/PLG-TODO-008-plugin构建入口接线收敛.md：记录输入依据、外部参考、依赖元数据 blocker 修复与 Build 合规复核。
3. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md：将 PLG-TODO-008 回写为 Done，并同步修正 008/009/010 的映射表与前置依赖元数据，使其与 Phase 3 顺序一致。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra`：通过。
3. `rg -n "DASALL_INFRA_PLUGIN_(SOURCES|PUBLIC_HEADERS)" infra/CMakeLists.txt`：通过，plugin 构建入口变量已可检索。

Build 合规复核：

1. 代码注释：本轮只做 CMake 变量收口，变量命名已直接表达 plugin 组件边界，无需新增冗余注释。
2. 正负例覆盖：008 属构建接线任务，二值出口是 build 成功与入口变量存在；负向守卫体现在 TODO 元数据 blocker 修复，避免后续轮次误判依赖未满足。
3. 测试发现性：本轮不触碰 unit/contract 注册，discoverability 将在 009/010 单独验证，不混入同一提交。
4. TODO 证据回写：已完成任务状态、依赖元数据、交付物路径与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 plugin 构建入口收口、设计收敛文档、专项 TODO 与工作日志证据。

阻塞修复：

1. 任务表把 008/009 的前置依赖写得过宽，混入了尚未开始的 005/006；本轮已按 Phase 7.1 与关键路径把 008/009/010 的依赖元数据收敛到真实串行顺序。
2. 当前仓库虽已接入 plugin 文件，但仍未形成组件级 build 入口；本轮通过新增 plugin 专属 source/header 列表完成最小构建收口，而未扩张到 tests 或 pipeline。

---

## 20. 本轮执行记录（2026-04-07 / PLG-TODO-009）

### 20.1 选中任务

1. 本轮任务：PLG-TODO-009。
2. 可执行性依据：PLG-TODO-008 已完成，plugin 测试源码也已在 tests/unit/infra/plugin 落盘；当前剩余缺口仅是 unit 注册入口仍停留在父级 CMakeLists 中，可在单轮内完成“子目录入口收口 + discoverability 验证 + TODO/工作日志回写”。

### 20.2 研究与 Design 结论

本地证据：

1. tests/unit/infra/plugin 目录已存在五个 plugin 单测源文件，说明 009 的问题不是缺测试，而是缺少子目录级注册入口。
2. tests/unit/infra/CMakeLists.txt 当前直接定义五个 plugin unit executable，与 health/watchdog 等其他组件混排，不利于后续组件级维护。
3. tests/unit/CMakeLists.txt 直接硬编码五个 plugin target 名称，顶层聚合尚未真正消费组件级输出列表。

外部参考：

1. CMake 官方 add_subdirectory 文档说明，子目录会立即纳入当前构建图；本轮据此把 plugin unit 注册逻辑收敛到 tests/unit/infra/plugin/CMakeLists.txt。
2. CMake 官方 add_test / set_tests_properties 文档说明，测试应在创建目录内注册并设置标签；本轮据此统一赋予 plugin unit 用例 `unit;plugin` 标签，保证 discoverability 与组件过滤一致。

D 结论：

1. 新增 docs/todos/infrastructure/deliverables/PLG-TODO-009-plugin单元测试入口注册收敛.md，冻结子目录入口、顶层聚合列表和 discoverability 验证链路。
2. Build 三件套锁定为：
        - 代码目标：tests/unit/infra/plugin/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt。
        - 测试目标：五个 plugin unit 用例可被 unit 视图发现，且 plugin 标签子集可直接执行。
        - 验收命令：cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_plugin_descriptor_unit_test dasall_plugin_catalog_unit_test dasall_plugin_error_code_unit_test dasall_plugin_manager_interface_unit_test dasall_plugin_policy_gate_interface_unit_test && ctest --test-dir build-ci -N -L unit | grep -i plugin && ctest --test-dir build-ci --output-on-failure -L plugin。
3. D Gate：PASS。

### 20.3 Build 交付与证据

交付物：

1. tests/unit/infra/plugin/CMakeLists.txt：新增 dasall_register_plugin_unit_test(...) helper，统一五个 plugin unit 用例的 add_executable / add_test / labels。
2. tests/unit/infra/CMakeLists.txt：移除父级内联的 plugin unit 注册片段，改为 add_subdirectory(plugin) 并向上导出 DASALL_PLUGIN_UNIT_TEST_EXECUTABLE_TARGETS。
3. tests/unit/CMakeLists.txt：改为消费 ${DASALL_PLUGIN_UNIT_TEST_EXECUTABLE_TARGETS}，不再硬编码五个 plugin unit target。
4. docs/todos/infrastructure/deliverables/PLG-TODO-009-plugin单元测试入口注册收敛.md：记录输入依据、外部参考、Design->Build 映射与 Build 合规复核。
5. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md：将 PLG-TODO-009 回写为 Done，并补充本轮执行记录。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_plugin_descriptor_unit_test dasall_plugin_catalog_unit_test dasall_plugin_error_code_unit_test dasall_plugin_manager_interface_unit_test dasall_plugin_policy_gate_interface_unit_test`：通过。
3. `ctest --test-dir build-ci -N -L unit | grep -i plugin`：通过，可发现 5 个 plugin 单测入口。
4. `ctest --test-dir build-ci --output-on-failure -L plugin`：通过，5/5 tests passed。

Build 合规复核：

1. 代码注释：本轮只做 CMake 注册入口收口，helper 与列表命名已直接表达 plugin unit 归属，无需新增冗余注释。
2. 正负例覆盖：复用既有五个 plugin unit 测试的正负例覆盖，本轮不重写测试断言。
3. 测试发现性：已同时验证 `ctest -N -L unit` 的发现性和 `ctest -L plugin` 的可执行性。
4. TODO 证据回写：已完成任务状态、交付物路径与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 plugin unit 注册入口收口、设计收敛文档、专项 TODO 与工作日志证据。

阻塞修复：

1. 当前仓库已有 plugin unit 用例，但注册入口仍留在父级 CMakeLists，导致 009 的“目录与入口注册”目标未闭环；本轮通过新增子目录 CMake 完成最小收口，而不改测试语义。
2. 顶层 unit 聚合此前直接硬编码五个 plugin target 名称；本轮改为消费组件级列表，避免后续新增 plugin 测试时重复在多处手工补点。

---

## 本文档历史与评审

| 版本 | 日期 | 变更说明 | 评审人 |
|---|---|---|---|
| v1.0 | 2026-03-25 | 初稿：完整 TODO 框架、粒度评估、映射与拆解、阻塞管理 | （待评审） |
| v1.1 | 2026-04-01 | 回写 PLG-TODO-001 设计/构建证据，修正该任务验收命令中的测试目标构建缺口 | （待评审） |
| v1.2 | 2026-04-01 | 回写 PLG-TODO-002 设计/构建证据，修正该任务验收命令中的测试目标构建缺口 | （待评审） |
| v1.3 | 2026-04-01 | 回写 PLG-TODO-007 的 blocker 修复、设计/构建证据，并修正该任务验收命令中的测试目标构建缺口 | （待评审） |
| v1.4 | 2026-04-01 | 回写 PLG-TODO-003 的接口边界 blocker 修复、设计/构建证据，并修正该任务验收命令中的测试目标构建缺口 | （待评审） |
| v1.5 | 2026-04-01 | 回写 PLG-TODO-004 的 manifest 输入边界 blocker 修复、设计/构建证据，并修正该任务验收命令中的测试目标构建缺口 | （待评审） |
| v1.6 | 2026-04-07 | 回写 PLG-TODO-008 的构建入口收口与验证证据，并修正 008/009/010 在映射表与前置依赖中的元数据错位 | （待评审） |
| v1.7 | 2026-04-07 | 回写 PLG-TODO-009 的 unit 注册入口收口与 discoverability 证据，使顶层 unit 聚合改为消费 plugin 组件级测试列表 | （待评审） |

---

## 附录：与 infra 子系统 TODO 的对齐关系

### A1. INF-TODO-019 与此文档的对应关系

| infra 子系统 TODO | plugin 专项 TODO | 对应关系 |
|---|---|---|
| INF-TODO-019（开始） | PLG-TODO（全系列） | INF-TODO-019 是 infra 体系统筹，PLG-TODO 是具体拆解 |
| INF-BLK-09（阻塞） | PLG-BLK-01/02/03/05 | 同一根本原因（manifest/ABI/signature 未冻结）的衍生阻塞 |
| INF-TODO-017（SecurityPolicyManager） | PLG-TODO-004、PLG-TODO-005 | 依赖关系 |
| INF-TODO-016（AuditService） | PLG-TODO-006、PLG-TODO-012 | 依赖关系 |

### A2. 如何响应"请立即推进 PLG-TODO"的需求

当用户要求启动 plugin 专项 TODO 执行时，应：

1. **首先确认**当前状态是否允许开始（参见 4.1 当前可执行范围）
2. **确保前置**任务（INF-TODO-017、016）已至少进 Not Started 状态
3. **优先级**：Phase 1-3 > Phase 4 > 解阻项
4. **并行建议**：Phase 1 三个对象可并行；Phase 2-3 可并行（以 Phase 1 完成为前置）
5. **每个任务完成标准**参见对应任务行的"完成判定"列

---

本文档作为 DASALL 项目正式的 plugin 组件专项 TODO，自此版本起，所有相关任务执行必须遵循此文档定义的目标、范围、粒度、阻塞与门禁条件。

若有变更需求，必须通过正式评审流程并更新此文档版本记录。
