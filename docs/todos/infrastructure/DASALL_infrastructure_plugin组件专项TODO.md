# DASALL infra/plugin 组件专项 TODO

最近更新时间：2026-04-08  
阶段：Detailed Design -> Special TODO  
适用范围：infra/plugin（infra/src/plugin、infra/include/plugin）  
当前结论：**014~017 已全部完成，最细稳定边界已落到 L2（数据结构/接口级）；下一阶段转向 platform + plugin 联合冻结 PluginRuntimeBridge v1 最小契约，再进入真实装载链收口**

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
9. 当前仓库代码现状：infra/src/plugin、infra/include/plugin、tests/unit/infra/plugin 与 tests/contract/plugin 已建立；014~017 已完成，shared report 与 validation aggregation 边界已落盘

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
| infra/src/plugin 子目录 | 已建立并接入 dasall_infra 构建 | High | Done |
| infra/include/plugin 接口头文件 | 已建立基础接口与对象头文件集合 | High | Done |
| PluginDescriptor/Manifest/Catalog 对象 | Descriptor/Catalog/Manifest/Reports 已落盘 | High | Done |
| IPluginManager 接口 | 已落盘最小 request/result 边界与 skeleton | High | Done |
| PluginManifest schema_version 与字段冻结 | 已由详细设计冻结并在 PluginManifest.h 落盘 | High | Done |
| ABI 兼容矩阵规则 | 已冻结规则并在 IPluginCompatibilityEngine.h 落盘 host ABI / matrix boundary | High | Done |
| 签名校验链路规范 | 已冻结规则并在 IPluginSignatureVerifier.h 落盘最小 verifier boundary | High | Done |
| Plugin 单元测试基线 | 已建立 tests/unit/infra/plugin 并完成组件级注册 | Medium | Done |
| PluginValidationPipeline 实现 | 三检骨架与 shared report aggregation 已落盘 | Medium | Done |
| PluginAuditAdapter 实现 | 高风险动作审计适配已落盘 | Medium | Done |

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
6. ✓ **PluginManifest 关键对象冻结已完成**：schema_version、`required_abi`、capabilities 与扩展命名空间已在 public header 与 unit/contract 中落盘。
7. ✓ **签名校验规范与 verifier boundary 已冻结**：trust_store 信任锚、允许算法、chain_status、来源 trust level 次序与最小输入输出对象已明确。
8. ✓ **ABI 兼容矩阵与 compatibility boundary 已冻结**：platform tag、strict/non-strict 规则、host ABI snapshot 与最小 report 输入输出已明确。
9. ✗ **缺失 PluginRuntimeBridge 入口**：platform 层交互接口约定不完整。

### 4.2 粒度可行性分级表

| 设计对象 | 设计锚点 | 粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| PluginDescriptor | 详设 6.5 | L2 | 字段清单、必填约束、治理状态机、contracts 映射 | 序列化格式 | 直接拆数据结构任务 |
| PluginManifest | 详设 6.5；PLG-TODO-014 | L2 | schema_version、required_abi、capabilities、扩展命名空间及 unit/contract 守卫已落盘 | parser/serialization 中间格式 | 对象冻结已完成；若后续需要 parser，另拆原子任务 |
| PluginCatalog | 详设 6.3、6.5 | L2 | 结构字段、rejected_plugins 与拒绝原因链接 | 大小限制、导出限制 | 直接拆数据结构任务 |
| SignatureReport | 详设 6.5、6.8；PLG-TODO-017 | L2 | shared report header、chain_status 词典、algorithm allow-list 与 manager/pipeline optional aggregation 已落盘 | 外部导出/序列化格式 | 对象冻结已完成；若后续需要 export schema，另拆原子任务 |
| CompatibilityReport | 详设 6.5、6.8；PLG-TODO-017 | L2 | shared report header、abi/api/dependency 三检语义与 manager/pipeline optional aggregation 已落盘 | 外部导出/序列化格式 | 对象冻结已完成；若后续需要 export schema，另拆原子任务 |
| IPluginManager | 详设 6.6；infra 系统 6.6 | L2 | 方法名、输入输出对象、主流程调用关系 | 内部错误重试策略、timeout 约定、exception 映射 | 直接拆接口级任务，实现保持 skeleton |
| IPluginPolicyGate | 详设 6.3、6.6 | L2 | 方法名、输入（PolicySnapshot、allowlist）、输出 | 裁定优先级冲突时的详细规则 | 直接拆接口级任务，依赖 INF-TODO-017 |
| IPluginSignatureVerifier | 详设 6.3、6.6；PLG-TODO-015 | L2 | verify(request)->SignatureReport、trust anchor 输入面、chain_status 词典、shared report 头复用及 unit/contract 守卫已落盘 | 真实验签实现与外部 trust store 读取链 | 接口冻结已完成；后续只允许在保持签名不变前提下接实现 |
| IPluginCompatibilityEngine | 详设 6.3、6.6；PLG-TODO-016 | L2 | check(request)->CompatibilityReport、platform tag allow-list、strict/non-strict matrix、shared report 头复用与 host/dependency snapshot 守卫已落盘 | 真实 host ABI 识别与外部依赖解析实现 | 接口冻结已完成；后续只允许在保持签名不变前提下接实现 |
| PluginValidationPipeline | 详设 6.3、6.7/6.8 | L2 | 触发时机、三检顺序、失败枝条、输出聚合对象 | 并行 vs 串行执行约束、timeout 编排、回滚能力 | 直接拆流程任务，依赖接口与 INF-TODO-017 |
| PluginLifecycleManager | 详设 6.7/6.8 | L2 | 状态机图、load/unload/enable/disable 转移、故障恢复 | handle 释放细节、signal 处理、真实平台桥接 | 可直接拆流程骨架任务；真实 runtime bridge 仍受 PLG-BLK-04 约束 |
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
| SignatureReport 对象冻结 | 详设 6.5、6.8 | 数据结构 | P1 | Done | PLG-TODO-017 | 已通过 PluginReports.h 与 manager/pipeline optional aggregation 冻结 chain_status、reason_code 与 traceable ref 双承载边界 |
| CompatibilityReport 对象冻结 | 详设 6.5、6.8 | 数据结构 | P1 | Done | PLG-TODO-017 | 已通过 PluginReports.h 与 manager/pipeline optional aggregation 冻结 abi/api/dependency 三检、reason_codes 与 traceable ref 双承载边界 |
| IPluginSignatureVerifier 接口 | 详设 6.6 | 接口 | P1 | Done | PLG-TODO-015 | 已通过 IPluginSignatureVerifier.h + compile/boundary tests 冻结 signature/trust 最小输入输出对象 |
| IPluginCompatibilityEngine 接口 | 详设 6.6 | 接口 | P1 | Done | PLG-TODO-016 | 已通过 IPluginCompatibilityEngine.h + matrix/boundary tests 冻结 host ABI snapshot、dependency snapshot 与 CompatibilityReport |
| PluginManifest 对象冻结 | 详设 6.5；INF-BLK-09 | 数据结构 | P0 | Done | PLG-TODO-014 | 已通过 PluginManifest.h + unit/contract 守卫落盘 schema_version、required_abi 与 extension namespace 冻结边界 |
| PluginValidationPipeline 建立 | 详设 6.3、6.7/6.8 | 流程 | P0 | Done | PLG-TODO-005 | 已落盘三检骨架，并通过 unit/contract 验证 policy deny、signature fail、compatibility fail 三类失败枝条 |
| PluginAuditAdapter 建立 | 详设 6.10；infra 系统 6.10 | 适配器 | P0 | Done | PLG-TODO-006 | 已落盘高风险动作审计适配层，并通过 unit/integration 验证 load、unload、policy deny 的事件投影与导出追踪 |
| plugin 私有错误码域 | 详设 6.6、9.1 | 错误码 | P0 | Done | PLG-TODO-007 | 六个错误码已列，可直接定义 |
| plugin 构建入口接线 | 详设 8.1、8.2 | 构建/测试 | P0 | Done | PLG-TODO-008 | plugin source/public header 已按组件收敛到 infra CMake 入口 |
| plugin 单元测试入口注册 | 详设 9.1；编码规范 3.7 | 测试 | P0 | Done | PLG-TODO-009 | plugin unit 注册逻辑已下沉到 tests/unit/infra/plugin 子目录，并以组件级列表接入顶层聚合 |
| plugin 合约边界测试入口注册 | 蓝图 4.3；contracts 冻结策略；编码规范 3.7 | 测试 | P0 | Done | PLG-TODO-010 | plugin contract 注册逻辑已下沉到 tests/contract/plugin 子目录，并统一收敛 helper 与 discoverability |
| PluginLifecycleManager 状态机 | 详设 6.7/6.8 | 流程 | P1 | Done | PLG-TODO-011 | 已落盘生命周期骨架，并通过 unit/contract 验证关键状态转移、failed cleanup 与公共结果边界稳定 |
| 失败注入测试与指标验证 | 详设 9.1、9.2 | 测试 | P1 | Done | PLG-TODO-012 | 已通过 integration 验证 signature fail、compatibility fail、load timeout 三条失败路径的 report/audit 证据链，并补齐 validation failure audit 接线 |
| Profile 插件治理行为矩阵测试 | 蓝图 5.1/5.2；详设 6.9 | 测试 | P1 | Done | PLG-TODO-013 | 已补齐五档 profile 的 infra.plugin.* schema，并通过 ConfigLoader + contract/integration 冻结三档 profile 的治理矩阵 |

---

## 6. 原子任务清单

