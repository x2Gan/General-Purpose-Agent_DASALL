# WP02-T007 RuntimeBudget 字段清单

最近更新时间：2026-03-14
任务状态：In Review
任务编号：WP02-T007
上游输入：架构文档、计划文档

## 1. 任务理解

本任务只处理 WP02-T007：设计 RuntimeBudget 顶层语义，并形成可评审字段清单。

本任务完成判定要求至少覆盖 token、turn、tool_call、latency、replan 五类预算。

本任务不扩展到 T008 的预算消耗快照表达细节，不进入代码实现。

## 2. 约束与边界

### 2.1 可追溯约束

1. 来自 WP-02 TODO：T007 完成判定要求覆盖 token、turn、tool_call、latency、replan。
2. 来自实施计划：RuntimeBudget 不只表达 token，还要表达轮次、工具调用次数、延迟等顶层预算。
3. 来自架构文档：Runtime 主循环必须统一接入 MAX_TOOL_CALLS、MAX_REPLAN_COUNT、STEP_TIMEOUT_SECONDS、SESSION_TIMEOUT_SECONDS 等防护项。
4. 来自架构文档：RuntimeBudget 已作为 contracts 中 checkpoint 邻近核心对象，且示例结构包含 max_rounds、max_tool_calls、max_latency_ms、max_tokens。
5. 来自工程策略：contracts 冻结阶段遵循兼容优先，新增优于修改，语义重解释视为 breaking。

### 2.2 边界

1. 本任务只定义 RuntimeBudget 顶层维度和字段语义。
2. 本任务不定义预算实时消耗快照、不定义拒绝原因编码细则（这些属于 T008）。
3. 不改写 ADR 既有结论，不改动 Runtime 与 Cognition 职责边界。

### 2.3 非目标

1. 不设计 BudgetSnapshot 的 current/remaining 计算规则。
2. 不定义 token 压缩策略与模型路由策略细节。
3. 不编写 contracts 头文件或序列化代码。

## 3. 方案对比与决策

### 3.1 方案 A：单一总预算字段

设计思路：仅定义 total_budget 一个总量字段，具体预算维度由实现层自行解释。

优点：

1. 字段最少，接入快。

缺点：

1. 无法满足 T007 的五维预算完成判定。
2. 不同模块会重新发明预算语义，破坏横切一致性。
3. 无法稳定支持 T008 消耗表达。

### 3.2 方案 B：显式多维预算上限字段

设计思路：RuntimeBudget 显式定义五个核心预算维度的上限字段，并保留扩展位；消耗与快照延后到 T008。

优点：

1. 直接满足 T007 完成判定。
2. 与主循环防护项（tool_call、replan、timeout）一致。
3. 为 T008 提供稳定输入，不越界实现细节。

缺点：

1. 需要评审时统一单位和命名口径。

### 3.3 决策

采用方案 B。

取舍理由：方案 B 在最小范围内冻结预算语义，既满足验收又避免提前绑定 T008 的表达细节。

## 4. 最终产出

### 4.1 RuntimeBudget 顶层语义

RuntimeBudget 表示“单次请求或会话执行期间的运行时资源上限约束集合”，用于驱动 Runtime 防护、超限判定与恢复路径选择。

语义约束：

1. RuntimeBudget 只表达上限约束，不直接表达实时消耗。
2. 任一预算维度超限后，Runtime 必须进入受控处理（拒绝继续扩张、触发恢复或失败收敛）。
3. 预算是运行时控制面契约，不由单个子域对象自行新增同类顶层预算字段。

### 4.2 RuntimeBudget 字段清单（T007 冻结草案）

| 字段 | 必填 | 类型建议 | 单位/口径 | 语义定义 |
|---|---|---|---|---|
| max_tokens | 是 | uint32 | token | 单次任务允许消耗的 token 上限 |
| max_turns | 是 | uint32 | turn | 单次任务允许的主循环轮次上限 |
| max_tool_calls | 是 | uint32 | call | 单次任务允许的工具调用次数上限 |
| max_latency_ms | 是 | uint32 | ms | 单次任务允许的端到端时延上限 |
| max_replan_count | 是 | uint32 | count | 单次任务允许的重规划次数上限 |

说明：

1. 五个字段为最小必填集合，满足 T007 完成判定。
2. 与架构中的 MAX_TOOL_CALLS、MAX_REPLAN_COUNT、超时控制要求保持一致。
3. T008 再定义 current/remaining/reject_reason 等快照与消耗表达。

### 4.3 兼容性建议

1. 新增预算维度时采用新增字段方式，不重定义既有字段语义。
2. 字段单位一旦冻结（token/turn/call/ms/count），不得无版本迁移直接改单位。
3. 若需要支持 profile 级差异预算，采用外层策略配置映射，不在 RuntimeBudget 顶层改写语义。

### 4.4 Mode Extension

Design 模式产出：

1. 本文档为评审材料，不改代码。
2. 已输出对象语义、字段建议、边界说明与兼容性建议。
3. 可直接落盘路径：docs/todos/contracts/deliverables/WP02-T007-RuntimeBudget字段清单.md。

Build 模式预备清单（不在本任务执行）：

1. 候选改动文件：contracts/checkpoint 下 RuntimeBudget 契约头文件及校验入口。
2. 关键接口：Runtime BudgetController 读取与阈值检查接口。
3. 冲突检查：若现有实现使用 max_rounds 命名，应提供向 max_turns 的兼容映射或别名策略。
4. 验证步骤：
   - 构建：执行 CMake 构建验证 contracts/runtime 目标。
   - 测试：覆盖 token、turn、tool_call、latency、replan 五维超限判定。
   - 契约校验：验证五个字段必填、单位一致、非负约束。

## 5. 验收清单

1. 已形成 RuntimeBudget 字段清单文档。
2. 已覆盖 token、turn、tool_call、latency、replan 五类预算。
3. 已明确本任务边界，不越权进入 T008 快照细节。
4. 产出可直接作为 T008 输入。

Quality Gate 回答：

1. 当前任务是否达成 Done Criteria：是。
2. 产出是否可被下一任务直接消费：是，可直接输入 T008。
3. 是否引入 breaking change 风险：当前仅文档冻结，无实现变更；若后续重命名已落地字段且无兼容层，存在 breaking 风险。
4. 是否需要触发 ADR 或版本变更流程：当前不需要；若修改预算维度主语义或单位，应触发版本评审流程。

## 6. 风险与回退

1. 风险：turn 与 replan 语义被混用，导致预算统计失真。
   回退：强制区分“循环轮次预算”和“重规划预算”，分别计数。
2. 风险：latency 口径不一致（端到端 vs 单步骤）。
   回退：本任务冻结为端到端口径，步骤级时延另行作为扩展字段评审。
3. 风险：现有实现字段命名不一致（如 max_rounds 与 max_turns）。
   回退：先引入兼容映射，再统一主命名，避免一次性破坏。

## 7. 下一任务建议

1. WP02-T008 设计 budget 消耗与快照表达方式。
