# Developing Automation 方案设计

> 文档定位：本文档定义 DASALL 项目“开发过程自动化”的完整方案，目标是把“打开 TODO -> 选择原子任务 -> 执行实现 -> 验证 -> 处理 blocker -> 回到下一任务”的重复动作，收敛为一套稳定、可追踪、可扩展的项目级自动化控制方案。
>
> 文档范围：本文档只讨论开发过程自动化。该方案以 automation 为独立根目录，自带所需的 skills、tools、tasks、tests、state 等资产，不与 DASALL 现有目录共用实现资产；DASALL 在该方案中只作为被分析、被修改、被验证的目标项目。

---

## 1. 背景与问题定义

当前项目推进中存在稳定重复动作链：

1. 打开任务 TODO 文档。
2. 选择一个可执行的原子任务。
3. 复制提示词并启动 AI 执行任务。
4. 若任务未阻塞，则回到步骤 1。
5. 若任务阻塞，则切换到 blocker 修复流程。
6. blocker 修复完成后，再回到步骤 1。

这套流程具备以下特征：

1. 是多轮、可循环的开发控制流程，而不是单次问答。
2. 依赖明确的项目状态，包括 TODO 状态、依赖关系、阻塞项和验收结果。
3. 需要在文档、提示词、代码、测试和状态回写之间形成闭环。
4. 需要在“继续推进”和“优先解阻”两种模式之间切换。

若没有显式的控制面，Copilot 每轮都需要重新猜测：

1. 当前应该看哪份任务文档。
2. 下一个任务是什么。
3. 什么算 blocker。
4. blocker 修复完以后如何恢复主循环。

因此，本问题的本质不是“如何多写几个提示词”，而是“如何为 Copilot 提供一套项目级、有状态、可回放的开发自动化协议”。

---

## 2. 方案目标

### 2.1 总体目标

在仓库内建立一套 Developing Automation 控制面，使 Copilot 能够基于明确状态执行如下闭环：

1. 读取任务池。
2. 选择下一个可执行原子任务。
3. 按约束实施任务。
4. 运行验收命令并判断结果。
5. 回写任务状态。
6. 识别 blocker 并切换解阻。
7. 解阻完成后恢复主循环。

### 2.2 直接收益

1. 降低人工重复粘贴提示词的成本。
2. 将“任务推进逻辑”从人脑记忆变为仓库内显式协议。
3. 提升 AI 执行的稳定性、可复查性和一致性。
4. 为后续外部调度器、MCP server 或 VS Code 扩展提供稳定接入面。

### 2.3 非目标

本方案当前不试图解决以下问题：

1. 不实现无人值守的后台常驻自治循环。
2. 不替代现有构建系统、测试系统或 CI 流程。
3. 不在第一阶段重构已有模块分层。
4. 不在第一阶段引入新的运行时服务进程。

---

## 3. 设计结论

### 3.1 根目录落位

推荐在仓库根目录新增目录：`automation/`

定义如下：

`automation/` 是项目开发循环自动化的独立根目录。该目录内部自包含任务状态源、控制入口、prompt/spec/workflow 资产、工具执行定义、blocker 管理、状态快照、审计与测试资产，不依赖 DASALL 现有 `skills/`、`tools/`、`tests/`、`scripts/`、`docs/todos/` 等目录作为其内部运行资产。

### 3.2 为什么必须完全独立

本方案明确要求 automation 与 DASALL 现有目录不共用实现资产，原因如下：

1. 避免开发自动化协议和业务工程目录发生职责耦合。
2. 避免后续演进时被 DASALL 当前目录结构反向约束。
3. 避免任务状态、提示词资产、工具规范分散在多个顶层目录，增加维护成本。
4. 允许后续将 `automation/` 迁移到其他项目时保持最小改造。
5. 允许以 `automation/` 为边界接入 runner、MCP server、VS Code 扩展或外部调度器。

因此，本方案不再采用“automation 编排 + 复用现有 skills/tools/docs”的模式，而改为“automation 自带完整开发自动化资产，DASALL 仅作为目标代码仓”的模式。

### 3.3 为什么命名为 automation

推荐 `automation/` 的原因：

1. 语义直接，容易让读者理解为“开发过程自动化”。
2. 不与 runtime 中的 orchestrator 概念冲突。
3. 允许在 `automation/` 内部自然组织自己的 skills、tools、tests 与状态资产。
4. 允许未来平滑演进到 runner、adapter、controller、state 等子域。

不推荐下列顶层命名：