### 6.1 Build-ready 任务（可直接推进）

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度 | 代码目标 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PLG-TODO-001 | Done | 定义 PluginDescriptor 数据结构 | 详设 6.5；infra 系统 6.5 | 详设 6.5 核心对象与 contracts 对齐 | L2 | infra/include/plugin/PluginDescriptor.h，承载 plugin_id、version、abi、trust_level、status、source 字段 | unit：字段默认值与 unknown 兜底；contract：不越权扩写标识语义 | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_descriptor_unit_test dasall_contract_plugin_descriptor_boundary_test && ctest --test-dir build-ci -N -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest"` | 无 | 无 | 无 | 数据结构头文件、基础单测、字段说明文档 | 已完成（2026-04-01）：PluginDescriptor.h、PluginDescriptorFieldTest、PluginDescriptorBoundaryContractTest 落盘；空值统一收敛为 unknown，ctest 发现 2 个用例并全部通过 |
| PLG-TODO-002 | Done | 定义 PluginCatalog 数据结构 | 详设 6.5、6.3 | 详设 6.5 对象、6.3 发现结果语义 | L2 | infra/include/plugin/PluginCatalog.h，承载 discovered_plugins[]、rejected_plugins[] 字段，拒绝原因可追溯 | unit：空 catalog、全发现、全拒绝三种状态；contract：与 Observation evidence_ref 关联性测试 | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_catalog_unit_test dasall_contract_plugin_catalog_boundary_test && ctest --test-dir build-ci -N -R "PluginCatalogTest|PluginCatalogBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginCatalogTest|PluginCatalogBoundaryContractTest"` | PLG-TODO-001 | 无 | 无 | 数据结构头文件、单测与合约测试 | 已完成（2026-04-01）：PluginCatalog.h、PluginCatalogTest、PluginCatalogBoundaryContractTest 落盘；空 catalog/全发现/全拒绝和 evidence_ref 边界均已覆盖，ctest 发现 2 个用例并全部通过 |
| PLG-TODO-003 | Done | 新增 IPluginManager 接口与骨架实现 | 详设 6.6；infra 系统 6.6 | 详设 6.6 核心接口、6.7 主流程时序 | L2 | infra/include/plugin/IPluginManager.h 与 infra/src/plugin/PluginManager.cpp 骨架，包含 discover()、validate()、load()、unload()、list_active() 五个方法 | unit：接口编译与返回对象类型；contract：ResultCode 引用不越权 | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_manager_interface_unit_test dasall_contract_plugin_manager_boundary_test && ctest --test-dir build-ci -N -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest"` | PLG-TODO-001、PLG-TODO-002 | 无 | 无 | 接口头文件、空壳实现、编译与单测证据 | 已完成（2026-04-01）：IPluginManager.h、PluginManager.cpp、PluginManagerInterfaceCompileTest、PluginManagerBoundaryContractTest 落盘；最小 request/result 边界已在同轮收敛，ctest 发现 2 个用例并全部通过 |
| PLG-TODO-004 | Done | 新增 IPluginPolicyGate 接口 | 详设 6.6；infra 系统 6.6 | 详设 6.6 IPluginPolicyGate、6.3 策略决策 | L2 | infra/include/plugin/IPluginPolicyGate.h，包含 evaluate(manifest, policy_snapshot, profile) 方法，返回 PolicyDecisionRef | unit：接口编译；contract：不越权扩写策略对象 | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_policy_gate_interface_unit_test dasall_contract_plugin_policy_gate_boundary_test && ctest --test-dir build-ci -N -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginPolicyGateInterfaceCompileTest|PluginPolicyGateBoundaryContractTest"` | PLG-TODO-001、INF-TODO-017（SecurityPolicyManager） | 无 | 无 | 接口头文件、编译证据 | 已完成（2026-04-01）：IPluginPolicyGate.h、PluginPolicyGateInterfaceCompileTest、PluginPolicyGateBoundaryContractTest 落盘；最小 PluginPolicyRequest 已在同轮收敛，ctest 发现 2 个用例并全部通过 |
| PLG-TODO-005 | Done | 建立 PluginValidationPipeline 骨架与三检流程 | 详设 6.3、6.7、6.8 | 详设 6.7 主流程、6.8 异常流程、6.3 子组件依赖 | L2 | infra/src/plugin/PluginValidationPipeline.cpp（or PluginValidatorImpl.cpp），骨架实现 policy->signature->compat 三检流程，失败时返回对应 Report | unit：三检失败分支可区分；contract：聚合 Report 语义稳定 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_plugin_validation_pipeline_unit_test dasall_contract_plugin_validation_pipeline_boundary_test && ctest --test-dir build-ci --output-on-failure -R "PluginValidationPipelineTest|PluginValidationPipelineBoundaryContractTest"` | PLG-TODO-003、PLG-TODO-004、PLG-TODO-001、PLG-TODO-002（以及 INF-TODO-017） | 无 | 无 | pipeline 实现、单测、边界测试 | 已完成（2026-04-07）：新增 PluginValidationPipeline.h/.cpp，把 PluginManager.validate() 接入统一三检骨架，并通过 PluginValidationPipelineTest 与 PluginValidationPipelineBoundaryContractTest 验证失败枝条与 ref-only 聚合边界 |
| PLG-TODO-006 | Done | 新增 PluginAuditAdapter 适配器 | 详设 6.10；infra 系统 6.10 | 详设 6.10 审计点与字段清单 | L2 | infra/src/plugin/PluginAuditAdapter.cpp，负责生成 plugin load/unload 与 policy deny 的 AuditEvent，含 actor、action、target、outcome、evidence_ref、reason_code | unit：AuditEvent 必填字段校验；integration：审计写入与导出可追踪 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_plugin_audit_adapter_unit_test dasall_plugin_audit_trace_integration_test && ctest --test-dir build-ci --output-on-failure -R "PluginAuditAdapterTest|PluginAuditTraceIntegrationTest"` | PLG-TODO-001、INF-TODO-016（AuditService） | 无 | 无 | adapter 实现、单测与集成测试 | 已完成（2026-04-07）：新增 PluginAuditAdapter.h/.cpp、PluginAuditAdapterTest、PluginAuditTraceIntegrationTest，并验证 load/unload/policy deny 三类高风险动作可写入 AuditService 且按 action 导出追踪 |
| PLG-TODO-007 | Done | 定义 plugin 私有错误码域 | 详设 6.6、9.1 | 详设 6.6 错误语义、9.1 failure injection 场景 | L2 | infra/include/InfraErrorCode.h（or 新增 plugin/PluginErrorCode.h），定义六个 INF_E_PLUGIN_* 错误码并在 infra facade 侧建立映射入口 | unit：码值可判定；contract：映射 contracts::ResultCode 时不新增共享语义 | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_error_code_unit_test dasall_contract_plugin_error_code_boundary_test && ctest --test-dir build-ci -N -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest"` | 无 | 无 | 无 | 错误码头文件、映射说明、单测与合约测试 | 已完成（2026-04-01）：PluginErrorCode.h、PluginErrorCodeTest、PluginErrorCodeBoundaryContractTest 落盘；六个 `INF_E_PLUGIN_*` 码名已在设计收敛文档中冻结并全部通过 unit/contract 验证 |
| PLG-TODO-008 | Done | 接线 infra/src/plugin 与 infra/include/plugin CMake 目标 | 详设 8.1；当前 CMake 现状 | 详设 8.1 目录落盘建议、8.2 分阶段实施 | L2 | 更新 infra/CMakeLists.txt，新增 plugin 子目录与源文件列表（允许仅添加头文件与空实现入口） | build：dasall_infra 目标可编译且包含 plugin 真实头文件 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra && rg -n "DASALL_INFRA_PLUGIN_(SOURCES|PUBLIC_HEADERS)" infra/CMakeLists.txt` | PLG-TODO-001、PLG-TODO-002、PLG-TODO-003、PLG-TODO-004、PLG-TODO-007 | 无 | 无 | infra/CMakeLists.txt 改动、构建输出 | 已完成（2026-04-07）：infra/CMakeLists.txt 新增 `DASALL_INFRA_PLUGIN_SOURCES` 与 `DASALL_INFRA_PLUGIN_PUBLIC_HEADERS`，plugin 源与公开头不再散落在全局清单中，`dasall_infra` 构建通过 |
| PLG-TODO-009 | Done | 注册 tests/unit/infra/plugin 单元测试入口 | 详设 9.1；编码规范 3.7 | 详设 9.1 测试矩阵、编码规范 3.7 | L2 | 新增 tests/unit/infra/plugin/ 目录与 PluginDescriptorTest.cpp/PluginCatalogTest.cpp 等，在 tests/unit/CMakeLists.txt 中注册 plugin 子目录 | unit：用例被 ctest -L unit 发现 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_plugin_descriptor_unit_test dasall_plugin_catalog_unit_test dasall_plugin_error_code_unit_test dasall_plugin_manager_interface_unit_test dasall_plugin_policy_gate_interface_unit_test && ctest --test-dir build-ci -N -L unit \| grep -i plugin && ctest --test-dir build-ci --output-on-failure -L plugin` | PLG-TODO-001、PLG-TODO-002、PLG-TODO-003、PLG-TODO-004、PLG-TODO-007、PLG-TODO-008 | 无 | 无 | tests/unit/infra/plugin 目录与 CMakeLists.txt、ctest 发现证据 | 已完成（2026-04-07）：新增 tests/unit/infra/plugin/CMakeLists.txt，并把 plugin unit 注册从父级 CMake 下沉到子目录入口；`ctest -N -L unit` 可发现 5 个 plugin 用例，`ctest -L plugin` 5/5 通过 |
| PLG-TODO-010 | Done | 注册 tests/contract/infra/plugin 合约边界测试入口 | 蓝图 4.3；contracts 冻结策略；编码规范 3.7 | 详设 6.5 contracts 对齐关系、9.1 contract 覆盖目标 | L2 | 在 tests/contract/ 现有注册机制下新增 plugin 边界用例，例如 PluginObjectBoundaryTest 检查标识字段不越权、ErrorCodeMappingTest 检查码映射稳定 | contract：标识字段、错误码、AuditEvent 引用不越权 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_plugin_descriptor_boundary_test dasall_contract_plugin_catalog_boundary_test dasall_contract_plugin_error_code_boundary_test dasall_contract_plugin_manager_boundary_test dasall_contract_plugin_policy_gate_boundary_test && ctest --test-dir build-ci -N -L contract | grep -i Plugin && ctest --test-dir build-ci --output-on-failure -L contract -R "Plugin"` | PLG-TODO-001、PLG-TODO-002、PLG-TODO-003、PLG-TODO-004、PLG-TODO-007、PLG-TODO-008、PLG-TODO-009 | 无 | 无 | tests/contract/plugin/ 源文件、contract 执行记录 | 已完成（2026-04-07）：新增 tests/contract/plugin/CMakeLists.txt，并把 plugin contract 注册从主文件下沉到子目录入口；`ctest -N -L contract` 可发现 5 个 plugin contract 用例，`ctest -L contract -R Plugin` 5/5 通过 |
| PLG-TODO-011 | Done | 新增 PluginLifecycleManager 状态机与转移测试 | 详设 6.7、6.8 | 详设 6.7 主流程、6.8 异常与恢复时序 | L2 | infra/src/plugin/PluginLifecycleManager.cpp（skeleton），包含 load/unload/enable/disable 四个方法与内部状态转移，不进入实际 dlopen/dlsym 实现 | unit：状态机转移枚举测试（loaded->active、loaded->disabled、failed->cleanup）；contract：LoadResult/UnloadResult 返回值稳定 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_plugin_lifecycle_state_unit_test dasall_contract_plugin_manager_boundary_test && ctest --test-dir build-ci --output-on-failure -R "PluginLifecycleStateTest|PluginManagerBoundaryContractTest"` | PLG-TODO-003、PLG-TODO-006 | 无 | 无 | lifecycle 骨架实现、状态转移单测 | 已完成（2026-04-07）：新增 PluginLifecycleManager.h/.cpp 与 PluginLifecycleStateTest，并将 PluginManager.load/unload/list_active 接入生命周期骨架，验证关键状态转移、failed cleanup 与合约边界稳定 |
| PLG-TODO-012 | Done | 编写 plugin 失败注入与可观测性测试 | 详设 9.1、9.2 | 详设 9.1 failure injection 场景、9.2 quality gates | L2 | tests/ 下新增 PluginSignatureFailureTest、PluginCompatibilityFailureTest、PluginLoadTimeoutTest 等，验证失败路径有日志/审计/指标 | integration：当 validate fail 时有 AuditEvent、当 load fail 时有 CompatibilityReport | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_plugin_audit_adapter_unit_test dasall_plugin_failure_observability_integration_test && ctest --test-dir build-ci -N -L integration | grep -E "Plugin(AuditTraceIntegrationTest|FailureObservabilityIntegrationTest)" && ctest --test-dir build-ci --output-on-failure -R "PluginAuditAdapterTest|PluginFailureObservabilityIntegrationTest"` | PLG-TODO-005、PLG-TODO-006、PLG-TODO-011 | 无 | 无 | 失败注入测试源文件、执行记录 | 已完成（2026-04-07）：新增 PluginFailureObservabilityIntegrationTest，并把 PluginValidationPipeline 接入 signature/compatibility failure audit；定向 build、discoverability 与 ctest 验证通过 |
| PLG-TODO-013 | Done | 编写 Profile 插件治理行为矩阵测试 | 蓝图 5.1、5.2；详设 6.9 | 详设 6.9 配置项与默认策略、蓝图 3.13 profile 机制 | L2 | tests/ 下新增 ProfilePluginMatrixTest.cpp，覆盖 desktop_full/edge_balanced/edge_minimal 三档 profile 下 plugin 配置项的行为差异验证 | compatibility：三档 profile 下 plugin.allowlist、infra.plugin.signature.required、infra.plugin.abi.strict_mode 等行为一致性检查 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_profile_runtime_policy_schema_test dasall_profile_plugin_matrix_integration_test && ctest --test-dir build-ci -N | grep -E "Profile(RuntimePolicySchemaContractTest|PluginMatrixIntegrationTest)" && ctest --test-dir build-ci --output-on-failure -R "ProfileRuntimePolicySchemaContractTest|ProfilePluginMatrixIntegrationTest"` | 详设 9.1 里程碑 M4（all 标准对象与接口冻结） | 无 | 无 | profile 矩阵测试源文件、行为表 | 已完成（2026-04-07）：补齐五档 profile 的 infra.plugin.* schema，新增 ProfilePluginMatrixIntegrationTest，并通过 schema contract + integration 冻结三档 profile 的治理矩阵 |

