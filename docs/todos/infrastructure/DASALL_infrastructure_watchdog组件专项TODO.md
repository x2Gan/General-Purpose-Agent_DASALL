# DASALL infrastructure 子系统 watchdog 组件专项 TODO

最近更新时间：2026-03-25  
阶段：Detailed Design -> Special TODO  
适用范围：infra/watchdog

## 1. 文档头

本文档严格基于以下输入生成，不新增超出输入边界的架构假设：

1. docs/architecture/DASALL_infra_watchdog模块详细设计.md
2. docs/architecture/DASALL_infrastructure子系统详细设计.md
3. docs/architecture/DASSALL_Agent_architecture.md
4. docs/architecture/DASALL_Engineering_Blueprint.md
5. docs/adr/ADR-005-architecture-review-baseline.md
6. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
7. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
8. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
9. docs/plans/DASALL_工程落地实现步骤指引.md
10. docs/development/DASALL_工程协作与编码规范.md
11. docs/todos/contracts/
12. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md
13. docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md
14. docs/todos/infrastructure/DASALL_infrastructure_tracing组件专项TODO.md
15. docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md
16. docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md
17. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md
18. docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md
19. 代码现状：infra/CMakeLists.txt、infra/include/、infra/src/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt

生成原则：

1. 不改写已冻结 ADR 结论。
2. 不越过 infrastructure/watchdog 组件边界。
3. 讨论事项不进入 Build-ready 实现任务。
4. 每个任务必须包含代码目标、测试目标、验收命令。
5. 设计证据不足时先列 Blocked 与补设计清单。

## 2. 子系统目标与范围

### 2.1 组件目标

1. 提供关键实体心跳注册、采集、deadline 监督能力。
2. 将超时事实输出为 TimeoutEvent、审计事件、指标与快照。
3. 与 health/runtime 形成建议-执行分离链路，只输出 RecoveryHintRequest，不执行恢复。
4. 保持 profile 可裁剪、错误可观测、contracts 语义兼容。

### 2.2 范围边界

纳入范围：

1. watchdog 接口、数据结构、错误码、主异常流程、配置模型。
2. watchdog 的 CMake 接线、unit/contract/integration/failure 测试入口规划。
3. 质量门、阻塞项、风险与回退策略。

不纳入范围：

1. runtime 的恢复执行与全局调度裁定。
2. contracts 共享对象重定义。
3. 外部 supervisor 跨进程聚合链路（v2 演进项）。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的影响 |
|---|---|---|---|---|
| WDG-TC001 | watchdog 设计 1.1/6.1；架构 5.10/8.7/11.1 | Must | watchdog 必须提供关键线程监督与超时可观测 | 必须覆盖接口、对象、主流程、异常流程与观测出口 |
| WDG-TC002 | 架构 3.7；蓝图 4.2 | Must | infra 不反向依赖业务实现 | 代码目标仅限 infra/tests/docs/cmake |
| WDG-TC003 | 蓝图 4.3；架构 3.8 | Must | 跨模块调用通过冻结 contracts 或抽象接口 | 错误映射与事件包装必须走既有 contracts 语义 |
| WDG-TC004 | ADR-005 | Must | 在冻结边界前不能以实现反改架构 | 证据不足项必须 Blocked，不得伪造实现任务 |
| WDG-TC005 | ADR-006 | Must-Not | watchdog 不承担上下文装配和 Prompt 渲染 | 禁止写入认知语义处理任务 |
| WDG-TC006 | ADR-007 | Must-Not | watchdog 不做失败归因与恢复执行 | RecoveryRequestEmitter 只能输出建议 |
| WDG-TC007 | ADR-008 | Must | watchdog 不拥有调度主权 | 任务不得引入主控状态机推进逻辑 |
| WDG-TC008 | contracts 冻结约束；watchdog 设计 6.5/6.6 | Must-Not | watchdog 实现细节不进入 contracts | 仅定义 infra 私有对象和私有错误码域 |
| WDG-TC009 | 工程规范 3.6 | Must | 失败不可吞错，必须可观测 | 异常路径任务必须绑定日志/指标/审计任一出口 |
| WDG-TC010 | 工程规范 3.7 | Should | 公共接口应配套测试 | 所有接口任务必须附 unit 或 contract 测试 |
| WDG-TC011 | watchdog 设计 6.9；蓝图 5.1 | Must | profile 只能裁剪能力，不绕过审计和主控链路 | 配置任务必须有覆盖优先级和回退策略 |
| WDG-TC012 | 落地步骤指引 阶段 C | Must | infra 底座建设必须伴随可验证门禁 | 执行顺序先接口对象，再链路，再门禁 |
| WDG-TC013 | watchdog 设计 11.1 | Must | 时钟/线程抽象、事件总线、恢复建议结构、profile 键名存在阻塞 | 需要显式 Blocked 任务与解阻动作 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/CMakeLists.txt | 已接入 core/audit/plugin/tracing 等真实源码 | watchdog 公共接口已落盘，但 watchdog 服务实现尚未接入构建 |
| infra/include/ | 已形成“根目录共享契约 + 组件目录公共接口”布局，watchdog/ 子目录已落盘接口与对象 | watchdog public headers 已冻结，后续差距集中在服务实现 |
| infra/src/ | 无 watchdog 子目录实现 | watchdog 主链路未落盘 |
| tests/CMakeLists.txt | 已接入 mocks/unit/contract/integration 并提供 dasall_integration_tests 聚合入口 | integration 拓扑已接入顶层，后续只需补 watchdog 具体集成/故障用例 |
| tests/unit/CMakeLists.txt | 已接入 infra 子目录 | watchdog unit 发现性已建立，后续只需补具体用例 |
| tests/contract/CMakeLists.txt | centralized registration 已可复用 | watchdog contract 边界可接入 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：可直接生成并执行 L3/L2 混合专项 TODO。  
当前最小可执行粒度：函数/接口/数据结构级（L3 为主，受阻项为 L2/L0）。

