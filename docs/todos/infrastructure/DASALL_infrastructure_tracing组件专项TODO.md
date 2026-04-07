# DASALL infrastructure 子系统 tracing 组件专项 TODO

最近更新时间：2026-03-25  
阶段：Detailed Design -> Special TODO  
适用范围：infra/tracing

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_infra_tracing模块详细设计.md
2. docs/architecture/DASALL_infrastructure子系统详细设计.md
3. docs/architecture/DASSALL_Agent_architecture.md
4. docs/architecture/DASALL_Engineering_Blueprint.md
5. docs/adr/ADR-005-architecture-review-baseline.md
6. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
7. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
8. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
9. docs/plans/DASALL_工程落地实现步骤指引.md
10. docs/development/DASALL_工程协作与编码规范.md
11. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md
12. docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md
13. 当前代码现状：infra/CMakeLists.txt、infra/include/、infra/src/tracing/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt

生成原则：

1. 不改写 ADR-005/006/007/008 冻结结论。
2. 不越过 infrastructure/tracing 边界扩张到无关子系统。
3. 每个任务必须包含代码目标、测试目标、验收命令。
4. 设计证据不足处必须标注 Blocked，并给出最小解阻动作。

## 2. 子系统目标与范围

### 2.1 tracing 组件目标

1. 提供统一追踪入口与 Span 生命周期管理。
2. 提供上下文传播（inject/extract）与 trace_id/span_id 关联能力。
3. 提供采样、批处理导出、force_flush/shutdown 语义。
4. 导出失败、队列满、上下文异常时可观测并可降级。

### 2.2 范围边界

纳入范围：

1. tracing 对外接口、核心对象、错误码域。
2. provider/tracer/span/pipeline/buffer/exporter 的最小可实现链路。
3. tracing 的 CMake 接线与 unit/contract/failure 测试门禁。
4. 与 infra/logging、infra/metrics、infra/audit 的 tracing 侧桥接点。

不纳入范围：

1. runtime 主控状态机、恢复裁定、调度策略。
2. contracts 公共语义对象反向扩写。
3. 全量 OTel SDK 深绑定与远程采样服务（仅预留接口，不默认推进）。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 tracing TODO 的影响 |
|---|---|---|---|---|
| TRC-C001 | 架构 3.4.7/5.10；tracing 设计 2.1 | Must | infra 必须提供 trace 能力并与 logging/metrics/audit 协同 | 任务必须覆盖主链路、桥接与可观测 |
| TRC-C002 | 架构 3.7；蓝图 4.2 | Must | 依赖方向单向，infra 不反向依赖业务实现 | 代码目标仅限 infra/tests/docs 路径 |
| TRC-C003 | 蓝图 4.2/4.3 | Must-Not | 禁止 infra -> runtime/cognition/tools/memory 实现依赖；跨模块调用走稳定接口 | 任务禁止直接 include 上层实现类型 |
| TRC-C004 | ADR-006 | Must-Not | tracing 不接管上下文语义装配与 Prompt 渲染 | ContextPropagation 仅做标识注入/提取 |
| TRC-C005 | ADR-007 | Must-Not | tracing 不做失败语义判定与恢复准入 | 仅记录失败证据并输出降级状态 |
| TRC-C006 | ADR-008 | Must | tracing 不拥有调度权，仅记录 orchestrator/coordinator/worker 链路 | 审计与追踪任务只做证据记录 |
| TRC-C007 | contracts 冻结计划；tracing 设计 5.3/6.5 | Must-Not | 不把采样器、批处理、导出协议策略写入 contracts | 所有实现策略留在 infra 私有对象 |
| TRC-C008 | 编码规范 3.6 | Must | 错误不可吞没，失败必须可观测 | 错误码、指标、审计必须绑定失败路径 |
| TRC-C009 | 编码规范 3.7 | Should | 新增公共接口同步新增 unit/integration/contract 测试 | 每个接口任务必须绑定测试目标 |
| TRC-C010 | tracing 设计 6.9 | Must | 配置支持默认/Profile/部署/运行时覆盖 | 配置任务必须明确层级覆盖与回退策略 |
| TRC-C011 | OTel Trace API/SDK（设计 2.1/4.3） | Should | Span 生命周期、采样、ForceFlush/Shutdown、并发语义与行业标准对齐 | L3 任务优先对齐 provider/tracer/span 语义 |
| TRC-C012 | 工程落地步骤指引 阶段 C | Must | 先底座后能力，且每阶段必须可测试 | 执行顺序需先接口对象，再导出与降级，再门禁 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/src/tracing/ | 空目录 | tracing 组件尚未落盘实现 |
| infra/include/ | 已形成“根目录共享契约 + 组件目录公共接口”布局，tracing/ 子目录已落盘接口与对象 | tracing public headers 与对象已冻结，后续差距集中在运行时实现 |
| infra/CMakeLists.txt | 已接入 core/audit/plugin/tracing 等真实源码 | tracing 接口与锚点已接入构建，具体 tracing 服务实现仍未接入构建目标 |
| tests/CMakeLists.txt | 已接入 mocks/unit/contract/integration 并提供 dasall_integration_tests 聚合入口 | integration 拓扑已接入顶层，后续只需补 tracing 具体集成/故障用例 |
| tests/unit/CMakeLists.txt | 无 infra 子目录 | tracing unit 发现性待补 |
| tests/contract/CMakeLists.txt | 已有 centralized registration 机制 | 可承载 tracing-contract 边界测试 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：本轮可生成 L3/L2 混合专项 TODO，其中接口语义、核心对象、主流程与错误路径可细化到 L3；跨子系统桥接与集成门禁受外部设计/拓扑约束，维持 L2/L1。

支撑证据：

