# DASALL contracts 冻结实施计划

## 1. 文档定位

本文档用于指导 DASALL Agent 的 contracts 层从“目录骨架已存在、对象尚未冻结”的状态，进入正式的详细设计、评审、冻结和验证阶段。

本文档不承担代码实现说明，而是作为 contracts 设计阶段的正式实施计划，回答以下问题：

1. 为什么 contracts 必须优先于其他模块设计。
2. contracts 应如何组织实施，整体先行还是逐目录推进。
3. 每一轮应冻结哪些对象、达到什么准入标准、如何避免返工。
4. 如何把现有架构文档、工程蓝图、ADR 和行业实践转化为可执行的工作包。

---

## 2. 背景与问题定义

当前仓库已经具备 contracts 骨架目录，但核心对象尚未正式落地。与此同时，总体架构、工程蓝图与三份关键边界 ADR 已经完成，意味着 contracts 已进入“可设计但不可再拖延”的阶段。

现状特征如下：

1. 总体架构已明确“契约优先”，要求先冻结数据契约与错误语义，再实现功能。
2. 工程蓝图已给出 contracts 的目录结构、关键对象清单和测试要求。
3. ADR-006 已冻结 ContextPacket 与 PromptComposeRequest/Result 的边界。
4. ADR-007 已冻结 ReflectionDecision、RecoveryOutcome、Checkpoint 等恢复相关契约的边界约束。
5. ADR-008 已冻结 MultiAgentRequest、MultiAgentResult、WorkerTask、WorkerLease 与顶层请求之间的层次关系。
6. 当前 contracts 若继续缺位，后续 runtime、cognition、memory、llm、tools、multi_agent 的详细设计将缺少共同语言，最终把返工放大到主流程与测试基线。

因此，本计划的目标不是“把头文件填满”，而是系统性地冻结一套能够支撑单 Agent 主链路、失败恢复路径和多 Agent 协同扩展的稳定契约基线。

---

## 3. 设计依据

### 3.1 仓库内依据

本实施计划主要依据以下材料：

1. [DASSALL_Agent_architecture.md](/home/gangan/DASALL-Agent/docs/architecture/DASSALL_Agent_architecture.md)
2. [DASALL_Engineering_Blueprint.md](/home/gangan/DASALL-Agent/docs/architecture/DASALL_Engineering_Blueprint.md)
3. [ADR-005-architecture-review-baseline.md](/home/gangan/DASALL-Agent/docs/adr/ADR-005-architecture-review-baseline.md)
4. [ADR-006-context-orchestrator-vs-prompt-composer.md](/home/gangan/DASALL-Agent/docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md)
5. [ADR-007-reflection-engine-vs-recovery-manager.md](/home/gangan/DASALL-Agent/docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md)
6. [ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md](/home/gangan/DASALL-Agent/docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md)
7. [DASALL_工程落地实现步骤指引.md](/home/gangan/DASALL-Agent/docs/todos/DASALL_工程落地实现步骤指引.md)
8. [DASALL_工程协作与编码规范.md](/home/gangan/DASALL-Agent/docs/development/DASALL_工程协作与编码规范.md)

### 3.2 行业资料依据

为降低 contracts 设计阶段的局部最优和后续演进风险，本计划吸收以下行业实践：

1. Azure API Design Guidance
结论：契约应作为稳定边界存在，不暴露内部实现；应显式定义 schema 或 IDL；兼容性与版本演进必须前置考虑；幂等与异步语义应通过契约体现，而不是靠实现猜测。

2. Proto Best Practices
结论：面向长期演进的契约必须避免破坏性字段变更；不得轻易改变字段类型、默认值和重复性；新增字段优于修改旧字段；枚举必须保留未指定值；消息应按单一职责拆分，避免单个对象膨胀为超大结构。

3. Azure Saga Pattern
结论：复杂工作流中的状态、步骤、补偿和可重试语义必须有显式契约；恢复与补偿依赖 durable state 和可追踪的步骤语义，不能依赖模块内部隐式状态。