### 6.2 原阻塞任务（已于 2026-04-08 全部完成）

| ID | 状态 | 任务标题 | 来源依据 | 阻塞原因 | 设计锚点 | 解阻条件 | 最小解阻动作 | 当前执行建议 | 解阻后任务号 |
|---|---|---|---|---|---|---|---|---|---|
| PLG-TODO-014 | Done | 定义 PluginManifest 对象与 schema 冻结 | 详设 6.5；INF-BLK-09 | shared blocker 已解除，本轮已完成 schema v1.0 对象落盘 | 详设 6.5；6.9 | 已新增 public header、unit/contract 与 deliverable 证据 | docs/todos/infrastructure/deliverables/PLG-TODO-014-PluginManifest设计收敛.md | 已完成；本轮无需再执行 | 完成后进入 015 |
| PLG-TODO-015 | Done | 定义 IPluginSignatureVerifier 与签名链路规范 | 详设 6.6；INF-BLK-09 | shared blocker 已解除，本轮已完成 verifier boundary 落盘 | 详设 6.6、6.8、9.1 | 已新增 public header、signature/trust 最小输入输出对象与 compile/boundary tests | docs/todos/infrastructure/deliverables/PLG-TODO-015-IPluginSignatureVerifier设计收敛.md | 已完成；本轮无需再执行 | 完成后进入 016 |
| PLG-TODO-016 | Done | 定义 IPluginCompatibilityEngine 与 ABI 兼容矩阵 | 详设 6.6；INF-BLK-09 | shared blocker 已解除，本轮已完成 compatibility boundary 落盘 | 详设 6.6、6.8、6.9 | 已新增 public header、host ABI/dependency 最小输入对象、CompatibilityReport 与 matrix/boundary tests | docs/todos/infrastructure/deliverables/PLG-TODO-016-IPluginCompatibilityEngine设计收敛.md | 已完成；本轮无需再执行 | 完成后进入 017 |
| PLG-TODO-017 | Done | 定义 SignatureReport 与 CompatibilityReport 对象 | 详设 6.5、6.8；INF-BLK-09 | shared blocker 已解除，本轮已完成 shared report 与 validation aggregation 收口 | 详设 6.5、6.8 | 已抽取 shared report public header，并通过 manager/pipeline unit/contract 验证 optional object + ref 双承载边界 | docs/todos/infrastructure/deliverables/PLG-TODO-017-PluginReports与聚合收敛.md | 已完成；本轮无需再执行 | 完成后评估 load/runtime bridge 相关实现 |
| PLG-TODO-018 | Not Started | 冻结 IPluginRuntimeBridge v1 最小契约并接线 PluginLifecycleManager | 详设 6.6、6.7/6.8；PLG-BLK-04 | 6.6 IPluginRuntimeBridge；6.7 load/unload；6.8 failure cleanup | L2 | infra/include/plugin/IPluginRuntimeBridge.h、infra/src/plugin/PluginRuntimeBridge.cpp、infra/src/plugin/PluginLifecycleManager.{h,cpp} | open_library/load_symbol/close_library/sandbox_hint；unit：bridge 句柄生命周期、symbol 缺失与错误映射；contract：bridge 边界不泄露 runtime 主控语义 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_plugin_runtime_bridge_unit_test dasall_contract_plugin_runtime_bridge_boundary_test && ctest --test-dir build-ci -N -R "PluginRuntimeBridgeTest|PluginRuntimeBridgeBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginRuntimeBridgeTest|PluginRuntimeBridgeBoundaryContractTest" | PLG-TODO-011、PLAT-LNX-TODO-026 | 无 | 无 | IPluginRuntimeBridge 头文件、桥接实现、PluginLifecycleManager 接线、unit/contract 证据 | 仅当 PluginLifecycleManager 不再以匿名回调描述真实装载边界，且桥接契约可被 unit/contract 测试稳定约束时完成 |

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
| **Phase 6：对象与接口冻结续航** | PLG-TODO-014、015、016、017 | ✓ 已完成：014 -> 015 -> 016 -> 017 | INF-BLK-09 shared blocker 已解除，四个串行冻结任务均已完成 |
| **Phase 7：runtime bridge 最小契约** | PLG-TODO-018 | 串行（以前置 PLAT-LNX-TODO-026 完成为准） | 先冻结 platform 动态库加载抽象，再把 plugin 生命周期骨架收敛为 IPluginRuntimeBridge v1，不再把该项表述为等待 runtime 主链 |

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
| **INF-BLK-09（上游）** | 已解阻（2026-04-07）：PluginManifest schema v1、signature trust policy v1、ABI compatibility matrix v1 已同步冻结 | shared blocker 已完成跨子域协调 | PLG-TODO-014/015/016/017、后续装载实现、测试完善 | infra 系统设计评审 + plugin 专项台账回写 | P0（critical path） | M3 已完成 | 直接启动 014 -> 015 -> 016 -> 017 |
| **PLG-BLK-01** | 已解阻（2026-04-07）：manifest schema 最终字段集与扩展命名空间已冻结 | shared blocker 已解除 | PLG-TODO-014 | plugin 组件负责人 | P0 | M2 已完成 | 直接执行 014 |
| **PLG-BLK-02** | 已解阻（2026-04-07）：ABI 兼容矩阵与 Host ABI 识别规则已冻结 | shared blocker 已解除 | PLG-TODO-016 | plugin 组件负责人 + platform 组件负责人 | P0 | M2 已完成 | 直接执行 016 |
| **PLG-BLK-03** | 已解阻（2026-04-07）：签名校验与信任链规范已冻结 | shared blocker 已解除 | PLG-TODO-015 | plugin 组件负责人 + security policy 组件负责人 | P0 | M2 已完成 | 直接执行 015 |
| **PLG-BLK-04** | PluginRuntimeBridge 与平台动态库接口的约定不完整 | platform 动态库加载抽象与 plugin 侧桥接边界尚未共同冻结 | PLG-TODO-018、后续真实装载实现 | platform 组件负责人 + plugin 组件负责人 | P0（下一原子任务） | PLAT-LNX-TODO-026 + PLG-TODO-018 | 先完成 platform loader 抽象，再冻结 IPluginRuntimeBridge v1；不再作为 runtime 子系统前置 |
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

#### 当前仍不推进的范围

以下范围在本轮 shared blocker 解阻后仍保持不可直接进入：

1. **完整装载实现与运行时集成**
        - **原因**：PLG-TODO-014~017 仍需先完成对象/接口冻结；PluginRuntimeBridge 平台细节未定
        - **预计解阻时间**：在 014~017 完成并单独评审 runtime bridge 后

### 11.2 当前最小可执行粒度

| 对象级别 | 可执行范围 | 例子 |
|---|---|---|
| **L2 数据结构** | ✓ 可继续推进 | PluginDescriptor、PluginCatalog、错误码枚举，且 PluginManifest 已完成；下一步进入 Reports |
| **L2 接口定义** | ✓ 可继续推进 | IPluginManager、IPluginPolicyGate，以及下一步的 verifier / compatibility 接口 |
| **L2 流程骨架** | ✓ 已具备 | ValidationPipeline、LifecycleManager 状态机架构 |
| **L3 函数实现** | ✗ 不可 | load()、unload() 完整实现（依赖 L2 约束冻结） |
| **L3 条件分支** | ✗ 不可 | 验签失败降级、ABI 兼容回退等具体逻辑 |
| **集成与联调** | ✗ 不可 | plugin 与 runtime 协同、PluginRuntimeBridge 平台接入 |

### 11.3 与上游依赖的关系

| 上游组件 | 当前状态 | 对 plugin 的影响 | 预期 Ready 时间 |
|---|---|---|---|
| INF-TODO-017（SecurityPolicyManager） | Done | PLG-TODO-004、PLG-TODO-005 已消费其冻结边界 | 已完成 |
| INF-TODO-016（AuditService） | Done | PLG-TODO-006 已消费其写入与导出边界 | 已完成 |
| INF-BLK-09（manifest/ABI/signature 三项冻结） | Resolved | 014~017 已恢复为可执行顺序任务 | 已完成 |
| platform PluginRuntimeBridge | 未启动 | 011 已通过私有回调骨架绕开该依赖；后续真实平台装载实现仍取决此 | 3-4 周 |

### 11.4 下一步建议

#### 下一阶段建议

1. ✓ 评估 PluginRuntimeBridge 与 load/runtime 集成的下一轮原子任务拆解，先补平台桥接约束再进入完整装载实现。
2. ✓ 若后续需要从 optional object + ref 双承载迁移到纯对象聚合，必须单独记录 breaking review 结论、迁移窗口与回退边界。
3. ✓ integration/end-to-end 级别的 plugin load 联调只在 platform/runtime bridge 冻结后推进。

#### 暂不启动

1. ○ 完整装载实现与 runtime/platform 集成测试
2. ○ 远程 plugin 仓库与供应链中心能力

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
| P0-M3 | manifest/signature/compatibility 对象与接口冻结 | PLG-TODO-014/015/016/017 | Week 3-4 | 015~017 仍需串行落盘并保持 breaking review 可见 |
| P1-M4 | 完整装载与运行时集成 | 后续任务 | Week 5+ | 平台层交付与性能验证 |

### 12.2 关键里程碑定义

- **M1（Week 1-2）**：所有 L2 对象与接口冻结，ctest 发现 plugin 用例数 ≥5
- **M2（Week 2-3）**：Phase 4 完成，PluginValidationPipeline 与 PluginLifecycleManager 骨架可编译可测
- **M3（Week 3-4）**：manifest/ABI/signature shared blocker 已解除，PLG-TODO-014/015/016/017 全部完成，plugin public boundary 已收敛到 shared reports + optional aggregation 形态
- **M4（Week 4-5）**：完整装载实现完成，unit/contract/failure injection/profile matrix 全部 gates 通过
- **M5（Week 5+）**：与 runtime/platform 集成联调完成，端到端 plugin 加载链路冒烟测试通过

---

## 13. 常见问题与澄清

### Q1：为什么仍然"不进入完整装载实现"？

**A**：虽然 INF-BLK-09 已解除，且 PLG-TODO-014/015/016/017 已完成，但 PluginRuntimeBridge 与真实装载链路仍未冻结。此时直接实现 load() 仍会把尚未稳定的平台桥接约束绑进装载路径，增加 breaking review 风险。遵循“先冻结边界，再接实现”的工程原则，避免返工。

