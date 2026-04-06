# DASALL infrastructure 子系统 metrics 组件专项 TODO

最近更新时间：2026-03-25  
阶段：Detailed Design -> Special TODO  
适用范围：infra/metrics

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md
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
15. 当前代码现状：infra/CMakeLists.txt、infra/include/、infra/src/{InfraServiceFacade.cpp,InfraErrorCode.cpp,audit/,plugin/,tracing/}、infra/src/metrics/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt

生成原则：

1. 不改写已冻结 ADR-005/006/007/008 结论。
2. 不越过 infrastructure/metrics 边界扩张到无关模块。
3. 不把讨论类事项写成 Done-ready Build 任务。
4. 每项任务必须包含代码目标、测试目标、验收命令三件套。
5. 设计证据不足处仅输出 Blocked 与补设计解阻动作，不伪造实现细节。

## 2. 子系统目标与范围

### 2.1 组件目标

1. 提供统一指标采集接口（Counter/Gauge/Histogram/UpDownCounter）。
2. 提供低开销聚合、导出、降级与恢复能力。
3. 提供标签治理与高基数防护，保证可持续运行。
4. 与 logging/tracing/audit/health 协同，但不接管其职责边界。

### 2.2 范围边界

纳入范围：

1. metrics 对外接口、核心对象、错误码域、配置模型。
2. 采样 -> 聚合 -> 导出 -> 健康快照主链路与异常链路。
3. metrics 组件构建接线与 unit/contract/failure 测试门禁。
4. metrics 组件对 logging/audit/health 的 metrics 侧桥接点。

不纳入范围：

1. runtime 主控状态机、恢复裁定、调度策略实现。
2. contracts 共享语义对象扩写或重定义。
3. OTel SDK 全量绑定与远程规则下发（仅保留兼容预留，不在本轮默认推进）。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1：约束与边界抽取输出）

| ID | 来源 | 类型 | 约束内容 | 对 metrics TODO 的影响 |
|---|---|---|---|---|
| MET-TC001 | metrics 设计 2.1；架构 3.4.7/5.10 | Must | infra 必须提供 metrics 并与 logging/trace/audit 协同 | 任务必须覆盖采样、导出、桥接与可观测 |
| MET-TC002 | 架构 3.7；蓝图 4.2 | Must | infra 依赖方向单向，不反向依赖业务实现 | 代码目标仅限 infra/tests/docs/cmake 路径 |
| MET-TC003 | 蓝图 4.2/4.3 | Must-Not | 禁止 infra -> runtime/cognition/tools/memory/knowledge/services/multi_agent 实现依赖；跨模块调用走稳定接口/contracts | 任务禁止 include 上层实现类 |
| MET-TC004 | ADR-005 | Must | contracts 与关键边界冻结结论不可被模块实现反向改写 | 对 breaking 风险必须设置评审门禁 |
| MET-TC005 | ADR-006 | Must-Not | metrics 不负责语义上下文装配与 Prompt 渲染 | 仅消费观测标签，不生成语义上下文 |
| MET-TC006 | ADR-007 | Must-Not | metrics 不负责失败判定与恢复准入 | 只记录失败统计、健康状态与审计证据 |
| MET-TC007 | ADR-008 | Must | metrics 不拥有全局调度权 | 仅记录 orchestrator/coordinator/worker 观测，不推进状态机 |
| MET-TC008 | metrics 设计 2.1/2.2；contracts 冻结约束 | Must-Not | 不把聚合窗口、桶策略、导出协议细节写入 contracts | 类型与配置细节保持 infra 私有 |
| MET-TC009 | 工程规范 3.6 | Must | 错误不可吞没，失败必须可观测 | 每个错误路径任务必须绑定错误码与可观测输出 |
| MET-TC010 | 工程规范 3.7 | Should | 新增公共接口同步 unit/contract/integration 测试 | 每个接口任务必须绑定测试目标 |
| MET-TC011 | 架构 7.5.1；蓝图 5.1 | Must | Profile 仅可裁剪能力和替换实现，不可绕过 Audit 与 Runtime 主控链路 | 配置任务必须显式约束覆盖层级与回退 |
| MET-TC012 | 落地步骤指引 阶段 C | Must | infra 底座先行，且每阶段必须可测试 | 执行顺序需先接口对象，再主链路，再门禁 |
| MET-TC013 | OTel Metrics / Prometheus / SRE（metrics 设计 2.1/4.3） | Should | API/SDK 解耦、低基数标签、围绕 SLO 的桶策略 | 任务需保留可插拔导出与标签治理 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/src/metrics/ | 目录为空 | metrics 实现尚未落盘 |
| infra/include/ | 已形成“根目录共享契约 + 组件目录公共接口”布局，metrics/ 子目录已落盘接口与对象 | metrics public headers 已冻结，后续差距集中在运行时实现 |
| infra/CMakeLists.txt | 已接入 core/audit/plugin/tracing 等真实源码 | metrics 公共接口已落盘，但 metrics 服务实现尚未接入构建目标 |
| tests/CMakeLists.txt | 已接入 mocks/unit/contract/integration 并提供 dasall_integration_tests 聚合入口 | integration 拓扑已接入顶层，后续只需补 metrics 具体集成/故障用例 |
| tests/unit/CMakeLists.txt | 已接入 infra 子目录 | metrics unit 发现性已建立，后续只需补具体用例 |
| tests/contract/CMakeLists.txt | centralized registration 已存在 | 可复用承载 metrics contract 边界测试 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：本轮可生成 L3/L2 混合专项 TODO。接口、对象、错误语义、主/异常流程可细化到 L3；跨子域桥接、组件 integration/failure 用例与 OTLP 依赖属于外部约束，需降为 L2/L1 或 Blocked。

支撑证据：

1. 已有明确核心接口清单：IMetricsProvider、IMeter、IMetricExporter、IMetricConfigPolicy、IMetricsHealthProbe（metrics 设计 6.6）。
2. 已有核心对象字段：MetricIdentity、MetricSample、MetricLabels、HistogramConfig、ExportBatchReport、MetricsModuleSnapshot（metrics 设计 6.5）。
3. 已有主流程与异常流程：正常流程 7 步、异常分类 4 类与恢复动作 4 类（metrics 设计 6.7/6.8）。
4. 已有错误码域：MET_E_PROVIDER_NOT_READY、MET_E_IDENTITY_INVALID、MET_E_LABEL_CARDINALITY_EXCEEDED、MET_E_QUEUE_FULL、MET_E_EXPORT_FAILURE、MET_E_EXPORT_TIMEOUT、MET_E_CONFIG_INVALID（metrics 设计 6.6）。
5. 已有落盘建议与测试出口：infra/include/metrics、infra/src/metrics、tests/unit/infra/metrics、tests/integration/infra/metrics（metrics 设计 8.1/7）。
6. 当前 tests 顶层已接入 integration，但 metrics 的 logging/health/audit 桥接接口签名仍未统一，且组件自身 integration 用例尚未落盘，导致部分任务仍需 Blocked。

当前最小可执行粒度：函数/接口/数据结构级（L3 为主，局部 L2/L1）。

