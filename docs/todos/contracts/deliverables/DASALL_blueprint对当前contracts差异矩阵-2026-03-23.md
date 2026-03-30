# DASALL blueprint 对当前 contracts 差异矩阵

> 版本：1.0 | 日期：2026-03-23 | 基线：Blueprint vs 当前 `contracts/include/`

---

## 1. 文档目的

本矩阵用于以 [docs/architecture/DASALL_Engineering_Blueprint.md](docs/architecture/DASALL_Engineering_Blueprint.md) 中的 contracts 对象清单为基准，逐对象比对当前 `contracts/include/` 的实际落地情况，区分以下状态。

配套架构说明文档：

1. [docs/architecture/DASALL_contracts目录设计说明.md](docs/architecture/DASALL_contracts%E7%9B%AE%E5%BD%95%E8%AE%BE%E8%AE%A1%E8%AF%B4%E6%98%8E.md)：解释为什么 contracts 对象不放在子系统目录，以及 src/ 无实现的设计原因。
2. [docs/architecture/DASALL_boundary治理与优化说明.md](docs/architecture/DASALL_boundary%E6%B2%BB%E7%90%86%E4%B8%8E%E4%BC%98%E5%8C%96%E8%AF%B4%E6%98%8E.md)：解释 boundary 目录的四类治理职责与 Checklist Gate 存在原因。
3. [docs/todos/contracts/DASALL_contracts验收整改TODO.md](docs/todos/contracts/DASALL_contracts%E9%AA%8C%E6%94%B6%E6%95%B4%E6%94%B9TODO.md)：矩阵中 Missing / Replaced-Partial 项的原子整改任务（T008-T011，T016-T019）。

状态定义：

1. Implemented：已按对象或同名对象落地。
2. Replaced/Partial：已由替代对象或部分对象承载，但未与 blueprint 完全同名或同职责闭合。
3. Deferred：当前明确延期，已在目录、接口或 TODO 中显式记录。
4. Missing：当前未见对象或正式替代关系。

---

## 2. 状态汇总

| 状态 | 数量 | 说明 |
|---|---|---|
| Implemented | 28 | 已形成对象定义并进入现有 contracts 基线 |
| Replaced / Partial | 1 | 已有替代对象或部分实现，但与 blueprint 不完全一致 |
| Deferred | 0 | 逐对象矩阵当前未单列 Deferred 项，延期信息主要体现在 supporting contracts 与 interface admission 证据 |
| Missing | 15 | 当前未见对象定义或正式替代关系 |

说明：上述计数以 blueprint 对象表为基准，不含 guards、tag、catalog、checklist 等辅助资产。

---

## 3. 逐对象差异矩阵