### Q2：PluginRuntimeBridge 的缺失是否阻塞当前任务？

**A**：否。当前任务（PLG-TODO-001~013）仅涉及接口定义与骨架实现，用 mock bridge 可验证状态机逻辑。真实平台接入是后续里程碑 M4-5 的事。

### Q3：如何处理 infra 与 runtime 的界限？

**A**：严格遵循 ADR-008：
- **plugin 职责**：发现、校验、装载、卸载、输出治理结果
- **runtime 职责**：根据 LoadResult 决策、调度、故障恢复
- plugin 不拥有调度权，只是被 runtime 的"守门员"。

### Q4：为什么 PluginManifest 曾经 Blocked，现在又可以完成？

**A**：PluginManifest 与 ABI / signature / report 边界强耦合；在 shared blocker 未解前，直接落盘会导致字段集和扩展命名空间后续反复改写。2026-04-07 已通过 INF-BLK-09 recovery 冻结 schema v1、trust policy v1 与 ABI matrix v1，因此 014 可以在不引入 parser/manager 改签名的前提下完成对象落盘。

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

## 21. 本轮执行记录（2026-04-07 / PLG-TODO-010）

### 21.1 选中任务

1. 本轮任务：PLG-TODO-010。
2. 可执行性依据：PLG-TODO-008/009 已完成，五个 plugin contract 用例也已存在；当前剩余缺口仅是 contract 注册入口仍留在 tests/contract/CMakeLists.txt 主文件中，可在单轮内完成“子目录入口收口 + contract discoverability 验证 + TODO/工作日志回写”。

### 21.2 研究与 Design 结论

本地证据：

1. tests/contract/smoke 已存在五个 plugin contract 用例，说明 010 的问题不是缺测试，而是缺少 plugin 专属注册 helper 和组件级入口。
2. tests/contract/CMakeLists.txt 已为 logging/audit/secret/metrics/tracing/ota 提供组件级 helper，但 plugin 仍维持最原始的通用注册片段，入口风格不对称。
3. 009 已把 unit 入口收口到 tests/unit/infra/plugin/CMakeLists.txt；010 若不采用同样的组件级路径，Phase 3 的 unit/contract 入口就无法形成对称结构。

外部参考：

1. CMake 官方 add_subdirectory 文档说明，子目录会立即纳入当前构建图；本轮据此把 plugin contract 注册从 tests/contract/CMakeLists.txt 主文件剥离到 tests/contract/plugin/CMakeLists.txt。
2. CMake 官方 add_test / set_tests_properties 文档说明，测试属性应在创建目录作用域内设置；本轮据此为 plugin contract 用例统一赋予 `contract;smoke;plugin` 标签。

D 结论：

1. 新增 docs/todos/infrastructure/deliverables/PLG-TODO-010-plugin合约边界测试入口注册收敛.md，冻结 plugin contract helper、子目录入口与 discoverability 验证链路。
2. Build 三件套锁定为：
        - 代码目标：tests/contract/plugin/CMakeLists.txt、tests/contract/CMakeLists.txt。
        - 测试目标：五个 plugin contract 用例可被 contract 视图发现，并可定向执行 `-R Plugin` 子集。
        - 验收命令：cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_plugin_descriptor_boundary_test dasall_contract_plugin_catalog_boundary_test dasall_contract_plugin_error_code_boundary_test dasall_contract_plugin_manager_boundary_test dasall_contract_plugin_policy_gate_boundary_test && ctest --test-dir build-ci -N -L contract | grep -i Plugin && ctest --test-dir build-ci --output-on-failure -L contract -R "Plugin"。
3. D Gate：PASS。

### 21.3 Build 交付与证据

交付物：

