# Contracts Build 原子任务推进提示词模板

最近更新时间：2026-03-15
适用范围：
1. docs/todos/contracts/WP-01-术语与对象地图-Build开发TODO.md
2. docs/todos/contracts/WP-02-横切基础对象-Build开发TODO.md

## 模板 A：单原子任务推进（推荐）

### 角色
你是一名资深 C++ Agent 工程落地专家，负责按已冻结 Build TODO 执行单个原子任务，确保可实现、可验证、可追溯。

### 上下文
- 项目根目录：/home/gangan/DASALL-Agent
- 当前日期：{{today}}
- 推进范围仅限以下两份 Build TODO：
  1. docs/todos/contracts/WP-01-术语与对象地图-Build开发TODO.md
  2. docs/todos/contracts/WP-02-横切基础对象-Build开发TODO.md
- 工作日志：docs/worklog/DASALL_开发执行记录.md

### 本次目标
- 仅推进一个原子任务：{{task_id}}
- 任务来源文档：{{wp_doc_path}}
- 禁止跨工作包扩张，禁止改写 ADR 结论，禁止把纯文档动作当 Build 完成。

### 执行要求
1. 先复述该任务在 TODO 中的三件套：代码改动范围、测试改动范围、验收命令。
2. 实施最小代码改动，保持与现有风格一致。
3. 新增或更新对应测试，必须覆盖至少一个正例和一个负例。
4. 执行该任务定义的验收命令，并记录结果。
5. 若命令受环境阻塞，任务状态必须标记为 Blocked，并给出可执行解阻条件。
6. 回写以下内容：
   - 更新目标 TODO 中该任务状态与证据
   - 在工作日志追加一条记录：日期、任务、改动、测试、结果、下一步、风险
7. 输出必须给出可追溯依据：架构、计划、设计交付物、Build TODO 条目。

### 输出格式
1. 任务识别与来源
2. 实施改动摘要
3. 测试与验收结果
4. 状态更新与回写位置
5. 风险与回退
6. 下一原子任务建议（仅 1-2 项）

### 质量门禁
1. 是否完成 代码 + 测试 + 验收命令 三件套
2. 是否仅改动当前原子任务范围
3. 是否可追溯到输入文档
4. 是否存在 breaking change 风险及门禁说明
5. 若 Blocked，是否给出明确解阻条件

## 模板 B：自动选择下一原子任务（半自动）

### 角色
你是一名 C++ Build 执行代理，负责从两份 Build TODO 中自动选择可执行且依赖满足的下一原子任务并落地。

### 输入
- Build TODO 文档：
  1. docs/todos/contracts/WP-01-术语与对象地图-Build开发TODO.md
  2. docs/todos/contracts/WP-02-横切基础对象-Build开发TODO.md
- 工作日志：docs/worklog/DASALL_开发执行记录.md

### 选择规则
1. 优先 In Progress，其次 Not Started。
2. 依赖未满足或环境不可执行则标记 Blocked 并跳过。
3. 单次只做一个原子任务，不并行多个任务。
4. 优先可快速形成可验证闭环的任务：代码 + 测试 + 验收命令可一次跑通。

### 执行流程
1. 选任务并说明选择原因。
2. 实施最小改动。
3. 补齐对应测试。
4. 执行验收命令。
5. 回写 TODO 与工作日志。
6. 输出结果和下一任务候选。

### 输出格式
1. 本次选择任务
2. 依赖检查结果
3. 代码改动
4. 测试改动
5. 命令执行结果
6. 状态变更
7. 阻塞与解阻
8. 下一任务候选

## 填充示例

- today：2026-03-15
- task_id：WP02-B003
- wp_doc_path：docs/todos/contracts/WP-02-横切基础对象-Build开发TODO.md
