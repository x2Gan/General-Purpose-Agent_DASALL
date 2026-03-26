# DASALL infrastructure 子系统专项 TODO

最近更新时间：2026-03-24  
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
11. 当前仓库代码与测试现状：infra/CMakeLists.txt、infra/src/placeholder.cpp、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt
12. 现有 TODO 风格基线：docs/todos/contracts-freeze/、docs/todos/foundation-stage-c/WP-C1-platform-linux-infra-logging-Build开发TODO.md
- docs/todos/DASALL_infrastructure子系统专项TODO.md
- docs/todos/DASALL_infrastructure_audit组件专项TODO.md
- docs/todos/DASALL_infrastructure_config组件专项TODO.md
- docs/todos/DASALL_infrastructure_diagnostics组件专项TODO.md
- docs/todos/DASALL_infrastructure_health组件专项TODO.md
- docs/todos/DASALL_infrastructure_logging组件专项TODO.md
- docs/todos/DASALL_infrastructure_metrics组件专项TODO.md
- docs/todos/DASALL_infrastructure_ota组件专项TODO.md
- docs/todos/DASALL_infrastructure_plugin组件专项TODO.md
- docs/todos/DASALL_infrastructure_policy组件专项TODO.md
- docs/todos/DASALL_infrastructure_secret组件专项TODO.md
- docs/todos/DASALL_infrastructure_tracing组件专项TODO.md
- docs/todos/DASALL_infrastructure_watchdog组件专项TODO.md
- docs/todos/DASALL_platform_linux组件专项TODO.md
- docs/todos/DASALL_profiles子系统专项TODO.md

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
| INF-C014 | 当前代码现状 | Must | infra 仍是 placeholder-only，tests 顶层未注册 integration | 需要把 CMake/test 注册作为显式任务，而不是隐含前提 |

### 3.2 现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/CMakeLists.txt | 仅构建 src/placeholder.cpp | infra 尚未形成真实能力落盘入口 |
| infra/src/placeholder.cpp | 仅维持空库可编译 | 所有真实子域都仍未落盘 |
| infra/include/ | 当前无对外接口头文件 | 接口级任务必须先行 |
| tests/CMakeLists.txt | 仅接入 mocks、unit、contract | integration 测试拓扑尚未进入顶层构建 |
| tests/unit/CMakeLists.txt | 无 infra 子目录 | infra unit 注册点缺失 |
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
| infra CMake / tests 注册点 | 详细设计 7、8、9；当前代码现状 | L2 | 目录建议、阶段里程碑、测试矩阵、现有 CMake 骨架 | integration 顶层接入规则、infra unit 子目录 | 直接拆注册任务 |

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
| Config 四层合并与 Profile 裁剪 | 详细设计 6.3、6.6、6.9；蓝图 3.13 | 配置 | INF-TODO-013、INF-BLK-01 | 方法名存在，但 TypedConfig/patch/schema 未冻结，先补设计 |
| SecurityPolicyManager | 详细设计 6.2、6.3、6.5、6.6、6.11；DASALL_infra_policy模块详细设计.md | 接口 / 数据结构 | INF-TODO-017、INF-BLK-07 | 先冻结 PolicyBundle/PolicyPatch/PolicySnapshot/DecisionRef 与接口，再进入实现 |
| DiagnosticsService | 详细设计 6.2、6.3、6.5、6.6、6.11 | 接口 / 流程 | INF-TODO-018、INF-BLK-08 | 先冻结诊断命令域与快照模型，再推进导出链路 |
| Secret backend 与最小安全边界 | 详细设计 6.3、6.6、6.9 | 配置 / 安全 | INF-TODO-014、INF-BLK-02 | 只能先列阻塞，不得直接进入实现 |
| OTA 回滚与升级结果对象 | 详细设计 6.5、6.6、6.8、6.9 | 流程 / 数据结构 | INF-TODO-015、INF-BLK-05 | UpgradeOutcome 可冻结，但 IOTAManager 仍依赖 package schema |
| PluginManager | 详细设计 6.2、6.3、6.5、6.6、6.11 | 接口 / 流程 | INF-TODO-019、INF-BLK-09 | 先冻结 manifest/兼容与签名模型，再推进装载闭环 |
| tests/unit、tests/contract、tests/integration 注册点 | 详细设计 7、8、9；当前 tests 结构 | 测试 / 门禁 | INF-TODO-011、INF-TODO-012 | 测试注册本身就是 infra 落地前置，不是附带动作 |
| 质量门执行记录与交付证据回写 | 详细设计 8.2、9.2、11.1；当前 gate 实践 | 门禁 / 交付证据 | INF-TODO-011、INF-TODO-012 | 当前轮不单列纯文档任务，首轮交付证据以 ctest 发现性、unit/contract 执行记录与 gate 结果为准 |
| Tracing/Metrics/Watchdog 子域 | 详细设计 6.2、6.3、6.8、6.10 | 接口 / 流程 | INF-BLK-03、INF-BLK-04 | 当前只有组件职责与观测项，不足以生成安全 Build 任务 |