1. 已有明确核心接口清单：ITracerProvider、ITracer、ISpan、ITraceContextPropagator、ISpanExporter、ITraceSampler（tracing 设计 6.6）。
2. 已有核心对象字段：TraceContext、SpanDescriptor、SpanEndResult、SamplingDecision、ExportBatchReport、TraceModuleSnapshot（tracing 设计 6.5）。
3. 已有主流程与异常流程：正常流程 7 步、异常分类 4 类与恢复动作（tracing 设计 6.7/6.8）。
4. 已有错误码域：TRC_E_PROVIDER_NOT_READY、TRC_E_INVALID_CONTEXT、TRC_E_QUEUE_FULL、TRC_E_EXPORT_TIMEOUT、TRC_E_EXPORT_FAILURE、TRC_E_SHUTDOWN_TIMEOUT、TRC_E_CONFIG_INVALID（tracing 设计 6.6）。
5. 已有落盘建议与测试出口：infra/include/tracing、infra/src/tracing、tests/unit/infra/tracing、tests/integration/infra/tracing（tracing 设计 8.1、7）。
6. 但 tracing 组件 integration/failure 用例尚未落盘、metrics/audit 适配接口未冻结，导致部分任务仍必须 Blocked。

当前最小可执行粒度：函数/接口/数据结构级（以 L3 为主，局部受阻降为 L2/L1）。