4. Consumer-Driven Contracts Pattern
结论：契约不只是 provider 自说自话的 schema，还应围绕真实消费方的最小必需需求建立稳定约束；契约测试应尽量围绕消费方真实依赖构建，避免“全字段刚性耦合”导致的进化停滞。

上述行业依据不会直接决定 DASALL 的对象命名，但会直接影响本计划中的版本策略、对象拆分方式、兼容性准则和 contract test 设计。

---

## 4. 设计目标

本计划要求在 contracts 设计阶段达成以下目标：

1. 建立统一的跨模块对象语言，使 apps、runtime、cognition、memory、llm、tools、multi_agent 使用同一组稳定对象协作。
2. 将主流程、异常流程、恢复流程和多 Agent 流程全部映射到显式对象，而不是依赖文档描述和模块内部约定。
3. 通过对象边界实现对 ADR-006、ADR-007、ADR-008 的工程落地，防止边界重新漂移。
4. 为后续模块详细设计、测试 Mock、契约测试和实现代码建立稳定依赖面。
5. 形成可演进的版本策略，允许后续以兼容优先方式扩展，而非频繁破坏性修改。

---

## 5. 设计范围与非目标

### 5.1 本阶段纳入范围

本阶段纳入以下内容：

1. contracts 目录结构下所有核心对象的分层设计。
2. 错误语义、预算语义、恢复语义、协同语义的顶层约定。
3. 主流程与异常流程对应对象的职责边界。
4. contracts 的版本、兼容性、字段演进规则。
5. contract test 的策略、覆盖范围和 Gate 标准。

### 5.2 本阶段不纳入范围

本阶段不纳入以下内容：

1. 具体模块实现类与算法实现。
2. runtime、memory、llm、tools、multi_agent 的详细执行逻辑。
3. 序列化框架的最终落地代码实现。
4. 跨进程、跨网络传输协议的最终技术选型落地。

说明：序列化和 IDL 相关约束可以在 contracts 设计中先定义原则与保留位，但不在本文档中直接绑定具体库。

---

## 6. 核心设计判断

### 6.1 不是逐目录独立设计，而是整体骨架先行

contracts 不能按单个子目录彼此独立推进，原因如下：

1. AgentRequest、GoalContract、ContextPacket、Observation、Checkpoint、ReflectionDecision、MultiAgentRequest 等对象天然跨越多个模块。
2. 如果先分别设计 context、policy、task、checkpoint，各目录会各自携带预算、错误、状态和追踪字段，最终产生重复与冲突。
3. 当前三份边界 ADR 的本质就是在防止“不同模块各自定义自己的中间对象”。

因此推荐策略是：

1. 先整体设计 contracts 对象地图。
2. 再按依赖顺序冻结对象波次。
3. 最后才把对象归入各子目录和头文件。

### 6.2 不是一次性全量细节冻结，而是分波次冻结

如果一次性设计全部字段，会带来两个问题：

1. 过早绑定尚未细化的模块实现细节。
2. 难以聚焦真正高扇出的核心对象，评审效率极低。

因此推荐采用“骨架先行、分波次冻结”的方法：

1. 先冻结横切基础对象与主链路对象。
2. 再冻结受 ADR 直接约束的边界对象。
3. 最后细化工具、Prompt、记忆、多 Agent 等子域对象。

### 6.3 不是 schema 优先于语义，而是语义优先于 schema

contracts 设计首先要回答“对象在系统中代表什么语义”，然后才回答“字段如何组织”。

例如：

1. Observation 与 ObservationDigest 的边界必须先明确，再谈字段结构。
2. ReflectionDecision 是语义建议还是执行控制，必须先由 ADR 确定，再谈字段。
3. WorkerTask 与 AgentRequest 的层级关系必须先明确，再谈 parent_task_id、lease_id 等字段。

---

## 7. contracts 总体对象地图

建议以以下对象链作为 contracts 的顶层设计骨架：