### 4.2 粒度可行性评估表（Step 2：详细设计可执行性扫描输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| IMetricsProvider | metrics 设计 6.6 | L3 | init/get_meter/force_flush/shutdown 方法语义完整 | 返回状态对象与 contracts 映射矩阵未成文 | 直接拆接口冻结 + 生命周期骨架 |
| IMeter | metrics 设计 6.6/6.7 | L3 | create_counter/create_gauge/create_histogram/record 语义完整 | InstrumentHandle 具体类型约束未成文 | 直接拆接口 + 行为测试 |
| IMetricExporter | metrics 设计 6.6/6.8 | L3 | export/flush/shutdown 语义完整 | OTLP 首版是否启用未冻结 | 先做 noop/prom_text，OTLP 标记 Blocked |
| IMetricConfigPolicy | metrics 设计 6.6/6.9 | L3 | validate/normalize/should_accept 语义完整 | 全局标签 taxonomy 评审未完成 | 先落核心白名单，扩展后置 |
| IMetricsHealthProbe | metrics 设计 6.6/6.10 | L2 | snapshot 语义、ready/degraded 输出明确 | health 统一接口签名未冻结 | 先产出 metrics 私有快照对象 |
| MetricIdentity / MetricSample / MetricLabels | metrics 设计 6.5 | L3 | 字段与语义约束完整 | labels value 类型边界未细化 | 直接拆数据结构任务 |
| HistogramConfig | metrics 设计 6.5/6.9 | L3 | bucket 单调递增与 temporality 语义明确 | 不同 profile 桶收敛策略未冻结 | 先落默认桶与校验函数 |
| ExportBatchReport / MetricsModuleSnapshot | metrics 设计 6.5/6.10 | L3 | 健康与导出指标字段完整 | 与 health/logging 桥接对象签名未统一 | 先落 metrics 私有对象与转换层 |
| MetricsFacade/Registry/AggregationEngine | metrics 设计 6.2/6.7 | L3 | 组件职责、调用顺序、输入输出完整 | 并发参数细节未冻结 | 先落单线程可测骨架，再扩展并发 |
| CardinalityGuard | metrics 设计 6.2/6.3/6.8 | L3 | 白名单 + 高基数拦截 + 失败可观测语义完整 | 领域标签 taxonomy 未最终评审 | 先实现核心白名单并计数拒绝 |
| MetricReaderScheduler + MetricsExporterAdapter | metrics 设计 6.2/6.7/6.8/6.9 | L2 | 调度与导出语义完整 | OTLP 依赖、退避参数未冻结 | 先实现 noop/prom_text 导出链路 |
| MetricsAuditBridge + MetricsLoggingBridge | metrics 设计 6.2/6.10 | L1 | 审计事件清单、日志事件语义明确 | audit/logging 写入接口签名未冻结 | 标记 Blocked，先补桥接接口设计 |
| tests/integration metrics 注册点 | metrics 设计 8.1/9.1；tests 现状 | L0 | 设计建议存在，且 tests 顶层 integration 拓扑已接入 | metrics integration 用例尚未落盘 | 直接拆 integration 注册任务 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3：Design -> TODO 映射建模输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| metrics 对外接口层 | metrics 设计 6.6 | 接口 | MET-TODO-001、MET-TODO-002、MET-TODO-003、MET-TODO-004、MET-TODO-005 | 先冻结边界，防止上层直连实现 |
| 核心对象模型 | metrics 设计 6.5 | 数据结构 | MET-TODO-006、MET-TODO-007 | 先稳定字段与语义，再推进实现 |
| 错误码域与失败语义 | metrics 设计 6.6/6.8；工程规范 3.6 | 错误处理 | MET-TODO-008、MET-TODO-015 | 错误码定义与降级恢复拆分，便于二值验收 |
| 主链路 record->aggregate->snapshot | metrics 设计 6.7 | 生命周期/流程 | MET-TODO-009、MET-TODO-010、MET-TODO-011 | 按组件单目标拆分，避免任务过载 |
| 标签治理与高基数防护 | metrics 设计 6.3/6.8/6.9 | 适配器/治理 | MET-TODO-012 | 独立治理任务，单独验证拒绝语义 |
| 导出调度与可插拔导出 | metrics 设计 6.2/6.7/6.8/6.9 | 适配器/流程 | MET-TODO-013、MET-TODO-014 | 调度与导出拆分；OTLP 延后 |
| 配置与 Profile 裁剪 | metrics 设计 6.9；架构 7.5.1 | 配置 | MET-TODO-016、MET-BLK-003 | 四层覆盖与回退策略单列任务 |
| CMake 与测试门禁 | metrics 设计 7/8/9；当前代码现状 | 测试/门禁 | MET-TODO-017、MET-TODO-018 | 构建接线、unit/contract 先行，integration 用例待组件后续落盘 |
| logging/audit/health 桥接 | metrics 设计 6.10 | 桥接 | MET-TODO-019、MET-BLK-002、MET-BLK-004 | 外部接口未冻结，必须先补设计 |
| 交付证据回写 | metrics 设计 9.2/11 | 文档/门禁 | MET-TODO-020 | 回写质量门、阻塞变化、回退执行证据 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | MET-TODO-001~005 |
| 数据结构定义类任务 | 是 | MET-TODO-006~007 |
| 生命周期与初始化类任务 | 是 | MET-TODO-009~011 |
| 适配器/桥接类任务 | 是 | MET-TODO-012~014、019 |
| 异常与错误处理类任务 | 是 | MET-TODO-008、015 |
| 配置与 Profile 裁剪类任务 | 是 | MET-TODO-016（含阻塞 MET-BLK-003） |
| 测试与门禁类任务 | 是 | MET-TODO-017~018 |
| 文档/交付证据回写类任务 | 是 | MET-TODO-020 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4：原子任务拆解输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| MET-TODO-001 | Done | 定义 IMetricsProvider 接口头文件 | metrics 设计 6.6；编码规范 3.7 | 6.6 IMetricsProvider | L3 | infra/include/metrics/IMetricsProvider.h | init(config), get_meter(scope), force_flush(timeout_ms), shutdown(timeout_ms) | unit：接口可编译；contract：错误语义入口可对接 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "MetricsProviderInterfaceTest|MetricsProviderInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "MetricsProviderInterfaceTest|MetricsProviderInterfaceBoundaryContractTest" | 无 | 无 | 无 | 接口头文件、unit/contract 测试；2026-04-01 已落盘 infra/include/metrics/IMetricsProvider.h、tests/unit/infra/MetricsProviderInterfaceTest.cpp、tests/contract/smoke/MetricsProviderInterfaceBoundaryContractTest.cpp，并完成 infra/tests CMake 注册 | 仅当方法签名与 6.6 一致、边界仅暴露 contracts 错误语义且测试通过时完成 |
| MET-TODO-002 | Done | 定义 IMeter 接口头文件 | metrics 设计 6.6/6.7 | 6.6 IMeter；6.7 主流程 | L3 | infra/include/metrics/IMeter.h | create_counter, create_gauge, create_histogram, record | unit：record 入口可编译；contract：采样对象语义不越权 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "MetricsMeterInterfaceTest|MetricsMeterInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "MetricsMeterInterfaceTest|MetricsMeterInterfaceBoundaryContractTest" | MET-TODO-001 | 无 | 无 | 接口头文件、unit/contract 测试；2026-04-01 已落盘 infra/include/metrics/IMeter.h、tests/unit/infra/MetricsMeterInterfaceTest.cpp、tests/contract/smoke/MetricsMeterInterfaceBoundaryContractTest.cpp，并完成 infra/tests CMake 注册 | 仅当接口能覆盖 6.7 正常流程输入、保持与后续 MetricTypes 前置声明兼容且测试通过时完成 |
| MET-TODO-003 | Done | 定义 IMetricExporter 接口头文件 | metrics 设计 6.6/6.8 | 6.6 IMetricExporter；6.8 导出异常 | L3 | infra/include/metrics/IMetricExporter.h | export_batch(batch), force_flush(timeout_ms), shutdown(timeout_ms) | unit：导出成功/失败调用面可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "MetricsExporterInterfaceTest|MetricsExporterInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "MetricsExporterInterfaceTest|MetricsExporterInterfaceBoundaryContractTest" | MET-TODO-001 | OTLP 首版启用未冻结 | 首版仅约束 noop/prom_text 语义 | 接口头文件、unit/contract 测试；2026-04-01 已落盘 infra/include/metrics/IMetricExporter.h、tests/unit/infra/MetricsExporterInterfaceTest.cpp、tests/contract/smoke/MetricsExporterInterfaceBoundaryContractTest.cpp，并完成 infra/tests CMake 注册 | 仅当导出接口语义覆盖成功/失败/超时三路径、保持 exporter 可插拔且测试通过时完成 |
| MET-TODO-004 | Done | 定义 IMetricConfigPolicy 接口头文件 | metrics 设计 6.6/6.9 | 6.6 IMetricConfigPolicy；6.9 配置项表 | L3 | infra/include/metrics/IMetricConfigPolicy.h | validate_identity, normalize_labels, should_accept | unit：identity 与 labels 策略入口可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "MetricsConfigPolicyInterfaceTest|MetricsConfigPolicyInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "MetricsConfigPolicyInterfaceTest|MetricsConfigPolicyInterfaceBoundaryContractTest" | MET-TODO-002、MET-TODO-006 | 标签 taxonomy 未全局评审 | 先冻结核心 allowlist 键集合 | 接口头文件、unit/contract 测试；2026-04-01 已落盘 infra/include/metrics/IMetricConfigPolicy.h、tests/unit/infra/MetricsConfigPolicyInterfaceTest.cpp、tests/contract/smoke/MetricsConfigPolicyInterfaceBoundaryContractTest.cpp，并完成 infra/tests CMake 注册 | 仅当策略接口与 6.9 配置键一致、边界不泄露动态标签实现且测试通过时完成 |
| MET-TODO-005 | Done | 定义 IMetricsHealthProbe 接口头文件 | metrics 设计 6.6/6.10 | 6.6 IMetricsHealthProbe；6.10 健康观测 | L2 | infra/include/metrics/IMetricsHealthProbe.h | snapshot() | unit：健康快照出口可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | MET-TODO-007 | 与 infra/health 统一探针签名未冻结 | 先输出 metrics 私有快照对象 | 接口头文件、编译记录；2026-04-01 已落盘 infra/include/metrics/IMetricsHealthProbe.h、tests/unit/infra/MetricsHealthProbeInterfaceTest.cpp，并完成 infra/tests CMake 注册 | 仅当 snapshot 返回对象能承载 queue/degraded/exporter_state 时完成 |
| MET-TODO-006 | Done | 定义 MetricTypes 核心对象头文件 | metrics 设计 6.5 | 6.5 MetricIdentity/MetricSample/MetricLabels/HistogramConfig | L3 | infra/include/metrics/MetricTypes.h | 上述 4 类对象字段定义 | unit：字段完整性与默认语义验证 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -N -R MetricTypesTest && ctest --test-dir build-ci --output-on-failure -R MetricTypesTest | 无 | 无 | 无 | 对象头文件、单测；2026-04-01 已落盘 infra/include/metrics/MetricTypes.h、tests/unit/infra/MetricTypesTest.cpp，并完成 infra/tests CMake 注册 | 仅当字段与 6.5 对齐、可二值判定的 guard 完整且单测通过时完成 |
| MET-TODO-007 | Done | 定义 MetricsSnapshots 对象头文件 | metrics 设计 6.5/6.10 | 6.5 ExportBatchReport/MetricsModuleSnapshot；6.10 指标清单 | L3 | infra/include/metrics/MetricsSnapshots.h | ExportBatchReport, MetricsModuleSnapshot | unit：导出与健康快照字段一致性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-006 | 无 | 无 | 对象头文件、单测；2026-04-01 已落盘 infra/include/metrics/MetricsSnapshots.h、tests/unit/infra/MetricsSnapshotsTest.cpp，并完成 infra/tests CMake 注册 | 仅当快照字段可覆盖成功/失败/队列/降级语义时完成 |
| MET-TODO-008 | Done | 定义 MetricsErrors 错误码域 | metrics 设计 6.6；工程规范 3.6 | 6.6 错误语义 | L3 | infra/include/metrics/MetricsErrors.h | MET_E_PROVIDER_NOT_READY...MET_E_CONFIG_INVALID | contract：映射 contracts::ResultCode；unit：枚举稳定性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | MET-TODO-001 | contracts 映射矩阵未成文 | 在 contract 测试固化映射矩阵 | 错误码头文件、映射测试；2026-04-01 已落盘 infra/include/metrics/MetricsErrors.h、tests/unit/infra/MetricsErrorsTest.cpp、tests/contract/smoke/MetricsErrorMappingContractTest.cpp，并完成 infra/tests CMake 注册 | 仅当 7 个错误码均有来源锚点且映射测试通过时完成 |
| MET-TODO-009 | Done | 实现 MetricsFacade 初始化与写入骨架 | metrics 设计 6.2/6.7 | 6.2 MetricsFacade；6.7 步骤 1/2 | L3 | infra/src/metrics/MetricsFacade.cpp | init/get_meter/record 入口骨架 | unit：未初始化/已初始化两路径；failure：非法 identity 路径 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-001、MET-TODO-002、MET-TODO-006、MET-TODO-008 | 无 | 无 | Facade 骨架、单测；2026-04-06 已落盘 infra/src/metrics/MetricsFacade.{h,cpp}、tests/unit/infra/metrics/MetricsFacadeTest.cpp，并在 tests/unit/infra/CMakeLists.txt 中以临时直编私有源码方式注册 MetricsFacadeTest | 仅当初始化状态机与错误码路径可二值判定时完成 |
| MET-TODO-010 | Done | 实现 InstrumentRegistry 唯一性管理骨架 | metrics 设计 6.2/6.3 | 6.2 InstrumentRegistry；6.3 同名同语义唯一 | L3 | infra/src/metrics/InstrumentRegistry.cpp | register_identity/find_identity | unit：同名冲突与重复注册路径 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-006、MET-TODO-008 | 无 | 无 | Registry 骨架、单测；2026-04-06 已落盘 infra/src/metrics/InstrumentRegistry.{h,cpp}、tests/unit/infra/metrics/InstrumentRegistryTest.cpp，并将 MetricsFacade 的 instrument 创建/record 路径切到 registry 骨架 | 仅当重复注册冲突返回可判定错误并可观测时完成 |
| MET-TODO-011 | Done | 实现 AggregationEngine 聚合骨架 | metrics 设计 6.2/6.7 | 6.2 AggregationEngine；6.7 步骤 5 | L3 | infra/src/metrics/AggregationEngine.cpp | aggregate_counter, aggregate_gauge, aggregate_histogram, snapshot | unit：Counter/Gauge/Histogram 聚合断言 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-006、MET-TODO-007、MET-TODO-010 | 并发参数未冻结 | 先落单线程可测实现 | 聚合骨架、单测；2026-04-06 已落盘 infra/src/metrics/AggregationEngine.{h,cpp}、tests/unit/infra/metrics/MetricsAggregationTest.cpp，并将 MetricsFacade 的 record 路径接到 aggregation engine | 仅当三类聚合行为均可重复验证时完成 |
| MET-TODO-012 | Done | 实现 CardinalityGuard 标签治理骨架 | metrics 设计 6.2/6.3/6.8/6.9 | 6.2 CardinalityGuard；6.8 标签异常；6.9 allowlist | L3 | infra/src/metrics/CardinalityGuard.cpp | validate_labels, reject_with_reason | unit：allowlist/超阈值拒绝；failure：reject_total 可观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-004、MET-TODO-006、MET-TODO-008 | 标签 taxonomy 全局评审未完成 | 先冻结 module/stage/profile/outcome/error_code | Guard 骨架、单测；2026-04-06 已落盘 infra/src/metrics/CardinalityGuard.{h,cpp}、tests/unit/infra/metrics/MetricsCardinalityGuardTest.cpp，并将 MetricsFacade 的 record 路径接到 CardinalityGuard，新增 module_snapshot() 暴露 guard_reject_total | 仅当未知标签与高基数路径均可二值判定时完成 |
| MET-TODO-013 | Done | 实现 MetricReaderScheduler 调度骨架 | metrics 设计 6.2/6.7/6.9 | 6.2 MetricReaderScheduler；6.7 步骤 6；6.9 interval 配置 | L2 | infra/src/metrics/MetricReaderScheduler.cpp | schedule_tick, flush_on_shutdown | unit：周期触发与 shutdown flush 行为 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-011、MET-TODO-016 | 线程模型细节未冻结 | 首版采用单工作线程策略 | 调度骨架、单测；2026-04-06 已落盘 infra/src/metrics/MetricReaderScheduler.{h,cpp}、tests/unit/infra/metrics/MetricsReaderSchedulerTest.cpp，并在 tests/unit/infra/CMakeLists.txt 中注册 MetricsReaderSchedulerTest | 仅当 tick 触发与 shutdown flush 均可稳定复现时完成 |
| MET-TODO-014 | Done | 实现 MetricsExporterAdapter 首版导出骨架 | metrics 设计 6.2/6.7/6.8/6.9 | 6.2 MetricsExporterAdapter；6.8 导出异常；6.9 exporter.type | L2 | infra/src/metrics/MetricsExporterAdapter.cpp | export_batch(noop/prom_text), fallback_to_noop | unit：导出成功/失败/超时；failure：export_failure_total 可观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-003、MET-TODO-007、MET-TODO-008、MET-TODO-013 | OTLP 依赖未冻结 | 首版仅 noop/prom_text，OTLP 后置评审 | 导出骨架、单测；2026-04-06 已落盘 infra/src/metrics/MetricsExporterAdapter.{h,cpp}、tests/unit/infra/metrics/MetricsExporterAdapterTest.cpp，并在 tests/unit/infra/CMakeLists.txt 中注册 MetricsExporterAdapterTest | 仅当成功/失败/超时三路径均有可观测结果且不阻塞主流程时完成 |
| MET-TODO-015 | Done | 实现 MetricsRecovery 降级与恢复骨架 | metrics 设计 6.8/6.10；工程规范 3.6 | 6.8 恢复动作；6.10 degraded_mode | L2 | infra/src/metrics/MetricsRecovery.cpp | enter_degraded, recover_to_healthy, emit_recovery_event | failure-injection：连续失败触发降级与恢复回清 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-008、MET-TODO-014 | health/logging 接口签名未统一 | 先输出 metrics 私有健康快照与日志钩子占位 | 恢复骨架、故障注入测试；2026-04-06 已落盘 infra/src/metrics/MetricsRecovery.{h,cpp}、tests/unit/infra/metrics/MetricsRecoveryTest.cpp，并在 tests/unit/infra/CMakeLists.txt 中注册 MetricsRecoveryTest | 仅当降级进入/退出两路径均可二值判定时完成 |
| MET-TODO-016 | Done | 定义 MetricsConfigPolicy 配置模型与默认策略 | metrics 设计 6.9；架构 7.5.1 | 6.9 配置项表 | L3 | infra/src/metrics/MetricsConfigPolicy.cpp | merge(default/profile/deploy/runtime), validate_histogram_buckets | unit：默认值、覆盖优先级、桶单调性校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-004、MET-TODO-006、MET-TODO-008 | Profile 键一致性未冻结 | 先冻结最小键集合：enabled/exporter/interval/labels | 配置策略实现、单测；2026-04-06 已落盘 infra/src/metrics/MetricsConfigPolicy.{h,cpp}、tests/unit/infra/metrics/MetricsConfigMergeTest.cpp，并在 tests/unit/infra/CMakeLists.txt 中注册 MetricsConfigMergeTest | 仅当覆盖顺序与默认值与 6.9 完全一致时完成 |
| MET-TODO-017 | Done | 注册 metrics 代码到 infra CMake | metrics 设计 8.1；代码现状 | 8.1 文件落盘建议 | L2 | infra/CMakeLists.txt, infra/include/metrics/, infra/src/metrics/ | 将 metrics 源码与头文件纳入 dasall_infra | build：dasall_infra 可编译；unit：新增目标可链接 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | MET-TODO-001~MET-TODO-016 | 初期源文件可能为空 | 保留最小 non-empty 源文件并分批接线 | CMake 改动、构建记录；2026-04-06 已更新 infra/CMakeLists.txt，将 MetricsFacade/Registry/Aggregation/Guard/Config/Scheduler/Exporter/Recovery 全量源码与私有头纳入 dasall_infra | 仅当 placeholder 不再是唯一源码且 metrics 文件入图时完成 |
| MET-TODO-018 | Done | 注册 metrics 的 unit 与 contract 测试入口 | metrics 设计 7/8/9；工程规范 3.7 | 7 映射、8.1 目录、9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt, tests/unit/infra/metrics/, tests/contract/CMakeLists.txt | 新增 metrics 相关 unit/contract/failure 测试注册 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | MET-TODO-017 | 无 | integration 相关验收按组件用例落盘推进 | 测试代码、注册入口、执行记录；2026-04-06 已更新 tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt，完成 metrics unit 聚合总表、metrics contract 标签与 failure 标签收口 | 仅当 metrics 新增测试在 ctest -N 可见并执行通过时完成 |
| MET-TODO-019 | Done | 接线 MetricsAuditBridge 与 MetricsLoggingBridge 骨架 | metrics 设计 6.2/6.10 | 6.2 Bridge 组件；6.10 审计/日志事件 | L1 | infra/src/metrics/MetricsBridgeEvent.h, infra/src/metrics/MetricsAuditBridge.cpp, infra/src/metrics/MetricsLoggingBridge.cpp | bridge_write_audit_event, bridge_write_log_event, MetricsBridgeEvent | contract：审计字段完整；unit：桥接调用可达 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | MET-TODO-007、MET-TODO-008、MET-TODO-015 | 无 | 2026-04-06 已由 infra/include/audit/IAuditLogger.h、infra/include/audit/AuditTypes.h、infra/include/logging/ILogger.h、infra/include/LogEvent.h 解阻 | 桥接代码、unit/contract 测试、CMake 注册与执行记录；2026-04-06 已落盘 infra/src/metrics/Metrics{BridgeEvent,AuditBridge,LoggingBridge}*、tests/unit/infra/metrics/Metrics{Audit,Logging}BridgeTest.cpp、tests/contract/smoke/MetricsAuditBridgeBoundaryContractTest.cpp，并完成 infra/tests CMake 注册 | 仅当 MetricsRecovery 的 logging hook 可由 MetricsLoggingBridge best-effort 承接、MetricsAuditBridge 输出完整 AuditEvent/AuditContext 且 unit/contract 门禁通过时完成 |
| MET-TODO-020 | Not Started | 回写 metrics 质量门与交付证据 | metrics 设计 9.2/11；工程规范 6.2 | 9.2 Gate 建议；11 风险与回退 | L2 | docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md | Gate 执行结果、阻塞变化、回退记录回写 | process test：门禁记录可追溯 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | MET-TODO-018 | 无 | 无 | 更新后的 TODO 证据段 | 仅当每个门禁项都有通过/失败结论与证据命令时完成 |

### 6.2 Blocked 任务索引

| 任务 ID | 对应阻塞项 |
|---|---|
| 无 | 无 |

## 7. 执行顺序建议

### 7.1 顺序与并行段（Step 5：顺序与门禁编排输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 接口与对象冻结 | MET-TODO-001~008 | 可并行（接口组/对象组/错误码组并行） | 先稳定边界，避免实现期返工 |
| B 主链路骨架 | MET-TODO-009~011 | 串行 | facade -> registry -> aggregation |
| C 治理与导出 | MET-TODO-012~014 | 串行 | guard -> scheduler -> exporter |
| D 配置与恢复 | MET-TODO-015~016 | 可并行 | 恢复策略与配置模型并行推进 |
| E 构建与测试接线 | MET-TODO-017~018 | 可并行 | CMake 接线与测试注册同步收口 |
| F 解阻后桥接 | MET-TODO-019 | 串行 | 依赖 logging/audit 接口冻结 |
| G 证据收口 | MET-TODO-020 | 串行 | 统一回写质量门和阻塞变化 |

### 7.2 必过门禁

| Gate ID | 门禁项 | 通过标准 | 失败处置 |000002.14
|---|---|---|---|
| MET-GATE-01 | 接口冻结门 | IMetricsProvider/IMeter/IMetricExporter/MetricTypes/MetricsErrors 全部落盘并编译通过 | 回退到接口定义阶段，不推进实现 |
| MET-GATE-02 | 主链路闭环门 | record -> aggregate -> snapshot 单测通过 | 回退 MET-TODO-009~011 |
| MET-GATE-03 | 异常可观测门 | label reject/queue full/export failure/config invalid 均有错误码与观测输出 | 补齐失败路径后再提测 |
| MET-GATE-04 | 构建接线门 | dasall_infra 构建通过且 metrics 源码入图 | 修复 CMake 接线 |
| MET-GATE-05 | 测试发现性门 | ctest -N 可见 metrics 新增 unit/contract 用例 | 修复 tests 注册 |
| MET-GATE-06 | breaking 评审门 | 任何接口签名或错误码映射变化都有评审结论 | 未评审不得合入 |
| MET-GATE-07 | integration 准入门 | tests 顶层已接入 integration 子目录并定义标签规范，且 metrics 组件用例已落盘 | 未通过前补齐 metrics integration/failure 用例与注册 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| MET-BLK-001 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；metrics integration 是否可执行改由组件自身落盘负责 | 后续 metrics integration 任务 | 无；后续仅需按组件落盘 integration/failure 用例 | 证据回链到 infra 专项 TODO 的 INF-BLK-06 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt | 若 tests 顶层 integration 接线或聚合依赖回退，则重新转为 Blocked |
| MET-BLK-002 | 已解阻（2026-04-06）：audit 子域已冻结 `IAuditLogger::write_audit(const AuditEvent&, const AuditContext&)` 与 `AuditEvent/AuditContext` 最小写入边界，MetricsAuditBridge 可稳定落盘 | MET-TODO-019 | 无 | 证据回链到 infra/include/audit/IAuditLogger.h、infra/include/audit/AuditTypes.h 与本 TODO 第 31 节执行记录 | 若 audit 写入接口出现未评审 breaking 变更，则重新转为 Blocked |
| MET-BLK-003 | Profile 中 metrics 配置键尚未统一，跨档位覆盖规则不稳定 | MET-TODO-016 | 冻结 enabled/exporter/interval/labels/queue/buckets 键集合 | 先冻结最小键集合并在 profile 文档回写 | 暂时禁用运行时动态覆盖 |
| MET-BLK-004 | 已解阻（2026-04-06）：logging 子域已冻结 `ILogger::log(const LogEvent&)` 与 `LogEvent` 结构化写入边界，MetricsLoggingBridge 可稳定接线 | MET-TODO-019 | 无 | 证据回链到 infra/include/logging/ILogger.h、infra/include/LogEvent.h 与本 TODO 第 31 节执行记录 | 若 logging 写入接口出现未评审 breaking 变更，则重新转为 Blocked |
| MET-BLK-005 | OTLP exporter 依赖与构建方式未冻结 | OTLP 相关扩展任务 | 明确 third_party 接入策略与版本 | 首版只实现 noop/prom_text，OTLP 延后 | 默认 exporter.type 维持 noop |