1. `orchestration/`：容易与运行时编排概念冲突。
2. `workflows/`：语义过泛，不能体现这是完整自动化根目录。
3. `devops/`：容易误导为部署或 CI/CD 基础设施。
4. `control/`：过宽泛，不体现开发流程自动化语义。
5. `project-skills/`：会误导为只是提示词集合，无法体现独立控制系统属性。

---

## 4. 设计原则

本方案遵循以下设计原则：

1. 状态优先：先定义状态源、状态字段和状态转移，再定义提示词与执行流程。
2. 自治封装：automation 内部自带所需资产，不依赖外部同类目录。
3. 文档即协议：TODO、blocker、规则、验收命令都必须显式化、可追踪。
4. 最小可用：先实现“单次触发，自动推进一轮直到完成或 blocked”，再考虑常驻化。
5. 可恢复：任何任务推进结果都应能回写，使下一轮可从上次状态继续。
6. 可审计：任务选择、blocker 判定、验收结果都应能复查。
7. 目标与控制分离：automation 是控制系统，DASALL 代码树是目标系统，两者通过明确定义的输入输出交互。

---

## 5. 与 DASALL 项目的边界

### 5.1 边界模型

本方案将仓库分为两个逻辑区域：

1. `automation/`：开发自动化系统本体。
2. DASALL 其余目录：被 automation 分析、修改、验证的目标项目。

### 5.2 automation 的独立职责

`automation/` 内部独立承接以下职责：

1. 任务池与 blocker 主状态源。
2. prompt、spec、workflow 等技能资产。
3. 自动化专用工具定义与执行策略。
4. 状态机、快照与审计记录。
5. 自动化方案自身的测试与验证资产。

### 5.3 DASALL 的角色

DASALL 现有目录在本方案中的角色只有三类：

1. 输入对象：提供源码、文档、配置、构建系统和测试资产。
2. 修改对象：被自动化流程修改代码或文档。
3. 验证对象：被自动化流程执行构建、测试、校验命令。

### 5.4 边界声明

1. automation 不复用 DASALL 现有 `skills/` 作为自己的技能资产目录。
2. automation 不复用 DASALL 现有 `tools/` 作为自己的工具定义目录。
3. automation 不复用 DASALL 现有 `docs/todos/` 作为自己的任务主状态源。
4. automation 可以读取、修改和验证 DASALL 代码树，但这些目录只是目标对象而非 automation 的内部实现目录。

---

## 6. 总体架构

### 6.1 控制闭环

Developing Automation 的目标闭环如下：

```text
读取 TODO -> 选择可执行任务 -> 执行实现 -> 运行验收 -> 回写状态
			^                                             |
			|                                             v
			+------ blocker 修复 <- blocker 识别 <- 失败分析
```

### 6.2 逻辑分层

```text
┌──────────────────────────────────────────────┐
│ User / Copilot Trigger                       │
│ 继续推进项目 / 优先修复 blocker                │
└──────────────────────────────────────────────┘
											│
											v
┌──────────────────────────────────────────────┐
│ automation/controller                        │
│ 主状态机 / 模式切换 / 入口控制                 │
└──────────────────────────────────────────────┘
					 │
					 v
┌──────────────────────────────────────────────┐
│ automation/tasks + automation/blocker        │
│ 任务池 / blocker 池 / 选择 / triage / recovery │
└──────────────────────────────────────────────┘
					 │
					 v
┌──────────────────────────────────────────────┐
│ automation/skills + automation/tools         │
│ 规则资产 / 提示词资产 / 执行定义 / 校验策略      │
└──────────────────────────────────────────────┘
					 │
					 v
┌──────────────────────────────────────────────┐
│ automation/integration                       │
│ 目标仓库适配 / 文件扫描 / 构建测试接入 / 审计     │
└──────────────────────────────────────────────┘
					 │
					 v
┌──────────────────────────────────────────────┐
│ DASALL Target Repository                     │
│ source / docs / build / tests / configs      │
└──────────────────────────────────────────────┘
```

### 6.3 控制面与资产层分离

本方案核心思想是：

1. `automation/` 内部同时持有控制逻辑与自动化资产。
2. DASALL 现有目录不再承担 automation 的内部资产职责。
3. automation 通过 integration 层面向目标仓库执行读取、修改与验证。
4. automation 的迁移和复用应以目录自治为前提。

这种分离有利于：

1. 减少自动化系统与目标项目之间的耦合。
2. 允许将 automation 独立演进为可复用框架。
3. 让后续外部 orchestrator 可直接接入 `automation/` 而不依赖 DASALL 目录细节。

---

## 7. automation 根目录设计

### 7.1 推荐目录树

