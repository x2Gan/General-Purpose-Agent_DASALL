# DASALL infrastructure 子系统专项 TODO

最近更新时间：2026-03-30  
阶段：Detailed Design -> Special TODO  
适用范围：infra/  
当前结论：可进入部分执行，最细可安全落到 L2（数据结构/接口级），暂不允许按 L3（函数/方法级）全面展开。

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md
2. docs/architecture/DASSALL_Agent_architecture.md
3. docs/architecture/DASALL_Engineering_Blueprint.md
4. docs/adr/ADR-005-architecture-review-baseline.md
5. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
6. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
7. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
8. docs/plans/DASALL_工程落地实现步骤指引.md
9. docs/development/DASALL_工程协作与编码规范.md
10. infrastructure子系统组件详细设计
    - docs/architecture/DASALL_infra_audit模块详细设计.md
    - docs/architecture/DASALL_infra_config模块详细设计方案.md
    - docs/architecture/DASALL_infra_diagnostics模块详细设计.md
    - docs/architecture/DASALL_infra_health模块详细设计.md
    - docs/architecture/DASALL_infra_logging模块详细设计.md
    - docs/architecture/DASALL_infra_metrics模块详细设计.md
    - docs/architecture/DASALL_infra_OTA模块详细设计.md
    - docs/architecture/DASALL_infra_plugin模块详细设计.md
    - docs/architecture/DASALL_infra_policy模块详细设计.md
    - docs/architecture/DASALL_infra_secret模块详细设计.md
    - docs/architecture/DASALL_infra_tracing模块详细设计.md
    - docs/architecture/DASALL_infra_watchdog模块详细设计.md
11. 当前仓库代码与测试现状：infra/CMakeLists.txt、infra/include/、infra/src/InfraServiceFacade.cpp、infra/src/InfraErrorCode.cpp、infra/src/audit/、infra/src/plugin/、infra/src/tracing/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt
12. 现有 TODO 风格基线：docs/todos/contracts/、docs/todos/foundation-stage-c/WP-C1-platform-linux-infra-logging-Build开发TODO.md
- docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md
- docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md
- docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md
- docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md
- docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md
- docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md
- docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md
- docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md
- docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md
- docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md
- docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md
- docs/todos/infrastructure/DASALL_infrastructure_tracing组件专项TODO.md
- docs/todos/infrastructure/DASALL_infrastructure_watchdog组件专项TODO.md
- docs/todos/platform/DASALL_platform_linux组件专项TODO.md
- docs/todos/profiles/DASALL_profiles子系统专项TODO.md

本文档目的不是补写新架构，也不是默认推进所有 infra 子域实现，而是把已有详细设计转化为：

1. 可追溯的约束清单
2. 可执行粒度评估
3. Design -> TODO 映射
4. 最小原子任务清单
5. 阻塞项、门禁、风险与回退策略

## 2. 子系统目标与范围

### 2.1 子系统目标

根据详细设计 1.1、架构文档 3.4.7/5.10、蓝图 3.12，infrastructure 子系统目标固定为：

1. 统一日志、追踪、指标、审计四类可观测能力，且审计链路与普通日志链路分离。
2. 提供配置、密钥、安全策略、健康检查、诊断、升级、插件治理等基础能力。
3. 向 runtime 及上层模块提供稳定、可裁剪、可验证的基础设施接口。

### 2.2 范围边界

纳入本专项 TODO 的对象：

1. infra 统一入口与生命周期管理。
2. 已在详细设计中出现名字和语义的接口、数据结构、错误码域。
3. CMake 落盘点、测试注册点、contracts 边界校验点。
4. 因设计证据不足而必须前置的补设计阻塞项。

不纳入本专项 TODO 的对象：

1. runtime 主状态机、失败语义判定、全局调度逻辑。
2. memory 上下文装配、llm prompt 渲染、multi-agent 协同裁定。
3. contracts 共享对象的反向重定义。
4. 未在输入文档中明确到接口名或对象模型的 infra 内部实现细节。

## 3. 输入依据与约束清单

### 3.1 约束与边界抽取表

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| INF-C001 | 详细设计 2.1；架构 3.4.7/5.10 | Must | infra 必须提供 logging/tracing/metrics/audit 与 config/secret/security_policy/diagnostics/health/plugin/ota 能力 | TODO 必须覆盖可观测、治理、测试门禁三类事项 |
| INF-C002 | 架构 3.7；蓝图 4.1 | Must | 上层仅依赖下层抽象，infra 不反向依赖业务模块 | 任务不得引入 runtime/cognition/llm 实现依赖 |
| INF-C003 | 蓝图 4.2 | Must-Not | infra 不得依赖业务模块实现 | 所有代码目标必须落在 infra/、tests/、docs/、scripts/ 或 contracts 边界测试 |
| INF-C004 | 蓝图 4.3；架构 3.8 | Must | 跨模块调用必须通过 contracts 冻结接口 | 错误码映射、上下游标识字段只能消费现有 contracts 语义 |
| INF-C005 | ADR-005 | Must | contracts 与关键边界冻结前，不得以 infra 设计反向改写主架构 | 设计缺口必须列为 Blocked，不得伪造实现任务 |
| INF-C006 | ADR-006 | Must-Not | infra 不接管 ContextPacket 组装与 Prompt 渲染 | tracing/logging 只能记录 context IDs，不生成语义上下文 |
| INF-C007 | ADR-007 | Must-Not | infra 不做失败归因与恢复裁定 | health/watchdog/ota 只能产出观测、事件、回滚执行结果 |
| INF-C008 | ADR-008 | Must | infra 仅支撑主控与协同链路，不拥有调度权 | 审计与指标只记录 agent/task/lease 标识，不推进状态机 |
| INF-C009 | 详细设计 6.5；contracts 冻结现状 | Must | InfraContext、AuditEvent 等对象只能引用 contracts 既有标识语义 | 任务可定义 infra 私有对象，但不得扩写 contracts 公共字段语义 |
| INF-C010 | 详细设计 2.1、10；contracts 计划 | Must | 默认向后兼容；breaking change 必须评审门禁 | 所有接口任务需默认保留兼容演进空间 |
| INF-C011 | 编码规范 3.6 | Must | 失败不可吞没，必须可观测 | 错误码域、审计失败、queue overflow 等必须有测试出口 |
| INF-C012 | 编码规范 3.7；蓝图 7 | Should | 公共接口应同步新增 unit/contract/integration 测试 | TODO 必须附测试目标与命令 |
| INF-C013 | 蓝图 3.13/5.1 | Must | Profile 只能裁剪能力和替换实现，不得绕过 Audit 与 Runtime 主控链路 | config 相关任务必须受 profile 冻结约束 |
| INF-C014 | 当前代码现状 | Must | tests 顶层已注册 integration，但 integration 聚合 gate 必须显式依赖已注册测试可执行文件 | 需要把 CMake/test 注册与聚合依赖维护作为显式任务，而不是隐含前提 |

### 3.2 现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/CMakeLists.txt | 已接入 core/audit/plugin/tracing 源文件与 PUBLIC_HEADER | infra 已形成最小真实落盘入口，不再是 placeholder-only 构建 |
| infra/src/ | 已有 InfraServiceFacade、InfraErrorCode、AuditService、PluginManager、TracingModuleAnchor | 真实源码已进入构建，后续按子域继续补齐实现骨架 |
| infra/include/ | 已形成“根目录共享契约 + 组件目录公共接口”布局 | 接口级冻结已完成，后续需防止 wrapper 回流 |
| tests/CMakeLists.txt | 已接入 mocks、unit、contract、integration，并提供 dasall_integration_tests 聚合入口 | integration 测试拓扑已进入顶层构建，但聚合 gate 必须显式依赖 integration 可执行目标 |
| tests/unit/CMakeLists.txt | 已接入 infra 子目录 | infra unit 注册点已建立 |
| tests/contract/CMakeLists.txt | 已有 contracts 边界测试注册机制 | infra 与 contracts 的边界校验可复用现有 contract 测试模式 |

## 4. 粒度可行性评估

### 4.1 总体结论

结论：当前 infrastructure 详细设计整体达到 L2，不达到全局 L3。

理由：

1. 已具备明确的核心接口清单：IInfrastructureService、ILogger、IAuditLogger、IConfigCenter、IHealthMonitor、ISecretManager、IOTAManager。
2. 已具备核心对象字段：InfraContext、LogEvent、AuditEvent、HealthSnapshot、UpgradeOutcome。
3. 已具备主流程、异常流程、错误码清单、目录落盘建议、测试矩阵与门禁建议。
4. 但多数接口缺少明确签名、输入输出对象定义、异常分支返回约束、测试名与注册细节，无法安全细化到函数实现级。
5. tracing、metrics、watchdog、config typed model、secret backend、ota package schema、plugin ABI/signature 仍存在证据缺口。
6. 已新增 audit/security policy/diagnostics/plugin 的组件化设计锚点，可进入接口/对象级任务拆解。

因此：

1. 允许拆到数据结构级与接口级原子任务。
2. 不允许默认拆到方法实现级和跨子域联调级任务。
3. 所有缺乏对象模型的子域必须先补设计，再进入 Build-ready 阶段。

### 4.2 粒度可行性评估表

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| InfraServiceFacade / IInfrastructureService | 详细设计 6.2、6.3、6.6、6.7、8.1 | L2 | 接口名、方法名、主流程、落盘目录、测试矩阵 | init(config) 的 config 类型、execute(command) 的命令对象、返回对象签名 | 直接拆接口级任务；实现保持 skeleton |
| InfraContext | 详细设计 6.5 | L2 | 字段清单、contracts 对齐约束 | 字段类型别名与序列化约束 | 直接拆数据结构任务 |
| LogEvent | 详细设计 6.5、6.10 | L2 | 字段清单、脱敏约束、日志点、指标点 | attrs 键白名单、序列化边界 | 直接拆数据结构任务 |
| AuditEvent | 详细设计 6.5、6.8、6.10 | L2 | 字段清单、不可静默丢失约束、高风险命令覆盖要求 | side_effects 精确定义、导出过滤模型 | 直接拆数据结构任务 |
| ILogger / IAuditLogger | 详细设计 6.6、6.8、6.10 | L2 | 接口名、方法名、异常策略、配置项 | deadline/filter 类型、sink 结构、异步线程模型 | 直接拆接口级任务；实现不越过设计缺口 |
| AuditService | 详细设计 6.2、6.3、6.6、6.11 | L2 | 独立组件化建议、输入输出、接口语义、失败兜底策略 | 导出保留策略、存储后端约束 | 直接拆组件骨架任务；实现先最小闭环 |
| HealthSnapshot | 详细设计 6.5、6.8、9.1 | L2 | 字段清单、状态语义、失败注入入口 | 状态枚举细节、事件载荷对象 | 直接拆数据结构任务 |
| IHealthMonitor | 详细设计 6.6、6.8、9.1 | L2 | 接口名、方法名、探针注册语义、测试出口 | IHealthProbe 形状、超时策略对象 | 直接拆接口级任务；Watchdog 先阻塞 |
| ConfigCenter / IConfigCenter | 详细设计 6.2、6.3、6.6、6.9 | L2 | 方法名、四层配置、配置键表、错误码 | TypedConfig、patch 模型、配置文件格式、冲突裁定规则 | 先补设计，再进接口任务 |
| SecurityPolicyManager | 详细设计 6.2、6.3、6.5、6.6、6.11；infra/policy 模块详细设计 6.2~6.10 | L2 | 接口名、策略对象、策略快照和回滚语义、规则域与 effect 建议、冲突裁定与可观测设计 | 规则 schema 评审结论与桥接接口签名 | 先冻结对象与接口，再进入实现 |
| DiagnosticsService | 详细设计 6.2、6.3、6.5、6.6、6.11 | L2 | 接口名、快照对象、导出语义、脱敏约束 | 命令白名单、导出格式细节 | 先冻结命令域与对象，再进入实现 |
| SecretManager / ISecretManager | 详细设计 6.2、6.3、6.6、6.9 | L1 | 方法名、后端选项、明文不落盘约束 | SecretHandle、RotationRequest、权限控制对象、生产后端约束 | 先补设计，再进接口任务 |
| OTAManager / IOTAManager / UpgradeOutcome | 详细设计 6.2、6.3、6.5、6.6、6.8、6.9 | L1 | 方法名、UpgradeOutcome 字段、回滚语义 | UpgradePlan、Package、rollback token、签名与存储规范 | 先补设计，再进接口任务 |
| PluginManager | 详细设计 6.2、6.3、6.5、6.6、6.11 | L1 | 接口名、插件对象、兼容检查职责 | plugin ABI、manifest、签名链路 | 先补设计，再进实现任务 |
| TracingService | 详细设计 6.2、6.3、6.10 | L1 | 组件职责、输入输出、trace_id/span_id 关联 | 核心接口、Span 对象、导出策略、测试出口 | 先补设计，当前不拆 Build 任务 |
| MetricsService | 详细设计 6.2、6.3、6.10 | L1 | 组件职责、指标名样例、导出周期配置 | 指标对象模型、exporter 接口、标签白名单细则 | 先补设计，当前不拆 Build 任务 |
| WatchdogAgent | 详细设计 6.2、6.3、6.8、6.9 | L1 | 组件职责、超时阈值、动作说明 | 心跳接口、线程模型、与 runtime 事件对接对象 | 先补设计，当前不拆 Build 任务 |
| infra CMake / tests 注册点 | 详细设计 7、8、9；当前代码现状 | L2 | 目录建议、阶段里程碑、测试矩阵、现有 CMake 骨架 | integration 聚合 target 与 executable 依赖需要持续同步维护 | 直接拆注册任务 |