## 9. 验收与质量门

### 9.1 验收命令基线

| 用途 | 命令 |
|---|---|
| 配置构建目录 | cmake -S . -B build-ci -G Ninja |
| 构建 infra | cmake --build build-ci --target dasall_infra |
| 执行 unit 套件 | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit |
| 执行 contract 套件 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract |
| 检查测试发现性 | ctest --test-dir build-ci -N |

说明：

1. integration 命令暂不纳入首轮验收基线，原因是 metrics integration 用例尚未落盘；顶层 integration 拓扑已于 2026-03-30 解阻。
2. 每项任务至少包含 1 条构建命令和 1 条测试命令。

### 9.2 质量门逐项回答

1. 是否给出 Design -> TODO 映射：是。
2. 是否明确当前最细可达到粒度：是，L3/L2 混合，最小可达函数/数据结构级。
3. 是否所有任务都具备代码目标 + 测试目标 + 验收命令：是。
4. 是否所有 Blocked 项都有证据与解阻条件：是。
5. 是否所有任务都有可二值判定完成标准：是。
6. 是否避免跨子系统范围扩张：是。
7. 若要求函数/数据结构级拆分，是否真正落到对象：是。

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 回退策略 |
|---|---|---|---|---|
| 接口冻结后实现长期滞后形成空壳 | Medium | 仅完成头文件，不推进主链路骨架 | MET-TODO-009~014 长期未启动 | 触发 MET-GATE-02 强制推进 |
| 高基数标签失控导致资源抖动 | High | 调用侧写入动态高基数字段 | metrics_guard_cardinality_reject_total 异常上升、queue_depth 激增 | 收紧 allowlist，回退 strict 模式并降低采样入口 |
| 导出失败被吞没 | High | 导出异常未映射错误码与审计/日志 | export_failure_total 不增长但导出失败 | 回退到 MET-TODO-014 并强制补齐失败路径观测 |
| 配置覆盖破坏 Profile 约束 | Medium | 运行时覆盖越过 Profile 边界 | 配置回放与运行行为不一致 | 回退部署层静态配置，禁用运行时覆盖 |
| 桥接接口变化触发 breaking change | High | logging/audit 接口签名变更未评审 | 边界变更无评审记录 | 立即冻结合入，执行 MET-GATE-06 |
| OTLP 依赖提前硬接入造成构建不稳定 | Medium | 在未冻结依赖策略前接入 OTLP | build-ci 构建失败或边缘 profile 超预算 | 回退到 noop/prom_text 导出，OTLP 后置 |

## 11. 可行性结论

1. 结论：可直接生成并执行函数/数据结构级专项 TODO（L3/L2 混合）。
2. 原因：
   - metrics 详细设计已明确核心接口清单与方法语义。
   - metrics 详细设计已明确核心对象字段、主流程与异常流程。
   - metrics 详细设计已明确错误码域、配置项与默认策略。
   - 已给出建议落盘目录/文件与测试出口，且可映射到现有 CMake 结构。
   - 当前阻塞集中在跨子域桥接、组件 integration/failure 用例与 OTLP 依赖，不阻断 metrics 本地闭环落地。
3. 当前最小可执行粒度：函数/接口/数据结构。
4. 若未达到全域函数级的缺失信息：
   - logging/audit/health 的最小桥接接口签名。
   - metrics 组件 integration/failure 用例与标签落盘。
   - OTLP exporter 依赖与构建策略冻结结论。
5. 下一步建议：
   - 先执行 MET-TODO-001~018，完成 metrics 本地闭环与构建/测试门禁。
   - 并行推进 MET-BLK-002~005 解阻；MET-BLK-001 已完成仓库级解阻。
   - 解阻后执行 MET-TODO-019 与 integration 验收，不跳过 breaking 评审门禁。

## 12. ARC 修复增量（2026-03-26）

| ID | 状态 | 对应问题 | 任务描述 | 代码目标 | 测试目标 | 验收命令 | 前置依赖 | 完成判定 |
|---|---|---|---|---|---|---|---|---|
| MET-TODO-021 | Not Started | ARC-01 | 在 metrics contract 边界增加 planning 阶段预算观测约束：stage=planning 标签与 planning_budget_ms 指标必须可追踪 | tests/contract/infra/metrics/MetricsPlanningStageBudgetContractTest.cpp, infra/include/metrics/MetricTypes.h | contract：MetricsPlanningStageBudgetContractTest 校验 planning 阶段标签、预算字段和退化路径统计的一致性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -R MetricsPlanningStageBudgetContractTest | MET-TODO-006、MET-TODO-018 | 仅当 planning 标签与预算观测在 contract 测试中被稳定约束，且不改动 contracts 公共对象时完成 |
| MET-TODO-022 | Not Started | ARC-02 | 将 metrics 任务纳入仓库级 Blocked-first gate 流程，禁止绕过前置解阻直接推进实现 | scripts/ci/infra_gate.sh, docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md | process test：默认执行 gate 时存在 Blocked 即失败；审批窗口通过 ALLOW_BLOCKED=1 执行例外 | bash scripts/ci/infra_gate.sh | MET-TODO-018 | 仅当 metrics 执行流程与 gate 绑定，并形成可重复执行记录时完成 |

## 13. 本轮执行记录（2026-04-01 / MET-TODO-001）

### 13.1 选中任务

1. 本轮任务：MET-TODO-001。
2. 可执行性依据：这是 metrics A 阶段的首个原子任务，无前置依赖；metrics 设计 6.6 已明确 IMetricsProvider 的四个方法语义，当前仓库尚无 metrics 公共头文件，不存在兼容面冲突。

### 13.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.6 已冻结 IMetricsProvider 的 `init(config)`、`get_meter(scope)`、`force_flush(timeout_ms)`、`shutdown(timeout_ms)` 四个入口。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.9 已给出 provider/exporter/interval/timeout 的最小配置键与默认值，可支撑 provider config 占位对象。
3. infra/CMakeLists.txt 当前尚未暴露 metrics 头文件；tests/unit/CMakeLists.txt 与 tests/contract/CMakeLists.txt 已提供可复用的 unit/contract 集中注册骨架。
4. contracts/include/error/ResultCode.h 已冻结 ResultCode/ErrorInfo 语义，满足本轮“错误语义入口可对接”的边界要求。

外部参考：

1. OpenTelemetry Metrics API（stable）指出 MeterProvider 是 Metrics API 入口，Get a Meter 必须接受 `name`，并可选接受 `version` 与 `schema_url`；据此本轮将 `MeterScope` 冻结为 `name/version/schema_url` 三字段最小占位，而不提前承诺更重的 instrumentation attributes。

D 结论：

1. Design -> Build 映射：新增 IMetricsProvider.h，冻结 `MetricsProviderConfig`、`MeterScope`、`MetricsCallDeadline`、`MetricsOperationStatus` 与 IMetricsProvider 四个方法签名。
2. Build 三件套：
   - 代码目标：新增 infra/include/metrics/IMetricsProvider.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 MetricsProviderInterfaceTest 与 MetricsProviderInterfaceBoundaryContractTest。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "MetricsProviderInterfaceTest|MetricsProviderInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "MetricsProviderInterfaceTest|MetricsProviderInterfaceBoundaryContractTest"。
3. D Gate：PASS。

### 13.3 Build 交付与证据

交付物：

1. infra/include/metrics/IMetricsProvider.h：新增 provider config、meter scope、lifecycle deadline、operation status 与 IMetricsProvider 四个入口。
2. tests/unit/infra/MetricsProviderInterfaceTest.cpp：覆盖有效 init/get_meter/force_flush/shutdown 正例，以及非法 config/timeout 的负例。
3. tests/contract/smoke/MetricsProviderInterfaceBoundaryContractTest.cpp：覆盖错误面仅暴露 contracts::ResultCode/ErrorInfo、meter scope 守卫与 deadline 守卫。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件与 unit/contract 测试注册。

验收结果：

1. Build_CMakeTools：失败，报错“无法配置项目”；按仓库既定回退策略改用 build-ci 命令链继续验收。
2. `cmake -S . -B build-ci -G Ninja`：通过。
3. `cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests`：通过；聚合执行结果为 unit 52/52、contract 101/101 全部通过，新增 `MetricsProviderInterfaceBoundaryContractTest` 已编入 contract 聚合目标。
4. `ctest --test-dir build-ci -N -R "MetricsProviderInterfaceTest|MetricsProviderInterfaceBoundaryContractTest"`：通过，发现 2 个测试，分别为 `MetricsProviderInterfaceTest` 与 `MetricsProviderInterfaceBoundaryContractTest`。
5. `ctest --test-dir build-ci --output-on-failure -R "MetricsProviderInterfaceTest|MetricsProviderInterfaceBoundaryContractTest"`：通过，2/2 tests passed。

Blocker 修复记录：

1. 环境阻塞：VS Code CMake Tools 当前无法为本仓库建立配置上下文，导致 Build_CMakeTools 不能直接用于验证。
2. 本轮最小解阻动作：回退到仓库已验证的 build-ci 命令链 `cmake -S . -B build-ci -G Ninja`、`cmake --build build-ci ...` 与 `ctest --test-dir build-ci ...`。
3. 解阻结论：Blocker cleared，同轮完成 MET-TODO-001，不扩大到其他 metrics 任务。

Build 合规复核：

1. 代码注释：新增类型命名已直接表达 provider/config/scope/deadline/error surface 语义，无需重复性注释。
2. 正负例覆盖：unit 覆盖有效生命周期调用正例与非法 config/timeout 负例；contract 覆盖错误面边界与局部 guard 二值行为。
3. 测试发现性：已用 `ctest -N -R ...` 回填 2 个新增测试的发现性证据。
4. TODO 证据回写：已回写任务状态、交付物、环境 blocker 恢复与验收摘要。
5. 提交隔离：本轮提交范围限定为 IMetricsProvider 头文件、对应 unit/contract 测试、CMake 注册与本 TODO 证据更新。

## 14. 本轮执行记录（2026-04-01 / MET-TODO-002）

### 14.1 选中任务

1. 本轮任务：MET-TODO-002。
2. 可执行性依据：MET-TODO-001 已完成，IMetricsProvider 已冻结 provider/config/scope/error surface；metrics 设计 6.6/6.7 已明确 IMeter 负责创建 Counter/Gauge/Histogram 并接收 record(sample) 调用，可直接收敛为接口头文件任务。

### 14.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.6 已冻结 IMeter 的 `create_counter`、`create_gauge`、`create_histogram`、`record` 四个入口。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.7 已明确主流程为“先取 meter 与 instrument，再写入 MetricSample”，说明 IMeter 不应提前承担聚合或配置职责。
3. docs/architecture/DASALL_infra_metrics模块详细设计.md 4.2 粒度评估指出 InstrumentHandle 具体类型约束未完全成文，因此本轮只冻结最小 handle 占位，不把实现期 registry 细节泄露到边界。
4. MET-TODO-006 尚未完成，MetricIdentity/MetricSample 还未正式落盘；为避免后续返工，本轮接口直接依赖前置声明而不重复定义临时对象模型。

外部参考：

1. OpenTelemetry Metrics API（stable）要求 Meter 提供创建 Counter/Gauge/Histogram 的能力，且明确 Meter 不负责配置；据此本轮将 IMeter 限定为 instrument 创建与 sample 录入边界，不引入配置、聚合或导出职责。

D 结论：

1. Design -> Build 映射：新增 IMeter.h，冻结 `InstrumentHandle` 最小占位，以及依赖 `MetricIdentity`、`MetricSample` 前置声明的四个接口签名。
2. Build 三件套：
   - 代码目标：新增 infra/include/metrics/IMeter.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 MetricsMeterInterfaceTest 与 MetricsMeterInterfaceBoundaryContractTest。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "MetricsMeterInterfaceTest|MetricsMeterInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "MetricsMeterInterfaceTest|MetricsMeterInterfaceBoundaryContractTest"。
3. D Gate：PASS。

### 14.3 Build 交付与证据

交付物：

1. infra/include/metrics/IMeter.h：新增 InstrumentHandle 最小占位，并冻结 create_counter/create_gauge/create_histogram/record 四个入口。
2. tests/unit/infra/MetricsMeterInterfaceTest.cpp：覆盖编译期签名检查、InstrumentHandle 正负例与 record 错误面 contract 约束。
3. tests/contract/smoke/MetricsMeterInterfaceBoundaryContractTest.cpp：覆盖 IMeter 仅依赖本地前置声明对象和 contracts 错误面的边界断言。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件与 unit/contract 测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests`：通过；聚合执行结果为 unit 53/53、contract 102/102 全部通过，新增 `MetricsMeterInterfaceBoundaryContractTest` 已编入 contract 聚合目标。
3. `ctest --test-dir build-ci -N -R "MetricsMeterInterfaceTest|MetricsMeterInterfaceBoundaryContractTest"`：通过，发现 2 个测试，分别为 `MetricsMeterInterfaceTest` 与 `MetricsMeterInterfaceBoundaryContractTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "MetricsMeterInterfaceTest|MetricsMeterInterfaceBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：接口与占位类型命名已直接表达职责，无新增实现细节，故未补充冗余注释。
2. 正负例覆盖：unit 覆盖 InstrumentHandle 正负例与错误面负例；contract 覆盖本地前置声明边界与 contracts 错误面约束。
3. 测试发现性：已用 `ctest -N -R ...` 回填 2 个新增测试的发现性证据。
4. TODO 证据回写：已回写任务状态、交付物与验收摘要。
5. 提交隔离：本轮提交范围限定为 IMeter 头文件、对应 unit/contract 测试、CMake 注册与本 TODO 证据更新。

## 15. 本轮执行记录（2026-04-01 / MET-TODO-003）

### 15.1 选中任务

1. 本轮任务：MET-TODO-003。
2. 可执行性依据：MET-TODO-001 已完成，MetricsOperationStatus 和 lifecycle deadline 已冻结；metrics 设计 6.6/6.8 已明确 exporter 需要提供 export/flush/shutdown 三个出口，而 OTLP 依赖仍未冻结，适合先收敛接口头文件。

### 15.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.6 已冻结 IMetricExporter 的 `export_batch(batch)`、`force_flush(timeout_ms)`、`shutdown(timeout_ms)` 三个入口。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.8 已明确导出异常分为远端不可达、超时、协议错误，并要求失败保持可观测与可降级。
3. docs/architecture/DASALL_infra_metrics模块详细设计.md 4.2 已指出 OTLP 首版是否启用未冻结，因此本轮只应冻结可插拔 exporter 边界，不承诺具体协议实现。
4. 当前 TODO 的阻塞项 MET-BLK-005 也要求 OTLP 依赖后置，说明接口必须兼容 noop/prom_text 等最小导出实现。

外部参考：

1. OpenTelemetry Metrics Exporters 规范将 In-memory、OTLP、Prometheus、Stdout 并列为 exporter 族，表明 exporter 接口需要先抽象“可插拔导出通道”，而不是把某一种协议细节直接写死在边界上。

D 结论：

1. Design -> Build 映射：新增 IMetricExporter.h，冻结 `MetricExportBatch` 最小批次占位，以及 export_batch/force_flush/shutdown 三个方法签名。
2. Build 三件套：
   - 代码目标：新增 infra/include/metrics/IMetricExporter.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 MetricsExporterInterfaceTest 与 MetricsExporterInterfaceBoundaryContractTest。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "MetricsExporterInterfaceTest|MetricsExporterInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "MetricsExporterInterfaceTest|MetricsExporterInterfaceBoundaryContractTest"。
3. D Gate：PASS。

### 15.3 Build 交付与证据

交付物：

