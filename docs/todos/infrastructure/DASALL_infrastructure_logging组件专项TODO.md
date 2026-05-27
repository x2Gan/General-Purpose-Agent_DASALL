# DASALL infrastructure 子系统 logging 组件专项 TODO

最近更新时间：2026-05-27  
阶段：Detailed Design -> Special TODO  
适用范围：infra/logging

2026-05-27 追加冻结说明：

1. 本文档中的 `LOG-TODO-001~019` 完成状态，当前只代表接口/骨架/focused evidence 已落盘，不等于 production-ready。
2. logging production acceptance 已统一迁入 `docs/ssot/LoggingProductionAcceptanceMatrix.md`，并由 `INF-LOG-FIX-001~011` 管理 evidence level、installed artifact、owner boundary 与 non-extrapolation 规则。
3. 后续若要宣称 production / installed ready，必须同时满足 matrix 中的 gate 与 local installed authoritative evidence；不得再把早期 unit/contract/fixture 结果直接上卷成 production 结论。
4. 截至 2026-05-27，`INF-LOG-FIX-002` 已把 `StructuredFormatter` / `RedactionFilter` / `LoggingFacade` 默认主链、`dasall.logging.event.v1` schema 与 deny-by-default redaction pattern 冻结为 L2 focused evidence；后续若扩展字段或规则，必须沿 matrix 新开任务，不得覆盖当前 frozen tuple。
5. 截至 2026-05-27，`BLK-INF-LOG-003` 已冻结 writable path / permission policy：`DASALL_STATE_ROOT` 是唯一 state_root override，build-tree focused 仍使用 `logs/runtime.log`，installed authoritative path 固定为 `state_root/logging/runtime.log`，其他不可写路径必须 fail-closed，不得 silently fallback 到 repo 根、`/tmp` 或 qemu guest-side 路径。
6. 截至 2026-05-27，`INF-LOG-FIX-003` 已通过 `ILogSink` / `FileLogSink` / `SinkDispatcher` 闭合 build-tree file persistence、rotation 与 fail-closed focused evidence；默认未注入 sink 的 dispatcher 仍只保留 skeleton 行为，不直接宣称 live composition / installed ready。
7. 截至 2026-05-27，`INF-LOG-FIX-004` 已把 `AsyncQueueController` 推进到 deterministic single-worker queue，并把 `SinkDispatcher` sink write 移入 worker callback；`LoggingFacade::flush()` / `stop()` 现在对 drain success、timeout、worker stuck 给出明确结果，`BLK-INF-LOG-004` 可关闭。
8. 截至 2026-05-27，`INF-LOG-FIX-008` 已在 build-tree focused 范围内闭合 `AuditLinkAdapter` high-risk correlation、`SinkDispatcher` audit route 选择、`LoggingFacade -> IAuditLogger::write_audit()` handoff 与 `compose_live_observability()` attach audit owner；`LoggingAuditRouteIntegrationTest`、`AuditLinkAdapterPersistenceTest` 与 `AuditLogCorrelationContractTest` 已形成 L2 route/persistence/correlation 证据，但结论仍不外推到 installed / qemu。

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_infra_logging模块详细设计.md
2. docs/architecture/DASALL_infrastructure子系统详细设计.md
3. docs/architecture/DASSALL_Agent_architecture.md
4. docs/architecture/DASALL_Engineering_Blueprint.md
5. docs/adr/ADR-005-architecture-review-baseline.md
6. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
7. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
8. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
9. docs/plans/DASALL_工程落地实现步骤指引.md
10. docs/development/DASALL_工程协作与编码规范.md
11. 当前代码与测试现状：infra/CMakeLists.txt、infra/src/logging/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt、docs/todos/contracts/
12. 现有专项 TODO：docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md

生成原则：

1. 不改写已冻结 ADR。
2. 不越出 infrastructure/logging 组件边界。
3. 任务必须具备代码目标、测试目标、验收命令三件套。
4. 设计证据不足处只输出 Blocked 与补设计任务，不伪造实现任务。

## 2. 子系统目标与范围

### 2.1 目标

logging 组件目标固定为：

1. 提供统一普通日志能力与审计关联能力，并保持语义分离。
2. 维持结构化日志字段，与 trace/metrics/audit 协同。
3. 支持四层配置覆盖与 Profile 裁剪。
4. 在 sink/队列/格式异常时可观测、可降级、可恢复。

### 2.2 范围

纳入：

1. logging 组件接口与数据结构。
2. logging 组件错误码语义与配置模型。
3. logging 的 CMake 接线与测试注册点（unit/contract/integration）。
4. 与 infra/config、infra/metrics、infra/health 的 logging 侧适配边界。

不纳入：

1. runtime 状态机、恢复裁定、任务调度逻辑。
2. contracts 共享对象的字段扩写。
3. tracing/metrics/config 子系统完整实现（仅处理 logging 侧接入点）。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 logging TODO 的影响 |
|---|---|---|---|---|
| LOG-C001 | logging 设计 2.1；架构 8.5/9.4 | Must | 日志须携带 request_id/session_id/trace_id 并支持诊断拉取 | LogContext/LogEvent 必须冻结这些字段 |
| LOG-C002 | logging 设计 2.1；蓝图 3.12 | Must | 必须提供 Logger 与 AuditLinkAdapter 协同能力，IAuditLogger 由 infra/audit 持有 | logging 任务需聚焦审计关联，禁止重复建设审计主存储 |
| LOG-C003 | 架构 3.7；蓝图 4.2 | Must-Not | infra 不反向依赖业务模块实现 | 代码目标仅限 infra/tests/docs/cmake 路径 |
| LOG-C004 | 架构 3.8；蓝图 4.3；ADR-005 | Must | 仅消费 contracts 已冻结语义，保持兼容 | 禁止在 logging 侧重定义 contracts 语义对象 |
| LOG-C005 | ADR-006 | Must-Not | logging 不接管语义上下文组装与 Prompt 渲染 | logging 仅记录上下文标识，不生成语义上下文 |
| LOG-C006 | ADR-007 | Must-Not | logging 不拥有恢复决策权 | 仅记录失败证据与降级状态，不裁定恢复策略 |
| LOG-C007 | ADR-008 | Must | 多 Agent 审计字段需保留 parent_task_id/lease_id/worker_type | AuditEvent 必须含协同链路标识 |
| LOG-C008 | 架构 8.6；logging 设计 6.9 | Must | 配置支持默认->Profile->部署->运行时覆盖 | LoggingConfigAdapter 任务必须保留四层合并顺序 |
| LOG-C009 | 架构 8.8；logging 设计 6.8 | Must | 审计失败不可静默丢弃，需独立存储与告警 | 审计故障任务必须含 fallback 与告警测试 |
| LOG-C010 | 编码规范 3.6 | Must | 禁止吞错，失败必须可观测 | 错误路径任务必须含 error code + metrics |
| LOG-C011 | 编码规范 3.7 | Should | 新增公共接口同步测试 | 所有接口任务必须绑定 unit 或 contract |
| LOG-C012 | 工程落地步骤指引 阶段 C | Must | infra 底座先行，且每阶段含测试 | logging 任务顺序必须先接口/对象，再管线，再集成 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/CMakeLists.txt | 已接入 logging facade/sink/query/recovery/formatter/filter 等真实源码 | logging 运行时实现已纳入 `dasall_infra`，当前差距转向真实 sink/async/config/recovery/live composition 深化 |
| infra/include/ | 已形成“根目录共享契约 + 组件目录公共接口”布局，logging/ 子目录包含 `ILogger`、`ILogConfigurator`、`RedactionFilter`、`StructuredFormatter` 等公共接口 | logging public headers 已冻结，后续新增字段/规则必须遵守 matrix |
| infra/src/logging/ | `LoggingFacade`、`SinkDispatcher`、`AsyncQueueController`、`AuditLinkAdapter`、`LoggingRecovery`、`LoggingConfigAdapter`、`LoggingMetricsBridge`、`LoggingHealthProbe`、`LogQueryService`、`RedactionFilter`、`StructuredFormatter` 均已落盘 | 当前不再是“无实现”，而是生产 sink / installed evidence / 子系统接入尚未闭合 |
| tests/CMakeLists.txt | 已接入 mocks/unit/contract/integration 并提供 dasall_integration_tests 聚合入口 | integration 拓扑已接入顶层，后续只需补 logging 具体集成用例 |
| tests/unit/CMakeLists.txt | 已注册 infra 子目录 | logging unit 发现性已建立，后续只需补具体用例 |
| tests/contract/CMakeLists.txt | 已有 centralized registration 机制 | 可复用为 logging contract 边界校验入口 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：本轮可生成 L2 为主、局部 L3 的专项 TODO；不可全量进入函数实现级。

