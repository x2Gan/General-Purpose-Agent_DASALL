# DASALL 工程落地实现步骤指引（分阶段顺序版）

## 1. 文档目的

本指引用于将《DASALL Agent 架构设计文档》与《DASALL Agent 工程架构蓝图》转化为可执行的工程落地顺序，确保后续按统一节奏逐个子系统开展深化设计、实现、测试与验收。

适用范围：
- x86 桌面/工作站 Linux
- ARM Embedded Linux
- 单 Agent 到多 Agent 的渐进式建设

---

## 2. 实施总原则

1. 契约优先：先冻结 `contracts/`，再实现各模块。
2. 主控链路不可绕过：所有执行动作必须经过 Runtime 主控和 Tool 治理链路。
3. 先底座后能力：先打通 `infra/`、`platform/`、`services/`，再上层智能能力。
4. 先单 Agent 最小闭环，再扩展多 Agent、MCP、知识增强。
5. Profile 驱动落地：同一主流程，不分叉代码路径，以 `profiles/` 控制裁剪。
6. 每阶段都包含测试与可观测建设，不做“先实现后补测试”。

---

## 3. 推荐实施顺序总览

按依赖关系与风险收敛，推荐以下顺序：

1. 阶段 A：工程基线与开发骨架（仓库、构建、CI、规范）
2. 阶段 B：契约层冻结与契约测试（`contracts/`）
3. 阶段 C：基础设施与平台底座（`infra/` + `platform/`）
4. 阶段 D：能力服务层（`services/`）
5. 阶段 E：LLM 子系统（`llm/`）
6. 阶段 F：工具治理子系统（`tools/`）
7. 阶段 G：记忆子系统（`memory/`）
8. 阶段 H：知识子系统（`knowledge/`）
9. 阶段 I：认知子系统（`cognition/`）
10. 阶段 J：运行时主控平面（`runtime/`）
11. 阶段 K：产品入口层（`apps/`）
12. 阶段 L：多 Agent 协同（`multi_agent/`）
13. 阶段 M：Profile 全量收敛与发布前加固（`profiles/` + 全链路）

说明：
- `skills/` 资产建设贯穿阶段 I~L，与 Planner/Tool/Workflow 联动推进。
- `tests/` 每阶段同步落地，不独立后置。

---

## 4. 分阶段实施细则

## 阶段 A：工程基线与开发骨架

目标：建立可持续开发与验证的基础工程能力。

实施项：
1. 创建顶层目录骨架与各模块 `CMakeLists.txt`。
2. 建立统一编译选项、第三方依赖接入策略（submodule 或 FetchContent）。
3. 建立基础 CI 流水线：编译、单测、契约测试、静态检查。
4. 初始化 `tests/` 目录结构与公共 Mock 框架。
5. 建立编码规范、命名规范、分支与提交流程。

阶段产出：
- 可编译空壳工程
- 最小 CI 绿灯
- 工程规范文档

准入门槛（Gate）：
- x86 环境可完成全量空壳编译
- 单元测试框架可执行

---

## 阶段 B：契约层冻结与契约测试（`contracts/`）

目标：冻结跨模块共享对象，消除后续接口返工风险。

实施项：
1. 完成核心契约头文件定义：
   - `agent/`, `context/`, `observation/`, `memory/`, `tool/`, `llm/`, `prompt/`, `policy/`, `task/`, `event/`, `error/`, `checkpoint/`
2. 统一错误码语义、事件封装规范、序列化约定。
3. 建立 `tests/contract/`：序列化/反序列化稳定性、向后兼容检查。

阶段产出：
- 冻结版 V1 契约包
- 契约测试基线

准入门槛（Gate）：
- 契约测试通过率 100%
- 关键结构体字段完成评审并锁定版本

---

## 阶段 C：基础设施与平台底座（`infra/` + `platform/`）

目标：先把运行底座做稳定，给上层提供统一运行环境。

实施项：
1. `infra/`：
   - logging（结构化日志 + trace_id）
   - metrics（时延/成功率/队列积压）
   - tracing（调用链追踪）
   - config（默认/Profile/部署/运行时覆盖）
   - secret（敏感配置隔离）
   - health/watchdog（关键线程保活）
2. `platform/`：
   - Linux 通用抽象：线程、定时器、队列、文件、网络、IPC
   - ARM HAL 接口定义与占位实现（GPIO/UART/I2C/SPI/CAN）
3. 首版 `profiles/desktop_full` 与 `profiles/edge_balanced` 骨架策略。

阶段产出：
- 可被上层直接依赖的基础组件
- 配置中心与观测链路可用

准入门槛（Gate）：
- 在 x86 上完成基础组件联调
- Watchdog 与日志审计链路可验证

---

## 阶段 D：能力服务层（`services/`）

目标：建立对外部执行和数据能力的稳定服务接口，隔离业务细节。