1. infra/include/metrics/IMetricExporter.h：新增 MetricExportBatch 占位，并冻结 export_batch/force_flush/shutdown 三个入口。
2. tests/unit/infra/MetricsExporterInterfaceTest.cpp：覆盖有效 batch/exporter lifecycle 正例，以及空 batch/零超时负例。
3. tests/contract/smoke/MetricsExporterInterfaceBoundaryContractTest.cpp：覆盖 exporter 仅暴露本地 batch 占位与 contracts 错误面。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件与 unit/contract 测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests`：通过；聚合执行结果为 unit 54/54、contract 103/103 全部通过，新增 `MetricsExporterInterfaceBoundaryContractTest` 已编入 contract 聚合目标。
3. `ctest --test-dir build-ci -N -R "MetricsExporterInterfaceTest|MetricsExporterInterfaceBoundaryContractTest"`：通过，发现 2 个测试，分别为 `MetricsExporterInterfaceTest` 与 `MetricsExporterInterfaceBoundaryContractTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "MetricsExporterInterfaceTest|MetricsExporterInterfaceBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：exporter 接口与 batch 占位命名已直接表达导出职责与边界，无需附加实现注释。
2. 正负例覆盖：unit 覆盖有效 batch/exporter lifecycle 正例与空 batch/零超时负例；contract 覆盖 batch guard 与 contracts 错误面约束。
3. 测试发现性：已用 `ctest -N -R ...` 回填 2 个新增测试的发现性证据。
4. TODO 证据回写：已回写任务状态、交付物与验收摘要。
5. 提交隔离：本轮提交范围限定为 IMetricExporter 头文件、对应 unit/contract 测试、CMake 注册与本 TODO 证据更新。

## 16. 本轮执行记录（2026-04-01 / MET-TODO-006）

### 16.1 选中任务

1. 本轮任务：MET-TODO-006。
2. 选择原因：MET-TODO-004 依赖 MET-TODO-006，尚不可执行；按 blocker-first 规则先完成最小 blocker fix，用 MetricTypes 冻结解开后续 config policy 与 meter/sample 语义依赖。

### 16.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.5 已冻结 MetricIdentity、MetricSample、MetricLabels、HistogramConfig 四类对象及其核心字段。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.9 已给出默认 histogram buckets 和 label allowlist，可直接下沉为默认值与静态 guard。
3. MET-TODO-002 已把 IMeter 收敛为前置声明 `MetricIdentity`/`MetricSample` 的接口，说明对象头文件现在是后续 004/009/010/011 的共同前置。
4. 当前仓库尚无 metrics 对象定义，因此本轮可以一次性锁定字段与默认 guard，而不会与现有实现冲突。

外部参考：

1. OpenTelemetry Metrics API 对 instrument name 规定了首字符必须是字母、仅允许 ASCII 字母数字与 `_.-/`，并把 unit 视为受限长度的 ASCII 字符串；据此本轮把 MetricIdentity 的 name/unit guard 明确成可程序化校验。

D 结论：

1. Design -> Build 映射：新增 MetricTypes.h，冻结 `MetricType`、`MetricTemporality`、`MetricIdentity`、`MetricLabels`、`MetricSample`、`HistogramConfig` 及最小 allowlist 常量。
2. Build 三件套：
   - 代码目标：新增 infra/include/metrics/MetricTypes.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 MetricTypesTest，覆盖 identity/sample/labels/histogram 的正负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -N -R MetricTypesTest && ctest --test-dir build-ci --output-on-failure -R MetricTypesTest。
3. D Gate：PASS。

### 16.3 Build 交付与证据

交付物：

1. infra/include/metrics/MetricTypes.h：新增 MetricType/MetricTemporality 枚举、MetricIdentity、MetricLabels、MetricSample、HistogramConfig 与 allowlist 常量。
2. tests/unit/infra/MetricTypesTest.cpp：覆盖 name/unit guard、labels 必填 guard、counter sample 非负约束与 histogram bucket 单调性。
3. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：完成头文件与 unit 测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_unit_tests`：通过；聚合执行结果为 unit 55/55 全部通过，新增 `MetricTypesTest` 已编入 unit 聚合目标。
3. `ctest --test-dir build-ci -N -R MetricTypesTest`：通过，发现 1 个测试：`MetricTypesTest`。
4. `ctest --test-dir build-ci --output-on-failure -R MetricTypesTest`：通过，1/1 tests passed。

Blocker 恢复结论：

1. MET-TODO-004 的关键前置依赖已由本轮解除，后续可以基于 MetricIdentity/MetricLabels 继续冻结 IMetricConfigPolicy，而不必再引入一次性占位对象。

Build 合规复核：

1. 代码注释：对象字段和 guard 函数命名已直接表达语义，无需重复注释。
2. 正负例覆盖：unit 覆盖 identity/sample 正例，以及非法 name/unit、缺失 labels、非单调 buckets、counter 负值等负例。
3. 测试发现性：已用 `ctest -N -R MetricTypesTest` 回填发现性证据。
4. TODO 证据回写：已回写任务状态、交付物与 blocker 恢复结论。
5. 提交隔离：本轮提交范围限定为 MetricTypes 头文件、对应 unit 测试、CMake 注册与本 TODO 证据更新。

## 17. 本轮执行记录（2026-04-01 / MET-TODO-004）

### 17.1 选中任务

1. 本轮任务：MET-TODO-004。
2. 可执行性依据：MET-TODO-006 已在上一轮完成并解除前置依赖；IMeter 和 MetricTypes 已冻结 identity/sample/labels 语义，metrics 设计 6.6/6.9 现在可以直接收敛 config policy 接口。

### 17.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.6 已冻结 IMetricConfigPolicy 的 `validate_identity`、`normalize_labels`、`should_accept` 三个入口。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.9 已明确 labels.allowlist 默认为 `module,stage,profile,outcome,error_code`，且需要 max cardinality 治理入口。
3. MET-TODO-006 已把 MetricLabels 收敛为固定五元组，因此本轮可以直接把 allowlist 固化为结构化边界，而不引入运行期动态标签字典。
4. TODO 中 MET-BLK-003 指出 taxonomy 尚未全局评审，因此本轮只冻结核心 allowlist 集合和最小 normalize/accept 语义，不扩张到额外标签域。

外部参考：

1. Prometheus 官方最佳实践明确提醒每个唯一 label 组合都会产生新的 time series，禁止把 user id、email 等高基数字段写入标签；据此本轮 policy 仅接受固定 allowlist 字段，并把空 `error_code` 规范化为稳定占位值而不是放开动态标签面。

D 结论：

1. Design -> Build 映射：新增 IMetricConfigPolicy.h，冻结 `MetricPolicyResult`、`MetricLabelsNormalizationResult` 与三个策略接口。
2. Build 三件套：
   - 代码目标：新增 infra/include/metrics/IMetricConfigPolicy.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 MetricsConfigPolicyInterfaceTest 与 MetricsConfigPolicyInterfaceBoundaryContractTest。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "MetricsConfigPolicyInterfaceTest|MetricsConfigPolicyInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "MetricsConfigPolicyInterfaceTest|MetricsConfigPolicyInterfaceBoundaryContractTest"。
3. D Gate：PASS。

### 17.3 Build 交付与证据

交付物：

1. infra/include/metrics/IMetricConfigPolicy.h：新增 identity 校验、labels 规范化、accept 决策三类结果对象与接口签名。
2. tests/unit/infra/MetricsConfigPolicyInterfaceTest.cpp：覆盖有效 identity/labels 正例，以及非法 identity/labels 的负例。
3. tests/contract/smoke/MetricsConfigPolicyInterfaceBoundaryContractTest.cpp：覆盖本地 MetricLabels 边界与 contracts 错误面约束。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件与 unit/contract 测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests`：通过；聚合执行结果为 unit 56/56、contract 104/104 全部通过，新增 `MetricsConfigPolicyInterfaceBoundaryContractTest` 已编入 contract 聚合目标。
3. `ctest --test-dir build-ci -N -R "MetricsConfigPolicyInterfaceTest|MetricsConfigPolicyInterfaceBoundaryContractTest"`：通过，发现 2 个测试，分别为 `MetricsConfigPolicyInterfaceTest` 与 `MetricsConfigPolicyInterfaceBoundaryContractTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "MetricsConfigPolicyInterfaceTest|MetricsConfigPolicyInterfaceBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：结果对象与接口命名已直接表达策略职责和 contracts 错误面，无需额外注释。
2. 正负例覆盖：unit 覆盖 identity/labels 正负例；contract 覆盖 allowlist 边界与 contracts 错误面约束。
3. 测试发现性：已用 `ctest -N -R ...` 回填 2 个新增测试的发现性证据。
4. TODO 证据回写：已回写任务状态、交付物与由 006 解阻后的恢复执行摘要。
5. 提交隔离：本轮提交范围限定为 IMetricConfigPolicy 头文件、对应 unit/contract 测试、CMake 注册与本 TODO 证据更新。

## 18. 本轮执行记录（2026-04-01 / MET-TODO-007）

### 18.1 选中任务

1. 本轮任务：MET-TODO-007。
2. 可执行性依据：MET-TODO-006 已冻结 MetricTypes；MET-TODO-005 与后续聚合/导出任务都需要稳定的 ExportBatchReport 与 MetricsModuleSnapshot 来承载 success/fail、queue/degraded、exporter_state 语义。

### 18.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.5 已明确 ExportBatchReport 与 MetricsModuleSnapshot 属于首批核心对象，需要先于实现骨架冻结字段语义。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.8 要求导出失败进入 degraded_mode 时仍保留本地聚合与健康指标，因此健康快照必须独立表达 degraded 状态。
3. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.10 已把 queue_depth、guard_reject_total、exporter_state 归入 metrics 健康观测最小集合，本轮对象需要直接覆盖这些字段。
4. 专项 TODO 中 MET-TODO-005 明确 snapshot() 返回对象必须承载 queue/degraded/exporter_state，因此必须先完成本轮对象冻结，后续健康探针接口才能稳定落盘。

D 结论：

1. Design -> Build 映射：新增 MetricsSnapshots.h，冻结 ExportBatchReport 与 MetricsModuleSnapshot 两个对象，并把最小有效性判定压缩为二值 guard，避免后续实现阶段再改字段面。
2. Build 三件套：
   - 代码目标：新增 infra/include/metrics/MetricsSnapshots.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 MetricsSnapshotsTest，覆盖导出成功/失败与健康/降级两组二值语义，并接入 tests/unit 聚合。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -N -R MetricsSnapshotsTest && ctest --test-dir build-ci --output-on-failure -R MetricsSnapshotsTest。
3. D Gate：PASS。

### 18.3 Build 交付与证据

交付物：

1. infra/include/metrics/MetricsSnapshots.h：新增 ExportBatchReport 与 MetricsModuleSnapshot，冻结 success/fail/latency/dropped、queue_depth/guard_reject_total/exporter_state/degraded 字段与最小 guard。
2. tests/unit/infra/MetricsSnapshotsTest.cpp：覆盖有效导出批次、失败批次、健康快照、降级快照与无效占位值的正负例。
3. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：完成头文件与 unit 测试注册。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_unit_tests：通过；聚合执行结果为 unit 57/57 全部通过，新增 MetricsSnapshotsTest 已编入 unit 聚合目标。
3. ctest --test-dir build-ci -N -R MetricsSnapshotsTest：通过，发现 1 个测试，为 MetricsSnapshotsTest。
4. ctest --test-dir build-ci --output-on-failure -R MetricsSnapshotsTest：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：对象字段与 is_valid/has_failures/is_healthy 命名已直接表达快照语义，无需额外注释。
2. 正负例覆盖：unit 已覆盖 success/failure、healthy/degraded、finite/non-finite 与空 exporter_state 等正负路径。
3. 测试发现性：已用 ctest -N -R MetricsSnapshotsTest 回填新增 unit 用例发现性证据。
4. TODO 证据回写：已回写任务状态、交付物与对 MET-TODO-005 的解阻前置说明。
5. 提交隔离：本轮提交范围限定为 MetricsSnapshots 对象头文件、对应 unit 测试、CMake 注册与本 TODO 证据更新。

## 19. 本轮执行记录（2026-04-01 / MET-TODO-005）

### 19.1 选中任务

1. 本轮任务：MET-TODO-005。
2. 可执行性依据：MET-TODO-007 已在上一轮冻结 MetricsModuleSnapshot，当前可以直接落 IMetricsHealthProbe 的 snapshot() 边界；阻塞项里提到的 infra/health 统一探针签名仍未冻结，因此本轮只输出 metrics 私有快照接口，不复用通用 probe 合同。

### 19.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.6 已把 IMetricsHealthProbe 收敛为单一 snapshot() 接口，没有额外 monitor/register 语义。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.10 已把 queue_depth、guard_reject_total、exporter_state、degraded_mode 列为 metrics 健康观测最小集合，因此返回值必须直接承载这些字段。
3. 专项 TODO 中 MET-TODO-005 的阻塞说明明确指出与 infra/health 统一探针签名尚未冻结，所以本轮必须避免把 metrics 健康探针耦合到通用 IHealthProbe/ProbeResult 合同。
4. MET-TODO-007 已提供 MetricsModuleSnapshot，对 queue/degraded/exporter_state 语义已有冻结对象，本轮无需再引入临时占位类型。

D 结论：

1. Design -> Build 映射：新增 IMetricsHealthProbe.h，只保留 snapshot() const 一个出口，并直接返回 MetricsModuleSnapshot，保证 metrics 私有健康语义不泄漏到通用 infra/health 合同。
2. Build 三件套：
   - 代码目标：新增 infra/include/metrics/IMetricsHealthProbe.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 MetricsHealthProbeInterfaceTest，验证 snapshot() 签名、抽象边界与“不吸收通用 probe() 语义”这两个约束。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests && ctest --test-dir build-ci -N -R MetricsHealthProbeInterfaceTest && ctest --test-dir build-ci --output-on-failure -R MetricsHealthProbeInterfaceTest。
3. D Gate：PASS。

### 19.3 Build 交付与证据

交付物：

1. infra/include/metrics/IMetricsHealthProbe.h：新增 metrics 私有健康探针接口，冻结 snapshot() const -> MetricsModuleSnapshot 边界。
2. tests/unit/infra/MetricsHealthProbeInterfaceTest.cpp：覆盖 snapshot 签名、抽象接口、快照字段可达与不复用通用 probe() 语义的正负例。
3. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：完成头文件与 unit 测试注册。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_infra dasall_unit_tests：通过；聚合执行结果为 unit 58/58 全部通过，新增 MetricsHealthProbeInterfaceTest 已编入 unit 聚合目标。
3. ctest --test-dir build-ci -N -R MetricsHealthProbeInterfaceTest：通过，发现 1 个测试，为 MetricsHealthProbeInterfaceTest。
4. ctest --test-dir build-ci --output-on-failure -R MetricsHealthProbeInterfaceTest：通过，1/1 tests passed。

Build 合规复核：

1. 边界收敛：接口仅暴露 snapshot()，没有引入 probe/register/evaluate 等通用 health monitor 语义。
2. 正负例覆盖：unit 已覆盖有效 snapshot 返回、抽象接口约束与禁用通用 probe() 入口这三类检查。
3. 测试发现性：已用 ctest -N -R MetricsHealthProbeInterfaceTest 回填新增 unit 用例发现性证据。
4. TODO 证据回写：已回写任务状态、交付物与由 MET-TODO-007 解阻后的恢复执行摘要。
5. 提交隔离：本轮提交范围限定为 IMetricsHealthProbe 头文件、对应 unit 测试、CMake 注册与本 TODO 证据更新。

## 20. 本轮执行记录（2026-04-01 / MET-TODO-008）

### 20.1 选中任务

1. 本轮任务：MET-TODO-008。
2. 可执行性依据：MET-TODO-001 已冻结 metrics 对外 contracts 错误面，当前可以把 metrics 私有错误码域稳定映射到既有 contracts::ResultCode；后续 009、010、012、014、015、016 也都依赖这组错误码来返回可判定失败。

### 20.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.6 已冻结 metrics 私有错误语义集合：provider_not_ready、identity_invalid、label_cardinality_exceeded、queue_full、export_failure、export_timeout、config_invalid，共 7 个错误码。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.8 把标签异常、队列异常、导出异常、配置异常四类失败路径拆开，说明错误码既要区分来源，也要能向上映射到稳定 contracts 失败域。
3. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.9 给出了配置项表，因此 config_invalid 需要有独立来源锚点，而不是混入通用运行时错误。
4. 专项 TODO 中 MET-TODO-008 的阻塞说明明确指出 contracts 映射矩阵未成文，因此本轮除了头文件，还必须用 contract 测试把映射矩阵固化下来。

D 结论：

1. Design -> Build 映射：新增 MetricsErrors.h，冻结 MetricsErrorCode 枚举、metrics_error_code_name() 与 map_metrics_error_code()，并让每个错误码都携带 source_anchor 与 reason，满足来源锚点可追溯要求。
2. Build 三件套：
   - 代码目标：新增 infra/include/metrics/MetricsErrors.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 MetricsErrorsTest 与 MetricsErrorMappingContractTest，分别固化枚举稳定性和 contracts 映射矩阵。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "MetricsErrorsTest|MetricsErrorMappingContractTest" && ctest --test-dir build-ci --output-on-failure -R "MetricsErrorsTest|MetricsErrorMappingContractTest"。
3. D Gate：PASS。

### 20.3 Build 交付与证据

交付物：

1. infra/include/metrics/MetricsErrors.h：新增 7 个 metrics 私有错误码、稳定名称表、contracts 映射表与 source_anchor/reason 元数据。
2. tests/unit/infra/MetricsErrorsTest.cpp：覆盖枚举值与名称稳定性，以及 7 个错误码来源锚点非空检查。
3. tests/contract/smoke/MetricsErrorMappingContractTest.cpp：覆盖 7 个错误码到 contracts::ResultCode 的精确映射矩阵、来源锚点与 MET_E_* 本地域名约束。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件、unit 测试、contract 测试注册。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_unit_tests dasall_contract_tests：通过；dasall_unit_tests 与 dasall_contract_tests 均成功完成，新增 MetricsErrorsTest 与 MetricsErrorMappingContractTest 已编入聚合目标。
3. ctest --test-dir build-ci -N -R "MetricsErrorsTest|MetricsErrorMappingContractTest"：通过，发现 2 个测试，分别为 MetricsErrorsTest 与 MetricsErrorMappingContractTest。
4. ctest --test-dir build-ci --output-on-failure -R "MetricsErrorsTest|MetricsErrorMappingContractTest"：通过，2/2 tests passed。

Build 合规复核：

1. 错误码冻结：7 个错误码的枚举值与名称均由 unit 固化，防止后续重排或改名。
2. 来源锚点：7 个错误码都带有非空 source_anchor 与 reason，满足 TODO 的来源追溯判定。
3. 合同边界：contract 测试已固化全部映射矩阵，确保 metrics 私有错误域只向上暴露既有 contracts::ResultCode。
4. 测试发现性：已用 ctest -N -R "MetricsErrorsTest|MetricsErrorMappingContractTest" 回填 unit/contract 双测试发现性证据。
5. 提交隔离：本轮提交范围限定为 MetricsErrors 头文件、对应 unit/contract 测试、CMake 注册与本 TODO 证据更新。

## 21. 本轮执行记录（2026-04-06 / MET-TODO-009）

### 21.1 选中任务

1. 本轮任务：MET-TODO-009。
2. 可执行性依据：MET-TODO-001、002、006、008 均已完成，且 6.2/6.7 已冻结 MetricsFacade 的统一入口职责与“先 get meter/instrument，再写入 sample”的最小主链路；当前不存在需要优先解阻的前置 Blocked 任务。

### 21.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.2 已冻结 MetricsFacade 为模块统一入口，职责是封装仪表注册与写入接口。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.3 已明确 MetricsFacade 的输入来自上游采样调用，输出去向为 Registry/AggregationEngine，且必须返回可判定状态码。
3. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.4 已给出依赖顺序 `MetricsFacade -> InstrumentRegistry -> AggregationEngine`，因此本轮只实现 façade 生命周期与写入骨架，不越权实现 registry/aggregation 语义。
4. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.7 已冻结正常路径步骤 1/2：先获取 meter 与 instrument，再写入 MetricSample。
5. 当前仓库尚无 infra/src/metrics/ 运行时源码，而 MET-TODO-017 尚未完成，因此本轮必须保持实现为 private source，并由 unit target 临时直编私有源码验证，不提前改写 dasall_infra 入图策略。

外部参考：

1. OpenTelemetry Metrics API（stable）明确 `MeterProvider` 是 API 入口、`Meter` 负责创建 `Instrument`，且相同参数的 Meter 应保持一致实例语义；据此本轮将 `MetricsFacade` 实现为 provider 入口，并在同一 `MeterScope` 下缓存 meter 占位实例，而不把配置职责下沉到 meter。

D 结论：

1. Design -> Build 映射：新增 private `MetricsFacade.h/.cpp`，实现 `IMetricsProvider` 的 `init/get_meter/force_flush/shutdown` 骨架，并以内嵌 `FacadeMeter` 承担 `create_counter/create_gauge/create_histogram/record` 最小代理语义。
2. 错误语义映射：本轮只使用已冻结的 `MetricsErrors`，把未初始化路径映射到 `ProviderNotReady`，把非法 sample/identity 路径映射到 `IdentityInvalid`，把无效 deadline/config 路径映射到 `ConfigInvalid`。
3. Build 三件套：
   - 代码目标：新增 infra/src/metrics/MetricsFacade.h、infra/src/metrics/MetricsFacade.cpp。
   - 测试目标：新增 tests/unit/infra/metrics/MetricsFacadeTest.cpp，并在 tests/unit/infra/CMakeLists.txt 注册 `MetricsFacadeTest`。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_metrics_facade_unit_test`、`cmake --build build-ci --target dasall_unit_tests`、`ctest --test-dir build-ci -N -R MetricsFacadeTest`、`ctest --test-dir build-ci --output-on-failure -R MetricsFacadeTest`、`ctest --test-dir build-ci --output-on-failure -L unit`。
