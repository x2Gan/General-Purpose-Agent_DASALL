# DASALL profiles 模块详细设计（Detailed Design）

版本：v1.0  
日期：2026-03-24  
阶段：Detailed Design  
适用模块：profiles/

## 1. 模块概览

### 1.1 目标与定位

profiles 模块负责将架构文档中定义的 Build Profile 机制落为可执行工程结构。它不是新的业务子系统，也不是运行时决策器，而是同时覆盖两层能力：

1. 编译裁剪：决定哪些模块、适配器、工具集和观测能力进入构建产物。
2. 运行治理：为 Runtime、LLM、Tools、Memory、Knowledge、Infra 提供按档位冻结的策略基线。

profiles 的设计目标是保证“同一主流程，多档位复用”，通过配置、注册表和依赖注入表达差异，而不是在主流程中散落平台分支。

来源依据：
1. docs/architecture/DASSALL_Agent_architecture.md 第 7.5 节
2. docs/architecture/DASALL_Engineering_Blueprint.md 第 3.13 节、第 5 章
3. docs/adr/ADR-005-architecture-review-baseline.md

### 1.2 边界定义

上游输入：
1. 构建入口与 CI：CMake 配置参数、交叉编译工具链、发布形态选择。
2. apps 启动入口：显式指定的 profile_id。
3. infra/config：部署覆盖与运行时覆盖。

下游消费者：
1. runtime：预算、降级、超时、安全模式、模式切换边界。
2. llm：model_profile、prompt_policy、timeout_policy。
3. tools：execution_policy、tool visibility、timeout_policy。
4. memory / knowledge：token_budget_policy、capability_cache_policy、是否启用向量与检索链路。
5. infra：ops_policy、审计等级、诊断可见性。
6. build system：enabled_modules、adapter 开关、目标平台参数。

非职责：
1. 不拥有 Agent 主状态机与运行裁定权。
2. 不重定义 contracts 共享语义对象。
3. 不替代 infra/config 做全局配置中心。
4. 不替代 runtime/recovery 做故障恢复决策。

### 1.3 与相邻模块依赖方向

| 相邻模块 | 依赖方向 | 说明 |
|---|---|---|
| contracts | profiles 消费，不能反向写入 | 仅消费稳定对象名与能力边界，不新增 profile 专属共享契约 |
| infra | profiles 依赖 infra/config 与 observability 抽象 | profile 作为配置层输入，不接管配置中心 |
| platform | build 侧依赖平台标签与 toolchain 信息 | 新平台优先补 platform adapter + profile |
| runtime | runtime 消费 RuntimePolicySnapshot | profiles 提供策略，不做主控裁定 |
| llm/tools/memory/knowledge/services | 各模块消费 profile 解析结果 | 不允许反向把模块内部结构写回 profiles 基线 |

### 1.4 设计范围

纳入范围：
1. profile 资产结构。
2. profile 解析、校验、激活、覆盖与观测机制。
3. Build 和 Runtime 双平面映射。
4. 测试与质量门、实施拆分、演进策略。

不纳入范围：
1. 各业务模块的内部算法实现。
2. contracts 字段级冻结本体。
3. 具体模型路由算法、工具执行算法、恢复算法。

---

## 2. 约束清单

### 2.1 Must / Should / Must-Not 约束表

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| PRF-C001 | DASSALL_Agent_architecture.md 7.5 | Must | 每个 Profile 必须同时覆盖编译裁剪与运行治理两层 | 子组件、配置模型 |
| PRF-C002 | DASSALL_Agent_architecture.md 7.5 | Must | 每个 Profile 至少冻结 target_platform、enabled_modules、runtime_budget、model_route、execution_policy、ops_policy | 配置域、验证 |
| PRF-C003 | DASSALL_Agent_architecture.md 7.5 | Must | Profile 差异必须通过配置、注册表和依赖注入落地，不在主流程中散落平台分支 | 接口、流程 |
| PRF-C004 | DASSALL_Agent_architecture.md 7.5 | Must-Not | Profile 不得绕过 Policy Gate、Audit 和 Runtime 主控链路 | 运行治理、测试 |
| PRF-C005 | DASALL_Engineering_Blueprint.md 3.13/5.1 | Must | 同一 contracts 层对象在不同 Profile 中保持一致 | contracts 对齐、兼容性 |
| PRF-C006 | DASALL_Engineering_Blueprint.md 4.2 | Must-Not | profiles 不得诱导上层形成跨模块实现闭环 | 依赖治理 |
| PRF-C007 | ADR-005-architecture-review-baseline.md | Must | 在 contracts 与关键边界未冻结前，不得用 profile 设计反向改写系统边界 | 方案治理 |
| PRF-C008 | ADR-006-context-orchestrator-vs-prompt-composer.md | Must-Not | profiles 只能提供 token/prompt 策略参数，不能接管 ContextPacket 组装或消息渲染职责 | llm、memory 边界 |
| PRF-C009 | ADR-007-reflection-engine-vs-recovery-manager.md | Must-Not | profiles 只能配置 retry/timeout/degrade 阈值，不能持有失败语义解释权或恢复执行权 | recovery 边界 |
| PRF-C010 | ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | Must-Not | profiles 只能启停 multi_agent 能力与预算，不得产生第二调度中心 | runtime、多 Agent 边界 |
| PRF-C011 | DASALL_contracts冻结实施计划.md 4/5/6 | Must-Not | 不把 build 开关、YAML 结构、adapter 选择等实现细节写入 contracts 共享对象 | contracts 边界 |
| PRF-C012 | DASALL_contracts冻结TODO总表.md 5/8 | Must | 向后兼容优先；profile 配置 schema 扩展优先新增字段，不直接修改旧语义 | 版本演进 |
| PRF-C013 | DASALL_infrastructure子系统详细设计.md 2/6 | Must | profile 层必须作为 infra ConfigCenter 的 Profile 层输入，而不是旁路配置体系 | 配置层次 |
| PRF-C014 | DASALL_工程协作与编码规范.md 3.6/3.7 | Must | profile 激活、校验、回退失败必须可观测；新增公共接口同步补测试 | 错误处理、测试 |
| PRF-C015 | 行业参考：External Configuration Store / CMake Presets | Should | 配置应支持显式版本、分层继承、最后已知正确配置回退与共享构建入口 | 运行治理、工程化 |

### 2.2 约束抽取结论