```text
automation/
├── README.md
├── controller/
│   ├── loop-state-machine.md
│   ├── continue-project.prompt.md
│   ├── fix-blocker.prompt.md
│   └── mode-switch-rules.md
├── skills/
│   ├── specs/
│   ├── prompts/
│   └── workflows/
├── tools/
│   ├── registry/
│   ├── executors/
│   └── validators/
├── tasks/
│   ├── backlog/
│   ├── active/
│   ├── archive/
│   └── task-template.md
├── blocker/
│   ├── queue/
│   ├── blocker-definition.md
│   ├── blocker-triage-rules.md
│   ├── blocker-recovery-rules.md
│   └── blocker-template.md
├── state/
│   ├── loop-status.template.yaml
│   ├── active-context.template.yaml
│   └── execution-audit.template.md
├── tests/
│   ├── unit/
│   ├── integration/
│   └── scenarios/
└── integration/
    ├── target-repository-contract.md
    ├── file-discovery-policy.md
    ├── build-test-adapter.md
    └── audit-output-policy.md
```

### 7.2 目录职责说明

#### `automation/controller/`

职责：

1. 定义主循环状态机。
2. 定义“继续推进项目”和“优先修复 blocker”两个统一入口。
3. 定义模式切换和退出条件。

不负责：

1. 保存任务主数据。
2. 实现底层工具。
3. 直接替代技能资产。

#### `automation/skills/`

职责：

1. 独立存放 prompt、spec、workflow 资产。
2. 为 controller、tasks、blocker 提供可复用规则定义。
3. 不依赖 DASALL 根目录下其它技能资产。

#### `automation/tools/`

职责：

1. 定义自动化系统使用的工具注册与执行策略。
2. 定义验证器、命令适配器和执行约束。
3. 对目标仓库的操作能力在此层声明，而不是借用 DASALL 现有 `tools/` 目录。

#### `automation/tasks/`

职责：

1. 作为任务主状态源。
2. 存放 backlog、当前激活任务与归档任务。
3. 定义任务模板、状态字段和选择规则接入点。

#### `automation/blocker/`

职责：

1. 定义什么情况算 blocker。
2. 定义 blocker 的分类、优先级和解阻准入条件。
3. 定义 blocker 修复完成后如何恢复主循环。

#### `automation/state/`

职责：

1. 定义循环状态快照格式。
2. 定义一次推进或一次解阻的审计记录格式。
3. 支撑后续 runner 或外部 orchestrator 接入。

#### `automation/tests/`

职责：

1. 验证自动化系统自身的规则、状态机和场景。
2. 不与 DASALL 业务测试目录混合。
3. 为后续回归提供专属验证资产。

#### `automation/integration/`

职责：

1. 定义 automation 与目标仓库的接入协议。
2. 约束文件发现、构建测试接入和输出审计格式。
3. 让 automation 只通过显式适配层与 DASALL 交互。

---

## 8. 状态源设计

### 8.1 主状态源原则

主状态源使用 `automation/tasks/`，原因如下：

1. 任务主数据必须与自动化控制逻辑同边界维护。
2. 独立状态源可以避免与目标项目文档目录发生耦合。
3. automation 若要迁移到其他仓库，必须带走完整任务协议，而不是依赖外部 TODO 结构。
4. 允许 automation 为任务池引入自己的模板、状态字段、审计规则和归档策略。

### 8.2 任务最小字段规范

每个原子任务至少应包含：

1. 任务 ID。
2. 任务标题。
3. 当前状态。
4. 前置依赖。
5. 代码目标。
6. 测试目标。
7. 验收命令。
8. blocker 判定条件。
9. 输出物路径。

推荐模板：

```md
## T-012 runtime timeout policy

- 状态: todo
- 前置依赖: T-009, T-010
- 代码目标:
	- 在 runtime 层增加超时策略配置与默认值
	- 将策略接入入口请求处理
- 测试目标:
	- 增加默认超时单测
	- 增加超时触发后的集成测试
- 验收命令:
	- 构建目标命令 A
	- 测试命令 B
- blocker 判定:
	- 如缺少 contracts 接口，则转 blocker
- 输出物:
	- target/docs/...
	- target/code/...
```

### 8.3 状态枚举建议

任务状态建议限定为：

1. `todo`：尚未执行。
2. `in_progress`：当前正在执行。
3. `blocked`：被 blocker 挂起。
4. `done`：已完成并通过验收。
5. `cancelled`：显式取消。

不建议引入过多中间态，避免状态机膨胀。

---

## 9. blocker 设计

### 9.1 blocker 定义

满足以下任一条件，可判定为 blocker：