4. D Gate：PASS。

### 21.3 Build 交付与证据

交付物：

1. infra/src/metrics/MetricsFacade.h：新增 `MetricsFacade` 私有声明，冻结 lifecycle state、meter cache、last sample 与 write attempt 观测面。
2. infra/src/metrics/MetricsFacade.cpp：新增 façade 生命周期骨架、同 scope meter 缓存、最小 `FacadeMeter` 实现，以及基于 `MetricsErrors` 的 provider_not_ready/config_invalid/identity_invalid 失败映射。
3. tests/unit/infra/metrics/MetricsFacadeTest.cpp：覆盖未初始化路径、初始化后 meter 缓存与有效 record 正例、非法 identity 负例。
4. tests/unit/infra/CMakeLists.txt：新增 `dasall_metrics_facade_unit_test`，在 `MET-TODO-017` 完成前临时直编 `infra/src/metrics/MetricsFacade.cpp` 并加入 `MetricsFacadeTest` 注册。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_metrics_facade_unit_test`：通过；仅出现仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是本轮新增问题。
3. `ctest --test-dir build-ci -N -R MetricsFacadeTest`：通过，发现 1 个测试：`MetricsFacadeTest`。
4. `ctest --test-dir build-ci --output-on-failure -R MetricsFacadeTest`：通过，1/1 tests passed。
5. `cmake --build build-ci --target dasall_unit_tests`：通过。
6. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，unit 标签 135/135 tests passed。

Build 合规复核：

1. 代码注释：`MetricsFacade` 与 `FacadeMeter` 的状态、缓存与错误路径命名已直接表达语义，无需额外注释。
2. 正负例覆盖：unit 已覆盖未初始化拒绝、初始化后正例 record、非法 identity 负例三类关键路径。
3. 测试发现性：已用 `ctest -N -R MetricsFacadeTest` 验证新增测试在当前 CMake 注册下可发现。
4. TODO 证据回写：已回写任务状态、交付物、临时直编策略与验收摘要。
5. 提交隔离：本轮提交范围限定为 MetricsFacade 私有源码、对应 unit 测试、CMake 注册、专项 TODO 与 worklog 证据更新。

## 22. 本轮执行记录（2026-04-06 / MET-TODO-010）

### 22.1 选中任务

1. 本轮任务：MET-TODO-010。
2. 可执行性依据：MET-TODO-006 与 MET-TODO-008 已完成，且 MET-TODO-009 已落下 MetricsFacade 最小入口骨架；根据 7.1 顺序编排，registry 是主链路骨架阶段中紧随 façade 的下一原子任务，当前无前置 Blocked 任务需要先处理。

### 22.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.2 已冻结 InstrumentRegistry 的职责为“管理 Instrument 定义、唯一性与生命周期”。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.3 已明确 InstrumentRegistry 的输入是 instrument 定义请求，输出是 InstrumentHandle，语义契约是“同名同语义唯一”。
3. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.4 已给出 `MetricsFacade -> InstrumentRegistry -> AggregationEngine` 依赖顺序，因此本轮应把 façade 的 instrument 创建路径切到 registry，而不是继续在 façade 内部扩张唯一性逻辑。
4. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.5 已冻结 `MetricIdentity` 的 `name/type/unit/description` 语义面，可直接作为 registry 判定“同名同语义”的最小比较集合。
5. MET-TODO-009 当前已存在 `FacadeMeter` 占位和 record 骨架，本轮可以在不引入 aggregation 的前提下，把“必须先创建 instrument 再 record”收敛成 registry 可测约束。

外部参考：

1. OpenTelemetry Metrics API（stable）将 instrument 的 identifying fields 定义为 `name`、`kind`、`unit`、`description`，且所有 identifying fields 相等时才视为 identical；据此本轮把 registry 的 canonical identity 判定收敛到 `MetricIdentity(name/type/unit/description)`，相同语义返回同一 handle，语义冲突则拒绝注册。

D 结论：

1. Design -> Build 映射：新增 private `InstrumentRegistry.h/.cpp`，实现 `register_identity`、`find_identity`、`size` 与 `InstrumentRegistrationResult`。
2. 接线策略：将 `MetricsFacade` 的 `FacadeMeter::create_*` 路径切到 `InstrumentRegistry::register_identity()`，并让 `record()` 仅接受已完成 instrument 注册的 sample identity。
3. Build 三件套：
   - 代码目标：新增 infra/src/metrics/InstrumentRegistry.h、infra/src/metrics/InstrumentRegistry.cpp，并调整 infra/src/metrics/MetricsFacade.{h,cpp}。
   - 测试目标：新增 tests/unit/infra/metrics/InstrumentRegistryTest.cpp，并把 MetricsFacadeTest 保留为 registry 接线回归。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_metrics_facade_unit_test dasall_instrument_registry_unit_test`、`cmake --build build-ci --target dasall_unit_tests`、`ctest --test-dir build-ci -N -R "(InstrumentRegistryTest|MetricsFacadeTest)"`、`ctest --test-dir build-ci --output-on-failure -R "(InstrumentRegistryTest|MetricsFacadeTest)"`、`ctest --test-dir build-ci --output-on-failure -L unit`。
4. D Gate：PASS。

### 22.3 Build 交付与证据

交付物：

1. infra/src/metrics/InstrumentRegistry.h：新增 registration result、registry entry 与 `register_identity/find_identity/size` 私有声明。
2. infra/src/metrics/InstrumentRegistry.cpp：新增同 identity 幂等注册、同名异义拒绝与 canonical handle 生成逻辑，并统一复用 `MetricsErrors::IdentityInvalid` 错误面。
3. infra/src/metrics/MetricsFacade.h、infra/src/metrics/MetricsFacade.cpp：把 façade 的 instrument 创建路径与 record 前置检查切到 registry 骨架，不再在 façade 内部直接生成 handle。
4. tests/unit/infra/metrics/InstrumentRegistryTest.cpp：覆盖同 identity 重复注册正例、同名异义冲突负例。
5. tests/unit/infra/CMakeLists.txt：为 `MetricsFacadeTest` 补编 `InstrumentRegistry.cpp`，并新增 `dasall_instrument_registry_unit_test` 与 `InstrumentRegistryTest` 注册。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_metrics_facade_unit_test dasall_instrument_registry_unit_test`：通过；仅出现仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是本轮新增问题。
3. `ctest --test-dir build-ci -N -R "(InstrumentRegistryTest|MetricsFacadeTest)"`：通过，发现 2 个测试：`MetricsFacadeTest`、`InstrumentRegistryTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "(InstrumentRegistryTest|MetricsFacadeTest)"`：通过，2/2 tests passed。
5. `cmake --build build-ci --target dasall_unit_tests`：通过。
6. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，unit 标签 136/136 tests passed。

Build 合规复核：

1. 代码注释：registry 结果对象、canonical handle 与 conflict 路径命名已直接表达语义，无需补充重复性注释。
2. 正负例覆盖：unit 已覆盖同 identity 幂等正例、同名异义冲突负例，以及 MetricsFacade 对 registry 接线的回归正例。
3. 测试发现性：已用 `ctest -N -R "(InstrumentRegistryTest|MetricsFacadeTest)"` 验证新增 registry 测试与 façade 回归测试均可发现。
4. TODO 证据回写：已回写任务状态、registry->facade 接线策略与验收摘要。
5. 提交隔离：本轮提交范围限定为 InstrumentRegistry 私有源码、MetricsFacade 接线变更、对应 unit 测试、CMake 注册、专项 TODO 与 worklog 证据更新。

## 23. 本轮执行记录（2026-04-06 / MET-TODO-011）

### 23.1 选中任务

1. 本轮任务：MET-TODO-011。
2. 可执行性依据：MET-TODO-010 已完成 registry 骨架，主链路顺序表要求本轮进入 `facade -> registry -> aggregation` 的最后一段；TODO 已明确并发参数未冻结，因此本轮可以单线程可测实现推进，不需要先解阻其他 Blocked 任务。

### 23.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.2 已冻结 AggregationEngine 的职责为“执行 Counter/Gauge/Histogram 聚合与窗口滚动”。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.3 已明确 AggregationEngine 的输入是采样事件流，输出是聚合快照，约束为“线程安全、无锁优先”；结合专项 TODO 的粒度说明，本轮先落单线程可测骨架，不提前承诺并发实现细节。
3. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.4 已给出依赖顺序 `MetricsFacade -> InstrumentRegistry -> AggregationEngine`，因此本轮需要把 façade 的 record 路径正式接到 aggregation，而不是继续停留在 registry-only 检查。
4. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.7 已冻结步骤 5：AggregationEngine 聚合样本并维护窗口，这是本轮最小闭环锚点。
5. 专项 TODO 4.2 与 6.1 已明确本任务的最小策略是“先落单线程可测实现”，说明 histogram bucket/window 先保留默认配置，不在本轮扩张到 ConfigCenter/reader/exporter。

外部参考：

1. OpenTelemetry Metrics API（stable）明确 Counter 记录非负增量、Gauge 记录当前绝对值、Histogram 记录数值样本并支持显式 bucket boundaries；据此本轮将 Counter 聚合为累计值，Gauge 聚合为最新值并保留观测 extrema，Histogram 聚合为 count/sum/min/max + explicit bucket counters。

D 结论：

1. Design -> Build 映射：新增 private `AggregationEngine.h/.cpp`，实现 `aggregate_counter`、`aggregate_gauge`、`aggregate_histogram`、`aggregate` 与 `snapshot`。
2. 主链接线：给 `MetricsFacade` 增加 `AggregationEngine` 成员与 `aggregation_snapshot()` 私有观测口，把 `record()` 从“registry 通过后直接成功”推进到“registry 检查后进入 aggregation”。
3. Build 三件套：
   - 代码目标：新增 infra/src/metrics/AggregationEngine.h、infra/src/metrics/AggregationEngine.cpp，并调整 infra/src/metrics/MetricsFacade.{h,cpp}。
   - 测试目标：新增 tests/unit/infra/metrics/MetricsAggregationTest.cpp，并在 tests/unit/infra/CMakeLists.txt 注册 `MetricsAggregationTest`；同时让 `MetricsFacadeTest` 直编 `AggregationEngine.cpp` 做回归。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_metrics_facade_unit_test dasall_instrument_registry_unit_test dasall_metrics_aggregation_unit_test`、`cmake --build build-ci --target dasall_unit_tests`、`ctest --test-dir build-ci -N -R "(MetricsAggregationTest|InstrumentRegistryTest|MetricsFacadeTest)"`、`ctest --test-dir build-ci --output-on-failure -R "(MetricsAggregationTest|InstrumentRegistryTest|MetricsFacadeTest)"`、`ctest --test-dir build-ci --output-on-failure -L unit`。
4. D Gate：PASS。

### 23.3 Build 交付与证据

交付物：

