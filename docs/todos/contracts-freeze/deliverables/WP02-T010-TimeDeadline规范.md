# WP02-T010 Time/Deadline 规范

最近更新时间：2026-03-14
任务状态：In Review
任务编号：WP02-T010
上游输入：WP02-T009-标识元数据规范.md、计划文档、架构文档

## 1. 任务理解

本任务只处理 WP02-T010：统一时间、截止时间和超时表达方式，产出 Time/Deadline 规范。

本任务完成判定要求：区分 created_at、deadline_at、timeout_ms、ttl。

本任务不扩展到 EventEnvelope 头部细节（T011）和代码实现。

## 2. 约束与边界

### 2.1 可追溯约束

1. 来自 WP-02 TODO：T010 完成判定要求明确区分 created_at、deadline_at、timeout_ms、ttl。
2. 来自计划文档阶段 1：通用时间与超时表达应统一，避免各目录各自发明。
3. 来自架构文档：Session 已使用 created_at；运行链路要求 CancelToken + Deadline 贯穿 LLM 与 Tool；存在 STEP/SESSION/TASK 超时控制。
4. 来自 T009：标识链路已统一，时间语义需与 request/session/task/lease 传播边界协同。

### 2.2 边界

1. 本任务只定义时间语义与字段口径，不定义调度器超时算法。
2. 本任务不定义事件封套最终字段集合。
3. 不改写 ADR-006/007/008 已冻结边界。

### 2.3 非目标

1. 不定义时区同步实现方案（NTP/PTP 等）。
2. 不定义日志落盘格式实现。
3. 不编写 contracts/runtime 代码。

## 3. 方案对比与决策

### 3.1 方案 A：统一使用绝对时间戳

设计思路：所有超时与有效期都用绝对时间（deadline_at）表达。

优点：

1. 终态判定直观。

缺点：

1. 创建时缺少 timeout 意图，配置不友好。
2. 难表达“从当前时刻起”动态生效窗口。
3. 不便与现有 timeout policy 配置对齐。

### 3.2 方案 B：绝对时间 + 相对时长双语义模型

设计思路：
created_at、deadline_at 用绝对时间戳表达事实；timeout_ms、ttl 用相对时长表达策略。

优点：

1. 同时满足事实追踪与策略配置。
2. 与 CancelToken + Deadline、STEP/SESSION 超时策略一致。
3. 可直接支撑 request/session/task/lease 四类对象。

缺点：

1. 需要明确换算规则，避免重复定义。

### 3.3 决策

采用方案 B。

取舍理由：方案 B 能最小化歧义，兼顾审计可追溯与运行时可配置性，且完全满足 T010 验收条件。

## 4. 最终产出

### 4.1 字段语义冻结

| 字段 | 类型建议 | 语义定义 | 必填建议 | 典型适用对象 |
|---|---|---|---|---|
| created_at | int64(ms epoch) | 对象被创建的绝对时间点 | 必填 | request/session/task/lease/event |
| deadline_at | int64(ms epoch) | 对象必须完成或失效的绝对截止时间点 | 条件必填 | request/task/tool_call/workflow_step |
| timeout_ms | uint32 | 从起算点开始可执行的最长时长（毫秒） | 条件必填 | step/session/tool_call |
| ttl | uint32 | 对象从创建或写入后可保留/可复用的生存时长（毫秒） | 条件必填 | cache/capability_entry/lease |

### 4.2 四字段区分规则

1. created_at 是事实时间，不参与策略推导。
2. deadline_at 是硬边界，判定是否超时以 now > deadline_at 为准。
3. timeout_ms 是策略参数，常用于在运行时换算 deadline_at。
4. ttl 是存活窗口，不等价于执行超时；过期后应触发刷新/失效，而非执行失败。

### 4.3 换算与优先级规则

1. 若存在 created_at 与 timeout_ms，可计算 deadline_at = created_at + timeout_ms。
2. 若外部显式提供 deadline_at，则以 deadline_at 为准，timeout_ms 仅用于诊断展示。
3. 若仅有 timeout_ms 且无 created_at，禁止落盘为完整对象，必须补齐 created_at。
4. ttl 不参与 deadline_at 计算，ttl 只控制对象有效期与可复用期。

### 4.4 传播范围建议

1. request/session/task 级对象：建议携带 created_at；有执行硬时限时携带 deadline_at。
2. step/tool_call 级对象：建议携带 timeout_ms，并在执行前换算到 deadline_at。
3. cache/lease/capability 快照：建议使用 ttl + created_at 进行过期判定。

### 4.5 兼容性建议

1. 字段单位统一为毫秒（ms），不得在无版本迁移时切换为秒。
2. 旧字段 timeout_seconds 若存在，先兼容读取并归一化为 timeout_ms。
3. 不在一个对象内同时使用多个语义重复字段（如 expires_at 与 deadline_at 混用）而不定义优先级。

### 4.6 Mode Extension

Design 模式产出：

1. 本文档为评审材料，不改代码。
2. 已输出对象语义、字段建议、边界说明、兼容性建议。
3. 可直接落盘路径：docs/todos/contracts-freeze/deliverables/WP02-T010-TimeDeadline规范.md。

Build 模式预备清单（不在本任务执行）：

1. 候选改动文件：contracts 中 request/session/task/lease 相关对象定义位置。
2. 关键接口：Deadline 计算器、超时判定器、TTL 过期判定器。
3. 冲突检查：若现有代码混用 timeout_seconds/timeout_ms，先加兼容转换层。
4. 验证步骤：
   - 构建：执行 CMake 构建验证 contracts/runtime。
   - 测试：覆盖 created_at/deadline_at/timeout_ms/ttl 的判定与换算。
   - 契约校验：验证单位统一、优先级规则和空值约束。

## 5. 验收清单

1. 已形成 Time/Deadline 规范文档。
2. 已明确区分 created_at、deadline_at、timeout_ms、ttl 四类字段。
3. 已定义换算规则、优先级规则与传播建议。
4. 产出可直接作为 T011 与 T013 输入。

Quality Gate 回答：

1. 当前任务是否达成 Done Criteria：是。
2. 产出是否可被下一任务直接消费：是，可被 T011 与 T013 直接消费。
3. 是否引入 breaking change 风险：当前为文档冻结，无实现变更；后续若切换字段单位或优先级会有 breaking 风险。
4. 是否需要触发 ADR 或版本变更流程：当前不需要；若重定义 deadline 与 timeout 的主语义关系，需触发版本评审流程。

## 6. 风险与回退

1. 风险：deadline_at 与 timeout_ms 在实现中双写且不一致。
   回退：固定以 deadline_at 为执行判定主字段，timeout_ms 仅作策略输入与展示。
2. 风险：ttl 被误用为执行超时，导致错误失败归因。
   回退：明确 ttl 仅用于对象有效期，执行超时仅由 deadline/timeout 判定。
3. 风险：历史代码使用秒单位引发误差。
   回退：先做秒到毫秒兼容转换并标记弃用字段，逐步迁移。

## 7. 下一任务建议

1. WP02-T011 设计 EventEnvelope 通用头部字段。
