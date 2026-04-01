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
15. 当前代码现状：infra/CMakeLists.txt、infra/src/placeholder.cpp、infra/include/、infra/src/metrics/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt

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
| infra/include/ | 目录为空 | metrics 接口与对象尚未冻结 |
| infra/CMakeLists.txt | 仅包含 src/placeholder.cpp | metrics 尚未接入构建目标 |
| tests/CMakeLists.txt | 仅接入 mocks/unit/contract | integration 顶层未接线 |
| tests/unit/CMakeLists.txt | 未接入 infra 子目录 | metrics unit 发现性缺失 |
| tests/contract/CMakeLists.txt | centralized registration 已存在 | 可复用承载 metrics contract 边界测试 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：本轮可生成 L3/L2 混合专项 TODO。接口、对象、错误语义、主/异常流程可细化到 L3；跨子域桥接、integration 拓扑、OTLP 依赖属于外部约束，需降为 L2/L1 或 Blocked。

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
| CMake 与测试门禁 | metrics 设计 7/8/9；当前代码现状 | 测试/门禁 | MET-TODO-017、MET-TODO-018、MET-BLK-001 | 构建接线、unit/contract 先行，integration 解阻后推进 |
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
| 测试与门禁类任务 | 是 | MET-TODO-017~018（含阻塞 MET-BLK-001） |
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
| MET-TODO-008 | Not Started | 定义 MetricsErrors 错误码域 | metrics 设计 6.6；工程规范 3.6 | 6.6 错误语义 | L3 | infra/include/metrics/MetricsErrors.h | MET_E_PROVIDER_NOT_READY...MET_E_CONFIG_INVALID | contract：映射 contracts::ResultCode；unit：枚举稳定性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | MET-TODO-001 | contracts 映射矩阵未成文 | 在 contract 测试固化映射矩阵 | 错误码头文件、映射测试 | 仅当 7 个错误码均有来源锚点且映射测试通过时完成 |
| MET-TODO-009 | Not Started | 实现 MetricsFacade 初始化与写入骨架 | metrics 设计 6.2/6.7 | 6.2 MetricsFacade；6.7 步骤 1/2 | L3 | infra/src/metrics/MetricsFacade.cpp | init/get_meter/record 入口骨架 | unit：未初始化/已初始化两路径；failure：非法 identity 路径 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-001、MET-TODO-002、MET-TODO-006、MET-TODO-008 | 无 | 无 | Facade 骨架、单测 | 仅当初始化状态机与错误码路径可二值判定时完成 |
| MET-TODO-010 | Not Started | 实现 InstrumentRegistry 唯一性管理骨架 | metrics 设计 6.2/6.3 | 6.2 InstrumentRegistry；6.3 同名同语义唯一 | L3 | infra/src/metrics/InstrumentRegistry.cpp | register_identity/find_identity | unit：同名冲突与重复注册路径 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-006、MET-TODO-008 | 无 | 无 | Registry 骨架、单测 | 仅当重复注册冲突返回可判定错误并可观测时完成 |
| MET-TODO-011 | Not Started | 实现 AggregationEngine 聚合骨架 | metrics 设计 6.2/6.7 | 6.2 AggregationEngine；6.7 步骤 5 | L3 | infra/src/metrics/AggregationEngine.cpp | aggregate_counter, aggregate_gauge, aggregate_histogram, snapshot | unit：Counter/Gauge/Histogram 聚合断言 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-006、MET-TODO-007、MET-TODO-010 | 并发参数未冻结 | 先落单线程可测实现 | 聚合骨架、单测 | 仅当三类聚合行为均可重复验证时完成 |
| MET-TODO-012 | Not Started | 实现 CardinalityGuard 标签治理骨架 | metrics 设计 6.2/6.3/6.8/6.9 | 6.2 CardinalityGuard；6.8 标签异常；6.9 allowlist | L3 | infra/src/metrics/CardinalityGuard.cpp | validate_labels, reject_with_reason | unit：allowlist/超阈值拒绝；failure：reject_total 可观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-004、MET-TODO-006、MET-TODO-008 | 标签 taxonomy 全局评审未完成 | 先冻结 module/stage/profile/outcome/error_code | Guard 骨架、单测 | 仅当未知标签与高基数路径均可二值判定时完成 |
| MET-TODO-013 | Not Started | 实现 MetricReaderScheduler 调度骨架 | metrics 设计 6.2/6.7/6.9 | 6.2 MetricReaderScheduler；6.7 步骤 6；6.9 interval 配置 | L2 | infra/src/metrics/MetricReaderScheduler.cpp | schedule_tick, flush_on_shutdown | unit：周期触发与 shutdown flush 行为 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-011、MET-TODO-016 | 线程模型细节未冻结 | 首版采用单工作线程策略 | 调度骨架、单测 | 仅当 tick 触发与 shutdown flush 均可稳定复现时完成 |
| MET-TODO-014 | Not Started | 实现 MetricsExporterAdapter 首版导出骨架 | metrics 设计 6.2/6.7/6.8/6.9 | 6.2 MetricsExporterAdapter；6.8 导出异常；6.9 exporter.type | L2 | infra/src/metrics/MetricsExporterAdapter.cpp | export_batch(noop/prom_text), fallback_to_noop | unit：导出成功/失败/超时；failure：export_failure_total 可观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-003、MET-TODO-007、MET-TODO-008、MET-TODO-013 | OTLP 依赖未冻结 | 首版仅 noop/prom_text，OTLP 后置评审 | 导出骨架、单测 | 仅当成功/失败/超时三路径均有可观测结果且不阻塞主流程时完成 |
| MET-TODO-015 | Not Started | 实现 MetricsRecovery 降级与恢复骨架 | metrics 设计 6.8/6.10；工程规范 3.6 | 6.8 恢复动作；6.10 degraded_mode | L2 | infra/src/metrics/MetricsRecovery.cpp | enter_degraded, recover_to_healthy, emit_recovery_event | failure-injection：连续失败触发降级与恢复回清 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-008、MET-TODO-014 | health/logging 接口签名未统一 | 先输出 metrics 私有健康快照与日志钩子占位 | 恢复骨架、故障注入测试 | 仅当降级进入/退出两路径均可二值判定时完成 |
| MET-TODO-016 | Not Started | 定义 MetricsConfigPolicy 配置模型与默认策略 | metrics 设计 6.9；架构 7.5.1 | 6.9 配置项表 | L3 | infra/src/metrics/MetricsConfigPolicy.cpp | merge(default/profile/deploy/runtime), validate_histogram_buckets | unit：默认值、覆盖优先级、桶单调性校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | MET-TODO-004、MET-TODO-006、MET-TODO-008 | Profile 键一致性未冻结 | 先冻结最小键集合：enabled/exporter/interval/labels | 配置策略实现、单测 | 仅当覆盖顺序与默认值与 6.9 完全一致时完成 |
| MET-TODO-017 | Not Started | 注册 metrics 代码到 infra CMake | metrics 设计 8.1；代码现状 | 8.1 文件落盘建议 | L2 | infra/CMakeLists.txt, infra/include/metrics/, infra/src/metrics/ | 将 metrics 源码与头文件纳入 dasall_infra | build：dasall_infra 可编译；unit：新增目标可链接 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | MET-TODO-001~MET-TODO-016 | 初期源文件可能为空 | 保留最小 non-empty 源文件并分批接线 | CMake 改动、构建记录 | 仅当 placeholder 不再是唯一源码且 metrics 文件入图时完成 |
| MET-TODO-018 | Not Started | 注册 metrics 的 unit 与 contract 测试入口 | metrics 设计 7/8/9；工程规范 3.7 | 7 映射、8.1 目录、9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt, tests/unit/infra/metrics/, tests/contract/CMakeLists.txt | 新增 metrics 相关 unit/contract/failure 测试注册 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | MET-TODO-017 | integration 顶层未接线 | integration 相关验收延后到 MET-BLK-001 解阻后 | 测试代码、注册入口、执行记录 | 仅当 metrics 新增测试在 ctest -N 可见并执行通过时完成 |
| MET-TODO-019 | Blocked | 接线 MetricsAuditBridge 与 MetricsLoggingBridge 骨架 | metrics 设计 6.2/6.10 | 6.2 Bridge 组件；6.10 审计/日志事件 | L1 | infra/src/metrics/MetricsAuditBridge.cpp, infra/src/metrics/MetricsLoggingBridge.cpp | bridge_write_audit_event, bridge_write_log_event | contract：审计字段完整；unit：桥接调用可达 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci --output-on-failure -L contract | MET-TODO-007、MET-TODO-008、MET-TODO-015 | MET-BLK-002、MET-BLK-004 | 冻结 audit/logging 最小写入接口与字段约束 | 桥接代码或阻塞记录 | 仅当外部接口冻结后，状态才可从 Blocked 改为 Not Started |
| MET-TODO-020 | Not Started | 回写 metrics 质量门与交付证据 | metrics 设计 9.2/11；工程规范 6.2 | 9.2 Gate 建议；11 风险与回退 | L2 | docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md | Gate 执行结果、阻塞变化、回退记录回写 | process test：门禁记录可追溯 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | MET-TODO-018 | 无 | 无 | 更新后的 TODO 证据段 | 仅当每个门禁项都有通过/失败结论与证据命令时完成 |