## 6. 原子任务清单

### 6.1 Build-ready 任务

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| INF-TODO-001 | Done | 定义 InfraContext 数据结构 | 详细设计 6.5；架构 3.8；ADR-008 | 详细设计 6.5 核心对象与 contracts 对齐关系 | L2 | infra/include/ 下新增 InfraContext 头文件，承载 request_id、session_id、trace_id、task_id、parent_task_id、lease_id | InfraContext | unit：字段默认值与 unknown 语义；contract：不越权扩写 contracts 标识语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L "unit|contract" | 无 | 无 | 无 | 数据结构头文件、基础测试、字段说明；2026-03-26 已落盘 infra/include/InfraContext.h、tests/unit/infra/InfraContextTest.cpp、tests/contract/smoke/InfraContextBoundaryContractTest.cpp | 仅当 InfraContext 字段与设计一致、编译通过、测试能验证 unknown 兜底语义时完成 |
| INF-TODO-002 | Not Started | 新增 IInfrastructureService 接口与 Facade 生命周期骨架 | 详细设计 6.2、6.3、6.6、6.7、8.1；蓝图 3.12 | 详细设计 6.6 核心接口语义定义；6.7 主流程时序 | L2 | infra/include/IInfrastructureService.h；infra/src/ 下新增/替换 InfraServiceFacade 生命周期骨架 | IInfrastructureService；InfraServiceFacade.init/start/stop/execute | unit：生命周期顺序与空实现可编译；contract：返回 ResultCode/ErrorInfo 引用不越权 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | INF-TODO-001 | execute(command) 的命令对象签名未冻结 | 先以接口骨架落盘，不实现命令细节 | 接口头文件、骨架实现、构建通过证据 | 仅当 placeholder 不再是唯一入口、接口方法与设计一致且 dasall_infra 可编译时完成 |
| INF-TODO-003 | Done | 定义 LogEvent 数据结构 | 详细设计 6.5、6.8、6.10；蓝图 3.12 | 详细设计 6.5 LogEvent；6.10 日志点/指标 | L2 | infra/include/ 下新增 LogEvent 头文件，冻结 level、module、message、attrs、ts | LogEvent | unit：attrs 可序列化约束；contract：敏感字段脱敏边界不侵入 contracts | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit | INF-TODO-001 | attrs 键白名单未冻结 | 先冻结字段与基本约束，白名单细则后补 | 数据结构头文件、最小单测；2026-03-26 已落盘 infra/include/LogEvent.h、tests/unit/infra/LogEventTest.cpp、tests/contract/smoke/LogEventBoundaryContractTest.cpp | 仅当 LogEvent 字段与设计一致、测试覆盖可序列化与脱敏前置约束时完成 |
| INF-TODO-004 | Done | 定义 AuditEvent 数据结构 | 详细设计 6.5、6.8、6.10；蓝图 3.12 | 详细设计 6.5 AuditEvent；6.8 审计 fallback；6.10 审计覆盖点 | L2 | infra/include/ 下新增 AuditEvent 头文件，冻结 action、actor、target、evidence_ref、outcome、side_effects | AuditEvent | unit：必填字段校验；contract：ToolResult/RecoveryOutcome 引用边界校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L "unit|contract" | INF-TODO-001 | side_effects 精确对象模型未冻结 | 先按字段级冻结引用关系，不扩写 side_effects 子结构 | 数据结构头文件、单测/契约测试；2026-03-26 已落盘 infra/include/AuditEvent.h、tests/unit/infra/AuditEventTest.cpp、tests/contract/smoke/AuditEventBoundaryContractTest.cpp | 仅当高风险命令审计对象字段齐备、合同测试能阻止越权字段时完成 |
| INF-TODO-005 | Not Started | 新增 ILogger 接口 | 详细设计 6.6、6.8、6.10；编码规范 3.6 | 详细设计 6.6 ILogger；6.8 queue 满兜底 | L2 | infra/include/ILogger.h | ILogger.log；ILogger.flush | unit：普通日志与 flush 接口可编译；contract：错误失败需可观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | INF-TODO-003 | flush(deadline) 的 deadline 类型未冻结 | 先以签名占位类型/前置声明冻结接口，不进入 sink 实现 | 接口头文件、编译通过证据 | 仅当 ILogger 与 LogEvent 对接关系清晰、接口可被上层包含且 dasall_infra 编译通过时完成 |
| INF-TODO-006 | Not Started | 新增 IAuditLogger 接口 | 详细设计 6.6、6.8、6.10；编码规范 3.6 | 详细设计 6.6 IAuditLogger；6.8 Audit sink 故障；6.10 高风险命令强制审计 | L2 | infra/include/audit/IAuditLogger.h | IAuditLogger.write_audit；IAuditLogger.export_audit | unit：审计写入接口可编译；contract：审计导出不越权扩写对象 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | INF-TODO-004 | export_audit(filter) 的 filter 模型未冻结 | 先冻结接口名与职责，不落地导出过滤模型 | 接口头文件、编译通过证据 | 仅当接口与 AuditEvent 匹配，且审计职责与普通日志职责分离时完成 |
| INF-TODO-007 | Not Started | 定义 HealthSnapshot 数据结构 | 详细设计 6.5、6.8、9.1 | 详细设计 6.5 HealthSnapshot；6.8 探针超时；9.1 测试矩阵 | L2 | infra/include/ 下新增 HealthSnapshot 头文件，冻结 liveness、readiness、degraded、failed_components | HealthSnapshot | unit：健康状态三值组合校验；contract：不反向写 runtime 状态 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit | 无 | failed_components 项元素类型未冻结 | 先冻结顶层状态字段与集合语义 | 数据结构头文件、单测 | 仅当 HealthSnapshot 字段与状态约束一致，且测试能区分 ready/degraded/fail 时完成 |
| INF-TODO-008 | Not Started | 新增 IHealthMonitor 接口 | 详细设计 6.6、6.8、9.1 | 详细设计 6.6 IHealthMonitor；6.8 异常与恢复时序 | L2 | infra/include/IHealthMonitor.h | IHealthMonitor.register_probe；IHealthMonitor.evaluate | unit：探针注册和评估接口可编译；contract：评价结果只输出 HealthSnapshot | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | INF-TODO-007 | IHealthProbe 形状与 probe timeout 细节未冻结 | 先冻结 monitor 侧接口，不落具体 probe 抽象 | 接口头文件、编译通过证据 | 仅当接口方法名、返回对象与设计一致，且不侵入 runtime 恢复判定时完成 |
| INF-TODO-009 | Not Started | 定义 infra 私有错误码域 | 详细设计 6.6、6.8、9.1；编码规范 3.6 | 详细设计 6.6 错误语义；9.1 failure injection | L2 | infra/include/ 下新增 infra 私有错误码枚举，并在 infra/src/ 建立最小映射入口 | INF_E_CONFIG_INVALID、INF_E_SECRET_UNAVAILABLE、INF_E_LOG_QUEUE_FULL、INF_E_AUDIT_WRITE_FAIL、INF_E_HEALTH_PROBE_TIMEOUT、INF_E_OTA_VERIFY_FAIL、INF_E_OTA_ROLLBACK_FAIL | unit：错误码可判定；contract：映射 contracts::ResultCode 时不新增共享语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L "unit|contract" | INF-TODO-002、INF-TODO-005、INF-TODO-006、INF-TODO-008 | contracts::ResultCode 细粒度映射表尚未在 infra 侧成文 | 先冻结 infra 私有码域和一对多映射规则，再补细项矩阵 | 错误码头文件、映射说明、测试 | 仅当七个私有错误码均可追溯到设计条目，且 contract 测试阻止越权映射时完成 |
| INF-TODO-010 | Not Started | 接线 infra CMake 落盘入口 | 详细设计 7、8.1、8.2；当前 infra/CMakeLists.txt 现状 | 详细设计 7 Design -> Build 映射；8.1 目录与文件落盘建议 | L2 | 更新 infra/CMakeLists.txt，使其不再只依赖 src/placeholder.cpp，并允许按子域增量接线 include/src 目录 | infra/CMakeLists.txt | build：dasall_infra 目标可在真实头文件存在时编译；test：为后续 unit/contract 注册提供目标依赖面 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | INF-TODO-001 至 INF-TODO-009 | 真实源文件数量尚少，短期仍需保留空实现兜底 | 允许保留最小 non-empty 实现，但不能再只有 placeholder-only 入口 | CMake 改动、构建通过证据 | 仅当 infra 目标能显式包含真实头文件/源文件入口，且 placeholder 不再是唯一源文件时完成 |
| INF-TODO-011 | Not Started | 注册 infra 单元测试入口 | 详细设计 8.1、9.1；当前 tests/unit/CMakeLists.txt 现状 | 详细设计 9.1 测试矩阵；编码规范 3.7 | L2 | 新增 tests/unit/infra/ 与 tests/unit/CMakeLists.txt 注册入口 | tests/unit/infra；tests/unit/CMakeLists.txt | unit：InfraContext、LogEvent、AuditEvent、HealthSnapshot、接口编译测试 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit | INF-TODO-001 至 INF-TODO-010 | 当前 unit 聚合未包含 infra 子目录 | 在 unit 顶层加入 infra 子目录并确保新增用例被发现 | 单测目录、注册入口、ctest 发现性证据 | 仅当 infra 单测可被 ctest -L unit 发现并执行时完成 |
| INF-TODO-012 | Not Started | 注册 infra contracts 边界测试入口 | 详细设计 6.5、9.1；蓝图 4.3；当前 tests/contract/CMakeLists.txt 机制 | 详细设计 6.5 contracts 对齐关系；9.1 Contract 覆盖要求 | L2 | 在 tests/contract/ 现有注册机制下新增 infra 边界用例，并扩展必要的 smoke/compatibility 断言 | tests/contract/CMakeLists.txt；tests/contract/smoke/ | contract：标识字段不越权、错误码映射不漂移、AuditEvent 引用边界稳定 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L contract | INF-TODO-001、INF-TODO-004、INF-TODO-009 | 具体测试文件名与首批边界断言尚未冻结 | 先沿用现有 centralized registration 模式，后补充断言细则 | 合同测试源文件、注册改动、执行记录 | 仅当新增 infra 合同测试被发现并能阻止 contracts 语义越权时完成 |
| INF-TODO-013 | Blocked | 定义 IConfigCenter 接口骨架 | 详细设计 6.3、6.6、6.9；蓝图 3.13 | 详细设计 6.6 IConfigCenter；6.9 配置项与默认策略 | L2 | 目标文件为 infra/include/IConfigCenter.h，但在 TypedConfig/patch/schema 冻结前禁止进入实现 | IConfigCenter.load_layers；IConfigCenter.get_typed；IConfigCenter.apply_override | unit：四层合并和覆盖次序；contract：Profile 不绕过 Audit 与 Runtime 主控链路 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | INF-BLK-01：TypedConfig、patch 模型、profiles 键命名未冻结 | 先完成配置模型补设计并确认 profiles 键命名 | 接口草案、阻塞记录 | 仅当配置模型补设计完成并评审通过后，才可从 Blocked 转 Not Started |
| INF-TODO-014 | Blocked | 定义 ISecretManager 接口骨架 | 详细设计 6.3、6.6、6.9 | 详细设计 6.6 ISecretManager；6.9 secret.backend | L1 | 目标文件为 infra/include/ISecretManager.h，但在 SecretHandle/back-end 模型冻结前禁止进入实现 | ISecretManager.get_secret；ISecretManager.rotate | unit：明文不落盘；contract：审计记录必须存在 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | INF-BLK-02：SecretHandle、RotationRequest、权限模型未冻结 | 先完成 secret 对象与权限边界补设计 | 接口草案、阻塞记录 | 仅当 secret 对象模型冻结并完成安全评审后，才可解除阻塞 |
| INF-TODO-015 | Blocked | 定义 IOTAManager 接口骨架与 UpgradeOutcome 对接点 | 详细设计 6.5、6.6、6.8、6.9 | 详细设计 6.5 UpgradeOutcome；6.6 IOTAManager；6.8 OTA 失败回滚 | L1 | 目标文件为 infra/include/IOTAManager.h 与 UpgradeOutcome 对接头文件；在 package/signature/token 模式冻结前禁止实现 | IOTAManager.precheck；IOTAManager.apply；IOTAManager.rollback；UpgradeOutcome | unit：回滚结果二值判定；integration：升级失败触发回滚 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | INF-TODO-009 | INF-BLK-05：UpgradePlan、Package、rollback token、签名与存储规范未冻结 | 先冻结 OTA 输入输出对象与签名/存储规范 | 接口草案、对象草案、阻塞记录 | 仅当 OTA 补设计完成并明确失败回滚输入输出后，才可解除阻塞 |
| INF-TODO-016 | Not Started | 新增 AuditService 独立组件骨架 | 详细设计 6.2、6.3、6.6、6.11；架构 8.8 | 详细设计 6.11 独立组件化建议；6.8 审计失败兜底 | L2 | infra/src/audit/ 组件骨架与 audit 生命周期接线；保持 logging/audit 目录分离 | AuditService.init/write/export/fallback | unit：AuditServiceFallbackTest；contract：AuditEvent 引用边界稳定 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -R "AuditServiceFallbackTest|contract" | INF-TODO-004、INF-TODO-006、INF-TODO-010 | 无 | 先落最小组件骨架，不实现复杂存储策略 | audit 目录骨架、测试与构建证据 | 仅当审计组件与 logging 目录分离且 fallback 失败路径可测试时完成 |
| INF-TODO-017 | Not Started | 冻结 SecurityPolicyManager 接口与策略对象 | 详细设计 6.2、6.5、6.6、6.11；架构 5.10/8.8；DASALL_infra_policy模块详细设计.md | 详细设计 6.5 SecurityPolicySet；6.6 ISecurityPolicyManager；policy 模块设计 6.5/6.6 | L2 | infra/include/policy/ISecurityPolicyManager.h、PolicyBundle/PolicyPatch/PolicySnapshot/PolicyDecisionRef 对象头文件 | load_policy/apply_patch/dry_run_patch/snapshot/rollback/evaluate | unit：PolicySnapshotCompatibilityTest；contract：PolicyDecisionBoundaryTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -R "PolicySnapshotCompatibilityTest|PolicyDecisionBoundaryTest" | INF-TODO-010 | INF-BLK-07：策略规则 schema 与冲突裁定顺序未冻结 | 先冻结最小规则集合、快照版本语义与 decision 引用边界 | 接口头文件、对象头文件、边界测试 | 仅当策略对象可版本化、可回滚且契约边界测试通过时完成 |
| INF-TODO-018 | Not Started | 冻结 DiagnosticsSnapshot 与 IDiagnosticsService 接口 | 详细设计 6.2、6.5、6.6、6.11；架构 9.5 | 详细设计 6.5 DiagnosticsSnapshot；6.6 IDiagnosticsService | L2 | infra/include/IDiagnosticsService.h、DiagnosticsSnapshot 对象与最小导出接口 | execute/export_snapshot；DiagnosticsSnapshot | unit：DiagnosticsSnapshotExportTest；integration：InfraDiagnosticsSmokeTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -R "DiagnosticsSnapshotExportTest|InfraDiagnosticsSmokeTest" | INF-TODO-010、INF-TODO-012 | INF-BLK-08：命令白名单与脱敏规则未冻结 | 先支持只读诊断命令与本地导出 | 接口头文件、对象头文件、最小 smoke 测试 | 仅当诊断命令执行与导出链路可测且包含脱敏前置校验时完成 |
| INF-TODO-019 | Blocked | 冻结 PluginDescriptor 与 IPluginManager 接口 | 详细设计 6.2、6.5、6.6、6.11；架构 5.10/7.5 | 详细设计 6.5 PluginDescriptor；6.6 IPluginManager | L1 | 目标文件为 infra/include/IPluginManager.h 与 PluginDescriptor 头文件；在 ABI/signature 未冻结前禁止实现装载逻辑 | discover/validate/load/unload；PluginDescriptor | unit：PluginManifestValidationTest；contract：不扩写 Tool/Skill 契约 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | INF-TODO-010 | INF-BLK-09：plugin manifest、ABI 兼容矩阵、签名链路未冻结 | 先冻结 plugin manifest 最小字段与兼容语义 | 接口草案、对象草案、阻塞记录 | 仅当插件 ABI 与签名规范冻结并评审通过后，才可解除阻塞 |

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
| 阶段 F：补设计解阻 | INF-TODO-013、INF-TODO-014、INF-TODO-015、INF-TODO-019 | 串行，且当前 Blocked | Config -> Secret -> OTA -> Plugin ABI/signature 的冻结顺序更稳妥 |

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
| INF-BLK-01 | ConfigCenter 缺少 TypedConfig、patch 模型、配置文件格式与 profiles 键命名冻结 | 配置子域 | INF-TODO-013 | 明确四层配置对象、冲突裁定规则、profiles 键命名 | 在详细设计中补齐配置对象与覆盖规则表 |
| INF-BLK-02 | SecretManager 缺少 SecretHandle、RotationRequest、权限控制与生产后端边界 | secret 子域 | INF-TODO-014 | 明确最小对象模型、权限边界和 file/kms/mock 的统一抽象 | 补齐 secret 对象表与最小安全边界说明 |
| INF-BLK-03 | Tracing/Metrics 只有组件职责，没有接口名、对象模型、导出策略与测试出口 | tracing/metrics 子域 | 后续专项 TODO | 明确 Span/Metric 对象、导出器接口、标签白名单与失败语义 | 新增 tracing/metrics 子模块详细设计 |
| INF-BLK-04 | Watchdog 缺少心跳对象、超时事件模型、与 runtime 恢复建议事件的边界 | health/watchdog 子域 | 后续专项 TODO | 明确心跳输入、deadline 模型、事件输出对象 | 新增 watchdog 详细设计或补章 |
| INF-BLK-05 | OTA 缺少 UpgradePlan、Package、rollback token、签名算法与存储规范 | ota 子域 | INF-TODO-015 | 冻结 OTA 输入输出对象与签名/存储规范 | 新增 OTA 输入输出表与包规范表 |
| INF-BLK-06 | tests 顶层未接入 integration，现有门禁只有 unit/contract | 测试门禁 | tracing/metrics/watchdog/ota 集成任务 | 明确 tests/integration 接入方式并提供 CMake 注册点 | 在 tests/CMakeLists.txt 中纳入 integration 子目录并建立发现规则 |
| INF-BLK-07 | 安全策略规则 schema 与冲突裁定顺序未冻结 | security policy 子域 | INF-TODO-017 | 明确规则对象、domain/effect、优先级、冲突裁定与回滚窗口 | 在 DASALL_infra_policy模块详细设计.md 基础上完成 schema 评审与裁定矩阵冻结 |
| INF-BLK-08 | 诊断命令白名单与脱敏规则未冻结 | diagnostics 子域 | INF-TODO-018 | 明确命令域、输出脱敏规则、导出格式 | 在详细设计中补齐诊断命令域与脱敏矩阵 |
| INF-BLK-09 | 插件 manifest、ABI 兼容矩阵与签名链路未冻结 | plugin 子域 | INF-TODO-019 | 明确 manifest 字段、ABI 兼容规则、签名校验流程 | 在详细设计中补齐插件对象表与校验流程 |

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
| integration 任务过早推进 | Medium | tests 顶层未接入 integration 就开始写集成门禁 | 暂停 integration 任务，仅保留 unit/contract |
| Profile 配置提前编码 | Medium | ConfigCenter 在 schema 未冻结前直接落实现 | 回退到 Blocked，先冻结配置模型 |