1. tests/contract/plugin/CMakeLists.txt：新增 dasall_register_plugin_contract_test(...) helper，并在子目录内注册五个 plugin contract 用例。
2. tests/contract/CMakeLists.txt：移除主文件内联的 plugin contract 注册片段，改为 add_subdirectory(plugin)。
3. docs/todos/infrastructure/deliverables/PLG-TODO-010-plugin合约边界测试入口注册收敛.md：记录输入依据、外部参考、Design->Build 映射与 Build 合规复核。
4. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md：将 PLG-TODO-010 回写为 Done，并补充本轮执行记录。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_contract_plugin_descriptor_boundary_test dasall_contract_plugin_catalog_boundary_test dasall_contract_plugin_error_code_boundary_test dasall_contract_plugin_manager_boundary_test dasall_contract_plugin_policy_gate_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -L contract | grep -i Plugin`：通过，可发现 5 个 plugin contract 入口。
4. `ctest --test-dir build-ci --output-on-failure -L contract -R "Plugin"`：通过，5/5 tests passed。

Build 合规复核：

1. 代码注释：本轮只做 CMake 注册入口收口，helper 与标签命名已直接表达 plugin contract 归属，无需新增冗余注释。
2. 正负例覆盖：复用既有五个 plugin contract 用例的正负例断言，本轮不重写测试语义检查。
3. 测试发现性：已同时验证 `ctest -N -L contract` 的发现性与 `ctest -L contract -R Plugin` 的执行结果。
4. TODO 证据回写：已完成任务状态、交付物路径与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 plugin contract 注册入口收口、设计收敛文档、专项 TODO 与工作日志证据。

阻塞修复：

1. 当前仓库已有 plugin contract 用例，但注册入口仍留在 tests/contract/CMakeLists.txt 主文件，导致 010 的“入口注册”目标未闭环；本轮通过新增 plugin 子目录 CMake 完成最小收口，而不改 contract 语义。
2. plugin contract 过去没有组件级 helper 与标签；本轮补齐 `contract;smoke;plugin` 标签后，plugin contract 子集可被稳定 discover 和定向执行。

---

## 22. 本轮执行记录（2026-04-07 / PLG-TODO-005）

### 22.1 选中任务

1. 本轮任务：PLG-TODO-005。
2. 可执行性依据：PLG-TODO-003/004 与 INF-TODO-017 已完成，validation request、policy request、policy snapshot 与 policy decision 的公共边界均已冻结；受 INF-BLK-09 影响的完整 manifest/signature/ABI 对象仍未解阻，但本轮只需维持 ref-only 骨架。

### 22.2 Blocker 修复与 Design 结论

阻塞证据：

1. PluginValidationRequest 只冻结了 plugin_id、manifest_ref、package_ref、profile_id，而 IPluginPolicyGate 需要 governance-ready 的 PluginPolicyRequest；若不先补派生逻辑，pipeline 无法真正驱动 policy 阶段。
2. infra/CMakeLists.txt 在 008 轮后虽然新增了 DASALL_INFRA_PLUGIN_SOURCES，但该变量定义晚于 DASALL_INFRA_CORE_SOURCES 的求值时机，导致新的 plugin 私有源文件不会自动进入 dasall_infra，实际会在测试链接阶段暴露 undefined reference。
3. 原任务行验收命令只构建 dasall_infra，无法证明新增的 pipeline unit/contract 目标本身能够编译和执行。

最小 blocker-fix：

1. 在 PluginValidationPipeline 内部把 PluginValidationRequest 归一化为最小 PluginPolicyRequest：descriptor 继续复用已冻结的 PluginDescriptor，status 固定为 Discovered，source 复用 package_ref，version/abi 以占位值承接未解阻对象。
2. 在 infra/CMakeLists.txt 中把 `${DASALL_INFRA_PLUGIN_SOURCES}` 显式追加到 dasall_infra 的 target_sources，并把 PluginValidationPipeline.h 注册为 private header，修复 plugin 私有源文件未真正接入构建图的根因。
3. 将 005 的验收命令升级为显式构建 pipeline unit/contract 目标并定向执行对应 ctest 子集。

D 结论：

1. PluginValidationPipeline 只拥有 validation 期的串行编排权，不承担 runtime load/unload 状态机，也不越权定义完整 SignatureReport/CompatibilityReport 对象。
2. policy deny、signature fail、compatibility fail 三类失败都必须返回 traceable refs；其中 policy deny 继续复用 PolicyDecisionRef，signature/compatibility 阶段继续维持 report_ref-only 边界。
3. Build 三件套锁定为：
        - 代码目标：infra/src/plugin/PluginValidationPipeline.h、PluginValidationPipeline.cpp、PluginManager.cpp、infra/CMakeLists.txt。
        - 测试目标：PluginValidationPipelineTest、PluginValidationPipelineBoundaryContractTest。
        - 验收命令：cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_plugin_validation_pipeline_unit_test dasall_contract_plugin_validation_pipeline_boundary_test && ctest --test-dir build-ci --output-on-failure -R "PluginValidationPipelineTest|PluginValidationPipelineBoundaryContractTest"。
4. D Gate：PASS。

### 22.3 Build 交付与证据

交付物：

1. infra/src/plugin/PluginValidationPipeline.h、PluginValidationPipeline.cpp：新增 validation pipeline 骨架，收敛 policy -> signature -> compatibility 三检顺序与统一出口。
2. infra/src/plugin/PluginManager.cpp：validate() 改为委托 PluginValidationPipeline，而不是返回统一 skeleton message。
3. infra/CMakeLists.txt：修复 plugin 私有源文件未真正进入 dasall_infra 的接线问题，并注册 pipeline 私有头。
4. tests/unit/infra/plugin/PluginValidationPipelineTest.cpp：覆盖 policy deny、signature fail、compatibility fail、三段全通过四类路径。
5. tests/contract/smoke/PluginValidationPipelineBoundaryContractTest.cpp：验证 report 仍为 ref-only 边界，且失败语义保持在 contracts ResultCode/ErrorInfo 范围内。
6. docs/todos/infrastructure/deliverables/PLG-TODO-005-plugin校验管线骨架收敛.md：记录输入依据、外部参考、Design->Build 映射、blocker 修复与 Build 合规复核。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_validation_pipeline_unit_test dasall_contract_plugin_validation_pipeline_boundary_test`：通过。
3. `ctest --test-dir build-ci --output-on-failure -R "PluginValidationPipelineTest|PluginValidationPipelineBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：pipeline、stage result 与测试名已直接表达治理阶段和失败枝条语义，无需新增冗余注释。
2. 正负例覆盖：unit 覆盖 policy deny、signature fail、compatibility fail 与 success 聚合；contract 覆盖 report ref-only 边界与 contracts 错误类型约束。
3. 测试发现性：通过显式构建和定向 ctest 已证明 pipeline 新增目标可编译、可执行、可回归。
4. TODO 证据回写：已完成任务状态、验收命令、交付物路径与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 validation pipeline、plugin build 接线修复、定向 unit/contract 测试和文档证据。

阻塞修复：

1. 005 原始设计要求 policy->signature->compat 三检，但 validation request 与 policy request 之间存在 descriptor 缺口；本轮通过 pipeline 内部最小归一化修复，不打破 003/004 已冻结的公共接口。
2. 005 实施过程中暴露出 008 轮遗留的 CMake 求值顺序问题；本轮已把 plugin sources 显式并入 target_sources，避免后续 006/011 再次踩到同类链接缺口。

---

## 23. 本轮执行记录（2026-04-07 / PLG-TODO-006）

### 23.1 选中任务

1. 本轮任务：PLG-TODO-006。
2. 可执行性依据：PLG-TODO-001/005 已完成，INF-TODO-016 已冻结 AuditService 与 audit::IAuditLogger 边界，plugin 私有源文件的真实构建接线问题也已在 005 轮修复；因此本轮只需补足 plugin 高风险动作的审计适配层和导出验证。

### 23.2 Design 结论

阻塞证据：

1. plugin 详细设计已经冻结 actor、action、target、outcome、evidence_ref、reason_code 等最小审计字段，但 infra/src/plugin 下并无独立适配器，意味着高风险动作的审计投影仍无法复用和统一验证。
2. tests/integration/infra/CMakeLists.txt 先前没有 plugin 子目录注册，无法证明 plugin 审计事件已经真正经过 AuditService 持久化并导出。
3. 若直接在 plugin 调用点散落组装 AuditEvent，会把 action 命名、side_effects 序列化和 write outcome 错误处理分散到多个位置，后续 011/012 难以保持审计语义稳定。

最小 blocker-fix：

1. 新增 PluginAuditAdapter，并把最小输入边界冻结为 PluginAuditRecord，把输出边界冻结为 PluginAuditEmitResult / PluginAuditAdapterStatus。
2. 通过 `std::shared_ptr<audit::IAuditLogger>` 解耦具体 sink，把 concrete AuditService 限定在 integration 测试中验证写入与导出链路。
3. 在 tests/integration/infra 下新增 plugin 子目录与定向用例，补上 plugin 审计的 integration discoverability 入口。

D 结论：

1. PluginAuditAdapter 只承担高风险动作的事件命名、字段投影、write outcome 处理与状态回传，不承担 runtime 主控或 lifecycle 状态推进。
2. 三类冻结动作是 `plugin.load`、`plugin.unload`、`plugin.policy_deny`；其中 policy deny 的 outcome 固定为 Rejected，load/unload 依赖 succeeded 标志映射到 Succeeded/Failed。
3. Build 三件套锁定为：
        - 代码目标：infra/src/plugin/PluginAuditAdapter.h、PluginAuditAdapter.cpp、infra/CMakeLists.txt、tests/integration/infra/CMakeLists.txt、tests/integration/infra/plugin/CMakeLists.txt。
        - 测试目标：PluginAuditAdapterTest、PluginAuditTraceIntegrationTest。
        - 验收命令：cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_plugin_audit_adapter_unit_test dasall_plugin_audit_trace_integration_test && ctest --test-dir build-ci --output-on-failure -R "PluginAuditAdapterTest|PluginAuditTraceIntegrationTest"。
4. D Gate：PASS。

### 23.3 Build 交付与证据

交付物：

1. infra/src/plugin/PluginAuditAdapter.h、PluginAuditAdapter.cpp：新增 plugin 审计适配层，冻结 high-risk action 命名、result_code side_effect、AuditContext 投影和 emit status 语义。
2. infra/CMakeLists.txt：把 PluginAuditAdapter.h/.cpp 纳入 plugin 私有源/头清单。
3. tests/unit/infra/plugin/CMakeLists.txt、tests/unit/infra/plugin/PluginAuditAdapterTest.cpp：注册并验证 load/unload/policy deny 成功路径、invalid record 拒绝路径和缺失 logger 失败路径。
4. tests/integration/infra/CMakeLists.txt、tests/integration/infra/plugin/CMakeLists.txt、tests/integration/infra/plugin/PluginAuditTraceIntegrationTest.cpp：补齐 plugin integration 入口，并验证事件经 AuditService 写入与导出后可按 `plugin.policy_deny` 过滤。
5. docs/todos/infrastructure/deliverables/PLG-TODO-006-plugin审计适配器收敛.md：记录输入依据、外部参考、Design->Build 映射与 Build 合规复核。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_audit_adapter_unit_test dasall_plugin_audit_trace_integration_test`：通过。
3. `ctest --test-dir build-ci --output-on-failure -R "PluginAuditAdapterTest|PluginAuditTraceIntegrationTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 边界：adapter 只依赖 audit::IAuditLogger 抽象，不反向依赖 runtime 或 concrete AuditService；AuditService 只在 integration 测试中使用。
2. 正负例覆盖：unit 同时覆盖成功 emit、无效 record 与缺失 sink；integration 覆盖写入、导出、按 action 过滤与 evidence/result_code 可追踪。
3. 测试发现性：新增的 unit 与 integration 目标均已显式构建并通过定向 ctest 子集验证，plugin 审计链路具备可持续回归入口。
4. TODO 证据回写：已完成任务状态、验收命令、交付物路径与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 PluginAuditAdapter、相关 unit/integration 注册与测试，以及 006 文档证据。

阻塞修复：

1. 006 原始缺口是 plugin 高风险动作没有组件级审计适配层；本轮通过 PluginAuditAdapter 将 action/outcome/side_effects 规则收敛到单一出口。
2. 006 原始测试缺口是 plugin integration 入口未注册；本轮已补齐 tests/integration/infra/plugin 目录与父级 add_subdirectory，避免 011/012 继续缺少审计导出验证路径。

---

## 24. 本轮执行记录（2026-04-07 / PLG-TODO-011）

### 24.1 选中任务

1. 本轮任务：PLG-TODO-011。
2. 可执行性依据：PLG-TODO-003/006 已完成，PluginLoadResult/PluginUnloadResult/ActivePluginSet 与 PluginAuditAdapter 均已冻结；TODO 的 Q2 也已明确 PluginRuntimeBridge 缺失不阻塞 skeleton 阶段，因此本轮只需补足生命周期状态机骨架与状态转移测试。

### 24.2 Design 结论

阻塞证据：

1. PluginManager.load()/unload() 仍返回统一 skeleton failure，说明 011 的真实缺口是 lifecycle state machine 不存在，而不是公共结果对象未定义。
2. 真实 PluginRuntimeBridge 未冻结，若把 011 直接做成平台装载实现，会把 skeleton 任务拖进 platform 依赖阻塞。
3. ActivePluginSet 的一致性检查要求 descriptor governance-ready；生命周期骨架若只维护 plugin_id/status，单测会在 active set gate 下失败。

最小 blocker-fix：

1. 新增 PluginLifecycleManager，并把 runtime 交互边界收敛为可注入的 load/unload callbacks，代替未冻结的 PluginRuntimeBridge。
2. 让 PluginManager 的 load/unload/list_active 统一委托生命周期骨架，而不是继续输出统一 skeleton error。
3. 在生命周期骨架内生成 governance-ready 的最小 descriptor 占位信息，并复用 PluginAuditAdapter 覆盖 failed unload 的审计出口。

D 结论：

1. PluginLifecycleManager 只承担 load/unload/enable/disable 状态推进、active set 维护、连续失败计数与 safe_mode 切换，不拥有 runtime 主控权。
2. RuntimeBridge 平台细节继续留在 private callback 边界之外，后续只需替换 callback 实现即可接入真实平台装载链路。
3. Build 三件套锁定为：
        - 代码目标：infra/src/plugin/PluginLifecycleManager.h、PluginLifecycleManager.cpp、PluginManager.cpp、infra/CMakeLists.txt、tests/unit/infra/plugin/CMakeLists.txt。
        - 测试目标：PluginLifecycleStateTest、PluginManagerBoundaryContractTest。
        - 验收命令：cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_plugin_lifecycle_state_unit_test dasall_contract_plugin_manager_boundary_test && ctest --test-dir build-ci --output-on-failure -R "PluginLifecycleStateTest|PluginManagerBoundaryContractTest"。
4. D Gate：PASS。

### 24.3 Build 交付与证据

交付物：

1. infra/src/plugin/PluginLifecycleManager.h、PluginLifecycleManager.cpp：新增生命周期骨架、runtime callbacks、managed plugin 集合、safe_mode 计数与 enable/disable 转移结果对象。
2. infra/src/plugin/PluginManager.cpp：load/unload/list_active 改为委托 PluginLifecycleManager。
3. infra/CMakeLists.txt：把 PluginLifecycleManager.h/.cpp 纳入 plugin 私有源/头清单。
4. tests/unit/infra/plugin/CMakeLists.txt、tests/unit/infra/plugin/PluginLifecycleStateTest.cpp：注册并验证 Loaded->Active、Loaded->Disabled->Unloaded、failed load cleanup + safe_mode、failed unload audit 四类路径。
5. docs/todos/infrastructure/deliverables/PLG-TODO-011-plugin生命周期骨架收敛.md：记录输入依据、外部参考、Design->Build 映射与 Build 合规复核。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_lifecycle_state_unit_test dasall_contract_plugin_manager_boundary_test`：通过。
3. `ctest --test-dir build-ci --output-on-failure -R "PluginLifecycleStateTest|PluginManagerBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 边界：未新增公共 plugin 接口；RuntimeBridge 缺口被限制在 private callbacks 中，不反向把 platform 细节带入 infra/plugin 公开边界。
2. 正负例覆盖：unit 同时覆盖正常状态转移、failed cleanup、safe_mode 和失败路径审计；contract 复用既有公共边界测试，确保 LoadResult/UnloadResult 未回归。
3. 测试发现性：生命周期状态机已有独立的 plugin unit 入口，后续 012 只需在此基础上扩展 failure injection。
4. TODO 证据回写：已完成任务状态、验收命令、交付物路径与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 PluginLifecycleManager、PluginManager lifecycle 接线、定向 unit/contract 验证与 011 文档证据。

阻塞修复：

1. 011 原始缺口是不存在可复用的生命周期状态机；本轮已把状态转移、failed cleanup 与 safe_mode 阈值收敛到 PluginLifecycleManager 单一出口。
2. 011 实施过程中暴露出 active set 需要 governance-ready descriptor 的隐含 gate；本轮已在生命周期骨架内补齐最小 descriptor 占位字段，避免 ActivePluginSet 一致性检查继续失效。

---

## 25. 本轮执行记录（2026-04-07 / PLG-TODO-012）

### 25.1 选中任务

1. 本轮任务：PLG-TODO-012。
2. 可执行性依据：PLG-TODO-005、PLG-TODO-006、PLG-TODO-011 已完成，validation stage callback、runtime load callback 与 AuditService 导出链路均已具备；当前唯一缺口是 validation failure 的统一审计出口和对应 integration discoverability。

### 25.2 Design 结论

阻塞证据：

1. plugin 详细设计 6.8/6.10/9.1 要求 signature fail、compatibility fail 与 load timeout 具备可观测证据，但当前代码只对 load/unload/policy deny 落了审计适配。
2. tests/integration/infra/plugin/CMakeLists.txt 在本轮前只有 PluginAuditTraceIntegrationTest，导致 failure injection 即使补齐也没有独立 integration 入口。
3. 本轮修改了 plugin integration 注册，必须同步验证 discoverability，不能只跑定向可执行文件。

最小 blocker-fix：

1. 扩展 PluginAuditAdapter，为 validation failure 冻结 `plugin.signature_fail` 与 `plugin.compatibility_fail` 两个私有审计动作。
2. 在 PluginValidationPipeline 中增加可选 PluginAuditAdapter 注入，把 policy deny / signature fail / compatibility fail 三类 validation rejection 接入统一审计出口。
3. 新增 PluginFailureObservabilityIntegrationTest，并把它接入 plugin integration 子目录注册。

D 结论：

1. 012 继续保持 public plugin interface 不变，所有新接线均限制在 infra/src/plugin 私有实现。
2. load timeout 仍通过 011 已冻结的 runtime callback 注入复现，不引入真实 PluginRuntimeBridge。
3. Build 三件套锁定为：
        - 代码目标：infra/src/plugin/PluginAuditAdapter.h/.cpp、PluginValidationPipeline.h/.cpp、tests/integration/infra/plugin/CMakeLists.txt。
        - 测试目标：PluginAuditAdapterTest、PluginFailureObservabilityIntegrationTest。
        - 验收命令：cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_plugin_audit_adapter_unit_test dasall_plugin_failure_observability_integration_test && ctest --test-dir build-ci -N -L integration | grep -E "Plugin(AuditTraceIntegrationTest|FailureObservabilityIntegrationTest)" && ctest --test-dir build-ci --output-on-failure -R "PluginAuditAdapterTest|PluginFailureObservabilityIntegrationTest"。
4. D Gate：PASS。

### 25.3 Build 交付与证据

交付物：

1. infra/src/plugin/PluginAuditAdapter.h、PluginAuditAdapter.cpp：新增 `plugin.signature_fail` 与 `plugin.compatibility_fail` 两个私有审计动作，并保持 rejected outcome 与 side_effects 语义稳定。
2. infra/src/plugin/PluginValidationPipeline.h、PluginValidationPipeline.cpp：新增可选 PluginAuditAdapter 注入，把 policy deny、signature fail、compatibility fail 三类 validation rejection 接入统一审计出口。
3. tests/unit/infra/plugin/PluginAuditAdapterTest.cpp：新增 validation failure action 的 unit 守卫。
4. tests/integration/infra/plugin/CMakeLists.txt、tests/integration/infra/plugin/PluginFailureObservabilityIntegrationTest.cpp：注册并验证 signature fail、compatibility fail、load timeout 三条 failure injection 路径的 report/audit 证据链。
5. docs/todos/infrastructure/deliverables/PLG-TODO-012-plugin失败注入与可观测性测试收敛.md：记录输入依据、外部参考、Design->Build 映射与 Build 合规复核。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_audit_adapter_unit_test dasall_plugin_failure_observability_integration_test`：通过。
3. `ctest --test-dir build-ci -N -L integration | grep -E "Plugin(AuditTraceIntegrationTest|FailureObservabilityIntegrationTest)"`：通过，可发现 2 个 plugin integration 用例。
4. `ctest --test-dir build-ci --output-on-failure -R "PluginAuditAdapterTest|PluginFailureObservabilityIntegrationTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 边界：没有新增任何 public plugin interface；validation failure 的审计接线仅存在于私有 pipeline/adapter。
2. 正负例覆盖：unit 覆盖新增 validation-failure audit action；integration 覆盖 signature fail、compatibility fail、load timeout 三类失败路径。
3. 测试发现性：新增 integration 用例已通过 `ctest -N -L integration` 进入 CTest 图。
4. TODO 证据回写：已完成任务状态、验收命令、交付物路径与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 012 的 failure-observability 接线、对应 unit/integration 测试与文档证据，不混入 013 的 profile 资产改动。

阻塞修复：

1. 012 的真实 blocker 不是“缺测试模板”，而是 validation failure 的统一审计出口未落盘；本轮已用 PluginAuditAdapter 最小扩展修复该根因。
2. 012 同时补齐了 plugin integration discoverability，避免新增 failure injection 用例只存在于显式 target 构建路径而无法被 CTest 图发现。

---

## 26. 本轮执行记录（2026-04-07 / PLG-TODO-013）

### 26.1 选中任务

1. 本轮任务：PLG-TODO-013。
2. 可执行性依据：012 完成后，plugin 失败注入与审计证据已稳定，接下来最短路径是把 profile 维度的 plugin 治理配置面冻结下来，并建立三档 profile 的行为矩阵测试。

### 26.2 Design 结论

阻塞证据：

1. 五档 runtime_policy.yaml 在本轮前都没有 `infra.plugin.*` 键，因此 013 的实际 blocker 是 profile 资产缺少 plugin 治理 schema，而不是“测试尚未编写”。
2. RuntimePolicySnapshot 当前不承载 plugin policy 域，如果直接扩写 snapshot，会把 013 扩成 profiles 公共对象改造任务，偏离原子粒度。
3. ProfileRuntimePolicySchemaContractTest 要求五档 profile 顶层 key 集合保持一致，因此不能只改 desktop_full/edge_balanced/edge_minimal 三档。

最小 blocker-fix：

1. 在五档 runtime_policy.yaml 中统一新增 `infra.plugin.*` schema，并为 cloud_full/factory_test 同步冻结占位策略值。
2. 在 ProfileRuntimePolicySchemaContractTest 中把 `infra.plugin.*` 纳入 required path 集合。
3. 新增 ProfilePluginMatrixIntegrationTest，直接用 ConfigLoader.load_profile() 读取真实 YAML，并验证三档 profile 的 typed config 行为矩阵。

D 结论：

1. 013 不修改 RuntimePolicySnapshot / RuntimePolicyProvider，保持 profiles 公共对象边界不变。
2. 013 采用“contract 锁结构 + integration 锁值语义”的双轨验证。
3. Build 三件套锁定为：
        - 代码目标：五档 runtime_policy.yaml、ProfileRuntimePolicySchemaContractTest.cpp、tests/integration/profiles/CMakeLists.txt。
        - 测试目标：ProfileRuntimePolicySchemaContractTest、ProfilePluginMatrixIntegrationTest。
        - 验收命令：cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_profile_runtime_policy_schema_test dasall_profile_plugin_matrix_integration_test && ctest --test-dir build-ci -N | grep -E "Profile(RuntimePolicySchemaContractTest|PluginMatrixIntegrationTest)" && ctest --test-dir build-ci --output-on-failure -R "ProfileRuntimePolicySchemaContractTest|ProfilePluginMatrixIntegrationTest"。
4. D Gate：PASS。

### 26.3 Build 交付与证据

交付物：

1. profiles/desktop_full/runtime_policy.yaml、profiles/cloud_full/runtime_policy.yaml、profiles/edge_balanced/runtime_policy.yaml、profiles/edge_minimal/runtime_policy.yaml、profiles/factory_test/runtime_policy.yaml：统一补齐 `infra.plugin.*` 配置域，并按 profile 冻结 allowlist、search_paths、load_timeout_ms、max_active、safe_mode.fail_threshold。
2. tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp：新增 `infra.plugin.*` required paths，并补充 profile plugin allowlist 基线断言。
3. tests/integration/profiles/CMakeLists.txt、tests/integration/profiles/ProfilePluginMatrixIntegrationTest.cpp：注册并验证 desktop_full / edge_balanced / edge_minimal 三档 profile 的 plugin typed config 行为矩阵。
4. docs/todos/infrastructure/deliverables/PLG-TODO-013-profile插件治理矩阵验证.md：记录 blocker 识别、schema 收敛、Design->Build 映射与合规复核。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_contract_profile_runtime_policy_schema_test dasall_profile_plugin_matrix_integration_test`：通过。
3. `ctest --test-dir build-ci -N | grep -E "Profile(RuntimePolicySchemaContractTest|PluginMatrixIntegrationTest)"`：通过，发现 2 个相关用例。
4. `ctest --test-dir build-ci --output-on-failure -R "ProfileRuntimePolicySchemaContractTest|ProfilePluginMatrixIntegrationTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 边界：没有修改 RuntimePolicySnapshot 或 PluginValidation/Lifecycle public interface；013 仍限制在 profile 资产与 tests。
2. 根因处理：直接修复了 `infra.plugin.*` schema 缺失的根因，而不是在测试中构造虚拟配置对象。
3. 结构与值：contract 保证五档 profile 结构对齐，integration 保证三档执行 profile 的治理矩阵值稳定。
4. 来源追溯：ProfilePluginMatrixIntegrationTest 同时验证 typed config 的 `source_kind=Profile` 与 `source_id=profiles/<profile>/runtime_policy.yaml`。
5. 提交隔离：本轮只包含 013 的 profile 资产、contract/integration 测试与文档证据，不混入后续 ABI/compatibility 子任务实现。

阻塞修复：

1. 013 的隐藏 blocker 已通过五档 schema 对齐消除；后续任何 profile 漂移都会先在 contract 层暴露。
2. 013 不再依赖 RuntimePolicySnapshot 扩写，因此保持了原子任务最小化。

---

## 27. 本轮执行记录（2026-04-07 / INF-BLK-09 解阻）

### 27.1 选中任务

1. 本轮任务：INF-BLK-09 / PLG-BLK-01~03 shared blocker recovery。
2. 可执行性依据：PLG-TODO-012、013 已完成并推送，当前阻断 014~017 的唯一前置只剩 manifest/signature/ABI 三项未同步冻结；该 blocker 可在单轮内通过设计补丁 + 台账校准完成最小修复。

### 27.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 的 6.5/6.6 只给出 PluginManifest、SignatureReport、CompatibilityReport 与 verifier/compatibility engine 的高层字段，没有冻结到 schema/rule 级别。
2. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 把 PLG-TODO-014~017 明确挂在 INF-BLK-09 下，要求 manifest/signature/ABI 三项同步冻结后才允许恢复执行。
3. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md 仍把 INF-BLK-09 作为 plugin 子域当前阻塞项，说明总台账尚未完成校准。
4. docs/architecture/DASALL_infrastructure子系统详细设计.md 已冻结 SecretManager 的只读 trust anchor 读取职责，足以支撑 plugin 验签边界。

外部参考：

1. SemVer 2.0.0：公开 API 与已发布版本不可原地改写，`MAJOR.MINOR.PATCH` 应显式表达兼容性含义；本轮据此冻结 `schema_version` 与 `required_abi` 的版本规则。
2. TUF Security Guidance：trust 不能永久授予，签名系统必须防御 rollback/freeze/key compromise；本轮据此冻结 trust anchor 读取职责、chain_status 集合与 signature freshness 约束。

D 结论：

1. 新增 docs/todos/infrastructure/deliverables/PLG-BLK-01-03-INF-BLK-09-plugin对象与校验链路冻结.md，统一回写 manifest/schema、签名链路和 ABI 矩阵冻结结论，以及 014~017 的 Design -> Build 映射。
2. 在 plugin 详细设计中新增 6.5.1、6.5.2、6.6.1，冻结 PluginManifest v1.0、SignatureReport / CompatibilityReport v1 与 trust/ABI 规则。
3. 在 infra/plugin 两级 TODO 台账中把 INF-BLK-09、PLG-BLK-01~03 迁移为 Resolved，并把 014~017 恢复为 Not Started。
4. 本轮不改 public code signature，因此 PLG-GATE-08 未触发；后续若 017 触及 aggregation public boundary，再单独走 breaking review。
5. D Gate：PASS。

### 27.3 验收与结果

验收命令：

1. `rg -n "schema_version|required_abi|ed25519|ecdsa-p256-sha256|trust_level_too_low|rollback_rejected|strict_mode=false|platform_tag" docs/architecture/DASALL_infra_plugin模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md`
2. `rg -n "INF-BLK-09|PLG-TODO-014|PLG-TODO-015|PLG-TODO-016|PLG-TODO-017" docs/worklog/DASALL_开发执行记录.md docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md`

结果：

1. INF-BLK-09 已由“缺设计”收敛为“已冻结的对象/规则基线”，014~017 不再需要等待额外跨域 blocker。
2. plugin 专项 TODO 与 infra 总 TODO 已同步恢复 014~017 的执行状态，可直接进入代码轮次。
3. 当前仍未解的只剩 PluginRuntimeBridge 平台装载细节；该问题不会再阻塞 014~017 的对象/接口冻结。

---

## 28. 本轮执行记录（2026-04-07 / PLG-TODO-014）

### 28.1 选中任务

1. 本轮任务：PLG-TODO-014。
2. 可执行性依据：INF-BLK-09 已在上一轮 shared blocker recovery 中解除；014 当前是 015~017 的最小前置对象任务，且可以在单轮内完成“public header + unit/contract + 证据回写”闭环。

### 28.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5.1 已冻结 PluginManifest v1.0 的字段集合、`required_abi` 编码与扩展命名空间。
2. infra/include/plugin/PluginDescriptor.h、PluginCatalog.h 与 infra/include/ota/OTATypes.h 已给出当前仓库的数据结构冻结风格：header-only + 显式一致性检查出口。
3. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 中 014 的本轮边界已收敛为“对象头文件 + unit/contract 守卫”，不要求 parser 或 manager 接线。

外部参考：

1. SemVer 2.0.0 要求公开版本语义明确且已发布版本不可原地改写；本轮据此把 `schema_version` 与 `version` 都冻结为可校验的 SemVer 语义。

D 结论：

1. 新增 docs/todos/infrastructure/deliverables/PLG-TODO-014-PluginManifest设计收敛.md，明确 schema v1.0、extension namespace 与 Design -> Build 映射。
2. Build 三件套锁定为：
        - 代码目标：infra/include/plugin/PluginManifest.h。
        - 测试目标：PluginManifestSchemaTest、PluginManifestBoundaryContractTest。
        - 验收命令：cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_plugin_manifest_unit_test dasall_contract_plugin_manifest_boundary_test && ctest --test-dir build-ci -N -R "PluginManifestSchemaTest|PluginManifestBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginManifestSchemaTest|PluginManifestBoundaryContractTest"。
3. D Gate：PASS。

### 28.3 Build 交付与证据

交付物：

1. infra/include/plugin/PluginManifest.h：新增 PluginManifest、PluginManifestExtension 以及 schema_version、SemVer、`required_abi` 与 extension namespace 的一致性检查出口。
2. tests/unit/infra/plugin/PluginManifestTest.cpp：覆盖默认 unknown、有效 v1 schema 正例，以及 reserved extension owner / malformed `required_abi` 负例。
3. tests/contract/smoke/PluginManifestBoundaryContractTest.cpp：覆盖不吸收 request/trace/task/tool/skill 语义的边界守卫。
4. infra/CMakeLists.txt、tests/unit/infra/plugin/CMakeLists.txt、tests/contract/plugin/CMakeLists.txt：完成 public header 与 unit/contract 注册。
5. docs/todos/infrastructure/deliverables/PLG-TODO-014-PluginManifest设计收敛.md：记录本地证据、外部参考、Design -> Build 映射与风险边界。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_manifest_unit_test dasall_contract_plugin_manifest_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "PluginManifestSchemaTest|PluginManifestBoundaryContractTest"`：通过，发现 2 个测试。
4. `ctest --test-dir build-ci --output-on-failure -R "PluginManifestSchemaTest|PluginManifestBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：014 选择 header-only 数据结构与显式 helper 命名，代码自解释，无需额外冗余注释。
2. 正负例覆盖：unit 同时覆盖 schema 正例与 extension / `required_abi` 负例；contract 覆盖 contracts/tool/skill 语义越权守卫。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/contract 用例进入 CTest 图。
4. TODO 证据回写：已完成任务状态、验收命令、交付物路径与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 PluginManifest 对象、对应 unit/contract 测试、CMake 注册与 014 文档证据，不混入 015/016 的接口变更。

---

## 29. 本轮执行记录（2026-04-08 / PLG-TODO-015）

### 29.1 选中任务

1. 本轮任务：PLG-TODO-015。
2. 可执行性依据：PLG-TODO-014 已完成，shared blocker 已解除；015 当前是 016/017 的直接前置接口任务，且可在单轮内完成“public header + compile/boundary tests + 证据回写”闭环。

### 29.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5.2 已冻结 SignatureReport v1 字段；6.6.1 已冻结 allow-list、anchor purpose、trust level 次序与 rollback/freeze 语义。
2. docs/todos/infrastructure/deliverables/PLG-BLK-01-03-INF-BLK-09-plugin对象与校验链路冻结.md 已明确 015 的 Build 目标是“verifier public header + 最小输入输出对象 + compile/boundary tests”。
3. infra/include/plugin/IPluginPolicyGate.h 与 infra/include/ota/IOTAPackageVerifier.h 已给出当前仓库的接口冻结风格：最小输入对象 + 可二值判定输出对象。

外部参考：

1. The Update Framework 对未知算法、trust anchor 缺失与 rollback/freeze 风险的防御要求，支撑本轮把 allow-list、anchor purpose 与 last known good version 统一收口到 verifier 最小输入对象。

D 结论：

1. 新增 docs/todos/infrastructure/deliverables/PLG-TODO-015-IPluginSignatureVerifier设计收敛.md，明确 verifier boundary、trust anchor 输入面与 Design -> Build 映射。
2. Build 三件套锁定为：
        - 代码目标：infra/include/plugin/IPluginSignatureVerifier.h。
        - 测试目标：PluginSignatureVerifierInterfaceCompileTest、PluginSignatureVerifierBoundaryContractTest。
        - 验收命令：cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_plugin_signature_verifier_interface_unit_test dasall_contract_plugin_signature_verifier_boundary_test && ctest --test-dir build-ci -N -R "PluginSignatureVerifierInterfaceCompileTest|PluginSignatureVerifierBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginSignatureVerifierInterfaceCompileTest|PluginSignatureVerifierBoundaryContractTest"。
3. D Gate：PASS。

### 29.3 Build 交付与证据

交付物：

1. infra/include/plugin/IPluginSignatureVerifier.h：新增 PluginSignatureChainStatus、PluginTrustAnchorMaterial、PluginSignatureVerificationRequest、SignatureReport 与 IPluginSignatureVerifier。
2. tests/unit/infra/plugin/PluginSignatureVerifierInterfaceTest.cpp：覆盖成功、algorithm_unsupported 与 rollback_rejected 三类路径。
3. tests/contract/smoke/PluginSignatureVerifierBoundaryContractTest.cpp：覆盖 anchor purpose 冻结、allow-list 冻结以及“无原始密钥/证书链字段”边界守卫。
4. infra/CMakeLists.txt、tests/unit/infra/plugin/CMakeLists.txt、tests/contract/plugin/CMakeLists.txt：完成 public header 与定向测试注册。
5. docs/todos/infrastructure/deliverables/PLG-TODO-015-IPluginSignatureVerifier设计收敛.md：记录本地证据、外部参考、Design -> Build 映射与风险边界。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_signature_verifier_interface_unit_test dasall_contract_plugin_signature_verifier_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "PluginSignatureVerifierInterfaceCompileTest|PluginSignatureVerifierBoundaryContractTest"`：通过，发现 2 个测试。
4. `ctest --test-dir build-ci --output-on-failure -R "PluginSignatureVerifierInterfaceCompileTest|PluginSignatureVerifierBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：015 选择 header-only interface boundary 与显式 helper 命名，代码自解释，无需新增冗余注释。
2. 正负例覆盖：unit 覆盖 verified 正例与 algorithm_unsupported / rollback_rejected 负例；contract 覆盖 trust anchor 与无 crypto blob 边界。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/contract 用例进入 CTest 图。
4. TODO 证据回写：已完成任务状态、验收命令、交付物路径与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 IPluginSignatureVerifier public header、定向 unit/contract、CMake 注册与 015 文档证据，不混入 016/017 的 compatibility/report 变更。

---

## 30. 本轮执行记录（2026-04-08 / PLG-TODO-016）

### 30.1 选中任务

1. 本轮任务：PLG-TODO-016。
2. 可执行性依据：PLG-TODO-015 已完成，shared blocker 已解除；016 当前是 017 的直接前置接口任务，且可在单轮内完成“public header + ABI matrix tests + 证据回写”闭环。

### 30.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5.2 已冻结 CompatibilityReport v1 字段；6.6.1 已冻结 platform tag allow-list、host ABI 快照最小字段与 strict/non-strict 规则。
2. docs/todos/infrastructure/deliverables/PLG-BLK-01-03-INF-BLK-09-plugin对象与校验链路冻结.md 已明确 016 的 Build 目标是“compatibility engine public header + host ABI snapshot / compatibility report + matrix tests”。
3. infra/src/ota/ArtifactCompatibilityEvaluator.h 已给出当前仓库的 compatibility gate 类型风格：snapshot 输入 + 二值 report 输出。

外部参考：

1. GNU triplet 与 SemVer 兼容规则的组合能够稳定表达平台与 ABI 版本边界；本轮据此把 platform tag allow-list、strict/non-strict 比较规则与 host snapshot 固定为 compatibility engine 的最小输入面。

D 结论：

1. 新增 docs/todos/infrastructure/deliverables/PLG-TODO-016-IPluginCompatibilityEngine设计收敛.md，明确 compatibility boundary、host ABI snapshot 与 Design -> Build 映射。
2. Build 三件套锁定为：
        - 代码目标：infra/include/plugin/IPluginCompatibilityEngine.h。
        - 测试目标：PluginCompatibilityEngineInterfaceCompileTest、PluginCompatibilityEngineBoundaryContractTest。
        - 验收命令：cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_plugin_compatibility_engine_interface_unit_test dasall_contract_plugin_compatibility_engine_boundary_test && ctest --test-dir build-ci -N -R "PluginCompatibilityEngineInterfaceCompileTest|PluginCompatibilityEngineBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "PluginCompatibilityEngineInterfaceCompileTest|PluginCompatibilityEngineBoundaryContractTest"。
3. D Gate：PASS。

### 30.3 Build 交付与证据

交付物：

1. infra/include/plugin/IPluginCompatibilityEngine.h：新增 PluginHostAbiSnapshot、PluginDependencyMatrixSnapshot、PluginCompatibilityCheckRequest、CompatibilityReport 与 IPluginCompatibilityEngine。
2. tests/unit/infra/plugin/PluginCompatibilityEngineInterfaceTest.cpp：覆盖 strict patch forward 正例、strict/non-strict minor matrix，以及 major mismatch + API/dependency 负例。
3. tests/contract/smoke/PluginCompatibilityEngineBoundaryContractTest.cpp：覆盖 platform tag allow-list、dependency snapshot 去重和“无 runtime/policy 内部字段”边界守卫。
4. infra/CMakeLists.txt、tests/unit/infra/plugin/CMakeLists.txt、tests/contract/plugin/CMakeLists.txt：完成 public header 与定向测试注册。
5. docs/todos/infrastructure/deliverables/PLG-TODO-016-IPluginCompatibilityEngine设计收敛.md：记录本地证据、外部参考、Design -> Build 映射与风险边界。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_compatibility_engine_interface_unit_test dasall_contract_plugin_compatibility_engine_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "PluginCompatibilityEngineInterfaceCompileTest|PluginCompatibilityEngineBoundaryContractTest"`：通过，发现 2 个测试。
4. `ctest --test-dir build-ci --output-on-failure -R "PluginCompatibilityEngineInterfaceCompileTest|PluginCompatibilityEngineBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：016 选择 header-only compatibility boundary 与显式 matrix helper 命名，代码自解释，无需新增冗余注释。
2. 正负例覆盖：unit 覆盖 strict forward patch 正例、strict/non-strict matrix 差异与 major/API/dependency 负例；contract 覆盖 platform tag 与 dependency snapshot 边界。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/contract 用例进入 CTest 图。
4. TODO 证据回写：已完成任务状态、验收命令、交付物路径与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 IPluginCompatibilityEngine public header、定向 unit/contract、CMake 注册与 016 文档证据，不混入 017 的 aggregation 变更。

---

## 31. 本轮执行记录（2026-04-08 / PLG-TODO-017）

### 31.1 选中任务

1. 本轮任务：PLG-TODO-017。
2. 可执行性依据：PLG-TODO-014/015/016 已完成，shared blocker 已解除；017 当前是 plugin 对象与接口冻结续航中的最后一个串行任务，且可在单轮内完成“shared report public header + validation aggregation tests + 证据回写”闭环。

### 31.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5.2 已冻结 SignatureReport / CompatibilityReport v1 字段；6.8 已要求 validate 失败路径返回可观测证据并保持 contracts 边界稳定。
2. PLG-TODO-015 与 PLG-TODO-016 已分别在 verifier / compatibility engine 中冻结最小 report 输出对象，但 IPluginManager 与 PluginValidationPipeline 仍停留在 ref-only aggregation。
3. tests/unit/infra/plugin/PluginManagerInterfaceTest.cpp、PluginValidationPipelineTest.cpp 与对应 contract tests 已构成当前 validation aggregation 的正式边界守卫，因此 017 必须在保持 ref traceability 的前提下新增 shared report object，而不是直接替换既有 ref 字段。

外部参考：

1. SemVer 2.0.0 要求对公开 API 的兼容演进保持向后兼容；本轮据此选择新增 optional shared report object，并保留既有 `*_report_ref` 字段，避免把 017 做成破坏性替换。

D 结论：

1. 新增 docs/todos/infrastructure/deliverables/PLG-TODO-017-PluginReports与聚合收敛.md，明确 shared report header、optional aggregation 与 Design -> Build 映射。
2. Build 三件套锁定为：
        - 代码目标：infra/include/plugin/PluginReports.h、IPluginManager.h、PluginValidationPipeline.h/.cpp。
        - 测试目标：PluginReportsTest、PluginReportsBoundaryContractTest、PluginManagerInterfaceCompileTest、PluginValidationPipelineTest、PluginManagerBoundaryContractTest、PluginValidationPipelineBoundaryContractTest。
        - 验收命令：cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_plugin_signature_verifier_interface_unit_test dasall_plugin_compatibility_engine_interface_unit_test dasall_plugin_reports_unit_test dasall_plugin_manager_interface_unit_test dasall_plugin_validation_pipeline_unit_test dasall_contract_plugin_signature_verifier_boundary_test dasall_contract_plugin_compatibility_engine_boundary_test dasall_contract_plugin_reports_boundary_test dasall_contract_plugin_manager_boundary_test dasall_contract_plugin_validation_pipeline_boundary_test && ctest --test-dir build-ci -N -R "Plugin(SignatureVerifierInterfaceCompileTest|CompatibilityEngineInterfaceCompileTest|ReportsTest|ManagerInterfaceCompileTest|ValidationPipelineTest|SignatureVerifierBoundaryContractTest|CompatibilityEngineBoundaryContractTest|ReportsBoundaryContractTest|ManagerBoundaryContractTest|ValidationPipelineBoundaryContractTest)" && ctest --test-dir build-ci --output-on-failure -R "Plugin(SignatureVerifierInterfaceCompileTest|CompatibilityEngineInterfaceCompileTest|ReportsTest|ManagerInterfaceCompileTest|ValidationPipelineTest|SignatureVerifierBoundaryContractTest|CompatibilityEngineBoundaryContractTest|ReportsBoundaryContractTest|ManagerBoundaryContractTest|ValidationPipelineBoundaryContractTest)"。
3. D Gate：PASS。

### 31.3 Build 交付与证据

交付物：

1. infra/include/plugin/PluginReports.h：统一落盘 PluginSignatureChainStatus、算法 allow-list、SignatureReport 与 CompatibilityReport，消除 015/016 之间的 report 重复定义。
2. infra/include/plugin/IPluginSignatureVerifier.h、infra/include/plugin/IPluginCompatibilityEngine.h：改为复用 shared report header，保持既有接口签名不变。
3. infra/include/plugin/IPluginManager.h：在保留 `signature_report_ref` / `compatibility_report_ref` 的同时，新增 optional shared report objects，并把 `has_traceable_refs()` 收紧为“有 object 时必须有 ref 且 object 有效”。
4. infra/src/plugin/PluginValidationPipeline.h/.cpp：在 stage/result 级别接入 optional shared reports，保持失败分支与成功分支都能返回 object + ref 双承载的聚合结果。
5. tests/unit/infra/plugin/PluginReportsTest.cpp 与 tests/contract/smoke/PluginReportsBoundaryContractTest.cpp：冻结 shared report 对象本身的正负例与 boundary 约束。
6. tests/unit/infra/plugin/PluginManagerInterfaceTest.cpp、tests/unit/infra/plugin/PluginValidationPipelineTest.cpp、tests/contract/smoke/PluginManagerBoundaryContractTest.cpp、tests/contract/smoke/PluginValidationPipelineBoundaryContractTest.cpp：同步验证 manager/pipeline 的 optional object + ref 双承载聚合边界。
7. infra/CMakeLists.txt、tests/unit/infra/plugin/CMakeLists.txt、tests/contract/plugin/CMakeLists.txt：完成 shared report public header 与新增 unit/contract 用例注册。
8. docs/todos/infrastructure/deliverables/PLG-TODO-017-PluginReports与聚合收敛.md：记录设计结论、Design -> Build 映射与风险边界。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_plugin_signature_verifier_interface_unit_test dasall_plugin_compatibility_engine_interface_unit_test dasall_plugin_reports_unit_test dasall_plugin_manager_interface_unit_test dasall_plugin_validation_pipeline_unit_test dasall_contract_plugin_signature_verifier_boundary_test dasall_contract_plugin_compatibility_engine_boundary_test dasall_contract_plugin_reports_boundary_test dasall_contract_plugin_manager_boundary_test dasall_contract_plugin_validation_pipeline_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "Plugin(SignatureVerifierInterfaceCompileTest|CompatibilityEngineInterfaceCompileTest|ReportsTest|ManagerInterfaceCompileTest|ValidationPipelineTest|SignatureVerifierBoundaryContractTest|CompatibilityEngineBoundaryContractTest|ReportsBoundaryContractTest|ManagerBoundaryContractTest|ValidationPipelineBoundaryContractTest)"`：通过，发现 10 个测试。
4. `ctest --test-dir build-ci --output-on-failure -R "Plugin(SignatureVerifierInterfaceCompileTest|CompatibilityEngineInterfaceCompileTest|ReportsTest|ManagerInterfaceCompileTest|ValidationPipelineTest|SignatureVerifierBoundaryContractTest|CompatibilityEngineBoundaryContractTest|ReportsBoundaryContractTest|ManagerBoundaryContractTest|ValidationPipelineBoundaryContractTest)"`：通过，10/10 tests passed。