1. 当前任务依赖的前置任务未完成。
2. 当前任务依赖的接口、契约或文档尚未冻结。
3. 当前失败无法在本任务边界内修复，继续推进会引入越界修改。
4. 需要用户明确架构决策才能继续。
5. 依赖外部资源、外部权限或外部环境，不具备当前解锁条件。

### 9.2 blocker 与普通失败的区别

普通失败：

1. 构建报错但修复范围在当前任务内。
2. 单测失败但可通过本任务预期变更修复。
3. 文档格式或实现细节不一致，但不需要跨任务扩边。

blocker：

1. 继续修会跨越冻结边界。
2. 继续修会引入新的设计决策。
3. 继续修需要修改其他未进入本轮范围的工作包。

### 9.3 blocker 最小字段规范

每个 blocker 至少包含：

1. blocker ID。
2. 来源任务 ID。
3. 问题描述。
4. 影响范围。
5. 根因猜测。
6. 解阻条件。
7. 是否需要用户决策。
8. 修复后回到哪个任务。

推荐模板：

```md
## B-003 missing timeout policy contract

- 来源任务: T-012
- 问题描述:
	- runtime 需要 TimeoutPolicy，但 contracts 尚未冻结
- 影响范围:
	- runtime
	- contracts
- 根因猜测:
	- 上游契约工作包未完成
- 解阻条件:
	- 冻结 TimeoutPolicy 契约
	- 明确默认值与错误语义
- 是否需要用户决策:
	- 是
- 恢复目标任务:
	- T-012
```

---

## 10. 主循环状态机设计

### 10.1 状态定义

主循环建议定义以下状态：

1. `idle`：未开始本轮推进。
2. `loading_context`：装载任务、约束和相关文档。
3. `selecting_task`：从 `automation/tasks/` 中选择下一个任务。
4. `executing_task`：执行任务实现与修改。
5. `validating_task`：运行验收命令并评估结果。
6. `writing_back`：回写任务状态、文档结论和审计记录。
7. `context_compacting`：执行轮次上下文压缩并生成下一轮启动摘要。
8. `triaging_blocker`：分析失败是否升级为 blocker。
9. `recovering_blocker`：执行 blocker 修复。
10. `completed_round`：当前轮次完成。
11. `waiting_user`：需要用户决策或外部输入。

### 10.2 状态转移规则

```text
idle
	-> loading_context
loading_context
	-> selecting_task
selecting_task
	-> executing_task
	-> waiting_user            (无可执行任务)
executing_task
	-> validating_task
	-> triaging_blocker        (执行失败)
validating_task
	-> writing_back            (验证通过)
	-> triaging_blocker        (验证失败)
writing_back
	-> context_compacting      (普通任务完成)
	-> completed_round         (仅登记 blocker 且等待人工处理)
context_compacting
	-> completed_round
completed_round
	-> selecting_task          (继续推进)
	-> idle                    (本轮结束)
triaging_blocker
	-> recovering_blocker      (可进入修复)
	-> writing_back            (仅登记 blocker)
	-> waiting_user            (需用户决策)
recovering_blocker
	-> validating_task         (修复后验证)
	-> waiting_user            (仍需外部决策)
```

### 10.3 最核心的行为约束

1. 一轮只允许激活一个原子任务。
2. 同一轮中若进入 blocker 分支，必须先完成 triage，再决定是否修复。
3. 修复完成后必须显式恢复到原任务或其替代任务，不能默默跳转。
4. 普通任务完成后必须先回写，再执行一次上下文压缩。
5. 若后继动作是 blocker 自动修复链路，则压缩必须延后到 blocker 修复完成之后执行。
6. 任何状态退出前都必须有可回写的记录。

---

## 11. 控制入口设计

### 11.1 推荐保留两个入口

建议只保留两个主入口提示：

1. `继续推进项目`
2. `优先修复 blocker`

### 11.2 入口一：继续推进项目

该入口内部执行：

1. 读取 `automation/tasks/` 主状态源。
2. 选择第一个满足条件的原子任务。
3. 加载相关 ADR、架构文档和规范。
4. 执行实现。
5. 运行验收命令。
6. 更新任务状态。
7. 若成功且后继动作不是 blocker 自动修复，则执行一次上下文压缩。
8. 若失败，进入 blocker triage。
9. 若本轮成功且存在后续任务，可结束本轮并等待下一次触发。

适用场景：

1. 正常推进开发节奏。
2. 从 `automation/tasks/backlog/` 中持续消化待办任务。

### 11.3 入口二：优先修复 blocker

该入口内部执行：