实施项：
1. 定义并实现：`IExecutionService`、`IDataService`、系统状态服务接口。
2. 加入风险治理：高风险动作确认门、串行化、熔断、审计。
3. 补齐服务层单测与 Mock（供 `tools/` 集成测试复用）。

阶段产出：
- 可供工具层调用的能力服务 API

准入门槛（Gate）：
- 危险动作防护逻辑全部可测
- 服务接口稳定并评审通过

---

## 阶段 E：LLM 子系统（`llm/`）

目标：提供统一模型调用能力和 Prompt 治理能力。

实施项：
1. 实现 `ILLMAdapter` 统一抽象及 `LLMManager`。
2. 实现 `ModelRouter` 与回退链路（Cloud -> LAN -> Local）。
3. 建设 Prompt 三段：`PromptRegistry`、`PromptComposer`、`PromptPolicy`。
4. 输出语义统一映射：DirectResponse / ToolCallIntent / ClarificationRequest / ReplanSuggestion。

阶段产出：
- 可稳定调用的 LLM 接口层
- Prompt 版本与治理机制

准入门槛（Gate）：
- 模型回退链路可通过故障注入验证
- Prompt 审计字段完整（prompt_id/version/model_name）

---

## 阶段 F：工具治理子系统（`tools/`）

目标：实现“受控执行”主链路，确保工具调用安全可审计。

实施项：
1. 打通治理流水线：Route -> Registry -> Validator -> PolicyGate -> Executor -> Audit -> ObservationDigest。
2. 首批内置工具落地：
   - Information Tool（只读）
   - Action Tool（带确认门）
3. 建立 Tool IR 与 ToolDescriptor 注册机制。
4. 实现失败补偿与回滚骨架。
5. MCP 先做能力抽象与缓存框架，不急于全量接入。

阶段产出：
- 可审计、可回滚的工具执行链路

准入门槛（Gate）：
- 所有 Action Tool 必须经过 PolicyGate
- ObservationDigest 字段完整输出

---

## 阶段 G：记忆子系统（`memory/`）

目标：完成上下文编排与多层记忆写回闭环。

实施项：
1. 分层实现：Working / ShortTerm / LongTerm / Experience（Vector 可后置到阶段 H）。
2. 实现 `ContextOrchestrator` 装配顺序与 Token 预算裁剪策略。
3. 建立会话快照与恢复所需最小持久化模型。
4. 建立写回策略：决策、事实、工具结果、失败经验。

阶段产出：
- 稳定的上下文构建能力
- 记忆写回闭环

准入门槛（Gate）：
- 多轮会话上下文连续性测试通过
- 预算压缩策略符合架构约束

---

## 阶段 H：知识子系统（`knowledge/`）

目标：形成检索增强证据供给能力。

实施项：
1. 实现 Retriever（向量 + 关键词混合召回）。
2. 实现 Reranker 与 EvidenceBuilder。
3. 实现 IndexManager（增量更新、版本标记）。
4. 与 `memory/VectorMemory` 对接（在 profile 可关闭）。

阶段产出：
- 可供认知层消费的证据包接口

准入门槛（Gate）：
- 检索到证据的结构化输出稳定
- 索引更新不影响在线查询可用性

---

## 阶段 I：认知子系统（`cognition/`）

目标：建立从理解到决策的认知链路，但不直接执行动作。

实施项：
1. 逐步实现：PerceptionEngine -> Planner -> Reasoner -> ReflectionEngine -> ResponseBuilder。
2. 接入 LLM 子系统并约束输出语义。
3. 显式输出自评信号：confidence / high_uncertainty_assumption / clarification_needed。
4. 仅输出 `ActionDecision`，不越界调用执行实现。

阶段产出：
- 可驱动执行层的认知决策引擎

准入门槛（Gate）：
- 澄清请求、重规划建议、终止策略可覆盖主要分支
- 认知层依赖关系符合禁止规则

---

## 阶段 J：运行时主控平面（`runtime/`）

目标：打通 Agent 主循环与可恢复运行机制，形成单 Agent 闭环。

实施项：
1. 实现核心组件：
   - AgentFacade
   - AgentOrchestrator
   - SessionManager
   - AgentFSM
   - Scheduler
   - BudgetController
   - RecoveryManager
   - CheckpointManager
2. 落地状态机与关键防护参数：
   - MAX_TOOL_CALLS
   - MAX_REPLAN_COUNT
   - STEP_TIMEOUT_SECONDS
   - SESSION_TIMEOUT_SECONDS
3. 打通认知、工具、记忆、知识调用编排。
4. 完成 checkpoint 保存与 resume 流程。

阶段产出：
- 单 Agent 主流程可运行
- 失败恢复链路可验证

准入门槛（Gate）：
- 主状态机全分支可覆盖
- 异常/超时可降级并安全收敛

---

## 阶段 K：产品入口层（`apps/`）