Must：
1. profiles 必须是 Build + Runtime 双平面机制，而不是单纯宏集合。
2. profiles 只能提供能力裁剪与策略基线，不能越权到主控、认知、恢复语义。
3. profiles 必须与 contracts 冻结策略兼容，不能制造 profile 专属共享语义。

Should：
1. 支持显式 schema_version。
2. 支持 last-known-good 回退。
3. 支持配置继承与统一 build/test 入口。

Must-Not：
1. 不改写 ADR。
2. 不绕过 Runtime / PolicyGate / Audit。
3. 不把模块内部实现细节反向上推到 contracts。

---

## 3. 现状与缺口

### 3.1 现状识别

当前 profiles 目录已存在 5 个档位：desktop_full、cloud_full、edge_balanced、edge_minimal、factory_test，但仍处于“资产占位”状态：

1. 每个 profile.cmake 仅声明 DASALL_PROFILE_NAME。
2. 每个 runtime_policy.yaml 仅保留 profile 名称、空 enabled_modules 和空 runtime_budget。
3. 仓库内尚无统一 ProfileCatalog、校验器、加载器、模块矩阵校验测试。
4. 架构文档中的 resolve_runtime_policy 调用仅存在于说明性伪代码层，尚未形成工程实现。

### 3.2 现状-目标差距表

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| 5 个 profile 具备完整策略域 | 占位 | 仅有空壳 YAML/CMake，缺少冻结策略内容 | High | P0 |
| Build 侧统一解析 profile | 缺失 | 没有通用 BuildProfileResolver 与模块矩阵验证 | High | P0 |
| Runtime 侧统一加载 profile | 缺失 | 没有 RuntimePolicyProvider、OverlayComposer、LKG 机制 | High | P0 |
| Profile schema 与兼容校验 | 缺失 | 配置字段无版本、无校验、无拒绝策略 | High | P0 |
| 与 infra/config 层次对齐 | 部分明确、未实现 | 架构上已定义四层配置，但 profile 层接线未落地 | Medium | P1 |
| 与各子系统的消费边界对齐 | 文档明确、未落地 | llm/tools/memory/runtime 还没有稳定消费接口 | Medium | P1 |
| 模块启用矩阵自动校验 | 缺失 | 蓝图有矩阵，仓库无自动检查器 | Medium | P1 |
| 单测/集成/失败注入覆盖 | 缺失 | 没有任何 profile 专项测试 | High | P0 |

### 3.3 风险冲突识别

| 冲突类型 | 描述 | 影响 | 风险等级 |
|---|---|---|---|
| 边界冲突 | profiles 若直接决定恢复动作或多 Agent 调度，将侵入 runtime 职责 | 破坏 ADR-007 / ADR-008 | High |
| 语义重复 | 若将 profile_id、enabled_modules 写入 contracts 共享对象，会污染 contracts | 增加冻结返工 | High |
| 依赖反转 | 若各模块各自读取 YAML，不经过统一 provider，会形成重复解析与逻辑漂移 | 难以维护与验证 | High |
| 平台分支扩散 | 若通过 if/else 分散到 runtime、tools、llm 内部实现 | 主流程分叉，破坏可裁剪性 | High |
| 配置漂移 | 若 CMake 侧和 YAML 侧各自独立维护启用矩阵 | Build 与 Runtime 不一致 | High |

---

## 4. 候选方案对比

### 4.1 候选方案说明

#### 方案 A：分散式直读方案

设计思路：
1. 每个消费方直接读取对应 profile.cmake 或 runtime_policy.yaml。
2. CMake、Runtime、LLM、Tools 各自维护自己的字段解析逻辑。

组件结构：
1. profile.cmake
2. runtime_policy.yaml
3. 各模块私有读取器

优点：
1. 初期编码量最少。
2. 不需要新增公共解析层。

风险：
1. 解析逻辑重复，字段漂移高。
2. Build 和 Runtime 一致性难校验。
3. 边界很容易被各模块私有扩写。

与 DASALL 约束匹配度：低。

#### 方案 B：集中式 Profile Catalog + 双阶段解析方案

设计思路：
1. 建立统一的 ProfileCatalog 管理 profile 元数据。
2. Build 侧通过 BuildProfileResolver 输出构建开关、模块矩阵和 toolchain 选择。
3. Runtime 侧通过 RuntimePolicyProvider 加载、校验并生成不可变 RuntimePolicySnapshot。
4. 通过 OverlayComposer 与 infra/config 四层合并机制对接。

组件结构：
1. ProfileCatalog
2. BuildProfileResolver
3. RuntimePolicyProvider
4. ProfileOverlayComposer
5. ProfileCompatibilityValidator
6. ProfileTelemetryAdapter

优点：
1. 架构边界清晰。
2. Build / Runtime 一致性可验证。
3. 便于做 schema 演进、LKG 回退与矩阵测试。

风险：
1. 初期实现多一个中间层。
2. 需要提前统一 schema 与模块标识。

与 DASALL 约束匹配度：高。

#### 方案 C：编译期固化生成方案

设计思路：
1. 以 profile.cmake 为唯一真源，在配置阶段生成 C++ 常量和静态配置头。
2. 运行时不再读取 YAML，只消费编译期生成的只读对象。

组件结构：
1. 生成脚本
2. 生成的 ProfileConfig.h / ProfileConfig.cpp
3. 每档位独立 build 目录

优点：
1. 运行时简单。
2. 二进制与配置强绑定，适合极端裁剪。

风险：
1. 与 infra/config 覆盖层耦合差，运行时可调能力弱。
2. factory_test、cloud_full 等需要部署覆盖的场景不灵活。
3. 灰度和 last-known-good 回退成本高。

与 DASALL 约束匹配度：中。

### 4.2 候选方案对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| 方案 A 分散式直读 | 低 | 低 | 低 | 配置漂移、重复解析、边界失控 | 淘汰：不满足长期演进与测试可验证性 |
| 方案 B 集中式 Catalog + 双阶段解析 | 高 | 高 | 中 | 需要先统一 schema 和接口 | 保留并采纳：最符合 DASALL 双平面设计 |
| 方案 C 编译期固化生成 | 中 | 中高 | 中高 | 运行时灵活性不足、部署覆盖困难 | 暂不采纳：可作为 edge_minimal 后续增强路径 |

### 4.3 行业实践可借鉴结论