原因证据：

1. 已有明确核心接口清单：ILogger、IAuditLinkAdapter、ILogContextProvider、ILogConfigurator。
2. 已有核心对象字段：LogContext、LogEvent、AuditEvent、SinkRoutePolicy、RedactionPolicy。
3. 已有主流程与异常流程：正常路径 7 步、异常分类 4 类、恢复动作 3 类。
4. 已有错误语义清单：LOG_E_QUEUE_FULL、LOG_E_SINK_IO、LOG_E_FORMAT_INVALID、LOG_E_CONFIG_INVALID。
5. 已有目录与测试落点建议：infra/include/logging、infra/src/logging、tests/unit/infra/logging、tests/integration/infra/logging。
6. 当前不再缺 `LoggingHealthProbe` 或 `LogQueryService` 的本地索引/保留策略实现；剩余缺口已收敛到跨子系统 e2e / installed authoritative evidence，而不是 logging 核心构件本身尚未落盘。

当前最小可执行粒度：接口/数据结构级（L2）。

### 4.2 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| ILogger | logging 设计 6.6、6.8 | L2 | 接口名、方法名、异常场景、错误码 | log/flush 返回类型与 deadline 类型 | 直接拆接口冻结任务 |
| IAuditLinkAdapter | logging 设计 6.6、6.8、6.10 | L2 | 接口名、方法名、审计关联职责与失败可观测要求 | attach_audit_ref 入参对象定义 | 直接拆接口冻结任务 |
| ILogContextProvider | logging 设计 6.6 | L2 | 方法名 current_context、上下文字段要求 | 上下文生命周期与线程域说明 | 直接拆接口冻结任务 |
| ILogConfigurator | logging 设计 6.6、6.9 | L2 | apply(config) 与四层覆盖约束 | config 模型结构与冲突裁定规则 | 先补配置模型再实现 |
| LogContext | logging 设计 6.5 | L3 | 字段完整、contracts 对齐语义明确 | 字段类型别名约束未定义 | 先冻结结构体，再补类型细节 |
| LogEvent | logging 设计 6.5、6.10 | L2 | 字段与最低结构化字段约束 | attrs 白名单与序列化边界 | 直接拆数据结构任务 |
| AuditEvent | logging 设计 6.5、6.10 | L2 | 字段列表、审计回放约束 | side_effects 子结构模型 | 直接拆数据结构任务 |
| SinkDispatcher | logging 设计 6.2、6.4、6.7 | L2 | 路由职责、调用链位置 | 具体 sink 选择策略与对象签名 | 先实现路由骨架，不绑定实现细节 |
| AsyncQueueController | logging 设计 6.2、6.8、6.9 | L2 | 队列容量与溢出策略配置项 | 线程池参数、统计采样窗口 | 直接拆控制器骨架 + 队列策略测试 |
| RedactionFilter | logging 设计 6.2、6.3、6.9 | L2 | 脱敏职责、规则配置项、`dasall.logging.event.v1` 配套 deny-by-default pattern | 更丰富的规则 DSL/模式库定义 | 当前先保持 frozen key/message pattern 与 golden fixture，后续 DSL 扩展另开任务 |
| LoggingMetricsBridge | logging 设计 6.2、6.10；metrics 设计 6.6.1、6.8.1 | L2 | 指标名清单、IMeter::record(MetricSample) 导出协议、MetricLabels 五元组与 non-recursive failure 语义已冻结 | health bridge 接口签名仍待 LOG-BLK-003 | 直接拆 bridge 骨架 + unit/contract 边界测试 |
| LoggingHealthProbe | logging 设计 6.2、6.8、6.10.1；health 设计 6.5、6.6 | L2 | degraded 语义、`IHealthProbe`/`ProbeResult` 通用契约、descriptor 固定值 | logging 本地状态 provider 与阈值实现尚未落盘 | 直接拆 `LoggingHealthProbe` 骨架任务 |
| LogQueryService | logging 设计 6.9、6.10.2、6.10.11、6.10.12、8.3；架构 13.3 | L3 | `LogQueryRequest` / `LogQueryAccessContext` / `LogQueryResult`、`enable_diag_pull` gate、local artifact 约束，以及 default-disabled/admin-only、redaction-at-query、owner-safe metadata index 已冻结 | 跨子系统 log caller wiring 与 installed authoritative evidence | 已闭合：`FileLogReader`、`LogQueryService` artifact/index、`LogRetentionPolicy` + focused unit/integration/tests |
| tests/integration 注册点 | logging 设计 8.1、9.1；tests/CMakeLists.txt 现状 | L2 | tests 顶层 integration 拓扑、logging 组件用例与 `integration;logging` 标签均已落盘 | 后续场景扩展与聚合目标维护 | 沿既有目录与标签继续增量扩展 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| 统一入口 ILogger/IAuditLinkAdapter | logging 设计 6.6 | 接口 | LOG-TODO-001、LOG-TODO-002 | 先冻结调用面，再进入实现 |
| 上下文与事件对象 | logging 设计 6.5 | 数据结构 | LOG-TODO-003、LOG-TODO-004、LOG-TODO-005 | 先固化字段与语义，防止调用方各自拼装 |
| 生命周期与初始化前置条件 | logging 设计 6.6 前置条件；6.7 主流程 | 流程 | LOG-TODO-006 | 把 init/flush 契约落入 facade 骨架 |
| 路由与异步控制 | logging 设计 6.2、6.4、6.7、6.8、6.9 | 流程/适配器 | LOG-TODO-007、LOG-TODO-008 | 分拆为路由与队列两个单目标任务 |
| 审计关联适配 | logging 设计 6.2、6.8、6.10 | 流程/适配器 | LOG-TODO-009 | 验证 evidence_ref 关联链路与失败可观测 |
| 异常与错误处理 | logging 设计 6.6、6.8 | 错误处理 | LOG-TODO-010、LOG-TODO-011 | 错误码与故障降级拆开，便于二值验收 |
| 配置与 Profile 裁剪 | logging 设计 6.9；蓝图 5.1 | 配置 | LOG-TODO-012、LOG-BLK-001 | 先补配置模型再接入适配器 |
| metrics/health 桥接 | logging 设计 6.2、6.10、6.10.1 | 适配器 | LOG-TODO-013、LOG-TODO-017（LOG-BLK-003 已于 2026-04-03 解阻） | metrics bridge 已落盘，health probe 可直接按通用 `IHealthProbe` 边界实现 |
| 诊断拉取与本地导出 | logging 设计 6.9、6.10.2、8.3；架构 13.3 | 服务/查询 | LOG-TODO-019（LOG-BLK-005 已于 2026-04-03 解阻） | 首版只提供受控 trace/session 本地 artifact 生成，不开放 remote export 或任意属性检索 |
| CMake 与测试门禁 | logging 设计 8.1、9.1；当前 CMake 现状 | 门禁/测试 | LOG-TODO-014、LOG-TODO-015、LOG-TODO-018 | 构建注册与 unit/contract 已完成，integration 用例由 018 收口并关闭 Gate-06 |
| 交付证据回写 | logging 设计 9.2、11.1 | 文档/证据 | LOG-TODO-016 | 将 gate 结果与阻塞处理记录回写专项 TODO |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | LOG-TODO-001/002/006 |
| 数据结构定义类任务 | 是 | LOG-TODO-003/004/005 |
| 生命周期与初始化类任务 | 是 | LOG-TODO-006 |
| 适配器/桥接类任务 | 是 | LOG-TODO-007/008/013 |
| 异常与错误处理类任务 | 是 | LOG-TODO-010/011 |
| 配置与 Profile 裁剪类任务 | 是 | LOG-TODO-012（LOG-BLK-001 已于 2026-04-03 解阻） |
| 查询/导出边界类任务 | 是 | LOG-TODO-019（LOG-BLK-005 已于 2026-04-03 解阻） |
| 测试与门禁类任务 | 是 | LOG-TODO-014/015/018 |
| 文档/交付证据回写类任务 | 是 | LOG-TODO-016 |