1. 入口链路：AgentRequest -> GoalContract -> SessionContext 摘要
2. 上下文链路：GoalContract + Session/Memory/Knowledge -> ContextPacket
3. 决策链路：ContextPacket + Observation -> ActionDecision / Clarification / Replan Hint
4. 执行链路：ActionDecision -> ToolRequest / WorkerTask / External Action
5. 观测链路：ToolResult / WorkerResult / RetrievalResult / HumanInput -> Observation -> ObservationDigest
6. 恢复链路：Observation + ErrorInfo + Checkpoint -> ReflectionDecision -> RecoveryOutcome
7. 输出链路：ContextPacket + Final Observations + Plan Status -> AgentResult
8. 协同链路：Goal Fragment + Plan Fragment -> MultiAgentRequest -> WorkerTask -> MultiAgentResult

这张对象地图必须在任何子目录设计前先被团队评审确认，因为它决定了哪些对象是真正的“系统流通货币”。

---

## 8. 分阶段实施方案

### 阶段 0：准备与术语收敛

目标：统一术语、范围和评审基线，避免对象定义从一开始就带着语义漂移。

工作内容：

1. 统一术语表：Goal、GoalContract、Observation、ObservationDigest、ActionDecision、ReflectionDecision、RecoveryOutcome、WorkerTask、MultiAgentResult、Checkpoint。
2. 明确 contracts 边界：哪些对象属于跨模块稳定契约，哪些只是模块内部结构。
3. 固定命名原则：对象名使用领域语义，不使用实现细节导向命名。
4. 固定字段分层原则：顶层对象不暴露下层模块内部结构。

产出：

1. contracts 术语表
2. contracts 对象地图草案
3. 设计边界说明

Gate：

1. 关键术语不存在一词多义。
2. 团队确认 ContextPacket、Observation、Checkpoint、WorkerTask 的基本语义一致。

### 阶段 1：横切基础对象冻结

目标：建立全目录共享的最小基础语言，防止每个子目录重复发明共性字段。

优先对象：

1. error/ResultCode
2. error/ErrorInfo
3. event/EventType
4. event/EventEnvelope 基础头部
5. checkpoint/RuntimeBudget
6. 通用标识与追踪元数据，例如 request_id、session_id、trace_id、task_id、parent_task_id、lease_id 的约定
7. 通用时间与超时表达

设计要点：

1. ErrorInfo 必须可表达 failure_type、retryable、safe_to_replan、details、来源引用。
2. RuntimeBudget 不只表达 token，还要表达轮次、工具调用次数、延迟等顶层预算。
3. EventEnvelope 只定义通用封套，不嵌入模块专属 payload 细节。
4. 所有枚举必须预留未指定值，便于向后兼容扩展。

行业落地要求：

1. 采用兼容优先原则，新增字段优于修改旧字段。
2. 禁止通过删除旧字段来“清理对象”。废弃只能走 deprecate 和版本迁移。
3. 枚举必须具备未指定默认值，布尔字段慎用，凡是未来可能扩展为多态状态的字段优先使用 enum。

产出：

1. 横切基础对象清单
2. 兼容性规则 v1
3. 枚举与错误码规范

Gate：

1. 任意子域对象不再需要自行发明 error、budget、id、time 语义。
2. 兼容性规则被明确写成 review checklist。

### 阶段 2：主链路最小闭环对象冻结

目标：优先打通单 Agent 主流程的契约语言，先服务系统最小闭环。

优先对象：

1. agent/AgentRequest
2. agent/AgentResult
3. agent/GoalContract
4. observation/Observation
5. observation/ObservationDigest
6. observation/BeliefState
7. context/ContextPacket
8. checkpoint/Checkpoint

设计要点：