1. External Configuration Store 模式表明配置应具备分层、版本化、缓存与最后已知正确值回退能力，适合作为 RuntimePolicyProvider 的设计依据。
2. CMake Presets 证明“共享的 configure/build/test 入口 + 继承机制”适合做 BuildProfileResolver 的工程化承载，不必只依赖手写命令。
3. Self-healing、Retry、Circuit Breaker 模式表明 profile 可以冻结阈值和 degrade 策略，但恢复裁定仍必须留在 runtime。
4. Bazel 平台/工具链实践说明平台标签、工具链选择和功能矩阵应集中建模，而不是散落在各模块的条件判断中。

---

## 5. 决策结论

### 5.1 最终选型

采纳方案 B：集中式 Profile Catalog + 双阶段解析方案。

### 5.2 选择依据

1. 与架构一致：符合“编译裁剪 + 运行治理”双平面机制，不引入新的业务主控。
2. 与 ADR 一致：只提供策略基线，不侵入 Context、Recovery、Orchestrator 等边界。
3. 与 contracts 一致：profile 配置保持在模块私有对象，不新增共享语义对象。
4. 与工程现状一致：可以在现有 5 个占位档位上增量落地，不需要重建目录结构。
5. 与测试诉求一致：可对 Build 解析、Runtime 加载、矩阵一致性、回退机制分别建测试。

### 5.3 放弃其他候选方案的理由

1. 放弃方案 A：重复解析必然导致 Build / Runtime 漂移，且无法稳定维护模块启用矩阵。
2. 放弃方案 C：虽然适合极简部署，但会削弱部署覆盖、运行时 override 和工厂诊断场景的工程弹性。

### 5.4 一致性说明

与架构一致：
1. 保持 Profile 作为跨层治理机制，而非业务模块。
2. 维持“同一主流程，不分叉代码路径”的原则。

与 ADR 一致：
1. ADR-006：profiles 只给出 prompt/token 策略，不生成消息。
2. ADR-007：profiles 只冻结 timeout/retry/degrade 阈值，不拥有恢复执行权。
3. ADR-008：profiles 只允许启停 multi_agent 及其预算，不形成二级 orchestrator。

与 contracts 一致：
1. profile_id、模块矩阵、YAML schema、build 开关均属于模块私有配置，不进入 contracts。
2. 不同 profile 对同一 contracts 对象只允许“是否使用”差异，不允许“字段语义”差异。

---

## 6. 详细设计

### 6.1 职责边界

profiles 模块职责：
1. 提供 profile 元数据目录与资产发现能力。
2. 输出 BuildProfileManifest 供 CMake/CI/发布流程使用。
3. 输出 RuntimePolicySnapshot 供 apps/runtime/各子系统消费。
4. 校验目标平台、模块矩阵、适配器集合与策略域完整性。
5. 在配置加载失败时执行 last-known-good 保留与显式拒绝策略。