1. infra/src/metrics/AggregationEngine.h：新增 `AggregatedMetricValue`、`AggregationSnapshot` 与 `AggregationEngine` 私有声明。
2. infra/src/metrics/AggregationEngine.cpp：新增 Counter/Gauge/Histogram/UpDownCounter 单线程聚合逻辑、显式 bucket 计数、same-name semantic guard 与快照输出。
3. infra/src/metrics/MetricsFacade.h、infra/src/metrics/MetricsFacade.cpp：新增 `aggregation_snapshot()` 观测口，并把 `record()` 接到 `AggregationEngine::aggregate()`。
4. tests/unit/infra/metrics/MetricsAggregationTest.cpp：覆盖 Counter 累计、Gauge 最新值、Histogram bucket 计数，以及 `record -> registry -> aggregation` 主链路断言。
5. tests/unit/infra/CMakeLists.txt：为 `MetricsFacadeTest` 增编 `AggregationEngine.cpp`，并新增 `dasall_metrics_aggregation_unit_test` 与 `MetricsAggregationTest` 注册。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_metrics_facade_unit_test dasall_instrument_registry_unit_test dasall_metrics_aggregation_unit_test`：通过；仅出现仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是本轮新增问题。
3. `ctest --test-dir build-ci -N -R "(MetricsAggregationTest|InstrumentRegistryTest|MetricsFacadeTest)"`：通过，发现 3 个测试：`MetricsFacadeTest`、`InstrumentRegistryTest`、`MetricsAggregationTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "(MetricsAggregationTest|InstrumentRegistryTest|MetricsFacadeTest)"`：通过，3/3 tests passed。
5. `cmake --build build-ci --target dasall_unit_tests`：通过。
6. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，unit 标签 137/137 tests passed。

Build 合规复核：

1. 代码注释：聚合对象字段、bucket 计数与 façade 桥接方法命名已直接表达语义，无需附加解释性注释。
2. 正负例覆盖：本轮 `MetricsAggregationTest` 覆盖三类聚合正例；负例语义由既有 `MetricsFacadeTest` 与 `InstrumentRegistryTest` 继续回归，确保 main-chain 聚合没有破坏前两轮错误面。
3. 测试发现性：已用 `ctest -N -R "(MetricsAggregationTest|InstrumentRegistryTest|MetricsFacadeTest)"` 验证三条 metrics 主链 unit 用例均可发现。
4. TODO 证据回写：已回写任务状态、aggregation 接线策略与验收摘要。
5. 提交隔离：本轮提交范围限定为 AggregationEngine 私有源码、MetricsFacade 聚合接线、对应 unit 测试、CMake 注册、专项 TODO 与 worklog 证据更新。

## 24. 本轮执行记录（2026-04-06 / MET-TODO-012）

### 24.1 选中任务

1. 本轮任务：MET-TODO-012。
2. 可执行性依据：MET-TODO-004、MET-TODO-006、MET-TODO-008 已完成，且 `MET-TODO-009~011` 已把主链推进到 `record -> registry -> aggregation`；当前不存在要求先完成的前置 Blocked 任务，可直接把标签治理插入主链。

### 24.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.2 已冻结 CardinalityGuard 的职责为“标签白名单与高基数拦截/降级”。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.3 已明确 CardinalityGuard 的输入是标签集合，输出是“通过/拒绝/降级结果”，且拒绝必须可观测。
3. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.7 已把 guard 固定在 `MetricContextEnricher` 之后、`AggregationEngine` 之前，因此本轮应把 guard 接到 MetricsFacade 的 record 路径，而不是停留为独立工具类。
4. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.8 已冻结标签异常恢复语义：拒绝该样本并累计 `guard_reject_total`，同时保留 sampling_drop 原因。
5. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.9 已冻结 allowlist 初版为 `module/stage/profile/outcome/error_code`，因此虽然 taxonomy 全局评审尚未完成，本轮仍可按专项 TODO 的解阻条件先实现这五个稳定标签键。
6. tests/unit/infra/MetricTypesTest.cpp 与 tests/unit/infra/MetricsConfigPolicyInterfaceTest.cpp 已把 `module/stage/profile/outcome` 必填、空 `error_code` 归一化为 `none` 的边界固化为既有门禁，可直接复用到 guard 骨架。

D 结论：

1. Design -> Build 映射：新增 private `CardinalityGuard.h/.cpp`，实现 `validate_labels` 与 `reject_with_reason`，固定 allowlist 校验、空 `error_code -> none` 归一化，以及按 metric name 维度的 label signature cardinality 上限控制。
2. 主链接线：调整 `MetricsFacade`，把 `record()` 从 `registry -> aggregation` 推进到 `registry -> guard -> aggregation`，并通过 `module_snapshot()` 暴露 `guard_reject_total`。
3. Build 三件套：
   - 代码目标：新增 infra/src/metrics/CardinalityGuard.h、infra/src/metrics/CardinalityGuard.cpp，并调整 infra/src/metrics/MetricsFacade.{h,cpp}。
   - 测试目标：新增 tests/unit/infra/metrics/MetricsCardinalityGuardTest.cpp，并在 tests/unit/infra/CMakeLists.txt 注册 `MetricsCardinalityGuardTest`；同时让 `MetricsFacadeTest` 与 `MetricsAggregationTest` 直编 `CardinalityGuard.cpp` 做回归。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_metrics_facade_unit_test dasall_metrics_cardinality_guard_unit_test dasall_metrics_aggregation_unit_test`、`ctest --test-dir build-ci -N -R "(MetricsCardinalityGuardTest|MetricsFacadeTest|MetricsAggregationTest)"`、`ctest --test-dir build-ci --output-on-failure -R "(MetricsCardinalityGuardTest|MetricsFacadeTest|MetricsAggregationTest)"`、`cmake --build build-ci --target dasall_unit_tests`、`ctest --test-dir build-ci --output-on-failure -L unit`。
4. D Gate：PASS。

### 24.3 Build 交付与证据

交付物：

1. infra/src/metrics/CardinalityGuard.h：新增 `MetricLabelEntry`、`CardinalityGuardDecision` 与 `CardinalityGuard` 私有声明，冻结 allowlist 判定、reject_total 与 per-metric cardinality 观察面。
2. infra/src/metrics/CardinalityGuard.cpp：新增空 `error_code` 归一化、未知标签拒绝、重复 label key 拒绝、per-metric label signature 上限控制，以及统一复用 `MetricsErrors::LabelCardinalityExceeded` 错误面。
3. infra/src/metrics/MetricsFacade.h、infra/src/metrics/MetricsFacade.cpp：新增 `module_snapshot()` 观测口，把 `record()` 路径接到 CardinalityGuard，并保证拒绝样本不会进入 AggregationEngine。
4. tests/unit/infra/metrics/MetricsCardinalityGuardTest.cpp：覆盖 allowlist 正例、未知标签拒绝、高基数拒绝，以及 `MetricsFacade` 对 `guard_reject_total` 的主链集成断言。
5. tests/unit/infra/CMakeLists.txt：新增 `dasall_metrics_cardinality_guard_unit_test`，并为 `MetricsFacadeTest`、`MetricsAggregationTest` 补编 `CardinalityGuard.cpp`。

验收结果：

1. Build_CMakeTools：失败，报错“生成失败：无法配置项目”；按仓库既定回退策略改用 build-ci 命令链继续验收。
2. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
3. `cmake --build build-ci --target dasall_metrics_facade_unit_test dasall_metrics_cardinality_guard_unit_test dasall_metrics_aggregation_unit_test`：通过；仅出现仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是本轮新增问题。
4. `ctest --test-dir build-ci -N -R "(MetricsCardinalityGuardTest|MetricsFacadeTest|MetricsAggregationTest)"`：通过，发现 3 个测试：`MetricsFacadeTest`、`MetricsCardinalityGuardTest`、`MetricsAggregationTest`。
5. `ctest --test-dir build-ci --output-on-failure -R "(MetricsCardinalityGuardTest|MetricsFacadeTest|MetricsAggregationTest)"`：通过，3/3 tests passed。
6. `cmake --build build-ci --target dasall_unit_tests`：通过。
7. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，unit 标签 138/138 tests passed。

Build 合规复核：

1. 代码注释：guard 决策对象、allowlist 结果与 façade 模块快照命名已直接表达语义，无需附加解释性注释。
2. 正负例覆盖：本轮新增单测已覆盖 allowlist 正例、未知标签拒绝、高基数拒绝与 façade 集成回归四条路径。
3. 测试发现性：已用 `ctest -N -R "(MetricsCardinalityGuardTest|MetricsFacadeTest|MetricsAggregationTest)"` 验证 guard 新测与主链回归用例均可发现。
4. TODO 证据回写：已回写任务状态、guard->facade 接线策略、Build_CMakeTools 回退记录与验收摘要。
5. 提交隔离：本轮提交范围限定为 CardinalityGuard 私有源码、MetricsFacade guard 接线、对应 unit 测试、CMake 注册、专项 TODO 与 worklog 证据更新。

## 25. 本轮执行记录（2026-04-06 / MET-TODO-016）

### 25.1 选中任务

1. 本轮任务：MET-TODO-016。
2. 选择原因：`MET-TODO-013` 明确依赖 `MET-TODO-016`，因此按 blocker-first/依赖先行规则，本轮先冻结 metrics 的最小配置模型与默认策略，为 reader interval / exporter timeout 等导出调度语义提供稳定输入。

### 25.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.9 已冻结 metrics 的默认配置表：`enabled=true`、`provider.type=internal`、`reader.interval_ms=5000`、`exporter.type=noop`、`exporter.timeout_ms=30000`、固定 allowlist 与默认 histogram buckets。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.7 已把 reader/exporter 放在 `AggregationEngine` 之后，说明 013/014 的实现必须先拿到稳定的 interval/exporter timeout/exporter type 默认值，而不能把这些值散落在 scheduler/exporter 私有常量中。
3. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.9 同时要求支持 default/Profile/部署/运行时 四层覆盖，因此 016 的最小实现应直接固化 merge 顺序，而不是提前接入 ConfigCenter 或 profile 文件解析。
4. infra/include/metrics/IMetricConfigPolicy.h 已在 `MET-TODO-004` 冻结 `validate_identity`、`normalize_labels`、`should_accept` 接口，说明本轮应在不改动公共边界的前提下新增 private `MetricsConfigPolicy` 实现类。
5. tests/unit/infra/MetricsConfigPolicyInterfaceTest.cpp 已把 `module/stage/profile/outcome` 必填、空 `error_code -> none` 归一化等接口语义固化，因此本轮私有实现必须与既有接口测试保持一致。

D 结论：

1. Design -> Build 映射：新增 private `MetricsConfigPolicy.h/.cpp`，实现 `MetricsConfigPatch`、`MetricsResolvedConfig`、`merge(default/profile/deploy/runtime)` 与 `validate_histogram_buckets`，并由 `MetricsConfigPolicy` 具体实现既有 `IMetricConfigPolicy`。
2. 最小冻结范围：本轮只冻结 `enabled/provider/exporter/reader_interval/exporter_timeout/labels/histogram_buckets/audit_on_policy_change` 这组 scheduler/exporter 即将消费的最小字段，不提前引入 profile 文档解析、ConfigCenter 订阅或动态回滚链路。
3. Build 三件套：
   - 代码目标：新增 infra/src/metrics/MetricsConfigPolicy.h、infra/src/metrics/MetricsConfigPolicy.cpp。
   - 测试目标：新增 tests/unit/infra/metrics/MetricsConfigMergeTest.cpp，并在 tests/unit/infra/CMakeLists.txt 注册 `MetricsConfigMergeTest`；同时保留 `MetricsConfigPolicyInterfaceTest` 做接口回归。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_metrics_config_merge_unit_test dasall_metrics_config_policy_interface_unit_test`、`ctest --test-dir build-ci -N -R "(MetricsConfigMergeTest|MetricsConfigPolicyInterfaceTest)"`、`ctest --test-dir build-ci --output-on-failure -R "(MetricsConfigMergeTest|MetricsConfigPolicyInterfaceTest)"`、`cmake --build build-ci --target dasall_unit_tests`、`ctest --test-dir build-ci --output-on-failure -L unit`。
4. D Gate：PASS。

### 25.3 Build 交付与证据

交付物：

1. infra/src/metrics/MetricsConfigPolicy.h：新增 `MetricsConfigPatch`、`MetricsResolvedConfig` 与 `MetricsConfigPolicy` 私有声明，冻结 metrics 配置模型、默认值与四层覆盖入口。
2. infra/src/metrics/MetricsConfigPolicy.cpp：新增 default/profile/deploy/runtime 覆盖顺序、固定 allowlist 校验、默认 histogram buckets 校验，以及与既有 `IMetricConfigPolicy` 一致的 identity/labels 接口实现。
3. tests/unit/infra/metrics/MetricsConfigMergeTest.cpp：覆盖 6.9 默认值、覆盖优先级与非单调 histogram bucket 拒绝三类断言。
4. tests/unit/infra/CMakeLists.txt：新增 `dasall_metrics_config_merge_unit_test` 与 `MetricsConfigMergeTest` 注册。

验收结果：

1. Build_CMakeTools：失败，报错“生成失败：无法配置项目”；按仓库既定回退策略改用 build-ci 命令链继续验收。
2. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
3. `cmake --build build-ci --target dasall_metrics_config_merge_unit_test dasall_metrics_config_policy_interface_unit_test`：通过；经二次收口后，本轮新增 `MetricsConfigPatch` 单测不再产生缺省字段初始化告警。
4. `ctest --test-dir build-ci -N -R "(MetricsConfigMergeTest|MetricsConfigPolicyInterfaceTest)"`：通过，发现 2 个测试：`MetricsConfigPolicyInterfaceTest`、`MetricsConfigMergeTest`。
5. `ctest --test-dir build-ci --output-on-failure -R "(MetricsConfigMergeTest|MetricsConfigPolicyInterfaceTest)"`：通过，2/2 tests passed。
6. `cmake --build build-ci --target dasall_unit_tests`：通过。
7. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，unit 标签 139/139 tests passed。

Build 合规复核：

1. 代码注释：配置 patch、resolved config 与 merge 顺序命名已直接表达语义，无需附加注释。
2. 正负例覆盖：本轮新增单测已覆盖默认值、覆盖优先级与桶单调性校验三类 016 验收点；既有 `MetricsConfigPolicyInterfaceTest` 继续作为接口边界回归。
3. 测试发现性：已用 `ctest -N -R "(MetricsConfigMergeTest|MetricsConfigPolicyInterfaceTest)"` 验证新旧两个配置相关单测均可发现。
4. TODO 证据回写：已回写任务状态、配置模型冻结范围、Build_CMakeTools 回退记录与 unit gate 结果。
5. 提交隔离：本轮提交范围限定为 MetricsConfigPolicy 私有源码、对应单测、CMake 注册、专项 TODO 与 worklog 证据更新。

## 26. 本轮执行记录（2026-04-06 / MET-TODO-013）

### 26.1 选中任务

1. 本轮任务：MET-TODO-013。
2. 可执行性依据：`MET-TODO-011` 已完成 AggregationEngine 聚合骨架，`MET-TODO-016` 已冻结 reader interval / exporter type / exporter timeout 等最小配置模型，因此本轮可以直接落 MetricReaderScheduler 的单工作线程调度骨架。

### 26.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.2 已冻结 MetricReaderScheduler 的职责为“周期读取快照并触发导出”。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.3 已明确其输入来自时间触发、输出去向为导出批次，且必须支持 flush/shutdown。
3. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.4 已给出依赖链 `AggregationEngine -> MetricReaderScheduler -> MetricsExporterAdapter`，说明本轮应先让 scheduler 对 `AggregationSnapshot` 形成 `MetricExportBatch` 队列，而不是提前把导出逻辑写进 scheduler 内部。
4. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.7 已冻结步骤 6：ReaderScheduler 周期触发导出批次，因此本轮最小闭环是“到点生成 batch / shutdown 强制 flush”。
5. `MET-TODO-016` 已提供 `MetricsResolvedConfig`，可直接为 scheduler 提供 `reader_interval_ms` 与 `exporter_type` 默认值，避免把这些配置散落在调度器私有常量中。

D 结论：

1. Design -> Build 映射：新增 private `MetricReaderScheduler.h/.cpp`，实现 `schedule_tick`、`flush_on_shutdown`、`pop_next_batch` 与批次队列观测面。
2. 首版策略：按专项 TODO 约束先采用单工作线程/单队列骨架，不提前扩张到线程池、queue overflow policy 或 exporter 重试；scheduler 只负责“何时出 batch”，不负责“如何导出 batch”。
3. Build 三件套：
   - 代码目标：新增 infra/src/metrics/MetricReaderScheduler.h、infra/src/metrics/MetricReaderScheduler.cpp。
   - 测试目标：新增 tests/unit/infra/metrics/MetricsReaderSchedulerTest.cpp，并在 tests/unit/infra/CMakeLists.txt 注册 `MetricsReaderSchedulerTest`；同时保留 `MetricsConfigMergeTest` 作为 016 依赖回归。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_metrics_reader_scheduler_unit_test dasall_metrics_config_merge_unit_test`、`ctest --test-dir build-ci -N -R "(MetricsReaderSchedulerTest|MetricsConfigMergeTest)"`、`ctest --test-dir build-ci --output-on-failure -R "(MetricsReaderSchedulerTest|MetricsConfigMergeTest)"`、`cmake --build build-ci --target dasall_unit_tests`、`ctest --test-dir build-ci --output-on-failure -L unit`。
