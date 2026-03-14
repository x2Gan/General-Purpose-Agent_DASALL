# WP02-T004 ResultCode 分类表

最近更新时间：2026-03-14
任务状态：In Review
任务编号：WP02-T004
上游输入：架构文档、规范文档、WP02-T002、WP02-T003

## 1. 任务理解

本任务目标是产出 ResultCode 的分类框架，用统一的错误码语义区分失败来源，支撑跨模块失败归因、恢复决策、审计与测试。

本任务只定义分类层级、命名框架、判定边界和评审口径，不定义 ErrorInfo 字段清单，不定义错误来源引用结构，不定义具体枚举值全集。

本任务完成后，至少应能稳定区分 validation、tool、provider、runtime、policy 五类失败，并可被 T005 与 T006 直接消费。

## 2. 约束与边界

### 2.1 直接约束

1. 来自 WP-02 TODO：T004 完成判定要求可区分 validation、tool、provider、runtime、policy 失败类别。
2. 来自架构文档：Tool 执行链路固定为 Validator -> Policy Gate -> Executor，且工具与 MCP 失败统一映射为 ErrorInfo，说明 ResultCode 需要覆盖校验失败、工具执行失败、Provider 失败与治理拒绝。
3. 来自架构文档与 ADR-007：Runtime 层承担超时、重试、熔断、降级、恢复裁定，说明 ResultCode 需要独立 runtime 失败域，不能混入 cognition 建议语义。
4. 来自 ADR-006：PromptPolicy 负责发送前合法性与治理校验，说明 policy 类失败不等于工具执行失败。
5. 来自工程规范：错误码、错误对象、失败原因优先通过 contracts 统一定义，且模块边界必须保留明确错误语义。
6. 来自 WP02-T002/T003：分类框架需符合兼容性与字段演进规则，避免通过重解释既有码值制造隐式 breaking。

### 2.2 本任务边界

1. 只定义 ResultCode 分类框架，不定义 ErrorInfo 必填字段。
2. 只定义类别与编码段，不下钻到 observation、tool call、worker task、checkpoint 的来源引用规则。
3. 不定义恢复策略本身，只定义可供恢复策略消费的失败类别语义。
4. 不改写 ADR-006、ADR-007、ADR-008 边界结论。
5. 不进入代码落盘与枚举完整实现。

### 2.3 非目标

1. 不设计 ErrorSource 引用约定。
2. 不设计 retryable 与 safe_to_replan 的最终判定字段。
3. 不设计 EventEnvelope 失败事件结构。
4. 不设计 provider 子类型完整目录。
5. 不设计失败码国际化消息模板。

## 3. 方案对比与决策

### 3.1 方案 A：扁平失败码池

把所有失败码放在同一枚举平面，仅靠名称区分来源。

优点：

1. 实现简单，初期上手快。

缺点：

1. 跨模块可读性差，难以做聚合统计与治理。
2. 后续扩展时容易出现名称冲突和语义漂移。
3. 不利于 T005、T006 做来源字段与引用规则设计。

### 3.2 方案 B：分域分层分类框架

按失败来源域先分一级类别，再在类别内定义二级子类，配套统一编码段和命名前缀。

优点：

1. 与架构分层和治理链路一致，追溯性强。
2. 可直接支持监控聚合、恢复策略分流和审计统计。
3. 便于后续增量扩展，兼容性控制更清晰。

缺点：

1. 文档规范要求更高，需要统一维护分类口径。

### 3.3 决策

选择方案 B。

决策理由：

1. 满足 T004 完成判定并且便于后续 T005、T006 消费。
2. 与 Validator、Policy Gate、Tool Executor、MCP Adapter、RecoveryManager 的职责分层一致。
3. 能在不进入代码实现的情况下先冻结语义边界，降低后续 breaking 风险。

## 4. 最终产出

### 4.1 ResultCode 分类框架

#### 4.1.1 一级分类

| 一级分类 | 分类语义 | 典型触发点 | 与其他分类的边界 |
|---|---|---|---|
| validation | 输入或参数不满足契约要求 | 请求归一化失败、工具参数校验失败、结构缺失或格式不合法 | 不包含权限拒绝与执行期失败 |
| policy | 治理层拒绝或需确认未通过 | Policy Gate deny、PromptPolicy 合法性拒绝、安全门禁拦截 | 不包含参数格式错误与执行异常 |
| tool | 工具执行链路内部失败 | Tool Executor 异常、工作流步骤失败、补偿失败 | 不包含远程 provider 协议级失败 |
| provider | 外部能力提供方失败 | MCP/模型/外部服务超时、握手失败、协议错误、不可用 | 不包含本地 runtime 调度与治理拒绝 |
| runtime | 控制平面失败 | 超时、重试预算耗尽、熔断、状态机非法迁移、checkpoint 异常 | 不包含业务参数错误与治理拒绝 |

#### 4.1.2 二级子类建议

| 一级分类 | 二级子类建议 | 说明 |
|---|---|---|
| validation | request_invalid, schema_invalid, field_missing, type_mismatch, value_out_of_range | 面向输入合法性，优先在边界早失败 |
| policy | denied, require_confirmation, trust_blocked, visibility_blocked, compliance_blocked | 面向治理决策，失败语义可审计 |
| tool | execution_failed, workflow_failed, compensation_failed, side_effect_risk, dependency_failed | 面向工具执行阶段与副作用治理 |
| provider | unavailable, timeout, protocol_error, auth_failed, quota_exceeded | 面向外部提供方和适配层问题 |
| runtime | step_timeout, retry_exhausted, circuit_open, budget_exhausted, state_conflict | 面向主循环控制与恢复链路 |