profiles 模块非职责：
1. 不做配置存储后端。
2. 不做业务决策。
3. 不直接调用工具、模型、记忆或多 Agent 实现。
4. 不接收用户请求形成最终结果。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| ProfileCatalog | 发现、注册、列举所有 profile 及其资产路径、schema_version、适用平台 |
| BuildProfileResolver | 将 profile 解析为构建维度的模块开关、adapter 选择、工具链提示和测试标签 |
| RuntimePolicyProvider | 读取 runtime_policy.yaml 与 overlay，生成不可变 RuntimePolicySnapshot |
| ProfileOverlayComposer | 与 infra/config 对齐，负责 Profile 层与 deployment/runtime override 的合并与校验 |
| ProfileCompatibilityValidator | 校验平台匹配、模块矩阵一致性、适配器依赖与 schema 兼容性 |
| LastKnownGoodStore | 缓存最近一次通过校验的 RuntimePolicySnapshot 引用，供启动失败或热更新失败时回退 |
| ProfileTelemetryAdapter | 输出 profile 激活、校验、拒绝、回退相关的日志、指标、追踪、审计 |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| ProfileCatalog | profiles/* 目录、schema_version、profile_id | BuildResolver、RuntimeProvider | 不解析业务字段，只保证资产发现正确 |
| BuildProfileResolver | profile_id、目标平台、交叉编译上下文 | CMake cache variables、模块启用矩阵、构建标签 | 输出只影响构建，不改变运行时共享语义 |
| RuntimePolicyProvider | profile_id、runtime_policy.yaml、overlay 引用 | RuntimePolicySnapshot | 输出为不可变快照，供运行时消费 |
| ProfileOverlayComposer | 基线快照、部署覆盖、运行时覆盖 | 合并后的候选快照 | 非法覆盖被拒绝，不产生部分成功状态 |
| ProfileCompatibilityValidator | 候选快照、BuildProfileManifest、环境事实 | ValidationReport | 报告必须显式区分 blocking / warning |
| LastKnownGoodStore | 已验证快照 | 快照引用 | 只保存已通过校验版本 |
| ProfileTelemetryAdapter | 激活事件、失败事件、回退事件 | 日志/指标/trace/audit | 所有高影响事件都要带 profile_id 与 reason_code |

### 6.4 子组件依赖关系

1. ProfileCatalog 为 BuildProfileResolver 和 RuntimePolicyProvider 的共同上游。
2. RuntimePolicyProvider 在加载完成后必须经过 ProfileCompatibilityValidator。
3. OverlayComposer 仅负责合并，不负责最终接受；最终接受由 Validator 结论决定。
4. LastKnownGoodStore 只在 Validator 通过后更新。
5. ProfileTelemetryAdapter 对所有子组件提供观测出口，但不反向依赖业务模块。

### 6.5 核心对象与 contracts 对齐关系

以下对象均为 profiles 模块私有对象，不进入 contracts：

| 核心对象 | 关键字段 | 约束 | 与 contracts 对齐关系 |
|---|---|---|---|
| ProfileDescriptor | profile_id, schema_version, target_platform, asset_paths, support_level | profile_id 唯一；schema_version 必填 | 只引用稳定模块名与能力域，不引入共享语义 |
| BuildProfileManifest | enabled_modules, enabled_adapters, observability_level, build_tags, toolchain_hint | Build 与 Runtime 对同一模块启停必须一致 | 不改变 contracts 对象结构 |
| RuntimePolicySnapshot | generation, effective_profile_id, runtime_budget, model_profile, token_budget_policy, prompt_policy, capability_cache_policy, degrade_policy, timeout_policy, execution_policy, ops_policy | 一旦激活即不可变；仅整体替换 | 由 runtime 消费，但不写回 contracts |
| ValidationReport | blocking_errors, warnings, dependency_gaps, compatibility_state | 任何 blocking error 均拒绝激活 | 仅引用通用 ResultCode / ErrorInfo 映射 |
| ProfileActivationRecord | requested_profile_id, effective_profile_id, source, activation_mode, fallback_reason, snapshot_ref | 每次激活必须可审计 | 不成为 AgentResult 一部分 |

### 6.6 核心接口语义定义

建议头文件分布：profiles/include/

#### IProfileCatalog

1. list_profiles()
   - 语义：返回当前仓库可用 profile 描述列表。
   - 前置条件：目录扫描成功。
   - 后置条件：返回集合中 profile_id 唯一。
   - 错误语义：目录不可读返回 PRF_E_CATALOG_UNAVAILABLE。

2. get_profile(profile_id)
   - 语义：按 profile_id 获取 ProfileDescriptor。
   - 前置条件：profile_id 非空。
   - 后置条件：找到则返回确定路径与 schema_version。
   - 错误语义：不存在返回 PRF_E_PROFILE_NOT_FOUND。

#### IBuildProfileResolver

1. resolve_build_manifest(request)
   - 语义：将 profile 请求转换为 BuildProfileManifest。
   - 前置条件：profile 存在，target_platform 可判定。
   - 后置条件：manifest 中 enabled_modules 与 adapter 选择完整。
   - 错误语义：平台不匹配返回 PRF_E_PLATFORM_MISMATCH。

#### IRuntimePolicyProvider

1. load_snapshot(request)
   - 语义：加载指定 profile 的运行时策略基线。
   - 前置条件：runtime_policy.yaml 存在且 schema_version 受支持。
   - 后置条件：返回未合并 override 的基线快照。
   - 错误语义：YAML 缺失或格式错误返回 PRF_E_SCHEMA_INVALID。

2. activate_snapshot(request)
   - 语义：完成加载、合并、校验、LKG 更新与激活记录。
   - 前置条件：overlay 来源合法。
   - 后置条件：成功则返回生效快照；失败则保持当前快照不变。
   - 错误语义：override 不合法返回 PRF_E_OVERRIDE_INVALID；回退失败返回 PRF_E_LAST_KNOWN_GOOD_UNAVAILABLE。

#### IProfileCompatibilityValidator

1. validate(candidate, build_manifest, environment)
   - 语义：对候选快照进行平台、模块、adapter、策略域一致性校验。
   - 前置条件：candidate 完整。
   - 后置条件：输出 ValidationReport，显式区分 blocking 和 warning。
   - 错误语义：缺失必须字段或矩阵冲突返回 PRF_E_MODULE_INCOMPATIBLE / PRF_E_REQUIRED_ADAPTER_MISSING。

#### ILastKnownGoodStore

1. save(snapshot_ref)
2. load(profile_id)
   - 语义：保存/读取最近一次通过校验的快照引用。
   - 约束：仅允许保存 validate 通过后的快照。

建议的模块内错误码：
1. PRF_E_PROFILE_NOT_FOUND
2. PRF_E_CATALOG_UNAVAILABLE
3. PRF_E_SCHEMA_INVALID
4. PRF_E_PLATFORM_MISMATCH
5. PRF_E_MODULE_INCOMPATIBLE
6. PRF_E_REQUIRED_ADAPTER_MISSING
7. PRF_E_OVERRIDE_INVALID
8. PRF_E_LAST_KNOWN_GOOD_UNAVAILABLE

说明：以上错误码为 profiles 私有错误域，可映射到 contracts 的通用 ResultCode/ErrorInfo，但不新增 contracts 专属枚举项。

### 6.7 主流程时序

#### 主流程 A：Build 侧解析

1. 构建入口传入 DASALL_PROFILE。
2. ProfileCatalog 定位对应 profile.cmake 与 runtime_policy.yaml。
3. BuildProfileResolver 解析 profile.cmake 基线，并读取 RuntimePolicy 中需要与构建一致的模块启停字段。
4. ProfileCompatibilityValidator 校验 enabled_modules 与目标平台/toolchain 的组合合法性。
5. 输出 BuildProfileManifest，注入 CMake cache variables、compile definitions、测试标签与安装规则。

#### 主流程 B：Runtime 启动激活

1. apps 启动入口传入 profile_id。
2. RuntimePolicyProvider 从 ProfileCatalog 读取基线策略。
3. ProfileOverlayComposer 按“模块默认值 < Profile 基线 < deployment override < runtime override”生成候选快照。
4. ProfileCompatibilityValidator 对候选快照做 schema、平台、依赖、矩阵校验。
5. 校验通过后更新 LastKnownGoodStore。
6. 输出 RuntimePolicySnapshot 给 runtime、llm、tools、memory、knowledge、infra。
7. ProfileTelemetryAdapter 记录 activation success 事件。

#### 主流程 C：运行时覆盖刷新

1. infra/config 发布运行时覆盖变更事件。
2. RuntimePolicyProvider 拉取当前生效快照与新 override。
3. OverlayComposer 合并生成候选快照。
4. Validator 校验通过则原子替换快照；失败则拒绝替换并保留当前快照。
5. TelemetryAdapter 记录 reload_success 或 reload_rejected。

### 6.8 异常与恢复时序

#### 异常分类

1. 启动期硬错误：profile 不存在、schema 无法解析、目标平台不匹配。
2. 启动期软错误：存在 warning，但不影响激活。
3. 运行期覆盖错误：新 override 非法或依赖不满足。
4. 运行期外部依赖错误：例如目标 LLM adapter 当前不可用。

#### 恢复路径

1. 启动期硬错误且显式指定 profile_id：直接失败并拒绝启动，不允许静默切到其他 profile。
2. 启动期未显式指定 profile_id 且存在仓库级默认映射：允许按部署策略选择默认 profile，但必须写审计事件。
3. 运行期覆盖错误：保留当前快照，记录 reject reason，不更新 LKG。
4. 运行期依赖不可用：由 runtime 按 degrade_policy 执行降级；profiles 只提供阈值与备用路线，不直接触发动作。
5. 若当前快照损坏且存在 LKG：回退到 LKG，并把 activation_mode 标记为 fallback_lkg。

#### 失败兜底原则

1. 不允许“部分字段成功、部分字段忽略”的隐式生效。
2. 不允许显式 profile 请求静默切换到其他档位。
3. 所有回退都必须产生 ProfileActivationRecord 与审计日志。

### 6.9 配置项与默认策略

#### 配置分层原则

1. 模块默认值：由各消费者模块的默认配置提供，profiles 不持有全部默认值。
2. Profile 基线：由 profiles/<profile_id>/runtime_policy.yaml 提供。
3. 部署覆盖：由 infra/config 提供环境、站点、设备级覆盖。
4. 运行时覆盖：由受控运维命令或诊断入口注入。

#### 建议的 runtime_policy.yaml 逻辑域

| 逻辑域 | 说明 | 默认策略 |
|---|---|---|
| profile_meta | profile_id、schema_version、target_platform、support_level | 必填，缺失即拒绝 |
| enabled_modules | 模块启停与 adapter 开关 | 与蓝图矩阵一致 |
| runtime_budget | 线程、上下文窗口、工具并发、内存/时延阈值 | 档位越低越保守 |
| model_profile | stage -> route/fallback/streaming | desktop_full 最宽，edge_minimal 最保守 |
| token_budget_policy | max_input/output/history/compression_threshold | edge 档位压缩更强 |
| prompt_policy | allowed_prompt_releases、trusted_sources、tool_visibility_rules | edge_minimal 冻结最小可信集合 |
| capability_cache_policy | refresh_interval、expire_after、stale_read_allowed、failure_backoff | 边缘档位优先允许 stale read |
| degrade_policy | 模型不可用、MCP 不可用、预算超限时的回退路径 | 只描述策略，不直接执行 |
| timeout_policy | LLM/Tool/MCP/Workflow 超时、重试、熔断阈值 | 高交互档位偏短、批处理档位可适度放宽 |
| execution_policy | 确认门、安全模式、审计等级、允许工具域 | 任何档位都不得放宽高风险确认门槛 |
| ops_policy | 日志等级、指标粒度、trace 抽样、远程诊断开关、升级策略 | factory_test 诊断更强，edge_minimal 最小化 |

#### runtime_policy.yaml v1 冻结规则

1. 当前冻结版本固定为 `schema_version: 1`；未显式声明或声明为其他版本的配置，在进入 overlay 合并前即拒绝。
2. `schema_version: 1` 的顶层逻辑域固定为 `profile_meta`、`enabled_modules`、`runtime_budget`、`model_profile`、`token_budget_policy`、`prompt_policy`、`capability_cache_policy`、`degrade_policy`、`timeout_policy`、`execution_policy`、`ops_policy`；不得省略任一域。
3. `profile_meta` 必填键固定为 `profile_id`、`target_platform`、`support_level`；`enabled_modules` 中所有模块与 adapter 开关必须显式声明为布尔值，不允许用“缺省即关闭”表示可选能力。
4. `runtime_budget` 必填键固定为 `worker_threads`、`max_memory_mb`、`max_tokens`、`max_turns`、`max_tool_calls`、`max_latency_ms`、`max_replan_count`；所有值必须为正整数，且档位越低越保守。
5. `model_profile` 至少冻结 `planner` 与 `responder` 两个 stage，且每个 stage 必须显式给出 `route`、`fallback_route`、`streaming_enabled`；`timeout_policy` 必须同时覆盖 `llm`、`tool`、`mcp`、`workflow` 四类预算。
6. `prompt_policy`、`capability_cache_policy`、`degrade_policy`、`execution_policy`、`ops_policy` 中的必填键不得依赖运行时推断；新增字段只允许追加，不允许在 `schema_version: 1` 内重解释既有字段语义。
7. `multi_agent`、`tools_mcp`、`llm_cloud_adapter` 等蓝图中标注为“可选”的能力，在具体档位资产中也必须冻结为显式基线值；若后续需要放开，只能通过新增字段或更高版本 schema 处理。

#### 各参考档位默认意图

| Profile | 默认策略摘要 |
|---|---|
| desktop_full | 全能力、多模型路由、完整观测、多 Agent 开启 |
| cloud_full | 云模型优先、完整能力、运维增强、可关闭本地冗余路径 |
| edge_balanced | LAN 主路、云端回退、工具并发受控、检索和向量能力可开 |
| edge_minimal | 本地轻量模型主路、精简工具集、关闭高成本检索与多 Agent |
| factory_test | 诊断与审计可见性增强、执行链路保留、高风险确认门槛不放宽 |

### 6.10 可观测性

#### 日志

关键日志点：
1. profile_discovery_started / completed
2. build_profile_resolved
3. runtime_policy_loaded
4. runtime_policy_validation_failed
5. runtime_policy_activated
6. runtime_policy_reload_rejected
7. runtime_policy_fallback_lkg

关键字段：
1. profile_id
2. effective_profile_id
3. schema_version
4. activation_mode
5. reason_code
6. trace_id
7. request_id / session_id（若在运行时链路中）

#### 指标

建议指标：
1. profile_activation_total{profile_id,result}
2. profile_validation_failures_total{profile_id,error_code}
3. profile_reload_latency_ms
4. profile_lkg_fallback_total{profile_id}
5. profile_matrix_mismatch_total{profile_id}

约束：
1. 不使用高基数标签承载原始错误详情。
2. profile_id 为有限枚举，可作为标签。

#### 追踪

建议 trace span：
1. profiles.catalog.scan
2. profiles.build.resolve
3. profiles.runtime.load
4. profiles.overlay.compose
5. profiles.compat.validate
6. profiles.activate

#### 审计

必须审计事件：
1. profile 显式切换
2. profile 激活失败
3. profile 回退到 LKG
4. 运行时 override 生效/拒绝
5. factory_test 档位下的诊断可见性增强开关变更

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 建立统一 ProfileCatalog | 新增 profile 资产发现与注册层 | 先统一 profile 真源，后续 Build/Runtime 共享 | 新增 profiles/include/IProfileCatalog.h、ProfileDescriptor.h；新增 profiles/src/ProfileCatalog.cpp | 新增 tests/unit/profiles/ProfileCatalogTest.cpp | cmake -S . -B build-ci -DDASALL_PROFILE=desktop_full && cmake --build build-ci | 依赖 profiles 模块 CMake 接线 |
| Build 与 Runtime 共享同一模块矩阵 | 新增 BuildProfileResolver + Matrix 校验器 | 防止 CMake 与 YAML 双源漂移 | 新增 IBuildProfileResolver.h、BuildProfileManifest.h、ProfileCompatibilityValidator.h 与对应实现 | 新增 tests/unit/profiles/BuildProfileResolverTest.cpp、ProfileMatrixValidatorTest.cpp | ctest --test-dir build-ci -L unit --output-on-failure | 依赖模块标识命名收敛 |
| Runtime 使用不可变策略快照 | 新增 RuntimePolicyProvider 与 RuntimePolicySnapshot | 避免各模块自行解析 YAML | 新增 IRuntimePolicyProvider.h、RuntimePolicySnapshot.h、RuntimePolicyProvider.cpp | 新增 tests/unit/profiles/RuntimePolicyProviderTest.cpp | ctest --test-dir build-ci -R RuntimePolicyProviderTest --output-on-failure | 依赖 YAML/配置解析库选型 |
| 配置分层与 infra 对齐 | 新增 ProfileOverlayComposer | Profile 作为 ConfigCenter 的 Profile 层输入 | 新增 ProfileOverlayComposer.h/.cpp | 新增 tests/unit/profiles/ProfileOverlayComposerTest.cpp | ctest --test-dir build-ci -R ProfileOverlayComposerTest --output-on-failure | 依赖 infra/config 覆盖接口冻结 |
| 激活失败需可回退 | 新增 LastKnownGoodStore 与激活服务 | 保障运行时覆盖失败可安全收敛 | 新增 LastKnownGoodStore.h/.cpp、ProfileActivationService.cpp | 新增 tests/unit/profiles/ProfileActivationFallbackTest.cpp | ctest --test-dir build-ci -R ProfileActivationFallbackTest --output-on-failure | 依赖持久化或文件引用策略确认 |
| 五档位策略必须补全 | 补齐 5 个 runtime_policy.yaml 与 profile.cmake | 先从资产冻结开始，再接代码 | 修改 profiles/*/runtime_policy.yaml 与 profile.cmake | 新增 tests/contract/ProfileSchemaContractTest.cpp | ctest --test-dir build-ci -L contract --output-on-failure | 依赖 schema_version 与字段集合冻结 |
| build/test 入口统一 | 新增 CMakePresets.json 或统一脚本封装 | 降低 profile 构建和测试命令漂移 | 新增根级 CMakePresets.json 或 cmake/ProfilePresets.cmake | 新增 tests/integration/profiles/ProfileBuildSmokeTest.cpp | cmake --preset edge_balanced && cmake --build --preset edge_balanced && ctest --preset edge_balanced | 依赖仓库 CMake 版本下限确认 |
| 激活/拒绝/回退必须可观测 | 新增 ProfileTelemetryAdapter | 满足错误可观测和审计约束 | 新增 ProfileTelemetryAdapter.h/.cpp | 新增 tests/integration/profiles/ProfileObservabilityIntegrationTest.cpp | ctest --test-dir build-ci -R ProfileObservabilityIntegrationTest --output-on-failure | 依赖 infra logging/metrics/tracing 接口可用 |

