# DASALL infrastructure 子系统 logging 组件专项 TODO

最近更新时间：2026-03-25  
阶段：Detailed Design -> Special TODO  
适用范围：infra/logging

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
| infra/CMakeLists.txt | 仅编译 src/placeholder.cpp | logging 尚未接入构建 |
| infra/include/ | 空目录 | logging 对外接口未落盘 |
| infra/src/logging/ | 目录存在但空 | 仅有设计，无实现 |
| tests/CMakeLists.txt | 仅 add_subdirectory(mocks/unit/contract) | integration 尚未纳入顶层注册 |
| tests/unit/CMakeLists.txt | 未注册 infra 子目录 | logging unit 发现性缺失 |
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
6. 但缺失多处函数签名细节（返回类型、参数对象、导出查询模型），且 integration 注册点在当前工程中未接线。

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
| RedactionFilter | logging 设计 6.2、6.3、6.9 | L2 | 脱敏职责、规则配置项 | 规则 DSL/模式库定义 | 先落规则集版本与最小匹配策略 |
| LoggingMetricsBridge | logging 设计 6.2、6.10 | L1 | 指标名清单 | 指标上报接口与标签约束 | 先补 metrics 适配接口设计 |
| LoggingHealthProbe | logging 设计 6.2、6.8 | L1 | degraded 语义与恢复信号 | IHealthProbe 接口形状、超时策略对象 | 先补 health 探针接口设计 |
| LogQueryService | logging 设计 6.9、8.3 | L1 | 诊断导出需求明确 | query 对象、权限边界、索引策略 | 标记 Blocked，先补设计 |
| tests/integration 注册点 | logging 设计 8.1、9.1；tests/CMakeLists.txt 现状 | L0 | 设计建议存在 | 顶层 CMake 未 add_subdirectory(integration) | 先解阻测试拓扑再拆集成任务 |

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
| metrics/health 桥接 | logging 设计 6.2、6.10 | 适配器 | LOG-TODO-013、LOG-BLK-002、LOG-BLK-003 | 设计证据不足，先定义边界再实现 |
| CMake 与测试门禁 | logging 设计 8.1、9.1；当前 CMake 现状 | 门禁/测试 | LOG-TODO-014、LOG-TODO-015、LOG-BLK-004 | 构建注册与 unit/contract 可先做，integration 先解阻 |
| 交付证据回写 | logging 设计 9.2、11.1 | 文档/证据 | LOG-TODO-016 | 将 gate 结果与阻塞处理记录回写专项 TODO |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | LOG-TODO-001/002/006 |
| 数据结构定义类任务 | 是 | LOG-TODO-003/004/005 |
| 生命周期与初始化类任务 | 是 | LOG-TODO-006 |
| 适配器/桥接类任务 | 是 | LOG-TODO-007/008/013 |
| 异常与错误处理类任务 | 是 | LOG-TODO-010/011 |
| 配置与 Profile 裁剪类任务 | 是 | LOG-TODO-012（含阻塞 LOG-BLK-001） |
| 测试与门禁类任务 | 是 | LOG-TODO-014/015（含阻塞 LOG-BLK-004） |
| 文档/交付证据回写类任务 | 是 | LOG-TODO-016 |

## 6. 原子任务清单

