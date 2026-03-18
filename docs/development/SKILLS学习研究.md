# SKILLS 学习研究

## 1. 文档目标

本文档用于系统学习两类不同但容易混淆的 Skills 体系：

1. VS Code Copilot 自定义 Skill。
2. DASALL 仓库内部的 Skill 资产与未来运行时体系。

本文档不把它们割裂处理，而是先建立统一概念模型，再分别进入编辑器侧与系统架构侧，最后汇合到“如何设计并落地一个 Skill”。目标不是停留在看懂名词，而是达到以下三层能力：

1. 能解释 Skill 与 Tool、Prompt、Agent、Workflow、Memory 的边界。
2. 能看懂 DASALL 当前关于 Skill 的目录、契约与实现缺口。
3. 能独立设计一个最小可用 Skill，并给出后续运行时落地路径。

---

## 2. 为什么要先研究 Skills

Skill 不是一个“多加一层配置”的概念，而是把一类任务的工程经验收敛成可复用资产的方式。如果没有 Skill，Agent 每次都需要从零决定：

1. 这类任务该用哪些工具。
2. 工具调用顺序应该怎样组织。
3. 提示词该如何按阶段切分。
4. 输入和输出应该遵循什么契约。
5. 关键步骤失败后如何降级或安全终止。

当系统规模扩大后，这些内容如果全部散落在 Prompt、代码分支和人工经验里，会出现四类问题：

1. 同类任务执行风格不稳定。
2. 工具权限暴露过大。
3. 难以做版本治理与回归评测。
4. 无法把“任务经验”变成长期可演进资产。

Skill 解决的问题不是“让模型更聪明”，而是“让系统在处理某类任务时更稳定、更可控、更可复用”。

---

## 3. 统一概念模型

### 3.1 Skill 的统一定义

可以把 Skill 理解为：

> 围绕某一类任务封装好的可复用执行模板，它同时约束 Prompt、Tool、Workflow、输入输出契约和失败降级路径。

一个完整 Skill 通常至少包含以下要素：

1. 适用任务范围。
2. 输入契约。
3. 允许使用的工具集合。
4. 推荐工作流模板。
5. 配套提示词包。
6. 成功判定标准。
7. 失败降级策略。
8. 评测与发布信息。

### 3.2 Skill 和相近概念的边界

#### Skill 不是 Tool

Tool 是原子能力，例如读取文件、搜索文本、执行测试、调用外部 API。Skill 是工具的任务级组织方式。Skill 关注“这类任务通常怎么做”，Tool 关注“单个动作能做什么”。

#### Skill 不是 Prompt

Prompt 只负责模型行为约束和输入组织。Skill 除了 Prompt 之外，还要定义工具边界、工作流、降级路径和输出契约。把 Skill 简化成 Prompt，会导致权限、治理和评测能力丢失。

#### Skill 不是 Agent

Agent 是运行时主体，是请求生命周期的拥有者。Skill 是 Agent 可以选择和实例化的资产。Agent 负责是否启用 Skill，Skill 不应该反向接管整个运行时。

#### Skill 不是 Workflow 的同义词

Workflow 是 Skill 的一部分。Workflow 解决“步骤如何推进”，Skill 还要解决“何时适用、能用什么工具、如何治理、如何回归验证”。

#### Skill 不是 Memory

Memory 保存过程状态、历史事实和可复用上下文。Skill 可以约定哪些记忆字段是输入输出，但不应直接拥有或篡改记忆层的职责。

### 3.3 一张关系图

```text
用户请求
→ Agent 感知意图
→ Planner / Reasoner 选择 Skill
→ Skill 约束 Prompt + Tools + Workflow
→ Executor 执行工具步骤
→ Memory 写回过程状态
→ Reflection 决定继续 / 重试 / 重规划 / 切换 Skill
→ Agent 汇总最终结果
```

这个图里最关键的结论是：