1. 读取 `automation/blocker/queue/` 主状态源。
2. 选择最高优先级或最早出现的 blocker。
3. 判断是否具备解阻条件。
4. 执行修复。
5. 跑验证。
6. 更新 blocker 状态与关联任务状态。
7. 若 blocker 清除，执行一次上下文压缩。
8. 若 blocker 清除，恢复主循环。

适用场景：

1. 积压 blocker 需要集中清理。
2. 当前推进效率主要受阻塞项制约。

---

## 12. 任务选择策略

### 12.1 选择规则

选择下一个任务时，必须同时满足：

1. 状态为 `todo`。
2. 所有前置依赖已完成。
3. 不被未解决 blocker 直接阻塞。
4. 任务边界足够原子，单轮可推进。
5. 验收命令明确且当前环境可执行。

### 12.2 选择优先级

若多个任务都可执行，建议按以下优先级排序：

1. 上游契约和边界冻结类任务。
2. 当前工作包中的阻塞解锁类任务。
3. 高依赖扇出任务。
4. 与当前上下文最接近、切换成本最低的任务。
5. 明确有验证命令且可快速闭环的任务。

### 12.3 禁止选择的任务

以下任务不得自动选择：

1. 需要人工架构拍板但未形成选项文档的任务。
2. 前置依赖缺失的任务。
3. 目标范围过大、无法单轮收敛的任务。
4. 缺少最小验收标准的任务。

---

## 13. Design -> Build 映射协议

### 13.1 为什么必须显式映射

自动化推进不能只看“做什么”，还必须明确“设计产物如何约束实现产物”。

若缺少 Design -> Build 映射，会导致：

1. 设计文档与代码实现脱钩。
2. 同一任务在不同轮次被重复解释。
3. 验收命令无法准确覆盖设计目标。
4. blocker 难以判断到底是设计缺失还是实现失败。

### 13.2 每个原子任务的双轨结构

建议每个原子任务都显式包含两条轨道：

1. Design 轨：说明边界、对象语义、非目标、兼容性和输出文档。
2. Build 轨：说明代码改动、测试改动、验收命令和回写要求。

推荐结构：

```md
## T-0XX <task title>

- 状态: todo
- 前置依赖: ...
- Design 目标:
	- 明确边界
	- 明确对象/接口语义
	- 明确非目标与兼容性
- Design 输出物:
	- target/docs/...
- Build 目标:
	- 修改哪些模块
	- 增加哪些测试
- Build 输出物:
	- target/src/...
	- target/tests/...
- 验收命令:
	- 构建命令
	- 测试命令
- blocker 判定:
	- 何时停止扩张并转 blocker
- 解阻条件:
	- 满足什么条件后恢复
```

### 13.3 控制器执行约束

controller 在推进任务时，必须按以下顺序检查映射是否完整：

1. 是否存在可消费的 Design 结论。
2. Build 目标是否能直接映射到具体代码变更。
3. 验收命令是否覆盖 Build 目标。
4. 失败时是否能映射回 Design 缺口或 Build 缺陷。

### 13.4 blocker 与映射关系

当出现以下情况时，应优先判定为 Design blocker，而不是直接进入 Build 修补：

1. 接口语义未冻结。
2. 对象边界存在歧义。
3. 兼容性策略未明确。
4. 无法判断测试期望结果。

当出现以下情况时，可继续在 Build 轨内修复：

1. 编译错误位于当前任务的已知改动范围内。
2. 单测失败与当前实现细节直接相关。
3. 验收命令失败但不需要新设计决策。

---

## 14. 执行与验收策略

### 14.1 执行策略

每个原子任务推进时，必须遵循：

1. 先读任务相关文档与边界。
2. 确认本轮只做当前任务要求的最小变更。
3. 先修根因，不做表面补丁式扩散。
4. 完成后立即做最小必要验证。

### 14.2 验收命令策略

验收命令应尽量由任务文档显式给出，并遵循：

1. 先局部验证，再全局验证。
2. 先成本低的校验，再成本高的校验。
3. 命令必须可在仓库环境中重复执行。

在本仓库中，典型验收路径通常包括：

1. 目标构建。
2. 单元测试或模块测试。
3. 必要时执行集成测试。
4. 必要时更新文档并检查一致性。

### 14.3 结果判定

1. 验证通过：任务进入 `done`。
2. 验证失败但可在当前任务内修复：继续任务执行分支。
3. 验证失败且超出边界：转 blocker triage。

---

## 15. 文档回写与审计

### 15.1 必须回写的内容

每轮结束后，至少回写以下信息：

1. 任务状态变化。
2. 本轮产出物路径。
3. 验收命令与结果摘要。
4. 若 blocked，则记录 blocker ID 和原因。
5. 若恢复成功，则记录恢复动作和影响范围。