## 6. 原子任务清单

### 6.1 任务清单（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| LOG-TODO-001 | Done | 定义 ILogger 接口头文件 | logging 设计 6.6；编码规范 3.7 | 6.6 核心接口语义定义 | L2 | infra/include/logging/ILogger.h | ILogger::log(event), ILogger::flush(timeout_ms), ILogger::set_level(level) | unit：接口可编译；contract：失败语义可映射 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | 无 | 无 | 接口头文件、最小编译测试；2026-03-31 已落盘 infra/include/logging/ILogger.h、tests/unit/infra/LoggerInterfaceTest.cpp、tests/contract/smoke/LoggerInterfaceBoundaryContractTest.cpp，并确认 logging::ILogger 作为唯一 canonical 日志入口，同时提供 log/flush 与最小 set_level(level) 控制面 | 仅当接口方法、命名、职责与 6.6 一致且编译通过时完成 |
| LOG-TODO-002 | Done | 定义 IAuditLinkAdapter 接口头文件 | logging 设计 6.6、6.10；架构 8.8 | 6.6 IAuditLinkAdapter；6.10 evidence_ref 关联要求 | L2 | infra/include/logging/IAuditLinkAdapter.h | IAuditLinkAdapter::attach_audit_ref(log_event), IAuditLinkAdapter::report_link_failure(reason) | unit：接口可编译；contract：审计关联字段不越权 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | 无（2026-03-31 已通过 AuditRef 前置声明占位解阻） | 已完成：先以前置声明/占位类型冻结接口，后续在 LOG-TODO-003 补全 AuditRef/LogTypes 具体结构 | 接口头文件、编译证据；2026-03-31 已落盘 infra/include/logging/IAuditLinkAdapter.h、tests/unit/infra/AuditLoggerInterfaceTest.cpp、tests/contract/smoke/AuditLoggerInterfaceBoundaryContractTest.cpp，并确认审计关联接口不暴露 write_audit/export_audit 审计存储职责 | 仅当审计关联接口与普通日志接口职责分离且编译通过时完成 |
| LOG-TODO-003 | Done | 定义 LogContext 数据结构 | logging 设计 6.5；架构 8.5 | 6.5 LogContext 字段约束 | L3 | infra/include/logging/LogTypes.h | LogContext{request_id,session_id,trace_id,task_id,parent_task_id,lease_id} | unit：unknown 兜底与非空语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-001 | 无 | 无 | 结构体定义、单测；2026-03-31 已落盘 infra/include/logging/LogTypes.h、tests/unit/infra/InfraContextTest.cpp、tests/contract/smoke/InfraContextBoundaryContractTest.cpp，并以 LogContext=InfraContext 兼容别名冻结 request/session/trace/task/parent_task/lease 六个标识字段，同时补齐最小 AuditRef 以解掉 002 的占位入参 | 仅当字段齐全、unknown 兜底被测试覆盖时完成 |
| LOG-TODO-004 | Done | 定义 LogEvent 数据结构 | logging 设计 6.5、6.10 | 6.5 LogEvent；6.10 结构化字段最小集合 | L2 | infra/include/logging/LogTypes.h | LogEvent{level,category,message,attrs,timestamp} | unit：attrs 可序列化；contract：不扩写 contracts 语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-003 | 无（attrs 键白名单保持后续补设计，不阻塞最小字段冻结） | 已完成：以 logging::LogEvent=infra::LogEvent 兼容别名冻结最小字段集合，继续通过 category() / has_timestamp() 维持 logging 设计术语，不重定义第二份日志对象布局 | 结构体定义、单测；2026-03-31 已更新 infra/include/logging/LogTypes.h、tests/unit/infra/LogEventTest.cpp、tests/contract/smoke/LogEventBoundaryContractTest.cpp，并确认 request/trace 等标识仍只作为 attrs 传递、不提升为顶层 contracts 字段 | 仅当最小字段集合与 6.10 一致且测试通过时完成 |
| LOG-TODO-005 | Done | 定义 AuditEvent 数据结构 | logging 设计 6.5、6.8、6.10；ADR-008 | 6.5 AuditEvent；6.10 审计字段 | L2 | infra/include/logging/LogTypes.h | AuditEvent{action,actor,target,side_effects,evidence_ref,outcome} | contract：多 Agent 链路标识覆盖；unit：字段必填校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | LOG-TODO-002 | 无（side_effects 子结构保持后续细化，不阻塞顶层字段冻结） | 已完成：以 logging::AuditEvent=infra::AuditEvent、logging::AuditContext=infra::AuditContext 兼容别名冻结审计事实与多 Agent 链路标识边界，避免 logging 侧重定义 audit 语义对象 | 结构体定义、contract 测试；2026-03-31 已更新 infra/include/logging/LogTypes.h、tests/unit/infra/AuditTypesTest.cpp、tests/contract/smoke/AuditBoundaryContractTest.cpp，并确认 parent_task_id/lease_id/worker_type 继续只通过 AuditContext 暴露、不抬升到 AuditEvent 顶层字段 | 仅当字段齐备且 contract 测试可阻止越权扩写时完成 |
| LOG-TODO-006 | Done | 实现 LoggingFacade 生命周期骨架 | logging 设计 6.2、6.6、6.7 | 6.2 子组件职责；6.7 主流程 | L2 | infra/src/logging/LoggingFacade.cpp | 初始化前置条件、log 调用主链、flush 出口 | unit：未初始化调用返回可判定失败；正常路径可走通 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-001、LOG-TODO-003、LOG-TODO-004 | 无（2026-04-03 已以 InfraOperationResult 承接 init/stop、以 LogWriteResult 承接 log/flush，解掉返回类型占位） | 已完成：先以内存 dispatch backend 固化 init -> context enrich -> dispatch -> flush 的最小主链，后续在 LOG-TODO-007 接入真实 SinkDispatcher | facade 实现骨架、单测；2026-04-03 已落盘 infra/src/logging/LoggingFacade.h、infra/src/logging/LoggingFacade.cpp、tests/unit/infra/logging/LoggingFacadeTest.cpp，并在 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt 增加当前任务所需的最小 unit 入口；验收以兼容 build-ci 既有生成器的 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_unit_tests、ctest --test-dir build-ci --output-on-failure -L unit 完成，103 个 unit 测试全部通过 | 仅当未初始化/已初始化两类路径均可二值判定时完成 |
| LOG-TODO-007 | Done | 实现 SinkDispatcher 路由骨架 | logging 设计 6.2、6.4、6.7 | 6.4 依赖关系；6.7 正常路径第 5 步 | L2 | infra/src/logging/SinkDispatcher.cpp | level/category -> sink route | unit：路由选择可判定；contract：不泄漏未声明字段 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-004 | 无（2026-04-03 已先固化 basic_file + audit 两路矩阵，避免等待完整 sink 类型表冻结） | 已完成：以 SinkDispatcher 实现最小 basic_file/audit 分流，并把 LoggingFacade 默认 dispatch backend 切到该路由骨架，为 LOG-TODO-008 的异步队列接入保留 backend 接口 | 路由实现骨架、单测；2026-04-03 已落盘 infra/src/logging/SinkDispatcher.h、infra/src/logging/SinkDispatcher.cpp、tests/unit/infra/logging/SinkDispatcherTest.cpp、tests/contract/smoke/SinkDispatcherBoundaryContractTest.cpp，并更新 infra/src/logging/LoggingFacade.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt；验收以 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_sink_dispatcher_unit_test dasall_logging_facade_unit_test dasall_contract_sink_dispatcher_boundary_test、ctest --test-dir build-ci --output-on-failure -R "(SinkDispatcherTest|LoggingFacadeTest|SinkDispatcherBoundaryContractTest)"、cmake --build build-ci --target dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci --output-on-failure -L unit、ctest --test-dir build-ci --output-on-failure -L contract 完成，104 个 unit 与 128 个 contract 测试全部通过 | 仅当普通与审计路由分流可测试验证时完成 |
| LOG-TODO-008 | Done | 实现 AsyncQueueController 队列策略骨架 | logging 设计 6.2、6.8、6.9；行业参考 spdlog async；docs/development/InfraConcurrencyPolicy.md | 6.8 队列满策略；6.9 queue_size/overflow_policy | L2 | infra/src/logging/AsyncQueueController.cpp | block/overrun_oldest 策略入口 | unit：队列满策略二值验证；failure-injection：drop 计数可观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-007 | 无（2026-04-03 已通过 docs/development/InfraConcurrencyPolicy.md 入口文档回链 docs/ssot/InfraConcurrencyPolicy.md，补齐并发策略引用路径后推进实现） | 已完成：固定 block 与 overrun_oldest 两类 backpressure 行为，并把 SinkDispatcher 切到 AsyncQueueController 以便后续异步线程池接入仍复用同一队列边界 | 控制器骨架、策略测试；2026-04-03 已落盘 docs/development/InfraConcurrencyPolicy.md、infra/src/logging/LoggingPipelineTypes.h、infra/src/logging/AsyncQueueController.h、infra/src/logging/AsyncQueueController.cpp、tests/unit/infra/logging/AsyncQueueControllerTest.cpp，并更新 infra/src/logging/SinkDispatcher.h、infra/src/logging/SinkDispatcher.cpp、tests/unit/infra/logging/SinkDispatcherTest.cpp、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt；验收以 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_async_queue_controller_unit_test dasall_sink_dispatcher_unit_test dasall_logging_facade_unit_test dasall_contract_sink_dispatcher_boundary_test、ctest --test-dir build-ci --output-on-failure -R "(AsyncQueueControllerTest|SinkDispatcherTest|LoggingFacadeTest|SinkDispatcherBoundaryContractTest)"、cmake --build build-ci --target dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci --output-on-failure -L unit、ctest --test-dir build-ci --output-on-failure -L contract 完成，105 个 unit 与 128 个 contract 测试全部通过 | 仅当两种溢出策略都能被测试稳定判定时完成 |
| LOG-TODO-009 | Done | 实现 AuditLinkAdapter 审计关联适配骨架 | logging 设计 6.2、6.8、6.10；架构 8.8 | 6.2 AuditLinkAdapter；6.8 审计关联失败处理 | L2 | infra/src/logging/AuditLinkAdapter.cpp | 高风险日志 evidence_ref 关联、失败告警入口 | unit：高风险日志可关联 evidence_ref；contract：审计关联字段完整性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | LOG-TODO-002、LOG-TODO-005、LOG-TODO-007 | 无（2026-04-03 已以 AuditRef 现有占位结构完成 evidence_ref/trace_id/task_id 关联，不再等待额外入参对象冻结） | 已完成：以 AuditLinkAdapter 固化高风险日志的 audit_ref_pending/evidence_ref/audit_trace_id/audit_task_id 关联字段，并把失败原因与 failure counter 作为告警出口保留下来，直接复用 SinkDispatcher 的 audit route 判定 | 审计关联适配骨架、合同测试；2026-04-03 已落盘 infra/src/logging/AuditLinkAdapter.h、infra/src/logging/AuditLinkAdapter.cpp、tests/unit/infra/logging/AuditLinkAdapterTest.cpp、tests/contract/smoke/AuditLinkAdapterBoundaryContractTest.cpp，并更新 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt；验收以 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_audit_link_adapter_unit_test dasall_contract_audit_link_adapter_boundary_test dasall_sink_dispatcher_unit_test、ctest --test-dir build-ci --output-on-failure -R "(AuditLinkAdapterTest|AuditLinkAdapterBoundaryContractTest|SinkDispatcherTest)"、cmake --build build-ci --target dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci --output-on-failure -L unit、ctest --test-dir build-ci --output-on-failure -L contract 完成，106 个 unit 与 129 个 contract 测试全部通过 | 仅当高风险日志与 evidence_ref 关联可验证且失败路径有告警出口时完成 |
| LOG-TODO-010 | Done | 定义 LoggingErrors 错误码域 | logging 设计 6.6、6.8 | 6.6 错误语义定义 | L3 | infra/include/logging/LoggingErrors.h | LOG_E_QUEUE_FULL, LOG_E_SINK_IO, LOG_E_FORMAT_INVALID, LOG_E_CONFIG_INVALID | unit：错误码枚举稳定；contract：与 contracts::ResultCode 映射一致 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | LOG-TODO-001、LOG-TODO-002 | 无（2026-04-03 已通过设计收敛文档把 logging -> contracts 映射矩阵固化，并以 unit/contract 测试替代口头矩阵） | 已完成：在 docs/todos/infrastructure/deliverables/LOG-TODO-010-LoggingErrors设计收敛.md 中冻结四个 LOG_E_* 码名、来源锚点与一级 contracts 映射，再落盘 header-only 错误码域和映射测试 | 错误码头文件、映射测试；2026-04-03 已落盘 infra/include/logging/LoggingErrors.h、tests/unit/infra/LoggingErrorsTest.cpp、tests/contract/smoke/LoggingErrorsBoundaryContractTest.cpp，并更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt；验收以 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_logging_errors_unit_test dasall_contract_logging_errors_boundary_test、ctest --test-dir build-ci -N -R "(LoggingErrorsTest|LoggingErrorsBoundaryContractTest)"、ctest --test-dir build-ci --output-on-failure -R "(LoggingErrorsTest|LoggingErrorsBoundaryContractTest)" 完成 | 仅当四个错误码均有来源锚点且映射测试通过时完成 |
| LOG-TODO-011 | Done | 实现 LoggingRecovery 故障降级骨架 | logging 设计 6.8；编码规范 3.6 | 6.8 sink IO/format 异常恢复动作 | L2 | infra/src/logging/LoggingRecovery.cpp | fallback sink, degraded 标记, 周期重试入口 | unit：degraded 状态切换；failure-injection：恢复后状态回清 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-009、LOG-TODO-010 | 无（2026-04-03 已通过 internal ILogRecoverySink 注入点与脚本化 mock sink 解掉 failure-injection 桩不足） | 已完成：在 docs/todos/infrastructure/deliverables/LOG-TODO-011-LoggingRecovery设计收敛.md 中固定 degraded/fallback/retry 三条恢复语义，并以可注入 sink + unit failure-injection 覆盖恢复成功/失败两类路径 | 恢复骨架、故障注入测试；2026-04-03 已落盘 infra/src/logging/LoggingRecovery.h、infra/src/logging/LoggingRecovery.cpp、tests/unit/infra/logging/LoggingRecoveryTest.cpp，并更新 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt；验收以 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_logging_recovery_unit_test、ctest --test-dir build-ci -N -R "LoggingRecoveryTest"、cmake --build build-ci --target dasall_unit_tests、ctest --test-dir build-ci --output-on-failure -L unit 完成 | 仅当降级触发、恢复成功、恢复失败三类路径可二值判定时完成 |
| LOG-TODO-012 | Done | 实现 LoggingConfigAdapter 四层配置适配 | logging 设计 6.6、6.9；架构 8.6 | 6.9 配置项与覆盖层级 | L2 | infra/src/logging/LoggingConfigAdapter.cpp | ILogConfigurator::apply(config) | unit：层级优先级验证；contract：Profile 不绕过审计主链 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_logging_config_merge_unit_test dasall_contract_log_configurator_boundary_test && ctest --test-dir build-ci -N -R "(LoggingConfigMergeTest|LogConfiguratorBoundaryContractTest)" && ctest --test-dir build-ci --output-on-failure -R "(LoggingConfigMergeTest|LogConfiguratorBoundaryContractTest)" && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | LOG-TODO-001 | 无（2026-04-03 已由 LOG-BLK-001 通过 LoggingConfig 对象表、键名与 per-key 层级接受规则冻结解阻） | 无 | 已完成：以 public `LoggingConfig` + `LoggingConfigApplyResult` 收敛 logging config surface，以 `LoggingConfigAdapter` 复用 ConfigCenter active typed config 并固化 per-key source acceptance/audit gate，再以 unit/contract 测试覆盖 runtime tunable 正例、non-tunable runtime override 负例与 profile 关闭 audit 的边界负例 | 配置接口、适配骨架、定向/聚合测试；2026-04-03 已落盘 infra/include/logging/ILogConfigurator.h、infra/src/logging/LoggingConfigAdapter.h、infra/src/logging/LoggingConfigAdapter.cpp、tests/unit/infra/logging/LoggingConfigMergeTest.cpp、tests/contract/smoke/LogConfiguratorBoundaryContractTest.cpp，并更新 infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt；验收以 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_infra dasall_logging_config_merge_unit_test dasall_contract_log_configurator_boundary_test、ctest --test-dir build-ci -N -R "(LoggingConfigMergeTest|LogConfiguratorBoundaryContractTest)"、ctest --test-dir build-ci --output-on-failure -R "(LoggingConfigMergeTest|LogConfiguratorBoundaryContractTest)"、cmake --build build-ci --target dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci --output-on-failure -L unit、ctest --test-dir build-ci --output-on-failure -L contract 完成，109 个 unit 与 131 个 contract 测试全部通过 | 仅当按冻结键域读取四层 active config、非法 runtime/profile 输入被拒绝且不绕过 audit 主链时完成 |
| LOG-TODO-013 | Done | 实现 LoggingMetricsBridge 指标桥接骨架 | logging 设计 6.2、6.10；架构 8.7；metrics 设计 6.6.1、6.8.1 | 6.10 指标清单与 label/failure contract | L2 | infra/src/logging/LoggingMetricsBridge.cpp | logging_write_total, logging_write_fail_total, logging_drop_total, logging_queue_depth, logging_flush_latency_ms | unit：指标发射成功/失败降级；contract：bridge 不直连 exporter 且不透传高基数标签 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_logging_metrics_bridge_unit_test dasall_contract_logging_metrics_bridge_boundary_test && ctest --test-dir build-ci -N -R "(LoggingMetricsBridgeTest|LoggingMetricsBridgeBoundaryContractTest)" && ctest --test-dir build-ci --output-on-failure -R "(LoggingMetricsBridgeTest|LoggingMetricsBridgeBoundaryContractTest)" && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | LOG-TODO-010 | 无（2026-04-03 已由 LOG-BLK-002 通过 IMeter::record 协议、MetricLabels 五元组填充表与 non-recursive failure 语义冻结解阻） | 无 | 已完成：新增 internal `LoggingMetricsBridge` / `LoggingMetricSignal` / `LoggingMetricsEmitResult`，首次 emit 预注册五个 frozen metric family，并在本地拒绝非白名单 stage/outcome/error_code；provider/meter 失败被归一到 `MetricsErrorCode` + `MetricsOperationStatus`，确保 bridge failure 不递归反噬 logging 主链 | 指标桥接骨架、定向/聚合 unit/contract 证据；2026-04-03 已落盘 infra/src/logging/LoggingMetricsBridge.h、infra/src/logging/LoggingMetricsBridge.cpp、tests/unit/infra/logging/LoggingMetricsBridgeTest.cpp、tests/contract/smoke/LoggingMetricsBridgeBoundaryContractTest.cpp，并更新 tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt；验收以 cmake -S . -B build-ci -G "Unix Makefiles"、cmake --build build-ci --target dasall_logging_metrics_bridge_unit_test dasall_contract_logging_metrics_bridge_boundary_test、ctest --test-dir build-ci -N -R "(LoggingMetricsBridgeTest|LoggingMetricsBridgeBoundaryContractTest)"、ctest --test-dir build-ci --output-on-failure -R "(LoggingMetricsBridgeTest|LoggingMetricsBridgeBoundaryContractTest)"、cmake --build build-ci --target dasall_unit_tests dasall_contract_tests、ctest --test-dir build-ci --output-on-failure -L unit、ctest --test-dir build-ci --output-on-failure -L contract 完成，110 个 unit 与 132 个 contract 测试全部通过 | 仅当五个 frozen 指标经 IMetricsProvider/IMeter 发射且上报失败不反噬 logging 主链时完成 |
| LOG-TODO-014 | Done | 注册 logging 构建落点到 infra CMake | logging 设计 8.1；代码现状 | 8.1 文件落盘建议 | L2 | infra/CMakeLists.txt | 新增 include/logging 与 src/logging 源文件接线 | build：dasall_infra 可编译；unit：新增测试目标可链接 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_logging_facade_unit_test dasall_sink_dispatcher_unit_test dasall_async_queue_controller_unit_test dasall_audit_link_adapter_unit_test dasall_logging_recovery_unit_test dasall_logging_config_merge_unit_test dasall_logging_metrics_bridge_unit_test dasall_contract_sink_dispatcher_boundary_test dasall_contract_audit_link_adapter_boundary_test dasall_contract_log_configurator_boundary_test dasall_contract_logging_metrics_bridge_boundary_test && ctest --test-dir build-ci -N -R "(LoggingFacadeTest|SinkDispatcherTest|AsyncQueueControllerTest|AuditLinkAdapterTest|LoggingRecoveryTest|LoggingConfigMergeTest|LoggingMetricsBridgeTest|SinkDispatcherBoundaryContractTest|AuditLinkAdapterBoundaryContractTest|LogConfiguratorBoundaryContractTest|LoggingMetricsBridgeBoundaryContractTest)" && ctest --test-dir build-ci --output-on-failure -R "(LoggingFacadeTest|SinkDispatcherTest|AsyncQueueControllerTest|AuditLinkAdapterTest|LoggingRecoveryTest|LoggingConfigMergeTest|LoggingMetricsBridgeTest|SinkDispatcherBoundaryContractTest|AuditLinkAdapterBoundaryContractTest|LogConfiguratorBoundaryContractTest|LoggingMetricsBridgeBoundaryContractTest)" | LOG-TODO-001 至 LOG-TODO-011 | 无（2026-04-03 已将 logging skeleton 正式纳入 `dasall_infra`，并移除 unit/contract 目标对同一批 logging 实现的重复编译） | 无 | 已完成：新增 `DASALL_INFRA_LOGGING_SOURCES`，将 AsyncQueueController/AuditLinkAdapter/LoggingConfigAdapter/LoggingFacade/LoggingMetricsBridge/LoggingRecovery/SinkDispatcher 正式接入 `dasall_infra`，并回收 logging 测试目标中的本地 `.cpp` 直编路径；交付物见 docs/todos/infrastructure/deliverables/LOG-TODO-014-Logging构建接线收敛.md，定向构建与 11 个受影响测试均通过 | 仅当 placeholder 不再是唯一源码入口且目标编译通过时完成 |
| LOG-TODO-015 | Done | 注册 logging 单元与契约测试入口 | logging 设计 8.1、9.1；编码规范 3.7 | 9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt, tests/unit/infra/logging/, tests/contract/CMakeLists.txt | 新增 logging 相关 unit/contract 测试注册 | unit + contract：ctest 可发现并执行新增用例 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -L logging && ctest --test-dir build-ci --output-on-failure -L logging | LOG-TODO-003、LOG-TODO-005、LOG-TODO-010 | 无（2026-04-03 已将散落的 logging unit/contract 入口收敛为显式 target 分组与 `logging` 标签 discoverability） | 无 | 已完成：新增 `DASALL_LOGGING_UNIT_TEST_EXECUTABLE_TARGETS`、`dasall_register_logging_unit_test(...)` 与 `dasall_register_logging_contract_test(...)`，将 12 个 unit 与 9 个 contract 用例统一纳入 logging 标签面；交付物见 docs/todos/infrastructure/deliverables/LOG-TODO-015-Logging测试注册收敛.md，`ctest -N -L logging` 发现 21 个测试且 21/21 通过 | 仅当新增测试在 ctest -N 可见且执行通过时完成 |
| LOG-TODO-016 | Done | 回写 logging 质量门与交付证据 | logging 设计 9.2、11.1；工程规范 6.2 | 9.2 Gate-LOG-01~05 | L2 | docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md | Gate 执行结论、阻塞变化、回退执行记录 | process test：门禁记录与执行结果可追溯 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | LOG-TODO-015 | 无（2026-04-03 已完成 Gate-LOG-01~06 与 blocker 快照回写；当前仅保留 LOG-GATE-06 未通过结论，不构成 014~016 的执行阻塞） | 无 | 已完成：在本文件新增 9.3/9.4/9.5 执行快照，统一记录 Gate-LOG-01~06 结论、LOG-BLK-001~005 当前状态、CMake Tools 工具态异常与“未触发代码回退”记录；交付物见 docs/todos/infrastructure/deliverables/LOG-TODO-016-LoggingGate回写收敛.md | 仅当每个门禁都有通过/失败结论与证据命令时完成 |
| LOG-TODO-017 | Done | 实现 LoggingHealthProbe 健康探针骨架 | logging 设计 6.2、6.8、6.10.1；health 设计 6.5、6.6 | 6.10.1 health probe descriptor 与状态映射 | L2 | infra/src/logging/LoggingHealthProbe.cpp | `IHealthProbe::probe()`；descriptor 合法性；Healthy/Degraded/Unhealthy 三态映射 | unit：descriptor 与状态映射可判定；failure：timeout failure 结构化返回 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_logging_health_probe_unit_test dasall_unit_tests && ctest --test-dir build-ci -N -R "LoggingHealthProbeTest" && ctest --test-dir build-ci --output-on-failure -R "LoggingHealthProbeTest" && ctest --test-dir build-ci -N -L logging && ctest --test-dir build-ci --output-on-failure -L logging && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-011 | 无（2026-04-03 已由 LOG-BLK-003 通过 `IHealthProbe`/`ProbeResult`、descriptor 固定值与 timeout 语义冻结解阻） | 无 | 已完成：新增 internal `ILoggingHealthSignalProvider` / `LoggingHealthSample` 与 `LoggingHealthProbe`，以 queue 高水位阈值、drop delta、recovery degraded/fallback、unrecoverable failure、metrics bridge degraded 汇聚 logging 本地信号，并在 unit 测试中验证 Healthy/Degraded/Unhealthy 与 timeout failure；交付物见 docs/todos/infrastructure/deliverables/LOG-TODO-017-LoggingHealthProbe骨架收敛.md，`ctest -N -R "LoggingHealthProbeTest"` 发现 1 个定向测试且 1/1 通过，`ctest -N -L logging` 发现 24 个 logging 测试且 24/24 通过 | 仅当 probe descriptor 合法、三态映射稳定且 timeout failure 可结构化返回时完成 |
| LOG-TODO-018 | Done | 落盘 logging integration 用例与标签注册 | logging 设计 8.1、9.1；Gate-LOG-06 | 8.1 integration 落点；9.1 集成测试矩阵 | L2 | tests/integration/infra/logging/, tests/integration/infra/CMakeLists.txt, tests/integration/CMakeLists.txt | LoggingFacade/SinkDispatcher/AsyncQueueController/AuditLinkAdapter 跨组件 smoke 与 failure integration | build + integration：logging integration 用例可发现、可执行，并纳入 `integration;logging` 标签 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_logging_pipeline_integration_test dasall_logging_audit_link_integration_test dasall_integration_tests && ctest --test-dir build-ci -N -R "(LoggingPipelineIntegrationTest|LoggingAuditLinkIntegrationTest)" && ctest --test-dir build-ci --output-on-failure -R "(LoggingPipelineIntegrationTest|LoggingAuditLinkIntegrationTest)" && ctest --test-dir build-ci -N -L integration && ctest --test-dir build-ci --output-on-failure -L integration | LOG-TODO-014、LOG-TODO-015 | 无（2026-04-03 tests 顶层 integration 拓扑已解阻，当前只需补组件级 logging 用例与标签） | 无 | 已完成：新增 `tests/integration/infra/logging/`、`LoggingPipelineIntegrationTest` 与 `LoggingAuditLinkIntegrationTest`，并在 integration CMake 中统一注册 `integration;logging` 标签；交付物见 docs/todos/infrastructure/deliverables/LOG-TODO-018-Logging集成用例收敛.md，Gate-LOG-06 已转为 Pass | 仅当 logging integration 用例在 ctest -N/-L integration 可见且执行通过时完成 |
| LOG-TODO-019 | Done | 实现 LogQueryService 受控查询与本地 artifact 导出骨架 | logging 设计 6.9、6.10.2、8.3；架构 13.3；diagnostics 设计 6.6/6.8/6.9 | 6.10.2 query schema、allow proof 与 local artifact 约束 | L2 | infra/src/logging/LogQueryService.cpp | `query(const LogQueryRequest&, const LogQueryAccessContext&)`；trace/session 精确 selector；local artifact_ref 生成 | unit：query 参数、allow proof 与 config gate 可判定；integration：trace/session 查询命中与本地 artifact 导出可执行 | cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_log_query_service_unit_test dasall_log_query_integration_test dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -N -R "(LogQueryServiceTest|LogQueryIntegrationTest)" && ctest --test-dir build-ci --output-on-failure -R "(LogQueryServiceTest|LogQueryIntegrationTest)" && ctest --test-dir build-ci -N -L integration && ctest --test-dir build-ci --output-on-failure -L integration && ctest --test-dir build-ci -N -L logging && ctest --test-dir build-ci --output-on-failure -L logging && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-012、LOG-TODO-018 | 无（2026-04-03 已由 LOG-BLK-005 通过 query schema、PolicyGate allow proof 与本地 artifact 导出约束冻结解阻） | 无 | 已完成：新增 internal `LogQueryService` / `ILogQueryRecordReader` 与 unit/integration 覆盖，交付物见 docs/todos/infrastructure/deliverables/LOG-TODO-019-LogQueryService骨架收敛.md；`ctest -N -R "(LogQueryServiceTest|LogQueryIntegrationTest)"` 发现 2 个定向测试且 2/2 通过，`ctest -N -L integration` 发现 10 个 integration 测试且 10/10 通过，`ctest -N -L logging` 发现 26 个 logging 测试且 26/26 通过 | 仅当 trace/session 精确 selector、`PolicyDenied`/`ValidationFieldMissing` 负例与本地 artifact 导出正例都可稳定判定时完成 |