说明：
1. 当前所有设计结论都可以映射为可执行 Build 目标，无需标记“无法映射”。
2. 若短期无法引入 CMakePresets.json，可先以 cmake/ProfilePresets.cmake 或脚本封装替代，但最终应收敛到共享构建入口。

---

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

建议在保留现有 profile 资产目录的前提下，新增统一代码与测试承载区：

| 路径 | 类型 | 作用 |
|---|---|---|
| profiles/include/IProfileCatalog.h | include | profile 资产发现接口 |
| profiles/include/IBuildProfileResolver.h | include | Build 解析接口 |
| profiles/include/IRuntimePolicyProvider.h | include | Runtime 策略加载接口 |
| profiles/include/ProfileDescriptor.h | include | profile 私有元对象 |
| profiles/include/BuildProfileManifest.h | include | Build 解析结果对象 |
| profiles/include/RuntimePolicySnapshot.h | include | 运行时不可变策略快照 |
| profiles/include/ProfileCompatibilityValidator.h | include | 校验接口 |
| profiles/src/ProfileCatalog.cpp | src | 资产发现与注册 |
| profiles/src/BuildProfileResolver.cpp | src | Build 解析与导出 |
| profiles/src/RuntimePolicyProvider.cpp | src | YAML 加载与快照生成 |
| profiles/src/ProfileOverlayComposer.cpp | src | Profile 层与 override 合并 |
| profiles/src/ProfileCompatibilityValidator.cpp | src | 平台/模块/adapter/schema 校验 |
| profiles/src/LastKnownGoodStore.cpp | src | LKG 存储与读取 |
| profiles/src/ProfileTelemetryAdapter.cpp | src | 日志/指标/trace/audit 输出 |
| tests/unit/profiles/ | tests | 单元测试 |
| tests/integration/profiles/ | tests | Build/Runtime 集成测试 |
| tests/contract/ProfileSchemaContractTest.cpp | tests | schema 与矩阵稳定性检查 |

