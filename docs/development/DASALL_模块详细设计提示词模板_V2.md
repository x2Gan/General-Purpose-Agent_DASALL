# DASALL 模块详细设计提示词模板 V2

文档版本：v2.0  
日期：2026-03-23  
状态：Active  
策略定位：通用模板 + 子模板（建议级增强）

---

## 1. 目标与适用范围

本模板用于在 DASALL 中对单一模块执行详细设计，并产出可直接用于任务拆解与评审的模块设计方案文档。

适用场景：

1. 依据架构文档、工程蓝图、ADR 与 contracts 冻结计划完成模块详细设计。
2. 形成可追溯设计结论，并将设计结论映射到 Build 可执行任务。
3. 在不跨模块扩张的前提下，为后续代码落地提供统一输入。

非目标：

1. 本模板不替代 Build 执行模板，不直接充当代码开发指令。
2. 本模板不用于一次覆盖多个模块。
3. 本模板不用于改写已冻结 ADR 结论。

---

## 2. 使用方式

1. 先复制第 4 节通用主模板。
2. 按第 3 节填充输入占位符。
3. 根据模块类型附加第 6 节对应子模板（可多选，但建议最多 1-2 个）。
4. 要求 LLM 按第 5 节字段级清单输出到模块设计方案文档。
5. 用第 7 节质量门进行评审，评审通过后再进入 Build 拆解。

建议执行节奏：

1. 一次只做一个模块。
2. 先评审设计文档，再做 Build TODO。
3. 设计结论回写到对应 TODO 或交付记录，保证可追溯。

---

## 3. 输入占位符（执行前必须替换）

- <PROJECT_ROOT>：项目根目录
- <DATE>：当前日期
- <MODULE_NAME>：模块名（如 runtime / llm / memory）
- <OUTPUT_DOC_PATH>：模块设计方案输出路径
- <ARCH_DOC_PATHS>：架构文档路径列表
- <BLUEPRINT_DOC_PATHS>：工程蓝图路径列表
- <ADR_PATHS>：相关 ADR 路径列表
- <CONTRACTS_PLAN_AND_TODOS>：contracts 计划与 TODO 路径
- <DEV_RULES_PATH>：工程协作规范路径
- <MODULE_CODE_PATHS>：当前模块代码与测试路径
- <EXTERNAL_REFERENCES_EXPECTED>：可选，行业参考方向

---

## 4. 可直接复制的通用主模板

### 4.1 Role

你是一名资深 C/C++ Agent 系统架构与工程落地专家，负责在 DASALL 项目中完成 LLM 子系统级详细设计。

你的输出必须同时满足：

1. 架构一致性
2. ADR 边界一致性
3. contracts 冻结策略一致性
4. 工程可实现性
5. 测试可验证性
6. 版本可演进性

### 4.2 Context

- 项目根目录：/home/gangan/DASALL/
- 当前日期：2026-4-10
- 当前模块：LLM 子系统
- 设计阶段：子系统架构设计/Detailed Design
- 输出语言：中文
- 输出文档：docs/architecture/

### 4.3 Inputs

请严格基于以下输入，不得跳过约束：

1. 架构文档：docs/architecture/DASSALL_Agent_architecture.md
2. 工程蓝图：docs/architecture/DASALL_Engineering_Blueprint.md
3. 相关 ADR：docs/adr
4. SSOT: docs/ssot
5. 交付的子系统设计：
   - docs/architecture/DASALL_profiles模块详细设计.md
   - docs/architecture/platform_linux_detailed_design.md
   - docs/architecture/DASALL_infrastructure子系统详细设计.md
     1. docs/architecture/DASALL_infra_audit模块详细设计.md
     2. docs/architecture/DASALL_infra_config模块详细设计方案.md
     3. docs/architecture/DASALL_infra_diagnostics模块详细设计.md
     4. docs/architecture/DASALL_infra_health模块详细设计.md
     5. docs/architecture/DASALL_infra_logging模块详细设计.md
     6. docs/architecture/DASALL_infra_metrics模块详细设计.md
     7. docs/architecture/DASALL_infra_OTA模块详细设计.md
     8. docs/architecture/DASALL_infra_plugin模块详细设计.md
     9. docs/architecture/DASALL_infra_policy模块详细设计.md
     10. docs/architecture/DASALL_infra_secret模块详细设计.md
     11. docs/architecture/DASALL_infra_tracing模块详细设计.md
     12. docs/architecture/DASALL_infra_watchdog模块详细设计.md
   - docs/architecture/DASALL_boundary治理与优化说明.md
   - docs/architecture/DASALL_capability_services子系统详细设计.md