## 11. 可行性结论

### 11.1 是否可以直接进入执行

可以，但只能部分进入执行。

当前可直接执行的范围：

1. 数据结构级任务：INF-TODO-001、003、004、007。
2. 接口级任务：INF-TODO-002、005、006、008。
3. 错误码/CMake/测试注册任务：INF-TODO-009、010、011、012。
4. 缺口能力补齐任务：INF-TODO-016、017、018。

当前不得直接推进的范围：

1. IConfigCenter 真实接口落盘与实现。
2. ISecretManager 真实接口落盘与实现。
3. IOTAManager 真实接口落盘与实现。
4. IPluginManager 真实装载实现（ABI/signature 未冻结）。
5. TracingService、MetricsService、WatchdogAgent 的 Build-ready 任务。

### 11.2 当前可落到的最细粒度

当前最细可安全落到 L2：

1. 单个数据结构。
2. 单个接口头文件。
3. 单个错误码域。
4. 单个 CMake 注册点。
5. 单个测试注册入口。

当前不能安全落到 L3 的证据缺口：

1. 多数接口没有完整的输入输出对象和签名。
2. config/secret/ota 缺少关键对象模型。
3. tracing/metrics/watchdog 只有职责，没有接口与测试出口。
4. integration 拓扑尚未进入 tests 顶层注册。