### 6.1 任务清单（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| LOG-TODO-001 | Not Started | 定义 ILogger 接口头文件 | logging 设计 6.6；编码规范 3.7 | 6.6 核心接口语义定义 | L2 | infra/include/logging/ILogger.h | ILogger::log(event), ILogger::flush(timeout_ms), ILogger::set_level(level) | unit：接口可编译；contract：失败语义可映射 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | 无 | 无 | 接口头文件、最小编译测试 | 仅当接口方法、命名、职责与 6.6 一致且编译通过时完成 |
| LOG-TODO-002 | Not Started | 定义 IAuditLinkAdapter 接口头文件 | logging 设计 6.6、6.10；架构 8.8 | 6.6 IAuditLinkAdapter；6.10 evidence_ref 关联要求 | L2 | infra/include/logging/IAuditLinkAdapter.h | IAuditLinkAdapter::attach_audit_ref(log_event), IAuditLinkAdapter::report_link_failure(reason) | unit：接口可编译；contract：审计关联字段不越权 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | attach_audit_ref 入参对象未冻结 | 先以前置声明/占位类型冻结接口 | 接口头文件、编译证据 | 仅当审计关联接口与普通日志接口职责分离且编译通过时完成 |
| LOG-TODO-003 | Not Started | 定义 LogContext 数据结构 | logging 设计 6.5；架构 8.5 | 6.5 LogContext 字段约束 | L3 | infra/include/logging/LogTypes.h | LogContext{request_id,session_id,trace_id,task_id,parent_task_id,lease_id} | unit：unknown 兜底与非空语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-001 | 无 | 无 | 结构体定义、单测 | 仅当字段齐全、unknown 兜底被测试覆盖时完成 |
| LOG-TODO-004 | Not Started | 定义 LogEvent 数据结构 | logging 设计 6.5、6.10 | 6.5 LogEvent；6.10 结构化字段最小集合 | L2 | infra/include/logging/LogTypes.h | LogEvent{level,category,message,attrs,timestamp} | unit：attrs 可序列化；contract：不扩写 contracts 语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-003 | attrs 白名单未冻结 | 先冻结字段，白名单另行补设计 | 结构体定义、单测 | 仅当最小字段集合与 6.10 一致且测试通过时完成 |
| LOG-TODO-005 | Not Started | 定义 AuditEvent 数据结构 | logging 设计 6.5、6.8、6.10；ADR-008 | 6.5 AuditEvent；6.10 审计字段 | L2 | infra/include/logging/LogTypes.h | AuditEvent{action,actor,target,side_effects,evidence_ref,outcome} | contract：多 Agent 链路标识覆盖；unit：字段必填校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | LOG-TODO-002 | side_effects 子结构未冻结 | 先定义顶层字段并约束不可空关键字段 | 结构体定义、contract 测试 | 仅当字段齐备且 contract 测试可阻止越权扩写时完成 |
| LOG-TODO-006 | Not Started | 实现 LoggingFacade 生命周期骨架 | logging 设计 6.2、6.6、6.7 | 6.2 子组件职责；6.7 主流程 | L2 | infra/src/logging/LoggingFacade.cpp | 初始化前置条件、log 调用主链、flush 出口 | unit：未初始化调用返回可判定失败；正常路径可走通 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-001、LOG-TODO-003、LOG-TODO-004 | 返回类型细节未冻结 | 以现有错误码域封装最小返回模型 | facade 实现骨架、单测 | 仅当未初始化/已初始化两类路径均可二值判定时完成 |
| LOG-TODO-007 | Not Started | 实现 SinkDispatcher 路由骨架 | logging 设计 6.2、6.4、6.7 | 6.4 依赖关系；6.7 正常路径第 5 步 | L2 | infra/src/logging/SinkDispatcher.cpp | level/category -> sink route | unit：路由选择可判定；contract：不泄漏未声明字段 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-004 | sink 类型矩阵未冻结 | 先支持 basic file + audit 路由骨架 | 路由实现骨架、单测 | 仅当普通与审计路由分流可测试验证时完成 |
| LOG-TODO-008 | Not Started | 实现 AsyncQueueController 队列策略骨架 | logging 设计 6.2、6.8、6.9；行业参考 spdlog async；docs/development/InfraConcurrencyPolicy.md | 6.8 队列满策略；6.9 queue_size/overflow_policy | L2 | infra/src/logging/AsyncQueueController.cpp | block/overrun_oldest 策略入口 | unit：队列满策略二值验证；failure-injection：drop 计数可观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-007 | 线程池参数未冻结 | 先按 InfraConcurrencyPolicy 固定 block 与 overrun_oldest 的 backpressure 边界，再接异步线程池 | 控制器骨架、策略测试 | 仅当两种溢出策略都能被测试稳定判定时完成 |
| LOG-TODO-009 | Not Started | 实现 AuditLinkAdapter 审计关联适配骨架 | logging 设计 6.2、6.8、6.10；架构 8.8 | 6.2 AuditLinkAdapter；6.8 审计关联失败处理 | L2 | infra/src/logging/AuditLinkAdapter.cpp | 高风险日志 evidence_ref 关联、失败告警入口 | unit：高风险日志可关联 evidence_ref；contract：审计关联字段完整性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | LOG-TODO-002、LOG-TODO-005、LOG-TODO-007 | 审计关联入参对象未冻结 | 先保留接口占位，不阻塞关联链路骨架实现 | 审计关联适配骨架、合同测试 | 仅当高风险日志与 evidence_ref 关联可验证且失败路径有告警出口时完成 |
| LOG-TODO-010 | Not Started | 定义 LoggingErrors 错误码域 | logging 设计 6.6、6.8 | 6.6 错误语义定义 | L3 | infra/include/logging/LoggingErrors.h | LOG_E_QUEUE_FULL, LOG_E_SINK_IO, LOG_E_FORMAT_INVALID, LOG_E_CONFIG_INVALID | unit：错误码枚举稳定；contract：与 contracts::ResultCode 映射一致 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | LOG-TODO-001、LOG-TODO-002 | 与 contracts 映射矩阵未成文 | 在测试中固化映射表并评审 | 错误码头文件、映射测试 | 仅当四个错误码均有来源锚点且映射测试通过时完成 |
| LOG-TODO-011 | Not Started | 实现 LoggingRecovery 故障降级骨架 | logging 设计 6.8；编码规范 3.6 | 6.8 sink IO/format 异常恢复动作 | L2 | infra/src/logging/LoggingRecovery.cpp | fallback sink, degraded 标记, 周期重试入口 | unit：degraded 状态切换；failure-injection：恢复后状态回清 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | LOG-TODO-009、LOG-TODO-010 | 失败注入桩不足 | 先补 mock sink 与故障注入点 | 恢复骨架、故障注入测试 | 仅当降级触发、恢复成功、恢复失败三类路径可二值判定时完成 |
| LOG-TODO-012 | Blocked | 实现 LoggingConfigAdapter 四层配置适配 | logging 设计 6.6、6.9；架构 8.6 | 6.9 配置项与覆盖层级 | L2 | infra/src/logging/LoggingConfigAdapter.cpp | ILogConfigurator::apply(config) | unit：层级优先级验证；contract：Profile 不绕过审计主链 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | LOG-TODO-001 | LOG-BLK-001 | 完成配置对象模型补设计并冻结键名 | 配置适配实现或阻塞记录 | 仅当配置模型冻结评审通过后，状态才能从 Blocked 改为 Not Started |
| LOG-TODO-013 | Blocked | 实现 LoggingMetricsBridge 指标桥接骨架 | logging 设计 6.2、6.10；架构 8.7 | 6.10 指标清单 | L1 | infra/src/logging/LoggingMetricsBridge.cpp | logging_write_total, logging_write_fail_total, logging_drop_total, logging_queue_depth, logging_flush_latency_ms | unit：指标发射接口调用；integration：与 metrics 子域联通 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | LOG-TODO-010 | LOG-BLK-002 | metrics 侧 exporter 接口与标签约束冻结 | 桥接骨架或阻塞记录 | 仅当 metrics 接口签名冻结后方可推进实现 |
| LOG-TODO-014 | Not Started | 注册 logging 构建落点到 infra CMake | logging 设计 8.1；代码现状 | 8.1 文件落盘建议 | L2 | infra/CMakeLists.txt | 新增 include/logging 与 src/logging 源文件接线 | build：dasall_infra 可编译；unit：新增测试目标可链接 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | LOG-TODO-001 至 LOG-TODO-011 | 源文件渐进落盘导致初期目标为空 | 保留最小 non-empty 源文件并按阶段接线 | CMake 改动、构建记录 | 仅当 placeholder 不再是唯一源码入口且目标编译通过时完成 |
| LOG-TODO-015 | Not Started | 注册 logging 单元与契约测试入口 | logging 设计 8.1、9.1；编码规范 3.7 | 9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt, tests/unit/infra/logging/, tests/contract/CMakeLists.txt | 新增 logging 相关 unit/contract 测试注册 | unit + contract：ctest 可发现并执行新增用例 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | LOG-TODO-003、LOG-TODO-005、LOG-TODO-010 | 无 | 无 | 测试代码、注册入口、执行记录 | 仅当新增测试在 ctest -N 可见且执行通过时完成 |
| LOG-TODO-016 | Not Started | 回写 logging 质量门与交付证据 | logging 设计 9.2、11.1；工程规范 6.2 | 9.2 Gate-LOG-01~05 | L2 | docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md | Gate 执行结论、阻塞变化、回退执行记录 | process test：门禁记录与执行结果可追溯 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | LOG-TODO-015 | 无 | 无 | 更新后的 TODO 证据段 | 仅当每个门禁都有通过/失败结论与证据命令时完成 |