证据：

1. 有明确核心接口清单：IWatchdogService、IHeartbeatSource、ITimeoutPolicy（watchdog 设计 6.6）。
2. 有核心对象字段：WatchedEntityDescriptor、HeartbeatSample、TimeoutDecision、WatchdogSnapshot、RecoveryHintRequest（watchdog 设计 6.5）。
3. 有主流程与异常流程：6.7 正常流程与 6.8 异常恢复与兜底策略。
4. 有错误语义清单：7 个 watchdog 私有错误码（watchdog 设计 6.6）。
5. 有落盘建议与测试出口：8.1、9.1、9.2、11.1。
6. 仍有阻塞：platform 时钟/线程抽象、事件总线接口、runtime 恢复建议输入结构、profile 键命名（watchdog 设计 11.1）。

### 4.2 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| IWatchdogService | watchdog 设计 6.6/6.7 | L3 | 方法清单、前后置条件、流程 | init(config) 配置对象类型细节 | 直接拆接口冻结任务 |
| IHeartbeatSource | watchdog 设计 6.6 | L3 | 方法名与语义明确 | describe() 返回对象别名未冻结 | 直接拆接口冻结任务 |
| ITimeoutPolicy | watchdog 设计 6.6 | L3 | evaluate 输入输出语义明确 | history 窗口结构未成文 | 直接拆接口冻结任务 |
| WatchedEntityDescriptor | watchdog 设计 6.5 | L3 | 字段、枚举约束、唯一性规则 | criticality 扩展策略细节 | 直接拆数据结构任务 |
| HeartbeatSample | watchdog 设计 6.5/6.3 | L3 | 字段、乱序丢弃语义 | seq_no 回绕策略未成文 | 直接拆数据结构任务 |
| TimeoutDecision | watchdog 设计 6.5/6.8 | L3 | 字段、等级、reason_code 约束 | reason_code 映射矩阵未成文 | 直接拆数据结构任务 |
| WatchdogSnapshot | watchdog 设计 6.5/6.7 | L3 | 字段、版本和快照语义 | 快照持久化策略未冻结 | 直接拆数据结构任务 |
| RecoveryHintRequest | watchdog 设计 6.5/6.8；ADR-007 | L2 | 建议语义边界明确 | 已解阻（2026-04-02） | 已通过 WP04-T009/T010 与 WatchdogRecoveryHintRequestBoundaryContractTest 冻结最小边界模板 |
| HeartbeatRegistry | watchdog 设计 6.2/6.3 | L3 | 注册/注销/唯一性语义明确 | 并发访问策略细节 | 直接拆实现骨架任务 |
| HeartbeatIngestor | watchdog 设计 6.2/6.3/6.8 | L3 | 输入输出、乱序处理、过旧样本策略 | 时间源抽象依赖未冻结 | 先拆实现骨架，时间源以接口占位 |
| DeadlineWheel | watchdog 设计 6.2/6.3/6.7 | L2 | 扫描职责和到期候选输出明确 | monotonic clock/scheduler 抽象未冻结 | 先 Blocked，再进实现 |
| TimeoutPolicyEngine | watchdog 设计 6.2/6.8/6.9 | L3 | 判级规则、grace、连续失败阈值 | hysteresis 参数细节未成文 | 直接拆策略任务 |
| TimeoutEventPublisher | watchdog 设计 6.2/6.8/6.10 | L2 | 发布职责与失败语义明确 | event bus 最小接口未冻结 | 先 Blocked，再进实现 |
| WatchdogAuditBridge | watchdog 设计 6.2/6.8/6.10 | L3 | critical/fatal 必审计，失败兜底明确 | 审计 sink 多路策略细节 | 直接拆实现骨架任务 |
| WatchdogMetricsBridge | watchdog 设计 6.2/6.10 | L3 | 指标清单和标签方向明确 | 指标标签白名单细则 | 直接拆指标桥接任务 |
| RecoveryRequestEmitter | watchdog 设计 6.2/6.8；ADR-007 | L2 | 建议-执行分离语义明确 | runtime 恢复建议对象冻结不足 | 先 Blocked，再进实现 |
| tests/integration/failure/profile | watchdog 设计 9.1/9.2；代码现状 | L0 | 用例与 gate 已给出 | tests 顶层 integration 已接线，缺口转为 watchdog 具体用例尚未落盘 | 直接拆集成与故障用例任务 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| IWatchdogService 接口冻结 | 6.6/6.7 | 接口定义 | WDG-TODO-001 | 先冻结生命周期与对外入口，阻断散装实现 |
| IHeartbeatSource 接口冻结 | 6.6 | 接口定义 | WDG-TODO-002 | 统一心跳上报入口，避免直接写入内部状态 |
| ITimeoutPolicy 接口冻结 | 6.6/6.8 | 接口定义 | WDG-TODO-003 | 把判级语义与执行链路解耦 |
| 核心对象冻结 | 6.5 | 数据结构 | WDG-TODO-004、005、006、007、008 | 先固定字段和边界，再进入实现 |
| 生命周期与初始化入口 | 6.2/6.7 | 生命周期/初始化 | WDG-TODO-009 | Facade 单独任务，避免与策略、事件混写 |
| 注册与采集链路 | 6.2/6.3/6.7 | 流程 | WDG-TODO-010、011 | Registry 与 Ingestor 拆分成单目标任务 |
| deadline 与策略判级 | 6.2/6.7/6.8/6.9 | 流程 | WDG-TODO-012、013 | 扫描与判级拆分，先解阻再实现扫描 |
| 事件、审计、指标桥接 | 6.2/6.8/6.10 | 适配器/桥接 | WDG-TODO-014、015、016 | 三条观测链路独立交付、独立验收 |
| 恢复建议边界守卫 | 6.8；ADR-007 | 异常与边界处理 | WDG-TODO-017 | 明确仅建议不执行，并在 contract 守卫 |
| 配置与 profile 裁剪 | 6.9/11.1 | 配置 | WDG-TODO-018 | 配置对象和覆盖规则先冻结 |
| 私有错误码与映射 | 6.6/9.1 | 错误处理 | WDG-TODO-019 | 错误可观测与映射稳定门禁 |
| CMake 接线 | 7/8.1；现状 | 注册点 | WDG-TODO-020 | watchdog 代码先进入构建图 |
| unit/contract 测试注册 | 9.1/9.2 | 测试与门禁 | WDG-TODO-021 | 先建可发现性与边界守卫 |
| integration/failure/profile 测试注册 | 9.1/9.2；现状 | 测试与门禁 | WDG-TODO-022 | 顶层 integration 已解阻，待 watchdog 具体 integration/failure/profile 用例落盘 |
| 交付证据回写 | 9.2/11.1 | 文档/证据 | WDG-TODO-023 | 把 gate、阻塞、回退证据闭环记录 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | WDG-TODO-001~003 |
| 数据结构定义类任务 | 是 | WDG-TODO-004~008 |
| 生命周期与初始化类任务 | 是 | WDG-TODO-009 |
| 适配器/桥接类任务 | 是 | WDG-TODO-014~016 |
| 异常与错误处理类任务 | 是 | WDG-TODO-017、019 |
| 配置与 Profile 裁剪类任务 | 是 | WDG-TODO-018 |
| 测试与门禁类任务 | 是 | WDG-TODO-021~022 |
| 文档/交付证据回写类任务 | 是 | WDG-TODO-023 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| WDG-TODO-001 | Done | 定义 IWatchdogService 接口头文件 | watchdog 设计 6.6/6.7；编码规范 3.7 | 6.6 IWatchdogService | L3 | infra/include/watchdog/IWatchdogService.h | init/start/stop/register_entity/unregister_entity/heartbeat/snapshot | unit：接口可编译；contract：返回 ResultCode/ErrorInfo 可映射 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | 无 | 无 | infra/include/watchdog/IWatchdogService.h；tests/unit/infra/watchdog/WatchdogServiceInterfaceTest.cpp；cmake --build build-ci --target dasall_infra 与 ctest --test-dir build-ci -R WatchdogServiceInterfaceTest --output-on-failure 通过（2026-04-02） | 仅当方法集合与 6.6 一致且无跨模块实现依赖时完成 |
| WDG-TODO-002 | Done | 定义 IHeartbeatSource 接口头文件 | watchdog 设计 6.6 | 6.6 IHeartbeatSource | L3 | infra/include/watchdog/IHeartbeatSource.h | emit_heartbeat/describe | unit：接口编译测试；contract：实体描述不越权扩写 contracts | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | 无 | 无 | infra/include/watchdog/IHeartbeatSource.h；tests/unit/infra/watchdog/HeartbeatSourceInterfaceTest.cpp；cmake --build build-ci --target dasall_infra 与 ctest --test-dir build-ci -R HeartbeatSourceInterfaceTest --output-on-failure 通过（2026-04-02） | 仅当心跳源最小接口冻结并可被 Facade 引用时完成 |
| WDG-TODO-003 | Done | 定义 ITimeoutPolicy 接口头文件 | watchdog 设计 6.6/6.8 | 6.6 ITimeoutPolicy | L3 | infra/include/watchdog/ITimeoutPolicy.h | evaluate(candidate,history) | unit：接口编译测试；unit：输入输出对象可绑定 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | 无 | 无 | infra/include/watchdog/ITimeoutPolicy.h；tests/unit/infra/watchdog/TimeoutPolicyInterfaceTest.cpp；cmake --build build-ci --target dasall_infra 与 ctest --test-dir build-ci -R TimeoutPolicyInterfaceTest --output-on-failure 通过（2026-04-02） | 仅当判级接口独立于执行链路并可编译时完成 |
| WDG-TODO-004 | Done | 定义 WatchedEntityDescriptor 数据结构 | watchdog 设计 6.5 | 6.5 WatchedEntityDescriptor | L3 | infra/include/watchdog/WatchedEntityDescriptor.h | entity_id/entity_type/owner_module/criticality/timeout_ms/grace_ms | unit：字段完整性与唯一性前置校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit | 无 | 无 | 无 | infra/include/watchdog/WatchedEntityDescriptor.h；tests/unit/infra/watchdog/WatchedEntityDescriptorTest.cpp；cmake --build build-ci 链接 91/91 目标，ctest --test-dir build-ci --output-on-failure -L unit 92/92 通过（2026-04-02） | 仅当字段与设计表一致且唯一性规则可测时完成 |
| WDG-TODO-005 | Done | 定义 HeartbeatSample 数据结构 | watchdog 设计 6.5/6.3 | 6.5 HeartbeatSample | L3 | infra/include/watchdog/HeartbeatSample.h | entity_id/heartbeat_ts/deadline_ts/seq_no | unit：乱序样本识别与旧样本判定辅助校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit | WDG-TODO-004 | 无 | 无 | infra/include/watchdog/HeartbeatSample.h；tests/unit/infra/watchdog/HeartbeatSampleTest.cpp；cmake --build build-ci 增量构建通过，ctest --test-dir build-ci --output-on-failure -L unit 93/93 通过（2026-04-02） | 仅当字段完整且可支撑乱序与过旧语义校验时完成 |
| WDG-TODO-006 | Done | 定义 TimeoutDecision 数据结构 | watchdog 设计 6.5/6.8 | 6.5 TimeoutDecision | L3 | infra/include/watchdog/TimeoutDecision.h | entity_id/timeout_level/consecutive_miss/reason_code/evidence_ref | unit：等级枚举与连续失败字段可校验；contract：reason_code 映射边界 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L "unit|contract" | WDG-TODO-005 | 无 | 无 | infra/include/watchdog/TimeoutDecision.h；tests/unit/infra/watchdog/TimeoutDecisionTest.cpp；tests/contract/smoke/WatchdogTimeoutDecisionBoundaryContractTest.cpp；ctest --test-dir build-ci --output-on-failure -L "unit|contract" 220/220 通过，ctest --test-dir build-ci -R WatchdogTimeoutDecisionBoundaryContractTest --output-on-failure 1/1 通过（2026-04-02） | 仅当等级与原因码语义可二值验证时完成 |
| WDG-TODO-007 | Done | 定义 WatchdogSnapshot 数据结构 | watchdog 设计 6.5/6.7 | 6.5 WatchdogSnapshot | L3 | infra/include/watchdog/WatchdogSnapshot.h | total_entities/timed_out_entities/degraded_entities/scan_lag_ms/ts | unit：快照字段一致性与版本单调语义校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit | WDG-TODO-004 | 无 | 无 | infra/include/watchdog/WatchdogSnapshot.h；tests/unit/infra/watchdog/WatchdogSnapshotTest.cpp；cmake --build build-ci 增量构建通过，ctest --test-dir build-ci --output-on-failure -L unit 95/95 通过（2026-04-02） | 仅当快照字段完整且可被 snapshot() 接口消费时完成 |
| WDG-TODO-008 | Done | 定义 RecoveryHintRequest 边界对象 | watchdog 设计 6.5/6.8；ADR-007 | 6.5 RecoveryHintRequest | L2 | infra/include/watchdog/RecoveryHintRequest.h | reason_code/target_ref/suggested_action/evidence_ref | contract：不含执行句柄字段；unit：字段完整性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | WDG-TODO-006 | 已解阻（2026-04-02） | 已通过 WP04-T009/T010 + WatchdogRecoveryHintRequestBoundaryContractTest 冻结最小边界模板 | infra/include/watchdog/RecoveryHintRequest.h；tests/unit/infra/watchdog/RecoveryHintRequestTest.cpp；tests/contract/smoke/WatchdogRecoveryHintRequestBoundaryContractTest.cpp；2026-04-02 通过 cmake --build build-ci --target dasall_infra dasall_watchdog_recovery_hint_request_unit_test dasall_contract_watchdog_recovery_hint_request_boundary_test、ctest --test-dir build-ci --output-on-failure -R "RecoveryHintRequestTest|WatchdogRecoveryHintRequestBoundaryContractTest" 2/2 通过，且 ctest --test-dir build-ci --output-on-failure -L contract 全量通过 | 仅当 contract 能阻断执行字段进入对象时完成 |
| WDG-TODO-009 | Done | 实现 WatchdogServiceFacade 生命周期骨架 | watchdog 设计 6.2/6.7 | 6.2 WatchdogServiceFacade；6.7 主流程 | L3 | infra/src/watchdog/WatchdogServiceFacade.cpp | init/start/stop/snapshot | unit：未初始化、已启动、优雅停机路径 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_watchdog_service_facade_unit_test && ctest --test-dir build-ci --output-on-failure -R WatchdogServiceFacadeTest | WDG-TODO-001、004、007 | 无 | 无 | infra/src/watchdog/WatchdogServiceFacade.h；infra/src/watchdog/WatchdogServiceFacade.cpp；tests/unit/infra/watchdog/WatchdogServiceFacadeTest.cpp；2026-04-08 通过 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_infra dasall_watchdog_service_facade_unit_test、ctest --test-dir build-ci -N -R WatchdogServiceFacadeTest 与 ctest --test-dir build-ci --output-on-failure -R WatchdogServiceFacadeTest | 仅当生命周期路径均可判定成功/失败且不越权到恢复执行时完成 |
| WDG-TODO-010 | Done | 实现 HeartbeatRegistry 注册治理骨架 | watchdog 设计 6.2/6.3 | 6.2 HeartbeatRegistry | L3 | infra/src/watchdog/HeartbeatRegistry.cpp | register_entity/unregister_entity/query_entity | unit：重复注册拒绝、未找到注销、上限检查 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_heartbeat_registry_unit_test && cmake --build build-ci --target dasall_watchdog_service_facade_unit_test && ctest --test-dir build-ci --output-on-failure -R "HeartbeatRegistryTest|WatchdogServiceFacadeTest" | WDG-TODO-004、009 | 无 | 无 | infra/src/watchdog/HeartbeatRegistry.h；infra/src/watchdog/HeartbeatRegistry.cpp；tests/unit/infra/watchdog/HeartbeatRegistryTest.cpp；tests/unit/infra/watchdog/WatchdogServiceFacadeTest.cpp；2026-04-08 通过 cmake --build build-ci --target dasall_infra、cmake --build build-ci --target dasall_heartbeat_registry_unit_test、cmake --build build-ci --target dasall_watchdog_service_facade_unit_test、ctest --test-dir build-ci -N -R "HeartbeatRegistryTest|WatchdogServiceFacadeTest" 与 ctest --test-dir build-ci --output-on-failure -R "HeartbeatRegistryTest|WatchdogServiceFacadeTest" | 仅当重复注册返回 INF_E_WATCHDOG_ENTITY_DUPLICATE 且测试通过时完成 |
| WDG-TODO-011 | Done | 实现 HeartbeatIngestor 采集骨架 | watchdog 设计 6.2/6.3/6.8 | 6.2 HeartbeatIngestor；6.3 输入输出 | L3 | infra/src/watchdog/HeartbeatIngestor.cpp | ingest(sample) | unit：乱序与过旧样本处理；failure：心跳风暴下丢弃策略可观测 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_heartbeat_ingestor_unit_test && cmake --build build-ci --target dasall_watchdog_service_facade_unit_test && ctest --test-dir build-ci --output-on-failure -R "HeartbeatIngestorTest|WatchdogServiceFacadeTest" | WDG-TODO-005、010 | 无 | 无 | infra/src/watchdog/HeartbeatIngestor.h；infra/src/watchdog/HeartbeatIngestor.cpp；tests/unit/infra/watchdog/HeartbeatIngestorTest.cpp；tests/unit/infra/watchdog/WatchdogServiceFacadeTest.cpp；2026-04-08 通过 cmake --build build-ci --target dasall_infra、cmake --build build-ci --target dasall_heartbeat_ingestor_unit_test、cmake --build build-ci --target dasall_watchdog_service_facade_unit_test、ctest --test-dir build-ci -N -R "HeartbeatIngestorTest|WatchdogServiceFacadeTest" 与 ctest --test-dir build-ci --output-on-failure -R "HeartbeatIngestorTest|WatchdogServiceFacadeTest" | 仅当 stale 样本可判定并映射 INF_E_WATCHDOG_HEARTBEAT_STALE 时完成 |
| WDG-TODO-012 | Done | 实现 DeadlineWheel 扫描骨架 | watchdog 设计 6.2/6.7；11.1 | 6.2 DeadlineWheel；6.7 步骤 4 | L2 | infra/src/watchdog/DeadlineWheel.cpp | tick_collect_due(now_ts)/scan_once() | unit：到期候选筛选；failure：扫描超期识别 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_deadline_wheel_unit_test && ctest --test-dir build-ci --output-on-failure -R DeadlineWheelTest | WDG-TODO-005、010、011 | 无（2026-04-08 已由 WDG-BLK-01 解阻） | 无 | infra/src/watchdog/DeadlineWheel.h；infra/src/watchdog/DeadlineWheel.cpp；tests/unit/infra/watchdog/DeadlineWheelTest.cpp；2026-04-08 通过 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_deadline_wheel_unit_test、ctest --test-dir build-ci -N -R DeadlineWheelTest 与 ctest --test-dir build-ci --output-on-failure -R DeadlineWheelTest | 仅当 due candidate 筛选、monotonic periodic 调度绑定与扫描超期 safe_observe_mode 均可重复验证时完成 |
| WDG-TODO-013 | Done | 实现 TimeoutPolicyEngine 判级骨架 | watchdog 设计 6.2/6.8/6.9 | 6.2 TimeoutPolicyEngine | L3 | infra/src/watchdog/TimeoutPolicyEngine.cpp | evaluate(candidate,history) | unit：grace 窗口与连续失败阈值；failure：warning->critical 升级 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_timeout_policy_unit_test && ctest --test-dir build-ci --output-on-failure -R TimeoutPolicyTest | WDG-TODO-003、006、011 | 无 | 无 | infra/src/watchdog/TimeoutPolicyEngine.h；infra/src/watchdog/TimeoutPolicyEngine.cpp；tests/unit/infra/watchdog/TimeoutPolicyEngineTest.cpp；2026-04-08 通过 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_timeout_policy_unit_test、ctest --test-dir build-ci -N -R TimeoutPolicyTest 与 ctest --test-dir build-ci --output-on-failure -R TimeoutPolicyTest | 仅当判级结果可重复验证且升级路径符合策略表时完成 |
| WDG-TODO-014 | Blocked | 实现 TimeoutEventPublisher 发布骨架 | watchdog 设计 6.2/6.8/6.10；11.1 | 6.2 TimeoutEventPublisher | L2 | infra/src/watchdog/TimeoutEventPublisher.cpp | publish_timeout(decision) | unit：仅超时触发发布；failure：发布失败计数 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit | WDG-TODO-006、013 | WDG-BLK-02 | event bus 最小发布接口冻结 | 发布骨架或阻塞记录 | 仅当接口冻结并可验证失败语义后可解阻 |
| WDG-TODO-015 | Done | 实现 WatchdogAuditBridge 审计桥接骨架 | watchdog 设计 6.2/6.8/6.10 | 6.2 WatchdogAuditBridge | L3 | infra/src/watchdog/WatchdogAuditBridge.cpp | write_timeout_audit(decision) | unit：critical/fatal 强制审计；failure：审计写失败可观测 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_watchdog_audit_bridge_unit_test && ctest --test-dir build-ci --output-on-failure -R WatchdogAuditBridgeTest | WDG-TODO-006、013 | 无 | 无 | infra/src/watchdog/WatchdogAuditBridge.h；infra/src/watchdog/WatchdogAuditBridge.cpp；tests/unit/infra/watchdog/WatchdogAuditBridgeTest.cpp；2026-04-08 通过 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_watchdog_audit_bridge_unit_test、ctest --test-dir build-ci -N -R WatchdogAuditBridgeTest 与 ctest --test-dir build-ci --output-on-failure -R WatchdogAuditBridgeTest | 仅当关键超时不会静默丢审计且失败可计数时完成 |
| WDG-TODO-016 | Done | 实现 WatchdogMetricsBridge 指标桥接骨架 | watchdog 设计 6.2/6.10 | 6.2 WatchdogMetricsBridge；6.10 指标清单 | L3 | infra/src/watchdog/WatchdogMetricsBridge.cpp | record_timeout/record_scan_lag/record_publish_fail | unit：指标名与标签集合一致性；failure：safe_mode 计数 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_watchdog_metrics_bridge_unit_test && ctest --test-dir build-ci --output-on-failure -R WatchdogMetricsBridgeTest | WDG-TODO-007、013、015 | 无 | 无 | infra/src/watchdog/WatchdogMetricsBridge.h；infra/src/watchdog/WatchdogMetricsBridge.cpp；tests/unit/infra/watchdog/WatchdogMetricsBridgeTest.cpp；2026-04-08 通过 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_watchdog_metrics_bridge_unit_test、ctest --test-dir build-ci -N -R WatchdogMetricsBridgeTest 与 ctest --test-dir build-ci --output-on-failure -R WatchdogMetricsBridgeTest | 仅当 7 项核心指标均有对应采样入口且测试通过时完成 |
| WDG-TODO-017 | Not Started | 实现 RecoveryRequestEmitter 边界守卫骨架 | watchdog 设计 6.2/6.8；ADR-007；11.1 | 6.2 RecoveryRequestEmitter | L2 | infra/src/watchdog/RecoveryRequestEmitter.cpp | emit_recovery_hint(decision)/sanitize_payload() | contract：建议与执行分离；unit：evidence_ref 完整性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | WDG-TODO-008、013 | 无（2026-04-02 已由 WDG-TODO-008 解阻；待 WDG-TODO-013 完成后进入实现） | RecoveryHintRequest 边界 contract 模板已冻结，可在判级器完成后直接复用 | 发射器骨架或阻塞记录 | 仅当 contract 模板冻结后方可推进 |
| WDG-TODO-018 | Blocked | 定义 WatchdogConfigPolicy 配置模型与覆盖规则 | watchdog 设计 6.9/11.1；蓝图 5.1 | 6.9 配置项表 | L2 | infra/src/watchdog/WatchdogConfigPolicy.cpp | load_defaults()/merge_layers()/validate_limits() | unit：默认值、覆盖优先级、阈值合法性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L unit | WDG-TODO-001、013 | WDG-BLK-04 | profiles 下 watchdog 键命名冻结 | 配置策略代码或阻塞记录 | 仅当键名与覆盖规则冻结后可解阻 |
| WDG-TODO-019 | Done | 定义 WatchdogErrors 私有错误码域与映射 | watchdog 设计 6.6/9.1；编码规范 3.6 | 6.6 错误语义 | L3 | infra/include/watchdog/WatchdogErrors.h | 7 个 INF_E_WATCHDOG_* 错误码与映射入口 | contract：映射 contracts::ResultCode 稳定；unit：枚举稳定 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_watchdog_errors_unit_test && cmake --build build-ci --target dasall_contract_watchdog_error_mapping_test && ctest --test-dir build-ci --output-on-failure -R "WatchdogErrorsTest|WatchdogErrorMappingContractTest" | WDG-TODO-001、006 | 无 | 无 | infra/include/watchdog/WatchdogErrors.h；tests/unit/infra/watchdog/WatchdogErrorsTest.cpp；tests/contract/smoke/WatchdogErrorMappingContractTest.cpp；2026-04-08 通过 cmake --build build-ci --target dasall_watchdog_errors_unit_test、cmake --build build-ci --target dasall_contract_watchdog_error_mapping_test、ctest --test-dir build-ci -N -R "WatchdogErrorsTest|WatchdogErrorMappingContractTest" 与 ctest --test-dir build-ci --output-on-failure -R "WatchdogErrorsTest|WatchdogErrorMappingContractTest" | 仅当 7 个错误码均可追溯到设计条目且 contract 测试通过时完成 |
| WDG-TODO-020 | Not Started | 接线 watchdog 到 infra CMake 构建入口 | watchdog 设计 7/8.1；代码现状 | 8.1 文件落盘建议 | L2 | infra/CMakeLists.txt、infra/src/watchdog/ | watchdog 源文件纳入 dasall_infra | build：dasall_infra 可编译；test：新增单测目标可链接 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | WDG-TODO-001~019 | 无 | 无 | CMake 改动、构建记录 | 仅当 placeholder 不再是唯一源码入口且 watchdog 文件入图时完成 |
| WDG-TODO-021 | Not Started | 注册 watchdog 的 unit 与 contract 测试入口 | watchdog 设计 9.1/9.2；工程规范 3.7 | 9.1 测试矩阵；9.2 gate | L2 | tests/unit/CMakeLists.txt、tests/unit/infra/watchdog/、tests/contract/CMakeLists.txt | unit：Registry/Ingestor/Policy；contract：边界与错误映射 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | WDG-TODO-019、020 | 无 | 无 | 测试注册改动、执行记录 | 仅当新增测试可被 ctest -N 发现并在 unit/contract 标签下执行时完成 |
| WDG-TODO-022 | Not Started | 注册 watchdog integration/failure/profile 测试入口 | watchdog 设计 9.1/9.2；代码现状 | 9.1 Integration/Failure/Compatibility | L0 | tests/CMakeLists.txt、tests/integration/infra/watchdog/、tests/stress/ | integration：超时闭环；failure：发布失败/扫描滞后/心跳风暴；profile：差异一致性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N | WDG-TODO-020、021 | 无（2026-03-30 已由 WDG-BLK-05 integration 顶层拓扑校准解阻） | 无；待 WDG-TODO-020、021 完成后落盘具体 watchdog integration/failure/profile 用例 | 测试注册改动或阻塞记录 | 仅当 watchdog integration/failure/profile 用例可被 ctest 发现并执行时完成 |
| WDG-TODO-023 | Not Started | 回写 watchdog 门禁结果与交付证据 | watchdog 设计 9.2/11.1；规范 6.2 | 9.2 Gate；11 风险阻塞 | L2 | docs/todos/infrastructure/DASALL_infrastructure_watchdog组件专项TODO.md | process test：门禁状态、阻塞变化、回退执行证据 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | WDG-TODO-021 | 无 | 无 | 更新后的 TODO 文档证据段 | 仅当每个 Gate 有通过/失败结论及对应命令证据时完成 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 接口与对象冻结 | WDG-TODO-001~008 | 可并行（接口组与对象组并行） | 先稳定边界与字段语义 |
| B 主链骨架闭环 | WDG-TODO-009~011、013、015、016、019 | 串行优先：009->010->011->013；其余可并行 | 先有注册采集与判级，再做观测桥接 |
| C 受阻项解锁推进 | WDG-TODO-012、014、017、018 | 串行按阻塞项解锁 | 时钟/总线/建议结构/profile 键名依赖明确 |
| D 构建与测试接线 | WDG-TODO-020、021 | 可并行但建议先 020 后 021 | 先保证源码入图，再保证测试发现性 |
| E 集成与兼容门禁 | WDG-TODO-022 | 串行 | 待 watchdog 组件具体 integration/failure/profile 用例落盘后推进 |
| F 证据收口 | WDG-TODO-023 | 串行 | 收敛 Gate 与阻塞变化记录 |