1. Agent 是运行主体。
2. Skill 是任务级资产。
3. Tool 是原子执行单元。
4. Prompt 是模型行为描述资产。
5. Workflow 是步骤推进结构。

---

## 4. 两套 Skills 体系的核心区别

### 4.1 VS Code Copilot 自定义 Skill

这套体系服务于“编辑器中的代理如何按需加载一个工作流”。它是工具型、交互型、轻量打包的 Skill 机制，核心是：

1. 用一个 SKILL.md 描述用途、触发语义和执行步骤。
2. 按需引用脚本、参考文档和模板资源。
3. 依赖描述字段进行自动发现与加载。
4. 强调渐进加载，避免一次把所有内容塞进上下文窗口。

它更像“面向人机协作的工作流包”。

### 4.2 DASALL 内部 Skill

这套体系服务于“Agent 系统如何把一类任务治理成产品级资产”。它是架构型、运行时型、可治理的 Skill 机制，核心是：

1. SkillSpec 定义静态能力边界。
2. SkillRegistry 负责注册与匹配。
3. SkillRuntime 负责实例化与生命周期控制。
4. SkillInstance 绑定本次请求的具体工具范围、上下文和流程模板。
5. Skill 资产需要版本、评测、回归与降级策略。

它更像“面向系统运行时的任务级能力资产”。

### 4.3 两者共同点

两者虽然定位不同，但仍然共享一组核心思想：

1. 都在解决重复任务的复用问题。
2. 都需要清晰的触发描述。
3. 都需要把知识、流程和资源打包在一起。
4. 都强调不要把所有上下文一次性全部加载。
5. 都需要避免模糊、不可验证的技能定义。

### 4.4 两者不能直接类比的地方

不能把 Copilot Skill 直接当成 DASALL Skill 的一比一原型，原因有三点：

1. Copilot Skill 偏交互工作流，不天然承担运行时编排权。
2. DASALL Skill 要接入统一 Tool 治理、策略守卫、评测发布和运行时边界。
3. Copilot Skill 的核心问题是“被发现和加载”，DASALL Skill 的核心问题是“被匹配、实例化、执行和治理”。

---

## 5. Copilot 自定义 Skill 体系详解

### 5.1 目录结构

官方结构如下：

```text
.github/skills/<skill-name>/
├── SKILL.md
├── scripts/
├── references/
└── assets/
```

项目级和个人级都可以放置 Skill。常见位置包括：

1. .github/skills/<name>/
2. .agents/skills/<name>/
3. .claude/skills/<name>/
4. ~/.copilot/skills/<name>/
5. ~/.agents/skills/<name>/
6. ~/.claude/skills/<name>/

### 5.2 SKILL.md 的 Frontmatter

核心字段如下：

```yaml
---
name: skill-name
description: 'What and when to use.'
argument-hint: 'Optional hint shown for slash invocation'
user-invocable: true
disable-model-invocation: false
---
```

字段职责：

1. name
必须和目录名一致，通常使用小写加连字符。它是技能身份标识。

2. description
这是发现面的核心字段。模型会根据这里的关键词判断是否应该加载这个 Skill。如果描述不包含“使用场景”和“触发词”，Skill 往往无法被正确发现。

3. argument-hint
用于斜杠调用时给用户提示参数格式。

4. user-invocable
控制这个 Skill 是否以斜杠命令的形式暴露给用户。

5. disable-model-invocation
控制模型是否可以自动触发该 Skill。

### 5.3 自动发现与斜杠调用

Copilot Skill 有两套入口：

1. 用户主动通过斜杠命令调用。
2. 模型基于描述自动判断并加载。

这带来一个关键设计点：

> description 不是说明书，它本质上是发现接口。

如果 description 写得抽象，例如“一个有帮助的技能”，那么无论正文写得多详细，都很难被正确触发。

### 5.4 渐进加载原则

Copilot Skill 的加载分三层：

1. 发现阶段只读 name 和 description。
2. 判断相关后再加载 SKILL.md 正文。
3. 只有在正文明确引用资源时，才继续加载 scripts、references、assets。