Build 合规复核：

1. 向后兼容：017 保留既有 report ref 字段，只新增 optional shared report object，未破坏 015/016/005 已冻结的 traceability surface。
2. 正负例覆盖：unit 覆盖 shared report 正负例、manager success 聚合与 pipeline policy/signature/compatibility 三类分支；contract 覆盖 shared report 无 contracts/policy 泄漏、manager aggregation 与 pipeline aggregation 边界。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增/受影响的 10 个 interface/unit/contract 用例进入 CTest 图。
4. TODO 证据回写：已完成任务状态、验收命令、交付物路径与本轮执行记录回写。
5. 提交隔离：本轮提交范围限定为 shared report header、manager/pipeline aggregation、对应 unit/contract 与 017 文档证据，不混入后续 load/runtime bridge 实现。

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
| v1.8 | 2026-04-07 | 回写 PLG-TODO-010 的 contract 注册入口收口与 discoverability 证据，使 plugin contract 用例具备组件级 helper 与标签 | （待评审） |
| v1.9 | 2026-04-07 | 回写 PLG-TODO-005 的 validation pipeline 骨架与失败枝条验证证据，并修复 plugin 私有源文件未真正进入 dasall_infra 的构建接线缺口 | （待评审） |
| v1.10 | 2026-04-07 | 回写 PLG-TODO-006 的 PluginAuditAdapter 审计适配层、unit/integration 验证证据，并同步修正已完成上游依赖状态 | （待评审） |
| v1.11 | 2026-04-07 | 回写 PLG-TODO-011 的生命周期骨架、safe_mode/failed cleanup 状态机验证证据，并修正 LifecycleManager 粒度与 runtime bridge 依赖描述 | （待评审） |
| v1.12 | 2026-04-07 | 回写 PLG-TODO-012 的 failure injection/observability 验证证据，补齐 validation failure audit 接线与 plugin integration discoverability | （待评审） |
| v1.13 | 2026-04-07 | 回写 PLG-TODO-013 的 profile plugin schema 收敛与三档治理矩阵验证证据，补齐五档 runtime_policy 对齐与 contract/integration 守卫 | （待评审） |
| v1.14 | 2026-04-07 | 回写 INF-BLK-09 的 shared blocker recovery：冻结 manifest/signature/ABI 规则，校准两级 TODO 台账，并将 PLG-TODO-014~017 从 Blocked 迁移为 Not Started | （待评审） |
| v1.15 | 2026-04-07 | 回写 PLG-TODO-014 的 PluginManifest 对象、unit/contract 证据与执行记录，并将下一步切换到 PLG-TODO-015 | （待评审） |
| v1.16 | 2026-04-08 | 回写 PLG-TODO-015 的 IPluginSignatureVerifier 边界、compile/boundary 验证证据与执行记录，并将下一步切换到 PLG-TODO-016 | （待评审） |
| v1.17 | 2026-04-08 | 回写 PLG-TODO-016 的 IPluginCompatibilityEngine 边界、ABI matrix/boundary 验证证据与执行记录，并将下一步切换到 PLG-TODO-017 | （待评审） |
| v1.18 | 2026-04-08 | 回写 PLG-TODO-017 的 shared report header、validation aggregation unit/contract 证据与执行记录，并将 Phase 6 标记为完成 | （待评审） |