| Blueprint 子目录 | Blueprint 对象 | 当前状态 | 当前代码/证据 | 差异说明 | 建议动作 |
|---|---|---|---|---|---|
| agent | AgentRequest | Implemented | [contracts/include/agent/AgentRequest.h](contracts/include/agent/AgentRequest.h) | 已落地并具备 guard/test | 保持 |
| agent | AgentResult | Implemented | [contracts/include/agent/AgentResult.h](contracts/include/agent/AgentResult.h) | 已落地并具备 guard/test | 保持 |
| agent | AgentInitConfig | Missing | 当前未见同名对象 | 启动配置对象尚未进入 contracts | 纳入后续 blueprint 收敛 |
| agent | ResumeToken | Missing | 当前未见同名对象 | resume 语义当前更多由 Checkpoint/Recovery 承载，但未形成独立 token 对象 | 新增或建立正式替代映射 |
| context | ContextPacket | Implemented | [contracts/include/context/ContextPacket.h](contracts/include/context/ContextPacket.h) | 已落地且 ADR-006 回归测试齐备 | 保持 |
| context | ContextAssembleRequest | Missing | 当前未见同名对象 | ContextOrchestrator 输入对象尚未显式化 | 新增对象 |
| context | ContextAssembleResult | Missing | 当前未见同名对象 | ContextOrchestrator 输出与 ContextPacket 之间仍缺中间对象 | 新增对象或正式说明直接输出 ContextPacket |
| context | CompressionRequest | Missing | 当前未见同名对象 | 历史压缩/预算裁剪未形成显式 contract | 新增对象 |
| context | CompressionResult | Missing | 当前未见同名对象 | 压缩输出结果未形成显式 contract | 新增对象 |
| observation | Observation | Implemented | [contracts/include/observation/Observation.h](contracts/include/observation/Observation.h) | 已落地 | 保持 |
| observation | ObservationDigest | Implemented | [contracts/include/observation/ObservationDigest.h](contracts/include/observation/ObservationDigest.h) | 已落地 | 保持 |
| observation | BeliefState | Implemented | [contracts/include/agent/BeliefState.h](contracts/include/agent/BeliefState.h) | 对象已落地，但目录位于 `agent/` 非 `observation/` | 可接受，建议在 blueprint 映射中注明 |
| memory | Turn | Implemented | [contracts/include/memory/Turn.h](contracts/include/memory/Turn.h) | 已落地 | 保持 |
| memory | Session | Implemented | [contracts/include/memory/Session.h](contracts/include/memory/Session.h) | 已落地 | 保持 |
| memory | SummaryMemory | Implemented | [contracts/include/memory/SummaryMemory.h](contracts/include/memory/SummaryMemory.h) | 已落地 | 保持 |
| memory | MemoryFact | Implemented | [contracts/include/memory/MemoryFact.h](contracts/include/memory/MemoryFact.h) | 已落地 | 保持 |
| memory | SessionSnapshot | Missing | 当前未见同名对象 | Session 与 Checkpoint 之间缺少稳定快照对象 | 新增对象 |
| tool | ToolRequest | Implemented | [contracts/include/tool/ToolRequest.h](contracts/include/tool/ToolRequest.h) | 已落地 | 保持 |
| tool | ToolResult | Implemented | [contracts/include/tool/ToolResult.h](contracts/include/tool/ToolResult.h) | 已落地 | 保持 |
| tool | ToolDescriptor | Implemented | [contracts/include/tool/ToolDescriptor.h](contracts/include/tool/ToolDescriptor.h) | 已落地 | 保持 |
| tool | ToolIR | Implemented | [contracts/include/tool/ToolIR.h](contracts/include/tool/ToolIR.h) | 已落地 | 保持 |
| tool | ToolRoute | Missing | 当前未见同名对象 | 路由语义仍由实现侧或 descriptor/registry 隐含承载 | 新增对象或正式映射 |
| tool | CompensationAction | Missing | 当前未见同名对象 | blueprint 的补偿动作对象尚未显式化 | 新增对象 |
| llm | LLMRequest | Implemented | [contracts/include/llm/LLMRequest.h](contracts/include/llm/LLMRequest.h) | 已落地 | 保持 |
| llm | LLMResponse | Implemented | [contracts/include/llm/LLMResponse.h](contracts/include/llm/LLMResponse.h) | 已落地 | 保持 |
| llm | ModelRoute | Missing | 当前仅有 `LLMRequest.model_route` 字段，[contracts/include/llm/LLMRequest.h](contracts/include/llm/LLMRequest.h#L36-L38) | 具备字符串字段，但未形成独立对象 | 新增对象或正式确认无需独立对象 |
| llm | StreamHandle | Missing | 当前未见同名对象 | streaming 生命周期句柄未进入 contracts | 新增对象 |
| prompt | PromptSpec | Implemented | [contracts/include/prompt/PromptSpec.h](contracts/include/prompt/PromptSpec.h) | 已落地 | 保持 |
| prompt | PromptRelease | Implemented | [contracts/include/prompt/PromptRelease.h](contracts/include/prompt/PromptRelease.h) | 已落地 | 保持 |
| prompt | PromptComposeRequest | Implemented | [contracts/include/prompt/PromptComposeRequest.h](contracts/include/prompt/PromptComposeRequest.h) | 已落地 | 保持 |
| prompt | PromptComposeResult | Implemented | [contracts/include/prompt/PromptComposeResult.h](contracts/include/prompt/PromptComposeResult.h) | 已落地 | 保持 |
| policy | PolicyDecision | Missing | 当前未见同名对象 | policy 决策尚未形成共享对象 | 新增对象 |
| policy | PromptPolicyDecision | Missing | 当前未见同名对象 | prompt policy 决策尚未形成共享对象 | 新增对象 |
| policy | ReflectionDecision | Implemented | [contracts/include/checkpoint/ReflectionDecision.h](contracts/include/checkpoint/ReflectionDecision.h) | 对象已落地，但目录位于 `checkpoint/` 非 `policy/` | 需在映射中注明目录漂移 |
| task | TaskRequest | Missing | 当前未见同名对象 | 顶层任务请求对象未显式化 | 新增对象 |
| task | TaskState | Missing | 当前未见同名对象 | 顶层任务状态未显式化 | 新增对象 |
| task | TaskGraph | Replaced / Partial | [contracts/include/task/TaskDomainContracts.h](contracts/include/task/TaskDomainContracts.h) | 当前只落地 `SubTaskGraph`，覆盖协同子图而非通用 `TaskGraph` | 新增通用对象或正式说明替代边界 |
| task | WorkerTask | Implemented | [contracts/include/task/WorkerTask.h](contracts/include/task/WorkerTask.h) | 已落地 | 保持 |
| event | EventEnvelope | Implemented | [contracts/include/event/EventEnvelope.h](contracts/include/event/EventEnvelope.h) | 已落地 | 保持 |
| event | EventType | Implemented | [contracts/include/event/EventType.h](contracts/include/event/EventType.h) | 已落地 | 保持 |
| error | ErrorInfo | Implemented | [contracts/include/error/ErrorInfo.h](contracts/include/error/ErrorInfo.h) | 已落地 | 保持 |
| error | ResultCode | Implemented | [contracts/include/error/ResultCode.h](contracts/include/error/ResultCode.h) | 已落地 | 保持 |
| checkpoint | Checkpoint | Implemented | [contracts/include/checkpoint/Checkpoint.h](contracts/include/checkpoint/Checkpoint.h) | 已落地 | 保持 |
| checkpoint | RuntimeBudget | Implemented | [contracts/include/checkpoint/RuntimeBudget.h](contracts/include/checkpoint/RuntimeBudget.h) | 已落地 | 保持 |

---

## 4. admission / supporting contracts 侧证据

接口与 supporting contracts 现状表明，contracts 冻结仍处于“V1 基线已成、全量蓝图尚未收口”状态：

1. 当前 `InterfaceCatalog` 仅将 `IToolManager` 与 `ILLMAdapter` 标记为 `ReviewReady`，[contracts/include/boundary/InterfaceCatalog.h](contracts/include/boundary/InterfaceCatalog.h#L82-L91)。
2. `IPlanner`、`IMemoryStore`、`IContextOrchestrator`、`IKnowledgeService`、`IExecutionService`、`IDataService`、`IAgentRegistry`、`IResultMerger` 仍处于 `AwaitingSupportingContracts`，[contracts/include/boundary/InterfaceCatalog.h](contracts/include/boundary/InterfaceCatalog.h#L73-L154)。
3. 对应 TODO 也明确写明了 `2 个 Admit + 8 个 Postpone`，[docs/todos/contracts/WP-05-子域细化与ContractTestsTODO.md](docs/todos/contracts/WP-05-%E5%AD%90%E5%9F%9F%E7%BB%86%E5%8C%96%E4%B8%8EContractTestsTODO.md#L75)。

---

## 5. 差异分析

### 5.1 已达到较高收敛度的区域

1. 主链路对象
2. ADR 边界对象
3. Tool / Prompt / Memory / LLM / Event / Error 的 V1 子域对象

### 5.2 明显差异区域

1. context 中间对象
2. policy 子域对象
3. 通用 task 对象
4. llm/tool 的路由与流式句柄对象
5. agent/memory 的配置快照与 resume token 对象

### 5.3 差异性质判断

当前差异并不表明已有对象设计错误，而更像是：

1. V1 冻结刻意缩小了实现面。
2. blueprint 作为全景蓝图，范围大于本次冻结计划。
3. 一部分对象被“字段化”了，但尚未“对象化”。
4. 一部分对象被“子域化替代”了，但尚未形成正式映射文件。

---

## 6. 收敛建议

### 6.1 第一优先级

1. `ResumeToken`
2. `SessionSnapshot`
3. `ModelRoute`
4. `AgentInitConfig`

原因：对象耦合度较低，且能直接提高 blueprint 对齐度。

### 6.2 第二优先级

1. `ContextAssembleRequest`
2. `ContextAssembleResult`
3. `CompressionRequest`
4. `CompressionResult`
5. `PolicyDecision`
6. `PromptPolicyDecision`

原因：这些对象影响后续 `memory/`、`llm/`、`cognition/`、`runtime/` 的接口边界，应在子系统深化前显式化。

### 6.3 第三优先级

1. `TaskRequest`
2. `TaskState`
3. `TaskGraph`
4. `ToolRoute`
5. `CompensationAction`
6. `StreamHandle`

原因：这些对象更容易与后续真实实现、调度器、执行链路耦合，需要在 supporting contracts 更充分时冻结。

---

## 7. 结论

1. 当前 `contracts` 已完成 blueprint 的高扇出核心对象部分，但未完成 blueprint 全量对象收敛。
2. 当前最准确的状态表述应为：

> blueprint 高优先级 contracts 对象已完成 V1 冻结；全景对象仍存在 Missing / Replaced / Deferred 项，需要在后续整改与子系统深化前继续收敛。

---

## 8. 相关文档

| 文档 | 用途 |
|---|---|
| [DASALL_contracts交付验收报告-2026-03-23.md](DASALL_contracts%E4%BA%A4%E4%BB%98%E9%AA%8C%E6%94%B6%E6%8A%A5%E5%91%8A-2026-03-23.md) | 完整交付验收评估报告，包含代码、测试、行业对标结论 |
| [docs/todos/contracts/DASALL_contracts验收整改TODO.md](docs/todos/contracts/DASALL_contracts%E9%AA%8C%E6%94%B6%E6%95%B4%E6%94%B9TODO.md) | 矩阵 Missing/Partial 项对应的原子整改任务 |
| [docs/architecture/DASALL_contracts目录设计说明.md](docs/architecture/DASALL_contracts%E7%9B%AE%E5%BD%95%E8%AE%BE%E8%AE%A1%E8%AF%B4%E6%98%8E.md) | contracts 目录独立存在的架构设计原理 |
| [docs/architecture/DASALL_boundary治理与优化说明.md](docs/architecture/DASALL_boundary%E6%B2%BB%E7%90%86%E4%B8%8E%E4%BC%98%E5%8C%96%E8%AF%B4%E6%98%8E.md) | boundary 治理层职责分层与 Checklist Gate 原理 |