现有资产目录继续保留：
1. profiles/desktop_full/
2. profiles/cloud_full/
3. profiles/edge_balanced/
4. profiles/edge_minimal/
5. profiles/factory_test/

### 8.2 分阶段实施计划

#### 阶段 P1：资产冻结与 schema 收敛

目标：定义五档位最小 schema、字段集合与模块矩阵。

完成判定：
1. 5 个 runtime_policy.yaml 字段齐全。
2. schema_version 固定。
3. enabled_modules 与蓝图矩阵一致。

#### 阶段 P2：Build 侧落地

目标：实现 BuildProfileResolver、模块矩阵验证与构建入口统一。

完成判定：
1. cmake -DDASALL_PROFILE=<id> 可驱动统一解析。
2. 非法 profile 或矩阵冲突在 configure 阶段失败。
3. 至少 desktop_full 与 edge_balanced 两档 smoke build 通过。

#### 阶段 P3：Runtime 侧落地

目标：实现 RuntimePolicyProvider、OverlayComposer、Validator、LKG。

完成判定：
1. apps 启动可显式激活 profile。
2. 非法 override 被拒绝且当前快照不变。
3. LKG 回退路径可验证。

#### 阶段 P4：观测与测试收口

目标：补齐日志、指标、审计和 profile 专项测试矩阵。

完成判定：
1. 激活/拒绝/回退均有日志与指标。
2. unit/contract/integration/failure injection 覆盖齐备。
3. profile 质量门可进入 CI。