4. D Gate：PASS。

### 26.3 Build 交付与证据

交付物：

1. infra/src/metrics/MetricReaderScheduler.h：新增 `MetricReaderTickResult` 与 `MetricReaderScheduler` 私有声明，冻结调度结果、pending queue、last batch 与 last tick 观测口。
2. infra/src/metrics/MetricReaderScheduler.cpp：新增首个 interval gating、shutdown 强制 flush、batch 入队与 `pop_next_batch` 语义，并统一复用 `MetricsErrors::ConfigInvalid` 处理非法时间戳或空批次请求。
3. tests/unit/infra/metrics/MetricsReaderSchedulerTest.cpp：覆盖周期触发与 shutdown flush 两条 013 验收路径，并通过 `MetricsConfigPolicy` 提供的 merged config 驱动 interval/exporter type。
4. tests/unit/infra/CMakeLists.txt：新增 `dasall_metrics_reader_scheduler_unit_test` 与 `MetricsReaderSchedulerTest` 注册。

验收结果：

1. Build_CMakeTools：失败，报错“生成失败：无法配置项目”；按仓库既定回退策略改用 build-ci 命令链继续验收。
2. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
3. `cmake --build build-ci --target dasall_metrics_reader_scheduler_unit_test dasall_metrics_config_merge_unit_test`：通过；仅出现仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是本轮新增问题。
4. `ctest --test-dir build-ci -N -R "(MetricsReaderSchedulerTest|MetricsConfigMergeTest)"`：通过，发现 2 个测试：`MetricsConfigMergeTest`、`MetricsReaderSchedulerTest`。
5. `ctest --test-dir build-ci --output-on-failure -R "(MetricsReaderSchedulerTest|MetricsConfigMergeTest)"`：通过，2/2 tests passed。
6. `cmake --build build-ci --target dasall_unit_tests`：通过。
7. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，unit 标签 140/140 tests passed。

Build 合规复核：

1. 代码注释：scheduler 结果对象、pending queue 与 flush 语义命名已直接表达意图，无需额外注释。
2. 正负例覆盖：本轮新增单测已覆盖 interval 未到不触发、到点触发 batch、shutdown 强制 flush 三条关键路径；`MetricsConfigMergeTest` 继续回归 016 配置输入。
3. 测试发现性：已用 `ctest -N -R "(MetricsReaderSchedulerTest|MetricsConfigMergeTest)"` 验证 scheduler 新测与配置依赖回归均可发现。
4. TODO 证据回写：已回写任务状态、单工作线程调度策略、Build_CMakeTools 回退记录与 unit gate 结果。
5. 提交隔离：本轮提交范围限定为 MetricReaderScheduler 私有源码、对应单测、CMake 注册、专项 TODO 与 worklog 证据更新。

## 27. 本轮执行记录（2026-04-06 / MET-TODO-014）

### 27.1 选中任务

1. 本轮任务：MET-TODO-014。
2. 可执行性依据：`MET-TODO-003`、`MET-TODO-007`、`MET-TODO-008`、`MET-TODO-013` 已完成，导出接口、快照对象、错误码域与 reader batch 队列均已具备，因此本轮可以直接落首版 `noop/prom_text` exporter 骨架。

### 27.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.2 已冻结 MetricsExporterAdapter 的职责为“可插拔导出器（noop/prometheus_text/otlp 预留）”。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.3 已明确 exporter 的输入是导出批次、输出去向为本地端点/远程出口，且导出失败必须可回退。
3. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.4 已给出 `AggregationEngine -> MetricReaderScheduler -> MetricsExporterAdapter` 依赖顺序，因此本轮应直接消费 `MetricReaderScheduler` 产出的 `MetricExportBatch`，而不是绕过 scheduler 重新造 batch。
4. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.8 已冻结导出异常的最小恢复动作：导出失败或超时要切回 degraded exporter（noop 或本地文本快照），但不能阻断主流程。
5. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.9 与专项 TODO 均已把 OTLP 标记为后置依赖，因此首版实现只应覆盖 `noop` 与 `prom_text`，对其他 exporter.type 明确失败并回退到 `noop`。

D 结论：

1. Design -> Build 映射：新增 private `MetricsExporterAdapter.h/.cpp`，实现 `export_batch(noop/prom_text)`、`fallback_to_noop`、`last_report()`、`module_snapshot()` 与 `last_rendered_text()`。
2. 主链接线：在单测中通过 `MetricReaderScheduler` 产出 `MetricExportBatch`，再交给 exporter 消费，验证 `scheduler -> exporter` 的首版串接，而不提前把 exporter 反向耦合进 scheduler。
3. Build 三件套：
   - 代码目标：新增 infra/src/metrics/MetricsExporterAdapter.h、infra/src/metrics/MetricsExporterAdapter.cpp。
   - 测试目标：新增 tests/unit/infra/metrics/MetricsExporterAdapterTest.cpp，并在 tests/unit/infra/CMakeLists.txt 注册 `MetricsExporterAdapterTest`；同时保留 `MetricsReaderSchedulerTest`、`MetricsConfigMergeTest` 作为依赖回归。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_metrics_exporter_adapter_unit_test dasall_metrics_reader_scheduler_unit_test dasall_metrics_config_merge_unit_test`、`ctest --test-dir build-ci -N -R "(MetricsExporterAdapterTest|MetricsReaderSchedulerTest|MetricsConfigMergeTest)"`、`ctest --test-dir build-ci --output-on-failure -R "(MetricsExporterAdapterTest|MetricsReaderSchedulerTest|MetricsConfigMergeTest)"`、`cmake --build build-ci --target dasall_unit_tests`、`ctest --test-dir build-ci --output-on-failure -L unit`。
4. D Gate：PASS。

### 27.3 Build 交付与证据

交付物：

1. infra/src/metrics/MetricsExporterAdapter.h：新增 exporter 状态、导出报告、成功/失败计数与 prom_text 文本观测面的私有声明。
2. infra/src/metrics/MetricsExporterAdapter.cpp：新增 `noop` 与 `prom_text` 导出实现、基于 sample_count 的首版 timeout 模拟、`unsupported exporter -> fallback_to_noop` 回退，以及 `export_failure_total`/`exporter_state` 的观测更新。
3. tests/unit/infra/metrics/MetricsExporterAdapterTest.cpp：覆盖从 `MetricReaderScheduler` 产出 batch 的 prom_text 成功路径、unsupported exporter 失败回退路径、timeout 路径三类断言。
4. tests/unit/infra/CMakeLists.txt：新增 `dasall_metrics_exporter_adapter_unit_test` 与 `MetricsExporterAdapterTest` 注册。

验收结果：

1. Build_CMakeTools：失败，报错“生成失败：无法配置项目”；按仓库既定回退策略改用 build-ci 命令链继续验收。
2. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
3. `cmake --build build-ci --target dasall_metrics_exporter_adapter_unit_test dasall_metrics_reader_scheduler_unit_test dasall_metrics_config_merge_unit_test`：首轮失败，定位为 `MetricsExporterAdapter.h` 缺失 `MetricsErrorCode` 声明来源；同轮补充 `#include "metrics/MetricsErrors.h"` 后重建通过。最终构建仅保留仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是本轮新增问题。
4. `ctest --test-dir build-ci -N -R "(MetricsExporterAdapterTest|MetricsReaderSchedulerTest|MetricsConfigMergeTest)"`：通过，发现 3 个测试：`MetricsConfigMergeTest`、`MetricsReaderSchedulerTest`、`MetricsExporterAdapterTest`。
5. `ctest --test-dir build-ci --output-on-failure -R "(MetricsExporterAdapterTest|MetricsReaderSchedulerTest|MetricsConfigMergeTest)"`：通过，3/3 tests passed。
6. `cmake --build build-ci --target dasall_unit_tests`：通过。
7. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，unit 标签 141/141 tests passed。

Build 合规复核：

1. 代码注释：exporter 状态、回退动作与导出报告命名已直接表达语义，无需额外注释。
2. 正负例覆盖：本轮新增单测已覆盖 prom_text 成功、unsupported exporter 失败回退、timeout 回退三条 014 验收路径；`MetricsReaderSchedulerTest` 与 `MetricsConfigMergeTest` 继续回归上游输入。
3. 测试发现性：已用 `ctest -N -R "(MetricsExporterAdapterTest|MetricsReaderSchedulerTest|MetricsConfigMergeTest)"` 验证 exporter 新测和依赖回归均可发现。
4. TODO 证据回写：已回写任务状态、同轮编译错误修复、Build_CMakeTools 回退记录与 unit gate 结果。
5. 提交隔离：本轮提交范围限定为 MetricsExporterAdapter 私有源码、对应单测、CMake 注册、专项 TODO 与 worklog 证据更新。

## 28. 本轮执行记录（2026-04-06 / MET-TODO-015）

### 28.1 选中任务

1. 本轮任务：MET-TODO-015。
2. 可执行性依据：`MET-TODO-008` 与 `MET-TODO-014` 已完成，导出失败错误码域和 exporter 的 degraded 信号均已具备；health/logging 的统一桥接接口虽未冻结，但本任务的最小解阻条件就是先输出 metrics 私有健康快照与日志钩子占位，因此无需转 Blocked。

### 28.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.8 已冻结 metrics 的恢复动作要求：导出失败进入 degraded，恢复成功后回清，并保留可观测证据。
2. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.10 已把 degraded_mode 纳入 metrics 健康快照最小集合，说明恢复骨架应直接消费并维护 `MetricsModuleSnapshot`。
3. infra/src/metrics/MetricsExporterAdapter.{h,cpp} 已在上一轮落下 `module_snapshot()`、`export_failure_total()` 与 fallback-to-noop 行为，可直接作为 015 的失败注入来源。
4. infra/src/logging/LoggingRecovery.{h,cpp} 已验证“私有恢复状态机 + 可替换 fallback/log hook”这种模式在 infra 子系统内可测、可回归，适合作为 metrics 恢复骨架的最小实现参考。
5. 当前 logging/audit bridge 接口尚未冻结，因此 015 不能直接耦合 `ILogger` 或健康总线，只能先产出 metrics 自有的恢复事件钩子占位。

外部参考：

1. OpenTelemetry 与常见 SRE 恢复实践都要求导出器故障与恢复状态可观测，且恢复判定应独立于 exporter 的瞬时失败结果，避免一次抖动就永久切换运行模式；据此本轮把 metrics recovery 收敛为“连续失败阈值 + 显式恢复回清”的私有状态机。

D 结论：

1. Design -> Build 映射：新增 private `MetricsRecovery.h/.cpp`，冻结 `MetricsRecoveryEvent`、`IMetricsRecoveryLogHook`、`observe_export_result`、`enter_degraded`、`recover_to_healthy` 与 `emit_recovery_event`。
2. 最小恢复策略：首版只消费 exporter 的 `MetricsOperationStatus` 与 `MetricsModuleSnapshot`，以连续失败阈值触发 degraded，并在成功导出后回清；不提前扩张到 health 总线、logging bridge 或 runtime 裁定链路。
3. Build 三件套：
   - 代码目标：新增 infra/src/metrics/MetricsRecovery.h、infra/src/metrics/MetricsRecovery.cpp。
   - 测试目标：新增 tests/unit/infra/metrics/MetricsRecoveryTest.cpp，并在 tests/unit/infra/CMakeLists.txt 注册 `MetricsRecoveryTest`。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_metrics_recovery_unit_test dasall_metrics_exporter_adapter_unit_test`、`ctest --test-dir build-ci -N -R "(MetricsRecoveryTest|MetricsExporterAdapterTest)"`、`ctest --test-dir build-ci --output-on-failure -R "(MetricsRecoveryTest|MetricsExporterAdapterTest)"`、`cmake --build build-ci --target dasall_unit_tests`、`ctest --test-dir build-ci --output-on-failure -L unit`。
4. D Gate：PASS。

### 28.3 Build 交付与证据

交付物：

1. infra/src/metrics/MetricsRecovery.h：新增 `MetricsRecoveryEvent`、`IMetricsRecoveryLogHook` 与 `MetricsRecovery` 私有声明，冻结连续失败计数、degraded 进入次数、恢复成功次数与模块快照观测口。
2. infra/src/metrics/MetricsRecovery.cpp：新增 exporter 结果观测、连续失败阈值判定、`enter_degraded`、`recover_to_healthy`、`emit_recovery_event` 与错误码推断逻辑。
3. tests/unit/infra/metrics/MetricsRecoveryTest.cpp：覆盖连续失败触发 degraded、成功导出回清 healthy，以及非法恢复输入拒绝三条 015 验收路径。
4. tests/unit/infra/CMakeLists.txt：新增 `dasall_metrics_recovery_unit_test` 与 `MetricsRecoveryTest` 注册，并保留 exporter/config 私有源码直编策略，等待 017 统一接入 `dasall_infra`。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_metrics_recovery_unit_test dasall_metrics_exporter_adapter_unit_test`：通过；仅出现仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是本轮新增问题。
3. `ctest --test-dir build-ci -N -R "(MetricsRecoveryTest|MetricsExporterAdapterTest)"`：通过，发现 2 个测试：`MetricsExporterAdapterTest`、`MetricsRecoveryTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "(MetricsRecoveryTest|MetricsExporterAdapterTest)"`：通过，2/2 tests passed。
5. `cmake --build build-ci --target dasall_unit_tests`：通过。
6. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，unit 标签 142/142 tests passed。

Build 合规复核：

1. 代码注释：恢复事件、日志钩子与状态机入口命名已直接表达 degraded/healthy 转换意图，无需补充重复性注释。
2. 正负例覆盖：本轮新增单测已覆盖连续失败降级、成功恢复回清与非法输入拒绝三条路径。
3. 测试发现性：已用 `ctest -N -R "(MetricsRecoveryTest|MetricsExporterAdapterTest)"` 验证恢复新测与 exporter 回归均可发现。
4. TODO 证据回写：已回写任务状态、Design -> Build 映射、定向与全量 unit 验证结果。
5. 提交隔离：本轮提交范围限定为 MetricsRecovery 私有源码、对应单测、CMake 注册、专项 TODO 与 worklog 证据更新。

## 29. 本轮执行记录（2026-04-06 / MET-TODO-017）

### 29.1 选中任务

1. 本轮任务：MET-TODO-017。
2. 可执行性依据：`MET-TODO-001~016` 已全部完成，metrics 私有运行时代码已落盘但仍停留在 tests 侧直编过渡形态；当前不存在要求先解的 Blocked 任务，适合先把源码正式接入 `dasall_infra`，为 018 的测试总表收口提供稳定链接面。

### 29.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 8.1 已给出 metrics 的落盘建议目录，明确 runtime 源码应归属于 infra/src/metrics 并由 infra CMake 统一管理。
2. infra/CMakeLists.txt 当前已采用“模块 sources + private headers 分组”模式管理 audit、health、logging、policy、secret 等私有实现，metrics 尚未入图，是当前唯一的例外路径。
3. `MET-TODO-009~015` 已实际产出 MetricsFacade、InstrumentRegistry、AggregationEngine、CardinalityGuard、MetricsConfigPolicy、MetricReaderScheduler、MetricsExporterAdapter 与 MetricsRecovery，这些源码不再是 placeholder，可一次性纳入 `dasall_infra`。
4. tests/unit/infra/CMakeLists.txt 目前仍通过直编私有源码维持 metrics 单测可执行，说明 017 的职责应是先让库目标入图，测试总表与去桥接工作留给 018 单独收口。

外部参考：

1. 常见 CMake 分层实践要求运行时代码优先归属于正式库目标，再由测试目标复用该库或在必要时做局部直编；据此本轮先解决“metrics 不在库图中”的根问题，而不把测试总表收口和 discoverability 修复混入同一提交。

D 结论：