### 6.2 Blocked 任务对应阻塞项索引

| 任务 ID | 对应阻塞项 |
|---|---|
| LOG-TODO-012 | LOG-BLK-001 |
| LOG-TODO-013 | LOG-BLK-002 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 对象与接口冻结 | LOG-TODO-001~005 | 可并行（001/002 与 003/004/005 两组） | 先冻结边界，避免实现期反复改接口 |
| B 主链路骨架 | LOG-TODO-006~009 | 串行 | facade -> dispatcher -> queue -> audit，保持单向依赖 |
| C 错误与恢复 | LOG-TODO-010~011 | 串行 | 先错误码，再降级恢复，便于统一测试断言 |
| D 构建与测试接线 | LOG-TODO-014~015 | 可并行 | CMake 接线和测试注册可并行推进 |
| E 阻塞项解消后推进 | LOG-TODO-012~013 | 串行 | 先配置模型，再指标桥接 |
| F 证据收口 | LOG-TODO-016 | 串行收口 | 统一回写 gate 与阻塞状态 |

### 7.2 必过门禁

| Gate ID | 门禁项 | 通过标准 | 失败处置 |
|---|---|---|---|
| LOG-GATE-01 | 接口冻结门 | ILogger/IAuditLinkAdapter/LogTypes/LoggingErrors 落盘且编译通过 | 回退到接口定义，不推进实现 |
| LOG-GATE-02 | 审计隔离门 | 审计关联与普通日志主链分离有测试证明 | 回退 AuditLinkAdapter，阻断集成推进 |
| LOG-GATE-03 | 异常可观测门 | queue/sink/format/config 失败路径有错误码和计数出口 | 补齐错误处理后再提测 |
| LOG-GATE-04 | 测试发现性门 | ctest -N 能看到 logging 新增 unit/contract 测试 | 修复 CMake 注册 |
| LOG-GATE-05 | breaking 评审门 | 任何接口签名/contracts 映射变化均有评审结论 | 未评审不得合入 |
| LOG-GATE-06 | integration 准入门 | tests 顶层完成 integration 注册策略 | 未通过前禁止开集成验收任务 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| LOG-BLK-001 | logging config 模型未冻结：apply(config) 的 config 结构、冲突裁定规则、运行时 patch 形状不明确 | LOG-TODO-012 | 完成 ILogConfigurator 配置对象补设计评审 | 在 logging 设计文档补充配置对象表与合并规则表 | 暂时固定静态配置，禁用运行时覆盖 |
| LOG-BLK-002 | metrics 子域接口未冻结：指标出口接口、标签白名单、上报失败语义缺失 | LOG-TODO-013 | metrics 侧提供最小 IMetricSink/bridge 接口 | 在 infra/metrics 详细设计补接口章节 | 暂时仅保留内存计数，不对外导出 |
| LOG-BLK-003 | health 探针接口未冻结：LoggingHealthProbe 输出对象和超时语义不明确 | 后续 LoggingHealthProbe 任务 | health 子域给出 IHealthProbe 统一签名 | 在 infra/health 详细设计补探针对象定义 | 暂时仅记录 degraded 本地状态 |
| LOG-BLK-004 | tests 顶层未纳入 integration 子目录，无法稳定注册 logging integration 用例 | 后续 integration 任务 | tests/CMakeLists.txt 接入 integration 并定义标签约定 | 新增 add_subdirectory(integration) 与 integration 标签规范 | integration 验收延期，仅执行 unit/contract |
| LOG-BLK-005 | LogQueryService 查询模型与权限边界未冻结 | 后续 LogQueryService 任务 | 定义 query 对象、授权策略、导出约束 | 在 logging 设计文档补 query schema 与权限表 | 仅保留文件级导出，不开放按 trace/session 检索 API |

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