### 11.3 后续建议

1. 先执行 INF-TODO-001 至 INF-TODO-012，完成 infrastructure 的 L2 冻结与测试入口接线。
2. 并行执行 INF-TODO-016、INF-TODO-017、INF-TODO-018，完成 audit/security policy/diagnostics 缺口补齐。
3. 并行补齐 INF-BLK-01 至 INF-BLK-09 对应设计缺口。
4. 基于 DASALL_infra_policy模块详细设计.md，为 security policy 生成组件级专项 TODO，并优先解掉 INF-BLK-07。
5. 仅在 Blocker 解消后，再生成 config、secret、ota、plugin、tracing、metrics、watchdog 的下一轮专项 TODO。

## 12. ARC 修复增量（2026-03-26）

以下增量任务用于直接修复评审报告中的 ARC-01、ARC-02：

| ID | 状态 | 对应问题 | 任务描述 | 代码目标 | 测试目标 | 验收命令 | 前置依赖 | 完成判定 |
|---|---|---|---|---|---|---|---|---|
| INF-TODO-020 | Not Started | ARC-01 | 在 tracing/metrics contracts 边界测试中显式增加 planning stage 标签、预算与延迟观测断言（仅补约束，不新增共享对象） | tests/contract/infra/tracing/, tests/contract/infra/metrics/, docs/todos/DASALL_infrastructure_tracing组件专项TODO.md, docs/todos/DASALL_infrastructure_metrics组件专项TODO.md | 新增 TracePlanningStageContractTest、MetricsPlanningStageBudgetContractTest，校验 stage=planning 与 budget_ms 标签可被稳定观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -R "TracePlanningStageContractTest|MetricsPlanningStageBudgetContractTest" | INF-TODO-012 | 仅当两个 contract 用例可被 ctest 发现并稳定通过，且不引入 contracts 共享对象改动时完成 |
| INF-TODO-021 | Done | ARC-02 | 建立并启用仓库级 infra gate，默认执行 Blocked 先解阻，禁止绕过前置设计直接推进实现 | scripts/ci/infra_gate.sh, docs/todos/DASALL_infrastructure子系统专项TODO.md | gate 脚本能检查三件套列、Blocked 条目，并分类执行 unit/contract/integration/failure；默认有 Blocked 时失败 | bash scripts/ci/infra_gate.sh | 无 | 已完成（2026-03-26）：默认模式拒绝含 Blocked 的推进；审批窗口可通过 ALLOW_BLOCKED=1 执行分类 gate，未注册类别显式 skipped |

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

1. Design -> Build 映射：以 header-only AuditEvent 冻结六个顶层字段，并新增 AuditEvidenceKind/AuditEvidenceRef 作为 evidence_ref 的最小类型化锚点，仅允许 ToolResult 或 RecoveryOutcome 两类 contracts 结果引用。
2. Build 三件套：
    - 代码目标：新增 infra/include/AuditEvent.h。
    - 测试目标：新增 unit 用例验证必填字段和 side_effects 序列化约束；新增 contract 用例验证 evidence_ref 只接受 ToolResult/RecoveryOutcome 风格引用，不嵌入 contracts 对象本体。
    - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract。
3. D Gate：PASS。

### 15.3 Build 交付与证据

交付物：

1. infra/include/AuditEvent.h：新增 AuditOutcome、AuditEvidenceKind、AuditEvidenceRef 与 AuditEvent 六字段定义，以及必填字段、contracts 引用和 side_effects 可序列化守卫。
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