### 8.3 原子实施任务表（建议级）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| PRF-T001 | Not Started | 新增 ProfileDescriptor 与 ProfileCatalog 接口 | 架构 7.5、蓝图 3.13 | profiles/include + profiles/src/ProfileCatalog.cpp | tests/unit/profiles/ProfileCatalogTest.cpp | cmake -S . -B build-ci -DDASALL_PROFILE=desktop_full && cmake --build build-ci | 可列出 5 个 profile，且 profile_id 唯一 |
| PRF-T002 | Not Started | 补齐五档位 runtime_policy.yaml 最小字段集 | 架构 7.5.1、蓝图 5.1 | profiles/*/runtime_policy.yaml | tests/contract/ProfileSchemaContractTest.cpp | ctest --test-dir build-ci -R ProfileSchemaContractTest --output-on-failure | 5 个档位均通过 schema 校验 |
| PRF-T003 | Not Started | 新增 BuildProfileManifest 与 BuildProfileResolver | 蓝图 5.1、5.2 | profiles/include + profiles/src/BuildProfileResolver.cpp | tests/unit/profiles/BuildProfileResolverTest.cpp | ctest --test-dir build-ci -R BuildProfileResolverTest --output-on-failure | 指定 profile 时产出稳定模块矩阵 |
| PRF-T004 | Not Started | 新增 ProfileCompatibilityValidator | ADR-005、蓝图 4.2 | profiles/src/ProfileCompatibilityValidator.cpp | tests/unit/profiles/ProfileMatrixValidatorTest.cpp | ctest --test-dir build-ci -R ProfileMatrixValidatorTest --output-on-failure | 非法矩阵组合被拒绝 |
| PRF-T005 | Not Started | 新增 RuntimePolicySnapshot 与 RuntimePolicyProvider | 架构 7.5.1 | profiles/src/RuntimePolicyProvider.cpp | tests/unit/profiles/RuntimePolicyProviderTest.cpp | ctest --test-dir build-ci -R RuntimePolicyProviderTest --output-on-failure | 可生成不可变快照对象 |
| PRF-T006 | Not Started | 新增 ProfileOverlayComposer 并对接 infra/config | infra 详细设计、架构 8.6 | profiles/src/ProfileOverlayComposer.cpp | tests/unit/profiles/ProfileOverlayComposerTest.cpp | ctest --test-dir build-ci -R ProfileOverlayComposerTest --output-on-failure | 覆盖顺序与拒绝规则可验证 |
| PRF-T007 | Not Started | 新增 LastKnownGoodStore 与回退路径 | Self-healing、Retry/Circuit Breaker 思路 | profiles/src/LastKnownGoodStore.cpp | tests/unit/profiles/ProfileActivationFallbackTest.cpp | ctest --test-dir build-ci -R ProfileActivationFallbackTest --output-on-failure | 非法更新失败时可回退到 LKG |
| PRF-T008 | Not Started | 新增 ProfileTelemetryAdapter | 工程规范 3.6、infra 详细设计 | profiles/src/ProfileTelemetryAdapter.cpp | tests/integration/profiles/ProfileObservabilityIntegrationTest.cpp | ctest --test-dir build-ci -R ProfileObservabilityIntegrationTest --output-on-failure | 激活/拒绝/回退事件全部可观测 |
| PRF-T009 | Not Started | 收敛共享构建与测试入口 | 蓝图 5.2、CMake Presets 行业实践 | CMakePresets.json 或 cmake/ProfilePresets.cmake | tests/integration/profiles/ProfileBuildSmokeTest.cpp | cmake --preset desktop_full && cmake --build --preset desktop_full | 至少 2 个 profile 能走统一入口 |

### 8.4 里程碑建议

| 里程碑 | 前置条件 | 完成判定 | 对应任务 |
|---|---|---|---|
| PRF-M1 资产冻结 | 无 | 五档位 schema 与矩阵冻结 | PRF-T001 ~ PRF-T002 |
| PRF-M2 Build 可用 | PRF-M1 | configure/build 可按 profile 收敛 | PRF-T003 ~ PRF-T004 |
| PRF-M3 Runtime 可激活 | PRF-M2 | profile 基线、overlay、校验、LKG 可运行 | PRF-T005 ~ PRF-T007 |
| PRF-M4 质量门就绪 | PRF-M3 | 观测与测试矩阵进入 CI | PRF-T008 ~ PRF-T009 |

---

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试类型 | 覆盖范围 | 目标 | 通过标准 |
|---|---|---|---|
| 单元测试 | ProfileCatalog、BuildProfileResolver、RuntimePolicyProvider、OverlayComposer、Validator、LKG | 校验核心对象与接口语义 | 关键类分支覆盖到成功/失败主路径 |
| 契约测试 | runtime_policy schema、模块矩阵、profile 私有错误码映射 | 保证五档位配置结构稳定 | 5 个 profile 全部通过 schema 检查 |
| 集成测试 | apps 启动 -> profile 激活 -> runtime 消费；build configure -> matrix validate | 验证 Build/Runtime 一致性 | 至少 desktop_full、edge_balanced 两档通过 |
| 失败注入测试 | profile 缺失、schema 错误、adapter 缺失、override 冲突、LKG 不存在 | 验证拒绝与回退路径 | 所有失败场景有确定结果，不出现部分生效 |
| 兼容性测试 | 新旧 schema_version、字段新增、deprecated 字段保留 | 保证向后兼容扩展 | 旧配置在兼容窗口内仍可解析 |

### 9.2 单元测试范围

必须覆盖：
1. profile 发现与重复 ID 拒绝。
2. 平台不匹配拒绝。
3. enabled_modules 与 adapter 依赖校验。
4. overlay 合并优先级。
5. LKG 保存与加载。
6. 显式 profile 请求失败时不静默切换。

### 9.3 契约测试影响点

虽然 profiles 不新增 contracts 对象，但仍影响以下契约测试检查点：

1. 不同 profile 下 contracts 对象集合必须保持一致。
2. 不能出现 profile 条件下字段缺失或不同序列化语义。
3. contracts boundary guards 不能被 profile 放宽。

### 9.4 集成测试路径

建议集成路径：
1. CMake configure with profile -> 生成 build manifest -> smoke build。
2. apps/cli 启动 -> 显式 profile 激活 -> runtime 获得快照。
3. runtime + llm/tools/memory 消费同一快照中的策略域。
4. 运行时 override 注入 -> 快照校验 -> 生效或拒绝。

### 9.5 失败注入测试点

1. runtime_policy.yaml 缺字段。
2. enabled_modules 要求 knowledge，但 profile 同时关闭 memory/vector。
3. edge_minimal 错误开启 multi_agent。
4. profile 指定 cloud adapter，但目标环境无该 adapter。
5. LKG 文件损坏。
6. deployment override 企图放宽高风险执行确认门槛。

### 9.6 质量门建议清单

| Gate ID | Gate 内容 | 判定标准 |
|---|---|---|
| PRF-G001 | schema 完整性 | 五档位 runtime_policy.yaml 全部通过 schema 校验 |
| PRF-G002 | 模块矩阵一致性 | BuildManifest 与 RuntimePolicySnapshot 的 enabled_modules 一致 |
| PRF-G003 | 平台一致性 | x86/ARM 档位不允许错误互配 |
| PRF-G004 | 边界一致性 | 不存在绕过 Runtime / PolicyGate / Audit 的 profile 规则 |
| PRF-G005 | 回退可用性 | 非法 override 发生时当前快照不变；若需回退则 LKG 成功 |
| PRF-G006 | 可观测性 | 激活/拒绝/回退均产生日志与指标 |
| PRF-G007 | CI 可执行性 | 至少 2 个 profile 的 build + unit + contract 流程进入 CI |

---

## 10. 兼容性与演进评估（建议级）

### 10.1 兼容与演进表

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | build scripts、CI | 优先新增字段，不修改现有 profile_id | 先在 desktop_full / edge_balanced 双档验证 | 预留 CMakePresets/workflow preset |
| Medium | runtime、llm、tools、memory | RuntimePolicySnapshot 新字段仅追加，旧消费者忽略未知字段 | 新字段先 behind validator warning，再升级为 required | 预留更多 strategy 域，如 resource_profile |
| Low | infra/config | profile 继续作为 Profile 层输入，不改变四层模型 | 先接 deployment override，再接 runtime override | 预留远程配置源与版本标记 |
| None | contracts 消费方 | profiles 不进入 contracts，无直接 breaking | N/A | 保持私有对象边界 |

### 10.2 breaking risk 结论

总体 breaking risk：Low。

原因：
1. 当前 profiles 仍是占位骨架，尚未被大范围代码依赖。
2. 推荐方案采用新增接口与私有对象，不修改 contracts。
3. 主要风险集中在 build/test 入口切换与 schema 收敛，不在共享语义层。

### 10.3 兼容迁移路径

1. 第一步：冻结现有 5 个 profile_id，不重命名目录。
2. 第二步：补齐 schema_version 和最小字段集，保持旧占位键兼容读取。
3. 第三步：引入统一 Provider，但短期保留旧脚本命令模板。
4. 第四步：在 CI 上先启用 desktop_full 与 edge_balanced，随后扩展到其余 3 档。

### 10.4 灰度策略

1. 先灰度 Build 侧，不先热切运行时策略。
2. Runtime 侧先支持只读激活，不先开放运行时 override。
3. LKG 回退在 desktop_full/factory_test 先验证，再推广到 edge 档位。

### 10.5 后续版本扩展预留点

1. schema_version 演进机制。
2. CMakePresets.json / workflow preset 标准化。
3. 远程配置源接入与本地缓存。
4. edge_minimal 的编译期固化子路径。
5. profile 与发布包、安装包、健康检查脚本的联动元数据。

---

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| 模块标识集合尚未统一命名 | PRF-T002 ~ PRF-T004 | 冻结 enabled_modules 与 adapter 命名表 | 先基于蓝图 5.1 形成最小枚举表 | 在 validator 中暂仅校验已冻结子集 |
| infra/config 的 deployment/runtime override 接口未冻结 | PRF-T006 | 明确 ConfigCenter 提供的覆盖输入形式 | 先定义本地文件/内存 patch 抽象接口 | 初版只支持 Profile 基线，不开放运行时 override |
| YAML/schema 校验库选型未定 | PRF-T002 / PRF-T005 | 确认复用现有配置解析能力或引入新依赖 | 先用最小字段手写校验器 | 先不支持复杂继承语法 |
| 统一构建入口尚未标准化 | PRF-T009 | 确定 CMake 版本下限与 preset 策略 | 先用 cmake/ProfilePresets.cmake 封装 | 暂保留现有 -DDASALL_PROFILE 命令方式 |
| LKG 存储介质未定 | PRF-T007 | 确定由文件、sqlite 还是内存引用承载 | 初版先用本地文件或内存快照 | 若无法持久化，则只支持进程内 LKG |

### 11.2 主要风险

| 风险 | 级别 | 描述 | 缓解措施 |
|---|---|---|---|
| 配置双源漂移 | High | profile.cmake 与 runtime_policy.yaml 各自维护模块开关 | 引入 BuildProfileManifest + validator 统一校验 |
| 业务模块私自读取 YAML | High | 形成解析重复与语义漂移 | 强制各模块只消费 RuntimePolicySnapshot |
| 边界越权 | High | profile 策略配置被误用为恢复/调度逻辑 | 在接口文档与测试中显式禁止 |
| 工具链与平台不一致 | Medium | edge profile 在 x86 构建或反向使用 | configure 阶段做平台拒绝 |
| 诊断开关过宽 | Medium | factory_test 误用于生产或放宽确认门槛 | execution_policy 中显式禁止放宽高风险确认 |

### 11.3 回退策略

1. Build 侧回退：若统一入口引发不兼容，短期保留 -DDASALL_PROFILE=<id> 路径，但仍使用统一 resolver。
2. Runtime 侧回退：若运行时 override 不稳定，退回“只读 profile 基线 + deployment override”模式。
3. edge_minimal 回退：若动态加载成本过高，后续可切到编译期固化生成子方案，但不得影响其他 profile 主路径。

---

## 12. 未决问题与后续任务

### 12.1 未决问题

1. Build 真源是否收敛为 CMakePresets.json，还是继续以 profile.cmake 为主、Preset 为封装层。
2. enabled_modules / adapter 标识是否由 profiles 私有枚举承载，还是复用更上层模块注册表中的稳定名称。
3. LKG 是采用文件持久化、sqlite 轻量存储，还是仅进程内缓存。
4. runtime override 的权限入口由 infra/config 暴露，还是由 apps/daemon 诊断入口统一代理。
5. edge_minimal 是否需要单独支持“编译期固化 profile”以减少启动期开销。

### 12.2 后续任务建议

1. 先冻结 5 个 profile 的最小 schema 与模块矩阵。
2. 再补 BuildProfileResolver 与 RuntimePolicyProvider 两条主链。
3. 然后引入 validator、LKG 与 observability。
4. 最后统一 CI/build/test 入口并补齐 5 档测试矩阵。

### 12.3 本文档对应的最近可执行起步集

1. PRF-T001：先把 ProfileCatalog 与 ProfileDescriptor 立起来。
2. PRF-T002：补齐 5 个 runtime_policy.yaml 的最小字段集。
3. PRF-T003：实现 BuildProfileResolver，使 desktop_full 与 edge_balanced 先可构建验证。