### 15.2 审计记录建议

建议为每轮推进保留简短审计条目，至少包含：

1. 时间。
2. 入口模式。
3. 任务 ID 或 blocker ID。
4. 执行动作摘要。
5. 验证结果。
6. 下一状态。

这样可以支持：

1. 复盘某个任务为什么 blocked。
2. 判断控制提示是否稳定。
3. 为后续 runner 接入提供可追踪状态。

---

## 16. 上下文压缩设计

### 16.1 目标

上下文压缩的目标不是生成普通总结，而是在每轮任务闭环后生成一份“下一轮可直接消费的启动上下文”，降低以下风险：

1. 历史上下文过长导致的模型降智。
2. 已完成结论在下一轮被重复推理。
3. blocker 修复前后因果链断裂。
4. 下一轮无法快速恢复关键文件、命令和决策边界。

### 16.2 触发时机

上下文压缩是主循环的强制步骤，触发规则如下：

1. 普通任务完成并完成回写后，自动执行一次压缩。
2. 若当前任务失败后立即进入 blocker 自动修复链路，则本次压缩延后。
3. 延后的压缩必须在 blocker 修复完成、验证通过并完成回写之后执行。
4. 若 blocker 进入 `waiting_user`，只允许生成轻量阻塞摘要，不执行完整轮次压缩。

### 16.3 两种压缩类型

建议定义两类压缩：

1. `task_post_commit_compaction`
2. `blocker_post_recovery_compaction`

两者的区别在于：

1. 前者强调任务产出、验证结果和下一任务准备。
2. 后者强调 blocker 根因、修复动作、恢复目标与上下文连续性。

### 16.4 blocker 链路延后规则

当系统判断后继动作是同一问题链上的 blocker 自动修复时，必须遵循：

1. 不执行 `task_post_commit_compaction`。
2. 保留原任务上下文直至 blocker 链路闭合。
3. 在 blocker 修复完成后执行 `blocker_post_recovery_compaction`。
4. 压缩结果必须同时覆盖“原任务为何被阻塞”“blocker 如何被修复”“恢复后应从哪里继续”。

该规则的核心原则是：连续性优先于即时压缩。

### 16.5 压缩输入

压缩阶段至少应消费以下输入：

1. 当前任务或 blocker 的结构化状态。
2. 本轮改动文件清单。
3. 本轮执行过的关键命令与验证结果。
4. 本轮新增的关键设计决策或边界结论。
5. 仍未解决的问题和风险。
6. 下一轮候选任务或恢复目标任务。

### 16.6 压缩输出

建议压缩结果采用结构化格式，至少包含：

1. `round_id`
2. `source_type`
3. `source_id`
4. `outcome_summary`
5. `retained_context`
6. `changed_targets`
7. `verification_summary`
8. `open_risks`
9. `next_entry_hint`
10. `discardable_context`

其中 `source_type` 建议限定为：

1. `task_complete`
2. `blocker_recovered`
3. `blocker_waiting_user`

### 16.7 保留与丢弃规则

压缩后必须保留：

1. 当前活跃任务 ID 或 blocker ID。
2. 本轮达成与未达成的结论。
3. 与下一轮直接相关的文件路径。
4. 验收命令及关键失败摘要。
5. blocker 根因、修复动作和恢复条件。

压缩后可以丢弃：

1. 已经固化到审计记录中的冗长探索过程。
2. 与当前工作链无关的历史旁支讨论。
3. 重复的命令输出细节。
4. 对下一轮无直接影响的临时推理分支。

### 16.8 验收标准

上下文压缩能力应满足以下验收条件：

1. 普通任务完成后，系统能自动生成下一轮可直接消费的压缩摘要。
2. blocker 自动修复链路中，不会在修复前截断关键上下文。
3. blocker 修复完成后，压缩结果能同时说明阻塞原因、修复动作和恢复目标。
4. 下一轮启动时，系统优先消费压缩摘要而不是重扫全部历史。

### 16.9 语义压缩与运行时压缩的边界

当前方案中的上下文压缩，本质上是为下一轮会话准备的结构化压缩，而不是对当前 Copilot Context Window 的即时 token 压缩。

其含义如下：

1. 压缩阶段会把本轮任务结果、关键约束、验证结论、未解决风险和下一轮启动提示提炼为高密度摘要。
2. 该摘要的主要用途是作为下一轮优先消费的启动上下文，而不是替换当前会话中已经存在的全部历史消息。
3. 因此，即使压缩结果已经生成，当前 Chat 会话中的历史消息仍然存在，当前轮的 Context Window 不会立刻表现为 token 占用下降。