因此一个好 Skill 不应该把所有规则都挤进一个超长 SKILL.md，而应该：

1. 把核心流程保留在正文。
2. 把长篇参考材料放到 references。
3. 把脚本放到 scripts。
4. 把模板和样板放到 assets。

### 5.5 Copilot Skill 的适用场景

适合使用 Copilot Skill 的场景：

1. 固定而可重复的工作流。
2. 需要绑定若干脚本、模板和说明文档。
3. 希望用户能够通过斜杠命令快速调用。
4. 希望模型在相关任务出现时自动发现。

不适合使用 Copilot Skill 的场景：

1. 全局、始终生效的团队约束。
2. 只是一个单次任务的提示模板。
3. 需要子代理上下文隔离或多阶段不同工具权限。

这些场景更适合用 instructions、prompt 或 custom agent。

### 5.6 常见失效点

1. name 和目录名不一致。
2. description 缺少明确触发词。
3. frontmatter YAML 语法错误。
4. 所有内容都堆进 SKILL.md，导致上下文负担过大。
5. 正文没有具体步骤，只有抽象描述。
6. 资源引用路径不是相对路径。

### 5.7 一个最小 Copilot Skill 心智模板

可以用下面的问题检查自己是否真的学会了：

1. 它解决什么重复问题。
2. 用户会在什么语境下触发它。
3. 模型会依据哪些关键词发现它。
4. 它需要哪些脚本、参考资料和模板。
5. 它的正文是否足够短，但又足够完成任务。

---

## 6. DASALL Skill 体系详解

### 6.1 当前仓库中的正式定义

根据学习资料与工程蓝图，DASALL 中的 Skill 可以概括为：

> 面向任务级复用的资产，封装某类任务常用的 Prompt、Tool、Workflow、输入输出契约和降级策略，由 Planner 或 Reasoner 选择实例化，但不绕过 ToolRegistry、PolicyGate 和 Executor。

这一定义比 Copilot Skill 更强调两件事：

1. 工具治理边界。
2. 运行时实例化边界。

### 6.2 SkillSpec 的核心字段

当前文档中给出的典型定义如下：

```python
@dataclass
class SkillSpec:
		skill_id: str
		name: str
		version: str
		description: str
		intent_patterns: list[str]
		input_contract: dict
		success_criteria: list[str]
		preconditions: list[str]
		allowed_tools: list[str]
		workflow_template: StepGraph | None
		prompt_bundle: dict
		fallback_strategy: dict
		eval_suite_ref: str | None
		owner: str | None
```

最重要的不是记住字段名字，而是理解它们各自解决什么问题：

1. intent_patterns
解决“这是什么任务”的识别问题。

2. input_contract
解决“调用前需要准备什么输入”的问题。

3. allowed_tools
解决“这类任务可以用什么工具，不能越权到哪里”的问题。

4. workflow_template
解决“推荐步骤如何推进”的问题。

5. prompt_bundle
解决“不同阶段应该给模型什么约束和上下文”的问题。

6. fallback_strategy
解决“关键步骤失败后如何处理”的问题。

7. eval_suite_ref
解决“这个 Skill 如何被持续回归验证”的问题。

### 6.3 工程目录落位

根据工程蓝图，DASALL 的技能资产目录如下：

```text
skills/
├── specs/
├── prompts/
├── workflows/
└── evals/
```

各自职责：

1. specs
存放 SkillSpec 的 YAML 或 JSON 定义。

2. prompts
存放 PromptBundle 和 PromptRelease 资产。

3. workflows
存放 WorkflowTemplate 或步骤图定义。

4. evals
存放输入样例、期望输出、验收标准等评测资产。

### 6.4 运行时对象

文档明确建议在运行时层实现三个对象：

1. SkillRegistry
负责注册与匹配。

2. SkillRuntime
负责实例化与生命周期控制。