1. Design -> Build 映射：更新 infra/CMakeLists.txt，新增 `DASALL_INFRA_METRICS_SOURCES` 与 `DASALL_INFRA_METRICS_PRIVATE_HEADERS`，并把全量 metrics runtime 源码正式纳入 `dasall_infra`。
2. 最小接线范围：本轮只调整 infra 库目标的源码图，不提前修改 tests/unit/CMakeLists.txt 的聚合注册，避免把 018 的测试入口收口与 017 的源码入图混为一轮。
3. Build 三件套：
   - 代码目标：更新 infra/CMakeLists.txt，把 metrics 私有运行时代码与头文件接入 `dasall_infra`。
   - 测试目标：验证 `dasall_metrics_recovery_unit_test` 在源码入图后仍可成功链接执行，作为 017 的最小 unit 链接证据。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_infra dasall_metrics_recovery_unit_test`、`ctest --test-dir build-ci --output-on-failure -R MetricsRecoveryTest`。
4. D Gate：PASS。

### 29.3 Build 交付与证据

交付物：

1. infra/CMakeLists.txt：新增 `DASALL_INFRA_METRICS_SOURCES` 与 `DASALL_INFRA_METRICS_PRIVATE_HEADERS`，将 MetricsFacade/Registry/Aggregation/Guard/Config/Scheduler/Exporter/Recovery 全量源码与私有头接入 `dasall_infra`。
2. build-ci 构建记录：确认 `dasall_infra` 现在会单独编译 metrics 源码对象，而不再只依赖 tests 侧直编过渡路径。
3. `dasall_metrics_recovery_unit_test` 链接回归：验证库目标入图后，现有 metrics 私有单测目标仍能成功链接并执行，为 018 的测试总表收口保留安全过渡面。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_metrics_recovery_unit_test`：通过；构建日志显示 `dasall_infra` 已开始单独编译 `src/metrics/*.cpp`，仅保留仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是本轮新增问题。
3. `ctest --test-dir build-ci --output-on-failure -R MetricsRecoveryTest`：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：本轮仅调整 CMake 源码图，不引入新的实现逻辑，无需新增注释。
2. 正负例覆盖：017 本质是构建接线任务，测试证据采用“库目标编译 + 现有 recovery 单测链接回归”的组合，不额外扩张到 018 的 discoverability 收口。
3. 测试发现性：本轮不修改测试总表，discoverability 与 contract 注册留待 018 统一完成，避免跨任务混改。
4. TODO 证据回写：已回写任务状态、metrics 源码入图范围与构建验证结果。
5. 提交隔离：本轮提交范围限定为 infra/CMakeLists.txt、专项 TODO 与 worklog 证据更新。

## 30. 本轮执行记录（2026-04-06 / MET-TODO-018）

### 30.1 选中任务

1. 本轮任务：MET-TODO-018。
2. 可执行性依据：`MET-TODO-017` 已完成，metrics runtime 源码已正式进入 `dasall_infra`，当前可以把 009~015 期间新增的 metrics 私有 unit 目标纳入顶层聚合清单，并为现有 metrics contract 测试补齐统一标签与发现性证据。

### 30.2 研究与 Design 结论

本地证据：

1. tests/unit/CMakeLists.txt 当前尚未把 `dasall_metrics_config_merge_unit_test`、`dasall_metrics_reader_scheduler_unit_test`、`dasall_metrics_exporter_adapter_unit_test`、`dasall_metrics_recovery_unit_test`、`dasall_metrics_facade_unit_test`、`dasall_instrument_registry_unit_test`、`dasall_metrics_cardinality_guard_unit_test`、`dasall_metrics_aggregation_unit_test` 纳入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`，导致 `dasall_unit_tests` 过去并不负责构建这些 metrics 私有目标。
2. tests/unit/infra/CMakeLists.txt 仍保留 metrics 私有单测对 `.cpp` 源码的直编桥接，而 `MET-TODO-017` 已让这些实现正式进入 `dasall_infra`，说明本轮可把单测切回“私有头 + 链接库目标”的更稳定形态。
3. tests/contract/CMakeLists.txt 中 metrics contract 测试虽然已注册，但缺少统一的 `metrics` 标签，不利于后续质量门和 discoverability 按模块过滤。
4. `ctest -N` 的 discoverability 门要求 metrics 新增 unit/contract 测试入口在统一清单中可见，因此 018 的职责是“入口总表 + 标签 + 聚合依赖”三者同步收口，而不是新增更多测试内容。

外部参考：

1. 常见 CMake 测试治理实践强调测试入口应通过少量稳定的聚合清单和标签管理，而不是依赖一次次手动定向构建；据此本轮将 metrics 测试入口收敛到顶层 unit 总表和 contract 标签体系中。

D 结论：

1. Design -> Build 映射：
   - 更新 tests/unit/CMakeLists.txt，新增 `DASALL_METRICS_UNIT_TEST_EXECUTABLE_TARGETS` 并纳入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`。
   - 更新 tests/unit/infra/CMakeLists.txt，把 metrics 私有 unit 目标从“直编私有 `.cpp`”收口为“包含私有头并链接 `dasall_infra`”，同时补齐 `metrics`/`failure` 标签。
   - 更新 tests/contract/CMakeLists.txt，新增 `dasall_register_metrics_contract_test`，给 metrics contract 测试统一打上 `metrics` 标签，并给错误映射测试补充 `failure` 标签。
2. 最小收口范围：本轮只处理测试入口和标签，不新增新的 metrics 业务测试文件，也不推进 integration 用例落盘。
3. Build 三件套：
   - 代码目标：更新 tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt。
   - 测试目标：让 metrics 私有 unit 目标进入 `dasall_unit_tests` 依赖图，并让 metrics contract 测试在统一标签体系中可过滤可发现。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`、`ctest --test-dir build-ci -N -R "(MetricsFacadeTest|InstrumentRegistryTest|MetricsCardinalityGuardTest|MetricsAggregationTest|MetricsConfigMergeTest|MetricsReaderSchedulerTest|MetricsExporterAdapterTest|MetricsRecoveryTest|MetricsProviderInterfaceBoundaryContractTest|MetricsConfigPolicyInterfaceBoundaryContractTest|MetricsErrorMappingContractTest|MetricsExporterInterfaceBoundaryContractTest|MetricsMeterInterfaceBoundaryContractTest)"`、`ctest --test-dir build-ci --output-on-failure -L unit`、`ctest --test-dir build-ci --output-on-failure -L contract`。
4. D Gate：PASS。

### 30.3 Build 交付与证据

交付物：

1. tests/unit/CMakeLists.txt：新增 `DASALL_METRICS_UNIT_TEST_EXECUTABLE_TARGETS`，把 metrics 接口与私有 runtime 单测统一纳入 `dasall_unit_tests` 顶层依赖清单。
2. tests/unit/infra/CMakeLists.txt：移除 metrics 私有 unit 目标对 runtime `.cpp` 的直编桥接，改为统一链接 `dasall_infra`，并为 metrics/failure 增加标签。
3. tests/contract/CMakeLists.txt：新增 `dasall_register_metrics_contract_test`，为 5 个 metrics contract 测试统一打上 `contract;smoke;metrics` 标签，并为 `MetricsErrorMappingContractTest` 增加 `failure` 标签。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过；构建日志显示 `dasall_metrics_config_merge_unit_test`、`dasall_metrics_reader_scheduler_unit_test`、`dasall_metrics_exporter_adapter_unit_test`、`dasall_metrics_recovery_unit_test`、`dasall_metrics_facade_unit_test`、`dasall_instrument_registry_unit_test`、`dasall_metrics_cardinality_guard_unit_test`、`dasall_metrics_aggregation_unit_test` 均已被顶层 unit 聚合目标链接。
3. `ctest --test-dir build-ci -N -R "(MetricsFacadeTest|InstrumentRegistryTest|MetricsCardinalityGuardTest|MetricsAggregationTest|MetricsConfigMergeTest|MetricsReaderSchedulerTest|MetricsExporterAdapterTest|MetricsRecoveryTest|MetricsProviderInterfaceBoundaryContractTest|MetricsConfigPolicyInterfaceBoundaryContractTest|MetricsErrorMappingContractTest|MetricsExporterInterfaceBoundaryContractTest|MetricsMeterInterfaceBoundaryContractTest)"`：通过，发现 13 个 metrics 相关 unit/contract 测试入口。
4. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，unit 标签 142/142 tests passed；标签摘要中 `metrics=8 tests`、`failure=3 tests`。
5. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，contract 标签 140/140 tests passed；标签摘要中 `metrics=5 tests`、`failure=1 test`。

Build 合规复核：

1. 代码注释：本轮只调整 CMake 测试入口与标签，不引入新的实现逻辑，无需新增注释。
2. 正负例覆盖：018 不新增测试逻辑，但通过 `failure` 标签显式标注了 metrics 的失败路径测试入口，保留既有正负例矩阵。
3. 测试发现性：已用 `ctest -N -R ...` 回填 13 个 metrics 相关 unit/contract 测试入口的统一 discoverability 证据。
4. TODO 证据回写：已回写任务状态、聚合清单收口、contract 标签与 unit/contract 门禁结果。
5. 提交隔离：本轮提交范围限定为 tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt、专项 TODO 与 worklog 证据更新。

## 31. 本轮执行记录（2026-04-06 / MET-TODO-019）

### 31.1 选中任务

1. 本轮任务：MET-TODO-019。
2. 可执行性依据：按 blocker-first 先核对 `MET-BLK-002` 与 `MET-BLK-004`；当前仓库已存在 `infra/include/audit/IAuditLogger.h`、`infra/include/audit/AuditTypes.h`、`infra/include/logging/ILogger.h` 与 `infra/include/LogEvent.h`，最小 audit/logging 写入接口与字段边界已经冻结，因此 019 无需再等待外部设计补齐，可直接进入 bridge 落盘。

### 31.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_metrics模块详细设计.md 6.10 已冻结 metrics 的日志/审计观测面：需记录配置变更、连续导出失败触发 degraded 与恢复 healthy 事件。
2. infra/src/metrics/MetricsRecovery.{h,cpp} 已在 `MET-TODO-015` 落下 `IMetricsRecoveryLogHook` 占位，说明本轮应把该占位对接到真实的 logging bridge，而不是继续停留在测试桩。
3. infra/include/audit/IAuditLogger.h 与 infra/include/audit/AuditTypes.h 已冻结 `write_audit(const AuditEvent&, const AuditContext&)`、`AuditEvent`、`AuditContext` 与 `AuditWriteOutcome`，满足 metrics audit bridge 的最小落盘前提。
4. infra/include/logging/ILogger.h 与 infra/include/LogEvent.h 已冻结 `log(const LogEvent&)` 与结构化 `LogEvent` 载荷，满足 metrics logging bridge 的最小落盘前提。
5. infra/src/secret/SecretAuditBridge.cpp 与 infra/src/policy/PolicyAuditBridge.cpp 已验证“bridge 仅依赖稳定 sink 接口 + 本地状态缓存”的实现模式，适合作为 metrics 审计桥接的最小参考；infra/src/logging/LoggingMetricsBridge.cpp 已验证“冻结标签/字段边界 + best-effort 降级”的桥接模式。

外部参考：

1. 常见 SRE/OTel 实践要求恢复与配置治理事件保持 best-effort 外部观测，但不得反向阻断主业务链路；据此本轮把 metrics bridge 固定为“记录治理事件，不接管恢复裁定”。

D 结论：

1. Design -> Build 映射：新增 private `MetricsBridgeEvent.h` 统一承载 recovery/config 治理事件；新增 private `MetricsLoggingBridge.h/.cpp` 与 `MetricsAuditBridge.h/.cpp`，分别对接 `ILogger` 与 `IAuditLogger`。
2. logging bridge 策略：`MetricsLoggingBridge` 实现 `IMetricsRecoveryLogHook`，把 recovery 事件转换为结构化 `LogEvent`，并坚持 best-effort 语义，即 sink 失败只在 bridge 本地记账，不反向打断 `MetricsRecovery`。
3. audit bridge 策略：`MetricsAuditBridge` 把 recovery/config 治理事件转换为 `AuditEvent` + `AuditContext`，证据引用统一落在既有 `ToolResult` reference class 内，不新增 contracts 类型。
4. Build 三件套：
   - 代码目标：新增 infra/src/metrics/MetricsBridgeEvent.h、MetricsLoggingBridge.{h,cpp}、MetricsAuditBridge.{h,cpp}，并更新 infra/CMakeLists.txt。
   - 测试目标：新增 tests/unit/infra/metrics/MetricsLoggingBridgeTest.cpp、MetricsAuditBridgeTest.cpp 与 tests/contract/smoke/MetricsAuditBridgeBoundaryContractTest.cpp，并更新 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt。
   - 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_infra dasall_metrics_logging_bridge_unit_test dasall_metrics_audit_bridge_unit_test dasall_contract_metrics_audit_bridge_boundary_test`、`ctest --test-dir build-ci -N -R "(MetricsLoggingBridgeTest|MetricsAuditBridgeTest|MetricsAuditBridgeBoundaryContractTest)"`、`ctest --test-dir build-ci --output-on-failure -R "(MetricsLoggingBridgeTest|MetricsAuditBridgeTest|MetricsAuditBridgeBoundaryContractTest)"`、`cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`、`ctest --test-dir build-ci --output-on-failure -L unit`、`ctest --test-dir build-ci --output-on-failure -L contract`。
5. D Gate：PASS。

### 31.3 Build 交付与证据

交付物：

1. infra/src/metrics/MetricsBridgeEvent.h：新增 `MetricsBridgeEventKind`、`MetricsBridgeEventOutcome`、`MetricsBridgeContext` 与 `MetricsBridgeEvent`，冻结 recovery/config 治理事件的最小字段约束。
2. infra/src/metrics/MetricsLoggingBridge.{h,cpp}：新增结构化日志桥接，实现 `write_log_event()` 与 `IMetricsRecoveryLogHook::write_recovery_event()`，并保留 best-effort 本地状态记录。
3. infra/src/metrics/MetricsAuditBridge.{h,cpp}：新增治理事件审计桥接，实现 `write_audit_event()` / `write_recovery_event()`，把 metrics recovery/config 事件收敛到 `AuditEvent` 与 `AuditContext`。
4. tests/unit/infra/metrics/MetricsLoggingBridgeTest.cpp：覆盖 recovery hook 可达、无效 payload 拒绝、logger sink 失败仍保持 best-effort 三条路径。
5. tests/unit/infra/metrics/MetricsAuditBridgeTest.cpp：覆盖 recovery 审计 payload 完整性与 audit sink 失败状态保留。
6. tests/contract/smoke/MetricsAuditBridgeBoundaryContractTest.cpp：固化 metrics 审计桥接只使用既有 `AuditEvent/AuditContext` 边界，不把 request/trace 等相关字段泄漏到 `side_effects`。
7. infra/tests CMake：将新的 metrics bridge 源码、unit 测试与 contract 测试接入 `dasall_infra`、`dasall_unit_tests`、`dasall_contract_tests` 聚合。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_metrics_logging_bridge_unit_test dasall_metrics_audit_bridge_unit_test dasall_contract_metrics_audit_bridge_boundary_test`：通过；仅保留仓库既有 `IMetricsProvider.h` 缺省初始化告警，不是本轮新增问题。
3. `ctest --test-dir build-ci -N -R "(MetricsLoggingBridgeTest|MetricsAuditBridgeTest|MetricsAuditBridgeBoundaryContractTest)"`：通过，发现 3 个新增 metrics bridge 测试入口。
4. `ctest --test-dir build-ci --output-on-failure -R "(MetricsLoggingBridgeTest|MetricsAuditBridgeTest|MetricsAuditBridgeBoundaryContractTest)"`：通过，3/3 tests passed。
5. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过；顶层构建日志显示 `dasall_metrics_logging_bridge_unit_test`、`dasall_metrics_audit_bridge_unit_test` 与 `dasall_contract_metrics_audit_bridge_boundary_test` 已进入聚合目标。
6. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，unit 标签 144/144 tests passed，标签摘要中 `metrics=10 tests`、`failure=5 tests`。
7. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，contract 标签 141/141 tests passed，标签摘要中 `metrics=6 tests`、`failure=1 test`。

Build 合规复核：

1. 代码注释：本轮 bridge 结构体、状态对象与字段命名已直接表达 recovery/config 治理语义，无需补充重复性注释。
2. 正负例覆盖：unit 已覆盖 recovery hook 正例、bridge payload guard、sink failure best-effort；contract 已覆盖 audit payload 边界与相关字段收敛。
3. blocker-first：`MET-BLK-002` 与 `MET-BLK-004` 经当前仓库公共接口复核后同轮解阻，并已在第 8 节阻塞表回写证据，不再作为 019 前置阻塞。
4. 测试发现性：新增 3 个 metrics bridge 测试入口已通过 `ctest -N -R ...` 回填发现性证据，并纳入顶层 unit/contract 聚合。
5. 提交隔离：本轮提交范围限定为 metrics bridge 私有源码、对应 unit/contract 测试、CMake 注册、专项 TODO 与 worklog 证据更新。