6. 交付的实现：
   - docs/todos/contracts/deliverables
   - docs/todos/infrastructure/deliverables
   - docs/todos/platform/deliverables
   - docs/todos/services/deliverables
7. 工程规范：docs/development/DASALL_工程协作与编码规范.md
8. 当前模块代码骨架：N/A
9. 可选行业参考方向：联网搜索行业内实践方案

### 4.4 Task

请输出一份完整的LLM 子系统详细设计方案，要求：

1. 完整详细且专业的子系统设计描述，配合流程图/时序图/数据流图进行说明
2. 明确边界、职责、输入输出与相邻模块依赖方向。
3. 给出子组件拆分、职责边界、接口语义、核心对象与关键流程。
4. 明确异常语义、恢复路径、可观测性与配置策略。
5. 输出可映射到 Build 的实施分解建议。
6. 输出测试策略、质量门、风险与回退策略。

### 4.5 约束策略

基础硬约束（必须遵守）：

1. 不改写已冻结 ADR/SSOT 结论。
2. 不把实现细节反向写入 contracts 共享语义对象。
3. 不跨模块扩张到无关工作范围。
4. 不输出纯概念方案，必须落到可实现结构。

建议级增强项（强烈建议纳入，若未纳入需说明理由）：

1. Design -> Build 映射表。
2. Build 三件套建议字段（代码目标、测试目标、验收命令）。
3. 阻塞项与解阻条件表。
4. 兼容性与演进评估表。

### 4.6 Required Workflow（按顺序执行）

#### Step 1: 约束抽取

1. 提取模块职责、层级边界、上下游依赖方向。
2. 提取 ADR 硬约束与禁止事项。
3. 提取 contracts 当前阶段允许/禁止动作。

输出：约束清单（Must / Should / Must-Not）。

#### Step 2: 现状分析

1. 识别模块现有实现状态（已实现/占位/缺失）。
2. 识别现状与目标差距。
3. 识别风险冲突（边界冲突、语义重复、依赖反转）。

输出：现状-目标差距表。

#### Step 3: 行业方案调研与候选设计

至少给出 2 个候选方案，每个方案包含：

1. 设计思路
2. 组件结构
3. 优点
4. 风险
5. 与 DASALL 约束匹配度

输出：候选方案对比矩阵。

#### Step 4: 方案决策

1. 明确最终选型。
2. 给出放弃其他候选方案的理由。
3. 说明与架构、ADR、contracts 的一致性。

输出：决策结论与依据。

#### Step 5: 模块详细设计

至少覆盖以下设计面：

1. 职责边界
2. 子组件清单与组件职责
3. 子组件输入/输出
4. 子组件依赖关系
5. 核心对象与 contracts 对齐关系
6. 核心接口语义定义
7. 主流程时序
8. 异常与恢复时序
9. 配置项与默认策略
10. 可观测性（日志/指标/追踪/审计）

输出：详细设计正文。

#### Step 6: Design -> Build 映射（建议级）

1. 将 Step 5 的关键设计结论映射为 Build 目标。
2. 对每项映射补充建议三件套：代码目标、测试目标、验收命令。
3. 对无法映射项标注原因与后续动作。

输出：Design -> Build 映射表。

#### Step 7: 工程落地设计

1. 给出目录与文件落盘建议（include/src/tests）。
2. 给出分阶段实施计划（最小可交付切分）。
3. 给出每阶段完成判定。
4. 给出阻塞项、解阻条件与回退策略（建议级）。

输出：实施计划与里程碑。

#### Step 8: 测试与质量门

至少覆盖：

1. 单元测试范围
2. 契约测试影响点
3. 集成测试路径
4. 失败注入测试点
5. 兼容性检查点

输出：测试矩阵与 Gate 建议清单。

#### Step 9: 兼容性与演进评估（建议级）

1. 识别 breaking risk 级别（None/Low/Medium/High）。
2. 给出兼容迁移路径与灰度策略。
3. 给出后续版本扩展预留点。

输出：兼容性与演进评估。

### 4.7 输出格式（固定）

请按以下章节输出最终文档：