### 7.2 必过门禁表

| Gate ID | 门禁项 | 触发时机 | 通过标准 | 不通过后动作 |
|---|---|---|---|---|
| WDG-GATE-01 | 接口冻结门 | 进入实现任务前 | IWatchdogService/IHeartbeatSource/ITimeoutPolicy 与对象头文件落盘且可编译 | 退回接口对象任务 |
| WDG-GATE-02 | 边界门 | WDG-TODO-017 前 | RecoveryHintRequest 仅建议语义，contract 测试通过 | 退回对象定义和边界断言 |
| WDG-GATE-03 | 错误语义门 | WDG-TODO-019 后 | 7 个私有错误码映射稳定，contract 测试通过 | 修复映射矩阵再测 |
| WDG-GATE-04 | 构建门 | WDG-TODO-020 后 | cmake --build build-ci --target dasall_infra 通过 | 修复 CMake 接线 |
| WDG-GATE-05 | 单测发现性门 | WDG-TODO-021 后 | ctest -N 可发现 watchdog unit/contract 测试 | 修复 tests 注册 |
| WDG-GATE-06 | 失败注入门 | WDG-TODO-022 执行期 | 事件发布失败、审计失败、扫描滞后、心跳风暴均有兜底证据 | 回退到 safe_observe_mode 与最小观测链 |
| WDG-GATE-07 | profile 兼容门 | WDG-TODO-022 执行期 | desktop_full/edge_balanced/edge_minimal 行为无 breaking 漂移 | 回退到默认策略并禁用运行时覆盖 |
| WDG-GATE-08 | breaking 评审门 | 任意接口签名或映射变更前 | 明确风险、迁移窗口、回退方案后再改动 | 未评审不得推进 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 |
|---|---|---|---|---|
| WDG-BLK-01 | 已解阻（2026-04-08）：platform/include/ITimer.h 已冻结 monotonic periodic timer 抽象，DeadlineWheel 通过 ITimer::start_periodic 绑定扫描调度 | WDG-TODO-012 | 无；后续可直接复用 platform/include/ITimer.h 与 tests/unit/platform/linux/PosixTimerProviderTest.cpp | 证据回链到 platform/include/ITimer.h、infra/src/watchdog/DeadlineWheel.h、infra/src/watchdog/DeadlineWheel.cpp、tests/unit/infra/watchdog/DeadlineWheelTest.cpp |
| WDG-BLK-02 | 事件总线最小发布接口未统一 | WDG-TODO-014 | publish_timeout 的输入输出与失败语义冻结 | 先定义最小 publish 接口与失败返回码 |
| WDG-BLK-03 | 已解阻（2026-04-02）：RecoveryHintRequest 边界 contract 模板已冻结到 tests/contract/smoke/WatchdogRecoveryHintRequestBoundaryContractTest.cpp | WDG-TODO-017 | 无；后续可直接复用 WatchdogRecoveryHintRequestBoundaryContractTest | 证据回链到 infra/include/watchdog/RecoveryHintRequest.h、tests/unit/infra/watchdog/RecoveryHintRequestTest.cpp、tests/contract/CMakeLists.txt | 若建议/执行分离模板被回退，则重新转为 Blocked |
| WDG-BLK-04 | profile 中 watchdog 键名与覆盖优先级未冻结 | WDG-TODO-018 | infra.watchdog.* 键命名冻结并评审通过 | 先补 profile 键名规范，再落配置合并逻辑 |
| WDG-BLK-05 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；watchdog integration/failure/profile 是否可执行改由组件自身落盘负责 | WDG-TODO-022 | 无；后续按组件落盘 watchdog integration/failure/profile 用例 | 证据回链到 tests/CMakeLists.txt、tests/integration/CMakeLists.txt 与 infra 专项 TODO INF-BLK-06 |