目标：形成真实可交互产品入口，完成端到端业务接入。

实施项：
1. 优先落地 `apps/cli/`（开发调试入口）。
2. 再落地 `apps/gateway/`（HTTP/WebSocket），最后 `daemon/` 与 `simulator/`。
3. 实现接入链路：协议适配 -> 认证鉴权 -> 请求归一化 -> Runtime。
4. 打通流式响应输出与 trace 透传。

阶段产出：
- 可对外服务的接入层

准入门槛（Gate）：
- CLI 与 Gateway 均可跑通单 Agent 全流程
- 请求追踪字段完整（request_id/session_id/trace_id）

---

## 阶段 L：多 Agent 协同（`multi_agent/`）

目标：在单 Agent 稳定后引入协同能力，避免过早复杂化。

实施项：
1. 实现 `AgentRegistry`、`MultiAgentCoordinator`、`ResultMerger`。
2. 引入 Orchestrator-Worker 模式（默认模式）。
3. 落地失败策略：RESCHEDULE / REPLAN / SKIP / ABORT_AND_ROLLBACK。
4. 统一链路追踪字段：trace_id、agent_id、task_id、lease_id。

阶段产出：
- 多 Agent 子任务分派与汇聚能力

准入门槛（Gate）：
- 至少一个真实任务场景完成多 Agent 闭环
- 冲突合并与失败回收可验证

---

## 阶段 M：Profile 全量收敛与发布前加固

目标：按部署形态完成裁剪、验证、压测、运维准备。

实施项：
1. 完成全部 Profile：desktop_full / cloud_full / edge_balanced / edge_minimal / factory_test。
2. 建立各 Profile 的模块启用矩阵自动校验。
3. 完成 e2e 必测场景 10 项全覆盖。
4. 完成 stress 长稳验证（72h+）与资源基线记录。
5. 完成部署、健康检查、升级、回滚脚本联调。

阶段产出：
- 可发布版本候选（RC）
- Profile 合规报告与测试报告

准入门槛（Gate）：
- 关键链路无 P0/P1 缺陷
- 目标平台通过发布验收清单

---

## 5. 子系统深化设计进入时机（建议）

1. `contracts/`：阶段 B 立即进入全面深化（优先级最高）。
2. `infra/`、`platform/`：阶段 C 进入深化，作为后续模块公共底座。
3. `services/`：阶段 D 深化，确保工具层有稳定依赖。
4. `llm/`：阶段 E 深化，先建立统一调用与回退机制。
5. `tools/`：阶段 F 深化，先完成治理链路再扩展工具类型。
6. `memory/`：阶段 G 深化，确保上下文闭环先于高级智能策略。
7. `knowledge/`：阶段 H 深化，避免过早引入检索复杂性。
8. `cognition/`：阶段 I 深化，在执行基础能力齐备后推进。
9. `runtime/`：阶段 J 深化，作为单 Agent 生产级闭环主控。
10. `apps/`：阶段 K 深化，对外接入放在主流程稳定之后。
11. `multi_agent/`：阶段 L 深化，单 Agent 稳定后再扩展协同。
12. `profiles/`：阶段 C 起草，阶段 M 完成全量收敛与发布验收。

---

## 6. 并行开发建议（不改变主顺序）

可并行轨道 A（工程治理）：
- CI/CD、代码规范、静态扫描、覆盖率报告

可并行轨道 B（测试资产）：
- `tests/mocks/`、契约测试模板、故障注入工具

可并行轨道 C（Skill 资产）：
- `skills/specs`、`skills/workflows`、`skills/evals` 与阶段 I~L 对齐

约束：
- 并行仅限于低耦合资产，不允许跨越依赖关系提前实现上层核心逻辑。

---

## 7. 里程碑建议（用于项目管理）

1. M1（阶段 A~C）：工程可运行底座完成。
2. M2（阶段 D~F）：受控执行链路完成（服务 + LLM + 工具）。
3. M3（阶段 G~J）：单 Agent 生产级闭环完成（记忆 + 认知 + Runtime）。
4. M4（阶段 K~L）：产品接入与多 Agent 协同完成。
5. M5（阶段 M）：多 Profile 收敛与发布验收完成。

---

## 8. 下一步执行建议（从现在开始）

当前建议立即启动的首批工作包：

1. 工作包 WP-01：阶段 A（工程骨架 + CI）
2. 工作包 WP-02：阶段 B（`contracts/` 全量冻结 + 契约测试）
3. 工作包 WP-03：阶段 C（`infra/logging+config+health` 与 `platform/linux` 最小实现）

完成上述 3 个工作包后，再进入阶段 D 的 `services/` 深化设计评审。

---

文档版本：v1.0  
日期：2026-03-12  
依据文档：
- `docs/architecture/DASSALL_Agent_architecture.md`
- `docs/architecture/DASALL_Engineering_Blueprint.md`