### 6.2 Blocked 任务对应阻塞项索引

| 任务 ID | 对应阻塞项 |
|---|---|
| 无 | 无 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 对象与接口冻结 | LOG-TODO-001~005 | 可并行（001/002 与 003/004/005 两组） | 先冻结边界，避免实现期反复改接口 |
| B 主链路骨架 | LOG-TODO-006~009 | 串行 | facade -> dispatcher -> queue -> audit，保持单向依赖 |
| C 错误与恢复 | LOG-TODO-010~011 | 串行 | 先错误码，再降级恢复，便于统一测试断言 |
| D 构建与测试接线 | LOG-TODO-014~015 | 可并行 | CMake 接线和测试注册可并行推进 |
| E 阻塞项解消后推进 | LOG-TODO-012、LOG-TODO-013、LOG-TODO-017 | 串行 | 先配置模型，再指标桥接，最后补 health probe |
| F 证据收口 | LOG-TODO-016 | 串行收口 | 统一回写 gate 与阻塞状态 |
| G 集成门禁 | LOG-TODO-018 | 串行 | 在顶层 integration 拓扑已解阻后，补 logging 组件用例并关闭 Gate-06 |
| H 受控诊断查询 | LOG-TODO-019 | 串行 | 在 config gate 与 integration 拓扑已就绪后，补受控 trace/session 本地 artifact 骨架 |