## 9. 验收与质量门

### 9.1 验收命令基线

| 用途 | 命令 |
|---|---|
| 配置构建目录 | cmake -S . -B build-ci -G Ninja |
| 构建 watchdog 所在库 | cmake --build build-ci --target dasall_infra |
| 执行 unit 标签 | ctest --test-dir build-ci --output-on-failure -L unit |
| 执行 contract 标签 | ctest --test-dir build-ci --output-on-failure -L contract |
| 检查测试发现性 | ctest --test-dir build-ci -N |
| 执行 tests 自定义聚合目标 | cmake --build build-ci --target dasall_unit_tests dasall_contract_tests |

说明：

1. integration/failure/profile 门禁当前不纳入必过基线，原因是 WDG-TODO-022 尚未落盘具体 watchdog 用例；顶层 integration 拓扑已于 2026-03-30 解阻。
2. 每项任务均有代码目标、测试目标、验收命令三件套。

### 9.2 质量门逐项回答

1. 是否给出 Design -> TODO 映射，而非仅任务标题：是。  
2. 是否明确当前最细可达到粒度：是（L3/L2，阻塞项 L0）。  
3. 是否所有任务都具备代码目标 + 测试目标 + 验收命令：是。  
4. 是否所有 Blocked 项都带证据与解阻条件：是。  
5. 是否所有任务都具备可二值判定完成标准：是。  
6. 是否避免跨子系统范围扩张：是。  
7. 若要求函数/数据结构级，是否真正落到对象：是。

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 回退策略 |
|---|---|---|---|---|
| 越权执行恢复 | High | RecoveryRequestEmitter 直接调用恢复执行 | 发现执行句柄或执行调用路径 | 立即回退为仅输出 RecoveryHintRequest，并加 contract 阻断 |
| 误报风暴 | Medium | 抖动阶段频繁触发 critical/fatal | timeout_total 激增且连续失败窗口不足 | 回退到 grace + consecutive_miss 阈值保守策略 |
| 观测丢失 | High | 事件发布失败且无 fallback | publish_fail_total 异常与审计缺失并存 | 启用 ring-buffer fallback + 强制审计/日志兜底 |
| 扫描滞后扩大 | Medium | 扫描线程持续超期 | scan_lag_ms 持续越阈 | 进入 safe_observe_mode，降低扫描频率并保留快照查询 |
| profile 行为漂移 | Medium | 不同 profile 键名/阈值不一致 | 兼容测试出现跨 profile 差异回归 | 回退到默认策略，禁用运行时覆盖 |
| 接口签名 breaking 漂移 | High | 直接改 public 接口或错误映射 | contract 测试失败或评审缺失 | 触发 WDG-GATE-08，停止推进并执行迁移评审 |

