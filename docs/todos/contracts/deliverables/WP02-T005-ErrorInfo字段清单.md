# WP02-T005 ErrorInfo 字段清单

最近更新时间：2026-03-14
任务状态：In Review
任务编号：WP02-T005
上游输入：WP02-T004、ADR-007、架构文档、实施计划

## 1. 任务理解

本任务只处理 WP02-T005：定义 ErrorInfo 必填字段集合，形成可评审、可落盘、可被后续任务直接消费的字段清单。

本任务完成判定必须覆盖以下字段：failure_type、retryable、safe_to_replan、details、source_ref。

本任务不扩展到 ErrorSource 详细引用规则、不定义完整 ResultCode 枚举全集、不下沉实现代码。

## 2. 约束与边界

### 2.1 可追溯约束

1. 来自 WP-02 TODO：T005 完成判定必须包含 failure_type、retryable、safe_to_replan、details、source_ref。
2. 来自 WP02-T004：failure_type 必须与 ResultCode 五大分类（validation、policy、tool、provider、runtime）保持一致口径。
3. 来自 ADR-007：ReflectionEngine 只负责失败语义建议，RecoveryManager 负责恢复准入与执行，ErrorInfo 需同时服务两者，不能混入执行控制细节。
4. 来自架构文档：Observation.error 统一承载 ErrorInfo，ErrorInfo 至少包含 failure_type、retryable、safe_to_replan、details。
5. 来自实施计划与总 TODO：contracts 冻结阶段遵循兼容优先，新增优于修改，语义重解释视为 breaking。

### 2.2 边界

1. 本任务只定义 ErrorInfo 必填字段与语义边界。
2. source_ref 只定义最小结构与引用对象类别，不定义跨对象跳转协议细节。
3. 不改写 ADR-007 结论，不重新定义 runtime/cognition 职责。

### 2.3 非目标

1. 不设计 T006 的 ErrorSource 全量引用规则。
2. 不设计 T007 RuntimeBudget 字段。
3. 不编写 contracts 头文件或序列化实现。

## 3. 方案对比与决策

### 3.1 方案 A：最小扁平字段集

设计思路：五个必填字段全部保持扁平表达，details 使用自由文本，source_ref 仅保存单个字符串。

优点：

1. 设计与实现门槛最低。
2. 对现有对象侵入小。

缺点：

1. details 难以稳定被机器消费，跨模块语义容易漂移。
2. source_ref 无法同时定位 observation/tool call/worker task/checkpoint。
3. 不利于 T006 直接承接。

### 3.2 方案 B：必填字段 + 结构化最小子结构

设计思路：保持五个必填顶层字段不变，同时对 details 与 source_ref 规定最小结构，保证可审计与可演进。

优点：

1. 满足 Done Criteria 且能直接支撑 T006。
2. 兼顾人读与机读，减少语义漂移。
3. 与 T004 分类框架、ADR-007 职责链一致。

缺点：

1. 文档评审需要统一字段解释口径。

### 3.3 决策

采用方案 B。

取舍理由：在不跨任务的前提下，方案 B 以最小结构化约束保障可追溯和可消费性，且不侵入 T006 的细节设计域。

## 4. 最终产出

### 4.1 ErrorInfo 必填字段清单（冻结版草案）

| 字段 | 必填 | 语义定义 | 建议类型 | 取值/结构约束 | 与 T004/ADR-007 对齐关系 |
|---|---|---|---|---|---|
| failure_type | 是 | 失败类别主键，用于表达失败根因归属 | enum/string | 仅允许 validation/policy/tool/provider/runtime 五类之一 | 对齐 T004 一级分类；供 Reflection 与 Recovery 共用 |
| retryable | 是 | 在当前运行上下文下，是否允许进入重试候选路径 | bool | 只表达候选性，不代表已执行重试 | 符合 ADR-007：执行权在 RecoveryManager |
| safe_to_replan | 是 | 当前失败是否适合进入重规划候选路径 | bool | 只表达语义建议，不代表已切换到 replan | 符合 ADR-007：Reflection 给建议，Runtime 做准入 |
| details | 是 | 失败上下文最小信息集合，用于诊断、审计、回放提示 | object | 至少包含 code、message、stage；可增量扩展 | 对齐架构中 ErrorInfo 诊断用途；兼容优先新增字段 |
| source_ref | 是 | 失败来源引用，指向可追溯对象 | object | 至少包含 ref_type、ref_id；ref_type 限 observation/tool_call/worker_task/checkpoint | 为 T006 提供输入，不提前定义引用协议 |

