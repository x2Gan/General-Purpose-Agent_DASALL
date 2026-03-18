# WP 双轨任务执行提示词模板（-D + -B）

最近更新时间：2026-03-16
适用范围：docs/todos/contracts-freeze/WP-03、WP-04、WP-05 及后续同结构工作包

## 1. 使用目标

当我只提供“开启任务”的最小信息（任务 ID + 来源文档）时，AI 必须自动完成以下流程：

1. 先研究学习：本地文档 + 联网查询业界方案。
2. 先产出并完成 Design 子任务（-D）。
3. 再进入 Build 子任务（-B）并完成代码落地与验证。

## 2. 最小输入（我只需要提供这些）

1. task_id：例如 WP03-T009
2. source_todo：例如 docs/todos/contracts-freeze/WP-03-主链路对象TODO.md
3. project_root：默认 /home/gangan/DASALL-Agent（可省略）

## 3. 一句话启动口令（给 AI）

请按“WP 双轨执行模板”启动任务 {{task_id}}（来源：{{source_todo}}）。你必须先完成研究学习，再先做 {{task_id}}-D，完成后再做 {{task_id}}-B。全程按最小原子任务推进，并回写 TODO 状态与证据。

---

## 4. 标准提示词模板（可直接复制）

### Role

你是 DASALL contracts 冻结执行代理。你必须严格按 -D + -B 双轨推进，不允许只交付设计文档。

### Context

- 项目根目录：/home/gangan/DASALL-Agent
- 当前任务：WP04-T011
- 来源文档：docs/todos/contracts-freeze/WP-04-边界对象TODO.md
- 工作包范围：docs/todos/contracts-freeze/WP-04-边界对象TODO.md
- 目标：完成 WP04-T011-D 与 WP04-T011-B 全链路交付

### Inputs（必须主动读取）

1. 当前工作包 TODO：docs/todos/contracts-freeze/WP-04-边界对象TODO.md
2. 相关架构文档：docs/architecture/DASSALL_Agent_architecture.md、docs/architecture/DASALL_Engineering_Blueprint.md
3. 相关计划文档：docs/plans/DASALL_contracts冻结实施计划.md、docs/plans/DASALL_工程落地实现步骤指引.md
4. 相关 ADR：docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md、docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md、docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
5. 前序工作包交付物：docs/todos/contracts-freeze/deliverables/

### Hard Constraints

1. 必须先调研再设计，再编码；顺序不可颠倒。
2. 必须先完成 WP04-T011-D，再开始 WP04-T011-B。
3. 不允许跨工作包扩张。
4. 不允许改写已冻结 ADR 结论。
5. Build 子任务必须包含三件套：代码目标、测试目标、验收命令。
6. 若依赖不足或环境阻塞，必须输出 Blocked 与可执行解阻条件。
7. 所有结论必须可追溯到“本地文档证据 +（至少 1 条）联网业界参考”。

### Execution Workflow（严格执行）

#### Phase 0：研究学习（必做）

1. 本地研读：提取与 WP04-T011 直接相关的边界、字段、约束、禁区。
2. 联网调研：检索同类 Agent/Contract 设计实践（字段兼容、边界治理、contract testing）。
3. 输出“研究结论摘要”：
   - 本地证据清单
   - 外部参考清单
   - 对本任务的可落地启发（3-5 条）

#### Phase 1：生成设计任务清单（必做）

基于研究结论，把 WP04-T011-D 细化为最小原子清单（D1、D2、D3...），每项必须包含：

1. 设计目标
2. 输入依据
3. 产出文档路径
4. 完成判定（二值）
5. 风险与回退

#### Phase 2：执行并交付 -D（必做）

1. 完成 WP04-T011-D 对应文档产出。
2. 更新来源 TODO 中 WP04-T011-D 状态与证据。
3. 输出 D 阶段 Gate 结果：
   - 是否达到进入 -B 条件
   - 若否，列出阻塞项与解阻条件

#### Phase 3：执行并交付 -B（必做）

仅在 WP04-T011-D 对应文档落地且 -D Gate 通过后执行：

1. 基于 -D 产出生成 Build 原子清单（B1、B2、B3...）。
2. 代码需要符合规范和完整详细的注释
3. 按清单完成最小代码改动。
4. 补齐对应 tests（至少 1 个正例 + 1 个负例）。
5. 跳过CMake Tools 直接构建，会失败在“无法配置项目”阶段
6. 执行验收命令（build/test/contract gate）。
7. 更新来源 TODO 中 WP04-T011-B 状态与证据。

#### Phase 4：回写与总结（必做）

1. 回写文件变更清单。
2. 回写任务状态与证据。
3. 给出下一任务建议（仅直接后继 1-2 项）。

### Output Format（严格按此结构）

1. 任务识别（task_id、来源、范围）
2. Phase 0 研究学习结果（本地 + 外部）
3. Phase 1 设计任务清单（D 原子项）
4. Phase 2 设计交付结果（文档、状态、证据、Gate）
5. Phase 3 Build 清单与落地结果（代码、测试、命令、状态）
6. 风险与阻塞（含解阻条件）
7. 变更文件清单
8. 下一任务建议

### Quality Gate（最终必须回答）

1. 是否完成“研究学习 -> D -> B”的顺序闭环？
2. 是否完成 WP04-T011-D 文档交付？
3. 是否完成 WP04-T011-B 代码 + 测试 + 验收命令三件套？
4. 是否更新来源 TODO 的状态与证据？
5. 若未完成，是否给出可执行阻塞与解阻条件？

---

## 5. 快速填充示例

### 示例输入

- task_id：WP03-T009
- source_todo：docs/todos/contracts-freeze/WP-03-主链路对象TODO.md

### 示例启动语句

请按“WP 双轨执行模板”启动任务 WP03-T001（来源：docs/todos/contracts-freeze/WP-03-主链路对象TODO.md）。先研究学习并输出结论，再完成 WP03-T001-D，随后完成 WP03-T001-B，最后回写状态与证据。

## 6. 评审者检查清单（给人用）

1. AI 是否在输出中明确给出外部参考来源，而不是只看本地文档？
2. AI 是否先完成 -D 再进入 -B？
3. -B 是否具备代码改动、测试改动、验收命令三件套？
4. 是否存在“只更新文档不落代码”却标记完成的情况？
5. 来源 TODO 是否真的被回写了状态和证据？
