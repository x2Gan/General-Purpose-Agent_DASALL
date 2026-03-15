# WP-02 横切基础对象 TODO

最近更新时间：2026-03-14

## 1. 工作包目标

冻结所有子域共享的基础语义，防止 error、budget、id、time、event 头部在不同目录重复定义。

## 2. 完成标准

1. 任意子域对象不再需要自行发明错误、预算、标识和时间表达。
2. 兼容性规则被写成显式 review checklist。
3. 枚举、错误码、事件封套和追踪元数据具备统一约定。

## 3. 原子任务清单

| ID | 状态 | 任务 | 输入依据 | 交付物 | 完成判定 |
|---|---|---|---|---|---|
| WP02-T001 | In Review | 定义横切基础对象全集范围 | WP-01 冻结包 | [横切对象范围表](deliverables/WP02-T001-%E6%A8%AA%E5%88%87%E5%9F%BA%E7%A1%80%E5%AF%B9%E8%B1%A1%E8%8C%83%E5%9B%B4%E8%A1%A8.md) | 明确纳入 error、event、budget、id、time |
| WP02-T002 | In Review | 编写兼容性总体原则草案 | 计划文档第 10 节 | [兼容性规则 v1](deliverables/WP02-T002-%E5%85%BC%E5%AE%B9%E6%80%A7%E8%A7%84%E5%88%99-v1.md) | 覆盖新增优于修改、废弃优于删除、语义变化视为 breaking |
| WP02-T003 | In Review | 编写字段演进规则草案 | T002 输出 | [字段演进规则表](deliverables/WP02-T003-%E5%AD%97%E6%AE%B5%E6%BC%94%E8%BF%9B%E8%A7%84%E5%88%99%E8%A1%A8.md) | 覆盖字段类型、可选性、多值性、枚举默认值 |
| WP02-T004 | In Review | 设计 ResultCode 分类框架 | 架构文档、规范文档 | [ResultCode 分类表](deliverables/WP02-T004-ResultCode%E5%88%86%E7%B1%BB%E8%A1%A8.md) | 能区分 validation、tool、provider、runtime、policy 等失败类别 |
| WP02-T005 | In Review | 设计 ErrorInfo 必填字段集合 | T004 输出、ADR-007 | [ErrorInfo 字段清单](deliverables/WP02-T005-ErrorInfo%E5%AD%97%E6%AE%B5%E6%B8%85%E5%8D%95.md) | 包含 failure_type、retryable、safe_to_replan、details、source_ref |
| WP02-T006 | In Review | 定义错误来源引用规则 | T005 输出 | [ErrorSource 引用约定](deliverables/WP02-T006-ErrorSource%E5%BC%95%E7%94%A8%E7%BA%A6%E5%AE%9A.md) | 能统一引用 observation、tool call、worker task、checkpoint |
| WP02-T007 | In Review | 设计 RuntimeBudget 顶层语义 | 架构文档、计划文档 | [RuntimeBudget 字段清单](deliverables/WP02-T007-RuntimeBudget%E5%AD%97%E6%AE%B5%E6%B8%85%E5%8D%95.md) | 至少覆盖 token、turn、tool_call、latency、replan 预算 |
| WP02-T008 | In Review | 设计 budget 消耗与快照表达方式 | T007 输出 | [BudgetSnapshot 规则](deliverables/WP02-T008-BudgetSnapshot%E8%A7%84%E5%88%99.md) | 能表达当前值、上限、剩余额度和拒绝原因 |
| WP02-T009 | In Review | 统一 request_id、session_id、trace_id、task_id、lease_id 规则 | 计划文档第 8 阶段 1 | [标识元数据规范](deliverables/WP02-T009-%E6%A0%87%E8%AF%86%E5%85%83%E6%95%B0%E6%8D%AE%E8%A7%84%E8%8C%83.md) | 明确命名、唯一性、传播范围和父子关系 |
| WP02-T010 | In Review | 统一时间、截止时间和超时表达方式 | T009 输出 | [Time/Deadline 规范](deliverables/WP02-T010-TimeDeadline%E8%A7%84%E8%8C%83.md) | 区分 created_at、deadline_at、timeout_ms、ttl |
| WP02-T011 | In Review | 设计 EventEnvelope 通用头部字段 | 架构文档事件链路 | [EventEnvelope 头部定义](deliverables/WP02-T011-EventEnvelope%E5%A4%B4%E9%83%A8%E5%AE%9A%E4%B9%89.md) | 只包含通用封套，不含模块私有 payload |
| WP02-T012 | In Review | 定义枚举默认值与弃用规则 | T002、T003 输出 | [枚举规范](deliverables/WP02-T012-%E6%9E%9A%E4%B8%BE%E8%A7%84%E8%8C%83.md) | 每个枚举保留 Unspecified 值，并定义 deprecate 方式 |
| WP02-T013 | In Review | 形成横切基础对象 review checklist | T002 至 T012 输出 | [Review Checklist v1](deliverables/WP02-T013-ReviewChecklist-v1.md) | 可直接用于后续对象评审 |
| WP02-T014 | In Review | 组织横切基础对象评审 | T004 至 T013 输出 | [评审纪要](deliverables/WP02-T014-%E8%AF%84%E5%AE%A1%E7%BA%AA%E8%A6%81.md) | 主要争议项形成决议或遗留项 |
| WP02-T015 | Done | 发布横切基础对象冻结版 | T014 输出 | [M2 冻结包](deliverables/WP02-T015-M2%E5%86%BB%E7%BB%93%E5%8C%85.md) | 后续子域不可再自行引入新基础语义 |

## 4. 推荐执行顺序

1. 先做 T001 至 T003，固定规则框架。
2. 再做 T004 至 T012，逐类冻结对象和字段规则。
3. 最后做 T013 至 T015，作为 M2 Gate 输出。

## 5. 依赖与风险

1. 如果 RuntimeBudget 定义不完整，WP-03 和 WP-04 会重复发明预算字段。
2. 如果 EventEnvelope 头部掺入模块专属字段，后续 event 子域会失去演进空间。