3. SkillInstance
负责绑定本次请求中的工具范围、PromptBundle 和具体工作流。

这三个对象的关系可以理解为：

```text
静态 SkillSpec
→ SkillRegistry 负责找到它
→ SkillRuntime 负责把它实例化
→ SkillInstance 负责为当前请求落地
```

### 6.5 Skill 在主链路中的位置

Skill 真正接入 Agent 时，不应该是“直接执行脚本”，而应该挂在主链路中：

```text
用户请求
→ 感知层提取意图
→ Planner / Reasoner 匹配 Skill
→ SkillRuntime 生成 SkillInstance
→ Executor 在 allowed_tools 边界内执行 workflow
→ Memory 回写状态
→ Reflection 决定继续、重试、切换 Skill 或回退通用路径
→ AgentOrchestrator 汇总 AgentResult
```

这里最容易犯的错误有四类：

1. Skill 绕过 ToolRegistry 直接调工具。
2. Skill 内部自己做权限判断并绕过策略守卫。
3. Skill 把运行态状态写回静态配置。
4. Skill 私自维护 Prompt 版本，绕开统一治理。

### 6.6 Skill 与 Prompt 边界

仓库里的 Prompt 契约和 Guard 已经体现出一个重要原则：

> PromptComposeRequest 只是 Prompt 组合请求对象，不应变成第二个 Context 拥有者。

这条原则对 Skill 学习很关键，因为它意味着：

1. Skill 可以引用 PromptBundle。
2. Skill 可以影响 Prompt 选择。
3. Skill 不能把 Prompt 子系统、Context 子系统和 Agent 主控职责混成一团。

### 6.7 Skill 与 Agent 边界

从 Agent 契约角度看，最终结果提交权在 AgentOrchestrator，而不是 Skill。Skill 只是被选择和实例化的任务级资产，不拥有最终生命周期闭环。

这个边界意味着：

1. Skill 不应直接产出最终 AgentResult 并跳过总控。
2. Skill 不应自定义一套独立的状态机替代 Agent 主状态机。
3. Skill 的失败与降级，应被 Reflection 和 Runtime 正式纳入主链路处理。

### 6.8 Skill 与 automation/skills 的关系

仓库里还存在另一套目录：automation/skills。它不是 DASALL 主系统技能目录的简单别名，而是自动化系统内部的独立技能资产区。其职责是：

1. 为 automation/controller、tasks、blocker 提供规则与 Prompt 资产。
2. 不依赖 DASALL 根目录 skills 作为自己的内部技能源。
3. 作为开发自动化系统的独立资产层存在。

因此学习时必须区分：

1. 根目录 skills 关注 DASALL Agent 本体能力资产。
2. automation/skills 关注项目开发自动化系统自身的能力资产。

---

## 7. 当前仓库的现状判断

### 7.1 已有内容

当前仓库已经具备以下能力基础：

1. 有清晰的 Skill 概念定义。
2. 有正式的工程目录落位方案。
3. 有 Prompt 和 Agent 的边界契约。
4. 有关于 SkillRegistry、SkillRuntime、SkillInstance 的文档设计。
5. 有 automation/skills 的独立架构说明。

### 7.2 尚未完成的部分

当前更明显的是“设计已成形，实现仍缺位”。缺口至少包括：

1. skills/specs 下缺少真实 SkillSpec 示例资产。
2. skills/workflows 下缺少具体 WorkflowTemplate 示例。
3. skills/prompts 下缺少成体系 PromptBundle 示例。
4. skills/evals 下缺少实际评测样例。
5. tools 或相关运行时模块中尚未看到完整的 SkillRegistry 和 SkillRuntime 落地代码。
6. 资产加载器、匹配器、执行器与测试接线仍需补齐。

### 7.3 学习上的正确预期

学习这个仓库时，不能带着“我要马上顺着代码一路调到完整运行时”的预期。更合理的顺序是：