---

## 附录：与 infra 子系统 TODO 的对齐关系

### A1. INF-TODO-019 与此文档的对应关系

| infra 子系统 TODO | plugin 专项 TODO | 对应关系 |
|---|---|---|
| INF-TODO-019（开始） | PLG-TODO（全系列） | INF-TODO-019 是 infra 体系统筹，PLG-TODO 是具体拆解 |
| INF-BLK-09（已解阻） | PLG-BLK-01/02/03/05 | 同一根本原因（manifest/ABI/signature 未冻结）的衍生阻塞，现已完成 shared blocker recovery |
| INF-TODO-017（SecurityPolicyManager） | PLG-TODO-004、PLG-TODO-005 | 依赖关系 |
| INF-TODO-016（AuditService） | PLG-TODO-006、PLG-TODO-012 | 依赖关系 |

### A2. 如何响应"请立即推进 PLG-TODO"的需求

当用户要求启动 plugin 专项 TODO 执行时，应：

1. **首先确认**当前状态是否允许开始（参见 4.1 当前可执行范围）
2. **确保前置**任务（INF-TODO-017、016）已完成或其冻结边界已可用
3. **优先级**：Phase 1-3 > Phase 4 > 解阻项
4. **并行建议**：Phase 1 三个对象可并行；Phase 2-3 可并行（以 Phase 1 完成为前置）
5. **每个任务完成标准**参见对应任务行的"完成判定"列

---

本文档作为 DASALL 项目正式的 plugin 组件专项 TODO，自此版本起，所有相关任务执行必须遵循此文档定义的目标、范围、粒度、阻塞与门禁条件。

若有变更需求，必须通过正式评审流程并更新此文档版本记录。
