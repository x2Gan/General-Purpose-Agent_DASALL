# DASALL Capability Services 子系统专项 TODO

最近更新时间：2026-04-09（CAP-TODO-040 已完成，D1 Adapter / ResultMapper Build 已闭环）  
阶段：Detailed Design -> Special TODO  
适用范围：services/  
当前结论：D1 已完成；当前最细可安全落到 L3（公共 supporting objects / 公共接口级），内部 lane / adapter / bridge 以 L2 为主；高风险动作、route 输入与 receipt mapping 三项补设计已收敛，AdapterRouter、AdapterBridge、LocalPlatformAdapter、LocalServiceAdapter、RemoteServiceAdapter 与 ResultMapper 已落盘并通过 unit / contract 验证，下一直接执行入口转为 D2 车道、cache / config / system skeleton 与 loopback fixture 方案，剩余前置门禁主要是 loopback fixture 与 shared-contract admission。

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_capability_services子系统详细设计.md
2. docs/architecture/DASSALL_Agent_architecture.md
3. docs/architecture/DASALL_Engineering_Blueprint.md
4. docs/adr/ADR-005-architecture-review-baseline.md
5. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
6. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
7. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
8. docs/ssot/InfraConcurrencyPolicy.md
9. docs/ssot/InfraIntegrationTopology.md
10. docs/plans/DASALL_工程落地实现步骤指引.md
11. docs/development/DASALL_工程协作与编码规范.md
12. 现有 TODO / 交付基线：docs/todos/contracts/deliverables、docs/todos/infrastructure/deliverables、docs/todos/platform/deliverables、docs/todos/profiles/DASALL_profiles子系统专项TODO.md、docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md
13. 当前代码与测试现状：CMakeLists.txt、services/CMakeLists.txt、services/src/placeholder.cpp、contracts/include/boundary/InterfaceCatalog.h、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt、tests/integration/CMakeLists.txt、tests/contract/smoke/InterfaceCatalogContractTest.cpp、tests/mocks/include/MockExecutionService.h

生成原则：

1. 不改写已冻结 ADR 结论。
2. 不越过 Capability Services 子系统边界扩张到无关模块。
3. 不把纯讨论类事项伪装成 Done-ready Build 任务。
4. 每项任务都保留代码目标、测试目标、验收命令三件套。
5. 对证据不足的 adapter/internal-only supporting objects 先补设计，再推进实现。
6. 不提前把 IExecutionService / IDataService 升格到共享 contracts。

## 2. 子系统目标与范围

### 2.1 子系统目标

1. 把 tools 发起的服务调用收敛为稳定的执行、查询、订阅、诊断语义，并保持 Layer 3 Capability Services Layer 定位。
2. 为 tools 提供可治理、可裁剪、可测试的 IExecutionService / IDataService 公共 ABI，屏蔽 platform、外部执行端、本地服务和远程服务细节。
3. 在不改写 Runtime 主控权、Tool Policy Gate 校验权和 Recovery 准入权的前提下，建立 execution / data / system 三个一级子域及其内部组合根。
4. 为 Phase 2-6 的工程落地建立可追溯的 Build 骨架、测试入口、阻塞项、质量门和 interface admission 前置条件。

### 2.2 范围边界

纳入本专项 TODO 的对象：

1. services 公共 include 面、ServiceTypes、IExecutionService、IDataService。
2. ServiceFacade、ServiceContextBuilder、Execution / Data / System 子域、Adapter 路由与可观测桥的工程落位。
3. RuntimePolicySnapshot -> ServicePolicyView 派生规则、overflow/backpressure 约束、集成测试与 Gate。
4. 支撑 Build 的补设计任务、评审门禁、风险与回退策略。

不纳入本专项 TODO 的对象：

1. Runtime 的调度、budget、deadline、取消、重试、恢复与补偿裁定。
2. Tool Policy Gate 的权限、确认与审批决策逻辑。
3. platform 具体驱动、HAL、协议句柄和 infra exporter 内部实现。
4. 共享 contracts 中新增 ISystemService 或提前冻结 IExecutionService / IDataService supporting contracts。
5. Task 子系统生命周期与状态机实现。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| CAP-TC001 | services 详细设计 1.1、架构 3.4.5 | Must | services 必须作为 Layer 3 稳定服务语义层，封装外部执行控制、数据查询和系统服务语义 | 任务必须围绕 services/ 内聚，不允许 tools 直连 platform/外部执行端 |
| CAP-TC002 | 架构 5.5.1、services 详细设计 2.1 | Must | Tool 只调用 Execution Service，不直接接触驱动、脚本执行器或远程控制端 | 必须优先落 IExecutionService 公共面与高风险动作治理入口 |
| CAP-TC003 | 架构 5.5、services 详细设计 6.1.4、6.1.5 | Must | Runtime 持有调度、deadline、取消、重试、恢复与补偿裁定；Tool 持有校验、权限、确认门控；services 只持有语义路由和执行语义 | 不生成 runtime/tool 治理逻辑实现任务；ServiceFacade 只能做编排与 recheck |
| CAP-TC004 | 架构 3.7、蓝图 3.9 | Must-Not | services 不反向依赖 cognition、llm、tools 实现 | 代码目标限定在 services/、tests/、docs/；不得引入上层实现依赖 |
| CAP-TC005 | ADR-005、services 详细设计 2.1 | Must | supporting contracts 未成熟前，不得把 services 内部请求/结果对象提前冻结进 contracts | 必须把 public ABI 限定在 services/include/，并保留 shared-contract admission 门禁 |
| CAP-TC006 | InterfaceCatalog.h、InterfaceCatalogContractTest.cpp | Must | IExecutionService、IDataService 当前必须保持 AwaitingSupportingContracts | 所有接口任务都要附带 InterfaceCatalogContractTest 不回退验证 |
| CAP-TC007 | services 详细设计 2.1、6.5；架构 3.8 | Must | services 只能消费 ResultCode、ErrorInfo、RuntimeBudget 等已冻结语义，不得重定义 Observation / ErrorInfo / RuntimeBudget | ServiceTypes 与错误映射任务必须只填值、不增改 contracts 语义 |
| CAP-TC008 | ADR-007、services 详细设计 6.8 | Must-Not | services 不拥有失败语义最终裁定与恢复执行权 | ExecutionCommandLane 只能输出 side_effects、compensation_hints 和结构化错误事实 |
| CAP-TC009 | 架构 8.5、8.7、8.8；services 详细设计 6.10 | Must | 服务链路必须具备日志、指标、追踪、审计四类可观测性 | 必须拆出 ServiceAuditBridge / ServiceMetricsBridge / ServiceTraceBridge / ServiceHealthProbe 任务 |
| CAP-TC010 | services 详细设计 6.9；蓝图 3.13 | Must | services 不新增 services.* 顶层 schema，只消费 RuntimePolicySnapshot / BuildProfileManifest / infra 子域 | 必须把 ServiceConfigAdapter 设计为内部派生，不得私自扩 profile schema |
| CAP-TC011 | InfraConcurrencyPolicy.md、services 详细设计 6.3、6.9.2 | Should | 若引入内部队列或订阅缓冲，必须显式声明 overflow_policy、backpressure 与可观测计数 | ExecutionSubscriptionHub 与 command/query lane 任务必须显式回链 SSOT |
| CAP-TC012 | InfraIntegrationTopology.md、services 详细设计 9.1、9.4 | Must | 新增核心链路后必须补至少 1 个 integration smoke 用例，并保证 ctest -N 可发现 | 必须拆出 services integration 注册与 smoke/failure/profile 三类任务 |
| CAP-TC013 | 编码规范 3.2、3.3、3.6、3.7 | Must | 对外接口放 include/；跨模块暴露路径使用模块 include 根下的稳定相对子路径；禁止吞错；新增公共接口至少补一个 unit 或 contract 测试 | 头文件布局、错误处理、测试注册都必须成为显式任务 |
| CAP-TC014 | 计划文档 阶段 D | Must | 服务层要建立稳定对外接口、风险治理和可复用 Mock / Test 支撑 | TODO 顺序必须先公共接口，再治理，再测试 |
| CAP-TC015 | 蓝图 3.9、7.2 | Must | services/ 至少映射 execution/、data/、system/ 三个子目录，并以 services/include/ 作为对外接口槽位 | 目录与 CMake 接线必须在 Phase 1 率先落位 |
| CAP-TC016 | services 详细设计 6.6.1、架构 5.5.2 | Must | 状态订阅必须以 cursor/batch 公共 ABI 暴露；安全模式切换必须经 execute 的 action taxonomy 到达 | 订阅与安全模式相关任务不得膨胀出新的顶层接口 |
| CAP-TC017 | services 详细设计 1.3、6.2、10.2 | Must-Not | 当前阶段不得新增 ISystemService 共享接口 | system 子域只能 internal-only 落位，不能在 contracts 扩张接口面 |
| CAP-TC018 | services 详细设计 8.3、12.2 | Must | 高风险动作 taxonomy、adapter capability map、integration fixture 是推进后续 Build 的前置条件 | 必须把这些证据缺口显式列为补设计或解阻任务 |
| CAP-TC019 | services 详细设计 2.1 CAP-C015；Azure CQRS / Bulkhead / Compensating Transaction | Should | 命令与查询宜分车道（CQRS）；高风险执行、数据查询、系统快照宜隔离资源池（Bulkhead）；补偿步骤必须具备幂等与可恢复性（Compensating Transaction） | 是 Execution 命令/查询分车道、lane worker 隔离和 CompensationCatalog 幂等要求的设计驱动力，为 CAP-TODO-015/016/017/018 的车道拆解与资源隔离提供依据 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| CMakeLists.txt | 已 add_subdirectory(services) | services 已纳入顶层构建图，不存在根级接线缺失 |
| services/CMakeLists.txt | `dasall_services` 仅编译 src/placeholder.cpp，PUBLIC include 指向当前缺失的 services/include | 目标存在，但公共接口面与真实源码树尚未落盘 |
| services/src/placeholder.cpp | 仅保留 keep_library_non_empty 占位实现 | 服务子域尚未开始实现 |
| services/include | 当前不存在 | 无模块公共头文件、无稳定 public include 面 |
| contracts/include/boundary/InterfaceCatalog.h | IExecutionService、IDataService 已登记为 Services owner，readiness=AwaitingSupportingContracts | 当前只能做模块内冻结，不应推进共享 contracts 准入 |
| tests/contract/smoke/InterfaceCatalogContractTest.cpp | 显式断言 services 只拥有 IExecutionService / IDataService，且两者保持 awaiting 状态 | 已存在 services contract regression gate |
| tests/unit/CMakeLists.txt | 当前未接入 services 子目录与 services unit target | unit discoverability 缺失，需要显式注册 |
| tests/integration/CMakeLists.txt | 顶层 integration 拓扑已有效，但当前未接入 services 子目录 | services 可复用现有 integration topology，不需重建顶层机制 |
| tests/mocks/include/MockExecutionService.h | 只有最小 `bool execute(const std::string&)` smoke mock | 只能支撑脚手架，不代表真实 request/result 语义已冻结 |
| **路径偏差说明** | 详细设计 8.1 记录的 include 路径为 `services/include/IExecutionService.h`（无额外模块前缀）；当前仓库已落盘模块（infra/platform/profiles）与本 TODO 均采用“模块 include 根 + 相对子路径”的目录口径，而不是 `services/include/dasall/services/` 这类循环嵌套。**本 TODO 以当前工程目录口径为准**；若需调整仓库统一规范，应先回写工程蓝图与编码规范。 | — |