### 7.2 必过门禁

| Gate ID | 门禁项 | 通过标准 | 失败处置 |
|---|---|---|---|
| LOG-GATE-01 | 接口冻结门 | ILogger/IAuditLinkAdapter/LogTypes/LoggingErrors 落盘且编译通过 | 回退到接口定义，不推进实现 |
| LOG-GATE-02 | 审计隔离门 | 审计关联与普通日志主链分离有测试证明 | 回退 AuditLinkAdapter，阻断集成推进 |
| LOG-GATE-03 | 异常可观测门 | queue/sink/format/config 失败路径有错误码和计数出口 | 补齐错误处理后再提测 |
| LOG-GATE-04 | 测试发现性门 | ctest -N 能看到 logging 新增 unit/contract 测试 | 修复 CMake 注册 |
| LOG-GATE-05 | breaking 评审门 | 任何接口签名/contracts 映射变化均有评审结论 | 未评审不得合入 |
| LOG-GATE-06 | integration 准入门 | tests 顶层已完成 integration 注册策略，且 logging 组件用例已落盘 | 未通过前补齐 logging 组件 integration 用例与标签注册 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| LOG-BLK-001 | 已解阻（2026-04-03）：ILogConfigurator 的 `LoggingConfig`/`LoggingConfigApplyResult`、`infra.logging.*` frozen key set、per-key 层级接受规则与 `infra.audit.required` 准入门已在 logging 详细设计与交付物中固化；runtime override 继续复用 ConfigCenter typed patch，不开放 logging 私有自由字典 | LOG-TODO-012 | 无；后续仅需保持 design/header/test 键域同步 | 证据回链到 docs/architecture/DASALL_infra_logging模块详细设计.md 6.6/6.9 与 docs/todos/infrastructure/deliverables/LOG-BLK-001-LoggingConfig设计收敛.md | 若 infra/config 的 key 域、ConfigSourceKind 契约或 audit.required 保护规则回退，则重新转为 Blocked |
| LOG-BLK-002 | 已解阻（2026-04-03）：metrics 详细设计已冻结 logging bridge 的 IMetricsProvider/IMeter 接入协议、五指标对象表、MetricLabels 五元组填充规则与 non-recursive failure semantics；logging 不直连 IMetricExporter，也不新增第二套 exporter/bridge 接口 | LOG-TODO-013 | 无；后续仅需保持 metrics/logging 设计、bridge 实现与测试标签白名单同步 | 证据回链到 docs/architecture/DASALL_infra_metrics模块详细设计.md 6.6.1/6.8.1/6.10、docs/architecture/DASALL_infra_logging模块详细设计.md 6.10 与 docs/todos/infrastructure/deliverables/LOG-BLK-002-LoggingMetricsBridge设计收敛.md | 若 IMeter、MetricLabels 或 MetricsErrors 契约回退，或 logging 重新尝试直连 exporter，则重新转为 Blocked |
| LOG-BLK-003 | 已解阻（2026-04-03）：infra/health 详细设计已冻结 `IHealthProbe`、`ProbeDescriptor`、`ProbeResult` 与 timeout failure 语义；logging 详细设计 6.10.1 已补齐 `LoggingHealthProbe` 的 descriptor 固定值、状态映射与只读采样约束 | LOG-TODO-017 | 无；后续仅需保持 logging probe 实现与 descriptor/status mapping 同步 | 证据回链到 docs/architecture/DASALL_infra_health模块详细设计.md 6.5/6.6、docs/architecture/DASALL_infra_logging模块详细设计.md 6.10.1 与 docs/todos/infrastructure/deliverables/LOG-BLK-003-LoggingHealthProbe设计收敛.md | 若 health 通用 `ProbeResult`/`IHealthProbe` 契约回退，或 logging probe 重新定义私有 output object，则重新转为 Blocked |
| LOG-BLK-004 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；logging integration 用例与 `integration;logging` 标签已于 2026-04-03 落盘 | 后续 integration 任务 | 无；后续仅需按组件扩展 logging integration 场景 | 证据回链到 infra 专项 TODO 的 INF-BLK-06 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt、tests/integration/infra/logging/ | 若 tests 顶层 integration 接线、聚合依赖或 logging 标签注册回退，则重新转为 Blocked |
| LOG-BLK-005 | 已解阻（2026-04-03）：logging 详细设计 6.10.2 已冻结 `LogQueryRequest`、`LogQueryAccessContext`、`LogQueryResult`，并将 diag pull 限定为“trace/session 精确选择 + 上游 allow 决策证明 + 本地 artifact 导出”；remote export 仍由 diagnostics 子域持有 | LOG-TODO-019 | 无；后续仅需保持 query schema、policy allow proof 与本地 artifact 约束同步 | 证据回链到 docs/architecture/DASALL_infra_logging模块详细设计.md 6.10.2 与 docs/todos/infrastructure/deliverables/LOG-BLK-005-LogQueryService设计收敛.md | 若 logging 重新开放自由查询语法、绕过 PolicyGate allow proof，或试图自持 remote export，则重新转为 Blocked |

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

