# Git 提交信息规范

最近更新时间：2026-03-16
适用范围：DASALL 全仓库（代码、测试、文档、脚本、构建配置）

## 1. 目标

1. 让提交信息可读、可检索、可追溯。
2. 让评审者可快速判断改动意图、风险和验证结果。
3. 在 WP 执行场景中，提交信息可直接映射到 Design/Build TODO 与工作日志。

## 2. 提交标题格式（强制）

提交标题使用以下格式：

```text
<type>(<scope>): <subject>
```

要求：

1. `type` 必填，使用规范词典。
2. `scope` 建议必填，使用模块名或任务号（如 `wp01`、`contracts`、`tests`、`docs`）。
3. `subject` 使用简明动词短语，说明“做了什么”，不写过程。
4. 标题总长度建议不超过 72 字符。
5. 不使用模糊标题，如“update files”“fix bug”。

## 3. type 词典（强制）

1. `feat`：新增能力或新文件（对外行为有增量）。
2. `fix`：修复缺陷或错误行为。
3. `refactor`：重构，保持行为不变。
4. `test`：新增或调整测试。
5. `build`：构建系统、CMake、脚本、依赖、CI 门禁。
6. `docs`：文档内容更新。
7. `chore`：杂项维护（不影响行为，非 docs/test/build）。
8. `revert`：回滚历史提交。

## 4. scope 约定（建议）

推荐优先级：

1. 工作包任务：`wp01`、`wp02`。
2. 模块边界：`contracts`、`runtime`、`tools`、`memory`。
3. 交付类型：`tests`、`docs`、`ci`、`cmake`。

可组合使用短横线：

```text
build(wp01-contracts): ...
docs(wp01): ...
test(contracts): ...
```

## 5. 提交正文模板（推荐）

当改动超过 3 个文件，或包含设计/测试/门禁变化时，必须写正文。

```text
<type>(<scope>): <subject>

Context:
- 任务来源：WPxx-Bxxx / WPxx-Txxx
- 约束来源：ADR-xxx / 架构文档章节

Changes:
- 改动 1
- 改动 2
- 改动 3

Validation:
- 命令 1 + 结果
- 命令 2 + 结果

Traceability:
- TODO: <path>#Lx
- Worklog: <path>#Lx
```

## 6. Footer 规则（可选但推荐）

1. 破坏性变更必须显式声明：

```text
BREAKING CHANGE: <影响面与迁移方式>
```

2. 任务关联（Issue/PR/WP）建议写在尾部：

```text
Refs: WP01-B007, WP01-B008
```

## 7. WP 场景示例

1. 仅构建入口收敛：

```text
build(wp01-contracts): converge contract test registration and gate deps
```

2. 恢复语义回归增强：

```text
test(wp01-recovery): add combination regression matrix for boundary guards
```

3. 协同语义回归增强：

```text
test(wp01-multi-agent): add layered violation matrix for request/result/worker
```

4. 文档回写：

```text
docs(wp01): sync build todo evidence and execution worklog
```

5. 目录迁移：

```text
refactor(contracts-boundary): move boundary headers to contracts/include/boundary
```

## 8. 提交前检查清单（强制）

1. 标题是否符合 `<type>(<scope>): <subject>`。
2. 是否能从标题判断改动意图。
3. 是否包含必要正文（中大型改动）。
4. 是否有验证命令与结果。
5. 是否与 TODO/工作日志可追溯。
6. 若有 breaking change，是否给出迁移说明。

## 9. 与常用命令的最小流程

```bash
git add .
git commit -m "<type>(<scope>): <subject>"
git push -u origin master
```

当改动较大，建议改为：

```bash
git add .
git commit
# 在编辑器中填写标题 + 正文模板
git push -u origin master
```
