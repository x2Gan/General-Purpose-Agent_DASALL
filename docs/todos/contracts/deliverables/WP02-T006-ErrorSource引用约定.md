# WP02-T006 ErrorSource 引用约定

最近更新时间：2026-03-14
任务状态：In Review
任务编号：WP02-T006
上游输入：WP02-T005-ErrorInfo字段清单.md

## 1. 任务理解

本任务只处理 WP02-T006：定义错误来源引用规则（ErrorSource 引用约定），用于支撑 ErrorInfo.source_ref 的统一表达与追溯。

本任务完成判定：能够统一引用 observation、tool call、worker task、checkpoint。

本任务不扩展到 RuntimeBudget、EventEnvelope、完整错误码实现或跨模块代码改造。

## 2. 约束与边界

### 2.1 可追溯约束

1. 来自 WP-02 TODO：T006 完成判定要求统一引用 observation、tool call、worker task、checkpoint。
2. 来自 T005：source_ref 已冻结最小键为 ref_type/ref_id，且 ref_type 限 observation/tool_call/worker_task/checkpoint，T006 需要在此基础上定义引用规则。
3. 来自 ADR-007：Tool/Workflow/MCP/外部执行失败统一映射为 Observation + ErrorInfo，Runtime 持有 checkpoint；因此 ErrorSource 必须兼容观测与恢复两条链路。
4. 来自架构文档：Observation 为统一失败折叠对象，Checkpoint 为恢复入口关键对象，子 Agent 异常最终折叠为失败 Observation。
5. 来自计划与总 TODO：contracts 冻结遵循兼容优先，新增优于修改，语义重解释视为 breaking。

### 2.2 边界

1. 本任务只定义 source_ref 的语义与引用规则，不重定义 ErrorInfo 其他字段。
2. 本任务只规定“如何引用”，不规定“对象内部字段全集”。
3. 不改写 ADR-007/ADR-008 的职责边界，不改变主控与协同控制权。

### 2.3 非目标

1. 不设计 T007 RuntimeBudget 语义。
2. 不设计失败恢复策略执行逻辑。
3. 不编写 contracts 代码与序列化实现。

## 3. 方案对比与决策

### 3.1 方案 A：单字符串路径引用

设计思路：source_ref 仅保存 path 字符串，如 observation:obs-123 或 checkpoint:cp-01。

优点：

1. 实现成本低。
2. 初期接入快。

缺点：

1. 规则不可验证，格式容易漂移。
2. 难表达多来源关联，审计与回放不稳定。
3. 对后续 T013 checklist 不友好。

### 3.2 方案 B：结构化单主引用 + 可选关联引用

设计思路：定义 source_ref 为结构化对象，包含主引用（primary）和可选关联引用（related），每个引用由 ref_type/ref_id 组成并受统一校验规则约束。

优点：

1. 与 T005 最小结构完全兼容，且可演进。
2. 主引用语义清晰，支持单点归因。
3. 可选关联引用支持跨链路追溯，仍不越界到实现细节。

缺点：

1. 文档评审需要统一“主引用优先级”口径。

### 3.3 决策

采用方案 B。

取舍理由：在不跨任务的前提下，方案 B 最小化复杂度且最大化可追溯性，能直接被后续评审与实现消费。

## 4. 最终产出

### 4.1 ErrorSource 引用模型

建议 ErrorInfo.source_ref 使用如下结构：

| 字段 | 必填 | 类型 | 规则 |
|---|---|---|---|
| primary.ref_type | 是 | enum/string | 仅允许 observation、tool_call、worker_task、checkpoint |
| primary.ref_id | 是 | string | 对应对象唯一标识，不能为空 |
| related[] | 否 | array<object> | 每项与 primary 同结构，用于补充关联链路 |

说明：

1. primary 用于唯一主归因。
2. related 仅用于补充上下游关联，不改变 primary 的主归因语义。
3. 兼容性策略：新增 related 子键可增量扩展，禁止重解释 primary 语义。