## 4. 粒度可行性评估

### 4.1 总体结论

结论：Capability Services 当前可直接生成 L3 / L2 混合专项 TODO，不能整体按纯 L3 推进。

当前最细可执行粒度：

1. L3：ServiceTypes 中已显式列出的公共 supporting objects，以及 IExecutionService / IDataService 两个模块内公共接口。
2. L2：ServiceContextBuilder、ServiceFacade、Execution / Data / System 子域骨架、ServiceConfigAdapter、可观测桥与健康探针。
3. L0-L1：loopback integration fixture、IExecutionService / IDataService shared-contract admission。

证据：

1. services 详细设计 6.6 已给出 15 个 public request/result/context object 字段定义和 7 个接口方法语义。
2. 6.7、6.8、6.9 已给出主流程、异常流程、错误分类、queue/overflow 策略和 RuntimePolicySnapshot 派生规则。
3. 7.1、8.1、9.1 已给出建议代码路径、测试文件槽位和阶段验收出口。
4. loopback integration fixture 与 shared-contract admission 仍停留在职责或评审门层；D1 Adapter / ResultMapper Build 已落盘为实际代码并形成稳定输入。
5. 因此当前可以直接拆 D2 车道、DataProjectionCache、SystemSnapshotLane、ServiceConfigAdapter 与 loopback fixture 方案，但仍不能提前承诺 integration/admission 的函数级落盘。