1. 模块概览
2. 约束清单
3. 现状与缺口
4. 候选方案对比
5. 决策结论
6. 详细设计
7. Design -> Build 映射（建议级）
8. 实施计划与里程碑
9. 测试与质量门
10. 兼容性与演进评估（建议级）
11. 风险、阻塞与回退（建议级）
12. 未决问题与后续任务

### 4.8 输出风格

1. 所有结论要有来源依据（架构/ADR/contracts/代码现状）。
2. 尽量使用表格表达映射、风险、测试矩阵与任务分解。
3. 任务描述使用工程动词（新增、重构、补齐、校验、收敛、迁移）。
4. 验收描述必须二值可判定，避免模糊词。
5. 理论、原理描述应该保持语言丰富

---

## 5. 字段级清单（用于审查输出是否可交付）

### 5.1 约束清单字段

| 字段 | 说明 |
|---|---|
| Constraint ID | 约束唯一标识 |
| 来源文档 | 架构/ADR/contracts/规范 |
| 类型 | Must / Should / Must-Not |
| 约束描述 | 可执行约束语句 |
| 影响范围 | 子组件/接口/测试/流程 |

### 5.2 现状-目标差距字段

| 字段 | 说明 |
|---|---|
| 设计目标 | 本项要达到的目标状态 |
| 当前状态 | 已实现/占位/缺失 |
| 差距描述 | 与目标差异 |
| 风险等级 | Low/Medium/High |
| 修复优先级 | P0/P1/P2 |

### 5.3 候选方案对比字段

| 字段 | 说明 |
|---|---|
| 方案名 | 候选方案标识 |
| 架构匹配度 | 与架构边界一致程度 |
| ADR匹配度 | 与 ADR 约束一致程度 |
| 工程复杂度 | 低/中/高 |
| 风险 | 关键风险摘要 |
| 结论 | 保留/淘汰 + 原因 |

### 5.4 详细设计字段

| 字段 | 说明 |
|---|---|
| 子组件 | 名称 + 职责 |
| 输入输出 | 输入来源、输出去向、语义契约 |
| 核心对象 | 对象名、关键字段、约束 |
| 核心接口 | 接口语义、前后置条件、错误语义 |
| 主流程 | 关键步骤与状态变化 |
| 异常恢复 | 异常分类、恢复动作、失败兜底 |
| 配置策略 | 配置项、默认值、覆盖层级 |
| 可观测性 | 日志点、指标、追踪、审计 |

### 5.5 Design -> Build 映射字段（建议级）

| 字段 | 说明 |
|---|---|
| Design结论 | 来自详细设计的结论项 |
| Build目标 | 可实现的工程目标 |
| 映射说明 | 为什么这样映射 |
| 代码目标 | 文件/目录/接口改动建议 |
| 测试目标 | unit/contract/integration 建议 |
| 验收命令 | 可直接执行命令模板 |
| 依赖/阻塞 | 前置条件与阻塞说明 |

### 5.6 原子实施任务字段（建议级）

| 字段 | 说明 |
|---|---|
| ID | 任务唯一标识 |
| 状态 | Not Started/In Progress/In Review/Blocked/Done |
| 任务描述 | 动词开头 |
| 输入依据 | 可追溯来源 |
| 代码目标 | 具体改动范围 |
| 测试目标 | 测试文件/用例范围 |
| 验收命令 | 可执行命令 |
| 完成判定 | 二值标准 |

### 5.7 阻塞管理字段（建议级）

| 字段 | 说明 |
|---|---|
| 阻塞项 | 具体阻塞描述 |
| 影响任务 | 受影响任务 ID |
| 解阻条件 | 可执行、可验证 |
| 最小解阻动作 | 本轮可执行最小动作 |
| 回退策略 | 无法解阻时的回退路径 |

### 5.8 兼容与演进字段（建议级）

| 字段 | 说明 |
|---|---|
| breaking risk | None/Low/Medium/High |
| 影响消费者 | 受影响模块或接口方 |
| 迁移路径 | 过渡方案与步骤 |
| 灰度策略 | 发布与回滚策略 |
| 扩展预留 | 下一版本预留点 |

---

## 6. 子模板（在通用模板基础上叠加）

使用规则：

1. 子模板只补充分析维度，不改通用主流程。
2. 推荐一次叠加 1 个子模板；最多 2 个。

### 6.1 runtime 子模板

附加关注点：

