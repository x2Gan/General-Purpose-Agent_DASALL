# WP-03 主链路对象 TODO

最近更新时间：2026-03-13

## 1. 工作包目标

优先冻结单 Agent 最小闭环所需的共享对象，确保后续模块设计围绕统一主链路展开。

## 2. 完成标准

1. AgentRequest -> GoalContract -> ContextPacket -> Observation -> Checkpoint -> AgentResult 可完整描述单 Agent 最小闭环。
2. 主链路对象职责无需口头补充解释。
3. ContextPacket、Observation、Checkpoint 的边界不与 Prompt、Recovery、Worker 子域混淆。

## 3. 原子任务清单

| ID | 状态 | 任务 | 输入依据 | 交付物 | 完成判定 |
|---|---|---|---|---|---|
| WP03-T001 | Not Started | 明确主链路对象全集与依赖顺序 | WP-01、WP-02 冻结包 | 主链路对象依赖表 | 明确 AgentRequest、GoalContract、ContextPacket、Observation、ObservationDigest、BeliefState、Checkpoint、AgentResult 顺序 |
| WP03-T002 | Not Started | 定义 AgentRequest 的最小语义范围 | 架构文档入口链路 | AgentRequest 语义说明 | 不夹带 runtime 内部状态和 provider 私有字段 |
| WP03-T003 | Not Started | 列出 AgentRequest 必填字段和可选字段 | T002 输出、WP-02 规则 | AgentRequest 字段表 | 字段仅围绕入口请求、约束、预算和请求元数据 |
| WP03-T004 | Not Started | 定义 GoalContract 的职责边界 | 架构文档、计划文档 | GoalContract 语义说明 | 能表达成功判据、约束、预算、审批策略 |
| WP03-T005 | Not Started | 列出 GoalContract 必填字段和约束表达 | T004 输出 | GoalContract 字段表 | 不依赖自然语言二次猜测解释目标 |
| WP03-T006 | Not Started | 定义 Observation 的统一折叠语义 | 架构文档观测链路 | Observation 语义说明 | 能统一承载 tool、knowledge、human、worker 输出 |
| WP03-T007 | Not Started | 列出 Observation 的来源分类和引用规则 | T006 输出、WP-02 ErrorSource 规则 | Observation 分类表 | 来源类型与引用方式可直接被下游消费 |
| WP03-T008 | Not Started | 定义 ObservationDigest 与 Observation 的分层边界 | T006 输出 | ObservationDigest 边界说明 | Digest 面向推理消费，Observation 保留执行语义 |
| WP03-T009 | Not Started | 定义 BeliefState 在主链路中的位置 | 架构文档认知链路 | BeliefState 语义说明 | 明确它不是入口请求，也不是恢复快照 |
| WP03-T010 | Not Started | 定义 ContextPacket 的语义组成 | ADR-006、WP-01 核对单 | ContextPacket 语义说明 | 只包含共享语义上下文，不含消息渲染内容 |
| WP03-T011 | Not Started | 列出 ContextPacket 的必选组成块 | T010 输出 | ContextPacket 字段表 | 至少覆盖目标、上下文摘要、记忆/知识/预算相关语义块 |
| WP03-T012 | Not Started | 定义 Checkpoint 的最小恢复语义 | 架构文档恢复链路、ADR-007 | Checkpoint 语义说明 | 只表达恢复所需最小状态，不沦为无限工作内存快照 |
| WP03-T013 | Not Started | 列出 Checkpoint 的恢复必需字段 | T012 输出 | Checkpoint 字段表 | 覆盖状态引用、进度、预算快照、局部子域快照入口 |
| WP03-T014 | Not Started | 定义 AgentResult 的最小输出语义 | 架构文档输出链路 | AgentResult 语义说明 | 明确最终结果、状态、摘要、引用而非内部执行细节 |
| WP03-T015 | Not Started | 绘制单 Agent 主链路对象流转图 | T002 至 T014 输出 | 主流程对象流图 | 从 CLI/入口到结果返回形成闭环 |
| WP03-T016 | Not Started | 进行主链路对象重叠检查 | T002 至 T015 输出 | 职责重叠检查单 | 无对象同时承担 Prompt、Recovery、Worker 的职责 |
| WP03-T017 | Not Started | 组织主链路对象评审 | T015、T016 输出 | 评审纪要 | 所有高扇出对象得到明确结论 |
| WP03-T018 | Not Started | 发布主链路对象冻结版 | T017 输出 | M3 冻结包 | 可作为 runtime/cognition/memory/llm 详细设计基线 |

## 4. 推荐执行顺序

1. 先做 T001 至 T005，冻结入口语义。
2. 再做 T006 至 T013，冻结上下文、观测和恢复快照语义。
3. 最后做 T014 至 T018，形成闭环图、评审和 M3 输出。

## 5. 依赖与风险

1. 若 GoalContract 没有冻结成功判据，后续 ResponseBuilder 和 Planner 会反复返工。
2. 若 Observation 与 ObservationDigest 不分层，memory、cognition、audit 会耦合到同一对象。
3. 若 Checkpoint 过载，runtime 和 multi_agent 将失去明确恢复边界。