### 6.2 Blocked 任务索引

| 任务 ID | 对应阻塞项 |
|---|---|
| MET-TODO-019 | MET-BLK-002、MET-BLK-004 |

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

| Gate ID | 门禁项 | 通过标准 | 失败处置 |
|---|---|---|---|
| MET-GATE-01 | 接口冻结门 | IMetricsProvider/IMeter/IMetricExporter/MetricTypes/MetricsErrors 全部落盘并编译通过 | 回退到接口定义阶段，不推进实现 |
| MET-GATE-02 | 主链路闭环门 | record -> aggregate -> snapshot 单测通过 | 回退 MET-TODO-009~011 |
| MET-GATE-03 | 异常可观测门 | label reject/queue full/export failure/config invalid 均有错误码与观测输出 | 补齐失败路径后再提测 |
| MET-GATE-04 | 构建接线门 | dasall_infra 构建通过且 metrics 源码入图 | 修复 CMake 接线 |
| MET-GATE-05 | 测试发现性门 | ctest -N 可见 metrics 新增 unit/contract 用例 | 修复 tests 注册 |
| MET-GATE-06 | breaking 评审门 | 任何接口签名或错误码映射变化都有评审结论 | 未评审不得合入 |
| MET-GATE-07 | integration 准入门 | tests 顶层接入 integration 子目录并定义标签规范 | 未通过前禁止推进 metrics integration 验收 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| MET-BLK-001 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；metrics integration 是否可执行改由组件自身落盘负责 | 后续 metrics integration 任务 | 无；后续仅需按组件落盘 integration/failure 用例 | 证据回链到 infra 专项 TODO 的 INF-BLK-06 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt | 若 tests 顶层 integration 接线或聚合依赖回退，则重新转为 Blocked |
| MET-BLK-002 | audit 子域写入接口未冻结，MetricsAuditBridge 无法稳定落盘 | MET-TODO-019 | audit 侧冻结最小写入接口与事件字段约束 | 在 infra/audit 或 logging 设计补桥接接口章节 | 暂时仅记录本地故障日志，不写审计管线 |
| MET-BLK-003 | Profile 中 metrics 配置键尚未统一，跨档位覆盖规则不稳定 | MET-TODO-016 | 冻结 enabled/exporter/interval/labels/queue/buckets 键集合 | 先冻结最小键集合并在 profile 文档回写 | 暂时禁用运行时动态覆盖 |
| MET-BLK-004 | logging 子域错误日志写入接口未冻结，MetricsLoggingBridge 无法稳定接线 | MET-TODO-019 | logging 侧冻结最小写入接口与字段要求 | 在 logging 设计补 bridge 接口段并评审 | 暂时仅保留 metrics 内部计数与快照 |
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
   - 当前阻塞集中在跨子域桥接、integration 拓扑与 OTLP 依赖，不阻断 metrics 本地闭环落地。
3. 当前最小可执行粒度：函数/接口/数据结构。
4. 若未达到全域函数级的缺失信息：
   - logging/audit/health 的最小桥接接口签名。
   - tests integration 顶层接线与标签规范。
   - OTLP exporter 依赖与构建策略冻结结论。
5. 下一步建议：
   - 先执行 MET-TODO-001~018，完成 metrics 本地闭环与构建/测试门禁。
   - 并行推进 MET-BLK-001~005 解阻。
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