1. AgentRequest 必须只表达统一入口请求语义，不夹带 runtime 内部状态。
2. GoalContract 必须冻结成功判据、约束、预算和审批策略，避免自然语言二次猜测。
3. Observation 必须是 Tool、Knowledge、Human、Worker 输出的统一折叠对象。
4. ObservationDigest 必须与 Observation 分层，前者面向后续推理消费，后者保留更完整执行语义。
5. ContextPacket 必须是共享语义上下文对象，而不是消息渲染对象。
6. Checkpoint 必须只表达恢复所需最小状态，不变成工作内存总快照的无限容器。

与仓库约束的直接映射：

1. [DASSALL_Agent_architecture.md](/home/gangan/DASALL-Agent/docs/architecture/DASSALL_Agent_architecture.md) 已要求主流程和异常流程使用同一套契约。
2. [ADR-006-context-orchestrator-vs-prompt-composer.md](/home/gangan/DASALL-Agent/docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md) 已要求 ContextPacket 不包含 final_messages、provider_payload、rendered_prompt。

产出：

1. 主链路对象定义草案
2. 主流程对象流转图
3. 单 Agent 最小闭环契约集

Gate：

1. 能够用这组对象完整描述 CLI 到 runtime 到 cognition 到 tool 到 response 的最小闭环。
2. 不再需要通过口头补充解释对象职责。

### 阶段 3：按 ADR 冻结边界对象

目标：把已经完成裁定的边界正式落到 contracts 上，防止后续模块详细设计重新争夺职责。

优先对象：

1. prompt/PromptComposeRequest
2. prompt/PromptComposeResult
3. policy/ReflectionDecision
4. recovery 相关输出对象，建议归入 policy 或 checkpoint 邻近目录的恢复结果对象，例如 RecoveryOutcome
5. task/MultiAgentRequest
6. task/MultiAgentResult
7. task/WorkerTask
8. task/WorkerLease

设计要点：

1. PromptComposeRequest 只能消费 ContextPacket，不能替代 ContextPacket。
2. ReflectionDecision 只表达语义判断，不表达 backoff、retry_after、circuit breaker 等执行细节。
3. RecoveryOutcome 只表达执行结果与控制元数据，不回写失败归因语义。
4. MultiAgentRequest 不复用 AgentRequest，必须只表达协同子域请求。
5. MultiAgentResult 不直接等于 AgentResult，只回传协同结果与冲突信息。
6. WorkerTask 只表达子任务执行单元，不承载全局 Session/FSM 语义。

与 ADR 的直接映射：

1. [ADR-006-context-orchestrator-vs-prompt-composer.md](/home/gangan/DASALL-Agent/docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md)
2. [ADR-007-reflection-engine-vs-recovery-manager.md](/home/gangan/DASALL-Agent/docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md)
3. [ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md](/home/gangan/DASALL-Agent/docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md)

产出：

1. 边界对象定义草案
2. ADR 到对象字段的映射表
3. 边界冲突检查单

Gate：

1. 三份 ADR 中提到的 contracts 影响项全部有对象承接。
2. 不存在一个字段同时承担两个边界主体的职责。

### 阶段 4：子域对象细化

目标：在整体骨架与边界稳定后，细化各子域目录中的对象。

子域顺序建议：

1. tool
2. prompt
3. memory
4. task
5. event
6. llm

说明：

1. tool 提前，是因为 ToolRequest、ToolResult、ToolDescriptor、ToolIR 会直接影响 Observation 构建和治理链路。
2. prompt 提前，是因为 PromptComposeRequest/Result 已在阶段 3 确定边界，后续 PromptSpec/PromptRelease 需要承接它们。
3. memory 放在其后，是因为 Turn、Session、SummaryMemory、MemoryFact 需依赖已经稳定的 Observation、ContextPacket 和 Checkpoint。
4. task 继续细化，是因为多 Agent 子域对象已经在阶段 3 先冻结了高扇出边界。
5. llm 放后，是因为 LLMRequest/LLMResponse 与具体 provider 适配更近，应尽量避免污染前面已经稳定的共享语义对象。

每个子域对象设计都应满足：

1. 不重复定义横切基础字段。
2. 不逆向修改主链路对象边界。
3. 尽量使用小对象组合，而不是把所有信息塞进单个超大结构。