## 5. Design -> TODO 映射表

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| Infra 统一入口与生命周期管理 | 详细设计 6.2、6.3、6.6、6.7 | 接口 / 流程 | INF-TODO-002、INF-TODO-010 | 先冻结对外入口，再把 placeholder-only CMake 改成真实入口装配点 |
| InfraContext 上下游标识对象 | 详细设计 6.5 | 数据结构 | INF-TODO-001 | 该对象已具备字段清单和 contracts 对齐约束，可直接落到 L2 |
| LogEvent 结构化日志对象 | 详细设计 6.5、6.10 | 数据结构 | INF-TODO-003 | 字段、脱敏约束、观测出口已明确，适合先冻结对象再做实现 |
| AuditEvent 审计对象 | 详细设计 6.5、6.8、6.10 | 数据结构 | INF-TODO-004 | 审计不可静默丢失，必须独立于普通日志对象冻结 |
| AuditService 独立组件 | 详细设计 6.2、6.3、6.6、6.11 | 接口 / 组件 | INF-TODO-016 | 审计从 logging 子域独立，形成单独生命周期与失败兜底 |
| ILogger / IAuditLogger | 详细设计 6.6、6.8、6.10 | 接口 | INF-TODO-005、INF-TODO-006 | 先定义接口边界，避免实现期直接绑具体 sink |
| HealthSnapshot / IHealthMonitor | 详细设计 6.5、6.6、6.8、9.1 | 数据结构 / 接口 | INF-TODO-007、INF-TODO-008 | 详细设计已给出健康状态与探针评价语义，可先落对象与接口 |
| infra 私有错误码域 | 详细设计 6.6、9.1 | 错误处理 / 门禁 | INF-TODO-009、INF-TODO-012 | 错误码必须映射 contracts::ResultCode，并用 contract 测试约束不越权 |
| Config 四层合并与 Profile 裁剪 | 详细设计 6.3、6.6、6.9；蓝图 3.13 | 配置 | INF-TODO-013 | TypedConfig/patch/schema/profile 键名已由 INF-BLK-01 解阻，IConfigCenter 头文件已按冻结对象落盘 |
| SecurityPolicyManager | 详细设计 6.2、6.3、6.5、6.6、6.11；DASALL_infra_policy模块详细设计.md | 接口 / 数据结构 | INF-TODO-017、INF-BLK-07 | 先冻结 PolicyBundle/PolicyPatch/PolicySnapshot/DecisionRef 与接口，再进入实现 |
| DiagnosticsService | 详细设计 6.2、6.3、6.5、6.6、6.11 | 接口 / 流程 | INF-TODO-018、INF-BLK-08 | 先冻结诊断命令域与快照模型，再推进导出链路 |
| Secret backend 与最小安全边界 | 详细设计 6.3、6.6、6.9 | 配置 / 安全 | INF-TODO-014、INF-BLK-02 | ISecretManager 与 SecretTypes 已冻结完成；后续 blocker 转入 secret 实现骨架与 backend 任务 |
| OTA 回滚与升级结果对象 | 详细设计 6.5、6.6、6.8、6.9 | 流程 / 数据结构 | INF-TODO-015、INF-BLK-05 | IOTAManager 与 OTATypes 已冻结完成；后续 blocker 转入签名/存储/升级闭环实现 |
| PluginManager | 详细设计 6.2、6.3、6.5、6.6、6.11 | 接口 / 流程 | INF-TODO-019、INF-BLK-09 | PluginDescriptor 与 IPluginManager 已冻结完成；manifest/签名/ABI 仍阻塞后续装载闭环 |
| tests/unit、tests/contract、tests/integration 注册点 | 详细设计 7、8、9；当前 tests 结构 | 测试 / 门禁 | INF-TODO-011、INF-TODO-012 | 测试注册本身就是 infra 落地前置，不是附带动作 |
| 质量门执行记录与交付证据回写 | 详细设计 8.2、9.2、11.1；当前 gate 实践 | 门禁 / 交付证据 | INF-TODO-011、INF-TODO-012 | 当前轮不单列纯文档任务，首轮交付证据以 ctest 发现性、unit/contract 执行记录与 gate 结果为准 |
| Tracing/Metrics/Watchdog 子域 | 详细设计 6.2、6.3、6.8、6.10 | 接口 / 流程 | INF-BLK-03、INF-BLK-04 | 当前只有组件职责与观测项，不足以生成安全 Build 任务 |

## 6. 原子任务清单

### 6.1 Build-ready 任务

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| INF-TODO-001 | Done | 定义 InfraContext 数据结构 | 详细设计 6.5；架构 3.8；ADR-008 | 详细设计 6.5 核心对象与 contracts 对齐关系 | L2 | infra/include/ 下新增 InfraContext 头文件，承载 request_id、session_id、trace_id、task_id、parent_task_id、lease_id | InfraContext | unit：字段默认值与 unknown 语义；contract：不越权扩写 contracts 标识语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L "unit|contract" | 无 | 无 | 无 | 数据结构头文件、基础测试、字段说明；2026-03-26 已落盘 infra/include/InfraContext.h、tests/unit/infra/InfraContextTest.cpp、tests/contract/smoke/InfraContextBoundaryContractTest.cpp | 仅当 InfraContext 字段与设计一致、编译通过、测试能验证 unknown 兜底语义时完成 |
| INF-TODO-002 | Done | 新增 IInfrastructureService 接口与 Facade 生命周期骨架 | 详细设计 6.2、6.3、6.6、6.7、8.1；蓝图 3.12 | 详细设计 6.6 核心接口语义定义；6.7 主流程时序 | L2 | infra/include/ 下新增 IInfrastructureService 头文件；infra/src/ 下新增 InfraServiceFacade 生命周期骨架 | IInfrastructureService；InfraServiceFacade.init/start/stop/execute | unit：生命周期顺序与空实现可编译；contract：返回 ResultCode/ErrorInfo 引用不越权 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L "unit|contract" | INF-TODO-001 | 无 | 无 | 接口头文件、骨架实现、单测/合同测试、构建通过证据 | 2026-03-26 已落盘 infra/include/IInfrastructureService.h、infra/src/InfraServiceFacade.cpp、tests/unit/infra/InfraServiceFacadeTest.cpp、tests/contract/smoke/InfrastructureServiceBoundaryContractTest.cpp，并确认 placeholder 不再是唯一真实入口 |
| INF-TODO-003 | Done | 定义 LogEvent 数据结构 | 详细设计 6.5、6.8、6.10；蓝图 3.12 | 详细设计 6.5 LogEvent；6.10 日志点/指标 | L2 | infra/include/ 下新增 LogEvent 头文件，冻结 level、module、message、attrs、ts | LogEvent | unit：attrs 可序列化约束；contract：敏感字段脱敏边界不侵入 contracts | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit | INF-TODO-001 | attrs 键白名单未冻结 | 先冻结字段与基本约束，白名单细则后补 | 数据结构头文件、最小单测；2026-03-26 已落盘 infra/include/LogEvent.h、tests/unit/infra/LogEventTest.cpp、tests/contract/smoke/LogEventBoundaryContractTest.cpp | 仅当 LogEvent 字段与设计一致、测试覆盖可序列化与脱敏前置约束时完成 |
| INF-TODO-004 | Done | 定义 AuditEvent 数据结构 | 详细设计 6.5、6.8、6.10；蓝图 3.12 | 详细设计 6.5 AuditEvent；6.8 审计 fallback；6.10 审计覆盖点 | L2 | infra/include/audit/AuditTypes.h 冻结 AuditEvent、AuditEvidenceRef、AuditOutcome、AuditEvidenceKind | AuditEvent | unit：必填字段校验；contract：ToolResult/RecoveryOutcome 引用边界校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L "unit|contract" | INF-TODO-001 | side_effects 精确对象模型未冻结 | 先按字段级冻结引用关系，不扩写 side_effects 子结构 | 数据结构头文件、单测/契约测试；2026-03-26 首次落盘后已统一收敛到 infra/include/audit/AuditTypes.h，并同步 tests/unit/infra/AuditEventTest.cpp、tests/contract/smoke/AuditEventBoundaryContractTest.cpp 的入口路径 | 仅当高风险命令审计对象字段齐备、合同测试能阻止越权字段时完成 |
| INF-TODO-005 | Done | 新增 ILogger 接口 | 详细设计 6.6、6.8、6.10；编码规范 3.6 | 详细设计 6.6 ILogger；6.8 queue 满兜底 | L2 | infra/include/logging/ILogger.h | ILogger.log；ILogger.flush；ILogger.set_level | unit：普通日志、flush 与 level 控制接口可编译；contract：错误失败需可观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L "unit|contract" | INF-TODO-003 | 无 | 无 | 接口头文件、编译测试、合同测试、构建通过证据 | 2026-03-26 首次落盘后已统一迁移至 infra/include/logging/ILogger.h，并更新 tests/unit/infra/LoggerInterfaceTest.cpp、tests/contract/smoke/LoggerInterfaceBoundaryContractTest.cpp，确认 logging::ILogger 作为唯一 canonical 日志入口保持头文件级冻结 |
| INF-TODO-006 | Done | 新增 IAuditLogger 接口 | 详细设计 6.6、6.8、6.10；编码规范 3.6 | 详细设计 6.6 IAuditLogger；6.8 Audit sink 故障；6.10 高风险命令强制审计 | L2 | infra/include/audit/IAuditLogger.h | IAuditLogger.write_audit；IAuditLogger.export_audit | unit：审计写入接口可编译；contract：审计导出不越权扩写对象 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L "unit|contract" | INF-TODO-004 | 无 | 无 | 接口头文件、编译测试、合同测试、构建通过证据 | 2026-03-26 已落盘 infra/include/audit/IAuditLogger.h、tests/unit/infra/AuditLoggerInterfaceTest.cpp、tests/contract/smoke/AuditLoggerInterfaceBoundaryContractTest.cpp，并确认审计职责与普通日志接口保持分离 |
| INF-TODO-007 | Done | 定义 HealthSnapshot 数据结构 | 详细设计 6.5、6.8、9.1 | 详细设计 6.5 HealthSnapshot；6.8 探针超时；9.1 测试矩阵 | L2 | infra/include/health/HealthStateTypes.h 冻结 HealthSnapshot 与 HealthTransition | HealthSnapshot | unit：健康状态三值组合校验；contract：不反向写 runtime 状态 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit | 无 | failed_components 项元素类型未冻结 | 先冻结顶层状态字段与集合语义 | 数据结构头文件、单测；2026-03-26 首次落盘后已统一收敛到 infra/include/health/HealthStateTypes.h，并同步 tests/unit/infra/HealthSnapshotTest.cpp、tests/contract/smoke/HealthSnapshotBoundaryContractTest.cpp 的入口路径 | 仅当 HealthSnapshot 字段与状态约束一致，且测试能区分 ready/degraded/fail 时完成 |
| INF-TODO-008 | Done | 新增 IHealthMonitor 接口 | 详细设计 6.6、6.8、9.1 | 详细设计 6.6 IHealthMonitor；6.8 异常与恢复时序 | L2 | infra/include/health/IHealthMonitor.h | IHealthMonitor.register_probe；IHealthMonitor.evaluate | unit：探针注册和评估接口可编译；contract：评价结果只输出 HealthSnapshot | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | INF-TODO-007 | IHealthProbe 形状与 probe timeout 细节未冻结 | 先冻结 monitor 侧接口，不落具体 probe 抽象 | 接口头文件、编译通过证据；2026-03-26 首次落盘后已统一收敛到 infra/include/health/IHealthMonitor.h，并更新 tests/unit/infra/HealthMonitorInterfaceTest.cpp、tests/contract/smoke/HealthMonitorInterfaceBoundaryContractTest.cpp，确认健康评估输出边界保持为 HealthSnapshot | 仅当接口方法名、返回对象与设计一致，且不侵入 runtime 恢复判定时完成 |
| INF-TODO-009 | Done | 定义 infra 私有错误码域 | 详细设计 6.6、6.8、9.1；编码规范 3.6 | 详细设计 6.6 错误语义；9.1 failure injection | L2 | infra/include/ 下新增 infra 私有错误码枚举，并在 infra/src/ 建立最小映射入口 | INF_E_CONFIG_INVALID、INF_E_SECRET_UNAVAILABLE、INF_E_LOG_QUEUE_FULL、INF_E_AUDIT_WRITE_FAIL、INF_E_HEALTH_PROBE_TIMEOUT、INF_E_OTA_VERIFY_FAIL、INF_E_OTA_ROLLBACK_FAIL | unit：错误码可判定；contract：映射 contracts::ResultCode 时不新增共享语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L "unit|contract" | INF-TODO-002、INF-TODO-005、INF-TODO-006、INF-TODO-008 | contracts::ResultCode 细粒度映射表尚未在 infra 侧成文 | 先冻结 infra 私有码域和一对多映射规则，再补细项矩阵 | 错误码头文件、映射说明、测试；2026-03-26 已落盘 infra/include/InfraErrorCode.h、infra/src/InfraErrorCode.cpp、tests/unit/infra/InfraErrorCodeTest.cpp、tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp，并确认七个私有码仍只映射到既有 contracts 粗粒度结果码 | 仅当七个私有错误码均可追溯到设计条目，且 contract 测试阻止越权映射时完成 |
| INF-TODO-010 | Done | 接线 infra CMake 落盘入口 | 详细设计 7、8.1、8.2；当前 infra/CMakeLists.txt 现状 | 详细设计 7 Design -> Build 映射；8.1 目录与文件落盘建议 | L2 | 更新 infra/CMakeLists.txt，使其显式接线 include/src 目录并清理无意义根层 wrapper / placeholder 入口 | infra/CMakeLists.txt | build：dasall_infra 目标可在真实头文件存在时编译；test：为后续 unit/contract 注册提供目标依赖面 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | INF-TODO-001 至 INF-TODO-009 | 无 | 无 | CMake 改动、构建通过证据；2026-03-26 已更新 infra/CMakeLists.txt，将 core/tracing 源文件分组与 PUBLIC_HEADER 公开头文件入口显式接入 dasall_infra，且当前不再保留 placeholder-only 或根层 wrapper 作为构建兜底 | 仅当 infra 目标能显式包含真实头文件/源文件入口，且不再依赖 placeholder-only 兜底时完成 |
| INF-TODO-011 | Done | 注册 infra 单元测试入口 | 详细设计 8.1、9.1；当前 tests/unit/CMakeLists.txt 现状 | 详细设计 9.1 测试矩阵；编码规范 3.7 | L2 | 新增 tests/unit/infra/ 与 tests/unit/CMakeLists.txt 注册入口 | tests/unit/infra；tests/unit/CMakeLists.txt | unit：InfraContext、LogEvent、AuditEvent、HealthSnapshot、接口编译测试 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit | INF-TODO-001 至 INF-TODO-010 | 当前 unit 聚合未包含 infra 子目录 | 在 unit 顶层加入 infra 子目录并确保新增用例被发现 | 单测目录、注册入口、ctest 发现性证据；2026-03-26 已确认 tests/unit/CMakeLists.txt 接入 infra，tests/unit/infra/CMakeLists.txt 注册 9 个 infra unit 目标，`ctest -N -L unit` 可发现、`ctest -L unit` 执行 10/10 通过 | 仅当 infra 单测可被 ctest -L unit 发现并执行时完成 |
| INF-TODO-012 | Done | 注册 infra contracts 边界测试入口 | 详细设计 6.5、9.1；蓝图 4.3；当前 tests/contract/CMakeLists.txt 机制 | 详细设计 6.5 contracts 对齐关系；9.1 Contract 覆盖要求 | L2 | 在 tests/contract/ 现有注册机制下新增 infra 边界用例，并扩展必要的 smoke/compatibility 断言 | tests/contract/CMakeLists.txt；tests/contract/smoke/ | contract：标识字段不越权、错误码映射不漂移、AuditEvent 引用边界稳定 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L contract | INF-TODO-001、INF-TODO-004、INF-TODO-009 | 具体测试文件名与首批边界断言尚未冻结 | 先沿用现有 centralized registration 模式，后补充断言细则 | 合同测试源文件、注册改动、执行记录；2026-03-26 已确认 tests/contract/CMakeLists.txt 集中注册 9 个 infra 边界 contract 用例，`ctest -N -L contract` 可发现、`ctest -L contract` 执行 90/90 通过 | 仅当新增 infra 合同测试被发现并能阻止 contracts 语义越权时完成 |
| INF-TODO-013 | Done | 定义 IConfigCenter 接口骨架 | 详细设计 6.3、6.6、6.9；蓝图 3.13 | 详细设计 6.6 IConfigCenter；6.9 配置项与默认策略 | L2 | 目标文件为 infra/include/config/IConfigCenter.h，在已冻结 TypedConfig/patch/schema/profile 键名基础上定义接口头文件 | IConfigCenter.load_layers；IConfigCenter.get_typed；IConfigCenter.apply_override；IConfigCenter.rollback；IConfigCenter.subscribe | unit：接口占位对象与查询/回滚/订阅守卫可编译；contract：运行时 override 不得越过 profile 保护键或漂移 contracts 错误语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "ConfigCenterInterfaceTest|ConfigCenterInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "ConfigCenterInterfaceTest|ConfigCenterInterfaceBoundaryContractTest" | 无 | 无 | 无 | 接口头文件、编译测试、边界 contract 测试；2026-03-30 已落盘 infra/include/config/IConfigCenter.h、tests/unit/infra/ConfigCenterInterfaceTest.cpp、tests/contract/smoke/ConfigCenterInterfaceBoundaryContractTest.cpp，并完成 infra/tests CMake 注册 | 仅当 IConfigCenter 与 config typed 对象对齐、编译通过且 contract 测试能阻止 profile/runtime override 边界漂移时完成 |
| INF-TODO-014 | Done | 定义 ISecretManager 接口骨架 | 详细设计 6.3、6.6、6.9；DASALL_infrastructure_secret组件专项TODO.md | 详细设计 6.6 ISecretManager；6.9 secret.backend | L1 | infra/include/secret/ISecretManager.h | ISecretManager.get_secret；materialize；release；rotate；revoke；inspect | unit：SecretManagerInterfaceTest；contract：SecretManagerInterfaceBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_secret_manager_interface_unit_test dasall_contract_secret_manager_interface_boundary_test && ctest --test-dir build-ci --output-on-failure -R "SecretManagerInterfaceTest|SecretManagerInterfaceBoundaryContractTest" | 无 | 无 | 无 | 接口头文件、编译与边界测试证据；2026-04-01 已回链到 secret 组件专项 TODO SEC-TODO-001 | 仅当 6 个方法与设计锚点一致、编译通过且 contract 测试不越权时完成 |
| INF-TODO-015 | Done | 定义 IOTAManager 接口骨架与 UpgradeOutcome 对接点 | 详细设计 6.5、6.6、6.8、6.9；DASALL_infrastructure_ota组件专项TODO.md | 详细设计 6.5 UpgradeOutcome；6.6 IOTAManager；6.8 OTA 失败回滚 | L1 | infra/include/ota/IOTAManager.h；infra/include/ota/OTATypes.h | IOTAManager.precheck；IOTAManager.apply；IOTAManager.rollback；IOTAManager.query_status；UpgradeOutcome | unit：OTATypesCompileTest、OTAInterfaceCompileTest；contract：OTATypeBoundaryContractTest、OTAManagerInterfaceBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_ota_types_unit_test dasall_ota_manager_interface_unit_test dasall_contract_ota_type_boundary_test dasall_contract_ota_manager_interface_boundary_test && ctest --test-dir build-ci --output-on-failure -R "OTATypesCompileTest|OTAInterfaceCompileTest|OTATypeBoundaryTest|OTAManagerInterfaceBoundaryContractTest" | 无 | 无 | 无 | 接口头文件、对象头文件、编译与边界测试证据；2026-04-01 已回链到 ota 组件专项 TODO OTA-TODO-001、002 | 仅当对象与接口同时冻结、编译通过且 contract 测试不越权时完成 |
| INF-TODO-016 | Done | 新增 AuditService 独立组件骨架 | 详细设计 6.2、6.3、6.6、6.11；架构 8.8 | 详细设计 6.11 独立组件化建议；6.8 审计失败兜底 | L2 | infra/src/audit/ 组件骨架与 audit 生命周期接线；保持 logging/audit 目录分离 | AuditService.init/write/export/fallback | unit：AuditServiceFallbackTest；contract：AuditEvent 引用边界稳定 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N -R "AuditServiceFallbackTest|AuditServiceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "AuditServiceFallbackTest|AuditServiceBoundaryContractTest" | INF-TODO-004、INF-TODO-006、INF-TODO-010 | 无 | 先落最小组件骨架，不实现复杂存储策略 | audit 目录骨架、测试与构建证据；2026-03-27 已落盘 infra/include/audit/AuditService.h、infra/src/audit/AuditService.cpp、tests/unit/infra/AuditServiceFallbackTest.cpp、tests/contract/smoke/AuditServiceBoundaryContractTest.cpp，并完成 infra/tests CMake 注册 | 仅当审计组件与 logging 目录分离且 fallback 失败路径可测试时完成 |
| INF-TODO-017 | Done | 冻结 SecurityPolicyManager 接口与策略对象 | 详细设计 6.2、6.5、6.6、6.11；架构 5.10/8.8；DASALL_infra_policy模块详细设计.md | 详细设计 6.5 SecurityPolicySet；6.6 ISecurityPolicyManager；policy 模块设计 6.5/6.6 | L2 | infra/include/policy/ISecurityPolicyManager.h、PolicyBundle/PolicyPatch/PolicySnapshot/PolicyDecisionRef 对象头文件 | load_policy/apply_patch/dry_run_patch/snapshot/rollback/evaluate | unit：PolicySnapshotCompatibilityTest；contract：PolicyDecisionBoundaryTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N -R "PolicySnapshotCompatibilityTest|PolicyDecisionBoundaryTest" && ctest --test-dir build-ci --output-on-failure -R "PolicySnapshotCompatibilityTest|PolicyDecisionBoundaryTest" | INF-TODO-010 | 无（INF-BLK-07 已由 policy 模块详细设计与本轮对象冻结共同解阻） | 先冻结最小规则集合、快照版本语义与 decision 引用边界 | 接口头文件、对象头文件、边界测试；2026-03-27 已落盘 infra/include/policy/ISecurityPolicyManager.h、infra/include/policy/PolicyBundle.h、infra/include/policy/PolicyPatch.h、infra/include/policy/PolicySnapshot.h、infra/include/policy/PolicyDecisionRef.h、tests/unit/infra/PolicySnapshotCompatibilityTest.cpp、tests/contract/smoke/PolicyDecisionBoundaryTest.cpp，并完成 infra/tests CMake 注册 | 仅当策略对象可版本化、可回滚且契约边界测试通过时完成 |
| INF-TODO-018 | Done | 冻结 DiagnosticsSnapshot 与 IDiagnosticsService 接口 | 详细设计 6.2、6.5、6.6、6.11；架构 9.5 | 详细设计 6.5 DiagnosticsSnapshot；6.6 IDiagnosticsService | L2 | infra/include/diagnostics/IDiagnosticsService.h、DiagnosticsSnapshot 对象与最小导出接口 | execute/export_snapshot；DiagnosticsSnapshot | unit：DiagnosticsSnapshotExportTest；integration：InfraDiagnosticsSmokeTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest" && ctest --test-dir build-ci --output-on-failure -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest" | INF-TODO-010、INF-TODO-012 | 无（INF-BLK-08 已由 diagnostics 模块详细设计与本轮对象冻结共同解阻） | 先支持只读诊断命令与本地导出 | 接口头文件、对象头文件、最小 smoke 测试；2026-03-27 首次落盘后已统一迁移至 infra/include/diagnostics/IDiagnosticsService.h，并完成 tests/unit/infra/DiagnosticsSnapshotExportTest.cpp、tests/integration/infra/InfraDiagnosticsSmokeTest.cpp 路径同步与 infra/tests CMake 注册 | 仅当诊断命令执行与导出链路可测且包含脱敏前置校验时完成 |
| INF-TODO-019 | Done | 冻结 PluginDescriptor 与 IPluginManager 接口 | 详细设计 6.2、6.5、6.6、6.11；DASALL_infrastructure_plugin组件专项TODO.md | 详细设计 6.5 PluginDescriptor；6.6 IPluginManager | L1 | infra/include/plugin/PluginDescriptor.h；infra/include/plugin/IPluginManager.h；infra/src/plugin/PluginManager.cpp | PluginDescriptor；IPluginManager.discover；validate；load；unload；list_active | unit：PluginDescriptorFieldTest、PluginManagerInterfaceCompileTest；contract：PluginDescriptorBoundaryContractTest、PluginManagerBoundaryContractTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_plugin_descriptor_unit_test dasall_plugin_manager_interface_unit_test dasall_contract_plugin_descriptor_boundary_test dasall_contract_plugin_manager_boundary_test && ctest --test-dir build-ci --output-on-failure -R "PluginDescriptorFieldTest|PluginManagerInterfaceCompileTest|PluginDescriptorBoundaryContractTest|PluginManagerBoundaryContractTest" | 无 | 无 | 无 | 接口头文件、对象头文件、空壳实现与边界测试证据；2026-04-01 已回链到 plugin 组件专项 TODO PLG-TODO-001、003 | 仅当对象与接口冻结完成、编译通过且 contract 测试不越权时完成 |