1. 先读概念和边界。
2. 再读目录与契约。
3. 再识别尚未落地的对象。
4. 最后补一套最小实现方案。

这不是妥协，而是当前仓库状态决定的合理路径。

---

## 8. 全量学习路线

下面给出按“从概念到实现”组织的完整学习路线。

### Phase 1: 统一术语与边界

学习目标：

1. 说清楚 Skill、Tool、Prompt、Agent、Workflow、Memory 的区别。
2. 能解释为什么 Skill 不能绕过工具治理。
3. 能解释为什么 Skill 不拥有 Agent 主生命周期。

必读材料：

1. docs/LLM Agent学习.md 中 7.10 附近关于 Skill 的章节。
2. contracts/include/prompt 下与 PromptComposeRequest、PromptBoundaryContracts 相关头文件。
3. contracts/include/agent 下与 AgentResult、GoalContract 相关头文件。

阶段产出：

1. 一页术语对照表。
2. 一张主链路关系图。
3. 一份常见误区清单。

完成标准：

1. 能用自己的话解释六个概念。
2. 能指出两个越界示例并说明为什么错。

### Phase 2: 学 Copilot 自定义 Skill

学习目标：

1. 掌握 Skill 目录结构和 SKILL.md 规范。
2. 理解自动发现、斜杠调用和渐进加载。
3. 能设计一个最小可用 Copilot Skill。

必读材料：

1. VS Code / Copilot 的 Agent Skills 规范。
2. agent customization 相关说明，特别是 Skill、Prompt、Instruction、Agent 的决策边界。

阶段产出：

1. 一个最小 SKILL.md 草案。
2. 一个正确的 description 模板。
3. 一份常见失效排查表。

完成标准：

1. 能判断什么场景应该用 Skill 而不是 Prompt。
2. 能写出一个被正确发现的 description。
3. 能解释 user-invocable 与 disable-model-invocation 的差异。

### Phase 3: 学 DASALL 技能资产层

学习目标：

1. 理解 skills 目录四类资产的职责。
2. 理解 SkillSpec 的关键字段与工程含义。
3. 理解 PromptBundle、WorkflowTemplate、eval suite 为什么都必须和 Skill 联动。

必读材料：

1. docs/architecture/DASALL_Engineering_Blueprint.md 中 3.14 skills。
2. docs/LLM Agent学习.md 中 SkillSpec、SkillRegistry、SkillRuntime 段落。
3. docs/architecture/DASSALL_Agent_architecture.md 中 Skill 运行时对象说明。

阶段产出：

1. 一份 SkillSpec 字段解释表。
2. 一份技能资产目录职责表。
3. 一份“静态资产与运行时对象”映射图。

完成标准：

1. 能说出 specs、prompts、workflows、evals 各放什么。
2. 能解释 allowed_tools、fallback_strategy、eval_suite_ref 的必要性。

### Phase 4: 学 DASALL 运行时链路

学习目标：

1. 理解 SkillRegistry、SkillRuntime、SkillInstance 的职责划分。
2. 理解 Skill 如何接入感知、规划、执行、记忆和反思链路。
3. 理解 AgentOrchestrator 与 MultiAgentCoordinator 的总控边界。

必读材料：

1. 与 AgentOrchestrator、MultiAgentCoordinator 边界相关的 ADR。
2. Prompt 相关契约头文件。
3. Agent 相关契约头文件。

阶段产出：

1. 一份最小 Skill 运行时时序图。
2. 一份“应由谁负责什么”的职责表。
3. 一份越界反例清单。

完成标准：

1. 能画出 Skill 从匹配到执行到反思的链路。
2. 能说明为什么 Skill 不能直接替代 Agent 主控。

### Phase 5: 双体系映射

学习目标：

1. 把 Copilot Skill 和 DASALL Skill 的共性与差异彻底讲清楚。
2. 找出哪些经验可以迁移，哪些不能迁移。

建议方法：

1. 用发现机制做对照。
2. 用资源组织做对照。
3. 用权限和治理做对照。
4. 用运行时责任做对照。