### 4.2 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| ITracerProvider | tracing 设计 6.6 | L3 | 方法名、职责、flush/shutdown 语义完整 | 返回状态对象细节未与 contracts 映射成文 | 直接拆接口冻结 + 最小实现 |
| ITracer | tracing 设计 6.6/6.7 | L3 | start_span/with_active_span/current_context 语义完整 | with_active_span 回调异常封装策略 | 直接拆接口 + 生命周期测试 |
| ISpan | tracing 设计 6.6/6.5 | L3 | set_attribute/add_event/set_status/end/get_context 语义完整 | attrs value 类型约束细则 | 直接拆接口 + Span 行为测试 |
| ITraceContextPropagator | tracing 设计 6.6/6.3 | L3 | inject/extract 明确，invalid/noop 语义明确 | carrier 统一容器类型未冻结 | 先冻结接口，carrier 以前置声明占位 |
| TraceContext | tracing 设计 6.5 | L3 | 字段与 invalid/noop 约束明确 | trace_state 编码规范细节缺失 | 直接拆数据结构 |
| SamplingPolicyEngine | tracing 设计 6.2/6.3/6.9 | L3 | 采样模式、输入输出、配置键完整 | 远程采样不在首版范围 | 直接拆函数级任务（本地采样） |
| SpanProcessorPipeline | tracing 设计 6.2/6.3/6.7 | L2 | OnStart/OnEnd 管线职责、非阻塞约束明确 | 处理链插件注册机制未展开 | 先做单链路实现，插件化后置 |
| BatchSpanBuffer | tracing 设计 6.2/6.8/6.9 | L3 | queue/批量/延迟/溢出策略完整 | 线程模型参数细节缺失 | 直接拆队列策略任务 |
| SpanExporterAdapter | tracing 设计 6.2/6.3/6.9 | L2 | noop/file/otlp 预留与失败语义明确 | OTLP 首版是否启用未冻结 | 先做 noop/file，OTLP 标记 Blocked |
| TraceMetricsBridge | tracing 设计 6.2/6.10 | L1 | 指标名清单完整 | metrics 接口签名未冻结 | 标记 Blocked，先补桥接接口设计 |
| TraceAuditBridge | tracing 设计 6.2/6.10 | L1 | 审计事件清单明确 | audit 写入接口签名未冻结 | 标记 Blocked，先补桥接接口设计 |
| tests/integration tracing 注册点 | tracing 设计 8.1/9.1；tests 现状 | L0 | 设计建议存在，且 tests 顶层 integration 拓扑已接入 | tracing integration/failure 用例尚未落盘 | 直接拆组件集成任务 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| 对外接口层 ITracerProvider/ITracer/ISpan/Propagator | tracing 设计 6.6 | 接口 | TRC-TODO-001、TRC-TODO-002、TRC-TODO-003、TRC-TODO-004 | 先冻结调用面，阻止上层直连实现细节 |
| 核心对象与错误码域 | tracing 设计 6.5/6.6 | 数据结构/错误处理 | TRC-TODO-005、TRC-TODO-006、TRC-TODO-007 | 先固化语义对象与错误码，再推进实现 |
| 生命周期主链路 | tracing 设计 6.7 | 流程 | TRC-TODO-008、TRC-TODO-009 | provider/tracing/span 分拆成单目标任务 |
| 上下文传播 | tracing 设计 6.3/6.6/6.8 | 适配器 | TRC-TODO-010 | 聚焦 inject/extract 与 invalid/noop 语义 |
| 采样策略 | tracing 设计 6.2/6.3/6.9 | 流程/配置 | TRC-TODO-011 | 先落本地采样策略，不推进远程采样 |
| 批处理导出管线 | tracing 设计 6.2/6.7/6.8/6.9 | 流程 | TRC-TODO-012、TRC-TODO-013 | queue 与 exporter 分任务，避免过载 |
| 异常降级与可观测 | tracing 设计 6.8/6.10 | 错误处理/门禁 | TRC-TODO-014、TRC-TODO-015 | 失败注入与降级路径独立验收 |
| 配置与 Profile 裁剪 | tracing 设计 6.9；蓝图 5 | 配置 | TRC-TODO-016 | 键名、默认值、覆盖层级明确落盘 |
| CMake 与测试门禁 | tracing 设计 7/8/9；当前代码现状 | 测试/门禁 | TRC-TODO-017、TRC-TODO-018 | 构建接线与 unit/contract 可先做，integration 用例待组件后续落盘 |
| metrics/audit 桥接 | tracing 设计 6.10 | 桥接/门禁 | TRC-BLK-001、TRC-BLK-002 | 外部接口未冻结，必须先补设计 |
| OTLP 导出 | tracing 设计 6.9/7/11 | 导出 | TRC-BLK-004 | 首版导出器选型未冻结，不默认推进 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| TRC-TODO-001 | Done | 定义 ITracerProvider 接口头文件 | tracing 设计 6.6；编码规范 3.7 | 6.6 ITracerProvider | L3 | infra/include/tracing/ITracerProvider.h | init(config), get_tracer(scope), force_flush(timeout_ms), shutdown(timeout_ms) | unit：接口可编译；contract：错误码映射入口可对接 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | 无 | 无 | 接口头文件、编译证据；2026-04-01 已新增 infra/include/tracing/ITracerProvider.h，并更新 infra/src/tracing/TracingModuleAnchor.cpp 显式包含该头文件，使 dasall_infra 构建路径可直接编译 provider 接口冻结面 | 仅当方法签名与 6.6 一致且不引入业务实现依赖时完成 |
| TRC-TODO-002 | Done | 定义 ITracer 接口头文件 | tracing 设计 6.6/6.7 | 6.6 ITracer；6.7 主流程 | L3 | infra/include/tracing/ITracer.h | start_span(descriptor,parent), with_active_span(span,fn), current_context() | unit：生命周期调用入口可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | TRC-TODO-001 | 无 | 无 | 接口头文件、编译证据；2026-04-01 已新增 infra/include/tracing/ITracer.h，并更新 infra/src/tracing/TracingModuleAnchor.cpp 显式包含 ITracerProvider/ITracer 两个 tracing 入口头文件，确认 dasall_infra 可直接编译 tracer 生命周期调用面 | 仅当接口方法能完整支撑 6.7 正常流程时完成 |
| TRC-TODO-003 | Done | 定义 ISpan 接口头文件 | tracing 设计 6.6/6.5 | 6.6 ISpan；6.5 SpanEndResult | L3 | infra/include/tracing/ISpan.h | set_attribute, add_event, set_status, end, get_context | unit：end 后不可写入语义校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | TRC-TODO-002 | attrs value 类型边界未成文 | 先按基础标量+字符串冻结首版 | 接口头文件、行为单测；2026-04-01 已新增 infra/include/tracing/ISpan.h，以 bool/int64/uint64/double/string 五类基础标量+字符串承载首版属性值，并新增 tests/unit/infra/tracing/SpanInterfaceTest.cpp 与对应 unit CMake 注册，验证 end 后写入不再生效 | 仅当 end 后写入被拒绝且测试可判定时完成 |
| TRC-TODO-004 | Done | 定义 ITraceContextPropagator 接口头文件 | tracing 设计 6.6/6.3 | 6.6 Propagator；6.3 输入输出契约 | L3 | infra/include/tracing/ITraceContextPropagator.h | inject(context,carrier), extract(carrier) | unit：invalid/noop context 处理 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | TRC-TODO-005 | carrier 容器类型未冻结 | 首版使用键值 carrier 抽象占位 | 接口头文件、单测；2026-04-01 已新增 infra/include/tracing/ITraceContextPropagator.h，复用 TraceTypes.h 中的键值 TraceCarrier 抽象，并新增 tests/unit/infra/tracing/TraceContextPropagatorInterfaceTest.cpp 与对应 unit CMake 注册，验证 valid round-trip、empty carrier -> noop、invalid traceparent -> invalid 三条路径 | 仅当 invalid/noop 语义可被测试验证时完成 |
| TRC-TODO-005 | Done | 定义 TraceTypes 数据结构 | tracing 设计 6.5 | 6.5 TraceContext/SpanDescriptor/SpanEndResult/SamplingDecision/ExportBatchReport/TraceModuleSnapshot | L3 | infra/include/tracing/TraceTypes.h | 上述 6 类核心对象 | unit：字段完整性与默认语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | 无 | 无 | 无 | 数据结构头文件、单测；2026-04-01 已新增 infra/include/tracing/TraceTypes.h，补齐 TracerScope/TraceOperationStatus 与 6 类核心 tracing 对象，并新增 tests/unit/infra/tracing/TraceTypesTest.cpp 及对应 unit CMake 注册，确保当前任务具备可执行的 unit 验收出口 | 仅当对象字段与 6.5 对齐且默认语义可二值判定时完成 |
| TRC-TODO-006 | Done | 定义 TraceErrors 错误码域 | tracing 设计 6.6；编码规范 3.6 | 6.6 错误语义 | L3 | infra/include/tracing/TraceErrors.h | TRC_E_PROVIDER_NOT_READY...TRC_E_CONFIG_INVALID | unit：错误码稳定；contract：映射 contracts::ResultCode 不越权 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | TRC-TODO-001 | 与 contracts 映射矩阵未成文 | 在 contract 测试中固化映射矩阵 | 错误码头文件、映射测试；2026-04-01 已新增 infra/include/tracing/TraceErrors.h，冻结 7 个 TRC_E_* 私有错误码及其 contracts::ResultCode 映射，并新增 tests/unit/infra/tracing/TraceErrorsTest.cpp 与 tests/contract/smoke/TraceErrorMappingContractTest.cpp 及对应 CMake 注册，固化错误码数值、命名空间与 source anchor 映射矩阵 | 仅当 7 个错误码均可追溯且映射测试通过时完成 |
| TRC-TODO-007 | Done | 注册 tracing 头文件到 infra 公开包含路径 | tracing 设计 8.1；infra 现状 | 8.1 落盘建议 | L2 | infra/CMakeLists.txt, infra/include/tracing/ | tracing 头文件目录与目标接线 | build：dasall_infra 可见 tracing 头文件 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | TRC-TODO-001~TRC-TODO-006 | 无 | 无 | CMake 改动、构建记录；2026-04-01 已将 ITracerProvider/ITracer/ISpan/ITraceContextPropagator/TraceTypes/TraceErrors 六个 tracing 冻结头文件统一加入 infra/CMakeLists.txt 的 DASALL_INFRA_PUBLIC_HEADERS，保持外部模块通过 tracing/ 前缀消费这些公开头文件时与 dasall_infra 导出面一致 | 仅当外部模块可包含 tracing 头文件且构建通过时完成 |
| TRC-TODO-008 | Done | 实现 TracerProviderImpl 生命周期骨架 | tracing 设计 6.2/6.6/6.7 | 6.2 TracerProviderImpl；6.7 正常流程步骤 1/7 | L3 | infra/src/tracing/TracerProviderImpl.cpp | init/get_tracer/force_flush/shutdown 骨架 | unit：未初始化、已初始化、shutdown 超时三路径 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | TRC-TODO-001、TRC-TODO-005、TRC-TODO-006 | 无 | 无 | Provider 骨架实现、单测；2026-04-06 已新增 infra/src/tracing/TracerProviderImpl.{h,cpp} 与 tests/unit/infra/tracing/TracerProviderImplTest.cpp，并更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt 使 provider 实现与单测入图。为解决 ITracerProvider::init(const TraceConfig&) 仅有前置声明的同轮 blocker，本轮仅在私有实现头中补齐最小 TraceConfig 占位（enabled/provider_type/force_flush_on_stop），不提前展开 TRC-TODO-016 的公开配置模型；新增单测验证未初始化 provider-not-ready、已初始化 tracer cache + force_flush、shutdown(0) -> TRC_E_SHUTDOWN_TIMEOUT 三条路径。验证记录：ctest --test-dir build-ci -N -R TracerProviderImplTest 可发现 1 个新用例，ctest --test-dir build-ci --output-on-failure -R TracerProviderImplTest 通过，ctest --test-dir build-ci --output-on-failure -L unit 145/145 通过（仅保留仓库既有 IMetricsProvider.h 缺省初始化告警） | 仅当三类状态路径可二值判定并返回明确错误码时完成 |
| TRC-TODO-009 | Done | 实现 TracerImpl 与 SpanImpl 生命周期闭环 | tracing 设计 6.2/6.7 | 6.2 TracerImpl/SpanImpl；6.7 步骤 2/3/4 | L3 | infra/src/tracing/TracerImpl.cpp, infra/src/tracing/SpanImpl.cpp | start_span/end/get_context/set_status | unit：span 生命周期、parent-child 关系 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | TRC-TODO-002、TRC-TODO-003、TRC-TODO-008 | 无 | 无 | Tracer/Span 实现、单测；2026-04-06 已新增 infra/src/tracing/TracerImpl.{h,cpp}、infra/src/tracing/SpanImpl.{h,cpp} 与 tests/unit/infra/tracing/TracerSpanLifecycleTest.cpp，并更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt 使 tracer/span 实现与单测入图。同时将 TracerProviderImpl 的 scope cache 从 NoopTracer 占位切换为真实 TracerImpl，以贯通 provider -> tracer -> span 的最小生命周期主链。新增单测验证 root span 新 trace_id、active scope 内 child span 继承 parent trace_id/span_id、with_active_span 嵌套恢复、end 后 terminal state 冻结与 Ok 状态优先级。验证记录：ctest --test-dir build-ci -N -R TracerSpanLifecycleTest 可发现 1 个新用例，ctest --test-dir build-ci --output-on-failure -R "TracerProviderImplTest|TracerSpanLifecycleTest" 通过，ctest --test-dir build-ci --output-on-failure -L unit 146/146 通过（未引入新增告警） | 仅当 start/end/context 与 parent-child 断言全部通过时完成 |
| TRC-TODO-010 | Done | 实现 ContextPropagationAdapter 注入提取 | tracing 设计 6.2/6.3/6.8 | 6.2 ContextPropagationAdapter；6.8 上下文异常恢复 | L3 | infra/src/tracing/ContextPropagationAdapter.cpp | inject/extract + invalid context fallback | unit：valid/invalid/noop 三路径 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | TRC-TODO-004、TRC-TODO-005、TRC-TODO-006 | carrier 跨线程协议未冻结 | 首版仅支持 in-process carrier | 适配器实现、单测；2026-04-06 已新增 infra/src/tracing/ContextPropagationAdapter.{h,cpp} 与 tests/unit/infra/tracing/ContextPropagationAdapterTest.cpp，并更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt 使 propagation 实现与单测入图。实现按 W3C Trace Context 00 版本格式解析 `traceparent`，支持 `traceparent`/`tracestate` 的小写注入、大小写不敏感读取、missing header -> noop、malformed traceparent -> invalid + TRC_E_INVALID_CONTEXT 计数，且无 `traceparent` 时会丢弃孤立 `tracestate`。验证记录：ctest --test-dir build-ci -N -R ContextPropagationAdapterTest 可发现 1 个新用例，ctest --test-dir build-ci --output-on-failure -R "TracerProviderImplTest|TracerSpanLifecycleTest|ContextPropagationAdapterTest" 通过，ctest --test-dir build-ci --output-on-failure -L unit 147/147 通过（未引入新增告警） | 仅当 invalid context 能稳定回退并记录 TRC_E_INVALID_CONTEXT 时完成 |
| TRC-TODO-011 | Done | 实现 SamplingPolicyEngine 本地采样策略 | tracing 设计 6.2/6.3/6.9 | 6.2 SamplingPolicyEngine；6.9 sampler 配置 | L3 | infra/src/tracing/SamplingPolicyEngine.cpp | always_on/off、parent_based、ratio | unit：四类采样策略决策 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | TRC-TODO-005、TRC-TODO-016 | 远程采样服务未冻结 | 远程采样延后，不阻塞本地采样 | 采样引擎实现、单测；2026-04-06 已新增 infra/src/tracing/SamplingPolicyEngine.{h,cpp}，实现 `always_on`、`always_off`、`parent_based_always_on`、`ratio` 四类本地采样决策，并按 trace_id 后缀构造确定性 ratio 判定。同步更新 infra/src/tracing/TracerImpl.{h,cpp}、SpanImpl.{h,cpp}、TracerProviderImpl.cpp，把采样决策接入现有 provider -> tracer -> span 主链：start_span 先基于 resolved parent/trace_id 求采样结果，再写入 sampled trace flag，并让 Drop 决策保留有效 unsampled TraceContext 但停止属性/事件/状态记录，为后续 012/013 的 buffer/export 过滤面做准备。新增 tests/unit/infra/tracing/SamplingPolicyTest.cpp，并更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt 使采样代码与单测入图。验证记录：ctest --test-dir build-ci -N -R "SamplingPolicyTest|TracerProviderImplTest|TracerSpanLifecycleTest|ContextPropagationAdapterTest" 可发现 4 个 tracing 相关用例，ctest --test-dir build-ci --output-on-failure -R "SamplingPolicyTest|TracerProviderImplTest|TracerSpanLifecycleTest|ContextPropagationAdapterTest" 通过，ctest --test-dir build-ci --output-on-failure -L unit 149/149 通过（未引入新增告警） | 仅当四类策略决策结果可重复验证时完成 |
| TRC-TODO-012 | Done | 实现 BatchSpanBuffer 队列与导出触发 | tracing 设计 6.2/6.7/6.8/6.9；docs/development/InfraConcurrencyPolicy.md | 6.2 BatchSpanBuffer；6.8 队列异常；6.9 overflow policy | L3 | infra/src/tracing/BatchSpanBuffer.cpp | enqueue/dequeue/schedule/overflow(block or drop_oldest) | unit：队列满策略；failure：dropped_total 可观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | TRC-TODO-005、TRC-TODO-006、TRC-TODO-011 | 线程池参数细节未冻结 | 首版按 InfraConcurrencyPolicy 默认采用 drop_oldest，并在 lock order 固定前禁止持 L2 锁执行 exporter I/O | 队列实现、策略测试；2026-04-06 已新增 infra/src/tracing/BatchSpanBuffer.{h,cpp}，实现 ended recording span 的 `enqueue()`、`dequeue_batch()`、`force_flush()`、`export_trigger()` 与 `should_export_now()`；首版按 tracing 默认 `drop_oldest` / 可选 `block` 两类 overflow policy 工作，并基于 oldest pending span 的 end_ts + schedule_delay_ms 触发定时导出。同步更新 infra/src/tracing/SpanImpl.{h,cpp}，补齐 `attributes()`、`end_result()` 等只读访问面，供后续 013 pipeline/exporter 直接消费已结束 span 数据而不再返工。新增 tests/unit/infra/tracing/BatchSpanBufferTest.cpp，并更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt 使 buffer 代码与 failure/unit 测试入图。验证记录：ctest --test-dir build-ci -N -R "BatchSpanBufferTest|SamplingPolicyTest|TracerSpanLifecycleTest|ContextPropagationAdapterTest" 可发现 4 个 tracing 相关用例，ctest --test-dir build-ci --output-on-failure -R "BatchSpanBufferTest|SamplingPolicyTest|TracerSpanLifecycleTest|ContextPropagationAdapterTest" 通过，ctest --test-dir build-ci --output-on-failure -L unit 150/150 通过（未引入新增告警） | 仅当 block/drop_oldest 两种策略均可二值判定时完成 |
| TRC-TODO-013 | Done | 实现 SpanProcessorPipeline 与 ExporterAdapter 首版 | tracing 设计 6.2/6.3/6.7/6.9 | 6.2 Pipeline/Exporter；6.7 步骤 4/5/6；6.9 exporter.type | L2 | infra/src/tracing/SpanProcessorPipeline.cpp, infra/src/tracing/SpanExporterAdapter.cpp | ended span -> batch -> export（noop/file） | unit：导出成功/失败路径；failure：export timeout/failure | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | TRC-TODO-012 | OTLP 是否首版启用未冻结 | 首版仅 noop/file，OTLP 走阻塞项 | pipeline/exporter 实现、测试；2026-04-06 已新增 infra/src/tracing/SpanProcessorPipeline.{h,cpp} 与 infra/src/tracing/SpanExporterAdapter.{h,cpp}，把 provider -> tracer -> span.end -> batch/export 主链闭环接通：TracerProviderImpl 在 init() 时持有共享 pipeline，get_tracer() 生成的 TracerImpl 会把 pipeline end-hook 注入新建 SpanImpl，使 ended sampled span 在 `end()` 首次完成时自动进入 `SpanProcessorPipeline::on_end()`。首版 pipeline 负责过滤 non-recording span、调用 BatchSpanBuffer 执行 enqueue/threshold/schedule/force_flush，并保证 exporter 调用发生在 queue drain 之后而非 L2 队列持有期内。首版 exporter 支持 `noop` 与 `file` 两类导出：`noop` 只更新批次报告与快照计数，`file` 生成可测试的 line-oriented rendered output；`otlp` 在本轮按阻塞项处理为可观测 ExportFailure，并自动 fallback 到 noop 以避免继续阻塞 hot path。同步更新 infra/src/tracing/TracerProviderImpl.{h,cpp}、TracerImpl.{h,cpp}、SpanImpl.{h,cpp}，补齐 pipeline/exporter 状态访问面、span end hook 与 `descriptor()` 只读访问面；新增 tests/unit/infra/tracing/BatchExportTest.cpp，并更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt 使 pipeline/exporter 代码与新单测入图。验证记录：ctest --test-dir build-ci -N -R "BatchExportTest|BatchSpanBufferTest|SamplingPolicyTest|TracerProviderImplTest|TracerSpanLifecycleTest|ContextPropagationAdapterTest" 可发现 6 个 tracing 相关用例，ctest --test-dir build-ci --output-on-failure -R "BatchExportTest|BatchSpanBufferTest|SamplingPolicyTest|TracerProviderImplTest|TracerSpanLifecycleTest|ContextPropagationAdapterTest" 通过，ctest --test-dir build-ci --output-on-failure -L unit 151/151 通过（未引入新增告警） | 仅当导出成功/失败路径均有可观测结果且不阻塞 hot path 时完成 |
| TRC-TODO-014 | Done | 实现 TraceHealthProbe 降级与恢复判定骨架 | tracing 设计 6.2/6.8/6.10 | 6.2 TraceHealthProbe；6.8 连续失败降级；6.10 健康快照 | L2 | infra/src/tracing/TraceHealthProbe.cpp | degraded_mode 切换与快照输出 | failure：连续失败触发降级、恢复回清 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | TRC-TODO-006、TRC-TODO-013 | health 统一接口未冻结 | 先输出 tracing 私有快照对象 | 降级实现、故障注入测试；2026-04-07 已新增 docs/todos/infrastructure/deliverables/TRC-TODO-014-TraceHealthProbe骨架收敛.md，完成 014 的 D Gate 收敛：明确本轮不接统一 health 注册接口，而是先输出 tracing 私有 `TraceHealthSnapshot`，并把 exporter 事实快照 `TraceModuleSnapshot` 与健康判定位 `degraded_mode` 显式拆开。同步新增 infra/src/tracing/TraceHealthProbe.{h,cpp}，落盘 `observe_result()`、`enter_degraded()`、`recover_to_healthy()` 与基于 `TraceOperationStatus + TraceModuleSnapshot` 的连续失败阈值判定；首版固定连续失败阈值为 2，在 tracing 配置未冻结 `health.*` 键前不扩张配置面。更新 infra/src/tracing/SpanProcessorPipeline.{h,cpp}，把 health probe 作为 pipeline 私有成员，在 `on_end()`、`export_batch()`、`force_flush()`、`shutdown()` 后以 best-effort 方式观察主链状态，确保 health 判定不反向覆盖 tracing 主结果；更新 infra/src/tracing/TracerProviderImpl.{h,cpp} 暴露 `health_snapshot()` 只读出口供后续 015/统一 health 接口收敛前消费。新增 tests/unit/infra/tracing/TraceHealthProbeTest.cpp，并更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt 使 014 新增实现与 failure/unit 测试入图。验证记录：ctest --test-dir build-ci -N -R "TraceHealthProbeTest|BatchExportTest|TracerProviderImplTest|BatchSpanBufferTest|SamplingPolicyTest|TracerSpanLifecycleTest|ContextPropagationAdapterTest" 可发现 7 个 tracing 相关用例，ctest --test-dir build-ci --output-on-failure -R "TraceHealthProbeTest|BatchExportTest|TracerProviderImplTest|BatchSpanBufferTest|SamplingPolicyTest|TracerSpanLifecycleTest|ContextPropagationAdapterTest" 通过，ctest --test-dir build-ci --output-on-failure -L unit 152/152 通过（未引入新增告警）。任务选择说明：TRC-TODO-016 已在 2026-04-06 完成并处于 Done，本轮按 project-implementation-cycle 仅执行当前唯一未完成且依赖满足的原子任务 TRC-TODO-014 | 仅当降级进入/退出均可被测试稳定判定时完成 |
| TRC-TODO-015 | Done | 实现 TraceMetricsBridge 与 TraceAuditBridge 桥接骨架 | tracing 设计 6.2/6.10 | 6.2 Bridge 组件；6.10 指标/审计清单 | L1 | infra/src/tracing/TraceMetricsBridge.cpp, infra/src/tracing/TraceAuditBridge.cpp | 指标与审计桥接写入 | unit+contract+runtime regression：provider/pipeline 状态变化可实际驱动 bridge 且边界字段完整 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_tracer_provider_impl_unit_test dasall_batch_export_unit_test dasall_trace_health_probe_unit_test dasall_trace_metrics_bridge_unit_test dasall_trace_audit_bridge_unit_test dasall_contract_trace_metrics_bridge_boundary_test dasall_contract_trace_audit_bridge_boundary_test && ctest --test-dir build-ci --output-on-failure -R 'TracerProviderImplTest|BatchExportTest|TraceHealthProbeTest|TraceMetricsBridgeTest|TraceAuditBridgeTest|TraceMetricsBridgeBoundaryContractTest|TraceAuditBridgeBoundaryContractTest' && ctest --test-dir build-ci --output-on-failure -L tracing | TRC-TODO-005、TRC-TODO-006 | 无 | 当前仓库尚无 tests/integration/infra/tracing 拓扑，runtime wiring 先由 tracing 专项 unit+contract gate 收口，integration 子拓扑后续单列原子任务推进 | 桥接代码、runtime wiring、测试与构建记录；2026-04-07 首轮已新增 infra/src/tracing/TraceMetricsBridge.{h,cpp} 与 TraceAuditBridge.{h,cpp}，冻结 8 个 tracing 指标族（`trace_span_started_total`、`trace_span_ended_total`、`trace_span_dropped_total`、`trace_export_success_total`、`trace_export_failure_total`、`trace_export_latency_ms`、`trace_batch_queue_depth`、`trace_context_invalid_total`）和 3 类治理审计事件（采样策略变更、导出 degraded 转迁、shutdown fallback），并新增 tests/unit/infra/tracing/TraceMetricsBridgeTest.cpp、TraceAuditBridgeTest.cpp、tests/contract/smoke/TraceMetricsBridgeBoundaryContractTest.cpp、TraceAuditBridgeBoundaryContractTest.cpp 及对应 CMake 注册；同日已进一步更新 infra/src/tracing/SpanProcessorPipeline.{h,cpp}、TracerProviderImpl.{h,cpp}、TraceMetricsBridge.{h,cpp} 与 tests/unit/infra/tracing/TracerProviderImplTest.cpp、BatchExportTest.cpp，使 provider/pipeline 的 sampler/export/health/shutdown 状态变化实际驱动 TraceMetricsBridge 与 TraceAuditBridge。验证记录：受影响目标构建通过，`ctest --test-dir build-ci --output-on-failure -R 'TracerProviderImplTest|BatchExportTest|TraceHealthProbeTest|TraceMetricsBridgeTest|TraceAuditBridgeTest|TraceMetricsBridgeBoundaryContractTest|TraceAuditBridgeBoundaryContractTest'` 7/7 通过，`ctest --test-dir build-ci --output-on-failure -L tracing` 17/17 通过 | 仅当 8 个 tracing 指标族与 3 类治理审计事件具备 bridge skeleton 与 runtime wiring 双重验证且 tracing gate 通过时完成 |
| TRC-TODO-016 | Done | 定义 tracing 配置模型与默认策略 | tracing 设计 6.9；蓝图 5 | 6.9 配置项表 | L3 | infra/include/tracing/TraceConfig.h | tracing.enabled/provider/sampler/batch/exporter/overflow/force_flush_on_stop | unit：默认值与覆盖优先级校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | TRC-TODO-005 | 无 | 无 | 配置模型、单测；2026-04-06 已新增 infra/include/tracing/TraceConfig.h，公开冻结 tracing provider/sampler/batch/exporter/overflow/force_flush_on_stop 配置模型，并补齐 TraceConfigPatch 与 default -> profile -> deploy -> runtime 覆盖语义；同时更新 infra/include/tracing/ITracerProvider.h、infra/src/tracing/TracerProviderImpl.{h,cpp} 收口掉 008 同轮引入的私有最小 TraceConfig，占位配置不再滞留在实现层。新增 tests/unit/infra/tracing/TraceConfigTest.cpp，并更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt 使公开配置头与单测入图。验证记录：ctest --test-dir build-ci -N -R "TraceConfigTest|TracerProviderImplTest|TracerSpanLifecycleTest" 可发现 3 个 tracing 相关用例，ctest --test-dir build-ci --output-on-failure -R "TraceConfigTest|TracerProviderImplTest|TracerSpanLifecycleTest" 通过，ctest --test-dir build-ci --output-on-failure -L unit 148/148 通过（未引入新增告警） | 仅当配置键名与默认值与 6.9 表一致且覆盖顺序可测试时完成 |
| TRC-TODO-017 | Done | 注册 tracing 源码到 infra CMake | tracing 设计 8.1；infra 现状 | 8.1 文件落盘建议 | L2 | infra/CMakeLists.txt, infra/src/tracing/ | tracing 源文件纳入 dasall_infra | build：dasall_infra 持续可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | TRC-TODO-008~TRC-TODO-014、TRC-TODO-016 | 无 | 无 | CMake 接线、构建记录；2026-04-07 已核查 infra/CMakeLists.txt 中 `DASALL_INFRA_TRACING_SOURCES` 已收口 TracingModuleAnchor、TracerProviderImpl、SamplingPolicyEngine、TracerImpl、SpanImpl、ContextPropagationAdapter、BatchSpanBuffer、SpanExporterAdapter、SpanProcessorPipeline、TraceHealthProbe 等 tracing 源文件，placeholder 不再是唯一编译源；验证记录：`cmake --build build-ci --target dasall_infra` 通过 | 仅当 placeholder 不再是唯一编译源且 tracing 源文件入图时完成 |
| TRC-TODO-018 | Done | 注册 tracing 的 unit 与 contract 测试入口 | tracing 设计 7/8/9；编码规范 3.7 | 7 映射表、8.1 目录建议、9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt, tests/unit/infra/tracing/, tests/contract/CMakeLists.txt | 新增 tracing 标签测试注册 | unit+contract：ctest 可发现并执行 tracing 新增用例 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | TRC-TODO-017 | 无 | integration 相关验收按组件用例落盘推进 | 测试代码、注册改动、执行记录；2026-04-07 已在 tests/unit/infra/CMakeLists.txt 为 TraceTypes/SpanInterface/TraceContextPropagatorInterface/TraceErrors/TracerProviderImpl/TracerSpanLifecycle/ContextPropagationAdapter 等 tracing unit 用例补齐 `unit;tracing` 标签，并在 tests/contract/CMakeLists.txt 新增 `dasall_register_tracing_contract_test` 收口 tracing contract 标签族，使 `TraceErrorMappingContractTest` 进入 `contract;smoke;tracing;failure`；验证记录：`ctest --test-dir build-ci -N -L tracing` 可发现 13 个 tracing 用例，`ctest --test-dir build-ci --output-on-failure -L tracing` 通过，`ctest --test-dir build-ci --output-on-failure -L unit` 152/152 通过，`ctest --test-dir build-ci --output-on-failure -L contract` 141/141 通过 | 仅当 tracing 测试在 ctest -N 可见并执行通过时完成 |

