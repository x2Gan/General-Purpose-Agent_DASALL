# DASALL 模块详细设计提示词模板

## 1. 使用目标

本模板用于在 DASALL 中按模块开展详细设计。

它适用于以下典型场景：

1. 基于架构文档和 ADR 约束，完成某个模块的详细设计文档。
2. 在设计前先调研主流技术方案，并给出可解释的选型结论。
3. 将设计结果直接映射到 contracts、接口、测试与实施计划。

---

## 2. 使用方法

1. 复制第 3 节完整模板。
2. 替换所有占位符（例如 <MODULE_NAME>）。
3. 指定输出路径（建议保存在 docs/architecture 或 docs/plans 下）。
4. 让 LLM 按模板步骤产出完整设计稿。

建议：一次只做一个模块，避免跨模块混合导致边界漂移。

---

## 3. 可直接复制的提示词模板

### Role

你是一名资深 C++ Agent 系统架构与工程落地专家，负责在 DASALL 项目中完成模块级详细设计。

你的设计必须同时满足：

1. 架构一致性
2. ADR 边界一致性
3. 工程可实现性
4. 测试可验证性
5. 版本可演进性

### Context

- 项目根目录：<PROJECT_ROOT>
- 当前日期：<DATE>
- 当前模块：<MODULE_NAME>
- 设计阶段：Detailed Design
- 输出语言：中文

### Inputs

请严格基于以下输入，不得忽略约束：

1. 架构文档：<ARCH_DOC_PATHS>
2. 工程蓝图：<BLUEPRINT_DOC_PATHS>
3. 相关 ADR：<ADR_PATHS>
4. contracts 计划与 TODO：<CONTRACTS_PLAN_AND_TODOS>
5. 工程规范：<DEV_RULES_PATH>
6. 当前模块代码骨架：<MODULE_CODE_PATHS>

### Task

请为 <MODULE_NAME> 产出一份完整的模块详细设计方案。

必须覆盖以下目标：

1. 明确模块边界、职责、输入输出。
2. 给出子组件划分和组件协作关系。
3. 明确模块对 contracts 的消费和影响。
4. 明确与相邻模块的接口边界和依赖方向。
5. 给出可执行的实施步骤和测试策略。

### Hard Constraints

1. 不允许改写已冻结 ADR 的结论。
2. 不允许把实现细节反向写入 contracts 共享语义对象。
3. 不允许跳过错误语义、预算语义、可观测性与恢复语义。
4. 不允许输出仅概念化方案，必须落到可实现结构。
5. 若发现前置依赖未满足，必须显式标注阻塞项与替代路径。

### Required Workflow

请严格按以下步骤执行。

#### Step 1: 约束抽取

1. 从架构文档抽取模块职责与上下游边界。
2. 从 ADR 抽取对该模块的硬约束。
3. 从 contracts 计划抽取当前阶段允许与禁止事项。

输出：约束清单（按 Must / Should / Must-Not 分类）。

#### Step 2: 现状分析

1. 识别该模块当前代码状态（骨架、占位、已实现能力）。
2. 识别与目标之间的缺口。
3. 识别高风险冲突点（边界冲突、语义重复、依赖反转）。

输出：现状-目标差距表。

#### Step 3: 行业方案调研与候选设计

请结合主流实践（例如 orchestrator-workers、RAG、tool governance、retry/compensation、event-driven）给出至少 2 个候选方案。

每个候选方案必须包含：

1. 设计思路
2. 组件结构
3. 优点
4. 风险
5. 与 DASALL 约束的匹配度

输出：候选方案对比矩阵。

#### Step 4: 方案决策

1. 明确最终选型方案。
2. 给出放弃其他候选的理由。
3. 明确该方案如何满足架构文档与 ADR。

输出：决策结论与依据。

#### Step 5: 模块详细设计

至少包含以下内容：

1. 模块职责边界
2. 子组件清单
3. 子组件输入/输出
4. 子组件依赖关系
5. 核心数据对象（与 contracts 对齐）
6. 核心接口定义（仅语义级，不必给全量代码）
7. 主流程时序
8. 异常与恢复流程时序
9. 配置项与默认策略
10. 可观测性（日志、指标、追踪、审计）

输出：模块详细设计正文。

#### Step 6: 工程落地设计

1. 文件与目录落盘建议（include/src/tests）。
2. 分阶段实现计划（按最小可交付切分）。
3. 每阶段验收标准。
4. 回归与兼容验证策略。

输出：实施计划与里程碑。

#### Step 7: 测试与质量门

至少覆盖：

1. 单元测试范围
2. 契约测试影响点
3. 集成测试路径
4. 失败注入测试点
5. 版本兼容性检查点

输出：测试矩阵与 Gate 清单。

### Output Format

请按以下结构输出最终结果：

1. 模块概览
2. 约束清单
3. 现状与缺口
4. 候选方案对比
5. 决策结论
6. 详细设计
7. 实施计划
8. 测试与质量门
9. 风险与缓解
10. 未决问题与后续任务

### Deliverables

请生成以下交付物：

1. 正式设计文档正文（Markdown）
2. 组件关系图的文字化描述
3. 接口与对象清单
4. 实施任务列表（可映射到 TODO）

### Quality Checklist

最终输出必须能明确回答：

1. 该模块是否遵守所有相关 ADR 约束。
2. 是否与 contracts 冻结策略一致。
3. 是否可直接指导编码落地。
4. 是否具备可测试、可观测、可演进能力。

---

## 4. 快速替换示例

示例模块：runtime

- <MODULE_NAME> = runtime
- <ARCH_DOC_PATHS> = docs/architecture/DASSALL_Agent_architecture.md
- <BLUEPRINT_DOC_PATHS> = docs/architecture/DASALL_Engineering_Blueprint.md
- <ADR_PATHS> = docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md; docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md; docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
- <CONTRACTS_PLAN_AND_TODOS> = docs/plans/DASALL_contracts冻结实施计划.md; docs/todos/contracts/DASALL_contracts冻结TODO总表.md
- <DEV_RULES_PATH> = docs/development/DASALL_工程协作与编码规范.md
- <MODULE_CODE_PATHS> = runtime/include; runtime/src; tests/unit/runtime; tests/integration

---

## 5. 建议执行节奏

1. 每次只推进一个模块。
2. 先完成 Design 文档评审，再进入代码落盘。
3. 设计结论要回写到对应 TODO，避免文档与执行脱节。

文档版本：v1.0
日期：2026-03-13
状态：Active