### 4.2 details 与 source_ref 最小结构建议

1. details 最小键：
   code：失败码标识（建议与 ResultCode 对齐）。
   message：面向日志和审计的人类可读信息。
   stage：失败发生阶段（如 validator、policy_gate、executor、runtime_recovery）。
2. source_ref 最小键：
   ref_type：observation、tool_call、worker_task、checkpoint。
   ref_id：对应对象唯一标识。

说明：

1. 上述仅为最小必需结构，不包含 T006 的跨对象解析规则与层级传播规则。
2. 若后续需要补充键，按“新增优于修改”原则演进。

### 4.3 字段判定口径

1. 先判定 failure_type，再判定 retryable 与 safe_to_replan，避免把恢复动作结果倒灌为失败根因。
2. retryable 与 safe_to_replan 可以同为 true，但最终是否执行由 RecoveryManager 准入裁定。
3. source_ref 必须指向真实对象，禁止填入不可解析的自然语言描述。

### 4.4 Mode Extension

Design 模式产出：

1. 本文档即评审材料，不做代码改动。
2. 产出对象语义、字段建议、边界与兼容性建议已完整给出。
3. 可直接落盘路径：docs/todos/contracts/deliverables/WP02-T005-ErrorInfo字段清单.md。

Build 模式预备清单（不在本任务执行）：

1. 候选改动文件：contracts/error 下 ErrorInfo 契约头文件及对应序列化/校验位置。
2. 关键接口：Observation.error 写入与 RecoveryManager/ReflectionEngine 读取接口。
3. 冲突前置检查：若现有实现把 retryable 当作“已重试”，需先修正文义再落盘。
4. 验证步骤：
   - 构建：执行 CMake 构建，确认 contracts 相关目标通过。
   - 测试：执行 contracts 与 runtime 的错误语义测试用例。
   - 契约校验：验证五个必填字段在失败路径均可观测。

## 5. 验收清单

1. 已输出 ErrorInfo 必填字段清单，且包含 failure_type、retryable、safe_to_replan、details、source_ref。
2. 字段语义与 T004 ResultCode 分类框架一致，failure_type 可直接映射五类失败。
3. 字段语义未越权覆盖 ADR-007 的执行裁定边界。
4. source_ref 已给出最小结构，能被 T006 直接消费。
5. 兼容性建议明确：新增优于修改、废弃优于删除、语义重解释视为 breaking。

Quality Gate 回答：

1. 当前任务是否达成 Done Criteria：是。
2. 产出是否可被下一任务直接消费：是，可直接作为 T006 输入。
3. 是否引入 breaking change 风险：当前仅文档冻结，风险低；若后续修改既有字段语义则为 breaking。
4. 是否需要触发 ADR 或版本变更流程：当前不需要；若新增 failure_type 一级分类或重定义字段语义，需要触发评审并考虑版本变更。

## 6. 风险与回退

1. 风险：failure_type 与 T004 分类口径漂移。
   回退：以 T004 分类表为单一事实源，先回滚新增分类定义，再评审。
2. 风险：retryable 与 safe_to_replan 被实现层误用为“已执行动作”。
   回退：恢复为“候选建议”语义，并在 RecoveryManager 准入处记录最终动作。
3. 风险：source_ref 先天不完整导致无法追溯。
   回退：先补齐 ref_type/ref_id 最小键，不扩展到 T006 之外。

## 7. 下一任务建议

1. WP02-T006 定义错误来源引用规则。