1. 主链路状态机与生命周期边界。
2. 调度、超时、重试、补偿策略。
3. 失败语义与恢复动作的职责分离。
4. 与 multi_agent、cognition 的边界与依赖方向。

附加输出：

1. 运行态时序图文字化描述。
2. 失败恢复路径矩阵（异常类型 -> 恢复动作 -> 终态）。

### 6.2 llm 子模板

附加关注点：

1. 模型路由与降级策略。
2. 提示词治理边界（模板、参数、上下文拼装责任）。
3. 预算治理（token、耗时、失败重试）。
4. 输出结构化与安全约束。

附加输出：

1. 请求路径决策表（条件 -> 模型/策略 -> 理由）。
2. 预算与退化策略表（阈值 -> 动作 -> 验证点）。

### 6.3 memory 子模板

附加关注点：

1. 会话态/长期态数据边界。
2. 持久化一致性与生命周期管理。
3. 读写并发、过期回收与恢复策略。
4. 与 knowledge、runtime 的协作边界。

附加输出：

1. 存储对象生命周期表（创建/更新/失效/回收）。
2. 一致性与恢复策略表（故障场景 -> 恢复动作 -> 数据保证）。

---

## 7. 质量门（评审时必须回答）

1. 设计是否严格遵守相关 ADR 与架构边界。
2. 是否与 contracts 冻结策略一致，且未引入语义污染。
3. 是否具备可执行落地路径，而不依赖口头补充。
4. 测试与验收是否二值可判定。
5. 是否识别兼容性风险与回退路径。
6. 是否存在未决阻塞项；若有，是否给出解阻条件。
7. Design -> Build 映射与三件套是否完整；若未完整，是否说明原因。

---

## 8. 快速替换示例（runtime）

- <MODULE_NAME> = runtime
- <ARCH_DOC_PATHS> = docs/architecture/DASSALL_Agent_architecture.md
- <BLUEPRINT_DOC_PATHS> = docs/architecture/DASALL_Engineering_Blueprint.md
- <ADR_PATHS> = docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md; docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md; docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
- <CONTRACTS_PLAN_AND_TODOS> = docs/plans/DASALL_contracts冻结实施计划.md; docs/todos/contracts/DASALL_contracts冻结TODO总表.md
- <DEV_RULES_PATH> = docs/development/DASALL_工程协作与编码规范.md
- <MODULE_CODE_PATHS> = runtime/include; runtime/src; tests/unit/runtime; tests/integration
- <OUTPUT_DOC_PATH> = docs/architecture/runtime_模块详细设计方案.md

---

## 9. V1 -> V2 迁移说明

1. 继续沿用 V1 的用户可读结构，但在 V2 中补齐字段级输出。
2. 若仅做基础设计，使用通用主模板。
3. 若涉及 runtime、llm、memory 的复杂语义，再叠加子模板。
4. 若要直接进入 Build 拆解，建议补齐 Design -> Build 映射与三件套建议字段。

---

## 10. 可直接复制的模块设计方案文档骨架

> 说明：将以下骨架作为 <OUTPUT_DOC_PATH> 的正文模板。

```markdown
# <MODULE_NAME> 模块详细设计方案

文档版本：v1.0
日期：<DATE>
状态：Draft/Reviewed/Approved

## 1. 模块概览
- 模块职责：
- 上下游边界：
- 设计目标：

## 2. 约束清单
| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|

## 3. 现状与缺口
| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|

## 4. 候选方案对比
| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|

## 5. 决策结论
- 最终选型：
- 选型依据：
- 放弃方案理由：

## 6. 详细设计
### 6.1 子组件与职责
### 6.2 输入/输出与依赖
### 6.3 核心对象与接口语义
### 6.4 主流程时序
### 6.5 异常与恢复时序
### 6.6 配置策略
### 6.7 可观测性设计

## 7. Design -> Build 映射（建议级）
| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|

## 8. 实施计划与里程碑
| 阶段 | 目标 | 关键动作 | 完成判定 | 风险 |
|---|---|---|---|---|

## 9. 测试与质量门
| 测试层级 | 覆盖范围 | 核心用例 | 验收方式 |
|---|---|---|---|

## 10. 兼容性与演进评估（建议级）
| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|

## 11. 风险、阻塞与回退（建议级）
| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|

## 12. 未决问题与后续任务
- 未决问题：
- 后续任务建议：
```