### 6.2 当前不进入任务拆解的对象

以下对象当前只保留在阻塞项，不生成 Build-ready 原子任务：

1. TracingService：缺少 Span 对象、接口名、导出器边界与测试出口。
2. MetricsService：缺少指标对象、exporter 接口、标签治理规则。
3. WatchdogAgent：缺少心跳接口、线程模型、与 runtime 事件对象对接约束。

原因：输入文档只到组件职责与观测项层，未达到可安全落盘的接口/数据结构级。

## 7. 执行顺序建议

### 7.1 顺序与并行段

| 顺序段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| 阶段 A：基础对象冻结 | INF-TODO-001、INF-TODO-003、INF-TODO-004、INF-TODO-007 | 可并行 | 四个数据结构互相弱依赖，且都直接来自 6.5 对象表 |
| 阶段 B：对外接口冻结 | INF-TODO-002、INF-TODO-005、INF-TODO-006、INF-TODO-008 | 可并行，但以阶段 A 为前置 | 接口依赖对应对象；不进入子域实现 |
| 阶段 C：错误与构建入口 | INF-TODO-009、INF-TODO-010 | 串行 | 先冻结错误码域，再更新 CMake 入口更稳妥 |
| 阶段 D：测试与门禁接线 | INF-TODO-011、INF-TODO-012 | 可并行 | unit 与 contract 可以并行推进，但都依赖阶段 A-C 落盘 |
| 阶段 E：缺口能力补齐（可执行） | INF-TODO-016、INF-TODO-017、INF-TODO-018 | 可并行，依赖 A-D | 审计独立组件 + 安全策略 + 诊断接口同步冻结 |
| 阶段 F：补设计解阻 | INF-TODO-013、INF-TODO-014、INF-TODO-015、INF-TODO-019 | INF-TODO-013、014、015、019 的接口/对象冻结已完成；剩余仅保留 secret/ota/plugin 实现侧补设计与高阶阻塞 | Config -> Secret runtime/backend -> OTA execution path -> Plugin manifest/signature/ABI 的推进顺序更稳妥 |

### 7.2 必过门禁表

