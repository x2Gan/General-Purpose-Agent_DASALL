# WP-04 边界对象 TODO

最近更新时间：2026-03-13

## 1. 工作包目标

把 ADR-006、ADR-007、ADR-008 的边界裁定直接落实为 contracts 对象和字段约束，避免职责重新漂移。

## 2. 完成标准

1. 三份 ADR 的 contracts 影响项全部由具体对象承接。
2. 不存在一个字段同时承担两个边界主体职责。
3. Prompt、Recovery、多 Agent 协同边界均可通过对象定义直接审查。

## 3. 原子任务清单

| ID | 状态 | 任务 | 输入依据 | 交付物 | 完成判定 |
|---|---|---|---|---|---|
| WP04-T001 | Not Started | 梳理 ADR-006 的对象影响项 | ADR-006 | ADR-006 对象影响清单 | 明确 ContextPacket、PromptComposeRequest、PromptComposeResult 的关系 |
| WP04-T002 | Not Started | 定义 PromptComposeRequest 的职责边界 | ADR-006、WP-03 ContextPacket | PromptComposeRequest 语义说明 | 只能消费 ContextPacket，不替代 ContextPacket |
| WP04-T003 | Not Started | 列出 PromptComposeRequest 的字段清单 | T002 输出 | PromptComposeRequest 字段表 | 不包含 provider_payload 或渲染结果字段 |
| WP04-T004 | Not Started | 定义 PromptComposeResult 的职责边界 | ADR-006 | PromptComposeResult 语义说明 | 只表达渲染结果和相关元数据 |
| WP04-T005 | Not Started | 列出 PromptComposeResult 的字段清单 | T004 输出 | PromptComposeResult 字段表 | 明确与 PromptPolicy、LLMRequest 的衔接点 |
| WP04-T006 | Not Started | 梳理 ADR-007 的对象影响项 | ADR-007 | ADR-007 对象影响清单 | 明确 ReflectionDecision、RecoveryRequest、RecoveryOutcome 的关系 |
| WP04-T007 | Not Started | 定义 ReflectionDecision 的职责边界 | ADR-007 | ReflectionDecision 语义说明 | 只表达语义判断和建议，不含调度细节 |
| WP04-T008 | Not Started | 列出 ReflectionDecision 的字段清单 | T007 输出 | ReflectionDecision 字段表 | 包含 decision_kind、rationale、confidence、hint/ref，不含 retry_after_ms 等字段 |
| WP04-T009 | Not Started | 定义 RecoveryManager 消费的恢复请求对象 | ADR-007、WP-02/03 | RecoveryRequest 语义说明 | 明确其为运行时准入输入，而非第二个反思对象 |
| WP04-T010 | Not Started | 列出 RecoveryRequest 的字段清单 | T009 输出 | RecoveryRequest 字段表 | 至少包含 reflection_decision、error_info、checkpoint、budget、idempotency 报告 |
| WP04-T011 | Not Started | 定义 RecoveryOutcome 的职责边界 | ADR-007 | RecoveryOutcome 语义说明 | 只表达执行结果与控制元数据 |
| WP04-T012 | Not Started | 列出 RecoveryOutcome 的字段清单 | T011 输出 | RecoveryOutcome 字段表 | 包含 executed_action、final_runtime_state、rejection_reason、checkpoint_ref |
| WP04-T013 | Not Started | 梳理 ADR-008 的对象影响项 | ADR-008 | ADR-008 对象影响清单 | 明确 AgentRequest 与 MultiAgentRequest 的分层关系 |
| WP04-T014 | Not Started | 定义 MultiAgentRequest 的职责边界 | ADR-008 | MultiAgentRequest 语义说明 | 只表达协同子域请求，不复用 AgentRequest |
| WP04-T015 | Not Started | 列出 MultiAgentRequest 的字段清单 | T014 输出 | MultiAgentRequest 字段表 | 包含 parent_request_id、goal_fragment、plan_fragment、guards、stop_conditions |
| WP04-T016 | Not Started | 定义 MultiAgentResult 的职责边界 | ADR-008 | MultiAgentResult 语义说明 | 只表达协同结果，不承担最终 AgentResult 角色 |
| WP04-T017 | Not Started | 列出 MultiAgentResult 的字段清单 | T016 输出 | MultiAgentResult 字段表 | 包含 subtask_results、merged_result、conflicts、worker_trace_refs、recommended_next_action |
| WP04-T018 | Not Started | 定义 WorkerTask 的职责边界 | ADR-008 | WorkerTask 语义说明 | 只表达子任务执行单元，不携带全局 Session/FSM 语义 |
| WP04-T019 | Not Started | 列出 WorkerTask 的字段清单 | T018 输出 | WorkerTask 字段表 | 覆盖 task_id、parent_task_id、lease_id、worker_type、allowed_tools、timeout、idempotency_key |
| WP04-T020 | Not Started | 定义 WorkerLease 的职责边界 | ADR-008 | WorkerLease 语义说明 | 明确租约而非任务结果或调度策略 |
| WP04-T021 | Not Started | 列出 WorkerLease 的字段清单 | T020 输出 | WorkerLease 字段表 | 覆盖 lease_id、worker_ref、deadline、renewal、release_reason |
| WP04-T022 | Not Started | 建立 ADR 到字段级约束映射表 | T001 至 T021 输出 | ADR 字段映射表 | 每条 ADR 约束都能映射到具体对象或禁止字段 |
| WP04-T023 | Not Started | 组织边界对象评审 | T022 输出 | 评审纪要 | 不存在双主控、双上下文主控、双恢复主控迹象 |
| WP04-T024 | Not Started | 发布边界对象冻结版 | T023 输出 | M4 冻结包 | 可作为 prompt、runtime、multi_agent 设计基线 |

## 4. 推荐执行顺序

1. 先做 T001 至 T005，完成 Prompt 边界。
2. 再做 T006 至 T012，完成恢复边界。
3. 再做 T013 至 T021，完成多 Agent 边界。
4. 最后做 T022 至 T024，形成 M4 输出。

## 5. 依赖与风险

1. 若 ReflectionDecision 混入运行时调度字段，ADR-007 将被实质性回退。
2. 若 MultiAgentRequest 复用 AgentRequest，ADR-008 的全局主控/协同子域分层会失效。
3. 若 PromptComposeRequest 重新承载 ContextPacket 语义，ADR-006 将被实质性回退。