1. integration 命令已恢复为 logging 专项的扩展验收链；仅当任务直接修改 `tests/integration/infra/logging/` 或跨组件主链时，才将其提升为当前原子任务的必验命令。
2. 每项任务验收最少需要一条 build 命令和一条 test 命令。

### 9.2 质量门逐项回答（第 7 章要求）

1. 是否给出 Design -> TODO 映射：是。
2. 是否明确当前最细粒度等级：是，L2 为主，局部 L3。
3. 是否全部任务具备代码目标 + 测试目标 + 验收命令：是。
4. 是否所有 Blocked 项带证据和解阻条件：是。
5. 是否所有任务可二值判定：是。
6. 是否避免跨子系统范围扩张：是。
7. 是否真正落到接口/数据结构级对象：是。

### 9.3 2026-04-03 Gate 执行快照

| Gate ID | 当前状态 | 证据 | 结论 |
|---|---|---|---|
| LOG-GATE-01 | Pass | LOG-TODO-001/002/003/010 已完成；`ctest --test-dir build-ci --output-on-failure -L logging` 覆盖 `ILogger` / `IAuditLinkAdapter` / `LogEvent` / `LoggingErrors` 边界测试 | logging public boundary 已冻结并保持可编译、可验证 |
| LOG-GATE-02 | Pass | LOG-TODO-009 已完成；`AuditLinkAdapterTest` 与 `AuditLinkAdapterBoundaryContractTest` 在 logging 标签测试面内通过 | 审计关联与普通日志主链分离仍有测试证明 |
| LOG-GATE-03 | Pass | LOG-TODO-010~019 已完成；`ctest --test-dir build-ci --output-on-failure -L unit` 112/112 通过，`ctest --test-dir build-ci --output-on-failure -L contract` 132/132 通过，`ctest --test-dir build-ci --output-on-failure -L integration` 10/10 通过 | queue/sink/config/metrics/health/log query failure 路径维持错误码与可观测出口 |
| LOG-GATE-04 | Pass | `ctest --test-dir build-ci -N` 发现 254 个测试；`ctest --test-dir build-ci -N -L logging` 发现 26 个 logging 测试，已覆盖 unit/contract/integration 三类标签面 | logging 测试发现性已独立收敛 |
| LOG-GATE-05 | Pass | LOG-TODO-014~019 与 LOG-BLK-005 解阻轮次仅修改 internal 源码、CMake、测试标签、TODO、设计交付物与 worklog；未改 public headers 或 contracts 映射 | 当前无 breaking change，评审门未被触发 |
| LOG-GATE-06 | Pass | `tests/integration/infra/logging/` 已落盘，`ctest --test-dir build-ci -N -R "(LoggingPipelineIntegrationTest|LoggingAuditLinkIntegrationTest|LogQueryIntegrationTest)"` 与 `ctest --test-dir build-ci --output-on-failure -L integration` 通过 | logging 组件已具备 integration 用例与标签注册，integration 准入门关闭 |

