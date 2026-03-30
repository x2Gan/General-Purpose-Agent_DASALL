# WP02-T008 BudgetSnapshot 规则

最近更新时间：2026-03-14
任务状态：In Review
任务编号：WP02-T008
上游输入：WP02-T007-RuntimeBudget字段清单.md

## 1. 任务理解

本任务只处理 WP02-T008：设计 budget 消耗与快照表达方式，形成统一的 BudgetSnapshot 规则。

本任务完成判定要求：能表达当前值、上限、剩余额度和拒绝原因。

本任务不扩展到新预算维度定义（已由 T007 冻结），不进入代码实现。

## 2. 约束与边界

### 2.1 可追溯约束

1. 来自 WP-02 TODO：T008 必须能表达 current、max、remaining、reject_reason。
2. 来自 T007：RuntimeBudget 已冻结五个顶层预算维度（token、turn、tool_call、latency、replan），T008 只能定义消耗与快照表达。
3. 来自架构文档：Runtime 主循环受 MAX_TOOL_CALLS、MAX_REPLAN_COUNT、超时等防护项约束，快照必须支持超限判定与恢复路径。
4. 来自计划文档：预算语义应避免各目录重复发明字段；契约演进遵循兼容优先。

### 2.2 边界

1. 本任务只定义 BudgetSnapshot 表达规则，不新增 RuntimeBudget 顶层维度。
2. 本任务只定义“表达与判定口径”，不定义具体调度算法。
3. 不改写任何 ADR 结论，不改动 runtime/cognition 分层。

### 2.3 非目标

1. 不定义模型路由策略。
2. 不定义恢复策略执行流程细节。
3. 不编写 contracts 代码与序列化实现。

## 3. 方案对比与决策

### 3.1 方案 A：单对象汇总快照

设计思路：整个预算只输出一组 current/max/remaining/reject_reason。

优点：

1. 结构简单。
2. 输出字段少。

缺点：

1. 无法按维度定位超限来源。
2. 不能支撑 token/turn/tool_call/latency/replan 的独立治理。
3. 对 T013 checklist 与后续实现可用性弱。

### 3.2 方案 B：按预算维度输出分项快照

设计思路：BudgetSnapshot 由多维 budget entries 组成，每个维度统一包含 current/max/remaining/reject_reason。

优点：

1. 直接满足 T008 完成判定。
2. 与 T007 五维预算一一映射，追溯明确。
3. 便于 Runtime 做超限判定与审计。

缺点：

1. 字段结构相对更长，需要统一单位口径。

### 3.3 决策

采用方案 B。

取舍理由：方案 B 在不扩张范围的前提下，最大化可观测性和可消费性，且与 T007 对齐最稳定。

## 4. 最终产出

### 4.1 BudgetSnapshot 顶层语义

BudgetSnapshot 表示某一时刻 RuntimeBudget 的预算使用状态，用于表达“已用多少、上限多少、还剩多少、为何拒绝继续”。

语义约束：

1. snapshot 是状态表达，不是策略执行指令。
2. remaining 由 max - current 计算得出，禁止手工写入与计算口径不一致值。
3. reject_reason 仅在超限或策略拒绝时填写；未拒绝时为空。

### 4.2 BudgetSnapshot 结构规则

建议结构：

| 字段 | 必填 | 类型建议 | 语义 |
|---|---|---|---|
| snapshot_at_ms | 是 | uint64 | 快照生成时间戳（ms） |
| entries | 是 | array<object> | 各预算维度快照列表 |
| overall_reject_reason | 否 | enum/string | 全局拒绝原因（可空） |

entry 结构：

| 字段 | 必填 | 类型建议 | 语义 |
|---|---|---|---|
| budget_type | 是 | enum/string | token/turn/tool_call/latency/replan |
| current | 是 | uint64 | 当前已消耗值 |
| max | 是 | uint64 | 预算上限 |
| remaining | 是 | int64 | 剩余额度，建议 max-current，可为负表示超限 |
| reject_reason | 否 | enum/string | 该维度拒绝原因 |

### 4.3 五维预算映射规则

| budget_type | current 含义 | max 来源（T007） | reject_reason 触发示例 |
|---|---|---|---|
| token | 当前已消耗 token 总量 | max_tokens | token_budget_exhausted |
| turn | 当前主循环轮次计数 | max_turns | turn_budget_exhausted |
| tool_call | 当前工具调用次数 | max_tool_calls | tool_call_budget_exhausted |
| latency | 当前累计端到端时延(ms) | max_latency_ms | latency_budget_exhausted |
| replan | 当前重规划计数 | max_replan_count | replan_budget_exhausted |

### 4.4 判定与输出规范

1. 同一快照中每个 budget_type 最多出现一次。
2. remaining >= 0 表示未超限；remaining < 0 表示已超限。
3. 当任一关键维度超限并导致拒绝继续执行时：
   - 对应 entry.reject_reason 必填。
   - overall_reject_reason 可填统一原因。
4. 若存在多维同时超限，允许多个 entry 设置 reject_reason；overall_reject_reason 填“first_rejecting_dimension”或策略层聚合原因。
5. 未触发拒绝时，reject_reason 必须为空，避免语义污染。

### 4.5 兼容性建议

1. 后续新增预算维度时，只允许在 budget_type 新增值，不改写既有类型含义。
2. current/max/remaining 语义冻结为数值计量；不得切换为百分比表达。
3. reject_reason 建议枚举化并保留 unknown/unspecified 兜底值，避免文本漂移。

### 4.6 Mode Extension

Design 模式产出：

1. 本文档为评审材料，不改代码。
2. 已输出对象语义、字段建议、边界说明、兼容性建议。
3. 可直接落盘路径：docs/todos/contracts/deliverables/WP02-T008-BudgetSnapshot规则.md。

Build 模式预备清单（不在本任务执行）：

1. 候选改动文件：contracts/checkpoint 下 BudgetSnapshot 契约定义与序列化入口。
2. 关键接口：BudgetController 快照导出接口、拒绝原因写入接口。
3. 冲突检查：若现有实现缺少 remaining 字段，先以计算字段兼容输出，再固化存储。
4. 验证步骤：
   - 构建：执行 CMake 构建验证 contracts/runtime。
   - 测试：覆盖五维快照生成、remaining 计算、reject_reason 触发与空值语义。
   - 契约校验：验证每维 current/max/remaining 完整且类型一致。

## 5. 验收清单

1. 已形成 BudgetSnapshot 规则文档。
2. 已定义 current、max、remaining、reject_reason 的统一表达。
3. 已与 T007 五维预算一一映射。
4. 产出可直接作为 T009/T010 与 T013 评审输入。

Quality Gate 回答：

1. 当前任务是否达成 Done Criteria：是。
2. 产出是否可被下一任务直接消费：是。
3. 是否引入 breaking change 风险：当前仅文档冻结，无实现变更；后续若更改 remaining 计算口径会有 breaking 风险。
4. 是否需要触发 ADR 或版本变更流程：当前不需要；若改写预算维度主语义或 reject_reason 基本语义，需要触发版本评审。

## 6. 风险与回退

1. 风险：current 与 remaining 同时手写导致不一致。
   回退：固定 remaining 由 max-current 推导，并在校验中强制一致性。
2. 风险：reject_reason 被泛化为任意文本，跨模块不可聚合。
   回退：先收敛到枚举集合并保留兜底值。
3. 风险：latency 统计口径混杂（端到端与阶段级）。
   回退：本任务固定端到端口径，阶段级另行扩展字段评审。

## 7. 下一任务建议

1. WP02-T009 统一 request_id、session_id、trace_id、task_id、lease_id 规则。