### 6.2 Blocked 任务与阻塞索引

| 任务 ID | 对应阻塞项 |
|---|---|

## 7. 执行顺序建议

### 7.1 顺序与并行段（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 接口与对象冻结 | TRC-TODO-001~TRC-TODO-007 | 可并行（接口组与对象组并行） | 先固定边界，降低实现返工 |
| B 生命周期与上下文闭环 | TRC-TODO-008~TRC-TODO-010 | 串行 | provider -> tracer/span -> propagation |
| C 采样与导出闭环 | TRC-TODO-011~TRC-TODO-013 | 串行 | sampler -> buffer -> pipeline/exporter |
| D 降级与配置 | TRC-TODO-014、TRC-TODO-016 | 可并行 | 降级逻辑与配置模型可并行推进 |
| E 构建与测试接线 | TRC-TODO-017、TRC-TODO-018 | 可并行 | CMake 接线与测试注册同步推进 |
| F 解阻后桥接 | TRC-TODO-015 | 串行 | 依赖 metrics/audit 接口冻结 |

### 7.2 必过门禁

| Gate ID | 门禁项 | 通过标准 | 失败处置 |
|---|---|---|---|
| TRC-GATE-01 | 接口冻结门 | ITracerProvider/ITracer/ISpan/TraceTypes/TraceErrors 全部落盘并编译通过 | 回退到接口定义阶段，不推进实现 |
| TRC-GATE-02 | 生命周期闭环门 | start/end/context 主链路单测通过 | 回退到 TracerImpl/SpanImpl |
| TRC-GATE-03 | 异常可观测门 | invalid context/queue full/export failure/shutdown timeout 均有错误码与可观测输出 | 补齐失败路径后再提测 |
| TRC-GATE-04 | 构建接线门 | dasall_infra 构建通过且 tracing 源码入图 | 修复 CMake 入口 |
| TRC-GATE-05 | 测试发现性门 | ctest -N 可见 tracing 新增 unit/contract 用例 | 修复 tests 注册 |
| TRC-GATE-06 | breaking 评审门 | 任何接口签名/错误映射变更均有评审结论 | 未评审不得合入 |
| TRC-GATE-07 | integration 准入门 | tests 顶层已接入 integration 子目录并明确标签策略，且 tracing 组件用例已落盘 | 未通过前补齐 tracing integration/failure 用例与注册 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| TRC-BLK-001 | 已解阻（2026-04-07）：metrics 子域桥接接口已冻结，TraceMetricsBridge 可稳定对接 `IMetricsProvider -> IMeter -> record(sample)` 协议 | TRC-TODO-015 | metrics 侧提供最小 bridge 接口与标签约束 | 解阻证据已由 docs/architecture/DASALL_infra_metrics模块详细设计.md 6.6.1/6.6.2、infra/include/metrics/IMetricsProvider.h、infra/include/metrics/IMeter.h，以及现有 LoggingMetricsBridge/PolicyMetricsBridge 落地实现共同满足 | 如后续 metrics 公共接口破坏性变更，回退为 tracing 内部计数快照 |
| TRC-BLK-002 | 已解阻（2026-04-07）：audit 子域写入接口与最小事件模型已冻结，TraceAuditBridge 可稳定对接 `IAuditLogger::write_audit` | TRC-TODO-015 | audit 侧冻结写入接口与最小事件模型 | 解阻证据已由 infra/include/audit/IAuditLogger.h，以及现有 MetricsAuditBridge/PolicyAuditBridge/SecretAuditBridge 与对应 contract 样板共同满足 | 如后续 audit 写入接口破坏性变更，回退为本地告警日志 + tracing 私有降级状态 |
| TRC-BLK-003 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；tracing integration 是否可执行改由组件自身落盘负责 | 后续 tracing integration 任务 | 无；后续仅需按组件落盘 integration/failure 用例 | 证据回链到 infra 专项 TODO 的 INF-BLK-06 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt | 若 tests 顶层 integration 接线或聚合依赖回退，则重新转为 Blocked |
| TRC-BLK-004 | OTLP exporter 首版是否启用未冻结，外部依赖与部署策略未定 | OTLP 相关任务 | 冻结 exporter 组合（noop/file/otlp）与依赖策略 | 先以 noop/file 形成可运行闭环，OTLP 后置评审 | 保持 exporter.type 默认 noop |

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