产出：

1. 各子目录对象设计稿
2. 目录级字段说明
3. 子域对象间依赖表

Gate：

1. 每个子目录对象都能回溯到阶段 1 到阶段 3 的骨架对象。
2. 没有子域对象越权承担主链路职责。

### 阶段 5：接口抽象与 contracts 测试设计

目标：在数据对象稳定后，才决定哪些接口抽象应进入 contracts，并建立契约测试基线。

接口设计原则：

1. 只有真正跨模块且需要作为稳定依赖面的抽象接口，才进入 contracts。
2. 不要为了“看起来解耦”而把所有接口都塞进 contracts。
3. 数据对象稳定前，不设计 I 开头接口。

测试设计原则：

1. 契约测试覆盖所有核心对象的序列化与反序列化稳定性。
2. 对错误码、事件格式、枚举演进、可选字段缺失进行兼容性测试。
3. 对主链路对象采用“just enough validation”策略，验证消费方真实依赖，而不是强迫所有测试耦合全部字段。
4. 对受 ADR 约束对象增加边界测试，例如 ContextPacket 不得出现消息层字段，ReflectionDecision 不得出现调度字段。

产出：

1. IXxx 接口候选清单
2. tests/contract 覆盖矩阵
3. 版本兼容测试规则

Gate：

1. 核心对象全部有对应 contract tests。
2. 每个 breaking change 都能被测试或 checklist 阻止。

---

## 9. 子目录设计优先级与工作包建议

建议将 contracts 设计拆成以下工作包：

### WP-01：术语与对象地图

范围：

1. 术语表
2. 顶层对象流图
3. 设计边界说明

完成标准：

1. 评审通过整体对象地图。

### WP-02：横切基础对象

范围：

1. error/
2. event/ 的通用封套部分
3. checkpoint/ 中 RuntimeBudget 基础部分

完成标准：

1. 错误码、预算、标识、时间表达统一。

### WP-03：主链路对象

范围：

1. agent/
2. context/
3. observation/
4. checkpoint/ 中 Checkpoint 主体

完成标准：

1. 单 Agent 最小闭环对象可完整描述。

### WP-04：边界对象

范围：

1. prompt/
2. policy/
3. task/ 中 MultiAgentRequest、MultiAgentResult、WorkerTask、WorkerLease

完成标准：

1. 三份 ADR 的 contracts 影响项全部落实。

### WP-05：子域细化与 contract tests

范围：

1. tool/
2. memory/
3. llm/
4. event/ 的具体事件扩展
5. tests/contract

完成标准：

1. contracts V1 可进入实现消费阶段。

---

## 10. 版本策略与兼容性策略

### 10.1 总体原则

1. 向后兼容优先于结构清爽。
2. 新增字段优于修改旧字段。
3. 废弃优于删除。
4. 改语义必须视为重大变更，即使字段名不变。

### 10.2 字段演进规则

1. 不随意修改字段类型。
2. 不把可选字段改为强制字段。
3. 不把多值字段收窄为单值字段。
4. 枚举必须保留未指定值。
5. 不使用语义可能持续扩张的布尔字段表达状态机。

### 10.3 对象拆分规则

1. 一个对象字段过多时，应优先拆成组合对象。
2. RPC 或消息传输对象不应直接兼做长期存储对象。
3. 顶层共享对象应单文件定义，降低跨目录依赖。

### 10.4 版本管理规则

1. contracts 采用语义版本号管理，至少区分 breaking 与 non-breaking 变更。
2. 每次计划中的对象冻结都要记录版本变更说明。
3. 一旦进入实现消费期，breaking change 必须经过专门评审，并更新迁移说明。

---

## 11. 评审与治理机制

建议采用三轮正式评审，而不是试图一次评完所有目录：

### 第一轮：整体骨架评审

关注点：

1. 术语是否收敛。
2. 对象流图是否完整覆盖主流程和异常流程。
3. 高扇出对象是否识别完整。

