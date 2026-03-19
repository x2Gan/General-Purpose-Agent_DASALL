# Project Implementation Cycle Skill

## 1. 这个 Skill 是做什么的

这个 Skill 用于驱动项目实施的一轮完整闭环。它适用于如下场景：

1. 打开一个 TODO 文档。
2. 选择一个可执行的原子任务。
3. 使用双轨任务模板启动任务执行。
4. 在 Build 结束前执行模板合规复核，包括代码注释、正负例、测试发现性和 TODO 证据。
5. 如果任务阻塞，则切换到 blocker 修复流程。
6. 如果任务完成，则优先通过 `git-task-submit` skill 按仓库规范提交并推送修改。
7. 完成本轮推进，并给出结果和下一任务建议。

这个 Skill 不是泛化问答模板，而是仓库内的项目推进工作流。

---

## 2. 如何触发这个 Skill

最常见的触发方式是在聊天里直接描述你的目标。

### 2.1 指定 TODO 文档启动

示例：

```text
请使用 project-implementation-cycle，从 docs/todos/contracts-freeze/WP-05-子域细化与ContractTestsTODO.md 选择一个可执行原子任务推进一轮；若阻塞则先修 blocker；完成后按 docs/development/Git提交信息规范.md 提交并推送。
```

### 2.2 使用自然语言触发

示例：

```text
请推进一轮项目实施，从 docs/todos/contracts-freeze/WP-05-子域细化与ContractTestsTODO.md 开始。
```

```text
打开当前工作包 TODO，选一个可执行原子任务，按双轨模板执行，若阻塞则解阻，最后提交并推送。
```

### 2.3 不指定 TODO 路径启动

如果你没有提供 TODO 文档，Skill 会先读取默认 TODO 路径资产：

`assets/default-todo-paths.md`

示例：

```text
请继续推进项目一轮，按默认 TODO 路径选择任务，若阻塞则走 blocker 修复流程，完成后提交并推送。
```

---

## 3. 它会自动执行什么流程

当 Skill 被命中后，预期执行顺序如下：

1. 确定 TODO 文档路径。
2. 读取 TODO 文档并识别候选任务。
3. 根据规则选择一个当前可执行的原子任务。
4. 组装任务启动提示词。
5. 按双轨执行模板推进任务。
6. 在离开 Build 前执行模板合规复核。
7. 如果阻塞，则进入 blocker 修复分支。
8. 如果任务完成，则优先调用 `git-task-submit` skill 提交并推送。
9. 如果提交失败，则输出 `Submission Blocked`，而不是把本轮报成完成。
10. 返回本轮结果、证据、提交信息和下一步建议。

---

## 4. 推荐输入方式

为了让 Skill 更稳定命中，建议在请求中尽量包含下面几项信息：

1. TODO 文档路径。
2. 是否只推进一轮。
3. 是否只选一个原子任务。
4. 遇到 blocker 时是否优先解阻。
5. 完成后是否需要直接提交并推送。
6. 是否要求严格执行 Build 合规复核。

推荐模板：

```text
请使用 project-implementation-cycle：
- TODO: docs/todos/contracts-freeze/WP-05-子域细化与ContractTestsTODO.md
- 只选一个可执行原子任务
- 若阻塞则先修 blocker
- 完成后按仓库规范提交并推送
```

---

## 5. 目录结构说明

本 Skill 目录当前包含以下文件：

1. `SKILL.md`
   Skill 主入口，定义用途、触发语义、主流程和引用资源。

2. `references/task-selection-rules.md`
   定义如何从 TODO 文档中选择“一个可执行原子任务”。

3. `references/blocker-recovery-rules.md`
   定义 blocker 分类、修复分支和停止条件。

4. `references/submission-handoff.md`
   定义完成本轮后如何按仓库规范提交并推送。

   说明：若工作区存在 `git-task-submit` skill，则该文件现在要求默认转交给该 skill，而不是把提交逻辑留给调用方临时拼装。

5. `assets/wp-task-launch-template.txt`
   主任务启动模板，用于把 TODO 和 task_id 组装成执行口令。

6. `assets/default-todo-paths.md`
   默认 TODO 路径资产，用于无路径启动时的默认入口。

7. `assets/blocker-recovery-launch-template.txt`
   blocker 修复启动模板，用于阻塞分支的结构化切换。

---

## 6. 这个 Skill 依赖哪些仓库文档

它的核心依赖有两类：

1. 任务执行模板
   `docs/development/WP双轨任务执行提示词模板.md`

2. 提交规范
   `docs/development/Git提交信息规范.md`

也就是说，这个 Skill 自己不重新定义仓库规则，而是把现有规则组织成一轮完整工作流。

---

## 7. blocker 是怎么处理的

如果在任务推进中出现 blocker，Skill 不会直接跳过，而是会：

1. 明确 blocker 类型。
2. 给出 blocker 证据。
3. 判断能否在本轮内最小修复。
4. 若可修复，则修复后恢复原任务。
5. 若不可修复，则输出 Blocked 和最小解阻条件。

这部分由下面两个文件支撑：

1. `references/blocker-recovery-rules.md`
2. `assets/blocker-recovery-launch-template.txt`

---

## 8. 提交和推送是怎么处理的

如果本轮任务完成，Skill 会进入提交推送阶段，并遵循仓库规范：

1. 只暂存当前轮次相关文件。
2. 默认优先调用 `git-task-submit` skill。
3. 按 `<type>(<scope>): <subject>` 组织提交标题。
4. 需要时补充提交正文。
5. 推送到仓库允许的远端目标。
6. 若提交或推送失败，输出 `Submission Blocked`。

相关规则在：

1. `references/submission-handoff.md`
2. `docs/development/Git提交信息规范.md`

---

## 9. 什么时候适合用它

适合：

1. 你要从 TODO 文档推进一轮真实工作。
2. 你要让 AI 帮你选一个可执行原子任务。
3. 你要把任务执行、blocker 修复和提交推送串成一个闭环。

不适合：

1. 只是问一个概念问题。
2. 只是修改一个文件但不需要任务选择和状态推进。
3. 只是单独想写 commit message。

第三种情况更适合直接用 `git-task-submit`。

---

## 10. 一个可直接复制的最短示例

```text
请使用 project-implementation-cycle，从 docs/todos/contracts-freeze/WP-05-子域细化与ContractTestsTODO.md 选择一个可执行原子任务推进一轮；若阻塞则先修 blocker；完成后按 docs/development/Git提交信息规范.md 提交并推送。
```

---

## 11. 学习这个 Skill 时建议的阅读顺序

1. `SKILL.md`
2. `assets/wp-task-launch-template.txt`
3. `assets/default-todo-paths.md`
4. `assets/blocker-recovery-launch-template.txt`
5. `references/task-selection-rules.md`
6. `references/blocker-recovery-rules.md`
7. `references/submission-handoff.md`

按这个顺序读，最容易看清主流程、默认入口、阻塞分支和提交闭环之间的关系。