阶段产出：

1. 一张对照表。
2. 一份迁移规则清单。

完成标准：

1. 看到一个 Skill 设计时，能判断它更接近 Copilot 风格还是 DASALL 风格。

### Phase 6: 最小实战

学习目标：

1. 设计一个最小任务级 Skill。
2. 给出 PromptBundle、WorkflowTemplate、allowed_tools、fallback_strategy 与 eval 的配套草案。
3. 给出最小运行时落地设计。

建议实战主题：

1. 代码审查 Skill。
2. Contracts 冻结文档生成 Skill。
3. Bug 定位与回归验证 Skill。

阶段产出：

1. 一个 SkillSpec 草案。
2. 一个 WorkflowTemplate 草案。
3. 一个 PromptBundle 草案。
4. 一个 eval 样例。
5. 一份运行时接线说明。

完成标准：

1. 能把一个具体任务完整拆到 Skill 资产层。
2. 能说明如果要落地，哪些模块需要新增实现。

### Phase 7: 复盘与验证

学习目标：

1. 检查自己是不是只记住了术语，而没有真正形成设计能力。
2. 检查自己是否能从“会解释”走向“会实现”。

阶段产出：

1. 一套自测题。
2. 一份能力缺口清单。
3. 下一步行动计划。

---

## 9. 推荐阅读顺序

建议按以下顺序阅读，避免一开始就陷入工程细节：

1. docs/LLM Agent学习.md
先建立 Skill 的统一概念和接口心智模型。

2. docs/architecture/DASALL_Engineering_Blueprint.md
再确认它在工程目录中的正式落位。

3. docs/architecture/DASSALL_Agent_architecture.md
补强整体架构视角。