### 第二轮：主链路与边界对象评审

关注点：

1. AgentRequest、GoalContract、ContextPacket、Observation、Checkpoint 是否稳定。
2. ADR-006、007、008 对应的对象边界是否严格执行。
3. 是否仍存在双主控、双上下文主控、双恢复主控的迹象。

### 第三轮：子域对象与测试评审

关注点：

1. tool、memory、llm、task 等目录是否只做细化而未改动骨架。
2. contract tests 是否覆盖高风险对象。
3. 兼容性和版本策略是否可执行。

---

## 12. 风险与缓解措施

### 风险 1：整体设计不足，直接进入逐目录写字段

后果：

1. 每个目录各自发明 id、budget、error、status 字段。
2. ContextPacket、Observation、Checkpoint、WorkerTask 多次返工。

缓解措施：

1. 阶段 0 和阶段 1 不得跳过。
2. 先画对象流图，再写目录级设计稿。

### 风险 2：对象过载，contracts 变成实现细节转储层

后果：

1. contracts 失去稳定性。
2. 下游模块对实现细节产生硬耦合。

缓解措施：

1. 每个对象评审必须回答“它是否真的跨模块共享”。
2. 超大对象一律拆分或退回模块内部结构。

### 风险 3：ADR 已冻结边界在 contracts 中被悄悄回退

后果：

1. ContextPacket 混入消息字段。
2. ReflectionDecision 混入执行控制字段。
3. MultiAgentResult 试图直接承担 AgentResult 角色。

缓解措施：

1. 在第二轮评审中增加 ADR 对象映射检查单。
2. 为受 ADR 约束对象写专门 contract tests。

### 风险 4：兼容性策略缺失，后续实现期频繁 breaking

后果：

1. 模块实现与测试桩同步返工。
2. contract tests 失去意义。

缓解措施：

1. 先定义版本与变更规则。
2. 每次对象冻结都记录兼容性说明。

### 风险 5：过早绑定具体序列化技术

后果：

1. 设计被技术细节牵引。
2. 尚未稳定的对象被迫适配具体框架限制。

缓解措施：

1. 先冻结语义对象与字段层级。
2. 序列化技术选型作为后续单独决策，但兼容性原则提前冻结。

---

## 13. 准入标准

只有满足以下条件，才建议把 contracts 视为首轮冻结完成，并允许后续模块以其为基线展开详细设计：

1. 顶层对象流图已完成并通过评审。
2. 横切基础对象已冻结。
3. 主链路对象已冻结并能表达单 Agent 最小闭环。
4. ADR-006、007、008 的 contracts 影响项全部有对象承接。
5. contract tests 基线已建立，至少覆盖序列化稳定性、错误码一致性、事件封套和受 ADR 约束字段边界。
6. contracts 变更流程与版本规则已明确。

---

## 14. 推荐执行顺序

综合仓库现状与行业实践，推荐立即按以下顺序推进：

1. 先完成 WP-01：术语与对象地图。
2. 再完成 WP-02：横切基础对象。
3. 再完成 WP-03：主链路对象。
4. 再完成 WP-04：边界对象。
5. 最后完成 WP-05：子域细化与 contract tests。

其中：

1. WP-01 到 WP-04 必须串行主导推进。
2. WP-05 可在骨架稳定后分子域并行，但不能回改前四个工作包的已冻结结论。

---

## 15. 下一步建议

本文档落地后，建议立刻启动以下两项工作，而不是直接开始写头文件：

1. 先输出一份 contracts 总体设计说明，内容包括术语表、对象地图、目录职责说明和兼容性规则。
2. 先完成 WP-01 和 WP-02 的评审稿，再进入具体对象字段设计。

如果跳过这两步，直接开始按目录写结构体，最终大概率会在 ContextPacket、Observation、Checkpoint、ReflectionDecision、WorkerTask 这些对象上集中返工。

---

文档版本：v1.0
日期：2026-03-13
状态：Draft for review