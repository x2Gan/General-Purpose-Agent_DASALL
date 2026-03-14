# WP02-T013 Review Checklist v1

最近更新时间：2026-03-14
任务状态：In Review
任务编号：WP02-T013
上游输入：WP02-T002 至 WP02-T012 全部交付物

## 1. 任务理解

本任务只处理 WP02-T013：把 T002 至 T012 的规则收敛为可直接执行的横切基础对象评审清单（Review Checklist v1）。

本任务完成判定：Checklist 可直接用于后续对象评审。

本任务不新增新的契约规则，不进入代码实现。

## 2. 约束与边界

### 2.1 可追溯约束

1. 来自 T002：兼容性三原则（新增优于修改、废弃优于删除、语义变化视为 breaking）与变更分类门禁必须进入评审项。
2. 来自 T003：字段类型、可选性、多值性、枚举默认值的演进判定必须可检查。
3. 来自 T004-T012：ResultCode、ErrorInfo、ErrorSource、RuntimeBudget、BudgetSnapshot、标识、时间、EventEnvelope、枚举规则均需形成可核验项。
4. 来自 WP-02 TODO：T013 交付应可直接用于后续对象评审。

### 2.2 边界

1. 本任务只聚合并结构化既有规则，不新增对象语义。
2. 本任务只提供评审模板与判定项，不给出对象实现代码。
3. 不改写 ADR-006/007/008 和已冻结交付文档结论。

### 2.3 非目标

1. 不组织正式评审会议（属于 T014）。
2. 不发布冻结包（属于 T015）。
3. 不定义新的跨模块对象字段。

## 3. 方案对比与决策

### 3.1 方案 A：按任务文档分散检查

设计思路：评审时逐篇查阅 T002-T012 文档，不提供统一清单。

优点：

1. 无需新增模板文档。

缺点：

1. 执行成本高，漏检概率大。
2. 无法快速形成通过/驳回结论。
3. 不满足“可直接用于后续对象评审”的目标。

### 3.2 方案 B：统一 Checklist + Gate 判定模板

设计思路：将 T002-T012 规则转为可勾选条目，增加结论、风险、回退、breaking 判定区。

优点：

1. 可直接执行，评审一致性高。
2. 便于沉淀审计记录和遗留项。
3. 直接服务 T014 评审组织。

缺点：

1. 需要维护版本，后续规则变化需同步更新。

### 3.3 决策

采用方案 B。

取舍理由：方案 B 最符合 T013 目标，且不跨越本任务边界。

## 4. 最终产出

### 4.1 使用说明

1. 每次对象评审必须完整填写本清单，不得跳项。
2. 每项使用 `PASS/FAIL/N-A` 标记，并记录证据路径。
3. 若出现 `FAIL`，必须给出修复项与回退策略。
4. 若出现 breaking 候选，必须触发专门评审，不得并入普通通过结论。

### 4.2 评审元信息模板

| 项目 | 填写内容 |
|---|---|
| 评审对象 |  |
| 评审日期 |  |
| 评审人 |  |
| 关联任务/PR |  |
| 结论 | PASS / CONDITIONAL PASS / REJECT |
| 是否存在 breaking 候选 | YES / NO |

### 4.3 Checklist 条目（v1）

#### A. 兼容性总门禁（来源：T002）

| 编号 | 检查项 | 判定 | 证据 |
|---|---|---|---|
| A1 | 变更是否遵循“新增优于修改” | PASS/FAIL/N-A |  |
| A2 | 若存在废弃，是否遵循“废弃优于删除” | PASS/FAIL/N-A |  |
| A3 | 是否存在语义重解释；若有是否按 breaking 处理 | PASS/FAIL/N-A |  |
| A4 | 是否完成变更分类（non-breaking/review-required/breaking） | PASS/FAIL/N-A |  |
| A5 | 是否记录迁移说明与影响范围（若需要） | PASS/FAIL/N-A |  |

#### B. 字段演进规则（来源：T003）

| 编号 | 检查项 | 判定 | 证据 |
|---|---|---|---|
| B1 | 是否避免直接修改既有字段类型 | PASS/FAIL/N-A |  |
| B2 | 是否避免把可选字段改为强制字段 | PASS/FAIL/N-A |  |
| B3 | 是否避免把多值字段收窄为单值 | PASS/FAIL/N-A |  |
| B4 | 枚举是否保留未指定值并保持语义稳定 | PASS/FAIL/N-A |  |
| B5 | 是否避免用 bool 承载可扩展状态机语义 | PASS/FAIL/N-A |  |

#### C. 错误语义与来源（来源：T004-T006）