4. contracts/include/prompt/*
理解 Prompt 组合边界，避免把 Skill 学成 Prompt 别名。

5. contracts/include/agent/*
理解 Agent 总控边界，避免把 Skill 学成 Agent 子系统。

6. docs/development/Developing Automation方案设计.md
理解为什么 automation/skills 是独立体系。

7. Copilot Skill 官方规范
最后再回看编辑器侧 Skill，会更容易做双体系对照。

---

## 10. 实战学习模板

下面给出一个适合练习的最小模板，用于训练从“任务描述”到“Skill 设计”的转化。

### 10.1 任务描述

以“代码审查 Skill”为例，需要解决：

1. 如何读取目标变更。
2. 如何定位高风险区域。
3. 如何组织 findings 输出。
4. 当测试不可运行时如何降级。

### 10.2 SkillSpec 草案

```yaml
skill_id: code-review
name: Code Review
version: 0.1.0
description: 对代码变更执行结构化风险审查，输出 findings、severity、evidence 和 residual_risks。
intent_patterns:
	- review code
	- inspect patch
	- analyze regression risk
input_contract:
	requires:
		- changed_files
		- target_scope
success_criteria:
	- findings are evidence-based
	- major regressions are called out
preconditions:
	- repository is readable
allowed_tools:
	- read_file
	- grep_search
	- get_errors
workflow_template: code-review-default
prompt_bundle: code-review-bundle
fallback_strategy:
	when_test_unavailable: static-analysis-only
eval_suite_ref: code-review-basic
owner: platform
```

### 10.3 设计检查项

设计完后必须回头问自己：

1. 这个 Skill 的输入是不是够明确。
2. 它的工具权限是不是最小化。
3. 它的工作流是不是可执行而不是空话。
4. 它失败时有没有合理降级。
5. 它能不能被评测和回归。

---

## 11. 从学习走向实现的最小落地方案

如果后续要把 DASALL 的 Skill 体系从文档推进到代码，实现上建议遵循“最小闭环优先”。

### 11.1 最小闭环应该包含什么

第一版不需要追求功能齐全，但必须闭环：

1. 能从 skills/specs 读取一个 SkillSpec。
2. 能根据简单意图匹配出一个 Skill。
3. 能实例化出 SkillInstance。
4. 能把 allowed_tools 传给执行层。
5. 能按固定 WorkflowTemplate 执行一个最小流程。
6. 能生成结构化结果。
7. 能跑一个最小测试用例。

### 11.2 推荐实现顺序

1. 先实现 SkillSpec 数据结构与解析器。
2. 再实现 SkillRegistry 的 register 和 match。
3. 再实现 SkillRuntime 的 instantiate。
4. 再给 Executor 增加基于 allowed_tools 的边界约束。
5. 再把 PromptBundle 接到 Prompt 组合链路。
6. 最后补 eval 与回归测试。

### 11.3 为什么不要一开始就做“大而全”

因为 Skills 的复杂度来自“跨层协同”，不是来自单个类本身。若一开始把动态工作流、复杂调度、多 Agent 路由、版本治理、回放评测一次性全部加入，系统很容易失控。更稳妥的做法是：

1. 先做单 Skill。
2. 再做多 Skill 匹配。
3. 再做降级切换。
4. 再做多 Agent 扩展。
5. 再做版本治理和评测平台化。

---

## 12. 学习中的高频误区

1. 误把 Skill 当 Prompt 模板。
结果是只有语言描述，没有工具、流程、契约和评测。

2. 误把 Skill 当 Agent。
结果是让 Skill 接管生命周期、状态机和最终结果输出。

3. 误把 Workflow 当 Skill 全部。
结果是只关注步骤，不关注适用条件、权限边界和回归治理。

4. 误把 Copilot Skill 直接当成系统运行时 Skill。
结果是只考虑发现与加载，不考虑治理与执行边界。

5. 误以为有了 Skill 目录就等于完成 Skill 体系。
结果是只有文件结构，没有注册、匹配、实例化、执行与测试闭环。

---

## 13. 自测题

学习完本文档后，至少应能回答以下问题：

1. Skill 与 Tool 的根本区别是什么。
2. 为什么 Skill 不能绕过 ToolRegistry 和 PolicyGate。
3. PromptBundle 在 Skill 里扮演什么角色。
4. 为什么 SkillInstance 需要和 SkillSpec 分离。
5. Copilot Skill 的 description 为什么是发现面而不是装饰字段。
6. user-invocable 和 disable-model-invocation 分别影响什么。
7. DASALL 的 skills 和 automation/skills 为什么不能混用成一个目录。
8. 一个没有 eval_suite_ref 的 Skill 在工程治理上会有什么问题。
9. 如果测试工具不可用，为什么 fallback_strategy 比直接失败更合理。
10. 如果后续要在 DASALL 里落地 SkillRuntime，第一版最小闭环应包括哪些对象。

如果这些问题不能稳定回答，说明还没有真正掌握 Skills。

---

## 14. 建议的后续产出

当本文档阅读完成后，建议继续产出三份配套材料：

1. 一份 Skill 术语表。
2. 一份 Copilot Skill 最小示例。
3. 一份 DASALL SkillSpec 与运行时最小落地草案。

这三份材料分别对应：

1. 概念是否清楚。
2. 编辑器侧是否会用。
3. 系统侧是否会设计。

---

## 15. 结论

Skills 的学习难点不在于文件格式，而在于边界意识。真正需要掌握的不是“Skill 有哪些字段”，而是下面这组能力：

1. 把一类任务抽象为可复用资产。
2. 知道这类资产该放在哪一层。
3. 知道它与 Prompt、Tool、Workflow、Agent 的接口如何切分。
4. 知道如何让它可发现、可实例化、可执行、可治理、可评测。

Copilot Skill 教你的是“如何把工作流打包给代理按需加载”。DASALL Skill 教你的是“如何把任务能力治理成系统级资产”。前者更偏交互与资源组织，后者更偏架构与运行时治理。把这两套体系同时学透，才能真正把 Skill 从“一个目录名”学成“一个可落地的工程能力模型”。