### 9.4 2026-04-03 Blocker 状态快照

| Blocker ID | 当前状态 | 是否影响 LOG-TODO-014~016 | 说明 |
|---|---|---|---|
| LOG-BLK-001 | Resolved | 否 | LoggingConfig / source acceptance / audit gate 已冻结并支撑 LOG-TODO-012 |
| LOG-BLK-002 | Resolved | 否 | metrics bridge 接入协议已冻结并支撑 LOG-TODO-013 |
| LOG-BLK-003 | Resolved | 否 | `LoggingHealthProbe` 已对齐通用 `IHealthProbe`/`ProbeResult` 且实现骨架已落盘；后续仅需在同一 internal provider 边界上扩展真实运行时 wiring |
| LOG-BLK-004 | Resolved | 否 | 顶层 integration 拓扑与 logging 组件用例均已落盘，后续只需维护场景扩展与标签聚合 |
| LOG-BLK-005 | Resolved | 否 | `LogQueryService` 查询对象、allow proof 与本地 artifact 约束已冻结，且 LOG-TODO-019 骨架已按同一 local-only 边界落盘 |

### 9.5 验证与回退记录

1. `ctest --test-dir build-ci -N`：发现 254 个测试。
2. `ctest --test-dir build-ci --output-on-failure -L unit`：112/112 通过。
3. `ctest --test-dir build-ci --output-on-failure -L contract`：132/132 通过。
4. `ctest --test-dir build-ci --output-on-failure -L integration`：10/10 通过，其中 logging integration 3/3 通过。
5. `ctest --test-dir build-ci -N -L logging`：发现 26 个 logging 测试。
6. `ctest --test-dir build-ci --output-on-failure -L logging`：26/26 通过。
7. `Build_CMakeTools` 与 `RunCtest_CMakeTools` 仍报“无法配置项目”，本阶段实际验收继续沿用显式 `cmake`/`ctest` 链路。
8. `ctest --test-dir build-ci -N -R "LoggingHealthProbeTest"`：发现 1 个定向测试，`ctest --test-dir build-ci --output-on-failure -R "LoggingHealthProbeTest"` 1/1 通过。
9. `ctest --test-dir build-ci -N -R "(LogQueryServiceTest|LogQueryIntegrationTest)"`：发现 2 个定向测试，`ctest --test-dir build-ci --output-on-failure -R "(LogQueryServiceTest|LogQueryIntegrationTest)"` 2/2 通过。
10. 014~019 未触发代码回退；`LOG-GATE-06` 维持 Pass。
11. `grep -n "LogQueryService\|LogQueryRequest\|LogQueryAccessContext\|diag://infra/logging/query\|LOG-TODO-019" docs/architecture/DASALL_infra_logging模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md docs/todos/infrastructure/deliverables/LOG-BLK-005-LogQueryService设计收敛.md`：可定位 query schema、allow proof、本地 artifact 约束与 TODO 回写证据。

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 回退策略 |
|---|---|---|---|---|
| 接口先行但实现滞后导致空壳长期存在 | Medium | 仅完成头文件不推进骨架实现 | 任务长期停留在 A 阶段 | 以 LOG-TODO-006 作为强制跟进门禁 |
| 审计字段不全导致追责链断裂 | High | AuditEvent 未覆盖 parent_task_id/lease_id | contract 测试失败或字段缺失 | 立即回退到 LOG-TODO-005 并补字段 |
| 队列策略实现偏差导致高峰期阻塞 | High | block/overrun_oldest 行为与配置不一致 | queue_depth 与 drop_total 异常 | 回退到同步 basic sink 并保留告警 |
| 故障降级路径吞错 | High | sink 或 formatter 失败未上报错误码/指标 | write_fail_total 无变化 | 回退并强制补 LoggingErrors 与计数埋点 |
| 配置合并破坏 Profile 约束 | Medium | 运行时覆盖越过审计强制链路 | contract 门禁触发 | 回退到部署层静态配置 |
| breaking change 被默认推进 | High | 修改接口签名或映射未过评审 | review 记录缺失 | 立即冻结变更并走 LOG-GATE-05 评审门 |

