# DASALL contracts 冻结 TODO 总表

## 1. 文档目的

本文档将 [docs/plans/DASALL_contracts冻结实施计划.md](../plans/DASALL_contracts%E5%86%BB%E7%BB%93%E5%AE%9E%E6%96%BD%E8%AE%A1%E5%88%92.md) 拆分为可追踪、可认领、可验收的最小任务清单。

使用方式：

1. 先按顺序完成 WP-01 至 WP-04。
2. WP-05 仅在前四个工作包 Gate 通过后展开并行细化。
3. 每完成一个任务，更新状态、日期、责任人和产出链接。
4. 如发生 breaking 变更，必须回写到计划文档和对应 ADR 影响清单。

## 2. 工作包结构

1. WP-01：术语与对象地图
   文档：[contracts-freeze/WP-01-术语与对象地图TODO.md](contracts-freeze/WP-01-%E6%9C%AF%E8%AF%AD%E4%B8%8E%E5%AF%B9%E8%B1%A1%E5%9C%B0%E5%9B%BETODO.md)
2. WP-02：横切基础对象
   文档：[contracts-freeze/WP-02-横切基础对象TODO.md](contracts-freeze/WP-02-%E6%A8%AA%E5%88%87%E5%9F%BA%E7%A1%80%E5%AF%B9%E8%B1%A1TODO.md)
3. WP-03：主链路对象
   文档：[contracts-freeze/WP-03-主链路对象TODO.md](contracts-freeze/WP-03-%E4%B8%BB%E9%93%BE%E8%B7%AF%E5%AF%B9%E8%B1%A1TODO.md)
4. WP-04：边界对象
   文档：[contracts-freeze/WP-04-边界对象TODO.md](contracts-freeze/WP-04-%E8%BE%B9%E7%95%8C%E5%AF%B9%E8%B1%A1TODO.md)
5. WP-05：子域细化与 Contract Tests
   文档：[contracts-freeze/WP-05-子域细化与ContractTestsTODO.md](contracts-freeze/WP-05-%E5%AD%90%E5%9F%9F%E7%BB%86%E5%8C%96%E4%B8%8EContractTestsTODO.md)

## 3. 执行顺序

1. 串行阶段：WP-01 -> WP-02 -> WP-03 -> WP-04
2. 并行阶段：WP-05，可按 tool / memory / llm / event / tests 分子域并行
3. 回退规则：任一后续工作包若提出前序对象 breaking 修改，必须回到前序工作包重新评审

## 4. 里程碑与 Gate

| 里程碑 | 前置条件 | 完成判定 | 对应工作包 |
|---|---|---|---|
| M1 术语收敛 | 无 | 术语表、对象地图、边界说明通过评审 | WP-01 |
| M2 横切语义冻结 | M1 | Error、Budget、EventEnvelope、ID/Time 规则通过评审 | WP-02 |
| M3 单 Agent 最小闭环冻结 | M2 | AgentRequest -> AgentResult 对象链完整闭环 | WP-03 |
| M4 ADR 边界落盘 | M3 | ADR-006/007/008 全部映射到具体对象约束 | WP-04 |
| M5 Contracts V1 Ready | M4 | 子域对象细化完成，Contract Tests 基线可执行 | WP-05 |

## 5. 最小任务执行规则

1. 一个任务只交付一个明确产出，不混合多个目录或多个评审目标。
2. 一个任务必须能由单次评审给出明确结论：通过、驳回或需修改。
3. 一个任务应优先落在单个对象、单个规则集或单个图表上。
4. 每个任务至少应包含：输入依据、交付物、完成判定。
5. 若某任务依赖 ADR 结论，只能引用已冻结 ADR，不得临时重写 ADR 语义。

## 6. 任务状态建议

建议统一使用以下状态值：

1. Not Started
2. In Progress
3. In Review
4. Blocked
5. Done

## 7. 当前推荐启动集

1. 先启动 WP-01 的 T001 至 T006，完成术语表、对象地图和边界说明初稿。
2. M1 通过后立即启动 WP-02 的 T001、T002、T004、T007，先锁定横切规则。
3. 不建议在 M2 之前提前设计具体 Prompt、Memory、LLM 子域字段。

## 8. 维护要求

1. 每个工作包文档顶部保留最近更新时间。
2. 每个任务完成后补充产出链接。
3. 若任务拆分仍过大，应在原编号下继续细分为子任务，不得直接在评审中口头补充。
4. docs/plans 保存“为什么这样做”，docs/todos 保存“接下来谁做什么、做到什么算完成”。
