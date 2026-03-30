# WP-01 术语与对象地图 TODO

最近更新时间：2026-03-13

## 1. 工作包目标

统一 contracts 设计术语、顶层对象地图与边界说明，形成后续所有对象定义的共同语义基线。

## 2. 完成标准

1. 关键术语不存在一词多义。
2. 顶层对象链可覆盖主流程、恢复流程和多 Agent 流程。
3. 团队对 ContextPacket、Observation、Checkpoint、WorkerTask 的基本语义达成一致。

## 3. 原子任务清单

| ID | 状态 | 任务 | 输入依据 | 交付物 | 完成判定 |
|---|---|---|---|---|---|
| WP01-T001 | Not Started | 提取架构与 ADR 中全部 contracts 关键术语 | 架构文档、ADR-006/007/008 | 术语候选列表 | 不遗漏 GoalContract、Observation、Checkpoint、ReflectionDecision、WorkerTask 等高扇出术语 |
| WP01-T002 | In Review | 清理重复术语和同义别名 | T001 输出 | deliverables/WP01-T002-术语归并表.md | 每个核心概念只有一个主名称，别名被显式记录 |
| WP01-T003 | In Review | 为每个核心术语补充一句话语义定义 | T002 输出 | deliverables/WP01-T003-术语定义表-v1.md | 定义不依赖实现细节，能被跨模块复用 |
| WP01-T004 | In Review | 标记每个术语所属层级和主要消费者 | T003 输出 | deliverables/WP01-T004-术语消费者矩阵.md | 能区分顶层共享对象与模块内部术语 |
| WP01-T005 | In Review | 绘制顶层对象流图初稿 | 计划文档第 7 节 | deliverables/WP01-T005-顶层对象流图-v1.md | 覆盖入口、上下文、决策、执行、观测、恢复、输出、协同八条链路 |
| WP01-T006 | In Review | 标出对象流图中的跨模块稳定对象 | T005 输出 | deliverables/WP01-T006-稳定对象标注版流图.md | 图中每个节点都能判断是否属于 contracts |
| WP01-T007 | In Review | 标记对象流图中的内部对象和禁止外溢对象 | T006 输出 | deliverables/WP01-T007-内部对象边界清单.md | 能明确哪些对象不能进入 contracts |
| WP01-T008 | In Review | 编写 contracts 边界说明初稿 | T004、T006、T007 | deliverables/WP01-T008-contracts边界说明-v1.md | 明确“跨模块稳定契约”和“模块内部结构”的判定规则 |
| WP01-T009 | In Review | 核对 ContextPacket 的边界是否与 ADR-006 一致 | ADR-006 | deliverables/WP01-T009-ContextPacket约束核对单.md | 明确排除 final_messages、provider_payload、rendered_prompt |
| WP01-T010 | In Review | 核对 ReflectionDecision 与 RecoveryOutcome 的边界语义 | ADR-007 | deliverables/WP01-T010-恢复语义核对单.md | 明确建议权与执行权分层 |
| WP01-T011 | In Review | 核对 MultiAgentRequest、MultiAgentResult、WorkerTask 的层级关系 | ADR-008 | deliverables/WP01-T011-协同语义核对单.md | 明确全局请求与协同子域请求分层 |
| WP01-T012 | In Review | 组织整体骨架评审并沉淀修订意见 | T003 至 T011 输出 | deliverables/WP01-T012-整体骨架评审纪要.md | 形成可进入 WP-02 的修订结论 |
| WP01-T013 | Completed | 发布术语表、对象地图和边界说明冻结版 | T012 输出 | deliverables/WP01-T013-M1冻结包.md | 评审意见已关闭或转为显式遗留项 |

## 4. 推荐执行顺序

1. 先做 T001 至 T005，形成基础材料。
2. 再做 T006 至 T011，完成边界对齐。
3. 最后做 T012、T013，作为 M1 Gate 输出。

## 5. 依赖与风险

1. 若 ADR 内容与架构文档仍有冲突，先以 ADR 为准并记录差异。
2. 若对象流图无法覆盖某条主路径，不得进入 WP-02。
