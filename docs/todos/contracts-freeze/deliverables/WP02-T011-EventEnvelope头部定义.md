# WP02-T011 EventEnvelope 头部定义

最近更新时间：2026-03-14
任务状态：In Review
任务编号：WP02-T011
上游输入：架构文档事件链路

## 1. 任务理解

本任务只处理 WP02-T011：设计 EventEnvelope 通用头部字段，形成跨模块统一事件封套。

本任务完成判定：只包含通用封套，不含模块私有 payload。

本任务不扩展到事件类型全集定义、业务 payload schema 设计和代码实现。

## 2. 约束与边界

### 2.1 可追溯约束

1. 来自 WP-02 TODO：T011 要求 EventEnvelope 只包含通用封套字段。
2. 来自计划文档阶段 1：EventEnvelope 头部属于横切基础对象，必须统一且避免模块重复定义。
3. 来自架构文档事件链路：已有 EventEnvelope 建议结构，头部字段应覆盖 type、event_id、source、session_id、task_id、timestamp_ms、trace。
4. 来自 T009/T010：标识与时间语义已冻结，EventEnvelope 头部需复用 session_id/task_id/trace_id 与 timestamp_ms 口径。

### 2.2 边界

1. 本任务只定义封套头部字段及其语义，不定义模块 payload 内容。
2. 本任务不扩展 EventType 枚举全集与领域事件字典。
3. 不改写 ADR-006/007/008 结论，不改变 runtime/cognition/multi_agent 职责边界。

### 2.3 非目标

1. 不定义具体事件投递队列与重试算法。
2. 不定义事件持久化存储模型。
3. 不编写 contracts/event 代码。

## 3. 方案对比与决策

### 3.1 方案 A：头部与 payload 融合

设计思路：在 EventEnvelope 里直接混入模块专属业务字段，减少二层结构。

优点：

1. 单事件查看时字段更集中。

缺点：

1. 违反 T011 完成判定，边界失控。
2. 各模块字段冲突风险高，难以演进。
3. 不利于统一审计与通用处理器复用。

### 3.2 方案 B：严格通用头部 + 独立 payload

设计思路：EventEnvelope 只承载通用头部；模块私有信息全部进入 payload 子对象。

优点：

1. 满足 T011 完成判定。
2. 通用中间件可仅依赖头部完成路由、追踪、审计。
3. 模块 payload 可独立演进，减少 breaking 风险。

缺点：

1. 需要额外约束 payload 命名与 schema 管理。

### 3.3 决策

采用方案 B。

取舍理由：方案 B 在保持最小公共语义的同时，最大化模块自治与兼容性。

## 4. 最终产出

### 4.1 EventEnvelope 通用头部字段

| 字段 | 必填 | 类型建议 | 语义定义 | 来源对齐 |
|---|---|---|---|---|
| type | 是 | enum/string | 事件类型标识（仅标识，不承载业务数据） | 对齐 EventType 概念 |
| event_id | 是 | string | 单事件唯一 ID | 事件链路唯一标识 |
| source | 是 | string | 事件来源组件/模块标识 | 用于审计与归因 |
| session_id | 否 | string | 关联会话 ID | 对齐 T009 标识规范 |
| task_id | 否 | string | 关联任务 ID | 对齐 T009 标识规范 |
| timestamp_ms | 是 | uint64 | 事件产生时间（毫秒） | 对齐 T010 时间口径 |
| trace | 是 | object | 追踪上下文（至少可关联 trace_id） | 对齐 T009 标识传播 |
| payload | 是 | object | 业务负载容器，仅容纳模块私有内容 | 与头部分层 |

### 4.2 头部与 payload 分层规则

1. 头部只允许通用元数据字段，不允许出现模块私有业务字段。
2. payload 必须是独立对象，业务字段全部放入 payload。
3. 任一处理器可仅依赖头部完成路由、监控和审计，不解析 payload 也可工作。
4. 模块字段如 tool_result、policy_decision、memory_delta 等必须位于 payload，禁止上浮到头部。

### 4.3 兼容性建议

1. 头部字段采用新增优于修改，不重解释既有字段语义。
2. 头部字段名保持 snake_case，避免别名并行。
3. 若后续新增通用头部字段，必须证明其“跨模块通用”属性并经过评审。

### 4.4 示例（仅示意）

```json
{
  "type": "TOOL_RESULT_READY",
  "event_id": "evt_01hxyz",
  "source": "runtime.tool_worker",
  "session_id": "sess_01habc",
  "task_id": "task_01hdef",
  "timestamp_ms": 1768452305123,
  "trace": {
    "trace_id": "trc_01hijk"
  },
  "payload": {
    "tool_name": "web_search",
    "status": "ok"
  }
}
```

说明：

1. 示例中的 tool_name/status 属于模块私有数据，只出现在 payload。
2. 头部字段保持通用，不绑定工具、策略或记忆子域细节。

### 4.5 Mode Extension

Design 模式产出：

1. 本文档为评审材料，不改代码。
2. 已输出对象语义、字段建议、边界说明、兼容性建议。
3. 可直接落盘路径：docs/todos/contracts-freeze/deliverables/WP02-T011-EventEnvelope头部定义.md。

Build 模式预备清单（不在本任务执行）：

1. 候选改动文件：contracts/event 下 EventEnvelope/EventType 定义位置。
2. 关键接口：事件发布器、事件总线适配层、审计记录器。
3. 冲突检查：若现有实现把模块字段放在头部，先迁移到 payload 并做兼容读取。
4. 验证步骤：
   - 构建：执行 CMake 构建验证 contracts/runtime/event 相关目标。
   - 测试：覆盖头部字段完整性、payload 分层与追踪透传。
   - 契约校验：验证头部无模块私有字段污染。

## 5. 验收清单

1. 已形成 EventEnvelope 头部定义文档。
2. 已明确头部通用字段集合。
3. 已明确“模块私有字段只能放 payload”的边界。
4. 产出可直接被 T012/T013 消费。

Quality Gate 回答：

1. 当前任务是否达成 Done Criteria：是，封套仅包含通用头部，业务字段限定在 payload。
2. 产出是否可被下一任务直接消费：是。
3. 是否引入 breaking change 风险：当前为文档冻结，无实现变更；后续若调整头部字段语义会有 breaking 风险。
4. 是否需要触发 ADR 或版本变更流程：当前不需要；若新增非通用字段进头部，应触发评审。

## 6. 风险与回退

1. 风险：模块将私有字段继续上浮到头部，导致边界污染。
   回退：强制校验头部白名单，超出字段迁回 payload。
2. 风险：source/type 命名不一致影响路由聚合。
   回退：固定命名规范并提供兼容映射表，逐步清退别名。
3. 风险：trace 信息缺失导致事件不可追踪。
   回退：发布前校验 trace.trace_id 必填，缺失事件标记为无效。

## 7. 下一任务建议

1. WP02-T012 定义枚举默认值与弃用规则。