因此，当前方案实现的是语义压缩，而不是运行时压缩。

若要让运行时压缩真实生效，必须额外具备会话切换机制，包括：

1. 在当前轮结束后持久化压缩摘要。
2. 启动下一轮新会话或新 agent 轮次。
3. 在新会话中优先加载压缩摘要，而不是完整旧历史。
4. 通过会话重建实现实际的上下文窗口收缩。

结论是：当前阶段的压缩目标，是提升下一轮启动质量与连续性，而不是在当前会话中即时回收 token。

---

## 17. automation 内部能力分层

### 17.1 推荐分层

建议在 `automation/` 内部拆成四层：

#### 第一层：controller 控制层

负责：

1. 决定当前模式。
2. 推动状态机转移。
3. 选择当前任务或 blocker。
4. 协调工具与技能资产的执行顺序。

#### 第二层：skills 资产层

负责：

1. `task-selector` 规则。
2. `task-executor` 提示与规范。
3. `blocker-triage` 规则。
4. `blocker-fixer` 提示与规范。
5. controller 入口提示资产。

#### 第三层：tools 执行层

负责：

1. 文件扫描与目标仓库访问。
2. 命令执行约束与校验。
3. 构建、测试和审计适配。

#### 第四层：tasks/blocker/state 数据层

负责：

1. 任务池。
2. blocker 队列。
3. 快照与审计记录。

### 17.2 推荐内部资产拆分

建议未来在 `automation/skills/` 下逐步补齐以下资产：

1. 任务选择规则资产。
2. 任务执行规则资产。
3. blocker 识别规则资产。
4. blocker 修复规则资产。
5. controller 入口提示资产。

建议未来在 `automation/tools/` 下逐步补齐以下资产：

1. 目标仓库文件发现策略。
2. 构建命令执行适配器。
3. 测试命令执行适配器。
4. 回写与审计输出验证器。

---

## 18. 最小可用落地方案

### 18.1 MVP 范围

第一阶段只落地“单次触发、一轮闭环”的能力：

1. 触发“继续推进项目”。
2. 自动选择一个原子任务。
3. 自动执行并验收。
4. 自动回写状态。
5. 自动执行一次上下文压缩，或在 blocker 修复后执行延后压缩。
6. 如遇 blocker，自动登记并在能力范围内修复一次。
7. 若仍需外部决策，则停止并给出明确阻塞说明。

### 18.2 MVP 必备文件

建议第一批至少具备：

1. `automation/README.md`
2. `automation/controller/loop-state-machine.md`
3. `automation/controller/continue-project.prompt.md`
4. `automation/controller/fix-blocker.prompt.md`
5. `automation/skills/specs/task-selection-rules.md`
6. `automation/blocker/blocker-triage-rules.md`
7. `automation/tasks/task-template.md`
8. `automation/tasks/backlog/README.md`
9. `automation/blocker/blocker-template.md`
10. `automation/state/context-compaction.template.yaml`
11. `automation/skills/specs/context-compaction-rules.md`
12. `automation/integration/target-repository-contract.md`

### 18.3 MVP 成功标准

满足以下条件可视为 MVP 成功：

1. Copilot 能基于 `automation/tasks/` 明确选中一个任务。
2. Copilot 能按任务边界完成一轮实施与验证。
3. Copilot 能在失败时区分“可继续修复”与“应转 blocker”。
4. Copilot 能在普通任务完成后自动生成上下文压缩结果。
5. Copilot 能把结果回写到文档中。
6. 用户只需要发起入口指令，不需要手工重新组织上下文。

---

## 19. 后续演进路径

### 19.1 第二阶段：半自动多轮推进

在 MVP 稳定后，可以补充：

1. `automation/state/` 中的状态快照文件。
2. 标准化审计记录。
3. 按优先级自动推荐下一任务或下一 blocker。
4. 基于压缩摘要恢复下一轮启动上下文。

目标：

1. 减少每轮上下文整理成本。
2. 提高多轮连续推进的一致性。

### 19.2 第三阶段：外部 orchestrator 接入

如果未来需要接近无人值守，可以在不改变 `automation/` 目录命名的前提下增加：

1. runner。
2. MCP server。
3. VS Code 扩展入口。
4. 脚本化调度器。

这类组件的职责是：

1. 自动读取状态。
2. 自动触发控制入口。
3. 自动收集结果并决定下一轮是否继续。

### 19.3 第四阶段：度量与治理

后续还可以补充：

1. blocker 密度统计。
2. 任务平均闭环时间。
3. 首次通过率。
4. prompt 稳定性评估。
5. 自动化推进的风险门禁。