1. integration 验收命令暂不纳入首轮 Gate，原因是 tracing integration 用例尚未落盘；顶层 integration 拓扑已于 2026-03-30 解阻。
2. 每项任务至少包含 1 条构建命令与 1 条测试命令。

### 9.2 质量门逐项回答（第 7 章要求）

1. 是否给出 Design -> TODO 映射：是。
2. 是否明确当前最细可达到粒度：是，L3/L2 混合，最小可到函数级。
3. 是否所有任务具备代码目标、测试目标、验收命令：是。
4. 是否所有 Blocked 项带证据和解阻条件：是。
5. 是否所有任务具备可二值判定完成标准：是。
6. 是否避免跨子系统范围扩张：是。
7. 若要求函数/数据结构级拆分，是否真正落到对象：是。

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 回退策略 |
|---|---|---|---|---|
| 接口冻结后实现长期滞后，形成空壳 | Medium | 仅完成头文件不推进 B/C 阶段任务 | TRC-TODO-008~013 长期未启动 | 设置 TRC-GATE-02 为强制推进门禁 |
| 上下文传播跨线程语义不一致 | High | 过早引入隐式传播或多载体协议 | invalid context 计数异常升高 | 回退为显式 in-process 传播 |
| 队列策略错误导致高峰期丢 Span | High | overflow 策略实现与配置不一致 | trace_span_dropped_total、queue_depth 异常 | 切换 block 策略并降低采样率 |
| 导出失败被吞没 | High | exporter 失败未记错误码/指标/审计 | trace_export_failure_total 不增长但导出失败 | 回退到 TRC-TODO-013，强制失败路径补齐 |
| breaking change 默认推进 | High | 接口签名或错误映射变更未评审 | 无评审记录但发生公共边界变更 | 立即冻结合入，执行 TRC-GATE-06 |
| 配置覆盖破坏 Profile 约束 | Medium | 运行时覆盖越过 Profile 边界 | 配置回放与行为不一致 | 回退到部署层静态配置 |