## 11. 可行性结论

1. 结论：可直接生成并执行函数/数据结构级专项 TODO（L3/L2 混合），但包含 5 个明确 Blocked 项，需先解阻后推进对应任务。
2. 原因：
   - 已具备明确接口清单、对象字段、主异常流程、错误语义、测试出口与落盘路径。
   - 设计中已给出 Design -> Build 映射、测试矩阵与质量 Gate。
   - 现有代码虽为空实现，但 CMake 与 tests 骨架已可承载增量接线。
   - 阻塞项具备明确证据、影响范围和最小解阻动作。
   - ADR-006/007/008 对职责边界裁定足以形成可执行门禁断言。
3. 当前最小可执行粒度：函数 / 接口 / 数据结构（L3），受阻链路为 L2/L0。
4. 若未达到全量函数级仍缺信息：
   - platform monotonic clock/scheduler 抽象接口。
   - 事件总线最小发布接口。
   - runtime RecoveryHintRequest 输入结构与边界 contract 模板。
   - profile 下 watchdog 键名与覆盖优先级冻结。
   - watchdog 具体 integration/failure/profile 用例与标签落盘。
5. 下一步建议：
   - 先执行 WDG-TODO-001~011、013、015、016、019、020、021 完成接口对象和主链骨架。
   - 并行解阻 WDG-BLK-01~04；WDG-BLK-05 已完成仓库级解阻。
   - 解阻后执行 WDG-TODO-012、014、017、018、022，最后执行 WDG-TODO-023 收口证据。