#### 4.1.3 编码段与命名框架

为保证可读性与后续扩展，建议 ResultCode 使用分类段编码和统一命名前缀。

| 一级分类 | 命名前缀 | 编码段建议 |
|---|---|---|
| validation | VALIDATION_ | 1000-1999 |
| policy | POLICY_ | 2000-2999 |
| tool | TOOL_ | 3000-3999 |
| provider | PROVIDER_ | 4000-4999 |
| runtime | RUNTIME_ | 5000-5999 |

说明：

1. 编码段只是分类框架，不是最终码值全集。
2. 各分类内部具体码位由后续任务按增量方式补充。
3. 任何跨分类重解释都按 breaking 处理。

#### 4.1.4 分类判定规则

1. 失败发生在参数校验与契约检查阶段，归 validation。
2. 失败由治理策略拒绝或确认门未通过，归 policy。
3. 失败发生在工具本地执行或工作流步骤，归 tool。
4. 失败来源于外部 provider 或协议适配，归 provider。
5. 失败来源于主循环调度、预算、状态机、恢复控制，归 runtime。
6. 若一次失败同时命中多个维度，按最上游根因优先分类；无法判定时标记 review-required，不在本任务中拍板。

#### 4.1.5 与后续任务接口

1. T005 应基于本分类框架定义 ErrorInfo 的 failure_type 与 ResultCode 对齐关系。
2. T006 应基于本分类框架定义 ErrorSource 引用规则，保证来源引用可回放。
3. T013 应把本分类判定规则固化为评审检查项。

### 4.2 ResultCode 分类表示例

| 示例码名 | 所属分类 | 说明 |
|---|---|---|
| VALIDATION_FIELD_MISSING | validation | 输入缺少必需字段 |
| POLICY_REQUIRE_CONFIRMATION | policy | 高风险动作未获得确认 |
| TOOL_EXECUTION_FAILED | tool | 工具执行返回失败 |
| PROVIDER_TIMEOUT | provider | 外部 Provider 超时 |
| RUNTIME_RETRY_EXHAUSTED | runtime | Runtime 恢复重试次数耗尽 |

### 4.3 文档路径与章节草案

本任务交付物路径：

1. docs/todos/contracts-freeze/deliverables/WP02-T004-ResultCode分类表.md

建议后续 M2 冻结包按以下顺序引用：

1. ResultCode 一级分类
2. 二级子类建议
3. 编码段与命名框架
4. 分类判定规则
5. 与 ErrorInfo 和 ErrorSource 的衔接说明

### 4.4 Build 模式补充

#### 4.4.1 代码变更清单

1. 本任务不产生 contracts 代码变更。
2. 本任务只新增 ResultCode 分类文档，并更新 WP-02 TODO 的状态与链接。
3. 枚举代码落盘应在 T005、T006、T012 与 T013 评审后统一推进。

#### 4.4.2 测试清单

1. 文档一致性检查：确认分类与架构治理链路一致。
2. 完成判定检查：确认覆盖 validation、tool、provider、runtime、policy。
3. 边界检查：确认未越权进入 ErrorInfo 字段设计与 ErrorSource 规则。
4. 构建测试检查：本任务无代码改动，不触发 CMake 构建与 CTest。

## 5. 验收清单

1. 已形成可审阅的 ResultCode 分类表。
2. 已明确区分 validation、tool、provider、runtime、policy 五类失败。
3. 已给出分类判定规则和命名框架。
4. 已明确与 T005、T006、T013 的输入输出关系。
5. 未发现前置依赖阻塞：T001-T003 已提供范围、兼容性和字段演进基线。

## 6. 风险与回退

### 6.1 主要风险

1. 风险：tool 与 provider 边界混淆，导致统计口径和恢复策略失真。
   应对：固定判定规则为先看失败根因来源，再看执行位置；远程协议/可用性问题优先归 provider。
2. 风险：policy 与 validation 混淆，导致治理拒绝被误当参数错误。
   应对：把是否由治理决策触发作为 policy 的唯一入口条件。
3. 风险：runtime 失败被分散到其他类别，削弱控制平面观测性。
   应对：凡涉及超时、预算、熔断、重试耗尽、状态机冲突，统一归 runtime。

### 6.2 回退策略

1. 若后续评审认为某分类边界不清，不直接改动下游对象定义，先在 T013/T014 形成分类口径决议。
2. 若实际实现发现分类不足，优先在既有一级分类下新增二级子类，不新增一级分类。
3. 若必须新增一级分类，应视为兼容性重大变更并走专门评审流程。

## 7. 下一任务建议

1. WP02-T005 设计 ErrorInfo 必填字段集合。
2. WP02-T006 定义错误来源引用规则。
3. WP02-T012 定义枚举默认值与弃用规则。

## 8. Quality Gate 回答

1. 当前任务是否达成 Done Criteria：是，已明确区分 validation、tool、provider、runtime、policy。
2. 产出是否可被下一任务直接消费：是，可直接作为 T005 与 T006 的输入。
3. 是否引入 breaking change 风险：否，本任务仅冻结分类框架，不改动现有代码或已冻结对象边界。
4. 是否需要触发 ADR 或版本变更流程：否，本任务未改写 ADR 结论，也未进入实现消费期的代码变更。