### 4.2 四类引用对象统一规则

| ref_type | 语义 | ref_id 建议来源 | 使用场景 |
|---|---|---|---|
| observation | 失败观测对象引用 | observation_id | 工具、检索、人类反馈、子 Agent 输出折叠后的失败归因 |
| tool_call | 工具调用实例引用 | tool_call_id | 需要定位具体工具调用及其参数上下文 |
| worker_task | 子任务执行引用 | task_id | 多 Agent 协同子任务失败归因 |
| checkpoint | 恢复快照引用 | checkpoint_id | 失败发生时的恢复点与状态机上下文追溯 |

### 4.3 引用判定与校验规则

1. 单条 ErrorInfo 必须且只能有一个 primary。
2. primary.ref_type 必须在四类枚举内；否则判定为无效引用。
3. primary.ref_id 必须非空且可映射到对应对象。
4. related 中的每个引用必须满足与 primary 相同的类型与非空约束。
5. 若 primary=checkpoint，则建议 related 补充触发失败的 observation 或 tool_call，以便恢复审计。
6. 若来源无法即时定位对象，标记 unresolved 并进入评审，不允许写入自由文本替代 ref_id。

### 4.4 与 T005/T007 的接口关系

1. 与 T005：本任务承接 source_ref 最小结构，补齐统一判定与引用规则。
2. 与 T007：RuntimeBudget 语义不在本任务定义域，仅要求 source_ref 可与 checkpoint 对齐，供预算场景后续引用。

### 4.5 Mode Extension

Design 模式产出：

1. 本文档为评审材料，不改代码。
2. 已给出对象语义、字段建议、边界说明、兼容性建议。
3. 可直接落盘路径：docs/todos/contracts/deliverables/WP02-T006-ErrorSource引用约定.md。

Build 模式预备清单（不在本任务执行）：

1. 候选改动文件：contracts/error 下 ErrorInfo/source_ref 契约定义与校验逻辑入口。
2. 关键接口：Observation 归一化写入、Tool 调用记录写入、WorkerTask 汇聚写入、Checkpoint 写入路径。
3. 冲突检查：若现有实现把 source_ref 存为纯字符串，需先做兼容适配层，再迁移为结构化对象。
4. 验证步骤：
   - 构建：执行 CMake 构建，确认 contracts 与 runtime 目标可编译。
   - 测试：覆盖四类 ref_type 的校验与解析测试。
   - 契约校验：验证 primary 必填、枚举约束与 related 约束。

## 5. 验收清单

1. 已形成 ErrorSource 引用约定文档。
2. 已统一定义 observation、tool_call、worker_task、checkpoint 四类引用对象。
3. 已给出 source_ref 结构、主引用规则和校验规则。
4. 产出可直接作为 T013 checklist 与后续实现输入。

Quality Gate 回答：

1. 当前任务是否达成 Done Criteria：是，四类引用已统一定义。
2. 产出是否可被下一任务直接消费：是，可被后续横切对象评审与实现阶段直接消费。
3. 是否引入 breaking change 风险：当前为文档冻结，无实现变更；后续若把已上线字符串语义改为结构化且无兼容层，存在 breaking 风险。
4. 是否需要触发 ADR 或版本变更流程：当前不需要；若后续改变 ref_type 枚举主语义或 primary 规则，需要触发版本评审流程。

## 6. 风险与回退

1. 风险：primary 与 related 混用导致主归因不稳定。
   回退：强制以 primary 作为唯一主归因，related 仅补充。
2. 风险：不同模块生成的 ref_id 命名不一致，导致解析失败。
   回退：先收敛命名规则到各对象既有唯一标识，再补充映射表，不扩展字段语义。
3. 风险：实现阶段直接替换旧字符串字段导致兼容问题。
   回退：先加兼容适配读取（string -> structured），通过后再收紧写入规则。

## 7. 下一任务建议

1. WP02-T007 设计 RuntimeBudget 顶层语义。