## 11. 可行性结论

### 11.1 结论

可直接生成并执行接口/数据结构级专项 TODO；当前不建议直接全面进入函数实现级。

### 11.2 支撑证据

1. logging 设计已明确接口名、对象字段、错误语义、主异常流程与目录落点。
2. 当前仓库中 logging 已完成接口与对象冻结，后续重点转向 facade/dispatcher/queue/recovery 等骨架实现。
3. tests 顶层 integration 已接入，且 logging 组件最小 integration smoke 已落盘；当前集成缺口已从“无用例”转为“后续场景扩展”。
4. metrics/config/health 与 `LogQueryService` persisted reader/index/retention 已全部收敛到实现期并落盘；当前剩余缺口主要集中在跨子系统 production logging gate 与 installed authoritative evidence，而不是 logging 核心构件缺失。
5. ADR-005/006/007/008 对边界限制明确，禁止越权扩张。

### 11.3 当前最小可执行粒度

接口级与数据结构级（L2），局部可达函数语义级（L3，仅限字段与错误码定义任务）。

### 11.4 未达全量函数级的缺失信息

1. 跨子系统 live composition 下的 owner-safe attrs/correlation matrix 与 queryability gate。
2. installed/package authoritative logging evidence、proof schema 与 release handoff。

### 11.5 下一步建议

1. 当前专项 TODO 已完成 001~019 的全部原子任务，且 `INF-LOG-FIX-009` 已把 retention、真实 persisted reader 与本地 artifact/index 实现补齐；若继续推进 logging 子域，应新开围绕跨子系统 e2e 或 installed evidence 的后续原子任务，而不是回退已完成主链。
2. `LOG-BLK-003`、`LOG-BLK-005` 已解阻且 `LOG-GATE-06` 已关闭；当前 logging 专项没有遗留 blocker。
3. 后续若新增 logging integration 场景，应沿用 `tests/integration/infra/logging/` 与 `integration;logging` 标签，不再回退到顶层无组件归属的 smoke 用例。