### 4.2 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| ServiceCallContext / CapabilityTargetRef / ServiceDataFreshness | 6.5、6.6 | L3 | 字段、语义、contracts 对齐约束明确 | 无实质缺口 | 直接拆到数据结构级任务 |
| Execution 请求对象族 | 6.5、6.6 | L3 | 5 个请求对象字段齐全，输入语义明确 | 无实质缺口 | 直接拆到数据结构级任务 |
| Execution 结果对象族 | 6.5、6.6、6.8 | L3 | 4 个结果对象字段、错误槽位、side_effects / compensation_hints 已明确 | 内部 evidence refs 映射还未外显为 supporting object | 先冻结 public 结果对象，再补 internal mapping 设计 |
| Data 请求 / 结果对象族 | 6.5、6.6 | L3 | 字段、读语义、from_cache 约束明确 | 无实质缺口 | 直接拆到数据结构级任务 |
| IExecutionService | 6.2、6.6、6.6.1 | L3 | 5 个方法名、输入输出、限制语义、ABI 边界明确 | supporting contracts 准入条件未满足 | 先做模块内接口冻结，不推进共享 contracts |
| IDataService | 6.2、6.6 | L3 | 2 个方法名、输入输出、只读边界明确 | supporting contracts 准入条件未满足 | 先做模块内接口冻结，不推进共享 contracts |
| ServiceContextBuilder | 6.2、6.3、6.7.1 | L2 | 组件名、输入输出、`normalize_context()` 时序点明确 | 头文件路径与完整签名未显式给出 | 先做组件级骨架和单测，不继续细化到私有 helper 级 |
| ServiceFacade | 6.2、6.3、6.4、7.1 | L2 | 组合根职责、依赖关系、测试槽位明确 | 内部成员布局未显式给出 | 先做组合根与委派骨架 |
| ExecutionCommandLane | 6.2、6.3、6.7、6.8、7.1 | L2 | 命令路径、幂等键、串行化、错误分类、补偿提示明确 | 命令车道本体尚未落盘；幂等与串行化执行面仍待实现 | D1 已完成，可直接推进命令车道实现 |
| ExecutionSubscriptionHub | 6.2、6.3、6.6.1、6.9.2 | L2 | cursor/batch 公共 ABI、`resync_required`、`drop_oldest` 已明确 | internal buffer / lease 细节未展开 | 先实现协议骨架，不暴露内部缓冲细节 |
| DataProjectionCache | 6.2、6.3、6.9.1、9.3 | L2 | TTL、stale-read、from_cache 语义明确 | query key / invalidation 细节未显式展开 | 先实现 cache 骨架，再由 DataQueryLane 消费 |
| DataQueryLane | 6.2、6.3、6.7、7.1 | L2 | 查询职责、projection/filter 输入、测试槽位明确 | DataProjectionCache 与 lane 本体尚未落盘 | 先完成 CAP-TODO-020，再推进查询车道实现 |
| SystemSnapshotLane | 6.2、6.3、7.1、12.1 | L1 | internal-only 职责、目录与测试槽位明确 | internal snapshot object 字段与稳定消费者边界未冻结 | 仅允许 internal-only skeleton，不细化到函数级 |
| ServiceConfigAdapter / ServicePolicyView | 6.2、6.5、6.9.1、6.9.2 | L2 | RuntimePolicySnapshot 派生表与固定策略明确 | `ServicePolicyView` 头文件路径与完整字段表未单列 | 可直接做组件级派生实现 |
| AdapterRouter / AdapterBridge / LocalPlatformAdapter / LocalServiceAdapter / RemoteServiceAdapter | 6.2、6.3、6.4、7.1、8.3 | L1→L2 | 路由输入输出、三类 adapter 槽位、测试槽位明确 | 无 D1 级缺口 | 已完成，可作为 D2 车道稳定依赖 |
| ResultMapper | 6.2、6.3、6.8；7.1 | L1→L2 | 错误分类表（ServiceErrorClass→ErrorInfo mapping）、side_effects / compensation_hints / evidence 约束已冻结 | 无 D1 级缺口 | 已完成，可作为 execution / data / system 车道的统一结果映射基础 |
| ServiceAuditBridge / ServiceMetricsBridge / ServiceTraceBridge / ServiceHealthProbe | 6.2、6.10、7.1、9.1 | L1-L2 | 必输字段、测试槽位、health 输出明确 | 依赖的 lane 结果面和部分 sink 映射细节未冻结 | 先在 lane / config 稳定后补组件骨架 |
| IExecutionService / IDataService shared-contract admission | 7.2、8.2 Phase 6、10.2 | L0 | admission 方向与门禁存在 | supporting contracts、integration evidence、review checklist 不完整 | 只能输出 Blocked/Review Gate，不生成实现任务 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| 公共 include 布局与模块 ABI 槽位 | 7.1、8.1；蓝图 7.3 | 目录 / 接口布局 | CAP-TODO-001、CAP-TODO-008 | 先落 services/include/ 下的稳定 public 头文件和 services CMake 骨架 |
| ServiceTypes supporting objects | 6.5、6.6 | 数据结构定义 | CAP-TODO-002、003、004、005 | 直接按字段级冻结公共 context/request/result |
| IExecutionService / IDataService 公共接口 | 6.2、6.6、6.6.1 | 接口定义 | CAP-TODO-006、007 | 先做模块内公共接口，不推进 contracts admission |
| 生命周期与统一入口 | 6.2、6.3、6.4、6.7.1 | 生命周期 / 初始化 | CAP-TODO-009、010 | 先做 context normalize 与 façade 委派骨架 |
| 高风险动作与补偿语义 | 6.6.1、6.8、8.3 | 补设计 + 异常处理 | CAP-TODO-012、015、016 | 先冻结 taxonomy，再做 command lane 与 compensation catalog |
| 查询 / 订阅 / 诊断路径 | 6.3、6.6、6.7、6.8 | 适配器 / 桥接 | CAP-TODO-017、018、019 | 依赖 route / receipt supporting objects 先补设计 |
| Data 查询与缓存 | 6.2、6.3、6.7、6.9.1 | 数据结构 / 流程 | CAP-TODO-020、021 | 先做 cache，再做 query lane |
| internal-only system 子域 | 6.2、6.3、7.1、12.1 | internal-only 组件 | CAP-TODO-022 | 只能做 internal skeleton，不生成共享接口 |
| Profile / 配置派生 | 6.5、6.9 | 配置与 Profile 裁剪 | CAP-TODO-023 | 只从 RuntimePolicySnapshot 派生 ServicePolicyView |
| 可观测桥与健康探针 | 6.10、9.1、9.4 | 桥接 / 测试门禁 | CAP-TODO-024、025、026、027 | 依赖 lane 结果面与 audit/metric/trace/health 字段口径 |
| Adapter 路由、桥接与结果映射 Build | 6.2、6.3、6.4、6.8；7.1 | 适配器 / 映射组件 Build | CAP-TODO-035、036、037、038、039、040 | 补设计完成后分步实现 AdapterRouter、AdapterBridge、三类 Adapter 和 ResultMapper；是 execution/data/system 深链路 Build 的直接前置 |
| integration fixture 与集成测试 | 7.1、8.3、9.1、9.3 | 测试与门禁 | CAP-TODO-028、029、030、031、032 | 先解阻 loopback/mock fixture，再补 smoke / failure / profile |
| Interface admission 与交付证据 | 7.2、8.2 Phase 6、9.4、10.2 | 文档 / 交付证据 | CAP-TODO-033、034 | 先收齐 supporting objects 和 integration evidence，再发起 admission review |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 任务 ID |
|---|---|---|
| 接口定义类任务 | 是 | CAP-TODO-001、006、007 |
| 数据结构定义类任务 | 是 | CAP-TODO-002、003、004、005 |
| 生命周期与初始化类任务 | 是 | CAP-TODO-008、009、010 |
| 适配器 / 桥接类任务 | 是 | CAP-TODO-013、014、015、017、018、019、024、025、026、027、028、035、036、037、038、039、040 |
| 异常与错误处理类任务 | 是 | CAP-TODO-012、014、015、016、017、018、019 |
| 配置与 Profile 裁剪类任务 | 是 | CAP-TODO-023、032 |
| 测试与门禁类任务 | 是 | CAP-TODO-011、029、030、031、032、033 |
| 文档 / 交付证据回写类任务 | 是 | CAP-TODO-034 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CAP-TODO-001 | Done | 新增 services 公共 include 布局 | 详细设计 7.1、8.1；蓝图 7.3；编码规范 3.2 | 7.1 第 1 行；8.1；蓝图 services/include/ | L2 | services/include/；services/CMakeLists.txt | ServiceTypes.h、IExecutionService.h、IDataService.h 的公共 include 根 | contract：InterfaceCatalogContractTest 不回退；unit：为后续 ServiceHeaderLayout smoke 提供可 discover 头文件面 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest` | 无 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-001-services公共include布局设计收敛.md、services/include/ServiceTypes.h、services/include/IExecutionService.h、services/include/IDataService.h、services/CMakeLists.txt public header file set；2026-04-09 已通过 configure/build 与 InterfaceCatalogContractTest | 仅当头文件可通过 services/include/ 根下的稳定 public 路径引用、`dasall_services` 可编译且 InterfaceCatalogContractTest 通过时完成 |
| CAP-TODO-002 | Done | 定义服务调用基础对象 | 详细设计 6.5、6.6；编码规范 3.4 | 6.5 supporting objects；6.6 代码草图 | L3 | services/include/ServiceTypes.h | ServiceCallContext、CapabilityTargetRef、ServiceDataFreshness | contract：不扩写 RuntimeBudget / ErrorInfo 语义；unit：基础对象编译通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest` | CAP-TODO-001 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-002-服务调用基础对象设计收敛.md、services/include/ServiceTypes.h 基础对象定义；2026-04-09 已通过构建、InterfaceCatalogContractTest 与 ServiceTypes.h 语法编译检查 | 仅当字段与 6.6 一致，且不引入新的共享 contracts helper family 时完成 |
| CAP-TODO-003 | Done | 定义 Execution 请求对象族 | 详细设计 6.5、6.6 | 6.6 Execution*Request | L3 | services/include/ServiceTypes.h | ExecutionCommandRequest、ExecutionCompensationRequest、ExecutionQueryRequest、ExecutionSubscriptionRequest、ExecutionDiagnoseRequest | contract：请求对象不直接嵌入 Observation / RecoveryOutcome；unit：请求对象可编译 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest` | CAP-TODO-001、002 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-003-Execution请求对象族设计收敛.md、services/include/ServiceTypes.h 中 Execution 请求对象；2026-04-09 已通过构建、InterfaceCatalogContractTest 与 request 类型语法编译检查 | 仅当 5 个请求对象字段齐全、只依赖 STL 与冻结 contracts 类型、并保持副作用/只读边界清晰时完成 |
| CAP-TODO-004 | Done | 定义 Execution 结果对象族 | 详细设计 6.5、6.6、6.8 | 6.6 Execution*Result；6.8 ServiceErrorClass 表 | L3 | services/include/ServiceTypes.h | ExecutionCommandResult、ExecutionQueryResult、ExecutionSubscriptionResult、ExecutionDiagnoseResult | contract：ErrorInfo.failure_type 仍由既有 contracts 承载；unit：结果对象可编译 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest` | CAP-TODO-001、002 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-004-Execution结果对象族设计收敛.md、services/include/ServiceTypes.h 中 Execution 结果对象；2026-04-09 已通过构建、InterfaceCatalogContractTest 与 result 类型语法编译检查 | 仅当 `side_effects`、`compensation_hints`、`resync_required`、`from_cache` 等字段与 6.6/6.8 一致且不引入新 contracts 语义时完成 |
| CAP-TODO-005 | Done | 定义 Data 请求与结果对象族 | 详细设计 6.5、6.6 | 6.6 Data*Request / Data*Result | L3 | services/include/ServiceTypes.h | DataQueryRequest、DataCatalogRequest、DataQueryResult、DataCatalogResult | contract：保持 query-only 语义；unit：数据对象可编译 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest` | CAP-TODO-001、002 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-005-Data请求与结果对象族设计收敛.md、services/include/ServiceTypes.h 中 Data 对象；2026-04-09 已通过构建、InterfaceCatalogContractTest 与 data 类型语法编译检查 | 仅当数据对象不承载业务写语义、`from_cache` / `catalog_json` 边界明确时完成 |
| CAP-TODO-006 | Done | 定义 IExecutionService 公共接口 | 详细设计 6.2、6.6、6.6.1；蓝图 3.9 | 6.6 IExecutionService | L3 | services/include/IExecutionService.h | execute()、compensate()、query_state()、subscribe()、diagnose() | contract：InterfaceCatalog 保持 awaiting；unit：接口头文件可编译 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest` | CAP-TODO-001、003、004 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-006-IExecutionService公共接口设计收敛.md、services/include/IExecutionService.h；2026-04-09 已通过构建、InterfaceCatalogContractTest 与 IExecutionService.h 语法编译检查 | 仅当 5 个方法签名与 6.6 一致，且未引入 `set_safe_mode()` 等额外顶层接口时完成 |
| CAP-TODO-007 | Done | 定义 IDataService 公共接口 | 详细设计 6.2、6.6；蓝图 3.9 | 6.6 IDataService | L3 | services/include/IDataService.h | query()、list_capabilities() | contract：InterfaceCatalog 保持 awaiting；unit：接口头文件可编译 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest` | CAP-TODO-001、005 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-007-IDataService公共接口设计收敛.md、services/include/IDataService.h；2026-04-09 已通过构建、InterfaceCatalogContractTest 与 IDataService.h 语法编译检查 | 仅当 2 个方法签名与 query-only / discoverability 语义一致，且未引入执行授权语义时完成 |
| CAP-TODO-008 | Done | 接线 services CMake 与源码骨架目录 | 详细设计 7.1、8.1；代码现状 | 8.1 目录建议；当前 services/CMakeLists.txt | L2 | services/CMakeLists.txt；services/src/ServiceFacade.cpp；services/src/ServiceContextBuilder.cpp；services/src/execution/；services/src/data/；services/src/system/ | services 目录骨架与 `dasall_services` 源文件接线 | build：`dasall_services` 不再是 placeholder-only；unit：后续 tests 可发现 services 目录骨架 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services && ctest --test-dir build-ci -N` | CAP-TODO-001、006、007 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-008-services-CMake与源码骨架目录接线设计收敛.md、services/CMakeLists.txt、services/src/ServiceFacade.cpp、services/src/ServiceContextBuilder.cpp、services/src/execution/placeholder.cpp、services/src/data/placeholder.cpp、services/src/system/placeholder.cpp；2026-04-09 已通过 configure、`dasall_services` 构建与 `ctest -N` discoverability 检查 | 仅当 `dasall_services` 目标可在真实源码树下构建，且不再只依赖 placeholder.cpp 时完成 |
| CAP-TODO-009 | Done | 实现 ServiceContextBuilder 上下文规范化骨架 | 详细设计 6.2、6.3、6.7.1 | 6.3 ServiceContextBuilder；6.7.1 `normalize_context()` | L2 | services/src/ServiceContextBuilder.cpp | normalize_context()；ServiceCallContext 透传规则 | unit：request_id / session_id / trace_id / tool_call_id / goal_id / budget / deadline 透传可测 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-002、008 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-009-ServiceContextBuilder上下文规范化骨架设计收敛.md、services/src/ServiceContextBuilder.h、services/src/ServiceContextBuilder.cpp、tests/unit/services/ServiceContextBuilderTest.cpp、tests/unit/CMakeLists.txt；2026-04-09 已通过 configure、`dasall_services`/`dasall_unit_tests` 构建与 `ctest -L unit` 检查 | 仅当上下文归一化不新增语义字段、不吞 budget/deadline，且负路径返回可观测失败时完成 |
| CAP-TODO-010 | Done | 实现 ServiceFacade 组合根骨架 | 详细设计 6.2、6.3、6.4、7.1 | 6.2 ServiceFacade；6.4 依赖图；7.1 ServiceFacade.cpp | L2 | services/src/ServiceFacade.cpp | IExecutionService / IDataService 委派骨架 | unit：ServiceFacade 实现两类接口且不含审批/恢复逻辑；contract：InterfaceCatalog 不回退 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest` | CAP-TODO-006、007、008、009 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-010-ServiceFacade组合根骨架设计收敛.md、services/src/ServiceFacade.h、services/src/ServiceFacade.cpp、tests/unit/services/ServiceFacadeTest.cpp、tests/unit/CMakeLists.txt；2026-04-09 已通过 configure、`dasall_services`/`dasall_unit_tests`/`dasall_contract_tests` 构建、`ctest -L unit` 与 InterfaceCatalogContractTest 检查 | 仅当 ServiceFacade 同时实现 IExecutionService / IDataService、只做编排和委派、且未引入 platform/infra 细节泄漏时完成 |
| CAP-TODO-011 | Done | 注册 services unit 测试拓扑 | 详细设计 7.1、8.1、9.1；当前 tests/unit/CMakeLists.txt 现状 | 9.1 测试矩阵；8.1 tests/unit/services/** | L2 | tests/unit/CMakeLists.txt；tests/unit/services/ | ServiceHeaderLayoutTest.cpp、ServiceFacadeTest.cpp 及后续 services unit 注册槽位 | unit：services 用例可被 `ctest -N` 与 `-L unit` 发现 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-008 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-011-services-unit测试拓扑注册设计收敛.md、tests/unit/CMakeLists.txt、tests/unit/services/CMakeLists.txt、tests/unit/services/ServiceHeaderLayoutTest.cpp；2026-04-09 已通过 configure、`dasall_unit_tests` 构建、`ctest -N` discoverability 与 `ctest -L unit` 检查 | 仅当 services unit 用例被顶层 unit 聚合 target 发现并可执行时完成 |
| CAP-TODO-012 | Done | 补齐高风险 action taxonomy 与确认映射表 | 详细设计 6.6.1、8.3、9.4；架构 5.5.2、5.5.3 | 6.6.1 安全模式与补偿收口；8.3 blocker | L0 | docs/architecture/DASALL_capability_services子系统详细设计.md | `safe_mode.enter`、`safe_mode.exit`、require_confirmation 动作集合、caller_domain/proof recheck 规则 | process：设计评审可二值通过；contract：与 execution_policy.requires_high_risk_confirmation 不冲突 | `rg -n "safe_mode|require_confirmation|allowed_tool_domains" docs/architecture/DASALL_capability_services子系统详细设计.md docs/architecture/DASSALL_Agent_architecture.md docs/architecture/DASALL_Engineering_Blueprint.md` | 无 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-012-高风险action-taxonomy与确认映射表设计收敛.md、docs/architecture/DASALL_capability_services子系统详细设计.md；2026-04-09 已通过 `rg` 校验 safe_mode / require_confirmation / allowed_tool_domains 对齐 | 仅当动作 taxonomy、确认要求和 caller domain 对齐表明文化并可回链到 6.6.1 / 9.4 时完成 |
| CAP-TODO-013 | Done | 补齐 AdapterSelection 与 route 输入契约 | 详细设计 6.3、6.4、8.3、12.2 | 6.3 AdapterRouter 输入输出；8.3 blocker | L0 | docs/architecture/DASALL_capability_services子系统详细设计.md | AdapterSelection、capability snapshot source、trust / availability 输入、fallback envelope | process：跨文档一致性评审通过 | `rg -n "AdapterSelection|capability snapshot|trust|availability|fallback" docs/architecture/DASALL_capability_services子系统详细设计.md docs/architecture/DASALL_Engineering_Blueprint.md` | 无 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-013-AdapterSelection与route输入契约设计收敛.md、docs/architecture/DASALL_capability_services子系统详细设计.md；2026-04-09 已通过 `rg` 校验 AdapterSelection / capability snapshot / trust / availability / fallback 条目 | 仅当 AdapterSelection 及其输入来源、fallback 约束和 owner 边界明确写入设计文档时完成 |
| CAP-TODO-014 | Done | 补齐 AdapterReceipt 与结果映射契约 | 详细设计 6.3、6.5、6.8、9.3 | 6.3 AdapterReceipt；6.5 contracts 对齐；6.8 ServiceErrorClass | L0 | docs/architecture/DASALL_capability_services子系统详细设计.md | AdapterReceipt、ServiceErrorClass -> ErrorInfo.failure_type 映射、evidence refs、side_effects / compensation_hints 约束 | process：设计评审可二值通过；contract：ErrorInfo 语义不回退 | `rg -n "AdapterReceipt|ServiceErrorClass|ErrorInfo|side_effects|compensation_hints" docs/architecture/DASALL_capability_services子系统详细设计.md` | 无 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-014-AdapterReceipt与结果映射契约设计收敛.md、docs/architecture/DASALL_capability_services子系统详细设计.md；2026-04-09 已通过 `rg` 校验 AdapterReceipt / ServiceErrorClass / ErrorInfo / side_effects / compensation_hints 条目 | 仅当 AdapterReceipt 字段、错误分类映射和 partial side effect 证据要求明确成表时完成 |
| CAP-TODO-015 | Todo | 实现 ExecutionCommandLane 命令车道 | 详细设计 6.2、6.3、6.7.1、6.8、7.1 | 7.1 ExecutionCommandLane.cpp；6.8 PartialSideEffect | L2 | services/src/execution/ExecutionCommandLane.cpp | execute 路径、幂等键、关键动作串行化、结构化错误输出 | unit：成功 / invalid request / partial side effect；contract：不越权修改 InterfaceCatalog readiness | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest` | CAP-TODO-008、009、010、011 | 无 | 无 | ExecutionCommandLane.cpp 与命令车道 unit 用例 | 仅当命令车道实现串行化与幂等语义、输出 side_effects / compensation_hints / ErrorInfo 且不承接恢复裁定时完成 |
| CAP-TODO-016 | Blocked | 实现 CompensationCatalog 静态补偿目录 | 详细设计 6.2、6.3、6.6.1、6.9.2 | 6.6.1 CompensationCatalog；6.9.2 static mode | L2 | services/src/execution/CompensationCatalog.cpp | capability_id + action + version -> compensation_hints | unit：仅输出提示，不自动执行补偿 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-014、015 | 无 | 完成 CAP-TODO-015 | CompensationCatalog.cpp 与补偿提示 unit 用例 | 仅当目录为 static 模式、输出幂等要求与动作先后约束且不变成自动回滚器时完成 |
| CAP-TODO-017 | Todo | 实现 ExecutionQueryLane 只读查询车道 | 详细设计 6.2、6.3、6.7、7.1 | 7.1 ExecutionQueryLane.cpp；6.8 DataStale / AdapterUnavailable | L2 | services/src/execution/ExecutionQueryLane.cpp | query_state 路径、freshness 处理、只读错误映射 | unit：只读查询不产生 side_effects | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-008、009、010、011 | 无 | 无 | ExecutionQueryLane.cpp 与查询车道 unit 用例 | 仅当查询路径不触发隐式写入、可区分 strict / allow_stale 语义且错误映射稳定时完成 |
| CAP-TODO-018 | Todo | 实现 ExecutionSubscriptionHub 订阅骨架 | 详细设计 6.2、6.3、6.6.1、6.9.2、9.3 | 6.6.1 subscribe 收口；6.9.2 `drop_oldest` | L2 | services/src/execution/ExecutionSubscriptionHub.cpp | subscribe、cursor/batch、`resync_required`、`dropped_count` | unit：overflow 后必须置 `resync_required`；process：回链 InfraConcurrencyPolicy | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit && rg -n "drop_oldest|resync_required|InfraConcurrencyPolicy" docs/architecture/DASALL_capability_services子系统详细设计.md docs/ssot/InfraConcurrencyPolicy.md` | CAP-TODO-008、010、011 | 无 | 无 | ExecutionSubscriptionHub.cpp 与订阅 unit 用例 | 仅当公共 ABI 只暴露 cursor/batch 语义、overflow 可观测且 internal buffer 实现未泄漏时完成 |
| CAP-TODO-019 | Todo | 实现 ExecutionDiagnoseService 诊断路径 | 详细设计 6.2、6.3、6.7、7.1 | 6.3 ExecutionDiagnoseService；7.1 execution diagnose scope | L2 | services/src/execution/ | diagnose 路径、`target_reachable`、`report_json` | unit：诊断路径只读、不可改变目标状态 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-008、010、011 | 无 | 无 | execution/ 诊断组件与 unit 用例 | 仅当诊断只返回 reachability / report 事实，不替代 infra diagnostics 导出时完成 |
| CAP-TODO-020 | Todo | 实现 DataProjectionCache 缓存骨架 | 详细设计 6.2、6.3、6.9.1、9.3 | 6.3 DataProjectionCache；6.9.1 TTL / stale-read 派生 | L2 | services/src/data/DataProjectionCache.cpp | cache ttl、stale-read、from_cache 标记 | unit：cache hit / miss / stale 行为可二值判定 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-008、011 | 无 | 无 | DataProjectionCache.cpp 与缓存 unit 用例 | 仅当缓存只服务只读路径、TTL 与 stale 标记符合 6.9.1，且无副作用命令结果缓存时完成 |
| CAP-TODO-021 | Blocked | 实现 DataQueryLane 查询车道 | 详细设计 6.2、6.3、6.7、7.1 | 7.1 DataQueryLane.cpp；9.1 data query test | L2 | services/src/data/DataQueryLane.cpp | query()、list_capabilities()、projection/filter 路径 | unit：stale read / cache hit-miss / projection 构建可测 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-010、011、020 | 无 | 完成 CAP-TODO-020 | DataQueryLane.cpp 与数据车道 unit 用例 | 仅当 DataQueryLane 维持 query-only 语义、显式标记 from_cache 且不掩盖 stale 事实时完成 |
| CAP-TODO-022 | Todo | 实现 SystemSnapshotLane internal-only 骨架 | 详细设计 6.2、6.3、7.1、12.1 | 7.1 SystemSnapshotLane.cpp；12.1 ISystemService 证据不足 | L1 | services/src/system/SystemSnapshotLane.cpp | internal snapshot query；internal system snapshot | unit：只做 internal snapshot，不新增共享 ABI | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-008、010、011 | 无 | 无 | SystemSnapshotLane.cpp 与 internal-only unit 用例 | 仅当 system 子域保持 internal-only、无 `ISystemService` 共享接口扩张且快照只供内部编排 / health 使用时完成 |
| CAP-TODO-023 | Todo | 实现 ServiceConfigAdapter 与 ServicePolicyView 派生 | 详细设计 6.2、6.5、6.9.1、6.9.2 | 6.2 ServiceConfigAdapter；6.9 配置映射表 | L2 | services/src/ops/ | RuntimePolicySnapshot / BuildProfileManifest -> ServicePolicyView 派生 | unit：worker / timeout / overflow / stale-read 派生可验证；contract：ProfileRuntimePolicySchemaContractTest 不回退 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -R ProfileRuntimePolicySchemaContractTest` | CAP-TODO-002、008、011 | 无 | 无 | ServiceConfigAdapter 组件与 unit 用例 | 仅当组件只消费 RuntimePolicySnapshot / BuildProfileManifest / adapter probe，且不新增 `services.*` 顶层键时完成 |
| CAP-TODO-024 | Blocked | 实现 ServiceAuditBridge | 详细设计 6.2、6.10、7.1、9.4 | 7.1 ServiceAuditBridge.cpp；9.4 Ops Gate | L2 | services/src/bridges/ServiceAuditBridge.cpp | 高风险动作前后、补偿入口、强制 fallback 拒绝的审计发射 | unit / integration：审计字段完整且与普通日志分离 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L integration` | CAP-TODO-015、016、023 | 无 | 无 | ServiceAuditBridge.cpp 与 observability 测试 | 仅当审计覆盖高风险动作与补偿入口，并保持与普通日志分离存储语义时完成 |
| CAP-TODO-025 | Blocked | 实现 ServiceMetricsBridge | 详细设计 6.2、6.10、7.1、9.1 | 7.1 ServiceMetricsBridge.cpp；9.1 metrics coverage | L2 | services/src/bridges/ServiceMetricsBridge.cpp | 请求量、成功率、P95/P99、熔断、缓存命中、overflow、补偿提示指标 | unit / integration：指标发射可被集成用例观测 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L integration` | CAP-TODO-015、017、018、020、021、023 | 无 | 无 | ServiceMetricsBridge.cpp 与 observability 测试 | 仅当指标覆盖设计要求且 exporter 异常不阻断主链路时完成 |
| CAP-TODO-026 | Blocked | 实现 ServiceTraceBridge | 详细设计 6.2、6.10、7.1 | 7.1 ServiceTraceBridge.cpp；6.10 trace fields | L2 | services/src/bridges/ServiceTraceBridge.cpp | ServiceFacade span、lane span、adapter span、external target span | unit / integration：trace 链路可串起 Tool -> Services -> Adapter -> External | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L integration` | CAP-TODO-015、017、019、021、023 | 无 | 无 | ServiceTraceBridge.cpp 与 observability 测试 | 仅当 trace 字段齐全、父子 span 关系可验证且 trace exporter 故障不吞掉主错误时完成 |
| CAP-TODO-027 | Blocked | 实现 ServiceHealthProbe | 详细设计 6.2、6.10、7.1、9.1 | 7.1 ServiceHealthProbe.cpp；6.10 HealthSnapshot | L2 | services/src/ops/ServiceHealthProbe.cpp | readiness / degraded / circuit 状态输出 | unit / integration：circuit open、adapter down、queue overflow 的健康输出可测 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L integration` | CAP-TODO-018、022、023 | 无 | 无 | ServiceHealthProbe.cpp 与 observability 测试 | 仅当 health 输出只提供事实快照、不越权裁定恢复动作且 degraded/readiness 状态可二值判定时完成 |
| CAP-TODO-028 | Todo | 收敛 loopback / mock target 集成夹具方案 | 详细设计 8.3、12.2；代码现状 | 8.3 integration fixture 缺失；12.2 loopback adapter 建议 | L1 | services/src/adapters/；tests/mocks/include/；tests/integration/services/ | loopback adapter 或 mock target 的最小可用夹具策略 | process：夹具设计评审通过；integration：可支撑 smoke 回路 | `rg -n "loopback|integration fixture|mock target|LocalPlatformAdapter|LocalServiceAdapter|RemoteServiceAdapter" docs/architecture/DASALL_capability_services子系统详细设计.md tests/mocks/include services/CMakeLists.txt` | CAP-TODO-013、014 | CAP-BLK-004 | 形成可落盘的 loopback / mock fixture 方案与最小目录约束 | 详细设计补丁、tests 方案回链 | 仅当集成夹具类型、落点路径、依赖边界和最小回路明确到可实现程度时完成 |
| CAP-TODO-035 | Done | 实现 AdapterRouter 路由组件 | 详细设计 6.2、6.3、6.4、7.1、8.3 | 6.3 AdapterRouter 输入输出；6.4 依赖图；7.1 AdapterRouter.cpp | L2 | services/src/adapters/AdapterRouter.cpp | select_adapter(capability_id, target_id, policy_view) -> AdapterSelection | unit：给定 profile / trust / availability 输入时路由选择可二值判定；语义等价 fallback 不改变动作语义 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-008、010、011、013 | 无 | 无 | docs/todos/services/deliverables/CAP-TODO-035-AdapterRouter路由组件设计收敛.md、services/src/adapters/AdapterRouter.h、services/src/adapters/AdapterRouter.cpp、tests/unit/services/adapters/AdapterRouterTest.cpp；2026-04-09 已通过 `dasall_services`/`dasall_unit_tests` 构建与 `ctest -L unit` 校验 | 仅当路由组件不越过 Runtime 给定的 fallback envelope、给定输入时路由稳定且不自发切换语义不等价后端时完成 |
| CAP-TODO-036 | Done | 实现 AdapterBridge 统一适配封装 | 详细设计 6.2、6.3、6.8；7.1 | 6.3 AdapterBridge 输入输出；6.8 AdapterReceipt；7.1 adapters/ | L2 | services/src/adapters/AdapterBridge.cpp | invoke(selection, adapter_request) -> AdapterReceipt | unit：统一返回 transport outcome / payload / latency / error / side_effects；负路径不吞错、AdapterReceipt 字段完整 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-035 | 无 | 完成 CAP-TODO-035 | docs/todos/services/deliverables/CAP-TODO-036-AdapterBridge统一适配封装设计收敛.md、services/src/adapters/AdapterBridge.h、services/src/adapters/AdapterBridge.cpp、tests/unit/services/adapters/AdapterBridgeTest.cpp；2026-04-09 已通过 `dasall_services`/`dasall_unit_tests` 构建与 `ctest -L unit` 校验 | 仅当 AdapterBridge 统一封装三类 adapter、AdapterReceipt 字段完整且错误不被吞没时完成 |
| CAP-TODO-037 | Done | 实现 LocalPlatformAdapter | 详细设计 6.2、6.3、6.4；7.1 | 6.4 LocalPlatformAdapter；7.1 adapters/LocalPlatformAdapter.cpp | L2 | services/src/adapters/LocalPlatformAdapter.cpp | platform 能力执行与状态读取适配 | unit：platform_hal disabled 时返回 RouteUnavailable；loopback/fake fixture 可替换此实现用于集成测试 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-036 | 无 | 完成 CAP-TODO-036 | docs/todos/services/deliverables/CAP-TODO-037-LocalPlatformAdapter最小骨架设计收敛.md、services/src/adapters/LocalPlatformAdapter.h、services/src/adapters/LocalPlatformAdapter.cpp、tests/unit/services/adapters/LocalPlatformAdapterTest.cpp；2026-04-09 已通过 `dasall_services`/`dasall_unit_tests` 构建与 `ctest -L unit` 校验 | 仅当 adapter 在 platform_hal enabled/disabled 两种 profile 下行为可二值判定时完成 |
| CAP-TODO-038 | Done | 实现 LocalServiceAdapter | 详细设计 6.2、6.3、6.4；7.1 | 6.4 LocalServiceAdapter；7.1 adapters/LocalServiceAdapter.cpp | L2 | services/src/adapters/LocalServiceAdapter.cpp | 本地业务服务调用适配 | unit：服务不可达时返回 AdapterUnavailable（retryable=true）；正路径可返回结构化 AdapterReceipt | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-036 | 无 | 完成 CAP-TODO-036 | docs/todos/services/deliverables/CAP-TODO-038-LocalServiceAdapter最小骨架设计收敛.md、services/src/adapters/LocalServiceAdapter.h、services/src/adapters/LocalServiceAdapter.cpp、tests/unit/services/adapters/LocalServiceAdapterTest.cpp；2026-04-09 已通过 `dasall_services`/`dasall_unit_tests` 构建与 `ctest -L unit` 校验 | 仅当 adapter 正确返回 AdapterReceipt、不可达时产生可观测错误时完成 |
| CAP-TODO-039 | Done | 实现 RemoteServiceAdapter 最小骨架 | 详细设计 6.2、6.3、6.4；7.1 | 6.4 RemoteServiceAdapter；7.1 adapters/RemoteServiceAdapter.cpp | L2 | services/src/adapters/RemoteServiceAdapter.cpp | 远程业务服务调用最小适配骨架（超时、不可达语义） | unit：超时返回 AdapterUnavailable（retryable=true）；V1 允许存根实现 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | CAP-TODO-036 | 无 | 完成 CAP-TODO-036 | docs/todos/services/deliverables/CAP-TODO-039-RemoteServiceAdapter最小骨架设计收敛.md、services/src/adapters/RemoteServiceAdapter.h、services/src/adapters/RemoteServiceAdapter.cpp、tests/unit/services/adapters/RemoteServiceAdapterTest.cpp；2026-04-09 已通过 `dasall_services`/`dasall_unit_tests` 构建与 `ctest -L unit` 校验 | 仅当超时与不可达的错误语义符合 6.8 ServiceErrorClass 映射、且不把远端副作用当本地成功时完成 |
| CAP-TODO-040 | Done | 实现 ResultMapper 结果映射组件 | 详细设计 6.2、6.3、6.8；7.1 | 6.3 ResultMapper 输入输出；6.8 ServiceErrorClass 映射表；7.1 | L2 | services/src/mapping/ResultMapper.cpp | map_result(AdapterReceipt, compensation_hints) -> 公共 result + ErrorInfo | unit：9 类 ServiceErrorClass 均映射到正确的 ErrorInfo.failure_type；PartialSideEffect 时必须携带 side_effects 和 compensation_hints；contract：不重定义 ErrorInfo 语义 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest` | CAP-TODO-014、036 | 无 | 完成 CAP-TODO-036 | docs/todos/services/deliverables/CAP-TODO-040-ResultMapper结果映射组件设计收敛.md、services/src/mapping/ResultMapper.h、services/src/mapping/ResultMapper.cpp、tests/unit/services/mapping/ResultMapperTest.cpp、tests/unit/services/mapping/CMakeLists.txt、tests/unit/services/CMakeLists.txt、tests/unit/CMakeLists.txt；2026-04-09 已通过 `dasall_services`/`dasall_unit_tests`/`dasall_contract_tests` 构建、`ctest -L unit`、`InterfaceCatalogContractTest` 校验 | 仅当全部 ServiceErrorClass 条目都有对应映射分支、PartialSideEffect 路径强制携带 evidence 且 ErrorInfo 语义不被重定义时完成 |
| CAP-TODO-029 | Blocked | 注册 services integration 测试拓扑 | 详细设计 7.1、8.1、9.1；InfraIntegrationTopology.md | 8.1 tests/integration/services/**；SSOT 顶层接线要求 | L2 | tests/integration/CMakeLists.txt；tests/integration/services/ | CapabilityServices*IntegrationTest 注册与 `integration` 标签 discoverability | integration：`ctest -N` 可发现 services 集成用例 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L integration` | CAP-TODO-011、028 | CAP-BLK-004 | 完成 CAP-TODO-028 | tests/integration/services/ 与 integration CMake 注册 | 仅当 services integration 用例被顶层 integration 聚合 target 发现并带合法标签时完成 |
| CAP-TODO-030 | Blocked | 验证 Capability Services smoke integration | 详细设计 7.1、9.1、9.4 | CapabilityServicesSmokeIntegrationTest；9.1 smoke coverage | L2 | tests/integration/services/ | Tool -> IExecutionService / IDataService -> adapter loopback -> result 最小闭环 | integration：至少 1 个 smoke 用例通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci --output-on-failure -L integration` | CAP-TODO-015、017、021、023、028、029 | CAP-BLK-004 | CAP-BLK-004 关闭且 integration topology 已注册 | smoke integration 用例与执行记录 | 仅当最小执行 / 查询闭环通过、日志/trace/audit 字段可观测且 ctest 可稳定发现时完成 |
| CAP-TODO-031 | Blocked | 验证 failure injection integration | 详细设计 7.1、9.1、9.3、9.4 | CapabilityServicesFailureIntegrationTest；9.3 failure 注入点 | L2 | tests/integration/services/ | adapter timeout、partial side effect、subscription overflow、circuit open | integration：`integration;failure` 用例通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci --output-on-failure -L integration && ctest --test-dir build-ci --output-on-failure -L failure` | CAP-TODO-015、016、018、024、025、026、027、028、029、030 | CAP-BLK-004 | CAP-BLK-004 关闭且 smoke integration 已通过 | failure integration 用例与执行记录 | 仅当 9.3 列出的关键注入点都有二值化结果，且错误/审计/指标表现符合设计时完成 |
| CAP-TODO-032 | Blocked | 验证 profile 差异 integration | 详细设计 9.1、10.2；6.9.1 派生表 | 9.1 profile 差异测试；10.2 灰度路径 | L2 | tests/integration/services/ | desktop_full 与 edge_balanced 的路由 / timeout / cache 差异 | integration：`integration;profile` 用例通过；contract：ProfileRuntimePolicySchemaContractTest 不回退 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L integration && ctest --test-dir build-ci --output-on-failure -R ProfileRuntimePolicySchemaContractTest` | CAP-TODO-023、028、029、030 | CAP-BLK-004 | 完成 CAP-TODO-028 且 smoke integration 通过 | profile integration 用例与执行记录 | 仅当 profile 差异只经 RuntimePolicySnapshot / BuildProfileManifest 派生体现，而非新增 `services.*` schema 时完成 |
| CAP-TODO-033 | Blocked | 发起 IExecutionService / IDataService admission 评审 | 详细设计 7.2、8.2 Phase 6、10.2；InterfaceCatalog 现状 | 7.2 当前无法直接映射项；10.2 V2 准备 | L0 | docs/architecture/DASALL_capability_services子系统详细设计.md；contracts/include/boundary/InterfaceCatalog.h；tests/contract/smoke/InterfaceCatalogContractTest.cpp | shared-contract readiness checklist；InterfaceCatalog readiness 决策 | contract：InterfaceCatalogContractTest 和 contract gate 通过；process：评审结论二值化 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest && ctest --test-dir build-ci --output-on-failure -L contract` | CAP-TODO-002、003、004、005、006、007、030、031、032 | CAP-BLK-005 | supporting objects 稳定、integration 证据齐全、评审门通过 | admission 评审记录、InterfaceCatalog 决策回写 | 仅当评审明确给出 admit / postpone 结论，且不破坏现有 contract gate 时完成 |
| CAP-TODO-034 | Blocked | 回写 services 专项 Gate 与交付证据 | 详细设计 9.4、10.2、11、12.2；本专项 TODO | 9.4 Gate；11 风险与阻塞；12.2 后续任务 | L2 | docs/todos/services/DASALL_capability_services子系统专项TODO.md | Gate、阻塞、验收、回退记录回写 | process：命令证据、阻塞变更、风险残留全部回写 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract && ctest --test-dir build-ci --output-on-failure -L integration` | CAP-TODO-011、029、030、031、032、033 | CAP-BLK-004、CAP-BLK-005 | 相关 blocker 全部关闭且 Gate 已实际执行 | 更新后的专项 TODO 与执行证据 | 仅当每个 Gate 都有通过/失败结论、命令证据、阻塞状态和后续动作回写时完成 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 公共 ABI 落盘 | CAP-TODO-001~007 | CAP-TODO-001 串行起步；002~005 可并行；006~007 在对象落盘后并行 | 先把 public include 面、ServiceTypes 和接口头文件稳定下来 |
| B 构建与单测骨架 | CAP-TODO-008~011 | 008 先行；009~010 并行；011 末尾收口 | 先让 services 从 placeholder-only 进入可测试骨架 |
| C 补设计解阻 | CAP-TODO-012~014 | 可并行，但必须在 D1 前完成 | 解决高风险动作、route 输入、receipt/mapping 三个证据缺口 |
| D1 Adapter / ResultMapper Build | CAP-TODO-035~040 | 035 先行；036 依赖 035；037~039 依赖 036 可并行；040 依赖 014、036 | 必须先于 D2；为 execution/data/system 各车道提供路由与结果映射基础 |
| D2 execution / data / system 子域 | CAP-TODO-015~022 | 015~019 依赖 C 与 D1；020 与 023 可并行；021 依赖 020；022 依赖 013 | 命令、查询、订阅、诊断、缓存、系统快照分车道推进；灰度约束见 7.3 |
| E 配置与观测 | CAP-TODO-023~027 | 023 先于 024~027；024 依赖 012；025~027 依赖 D2 产出 | ServicePolicyView 先稳定，再挂审计 / 指标 / trace / health |
| F 集成夹具与集成注册 | CAP-TODO-028~029 | 028 先于 029；028 可与 D1/D2 并行推进 | loopback / mock fixture 是 services integration 的直接前提；CAP-TODO-038/039 可作为 fixture 候选 |
| G 集成与失败注入 | CAP-TODO-030~032 | 030 先于 031 / 032；031 与 032 可并行 | 先打通 smoke，再做 failure / profile 差异 |
| H 评审与证据收口 | CAP-TODO-033~034 | 串行 | shared-contract admission 只能在 integration 证据之后发起 |

### 7.2 必过门禁表

| Gate ID | 门禁名称 | 触发时机 | 通过条件 | 不通过后动作 |
|---|---|---|---|---|
| CAP-GATE-01 | Public ABI 布局门 | 阶段 A 结束 | services/include/ 下的公共头文件落盘、`dasall_services` 可编译、InterfaceCatalogContractTest 通过 | 退回 CAP-TODO-001~007 |
| CAP-GATE-02 | Placeholder 退出门 | 阶段 B 结束 | services 不再仅依赖 placeholder.cpp；unit 拓扑已注册 | 退回 CAP-TODO-008~011 |
| CAP-GATE-03 | Adapter 证据冻结门 | 阶段 D 前 | CAP-BLK-001、002、003 全部关闭 | 禁止推进 Execution / Data / System 深入实现 |
| CAP-GATE-04 | Unit discoverability 门 | 阶段 D / E 前 | `ctest -N` 可发现 services unit 用例；`-L unit` 可执行 | 修复 tests/unit 接线 |
| CAP-GATE-05 | Integration discoverability 门 | 阶段 G 前 | `ctest -N` 可发现 services integration 用例且带合法标签 | 修复 CAP-TODO-028 / 029 |
| CAP-GATE-06 | 高风险审计门 | 阶段 G / H 前 | 高风险动作前后、补偿入口和强制 fallback 拒绝均有 audit 证据 | 退回 CAP-TODO-024、030、031 |
| CAP-GATE-07 | contracts 不回退门 | admission 前 | InterfaceCatalogContractTest 与 contract gate 全通过，IExecutionService / IDataService readiness 仅在评审后变化 | 退回 CAP-TODO-033 |
| CAP-GATE-08 | D2 灰度推进约束门 | D2 阶段命令车道实现前 | V1.1 的 query/diagnose-only integration smoke 至少通过一个测试用例，ServiceAuditBridge 至少有 unit 用例通过，才允许开始 V1.2/V1.3 高风险命令实现 | 退回 CAP-TODO-017/019 的 integration smoke，不得提前落 CAP-TODO-015 高风险分支代码

### 7.3 D2 阶段灰度推进约束

来源依据：详细设计 10.2 灰度路径

D2 阶段内的 Execution 子域任务必须按下列灰度顺序推进，不允许高风险命令车道先于只读路径启用：

| 灰度档位 | 启用内容 | 前置条件 | 对应任务 |
|---|---|---|---|
| V1.1 | query-only 路径、diagnose-only 路径、system snapshot（只读） | CAP-TODO-035~040（D1）全部完成 | CAP-TODO-017、019、022 |
| V1.2 | 低风险 action（audit + failure injection 用例先过） | V1.1 通过 integration smoke；ServiceAuditBridge 完成（CAP-TODO-024） | CAP-TODO-015（低风险 action 子集）、018 |
| V1.3 | require_confirmation 高风险动作（profile 白名单控制） | V1.2 通过；CAP-TODO-024 提供完整 audit gate；高风险 taxonomy 明确（CAP-TODO-012） | CAP-TODO-015（高风险 action 子集）、016 |

门禁约束：在 V1.1 的 integration smoke 通过之前，不得提交 V1.2/V1.3 相关的高风险动作实现代码（CAP-GATE-08）。

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响范围 | 影响任务 | 解阻条件 | 最小解阻动作 |
|---|---|---|---|---|---|
| CAP-BLK-001 | 已解阻：高风险 action taxonomy、require_confirmation 动作集合和 caller_domain / proof recheck 规则已在 6.6.1 / 9.4 成表 | 命令执行、审计、高风险 failure 注入 | CAP-TODO-015、016、024、030、031 | 2026-04-09 已完成 CAP-TODO-012，并与 execution_policy.requires_high_risk_confirmation / allowed_tool_domains 对齐 | 完成 CAP-TODO-012 |
| CAP-BLK-002 | 已解阻：AdapterSelection、capability snapshot source、trust / availability 输入和 fallback envelope 已在 6.3 / 6.4 / 9.4 明确成表 | route 选择、命令/查询/诊断/system 路径、integration fixture | CAP-TODO-015、017、018、019、021、022、028、030、031、035、036、037、038、039 | 2026-04-09 已完成 CAP-TODO-013，并补齐 route 输入、owner、fallback 约束与禁止越权点 | 完成 CAP-TODO-013 |
| CAP-BLK-003 | 已解阻：AdapterReceipt 字段、ResultMapper 映射规则、evidence refs 与 partial side effect 证据语义已在 6.3 / 6.5 / 6.8 / 9.4 成表 | 错误映射、补偿提示、query / subscribe / diagnose 结果、failure 注入 | CAP-TODO-015、016、017、018、019、021、030、031、036、040 | 2026-04-09 已完成 CAP-TODO-014，并冻结 receipt 字段、`ServiceErrorClass -> ErrorInfo.failure_type` 映射与 evidence 约束 | 完成 CAP-TODO-014 |
| CAP-BLK-004 | services integration loopback / mock fixture 方案未落盘 | services integration 注册、smoke / failure / profile 测试 | CAP-TODO-029、030、031、032 | 明确 fixture 类型、目录落点、依赖边界与最小闭环 | 完成 CAP-TODO-028 |
| CAP-BLK-005 | shared-contract admission 前置条件未满足：supporting objects 稳定度、integration 证据、review checklist 不完整 | IExecutionService / IDataService shared-contract 准入 | CAP-TODO-033、034 | public ABI 稳定、services integration 全量通过、评审结论明确 | 完成 CAP-TODO-033 |

## 9. 验收与质量门

### 9.1 验收命令基线

| 场景 | 基线命令 | 用途 |
|---|---|---|
| 配置 / 构建 | `cmake -S . -B build-ci -G "Unix Makefiles"` | 统一生成 build-ci |
| services 构建 | `cmake --build build-ci --target dasall_services` | 验证 services 模块本身可构建 |
| unit 聚合 | `cmake --build build-ci --target dasall_unit_tests` | 运行所有 unit 可执行目标 |
| unit 执行 | `ctest --test-dir build-ci --output-on-failure -L unit` | 验证 unit 门禁 |
| contract 聚合 | `cmake --build build-ci --target dasall_contract_tests` | 运行所有 contract 可执行目标 |
| services contract 回归 | `ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest` | 验证 services interface catalog 不回退 |
| integration 聚合 | `cmake --build build-ci --target dasall_integration_tests` | 运行 integration 可执行目标 |
| integration 发现性 | `ctest --test-dir build-ci -N` | 验证 discoverability |
| integration 执行 | `ctest --test-dir build-ci --output-on-failure -L integration` | 验证 smoke / failure / profile 集成门禁 |

### 9.2 质量门逐项回答

| 质量项 | 当前判定 | 说明 |
|---|---|---|
| 架构一致性 | 可满足 | 只要保持 Runtime / Tool / Service 控制权边界不漂移，TODO 已与 6.1.4、6.1.5 对齐 |
| ADR 边界一致性 | 可满足 | ADR-006/007/008 主要约束是“不越权”，当前任务未把上下文、恢复、全局调度拉入 services |
| contracts / 公共语义兼容性 | 可满足但有 admission 门禁 | 通过 CAP-GATE-07 控制，当前不允许把 IExecutionService / IDataService 直接升格为 shared contracts |
| 工程可实现性 | 部分直接可执行 | A-B-C 与 D1 阶段已完成；CAP-TODO-015、017、018、019、020、022、023、028 可直接进入执行；CAP-TODO-016、021 仍分别依赖 015、020，integration 阶段继续受 CAP-BLK-004~005 约束 |
| 测试可验证性 | 可满足 | 已有顶层 unit / contract / integration 聚合 target，可承接 services 用例 |
| 原子任务可执行性 | 可满足 | 公共 ABI 已拆到对象 / 接口级；内部复杂链路显式保留补设计前置 |
| 粒度可评审性 | 可满足 | 每个任务都回链到 6.x / 7.x / 8.x / 9.x 设计锚点或当前代码现状 |

## 10. 风险与回退策略

| 风险 ID | 风险描述 | 级别 | 触发条件 | 回退 / 缓解策略 |
|---|---|---|---|---|
| CAP-RISK-001 | 过早把 IExecutionService / IDataService 推进到共享 contracts | High | supporting objects 未稳定就改 InterfaceCatalog readiness | 维持模块内接口冻结；Phase 6 前只做 review checklist，不改共享 contracts |
| CAP-RISK-002 | 高风险 action taxonomy 或 caller_domain / proof recheck 漂移 | High | 命令车道绕过已冻结确认映射，或新增高风险动作未回写设计 | 保持 CAP-TODO-012 设计基线；新增高风险动作必须先回评审，再进入命令链实现 |
| CAP-RISK-003 | Adapter 自动 fallback 改变动作语义 | High | 语义不等价的 adapter 被自动切换 | 在 CAP-TODO-013 中冻结 fallback envelope；未冻结前不实现 router |
| CAP-RISK-004 | 订阅链路 silent drop | Medium | overflow 发生但未返回 `resync_required` 或 drop 计数 | 强制回链 InfraConcurrencyPolicy，保持 `drop_oldest + resync_required + dropped_count` 三联约束 |
| CAP-RISK-005 | services 私自引入 `services.*` 顶层 schema | Medium | ServiceConfigAdapter 绕过 RuntimePolicySnapshot 派生新键 | 只允许在 CAP-TODO-023 中消费既有 schema；若要新增键，先回到 profiles contract |
| CAP-RISK-006 | observability exporter 故障拖垮主链路 | Medium | metrics / trace / audit sink 异常阻断主路径 | audit 必须保留；metrics / trace 可降级，但不得吞掉主错误 |
| CAP-RISK-007 | 缺少 loopback / mock fixture 导致 integration 无法闭环 | Medium | tests/integration/services 无法形成最小回路 | 先完成 CAP-TODO-028，再进入 029~032 |

## 11. 可行性结论

### 11.1 是否可以直接进入执行

可以，但仅限部分执行。

当前可直接进入执行的任务集合：

1. CAP-TODO-001~014、035~040：公共 ABI、补设计与 D1 Adapter / ResultMapper Build 已完成。
2. CAP-TODO-015、017、018、019、020、022、023、028：命令 / 查询 / 订阅 / 诊断车道，DataProjectionCache，SystemSnapshotLane，ServiceConfigAdapter 与 loopback fixture 方案。
3. CAP-TODO-016 仍等待 CAP-TODO-015，CAP-TODO-021 仍等待 CAP-TODO-020；integration / admission 任务继续受 CAP-BLK-004~005 约束。

### 11.2 当前可落到的最细粒度

1. 公共 supporting objects 与 IExecutionService / IDataService：可落到 L3。
2. ServiceContextBuilder、ServiceFacade、DataProjectionCache、ServiceConfigAdapter：可落到 L2。
3. Execution / Query / Diagnose / Subscription / Adapter / Observability / Integration 深链路：当前只能落到 L2；其中 D1 已完成，D2 可从 015、017、018、019 起步，integration 仍继续受 CAP-BLK-004~005 约束。

### 11.3 阻止进一步细化到全量函数级的证据缺口

1. D1 Adapter / ResultMapper 代码路径已全部落盘并通过 unit / contract 验证，lane 的全量函数级实现已具备稳定的路由与结果映射基础。
2. loopback / mock integration fixture 尚无明确落盘方案，无法把 smoke / failure / profile integration 拆到用例级原子任务后直接执行。
3. IExecutionService / IDataService 的 shared-contract admission 仍明确处于 AwaitingSupportingContracts，不能把模块内接口任务误写成 contracts 编码任务。
4. observability 与 integration 深链路仍依赖 D2 产出，当前不能跳过车道实现直接承诺终态验证。

### 11.4 后续建议

1. A-B-C 与 D1 阶段已完成，当前进入 D2 Execution / Data / System 深链路实现。
2. 下一优先顺序可收敛为 `015 -> 016 -> 017 -> 018 -> 019 -> 020 -> 021 -> 022 -> 023`，其中 CAP-TODO-028 可并行推进。
3. loopback / mock fixture 方案仍需尽快落盘；在 CAP-TODO-028 未完成前，不要提前承诺 services integration 完成时间。
4. CAP-TODO-024~027 继续依赖 D2 结果面与 CAP-TODO-023，不应跳过车道实现直接落 observability 终态代码。
5. CAP-TODO-033 必须保持为评审门，不应在本轮 TODO 中默认写成 shared contracts 编码任务。