1. integration 命令当前不纳入验收基线，原因见 LOG-BLK-004。
2. 每项任务验收最少需要一条 build 命令和一条 test 命令。

### 9.2 质量门逐项回答（第 7 章要求）

1. 是否给出 Design -> TODO 映射：是。
2. 是否明确当前最细粒度等级：是，L2 为主，局部 L3。
3. 是否全部任务具备代码目标 + 测试目标 + 验收命令：是。
4. 是否所有 Blocked 项带证据和解阻条件：是。
5. 是否所有任务可二值判定：是。
6. 是否避免跨子系统范围扩张：是。
7. 是否真正落到接口/数据结构级对象：是。

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
2. 当前仓库中 logging 代码与 include 为空，必须先接口与对象冻结。
3. tests 顶层未纳入 integration，集成任务存在工程级阻塞。
4. metrics/health/config 的 logging 侧接入点存在签名缺口，不满足安全 L3 条件。
5. ADR-005/006/007/008 对边界限制明确，禁止越权扩张。

### 11.3 当前最小可执行粒度

接口级与数据结构级（L2），局部可达函数语义级（L3，仅限字段与错误码定义任务）。

### 11.4 未达全量函数级的缺失信息

1. ILogConfigurator 的 config/patch 对象定义。
2. metrics/health 桥接接口签名与标签治理规则。
3. integration 测试注册拓扑。
4. LogQueryService 查询对象与权限边界。

### 11.5 下一步建议

1. 先执行 LOG-TODO-001~011、014~016，完成 logging L2 冻结与构建/测试接线。
2. 并行推进 LOG-BLK-001~005 的补设计解阻。
3. 解阻后再生成 logging 集成与桥接的下一轮专项 TODO，不跳过评审门禁。