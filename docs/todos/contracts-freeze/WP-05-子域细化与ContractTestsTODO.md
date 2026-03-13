# WP-05 子域细化与 Contract Tests TODO

最近更新时间：2026-03-13

## 1. 工作包目标

在整体骨架和高扇出边界稳定后，细化各子域对象，并建立 contracts V1 的测试与治理基线。

## 2. 完成标准

1. 各子域对象均可回溯到 WP-01 至 WP-04 的骨架对象。
2. 不存在子域对象越权承担主链路职责。
3. Contract Tests 覆盖序列化稳定性、错误码一致性、事件封套和受 ADR 约束字段边界。

## 3. 原子任务清单

| ID | 状态 | 任务 | 输入依据 | 交付物 | 完成判定 |
|---|---|---|---|---|---|
| WP05-T001 | Not Started | 制定子域细化总顺序和并行边界 | 计划文档第 8 阶段 4、5 | 子域推进顺序表 | 明确 tool、prompt、memory、task、event、llm 的顺序与并行限制 |
| WP05-T002 | Not Started | 细化 ToolRequest 的职责边界 | WP-03、WP-04 冻结包 | ToolRequest 语义说明 | 不重复定义 error、budget、observation 语义 |
| WP05-T003 | Not Started | 细化 ToolResult 的职责边界 | T002 输出 | ToolResult 语义说明 | 能被统一折叠到 Observation |
| WP05-T004 | Not Started | 细化 ToolDescriptor 和 ToolIR 的职责分层 | 架构文档 tools 章节 | ToolDescriptor/ToolIR 说明 | 明确注册描述与执行请求分层 |
| WP05-T005 | Not Started | 细化 PromptSpec 与 PromptRelease 对象 | WP-04 Prompt 边界 | PromptSpec/PromptRelease 定义 | 不反向修改 PromptComposeRequest/Result |
| WP05-T006 | Not Started | 细化 Turn、Session、SummaryMemory 对象 | WP-03 ContextPacket/Observation | Memory 对象定义 | 与 SessionContext、Checkpoint 边界清晰 |
| WP05-T007 | Not Started | 细化 MemoryFact 与 ExperienceMemory 对象 | 架构文档 memory 章节 | 记忆事实对象定义 | 支持写回闭环而不污染主链路对象 |
| WP05-T008 | Not Started | 细化 task 子域其余对象 | WP-04 多 Agent 边界 | 任务子域对象定义 | 不越权承担顶层 Session/FSM 语义 |
| WP05-T009 | Not Started | 细化 event 子域具体事件类型 | WP-02 EventEnvelope | EventType/EventPayload 设计稿 | 事件封套和 payload 分层清晰 |
| WP05-T010 | Not Started | 细化 LLMRequest 与 LLMResponse 的职责边界 | 架构文档 llm 章节、WP-04 Prompt 边界 | LLMRequest/LLMResponse 说明 | 不污染共享语义对象，不泄漏 provider 私有字段到主链路 |
| WP05-T011 | Not Started | 识别真正需要进入 contracts 的跨模块接口抽象 | 计划文档阶段 5 | 接口候选清单 | 仅保留稳定依赖面，不把所有接口都放入 contracts |
| WP05-T012 | Not Started | 评估每个接口候选的必要性和边界 | T011 输出 | 接口准入评估单 | 明确保留、推迟或退回模块内部 |
| WP05-T013 | Not Started | 定义序列化稳定性测试矩阵 | 计划文档阶段 5 | 序列化测试矩阵 | 覆盖核心对象的序列化/反序列化兼容性 |
| WP05-T014 | Not Started | 定义错误码与枚举兼容性测试矩阵 | WP-02 冻结包 | 错误码/枚举测试矩阵 | 能捕获 breaking 枚举变更和错误语义漂移 |
| WP05-T015 | Not Started | 定义 EventEnvelope 兼容性测试矩阵 | WP-02、event 子域设计 | 事件测试矩阵 | 覆盖封套头部稳定性和 payload 扩展约束 |
| WP05-T016 | Not Started | 定义 ADR 约束对象的边界测试矩阵 | WP-04 冻结包 | ADR 边界测试矩阵 | 能捕获 ContextPacket、ReflectionDecision、MultiAgentResult 等越界字段 |
| WP05-T017 | Not Started | 汇总 tests/contract 覆盖矩阵 | T013 至 T016 输出 | Contract Tests 覆盖矩阵 | 每个高风险对象至少对应一种契约测试 |
| WP05-T018 | Not Started | 建立版本变更记录模板 | 计划文档第 10 节 | 版本变更模板 | 区分 breaking 与 non-breaking 变更 |
| WP05-T019 | Not Started | 建立 contracts 变更评审流程清单 | T018 输出 | 变更流程清单 | breaking change 必须触发专门评审 |
| WP05-T020 | Not Started | 组织子域对象与测试评审 | T001 至 T019 输出 | 评审纪要 | 细化未回改前四个工作包结论 |
| WP05-T021 | Not Started | 发布 contracts V1 Ready 包 | T020 输出 | M5 冻结包 | 允许后续模块以 V1 contracts 为实现基线 |

## 4. 推荐执行顺序

1. 先做 T001 至 T010，完成子域对象细化。
2. 再做 T011、T012，确定哪些接口抽象进入 contracts。
3. 再做 T013 至 T019，建立测试与版本治理基线。
4. 最后做 T020、T021，形成 M5 输出。

## 5. 依赖与风险

1. 若子域对象在细化阶段回改主链路或边界对象，应退回对应前序工作包处理。
2. 若 Contract Tests 不围绕真实消费方依赖设计，会导致字段刚性耦合和后续演进停滞。
3. 若过早把 provider/serialization 技术细节写进共享对象，contracts 的稳定性会显著下降。