---

## 20. 风险与控制措施

### 20.1 状态漂移风险

风险：

1. `automation/tasks/`、`automation/blocker/` 与 `automation/state/` 三者之间状态不一致。

控制措施：

1. 明确 `automation/tasks/` 为唯一任务主状态源。
2. 明确 `automation/blocker/queue/` 为唯一 blocker 主状态源。
3. `automation/state/` 只保存快照和审计，不维护独立主表。

### 20.2 目录职责重叠风险

风险：

1. automation 内部 `controller/`、`skills/`、`tools/`、`tasks/`、`blocker/` 出现职责混淆。

控制措施：

1. 在 `automation/README.md` 中明确定义目录边界。
2. controller 只做编排，skills 只做规则资产，tools 只做执行定义，tasks/blocker 只做主数据。

### 20.3 blocker 误判风险

风险：

1. 本可在当前任务内修复的问题被过早升级为 blocker。
2. 应阻塞的问题被当作普通修复继续扩边。

控制措施：

1. 固化 blocker 判定规则。
2. 明确“是否越界”“是否需要新决策”“是否依赖外部条件”三类准入判据。

### 20.4 自动化过度扩张风险

风险：

1. 在没有稳定协议前过早追求无人值守。

控制措施：

1. 坚持 MVP 优先。
2. 先把单轮闭环做稳定，再考虑常驻执行。

### 20.5 压缩失真风险

风险：

1. 上下文压缩丢失下一轮必需信息。
2. blocker 链路在修复前被过早压缩，导致因果链断裂。

控制措施：

1. 固化 `retained_context` 的最小字段集。
2. 对 blocker 自动修复链路启用延后压缩规则。
3. 将压缩输出纳入 `automation/tests/` 的场景回归验证。

### 20.6 运行时压缩未生效风险

风险：

1. 用户误以为生成压缩摘要后，当前 Copilot 会话的 Context Window 会立即缩短。
2. 在未引入会话切换机制时，压缩只能改善下一轮启动质量，不能直接降低当前轮 token 占用。
3. 若未明确区分语义压缩与运行时压缩，容易对方案能力边界产生误判。

控制措施：

1. 在方案中显式声明：当前压缩不是“实际压掉当前 Copilot Context Window token”的压缩，而是“为下一轮会话准备的结构化压缩”。
2. 将会话切换机制单独列为后续演进项，而不是隐含认为摘要生成本身就能完成 token 回收。
3. 在后续 runner、MCP server 或外部 orchestrator 设计中，把“压缩摘要持久化 + 新会话重建”定义为运行时压缩的正式生效路径。

---

## 21. 最终建议

基于当前仓库结构和项目推进方式，推荐采用如下最终策略：

1. 在根目录新增 `automation/`，作为 Developing Automation 的独立根目录。
2. 在 `automation/` 内部独立维护 skills、tools、tasks、blocker、state、tests 等子目录。
3. 将 DASALL 其余目录视为 automation 的目标对象，而不是其内部资产提供方。
4. 通过 `automation/controller/` 提供统一入口，而不是继续依赖分散提示词或外部目录拼装上下文。
5. 先落地最小可用版本，支持“继续推进项目”和“优先修复 blocker”两种模式。
6. 待内部协议稳定后，再考虑 runner、MCP server 或 VS Code 扩展等外部自动调度能力。

一句话总结：

本项目最合适的做法，不是复用 DASALL 现有的 `skills/`、`tools/` 或 `docs/todos/`，而是在仓库根目录建立完全独立的 `automation/`，让它自带状态源、技能资产、工具定义和控制协议，把 Copilot 从“单次执行器”提升为“项目推进代理”。

---

## 22. 后续落地清单

若进入实施阶段，建议按以下顺序推进：

1. 创建 `automation/README.md`，写清目录职责边界。
2. 补齐 `automation/controller/` 下的状态机与两个入口文档。
3. 创建 `automation/tasks/` 下的任务模板、backlog 约定和状态字段协议。
4. 为 `automation/state/` 增加上下文压缩模板与摘要格式定义。
5. 固化 `automation/blocker/` 下的 triage 与 recovery 规则。
6. 在 `automation/skills/` 中补充对应的 prompt/spec/workflow 资产，包括上下文压缩规则。
7. 在 `automation/tools/` 中补充目标仓库访问、构建测试适配和输出验证能力。
8. 在 `automation/tests/` 中补齐状态机、上下文压缩和 blocker 连续性场景回归用例。
9. 选择一个工作包做试点，验证单轮闭环是否稳定。

文档版本：v1.0
日期：2026-03-18
状态：Draft