| 编号 | 检查项 | 判定 | 证据 |
|---|---|---|---|
| C1 | ResultCode 是否可区分 validation/policy/tool/provider/runtime | PASS/FAIL/N-A |  |
| C2 | ErrorInfo 是否包含 failure_type/retryable/safe_to_replan/details/source_ref | PASS/FAIL/N-A |  |
| C3 | source_ref 是否支持 observation/tool_call/worker_task/checkpoint | PASS/FAIL/N-A |  |
| C4 | 是否保持建议权与执行权分层（不越权改写 ADR-007） | PASS/FAIL/N-A |  |

#### D. 预算与快照（来源：T007-T008）

| 编号 | 检查项 | 判定 | 证据 |
|---|---|---|---|
| D1 | RuntimeBudget 是否覆盖 token/turn/tool_call/latency/replan | PASS/FAIL/N-A |  |
| D2 | BudgetSnapshot 是否包含 current/max/remaining/reject_reason | PASS/FAIL/N-A |  |
| D3 | remaining 是否与 max-current 口径一致 | PASS/FAIL/N-A |  |
| D4 | reject_reason 是否仅在拒绝/超限场景填写 | PASS/FAIL/N-A |  |

#### E. 标识与时间（来源：T009-T010）

| 编号 | 检查项 | 判定 | 证据 |
|---|---|---|---|
| E1 | request_id/session_id/trace_id/task_id/lease_id 命名规则是否统一 | PASS/FAIL/N-A |  |
| E2 | 唯一性域、传播范围、父子关系是否明确 | PASS/FAIL/N-A |  |
| E3 | 是否区分 created_at/deadline_at/timeout_ms/ttl | PASS/FAIL/N-A |  |
| E4 | 时间单位与优先级规则是否统一（ms、deadline 优先） | PASS/FAIL/N-A |  |

#### F. 事件封套与枚举（来源：T011-T012）

| 编号 | 检查项 | 判定 | 证据 |
|---|---|---|---|
| F1 | EventEnvelope 头部是否仅含通用字段 | PASS/FAIL/N-A |  |
| F2 | 模块私有字段是否全部位于 payload | PASS/FAIL/N-A |  |
| F3 | 每个枚举是否保留 Unspecified 值 | PASS/FAIL/N-A |  |
| F4 | 是否定义 deprecate 元数据与 removal_condition | PASS/FAIL/N-A |  |

### 4.4 结论与处置模板

| 项目 | 记录 |
|---|---|
| 评审结论 | PASS / CONDITIONAL PASS / REJECT |
| 主要 FAIL 项 |  |
| 必改项清单 |  |
| breaking 候选列表 |  |
| 风险项 |  |
| 回退策略 |  |
| 复审时间 |  |

### 4.5 Mode Extension

Design 模式产出：

1. 本文档即评审执行模板，不改代码。
2. 已给出对象语义检查项、边界检查项、兼容性检查项。
3. 可直接落盘路径：docs/todos/contracts-freeze/deliverables/WP02-T013-ReviewChecklist-v1.md。

Build 模式预备清单（不在本任务执行）：

1. 候选改动：将 checklist 映射到 CI 规则（lint/contract checks）。
2. 关键接口：契约验证器、变更分类器、审计结果输出器。
3. 冲突检查：若实现行为与 checklist 冲突，先输出冲突项并回滚到文档规则。
4. 验证步骤：
   - 构建：执行 CMake 构建。
   - 测试：执行契约兼容与字段演进测试。
   - 校验：抽样对象按 checklist 打分并复核一致性。

## 5. 验收清单

1. 已形成 Review Checklist v1 文档。
2. 已覆盖 T002-T012 的核心评审项。
3. 已提供可直接填写的评审模板与结论模板。
4. 可直接用于后续对象评审（满足 Done Criteria）。

Quality Gate 回答：

1. 当前任务是否达成 Done Criteria：是。
2. 产出是否可被下一任务直接消费：是，可直接用于 T014 评审组织。
3. 是否引入 breaking change 风险：当前仅文档冻结，无实现变更。
4. 是否需要触发 ADR 或版本变更流程：当前不需要；仅当评审中识别 breaking 候选时触发。

## 6. 风险与回退

1. 风险：Checklist 过粗导致误判通过。
   回退：对 FAIL 高频项细化子检查项并发布 v1.x 补充版。
2. 风险：评审者跳项或证据缺失。
   回退：将“证据列必填”设为门禁，缺失即判为无效评审。
3. 风险：规则更新后 checklist 未同步。
   回退：建立版本号与来源映射，规则变更触发 checklist 同步任务。

## 7. 下一任务建议

1. WP02-T014 组织横切基础对象评审。