| Gate ID | 门禁项 | 触发时机 | 通过标准 | 不通过后动作 |
|---|---|---|---|---|
| INF-GATE-01 | 接口冻结门 | 阶段 B 结束前 | IInfrastructureService、ILogger、IAuditLogger、IHealthMonitor 头文件落盘且不越权 | 退回接口定义，不推进实现 |
| INF-GATE-02 | contracts 边界门 | 阶段 D 前 | 新增 infra 对象仅消费既有 contracts 标识语义，contract 测试通过 | 退回对象/错误码定义 |
| INF-GATE-03 | 构建门 | 阶段 C、D | cmake --build build-ci --target dasall_infra 成功 | 修复 CMake 入口后再继续 |
| INF-GATE-04 | unit 注册门 | 阶段 D | ctest --test-dir build-ci -L unit 能发现并执行 infra 测试 | 修复 tests/unit 注册 |
| INF-GATE-05 | integration 拓扑门 | 进入 tracing/metrics/watchdog/ota 实现前 | tests 顶层已明确 integration 注册策略 | 未过门时禁止生成 integration Build 任务 |
| INF-GATE-06 | breaking review 门 | 任意接口签名或 contracts 映射变更前 | 评审明确 breaking 风险、迁移窗口、回退方案 | 未评审不得推进 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响范围 | 影响任务 | 解阻条件 | 最小解阻动作 |
|---|---|---|---|---|---|
| INF-BLK-01 | 已解阻（2026-03-30 校准）：config 模块详细设计 6.5/6.5.1 已冻结 TypedConfig、ConfigPatch/ConfigPatchEntry、ConfigLayerRef、schema_version=1 与 profile 键命名；CFG-TODO-006 已落盘 ConfigTypes.h 与 unit/contract 证据 | 配置子域 | INF-TODO-013 | 无；后续仅需按已冻结对象落盘 IConfigCenter 头文件 | 证据回链到本节 8.1 校准记录，以及 docs/architecture/DASALL_infra_config模块详细设计方案.md、infra/include/config/ConfigTypes.h、tests/unit/infra/ConfigTypesTest.cpp、tests/contract/smoke/ConfigTypesBoundaryContractTest.cpp |
| INF-BLK-02 | SecretManager 生产后端边界、权限执行链与 runtime/backend 细节仍未冻结 | secret 子域 | secret 实现骨架与 backend 任务 | 明确最小权限执行链和 file/kms/mock 的统一抽象 | 补齐 secret 实现侧安全边界说明与 backend 适配策略 |
| INF-BLK-03 | Tracing/Metrics 只有组件职责，没有接口名、对象模型、导出策略与测试出口 | tracing/metrics 子域 | 后续专项 TODO | 明确 Span/Metric 对象、导出器接口、标签白名单与失败语义 | 新增 tracing/metrics 子模块详细设计 |
| INF-BLK-04 | Watchdog 缺少心跳对象、超时事件模型、与 runtime 恢复建议事件的边界 | health/watchdog 子域 | 后续专项 TODO | 明确心跳输入、deadline 模型、事件输出对象 | 新增 watchdog 详细设计或补章 |
| INF-BLK-05 | OTA 签名算法、包存储规范与升级/回滚执行细节仍未冻结 | ota 子域 | ota 实现骨架与升级闭环任务 | 冻结签名/存储规范与执行时序 | 补齐 OTA 执行闭环设计与包规范表 |
| INF-BLK-06 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与 dasall_integration_tests 聚合依赖已补齐，原阻塞为“顶层未接入 integration/聚合 gate 未依赖已注册测试可执行文件” | 测试门禁 | tracing/metrics/watchdog/ota 集成任务 | 无；后续仅需按组件落盘具体 integration 用例 | 证据回链到本节 8.1 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt |
| INF-BLK-07 | 已解阻（2026-03-30 校准）：policy 规则 schema、patch 操作白名单与冲突裁定顺序已由 policy 模块详细设计、INF-TODO-017 头文件落盘与边界测试共同固化 | security policy 子域 | INF-TODO-017 | 无；后续仅需保持 policy 详细设计、头文件与边界测试口径同步 | 证据回链到本节 8.1 校准记录，以及 infra/include/policy/*、tests/unit/infra/PolicySnapshotCompatibilityTest.cpp、tests/contract/smoke/PolicyDecisionBoundaryTest.cpp |
| INF-BLK-08 | 已解阻（2026-03-30 校准）：INF-TODO-018 已落盘 IDiagnosticsService.h、DiagnosticsTypes.h 与首批 smoke/unit 证据，诊断命令白名单、请求/返回对象与远程导出默认门禁已有代码/测试回链 | diagnostics 子域 | INF-TODO-018 | 无；后续仅需继续推进 diagnostics 组件内剩余的 allowed_commands 参数 schema、脱敏矩阵、导出细则与桥接接口阻塞 | 证据回链到本节 8.1 校准记录，以及 infra/include/diagnostics/IDiagnosticsService.h、infra/include/diagnostics/DiagnosticsTypes.h、tests/unit/infra/DiagnosticsSnapshotExportTest.cpp、tests/integration/infra/InfraDiagnosticsSmokeTest.cpp |
| INF-BLK-09 | 插件 manifest、ABI 兼容矩阵与签名链路未冻结 | plugin 子域 | plugin manifest/signature/compatibility 实现任务 | 明确 manifest 字段、ABI 兼容规则、签名校验流程 | 在详细设计中补齐插件对象表与校验流程 |

### 8.1 阻塞台账校准记录

| 阻塞项 ID | 当前状态 | 校准时间 | 证据 | 影响调整 |
|---|---|---|---|---|
| INF-BLK-01 | Resolved | 2026-03-30 | docs/architecture/DASALL_infra_config模块详细设计方案.md 6.5/6.5.1 已明确 TypedConfig、ConfigPatch/ConfigPatchEntry、ConfigLayerRef、`schema_version: 1`、五档 `profile_id` 与受保护路径；infra/include/config/ConfigTypes.h 已落盘；`ctest --test-dir build-ci -N -R "ConfigTypesTest|ConfigTypesBoundaryContractTest"` 发现 2 个测试，`ctest --test-dir build-ci --output-on-failure -R "ConfigTypesTest|ConfigTypesBoundaryContractTest"` 执行通过 | INF-TODO-013 应从 Blocked 转为 Not Started，后续只需继续完成 IConfigCenter 头文件和对应测试，不再把对象模型缺口计为当前阻塞 |
| INF-BLK-06 | Resolved | 2026-03-30 | tests/CMakeLists.txt 已纳入 integration 聚合并显式依赖 integration 可执行目标；tests/integration/CMakeLists.txt 已回传 integration 可执行目标清单；`cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -N -L integration` 发现 5 个 integration 测试，`ctest --test-dir build-ci --output-on-failure -L integration` 执行 5/5 通过 | 下游以“tests 顶层未接入 integration”为唯一阻塞原因的任务应转回 Not Started，gate 不再把该项计为当前 Blocked |
| INF-BLK-07 | Resolved | 2026-03-30 | docs/architecture/DASALL_infra_policy模块详细设计.md 6.5/6.9 已明确 PolicyRuleDescriptor 的 domain/effect/priority、PolicyPatch 的 operations 白名单与 priority_order；infra/include/policy/ISecurityPolicyManager.h、PolicyBundle.h、PolicyPatch.h、PolicySnapshot.h、PolicyDecisionRef.h 已落盘；`ctest --test-dir build-ci -N -R "PolicySnapshotCompatibilityTest|PolicyDecisionBoundaryTest"` 发现 2 个测试，`ctest --test-dir build-ci --output-on-failure -R "PolicySnapshotCompatibilityTest|PolicyDecisionBoundaryTest"` 执行 2/2 通过 | policy 专项中仅因 POL-BLK-001 被标记为 Blocked 的任务应回到 Not Started，gate 不再把 INF-BLK-07 视为当前阻塞 |
| INF-BLK-08 | Resolved | 2026-03-30 | docs/architecture/DASALL_infra_diagnostics模块详细设计.md 6.6/6.9 已明确 IDiagnosticsService 语义、allowed_commands 与 remote.enabled 默认关闭；infra/include/diagnostics/IDiagnosticsService.h 与 infra/include/diagnostics/DiagnosticsTypes.h 已落盘 SnapshotQuery、SnapshotExportRequest、DiagnosticsSnapshotResult 与只读命令白名单；`ctest --test-dir build-ci -N -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest"` 发现 2 个测试，`ctest --test-dir build-ci --output-on-failure -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest"` 执行 2/2 通过 | diagnostics 专项中仅因 DIA-BLK-001 被标记为 Blocked 的任务应完成或转回 Not Started；DIA-BLK-003/004/005/006 继续保留为当前阻塞；DIA-BLK-002 已由 DIA-TODO-008 于 2026-04-07 解阻 |

## 9. 验收与质量门

### 9.1 当前阶段验收命令基线

| 场景 | 命令 | 用途 |
|---|---|---|
| 配置构建目录 | cmake -S . -B build-ci -G Ninja | 刷新顶层构建图 |
| 构建 infra | cmake --build build-ci --target dasall_infra | 验证 infra 目标可编译 |
| 跑 unit 标签 | ctest --test-dir build-ci --output-on-failure -L unit | 验证 infra 单测能发现并执行 |
| 跑 contract 标签 | ctest --test-dir build-ci --output-on-failure -L contract | 验证 contracts 边界不漂移 |
| 查看测试发现性 | ctest --test-dir build-ci -N | 验证新增测试是否注册成功 |

交付证据回写约束：

1. 本轮不把纯文档回写拆成独立 Build-ready 原子任务。
2. INF-TODO-011 与 INF-TODO-012 的完成证据，必须至少包含 ctest 发现性输出、unit/contract 执行结果摘要。
3. 后续若引入独立 gate 脚本，再在下一轮专项 TODO 中单独拆“门禁脚本 + 执行记录”任务。

### 9.2 质量门表

| 质量门 | 检查对象 | 通过条件 | 失败判定 |
|---|---|---|---|
| QG-01 | 数据结构冻结 | InfraContext、LogEvent、AuditEvent、HealthSnapshot 字段与详细设计一致 | 任一字段缺失、增改无来源依据 |
| QG-02 | 接口边界冻结 | IInfrastructureService、ILogger、IAuditLogger、IHealthMonitor 不引入业务依赖 | 出现 runtime/cognition/llm 具体实现依赖 |
| QG-03 | 错误语义门 | infra 私有错误码与 contracts 映射可测 | 出现吞错或无映射说明 |
| QG-04 | unit 门 | infra 单测可发现、可执行、可重复 | ctest -L unit 无法发现或执行失败 |
| QG-05 | contract 门 | 标识语义与错误映射边界稳定 | contract 测试发现越权字段或语义漂移 |
| QG-06 | breaking 审核门 | 接口签名和 contracts 映射变更已评审 | 未评审即修改公共边界 |

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 回退策略 |
|---|---|---|---|
| 误把 L2 设计当成 L3 实现依据 | High | 直接开始写 logging/config/ota 具体实现 | 退回到接口/对象冻结，先补设计再实现 |
| infra 越权依赖上层模块 | High | 为了联调方便直接 include runtime/cognition/llm 实现头文件 | 立即回退到 contracts 接口；新增 contract 边界断言 |
| 错误码语义漂移 | High | 在 infra 私有码域里重新定义 contracts 共享失败语义 | 回退到映射层，不修改 contracts 公共对象 |
| 审计链路和普通日志混用 | Medium | 直接让 ILogger 承担 AuditEvent 写入 | 回退到 ILogger/IAuditLogger 双接口分离 |
| integration 任务过早推进 | Medium | 顶层 integration 拓扑未校准或聚合 gate 未依赖 integration 可执行目标时就开始写集成门禁 | 先修复 tests 顶层接线或聚合依赖，再推进 integration |
| Profile 配置提前编码 | Medium | ConfigCenter 在 schema 未冻结前直接落实现 | 回退到 Blocked，先冻结配置模型 |

## 11. 可行性结论

### 11.1 是否可以直接进入执行

可以，但只能部分进入执行。

当前可直接执行的范围：

1. 数据结构级任务：INF-TODO-001、003、004、007。
2. 接口级任务：INF-TODO-002、005、006、008。
3. 错误码/CMake/测试注册任务：INF-TODO-009、010、011、012。
4. 缺口能力补齐任务：INF-TODO-016、017、018。
5. config 接口冻结任务：INF-TODO-013（已完成，可作为后续 config 主链骨架前置）。

当前不得直接推进的范围：

1. ISecretManager 真实接口落盘与实现。
2. IOTAManager 真实接口落盘与实现。
3. IPluginManager 真实装载实现（ABI/signature 未冻结）。
4. TracingService、MetricsService、WatchdogAgent 的 Build-ready 任务。

### 11.2 当前可落到的最细粒度

当前最细可安全落到 L2：

1. 单个数据结构。
2. 单个接口头文件。
3. 单个错误码域。
4. 单个 CMake 注册点。
5. 单个测试注册入口。

当前不能安全落到 L3 的证据缺口：

1. 多数接口没有完整的输入输出对象和签名。
2. secret/ota 缺少关键对象模型；config 已进入接口头文件级，但仍未达到主链实现级。
3. tracing/metrics/watchdog 只有职责，没有接口与测试出口。
4. 顶层 integration 拓扑虽已进入 tests 构建图，但各组件 integration 用例仍需各自落盘并保持与聚合 target 的依赖清单同步。

### 11.3 后续建议

1. 先执行 INF-TODO-001 至 INF-TODO-012，完成 infrastructure 的 L2 冻结与测试入口接线。
2. 并行执行 INF-TODO-016、INF-TODO-017、INF-TODO-018，完成 audit/security policy/diagnostics 缺口补齐。
3. 并行补齐 INF-BLK-02 至 INF-BLK-09 对应设计缺口。
4. 基于 DASALL_infra_policy模块详细设计.md，持续同步 security policy 组件专项 TODO 与 INF-BLK-07 校准结论，并优先推进 POL-BLK-002、POL-BLK-006 的剩余解阻。
5. 仅在 Blocker 解消后，再生成 config、secret、ota、plugin、tracing、metrics、watchdog 的下一轮专项 TODO。

## 28. 本轮执行记录（2026-03-30 / INF-BLK-01）

### 28.1 选中任务

1. 本轮任务：INF-BLK-01 对应的最小 blocker-fix，回链到 config 专项 CFG-TODO-006。
2. 可执行性依据：INF-TODO-013 当前唯一阻塞为 TypedConfig/patch/schema/profile 键名未冻结；CFG-TODO-006 无前置依赖，且可直接提供解阻所需的设计与代码证据。

### 28.2 校准与解阻结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6/6.9 已冻结四层来源、runtime override TTL、受保护路径与 profile 边界。
2. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.5 原有对象表缺少 TypedConfig、ConfigLayerRef、配置格式与 profile 键名冻结，构成 INF-BLK-01 的直接文档缺口。
3. docs/architecture/DASALL_profiles模块详细设计.md 6.9 已冻结 `schema_version: 1`、`profile_meta` 必填键与 `enabled_modules` 命名表，可作为 config 侧 profile 键名真源。

外部参考：

1. Azure External Configuration Store 模式要求配置接口暴露 typed/structured 数据、作用域与版本控制，并在启动故障时保留 last-known-good fallback；本轮据此把 source format、schema_version 与 source_chain 一并冻结。
2. 12-Factor Config 强调把 deploy-time config 与代码分离，并避免无边界的环境分组膨胀；本轮据此固定五档 `profile_id` 与受保护 profile 键命名，不允许 runtime override 改写 profile 身份。

D 结论：

1. 通过补齐 config 详细设计 6.5.1，并落盘 ConfigTypes.h、unit/contract 测试，可把 INF-BLK-01 从当前阻塞转为已解阻。
2. 解阻后三件套：
    - 代码目标：infra/include/config/ConfigTypes.h。
    - 测试目标：ConfigTypesTest、ConfigTypesBoundaryContractTest。
    - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "ConfigTypesTest|ConfigTypesBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "ConfigTypesTest|ConfigTypesBoundaryContractTest"。
3. D Gate：PASS。

### 28.3 Build 交付与证据

交付物：

1. docs/architecture/DASALL_infra_config模块详细设计方案.md：补齐 TypedConfig / patch / schema / profiles 键名冻结章节。
2. infra/include/config/ConfigTypes.h：冻结 TypedConfig、ConfigPatchEntry、ConfigLayerRef 与 ConfigApplyResult 等对象。
3. tests/unit/infra/ConfigTypesTest.cpp、tests/contract/smoke/ConfigTypesBoundaryContractTest.cpp：补齐 unit/contract 边界证据。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件和测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过，unit 39/39、contract 94/94 全部通过。
3. `ctest --test-dir build-ci -N -R "ConfigTypesTest|ConfigTypesBoundaryContractTest"`：通过，发现 2 个测试。
4. `ctest --test-dir build-ci --output-on-failure -R "ConfigTypesTest|ConfigTypesBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：新增对象和守卫命名已直接表达 schema/profile/path 语义，无需补冗余注释。
2. 正负例覆盖：unit 覆盖合法 typed/query/patch/source_chain 正例与 schema/path/duplicate layer 负例；contract 覆盖 contracts 错误边界与 profile 保护路径。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/contract 用例均进入 CTest 图。
4. TODO 证据回写：已完成 blocker 校准、任务状态迁移与验收结果回写。
5. 提交隔离：本轮提交范围限定为 config 类型模型、测试、CMake 注册与 blocker 校准文档。

## 29. 本轮执行记录（2026-03-30 / INF-TODO-013）

### 29.1 选中任务

1. 本轮任务：INF-TODO-013。
2. 可执行性依据：INF-BLK-01 已于上一轮通过 CFG-TODO-006 解阻；IConfigCenter 仅需在既有 ConfigTypes 基础上冻结接口头文件与边界测试，不依赖 loader/validator/snapshot 实现。

### 29.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.5/6.6 已冻结 ConfigQuery、ConfigPatch、ConfigApplyResult 与 IConfigCenter 五个方法名。
2. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6/6.9 已明确 `apply_override(patch)` 只能接受受管 runtime override patch，并受 `profile_meta.*`、`enabled_modules.*` 保护路径约束。
3. infra/include/config/ConfigTypes.h 已落盘 TypedConfig、ConfigPatch、ConfigDiff 与 ConfigApplyResult，可直接作为 IConfigCenter 的 typed 输入输出对象。

外部参考：

1. Azure External Configuration Store 模式要求配置接口提供 typed/structured 访问、版本化与变更通知接口；本轮据此把 IConfigCenter 冻结为 typed lookup、managed override、rollback 与 namespace-filtered subscription 五个入口。

D 结论：

1. Design -> Build 映射：新增 infra/include/config/IConfigCenter.h，冻结 ConfigStartupContext、ConfigRollbackToken、ConfigSubscriptionRequest、ConfigSubscriptionHandle 与 IConfigCenter 接口。
2. Build 三件套：
    - 代码目标：infra/include/config/IConfigCenter.h。
    - 测试目标：ConfigCenterInterfaceTest、ConfigCenterInterfaceBoundaryContractTest。
    - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "ConfigCenterInterfaceTest|ConfigCenterInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "ConfigCenterInterfaceTest|ConfigCenterInterfaceBoundaryContractTest"。
3. D Gate：PASS。

### 29.3 Build 交付与证据

交付物：

1. infra/include/config/IConfigCenter.h：新增 startup context、rollback token、subscription request/handle 与 IConfigCenter 五个接口入口。
2. tests/unit/infra/ConfigCenterInterfaceTest.cpp、tests/contract/smoke/ConfigCenterInterfaceBoundaryContractTest.cpp：新增 unit/contract 边界证据。
3. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件与测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests`：通过，unit 40/40、contract 95/95 全部通过。
3. `ctest --test-dir build-ci -N -R "ConfigCenterInterfaceTest|ConfigCenterInterfaceBoundaryContractTest"`：通过，发现 2 个测试。
4. `ctest --test-dir build-ci --output-on-failure -R "ConfigCenterInterfaceTest|ConfigCenterInterfaceBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：新增接口和占位对象命名已直接表达 startup/query/rollback/subscribe 语义，无需冗余注释。
2. 正负例覆盖：unit 覆盖合法 startup/query/subscription 正例与非法 load/override/rollback 负例；contract 覆盖签名边界与受保护 profile 键拒绝路径。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/contract 用例均进入 CTest 图。
4. TODO 证据回写：已完成 INF-TODO-013 状态收口与验收结果回写。
5. 提交隔离：本轮提交范围限定为 IConfigCenter 头文件、测试、CMake 注册和专项 TODO 证据文档。

## 12. ARC 修复增量（2026-03-26）

以下增量任务用于直接修复评审报告中的 ARC-01、ARC-02：

| ID | 状态 | 对应问题 | 任务描述 | 代码目标 | 测试目标 | 验收命令 | 前置依赖 | 完成判定 |
|---|---|---|---|---|---|---|---|---|
| INF-TODO-020 | Not Started | ARC-01 | 在 tracing/metrics contracts 边界测试中显式增加 planning stage 标签、预算与延迟观测断言（仅补约束，不新增共享对象） | tests/contract/infra/tracing/, tests/contract/infra/metrics/, docs/todos/infrastructure/DASALL_infrastructure_tracing组件专项TODO.md, docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md | 新增 TracePlanningStageContractTest、MetricsPlanningStageBudgetContractTest，校验 stage=planning 与 budget_ms 标签可被稳定观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -R "TracePlanningStageContractTest|MetricsPlanningStageBudgetContractTest" | INF-TODO-012 | 仅当两个 contract 用例可被 ctest 发现并稳定通过，且不引入 contracts 共享对象改动时完成 |
| INF-TODO-021 | Done | ARC-02 | 建立并启用仓库级 infra gate，默认执行 Blocked 先解阻，禁止绕过前置设计直接推进实现 | scripts/ci/infra_gate.sh, docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md | gate 脚本能检查三件套列、Blocked 条目，并分类执行 unit/contract/integration/failure；默认有 Blocked 时失败 | bash scripts/ci/infra_gate.sh | 无 | 已完成（2026-03-26）：默认模式拒绝含 Blocked 的推进；审批窗口可通过 ALLOW_BLOCKED=1 执行分类 gate，未注册类别显式 skipped |

执行说明：

1. INF-TODO-021 先于所有实现类任务执行，用于落实 ARC-02 的统一门禁。
2. INF-TODO-020 在 contract 侧完成后，回写 tracing/metrics 专项 TODO 的对应任务状态。

## 13. 本轮执行记录（2026-03-26）

### 13.1 选中任务

1. 本轮任务：INF-TODO-001。
2. 可执行性依据：无前置依赖、无阻塞项、目标限定在单个数据结构与对应 unit/contract 测试，满足“一轮只做一个原子任务”。

### 13.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.5 明确 InfraContext 六个字段，以及“缺失字段允许 unknown，不允许空指针传递”。
2. contracts/include/agent/AgentRequest.h、contracts/include/task/WorkerTask.h、contracts/include/task/WorkerLease.h 提供 request/session/trace/task/parent_task/lease 六类既有标识语义来源。
3. docs/architecture/DASSALL_Agent_architecture.md 6.11 要求多 Agent 链路保留 trace_id、task_id、lease_id、parent_task_id 以支持追踪与回放。

外部参考：

1. OpenTelemetry Overview 强调 observability signals 共享 context propagation，并要求 tracing identifiers 在跨边界传播时保持稳定关联；本任务据此保持 InfraContext 为纯标识承载层，不引入额外控制语义。

D 结论：

1. Design -> Build 映射：以 header-only InfraContext 冻结六个字段；通过 from_contracts 显式消费 AgentRequest/WorkerTask/WorkerLease 的既有标识；所有缺失或空字符串统一规范化为 unknown。
2. Build 三件套：
    - 代码目标：新增 infra/include/InfraContext.h。
    - 测试目标：新增 unit 用例验证默认值和 normalize 规则；新增 contract 用例验证只消费现有 contracts 标识语义。
    - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract。
3. D Gate：PASS。

### 13.3 Build 交付与证据

交付物：

1. infra/include/InfraContext.h：新增 InfraContext 六字段定义、unknown 默认值和 from_contracts 映射函数。
2. tests/unit/infra/InfraContextTest.cpp：覆盖默认 unknown、正常映射、空值归一化与 lease fallback。
3. tests/contract/smoke/InfraContextBoundaryContractTest.cpp：覆盖 contracts 标识语义消费边界与禁止空字符串落盘。
4. tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过。
3. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，2/2 tests passed，新增 InfraContextUnitTest 被发现并执行。
4. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，82/82 tests passed，新增 InfraContextBoundaryContractTest 被发现并执行。

Build 合规复核：

1. 代码注释：本轮仅在不自解释的 contracts/语义来源处复用既有注释风格；新增结构本身字段命名已足够自解释，无额外冗余注释。
2. 正负例覆盖：unit 与 contract 均包含正例和负例/归一化路径。
3. 测试发现性：通过 CMake 注册新增 unit/contract 用例，验收阶段以 ctest 发现和执行结果为准。
4. TODO 证据回写：已回写本节执行记录与主任务状态。
5. 提交隔离：本轮提交范围限定为 InfraContext 相关代码、测试与证据文档。

## 14. 本轮执行记录（2026-03-26 / INF-TODO-003）

### 14.1 选中任务

1. 本轮任务：INF-TODO-003。
2. 可执行性依据：仅依赖已完成的 INF-TODO-001；attrs 白名单尚未冻结，但不影响字段级数据结构与最小脱敏边界 helper 的落盘。

### 14.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.5 明确 LogEvent 稳定字段为 level、module、message、attrs、ts，且 attrs 必须可序列化、敏感字段先脱敏。
2. docs/architecture/DASALL_infra_logging模块详细设计.md 6.5 补充 message 可空、attrs 必须可序列化，并要求敏感值不可明文落盘。
3. docs/architecture/DASALL_infra_logging模块详细设计.md 6.7 明确脱敏发生在 formatter/sink 之前，因此本轮仅冻结 LogEvent 本体与最小 redaction helper，不进入 sink/formatter 实现。

外部参考：

1. OpenTelemetry Logs Data Model 要求高频稳定语义使用顶层字段，补充信息进入 Attributes 集合，且 Attributes 应保持可序列化与语义可逆；本轮据此将 attrs 冻结为稳定字符串键值映射。

D 结论：

1. Design -> Build 映射：新增 header-only LogEvent，冻结 level/module/message/attrs/ts；用 `module` 作为顶层稳定分类字段，并提供 `category()` 访问别名兼容 logging 子模块文档术语。
2. Build 三件套：
    - 代码目标：新增 infra/include/LogEvent.h。
    - 测试目标：unit 覆盖 attrs 可序列化、空 key 负例、敏感字段脱敏；contract 覆盖 contracts 标识语义仅以 plain attrs 消费且不触发共享对象扩写。
    - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract。
3. D Gate：PASS。

### 14.3 Build 交付与证据

交付物：

1. infra/include/LogEvent.h：新增 LogLevel、LogEvent、attrs 可序列化检查与最小 redaction helper。
2. tests/unit/infra/LogEventTest.cpp：覆盖 message 可空、attrs 可序列化、空 key 负例、敏感字段脱敏。
3. tests/contract/smoke/LogEventBoundaryContractTest.cpp：覆盖 contracts 标识作为 plain attrs 消费以及脱敏边界不侵入 contracts。
4. tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过。
3. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，3/3 tests passed，新增 LogEventUnitTest 被发现并执行。
4. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，83/83 tests passed，新增 LogEventBoundaryContractTest 被发现并执行。

Build 合规复核：

1. 代码注释：字段和 helper 语义可由命名直接判读，本轮不补冗余注释。
2. 正负例覆盖：unit/contract 均包含正例和负例路径。
3. 测试发现性：通过 CMake 注册进入 unit/contract 标签集合。
4. TODO 证据回写：已记录本轮设计/构建映射与交付物。
5. 提交隔离：本轮提交范围限定为 LogEvent 相关代码、测试与证据文档。

## 15. 本轮执行记录（2026-03-26 / INF-TODO-004）

### 15.1 选中任务

1. 本轮任务：INF-TODO-004。
2. 可执行性依据：仅依赖已完成的 INF-TODO-001；side_effects 细粒度对象模型虽未冻结，但不影响先收敛 action/actor/target/evidence_ref/outcome/side_effects 六字段和最小边界守卫。

### 15.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.5 明确 AuditEvent 属于 infra 私有审计对象，只允许承载 action、actor、target、evidence_ref、outcome、side_effects 等字段，并要求审计记录不可静默丢失。
2. docs/architecture/DASALL_infra_audit模块详细设计.md 6.5 与 6.8 进一步收敛了 who/what/target/outcome/evidence 的审计语义，以及主写失败时 fallback 仍需保留可观测证据的约束。
3. contracts/include/tool/ToolResult.h、contracts/include/checkpoint/RecoveryOutcome.h 及其 guards 已冻结 execution-result 边界，适合作为本轮 AuditEvent evidence_ref 的唯一 contracts 锚点来源。

外部参考：

1. OWASP Logging Cheat Sheet 建议审计事件覆盖 who/what/when/where/outcome，并将审计与普通运行日志分离；本轮据此优先冻结 actor/action/target/outcome 和 evidence_ref，而不提前混入 sink 或导出实现。
2. OpenTelemetry Logs Data Model 强调“高频且语义稳定字段放顶层、变动明细保留在 attributes/collections”；本轮据此把 outcome 与 evidence_ref 固定为顶层字段，把 side_effects 保持为最小字符串集合，不扩展为复杂子对象。

D 结论：

1. Design -> Build 映射：以 audit/AuditTypes.h 中的 header-only AuditEvent 冻结六个顶层字段，并新增 AuditEvidenceKind/AuditEvidenceRef 作为 evidence_ref 的最小类型化锚点，仅允许 ToolResult 或 RecoveryOutcome 两类 contracts 结果引用。
2. Build 三件套：
    - 代码目标：新增 infra/include/audit/AuditTypes.h。
    - 测试目标：新增 unit 用例验证必填字段和 side_effects 序列化约束；新增 contract 用例验证 evidence_ref 只接受 ToolResult/RecoveryOutcome 风格引用，不嵌入 contracts 对象本体。
    - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract。
3. D Gate：PASS。

### 15.3 Build 交付与证据

交付物：

1. infra/include/audit/AuditTypes.h：新增 AuditOutcome、AuditEvidenceKind、AuditEvidenceRef 与 AuditEvent 六字段定义，以及必填字段、contracts 引用和 side_effects 可序列化守卫。
2. tests/unit/infra/AuditEventTest.cpp：覆盖必填字段正例、缺字段负例、side_effects 空值/重复值负例。
3. tests/contract/smoke/AuditEventBoundaryContractTest.cpp：覆盖 ToolResult 与 RecoveryOutcome 引用边界，以及空 evidence_ref 拒绝路径。
4. tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成新增测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过。
3. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，4/4 tests passed，新增 AuditEventUnitTest 被发现并执行。
4. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，84/84 tests passed，新增 AuditEventBoundaryContractTest 被发现并执行。

Build 合规复核：

1. 代码注释：新增代码均为短小 header-only/测试守卫，字段与 helper 命名可直接表达语义，无需额外冗余注释。
2. 正负例覆盖：unit 与 contract 均覆盖正例和负例路径。
3. 测试发现性：通过 CMake 注册使新增 unit/contract 用例被 ctest 标签发现并执行。
4. TODO 证据回写：已回写本节执行记录与主任务状态。
5. 提交隔离：本轮提交范围限定为 AuditEvent 相关代码、测试与证据文档。

## 16. 本轮执行记录（2026-03-26 / INF-TODO-007）

### 16.1 选中任务

1. 本轮任务：INF-TODO-007。
2. 可执行性依据：无前置依赖阻塞；failed_components 项元素类型虽未细化到对象模型，但不影响先冻结 liveness/readiness/degraded/failed_components 四字段和最小一致性守卫。

### 16.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.5 已将 HealthSnapshot 固定为 `liveness/readiness/degraded/failed_components` 四字段，且明确其状态机仅在 infra 内部，不反向写入 Runtime 状态。
2. docs/architecture/DASALL_infra_health模块详细设计.md 6.1、6.5、6.8 进一步收敛了健康三态、探针失败可观测以及 failed_components 仅作为 infra 私有失败组件集合的语义。
3. contracts/include/checkpoint/RecoveryOutcome.h 明确 `final_runtime_state` 属于 recovery 结果对象，不能被 health 快照反向吸收为跨模块共享状态。

外部参考：

1. Azure Health Endpoint Monitoring pattern 建议分离 liveness/readiness，并允许服务处于“可运行但受限”的降级状态；本轮据此固定 `degraded` 为独立布尔位，而不把其折叠进 readiness。

D 结论：

1. Design -> Build 映射：以 health/HealthStateTypes.h 中的 header-only HealthSnapshot 冻结四个顶层字段，并提供最小一致性 helper，用于区分 ready、degraded、failed 三类状态，以及拒绝无效 failed_components 条目。
2. Build 三件套：
    - 代码目标：新增 infra/include/health/HealthStateTypes.h。
    - 测试目标：unit 覆盖 ready/degraded/failed 三态与非法组合；contract 覆盖 failed_components 不得反向承载 runtime state 字段名。
    - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract。
3. D Gate：PASS。

### 16.3 Build 交付与证据

交付物：

1. infra/include/health/HealthStateTypes.h：新增 HealthSnapshot 四字段定义、状态一致性 helper 与 failed_components 边界守卫。
2. tests/unit/infra/HealthSnapshotTest.cpp：覆盖 ready/degraded/failed 三态、重复/空 failed_components 负例和非法状态组合。
3. tests/contract/smoke/HealthSnapshotBoundaryContractTest.cpp：覆盖 HealthSnapshot 顶层字段保持 infra 私有、拒绝 runtime state 字段名回流到 failed_components。
4. tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成新增测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过。
3. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，5/5 tests passed，新增 HealthSnapshotUnitTest 被发现并执行。
4. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，85/85 tests passed，新增 HealthSnapshotBoundaryContractTest 被发现并执行。

Build 合规复核：

1. 代码注释：新增代码均为短小 header-only 数据结构与测试守卫，命名可直接表达语义，无需额外冗余注释。
2. 正负例覆盖：unit 与 contract 均包含正例和负例路径。
3. 测试发现性：通过 CMake 注册使新增 unit/contract 用例被 ctest 标签发现并执行。
4. TODO 证据回写：已回写本节执行记录与主任务状态。
5. 提交隔离：本轮提交范围限定为 HealthSnapshot 相关代码、测试与证据文档。

## 17. 本轮执行记录（2026-03-26 / INF-TODO-002）

### 17.1 选中任务

1. 本轮任务：INF-TODO-002。
2. 可执行性依据：仅依赖已完成的 INF-TODO-001；`execute(command)` 的 payload 签名虽未冻结，但不影响先收敛最小命令名占位、生命周期顺序和 contracts 对齐的返回语义。

### 17.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6 已明确 `IInfrastructureService` 的四个核心方法为 `init/start/stop/execute`。
2. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.7 要求 `InfraServiceFacade` 作为统一生命周期主控点，承接 `init -> start -> stop` 的编排顺序。
3. contracts/include/error/ResultCode.h 与 contracts/include/error/ErrorInfo.h 已冻结共享错误语义，满足本轮“返回 ResultCode/ErrorInfo 引用不越权”的边界约束。

D 结论：

1. Design -> Build 映射：新增 `IInfrastructureService.h`，冻结 `InfrastructureConfig`、`InfraCommandRequest`、`InfraOperationResult` 和 `InfraServiceFacade` 生命周期骨架。
2. `InfrastructureConfig` 仅保留 `profile` 最小字段，`InfraCommandRequest` 仅保留 `name` 最小字段，不提前引入 diagnostics/ota 的 payload 模型。
3. `InfraOperationResult` 统一暴露 contracts `ResultCode` 与 `ErrorInfo`，并提供最小 helper 校验错误分类与结果码的一致性。
4. `InfraServiceFacade` 仅实现 `created -> initialized -> started -> stopped` 顺序守卫与空输入校验，不下沉到具体子组件实现。
5. D Gate：PASS。

### 17.3 Build 交付与证据

交付物：

1. infra/include/IInfrastructureService.h：新增接口、最小配置/命令/结果对象与 Facade 声明。
2. infra/src/InfraServiceFacade.cpp：新增生命周期顺序守卫与最小校验实现。
3. tests/unit/infra/InfraServiceFacadeTest.cpp：覆盖启动顺序、停止顺序、空 profile 与空 command 负例。
4. tests/contract/smoke/InfrastructureServiceBoundaryContractTest.cpp：覆盖返回值仅引用 contracts `ResultCode/ErrorInfo` 类型，以及命令对象保持最小占位边界。
5. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成源码与测试注册，使 placeholder 不再是 infra 唯一真实源码入口。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过。
3. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，6/6 tests passed，新增 `InfraServiceFacadeTest` 被发现并执行。
4. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，86/86 tests passed，新增 `InfrastructureServiceBoundaryContractTest` 被发现并执行。

Build 合规复核：

1. 代码注释：本轮代码命名已直接表达 skeleton 语义，未新增冗余注释。
2. 正负例覆盖：unit 和 contract 均包含正例与负例路径。
3. 测试发现性：新增测试通过 CMake 注册并被 ctest 标签发现执行。
4. TODO 证据回写：已回写本节执行记录与主任务状态。
5. 提交隔离：本轮提交范围限定为 IInfrastructureService/InfraServiceFacade 相关代码、测试与证据文档。

## 18. 本轮执行记录（2026-03-26 / INF-TODO-005）

### 18.1 选中任务

1. 本轮任务：INF-TODO-005。
2. 可执行性依据：仅依赖已完成的 INF-TODO-003；`flush(deadline)` 的 deadline 细节虽然未冻结，但不影响先以最小占位类型冻结接口边界和 contracts 对齐的失败语义。

### 18.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6 已明确 `ILogger` 最小方法集合为 `log(event)` 与 `flush(deadline)`。
2. docs/architecture/DASALL_infra_logging模块详细设计.md 6.5、6.6、6.8 已将 `ILogger` 与 `LogEvent` 对齐，并要求日志写入失败保持可观测。
3. infra/include/LogEvent.h 已冻结 `level/module/message/attrs/ts` 结构，满足本轮日志接口输入对象的稳定前提。

D 结论：

1. Design -> Build 映射：新增 `logging/ILogger.h`，冻结 `LogFlushDeadline` 占位类型、`LogWriteResult` 返回对象与 `ILogger` 接口。
2. `LogFlushDeadline` 本轮仅保留 `timeout_ms` 最小字段与有效性守卫，不提前引入具体 scheduler/deadline 对象模型。
3. `LogWriteResult` 统一暴露 contracts `ResultCode` 与 `ErrorInfo`，确保写入失败在接口层可观测且不新增共享错误对象。
4. `ILogger` 本轮冻结 `log`、`flush` 与 `set_level` 三个方法，其中 `set_level` 只承载最小 level 控制面，不提前冻结 sink 配置对象。
5. D Gate：PASS。

### 18.3 Build 交付与证据

交付物：

1. infra/include/logging/ILogger.h：新增 `LogFlushDeadline`、`LogWriteResult` 与 `ILogger` 接口定义。
2. tests/unit/infra/LoggerInterfaceTest.cpp：通过最小 `NullLogger` 验证 `ILogger` 与 `LogEvent`/`LogFlushDeadline` 的编译与校验关系。
3. tests/contract/smoke/LoggerInterfaceBoundaryContractTest.cpp：验证 `LogWriteResult` 只引用 contracts `ResultCode/ErrorInfo`，并保持 flush deadline 为 infra 本地占位类型。
4. tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成新增测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过。
3. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，7/7 tests passed，新增 `LoggerInterfaceTest` 被发现并执行。
4. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，87/87 tests passed，新增 `LoggerInterfaceBoundaryContractTest` 被发现并执行。

Build 合规复核：

1. 代码注释：本轮头文件和测试命名已直接表达占位接口语义，未新增冗余注释。
2. 正负例覆盖：unit 和 contract 均覆盖正常输入与失败可观测路径。
3. 测试发现性：新增测试通过 CMake 注册并被 ctest 标签发现执行。
4. TODO 证据回写：已回写本节执行记录与主任务状态。
5. 提交隔离：本轮提交范围限定为 ILogger 接口、测试与证据文档。

## 19. 本轮执行记录（2026-03-26 / INF-TODO-006）

### 19.1 选中任务

1. 本轮任务：INF-TODO-006。
2. 可执行性依据：仅依赖已完成的 INF-TODO-004；`export_audit(filter)` 的 filter 模型虽然未冻结，但不影响先以最小占位类型冻结接口边界与 contracts 对齐的失败语义。

### 19.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6 已明确 `IAuditLogger` 的最小方法集合为 `write_audit(event)` 与 `export_audit(filter)`。
2. docs/architecture/DASALL_infra_audit模块详细设计.md 6.5、6.6、6.8 已将 `AuditEvent`、审计导出与失败兜底语义固定在 infra/audit 边界内。
3. infra/include/audit/AuditTypes.h 已冻结 `action/actor/target/evidence_ref/outcome/side_effects`，满足本轮审计接口输入对象的稳定前提。

D 结论：

1. Design -> Build 映射：新增 `audit/IAuditLogger.h`，冻结 `AuditExportFilter` 占位类型、`AuditWriteResult`、`AuditExportResult` 与 `IAuditLogger` 接口。
2. `AuditExportFilter` 本轮仅保留 `opaque_selector` 最小字段与有效性守卫，不提前引入按 actor/action/time-window 的真实过滤模型。
3. `AuditWriteResult` 和 `AuditExportResult` 统一暴露 contracts `ResultCode` 与 `ErrorInfo`，确保写入和导出失败在接口层可观测且不新增共享错误对象。
4. `IAuditLogger` 本轮只包含 `write_audit` 与 `export_audit` 两个方法，不提前引入 retention/health/fallback 控制接口，避免越过主 TODO 的 L2 边界。
5. D Gate：PASS。

### 19.3 Build 交付与证据

交付物：

1. infra/include/audit/IAuditLogger.h：新增 `AuditExportFilter`、`AuditWriteResult`、`AuditExportResult` 与 `IAuditLogger` 接口定义。
2. tests/unit/infra/AuditLoggerInterfaceTest.cpp：通过最小 `NullAuditLogger` 验证 `IAuditLogger` 与 `AuditEvent`/`AuditExportFilter` 的编译与校验关系。
3. tests/contract/smoke/AuditLoggerInterfaceBoundaryContractTest.cpp：验证写入/导出结果只引用 contracts `ResultCode/ErrorInfo`，并保持导出 filter 为 infra 私有占位类型。
4. tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成新增测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过。
3. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，8/8 tests passed，新增 `AuditLoggerInterfaceTest` 被发现并执行。
4. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，88/88 tests passed，新增 `AuditLoggerInterfaceBoundaryContractTest` 被发现并执行。

Build 合规复核：

1. 代码注释：本轮头文件和测试命名已直接表达占位接口语义，未新增冗余注释。
2. 正负例覆盖：unit 和 contract 均覆盖正常输入与失败可观测路径。
3. 测试发现性：新增测试通过 CMake 注册并被 ctest 标签发现执行。
4. TODO 证据回写：已回写本节执行记录与主任务状态。
5. 提交隔离：本轮提交范围限定为 IAuditLogger 接口、测试与证据文档。

## 20. 本轮执行记录（2026-03-26 / INF-TODO-008）

### 20.1 选中任务

1. 本轮任务：INF-TODO-008。
2. 可执行性依据：仅依赖已完成的 INF-TODO-007；IHealthProbe 形状和 probe timeout 细节虽未冻结，但不影响先以最小占位注册类型冻结 monitor 侧接口边界与 contracts 对齐的失败语义。

### 20.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6 已明确 IHealthMonitor 属于 infra 对外健康入口，且详细设计 6.8 要求异常路径只输出健康证据与恢复建议，不直接执行恢复动作。
2. docs/architecture/DASALL_infra_health模块详细设计.md 6.5、6.6 已给出 HealthSnapshot、ProbeDescriptor/ProbeResult 的职责边界，并明确 IHealthMonitor 至少承担 register_probe 与健康评估语义。
3. infra/include/health/HealthStateTypes.h 已冻结 liveness/readiness/degraded/failed_components 四字段，满足本轮健康评估输出对象的稳定前提。

外部参考：

1. Azure Health Endpoint Monitoring pattern 强调 liveness/readiness 分离、健康检查结果与响应时间应被定期评估，并建议把监控细节与外部探针实现解耦；本轮据此把 probe 具体形状继续留在占位引用之外，只冻结 monitor 侧注册与评估边界。

D 结论：

1. Design -> Build 映射：新增 IHealthMonitor.h，冻结 HealthProbeRegistration 占位类型、HealthMonitorRegistrationResult、HealthEvaluationResult 与 IHealthMonitor 接口。
2. HealthProbeRegistration 本轮仅保留 probe_name、probe_group、opaque_probe_ref 三个最小字段与有效性守卫，不提前引入 IHealthProbe 抽象、超时策略或订阅接口。
3. HealthMonitorRegistrationResult 与 HealthEvaluationResult 统一暴露 contracts ResultCode 与 ErrorInfo，确保注册与评估失败在接口层可观测且不新增共享错误对象。
4. IHealthMonitor 本轮只包含 register_probe 与 evaluate 两个方法，不提前冻结 get_snapshot、subscribe 或 policy/scheduler 细节，避免越过主 TODO 的 L2 边界。
5. D Gate：PASS。

### 20.3 Build 交付与证据

交付物：

1. infra/include/health/IHealthMonitor.h：新增 HealthProbeRegistration、HealthMonitorRegistrationResult、HealthEvaluationResult 与 IHealthMonitor 接口定义。
2. tests/unit/infra/HealthMonitorInterfaceTest.cpp：通过最小 NullHealthMonitor 验证 IHealthMonitor 与 HealthProbeRegistration/HealthSnapshot 的编译与校验关系。
3. tests/contract/smoke/HealthMonitorInterfaceBoundaryContractTest.cpp：验证注册/评估结果只引用 contracts ResultCode/ErrorInfo，并保持探针注册模型为 infra 私有占位类型、评估输出边界保持为 HealthSnapshot。
4. tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成新增测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过。
3. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，9/9 tests passed，新增 `HealthMonitorInterfaceTest` 被发现并执行。
4. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，89/89 tests passed，新增 `HealthMonitorInterfaceBoundaryContractTest` 被发现并执行。

Build 合规复核：

1. 代码注释：本轮头文件和测试命名已直接表达占位接口语义，若后续引入 probe 调度与订阅细节再补针对性注释。
2. 正负例覆盖：unit 和 contract 均已设计正常输入与失败可观测路径。
3. 测试发现性：新增测试已完成 CMake 注册，待 ctest 执行结果回填。
4. TODO 证据回写：已先回写设计映射与交付物，待验收结果补全。
5. 提交隔离：本轮提交范围限定为 IHealthMonitor 接口、测试与证据文档。

## 21. 本轮执行记录（2026-03-26 / INF-TODO-009）

### 21.1 选中任务

1. 本轮任务：INF-TODO-009。
2. 可执行性依据：前置依赖 INF-TODO-002、INF-TODO-005、INF-TODO-006、INF-TODO-008 已全部完成；contracts 侧虽然只有五个粗粒度 ResultCode，但不影响本轮先冻结 infra 私有码域与一对多映射规则。

### 21.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6 已显式列出 infra 错误语义和私有码域清单，其中当前轮 Build-ready 范围正对应 `INF_E_CONFIG_INVALID` 至 `INF_E_OTA_ROLLBACK_FAIL` 七项。
2. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.8 明确配置非法、日志队列溢出、审计写入失败、健康探针超时和 OTA apply/rollback 失败都必须对上游返回明确失败码，不能吞错。
3. contracts/include/error/ResultCode.h 当前只冻结了五个一级失败类别样本码，要求 infra 侧只能通过映射消费既有 contracts 语义，不能反向扩写共享 ResultCode。

外部参考：

1. OWASP Logging Cheat Sheet 强调输入校验失败、配置修改、后端连接故障和系统事件都应被一致分类并保持可观测；本轮据此把 infra 私有码域显式聚合，并限制其只映射到现有 contracts validation/provider/runtime 三类粗粒度失败语义。

D 结论：

1. Design -> Build 映射：新增 InfraErrorCode.h 与 InfraErrorCode.cpp，冻结七个 infra 私有错误码、稳定名字空间和最小 `map_infra_error_code` 映射入口。
2. 映射策略本轮只做一对多粗粒度收敛：配置非法与 OTA 校验失败归入 contracts validation；密钥不可用与健康探针超时归入 contracts provider；日志队列满、审计写失败与 OTA 回滚失败归入 contracts runtime。
3. 本轮不修改 contracts ResultCode 枚举，也不把 plugin/policy/diagnostics 扩展错误码提前并入当前 Build-ready 范围，避免越过主 TODO 的 L2 边界。
4. Build 三件套：
    - 代码目标：新增 infra/include/InfraErrorCode.h、infra/src/InfraErrorCode.cpp。
    - 测试目标：新增 unit 用例验证七个私有码名称和映射稳定性；新增 contract 用例验证映射结果只落到既有 contracts ResultCode 且名字空间保持 `INF_E_*` 私有前缀。
    - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract。
5. D Gate：PASS。

### 21.3 Build 交付与证据

交付物：

1. infra/include/InfraErrorCode.h：新增七个 infra 私有码、名字查询与 contracts 映射声明。
2. infra/src/InfraErrorCode.cpp：新增私有码到 contracts ResultCode 的最小一对多映射入口。
3. tests/unit/infra/InfraErrorCodeTest.cpp：覆盖名称稳定性、核心映射结果和七个私有码全覆盖。
4. tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp：验证 infra 私有码仅映射到既有 contracts ResultCode，且名字空间仍保持在 infra 私有边界。
5. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成源码与测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过。
3. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，10/10 tests passed，新增 `InfraErrorCodeUnitTest` 被发现并执行。
4. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，90/90 tests passed，新增 `InfraErrorCodeBoundaryContractTest` 被发现并执行。

Build 合规复核：

1. 代码注释：本轮错误码命名和映射函数足够自解释，未引入需要长注释才能读懂的控制流。
2. 正负例覆盖：unit 和 contract 均覆盖稳定正例与边界约束断言。
3. 测试发现性：新增测试与源文件均已完成 CMake 注册，待 ctest 执行结果回填。
4. TODO 证据回写：已完成设计映射、交付物与验收结果回写。
5. 提交隔离：本轮提交范围限定为 infra CMake 入口收敛与对应证据文档。

## 22. 本轮执行记录（2026-03-26 / INF-TODO-010）

### 22.1 选中任务

1. 本轮任务：INF-TODO-010。
2. 可执行性依据：INF-TODO-001 至 INF-TODO-009 已完成，当前 infra 目标虽已不止 placeholder，但 CMake 入口仍缺少“公开头文件/真实源文件按角色显式接线”的稳定结构，适合在本轮完成最小收敛。

### 22.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 7 与 8.1 要求 infra 目录具备可持续扩展的 include/src 落盘入口，而不仅是维持空库可编译。
2. 当前 infra/CMakeLists.txt 已追加少量真实源文件，但仍以零散 target_sources 形式堆叠，尚未把公开头文件作为 target 的显式入口，也未形成按角色分组的稳定接线面。
3. tests/unit/CMakeLists.txt 已把 infra 作为独立子目录接入，说明本轮只需收敛 dasall_infra 目标本身，不需要再拆 unit/contract 注册逻辑。

外部参考：

1. CMake 官方目标声明实践强调把库的公开头文件与实现源文件作为目标的一部分显式收敛，有利于后续增量接线、安装/导出和 IDE 可见性；考虑当前仓库使用的 CMake 3.16，本轮采用源文件分组加 PUBLIC_HEADER 属性的兼容写法，而不是更高版本才稳定支持的 file set 语法。

D 结论：

1. Design -> Build 映射：更新 infra/CMakeLists.txt，把现有真实源文件收敛为 `DASALL_INFRA_CORE_SOURCES`、`DASALL_INFRA_TRACING_SOURCES`，并新增 `DASALL_INFRA_PUBLIC_HEADERS` 列表，以 `PUBLIC_HEADER` 属性作为 dasall_infra 的显式公开入口。
2. placeholder 本轮允许保留为最小 non-empty 实现兜底，但必须与 InfraServiceFacade、InfraErrorCode 和 tracing anchor 并列，且不再作为唯一真实源码入口。
3. 本轮不引入新的子域 CMakeLists，也不预先接线尚未冻结的 config/secret/ota/plugin 实现文件，避免越过当前 L2 边界。
4. Build 三件套：
    - 代码目标：更新 infra/CMakeLists.txt。
    - 测试目标：验证 dasall_infra 目标可编译；追加 ctest 发现性检查，确认现有 unit/contract 入口未被本轮 CMake 收敛破坏。
    - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -N。
5. D Gate：PASS。

### 22.3 Build 交付与证据

交付物：

1. infra/CMakeLists.txt：新增 core/tracing 源文件分组和 `DASALL_INFRA_PUBLIC_HEADERS` 列表，并通过 `PUBLIC_HEADER` 属性把公开头文件与真实源文件一起收敛为 dasall_infra 目标的显式入口。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra`：通过，`ninja: no work to do.`，说明修复后的构建图已稳定生成。
3. `ctest --test-dir build-ci -N`：通过，发现 101 个测试，包含既有 infra unit 与 contract 用例，说明本轮 CMake 收敛未破坏测试发现性。

Build 合规复核：

1. 代码注释：本轮仅以分组变量名表达 CMake 角色划分，配置本身已足够自解释，无需额外冗长注释。
2. 正负例覆盖：本轮为构建入口任务，不新增运行时逻辑；以构建成功和测试发现性复核作为二值验收出口。
3. 测试发现性：已通过 `ctest -N` 复核现有 unit/contract 入口不受影响。
4. TODO 证据回写：已完成设计映射、交付物与验收结果回写。
5. 提交隔离：本轮提交范围限定为 infra CMake 接线与证据文档。

## 23. 本轮执行记录（2026-03-26 / INF-TODO-011）

### 23.1 选中任务

1. 本轮任务：INF-TODO-011。
2. 可执行性依据：INF-TODO-001 至 INF-TODO-010 已完成；当前仓库中的 `tests/unit/CMakeLists.txt` 与 `tests/unit/infra/CMakeLists.txt` 已具备 infra unit 注册入口，本轮只需完成设计约束对账、发现性验证和执行证据闭环。

### 23.2 研究与 Design 结论

本地证据：

1. `tests/unit/CMakeLists.txt` 已包含 `add_subdirectory(infra)`，说明 unit 顶层聚合入口已经存在。
2. `tests/unit/infra/CMakeLists.txt` 已注册 `InfraContext`、`LogEvent`、`AuditEvent`、`HealthSnapshot`、`InfraServiceFacade`、`ILogger`、`IAuditLogger`、`IHealthMonitor`、`InfraErrorCode` 九个 infra unit 可执行目标，并统一打上 `unit` 标签。
3. 详细设计 8.1 与 9.1 对本轮的核心要求是“注册入口稳定存在并可被 ctest 发现执行”，而不是在已有入口之上重复改写 CMake 结构。

D 结论：

1. Design -> Build 映射：本轮不新增测试代码；以现有 `tests/unit/CMakeLists.txt` 和 `tests/unit/infra/CMakeLists.txt` 作为已落盘入口，对账其是否满足专项 TODO 中的 unit 注册要求。
2. Build 三件套：
    - 代码目标：确认 `tests/unit/CMakeLists.txt` 已接入 `infra`，且 `tests/unit/infra/CMakeLists.txt` 已注册现有 infra unit 用例。
    - 测试目标：验证带 `unit` 标签的测试可发现、可执行，并包含现有 infra unit 用例。
    - 验收命令：`cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N -L unit && ctest --test-dir build-ci --output-on-failure -L unit`。
3. D Gate：PASS。

### 23.3 Build 交付与证据

交付物：

1. `tests/unit/CMakeLists.txt`：确认 unit 顶层已接入 `infra` 子目录。
2. `tests/unit/infra/CMakeLists.txt`：确认已注册 9 个 infra unit 目标，并统一声明 `unit` 标签。
3. 本文档与开发执行记录：回写 INF-TODO-011 的验证结论和验收证据。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过，`ninja: no work to do.`。
3. `ctest --test-dir build-ci -N -L unit`：通过，发现 10 个 `unit` 标签测试，其中包含 `InfraContextUnitTest`、`LogEventUnitTest`、`AuditEventUnitTest`、`HealthSnapshotUnitTest`、`InfraServiceFacadeTest`、`LoggerInterfaceTest`、`AuditLoggerInterfaceTest`、`HealthMonitorInterfaceTest`、`InfraErrorCodeUnitTest`。
4. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，10/10 tests passed。

Build 合规复核：

1. 代码注释：本轮未新增代码，只对现有 unit 注册入口进行约束对账和证据回写。
2. 正负例覆盖：本轮不新增逻辑；以现有 unit 测试集合完整发现和稳定执行作为验收出口。
3. 测试发现性：已通过 `ctest -N -L unit` 证明 infra unit 用例处于 `unit` 标签集合内。
4. TODO 证据回写：已完成主任务状态和本节执行记录回写。
5. 提交隔离：本轮提交范围限定为 INF-TODO-011 的证据闭环文档。

## 24. 本轮执行记录（2026-03-26 / INF-TODO-012）

### 24.1 选中任务

1. 本轮任务：INF-TODO-012。
2. 可执行性依据：INF-TODO-001、INF-TODO-004、INF-TODO-009 已完成；当前仓库中的 `tests/contract/CMakeLists.txt` 已具备 centralized registration 机制，并已接入首批 infra 边界 contract 用例，本轮只需完成设计约束对账、发现性验证和执行证据闭环。

### 24.2 研究与 Design 结论

本地证据：

1. `tests/contract/CMakeLists.txt` 已通过 `dasall_register_contract_test(...)` 集中注册 infra 边界用例，包含 `InfraContextBoundaryContractTest`、`LogEventBoundaryContractTest`、`AuditEventBoundaryContractTest`、`HealthSnapshotBoundaryContractTest`、`InfrastructureServiceBoundaryContractTest`、`LoggerInterfaceBoundaryContractTest`、`AuditLoggerInterfaceBoundaryContractTest`、`HealthMonitorInterfaceBoundaryContractTest`、`InfraErrorCodeBoundaryContractTest`。
2. 上述 9 个 infra contract 目标均显式链接 `dasall_infra`，并在 CTest 中统一标记为 `contract;smoke`，满足当前阶段的 centralized registration 策略。
3. 详细设计 6.5 与 9.1 对本轮的核心要求是“contracts 边界测试注册稳定存在并可阻止语义越权”，而不是在已有注册入口之上重写 contract 测试框架。

D 结论：

1. Design -> Build 映射：本轮不新增 contract 测试代码；以现有 `tests/contract/CMakeLists.txt` 和已落盘的 infra 边界测试源文件作为 contract 注册入口，对账其是否满足专项 TODO 中的 contracts 边界测试要求。
2. Build 三件套：
    - 代码目标：确认 `tests/contract/CMakeLists.txt` 已集中注册现有 infra contract 用例，并为其补齐 `dasall_infra` 依赖。
    - 测试目标：验证带 `contract` 标签的测试可发现、可执行，并包含现有 infra 边界用例。
    - 验收命令：`cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N -L contract && ctest --test-dir build-ci --output-on-failure -L contract`。
3. D Gate：PASS。

### 24.3 Build 交付与证据

交付物：

1. `tests/contract/CMakeLists.txt`：确认 centralized registration 机制已接入 9 个 infra 边界 contract 用例，并为相关目标补齐 `dasall_infra` 依赖。
2. `tests/contract/smoke/`：确认现有 infra 边界测试源文件已落盘并纳入 `contract` 标签集合。
3. 本文档与开发执行记录：回写 INF-TODO-012 的验证结论和验收证据。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过，`ninja: no work to do.`。
3. `ctest --test-dir build-ci -N -L contract`：通过，发现 90 个 `contract` 标签测试，其中包含 `InfraContextBoundaryContractTest`、`LogEventBoundaryContractTest`、`AuditEventBoundaryContractTest`、`HealthSnapshotBoundaryContractTest`、`InfrastructureServiceBoundaryContractTest`、`LoggerInterfaceBoundaryContractTest`、`AuditLoggerInterfaceBoundaryContractTest`、`HealthMonitorInterfaceBoundaryContractTest`、`InfraErrorCodeBoundaryContractTest`。
4. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，90/90 tests passed。

Build 合规复核：

1. 代码注释：本轮未新增代码，只对现有 contract 注册入口和边界用例集合进行约束对账与证据回写。
2. 正负例覆盖：本轮不新增逻辑；以现有 contract 测试集合完整发现和稳定执行作为验收出口。
3. 测试发现性：已通过 `ctest -N -L contract` 证明 infra 边界用例处于 `contract` 标签集合内。
4. TODO 证据回写：已完成主任务状态和本节执行记录回写。
5. 提交隔离：本轮提交范围限定为 INF-TODO-012 的证据闭环文档。

## 25. 本轮执行记录（2026-03-27 / INF-TODO-016）

### 25.1 选中任务

1. 本轮任务：INF-TODO-016。
2. 可执行性依据：前置依赖 INF-TODO-004、INF-TODO-006、INF-TODO-010 已完成；当前仓库仅缺 AuditService 独立组件骨架、失败兜底路径和对应测试出口，不存在前置 blocker。

### 25.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.2、6.3、6.6、6.8、6.11 已明确 AuditService 必须作为独立于 LoggingService 的组件存在，并承担写入、导出、失败兜底和降级状态输出。
2. docs/architecture/DASALL_infra_audit模块详细设计.md 6.2、6.3、6.5、6.6、6.8 已把 AuditServiceFacade、主写管线、fallback 管线、导出接口以及“审计失败不可静默丢失”约束收敛到 L2，可直接映射到最小骨架实现。
3. infra/include/audit/AuditTypes.h、infra/include/audit/IAuditLogger.h 与 infra/CMakeLists.txt 已分别冻结 AuditEvent 边界、IAuditLogger 接口和 infra 公开头文件接线，满足本轮组件落盘前提。

外部参考：

1. OWASP Logging Cheat Sheet 强调审计/安全日志应与普通运行日志分离，并要求记录 who/what/when/where/outcome，同时验证日志失败场景和降级路径；本轮据此将 AuditService 保持为独立组件，并把 fallback exhaustion 作为显式测试出口。

D 结论：

1. Design -> Build 映射：新增 infra/include/audit/AuditService.h 与 infra/src/audit/AuditService.cpp，冻结 AuditService.init/start/stop/write_audit/export_audit 的最小组件骨架，并以内存主写入 + fallback 缓冲的方式覆盖失败兜底。
2. Build 三件套：
    - 代码目标：新增 audit 独立组件头文件和源文件，并把其接入 infra/CMakeLists.txt 的公开头文件/源文件集合。
    - 测试目标：新增 AuditServiceFallbackTest 覆盖主写入降级与 fallback exhaustion；新增 AuditServiceBoundaryContractTest 验证导出边界仍保持为 AuditEvent 且 evidence_ref 不越权。
    - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N -R "AuditServiceFallbackTest|AuditServiceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "AuditServiceFallbackTest|AuditServiceBoundaryContractTest"。
3. D Gate：PASS。

### 25.3 Build 交付与证据

交付物：

1. infra/include/audit/AuditService.h：新增 AuditServiceConfig 与 AuditService 生命周期、导出和降级状态骨架。
2. infra/src/audit/AuditService.cpp：新增最小主写入/导出/fallback 实现，显式暴露 degraded 状态和 fallback exhaustion 失败路径。
3. tests/unit/infra/AuditServiceFallbackTest.cpp：覆盖主路径写入、fallback 激活和 fallback exhaustion 负例。
4. tests/contract/smoke/AuditServiceBoundaryContractTest.cpp：覆盖导出记录仍为 AuditEvent，且非 contracts evidence 引用会被拒绝。
5. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成源码与测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过。
3. `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest|AuditServiceBoundaryContractTest"`：通过，发现 2 个测试，分别为 `AuditServiceFallbackTest` 与 `AuditServiceBoundaryContractTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "AuditServiceFallbackTest|AuditServiceBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：本轮新增类和测试命名已直接表达组件骨架、降级与边界语义，无需补充冗余注释。
2. 正负例覆盖：unit 覆盖主写入正例与 fallback exhaustion 负例；contract 覆盖边界正例与非 contracts evidence 拒绝负例。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/contract 测试均进入 CTest 图。
4. TODO 证据回写：已完成主任务状态、交付物、验收命令和结果摘要回写。
5. 提交隔离：本轮提交范围限定为 AuditService 组件、测试、CMake 注册和专项 TODO 证据文档。

## 26. 本轮执行记录（2026-03-27 / INF-TODO-017）

### 26.1 选中任务

1. 本轮任务：INF-TODO-017。
2. 可执行性依据：INF-TODO-010 已完成；虽然主 TODO 中仍保留 INF-BLK-07 文案，但 docs/architecture/DASALL_infra_policy模块详细设计.md 已把规则 schema、effect、优先级、快照与回滚窗口冻结到 L2，本轮只需把该设计证据固化为头文件和测试边界。

### 26.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.5、6.6、6.8 已明确 SecurityPolicyManager 的核心对象、接口方法、PolicyDecisionRef 语义以及“策略装载失败回退上一版本快照”的约束。
2. docs/architecture/DASALL_infra_policy模块详细设计.md 6.5、6.6、6.8 已冻结 PolicyBundle、PolicyRuleDescriptor、PolicyPatch、PolicySnapshot、PolicyQueryContext、PolicyDecisionRef，以及 deny 优先、priority 显式、可 dry-run、可 rollback 的裁定规则。
3. 当前仓库在 infra/include 和 infra/src 下均无 policy 相关代码，说明本轮落盘范围可限定在对象/接口头文件与测试，不需要进入实现层。

外部参考：

1. OPA FAQ 对 conflict resolution 与 secure policy authoring 的建议强调：策略系统必须显式定义 allow/deny 冲突优先级，并优先采用 default deny 或 deny override 语义；本轮据此把 deny > require_confirmation > allow > observe 的 precedence 固化到头文件 helper 中。

D 结论：

1. Design -> Build 映射：新增 infra/include/policy/PolicyBundle.h、PolicyPatch.h、PolicySnapshot.h、PolicyDecisionRef.h、ISecurityPolicyManager.h，把最小规则 schema、补丁白名单、快照代次与决策引用边界全部冻结到编译期接口层。
2. Build 三件套：
    - 代码目标：新增 policy 对象和接口头文件，并把它们接入 infra/CMakeLists.txt 的公开头文件列表。
    - 测试目标：新增 PolicySnapshotCompatibilityTest 验证 generation/LKG/precedence/patch 白名单；新增 PolicyDecisionBoundaryTest 验证 decision 引用边界和错误类型不越权。
    - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N -R "PolicySnapshotCompatibilityTest|PolicyDecisionBoundaryTest" && ctest --test-dir build-ci --output-on-failure -R "PolicySnapshotCompatibilityTest|PolicyDecisionBoundaryTest"。
3. D Gate：PASS。

### 26.3 Build 交付与证据

交付物：

1. infra/include/policy/PolicyBundle.h：冻结 domain/effect/mode、规则对象和 effect precedence helper。
2. infra/include/policy/PolicyPatch.h：冻结 patch 白名单操作与 base_generation 契约。
3. infra/include/policy/PolicySnapshot.h：冻结 generation、source_chain、last_known_good_ref 和 rollback 判定。
4. infra/include/policy/PolicyDecisionRef.h、infra/include/policy/ISecurityPolicyManager.h：冻结 query context、decision 引用边界、ValidationReport、PolicyOpResult 与管理器接口。
5. tests/unit/infra/PolicySnapshotCompatibilityTest.cpp、tests/contract/smoke/PolicyDecisionBoundaryTest.cpp、infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成测试与公开头文件注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过。
3. `ctest --test-dir build-ci -N -R "PolicySnapshotCompatibilityTest|PolicyDecisionBoundaryTest"`：通过，发现 2 个测试，分别为 `PolicySnapshotCompatibilityTest` 与 `PolicyDecisionBoundaryTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "PolicySnapshotCompatibilityTest|PolicyDecisionBoundaryTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：本轮新增头文件以结构命名和 helper 命名直接表达 schema/precedence 语义，无需额外冗余注释。
2. 正负例覆盖：unit 覆盖快照兼容正例与非法 patch 负例；contract 覆盖 decision 边界正例与失败类型边界负例。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/contract 测试进入 CTest 图。
4. TODO 证据回写：已完成任务状态、阻塞解消说明、交付物和验收结果回写。
5. 提交隔离：本轮提交范围限定为 policy 头文件、测试、CMake 注册和专项 TODO 证据文档。

## 27. 本轮执行记录（2026-03-27 / INF-TODO-018）

### 27.1 选中任务

1. 本轮任务：INF-TODO-018。
2. 可执行性依据：INF-TODO-010、INF-TODO-012 已完成；tests 顶层已经接入 integration，且 docs/architecture/DASALL_infra_diagnostics模块详细设计.md 已把命令白名单、脱敏 profile 和导出默认策略冻结到 L2，因此 INF-BLK-08 可在本轮通过对象/接口冻结直接解阻。

### 27.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.5、6.6、6.8、6.10 已明确 DiagnosticsSnapshot、IDiagnosticsService、命令拒绝、脱敏失败和导出失败的边界约束。
2. docs/architecture/DASALL_infra_diagnostics模块详细设计.md 6.5、6.6、6.8、6.9 已冻结 DiagnosticsCommand、CommandDecision、DiagnosticsSnapshot、SnapshotExportResult、命令白名单、strict/compat redaction profile 和远程导出默认关闭策略。
3. tests/CMakeLists.txt 当前已纳入 tests/integration，说明本轮可以直接新增 InfraDiagnosticsSmokeTest，而不需要先处理 integration 拓扑 blocker。

外部参考：

1. Azure Health Endpoint Monitoring pattern 强调运维诊断/监控入口应返回最小足够信息、限制暴露内容并对敏感信息做受控访问；本轮据此把 diagnostics 冻结为只读命令白名单、strict redaction 默认值和本地导出优先接口。

D 结论：

1. Design -> Build 映射：新增 infra/include/diagnostics/DiagnosticsTypes.h 与 infra/include/diagnostics/IDiagnosticsService.h，冻结 DiagnosticsCommand、DiagnosticsSnapshot、SnapshotExportRequest/Result、DiagnosticsSnapshotResult 和最小 read-only whitelist。
2. Build 三件套：
    - 代码目标：新增 diagnostics 类型与接口头文件，并把它们接入 infra/CMakeLists.txt 的公开头文件列表；在 tests/integration 下新增 infra 子目录与 smoke 测试。
    - 测试目标：新增 DiagnosticsSnapshotExportTest 验证命令白名单、strict redaction 和本地导出请求；新增 InfraDiagnosticsSmokeTest 打通 execute -> get_snapshot -> export_snapshot 最小链路。
    - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest" && ctest --test-dir build-ci --output-on-failure -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest"。
3. D Gate：PASS。

### 27.3 Build 交付与证据

交付物：

1. infra/include/diagnostics/DiagnosticsTypes.h：冻结 diagnostics 命令白名单、redaction profile、快照与导出结果对象。
2. infra/include/diagnostics/IDiagnosticsService.h：冻结 execute/get_snapshot/export_snapshot 接口。
3. tests/unit/infra/DiagnosticsSnapshotExportTest.cpp：覆盖只读命令白名单、本地导出正例和非法远程导出负例。
4. tests/integration/infra/CMakeLists.txt、tests/integration/infra/InfraDiagnosticsSmokeTest.cpp、tests/integration/CMakeLists.txt：完成 diagnostics smoke 集成注册与最小链路测试。
5. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt：完成公开头文件与单测注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci`：通过。
3. `ctest --test-dir build-ci -N -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest"`：通过，发现 2 个测试，分别为 `DiagnosticsSnapshotExportTest` 与 `InfraDiagnosticsSmokeTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：本轮新增对象与测试命名已经直接表达 whitelist/redaction/export/smoke 语义，无需冗余注释。
2. 正负例覆盖：unit 覆盖白名单与本地导出正例、非法远程导出负例；integration 覆盖 execute/export 正例和非白名单拒绝负例。
3. 测试发现性：已通过 `ctest -N -R ...` 验证新增 unit/integration 测试进入 CTest 图。
4. TODO 证据回写：已完成任务状态、阻塞解消说明、交付物和验收结果回写。
5. 提交隔离：本轮提交范围限定为 diagnostics 头文件、unit/integration 测试、CMake 注册和专项 TODO 证据文档。