## 11. 可行性结论

1. 结论：可直接生成并执行函数/数据结构级专项 TODO（L3/L2 混合）。
2. 原因：
   - tracing 详细设计已给出完整核心接口清单。
   - 已给出核心对象字段、主流程与异常流程。
   - 已给出错误码域与配置项默认策略。
   - 已给出落盘目录、建议文件与测试出口。
   - 当前阻塞主要集中在跨子域桥接与 integration 拓扑，不影响核心 tracing 闭环落地。
3. 当前最小可执行粒度：函数/接口/数据结构。
4. 未完全达到全域函数级的缺口：metrics/audit 桥接接口未冻结，tracing integration/failure 用例尚未落盘，OTLP 首版启用策略未冻结。
5. 下一步建议：
   - 先执行 TRC-TODO-001~TRC-TODO-014、TRC-TODO-016~TRC-TODO-018 完成 tracing 本地闭环。
   - 并行推进 TRC-BLK-001、TRC-BLK-002、TRC-BLK-004 解阻；TRC-BLK-003 已完成仓库级解阻，再进入桥接与集成验收。

## 12. ARC 修复增量（2026-03-26）

| ID | 状态 | 对应问题 | 任务描述 | 代码目标 | 测试目标 | 验收命令 | 前置依赖 | 完成判定 |
|---|---|---|---|---|---|---|---|---|
| TRC-TODO-020 | Not Started | ARC-01 | 在 tracing contract 边界增加 planning stage 观测约束：span 标签必须包含 stage=planning，且预算字段 budget_ms 可观测 | tests/contract/infra/tracing/TracePlanningStageContractTest.cpp, infra/include/tracing/TraceTypes.h | contract：TracePlanningStageContractTest 校验 stage 标签、budget_ms、trace_id/span_id 关联与降级可观测一致性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -R TracePlanningStageContractTest | TRC-TODO-005、TRC-TODO-018 | 仅当 planning 标签与预算字段被 contract 测试稳定约束，且不新增 contracts 共享对象时完成 |
| TRC-TODO-021 | Not Started | ARC-02 | 将 tracing 任务纳入仓库级 Blocked-first gate 流程，禁止在 Blocked 未解时推进实现任务 | scripts/ci/infra_gate.sh, docs/todos/infrastructure/DASALL_infrastructure_tracing组件专项TODO.md | process test：默认执行 gate 时存在 Blocked 即失败；解阻窗口需显式 ALLOW_BLOCKED=1 | bash scripts/ci/infra_gate.sh | TRC-TODO-018 | 仅当 tracing 执行流程与 gate 绑定，并形成可重复执行记录时完成 |
