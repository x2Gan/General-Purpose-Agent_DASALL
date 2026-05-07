# CLCFG-TODO-001 config v1 命令 grammar、TTY/non-TTY 与 exit contract 冻结

状态：Done
日期：2026-05-07
来源 TODO：docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md

## 1. 任务边界

1. 本任务只冻结 `config` 命令族的 v1 公开命令面、TTY/non-TTY 行为和本地 exit family；不提前实现 parser、workflow coordinator 或 formatter。
2. 本任务顺带收敛 `config plan` / `--dry-run`、`config apply --from-file` / `--no-input` 的关系，避免后续 parser/main 在 headless 路径上重复猜测。
3. 本任务不提前冻结 `InstallState`、`ConfigActionPlan`、`ConfigFileWriteTransaction` 的字段，也不提前冻结 plan / summary 的 JSON 主键；这些分别由 CLCFG-TODO-003、011、019 收口。
4. 本任务不吸收 P1 secret onboarding、P2 ToolSkillPage 或安装态 package payload 细节；这里只定义 v1 命令族如何被解析、何时允许交互、何时必须 fail-closed。

## 2. 证据与设计结论

### 2.1 本地证据

1. `docs/architecture/DASALL_cli_config交互式部署配置设计.md` 已给出 `config/show/plan/validate/apply --from-file` 的候选命令面和“非 TTY 默认 fail-closed”方向，但当前仍缺 config-specific exit family 与 `plan/apply` 交互边界的单点冻结文本。
2. `docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md` 将 CLCFG-TODO-001 定义为 CLCFG-TODO-006、011、012、013、014、015 的共同前置，说明 parser/main/workflow/service 接线都不能在本轮之后继续发明命令口径。
3. `docs/architecture/DASALL-cli本地控制面详细设计.md` 已冻结 CLI v1 的 `CliExitDecision` 基线：所有 CLI 用户面都必须收敛到 `0/2/3/4/5/6/7`，并保持 `--help` 优先、本地 parse/transport/protocol 先于 daemon/access facts 的裁定顺序。

### 2.2 外部参考

1. CLIG 明确建议：交互式 prompt 只应发生在 `stdin` 为 TTY 时；`--no-input` 应作为显式禁止交互的稳定入口；`--dry-run`、`--help`、稳定 exit code 与 stdout/stderr 分离要优先为脚本化让路。
2. AWS CLI `aws configure` 证明“安装后显式运行配置命令、回车保留当前值、profile 作为一等对象、交互与配置文件分离”是成熟的本地配置入口形态。
3. GitHub CLI quickstart 证明复杂 CLI 继续采用“交互入口 + 子命令 help + 每层 `--help` 自解释”比把一切塞进安装脚本或隐式别名更稳健。

### 2.3 本轮冻结结论

1. config v1 稳定命令面固定为：

```text
dasall-cli config
dasall-cli config show
dasall-cli config plan [--from-file <path>] [--dry-run]
dasall-cli config validate
dasall-cli config apply --from-file <path> --no-input
```

2. `dasall-cli config` 无子命令时固定表示交互式 wizard；它不是 `show`、`plan` 或 `validate` 的隐式别名。
3. `config show`、`config plan`、`config validate` 固定为非交互命令；无论是否在 TTY 中执行，都不得弹出 prompt、masked input 或确认页。
4. `config plan --dry-run` 固定为 `plan` 的语义别名或兼容 flag，不新增第二条执行路径；`--dry-run` 不得出现在 `show`、`validate`、`apply` 上。
5. `config apply` 首版只允许 `--from-file <path>` 入口；未提供 `--from-file` 的 `config apply` 必须 fail-closed，避免未来出现“半交互半无头”的第三条 apply 语义。
6. `config apply --from-file` 必须同时要求 `--no-input` 或等价确认策略；首版统一冻结为显式 `--no-input`，未提供时按参数/交互契约错误处理，而不是偷偷回退到提示确认。
7. 本轮只冻结命令 grammar 和行为边界，不提前冻结 human/JSON 输出主键；但后续 formatter 任务必须复用 CLI 既有 `--json` 习惯，不得另造 `--format plan-json` 一类新语法分叉。

## 3. TTY / non-TTY 语义与 exit family

### 3.1 TTY / non-TTY 规则

1. `config` 无子命令时要求交互式终端；若当前调用不满足交互条件，则必须 fail-closed，并明确提示使用 `config plan --from-file` 或 `config apply --from-file --no-input`。
2. `config show` 允许非 root、允许非 TTY；遇到权限不足的字段时展示 `unavailable` 或等价占位，而不是要求先提权才能得到基础摘要。
3. `config plan` 与 `config validate` 在 TTY 与非 TTY 下语义完全一致：只读、无副作用、不可触发确认页。
4. `config apply --from-file --no-input` 是 v1 唯一受支持的 headless mutating path；它必须共享与交互式 wizard 相同的 plan/apply 内核，但不共享 prompt 行为。
5. `--help` / `-h` 在顶层和各级子命令都优先显示帮助文本并返回成功，不再继续执行任何文件探测、校验或 systemd 动作。

### 3.2 config v1 exit family

config 命令族不得引入新的退出码家族，必须扩展既有 `CliExitDecision`，并继续收敛到 `0/2/3/4/5/6/7`：

| 场景 | 输出 exit code | 冻结说明 |
|---|---|---|
| `help`、`show`、`plan`、`validate`、`apply` 成功完成 | `0` | 包括交互式 wizard 成功结束、`plan --dry-run` 成功输出、`show` 成功输出摘要 |
| 参数/usage/TTY 契约错误 | `2` | 包括未知子命令、非法 flag 组合、非 TTY 调用交互式 `config`、`apply` 缺少 `--from-file` 或 `--no-input` |
| 本地依赖或本地执行入口不可用 | `3` | 包括缺少必须调用的本地二进制、必须的本地 helper 不可执行、安装态命令别名尚未存在 |
| 权限不足或 operator access 模型拒绝 | `4` | 包括需要 root/sudo 的写操作、P0 `0600 root/sudo-only` 模型下的非特权 mutating 调用 |
| 确定性配置失败 | `5` | 包括 `validate-only` 失败、文件事务提交失败、回滚失败、apply 完成后仍落入 fail-closed 的 definitive failure |
| 可重试或待外部条件满足的阻断 | `6` | 包括受控的 preflight block、短暂 systemd/daemon not-ready、未来可重试的 service action 阻塞 |
| 本地 contract / projection 不变量破坏 | `7` | 包括 parser / coordinator / formatter 之间的内部契约冲突或不应到达的 config disposition |

### 3.3 config-local disposition 扩展策略

1. `CliExitDecision` 继续保持“先本地，再协议，再服务端事实”的裁定顺序；对 config 命令而言，对应变为“先 grammar/TTY，再本地依赖/权限，再 preflight/validate/apply 结果”。
2. `config` 本地工作流可以引入 `local_argument_error`、`local_permission_required`、`local_dependency_unavailable`、`local_validation_failed`、`local_retryable_block` 一类内部 disposition，但这些内部枚举只能投影到既有 `0/2/3/4/5/6/7`，不得新增第八种对外 exit code。
3. 用户中止交互式 wizard 若属于显式取消而非错误，首版按 `2` 处理，保持“未完成且无副作用”的明确脚本语义；后续若要演进为单独 `cancelled` 文本，只能修改 human diagnostics，不得改变 exit family。

## 4. Design -> Build 映射

| 设计结论 | 直接解锁任务 | Build 落点 |
|---|---|---|
| `config/show/plan/validate/apply --from-file --no-input` 是唯一稳定语法面 | CLCFG-TODO-006、012、013 | `CliCommandParser::parse()`、`usage_string()`、`main()` 本地 dispatch 不再猜测 apply 入口 |
| 交互式 `config` 必须 TTY fail-closed | CLCFG-TODO-006、011、014 | parser / prompt engine / coordinator 共用同一 TTY guard，不再在页面层临时决定 |
| `plan --dry-run` 只是 `plan` 的别名 | CLCFG-TODO-006、012、013 | parser canonicalize 到同一 plan path；workflow / diff planner 不新增第二套 dry-run 内核 |
| config 命令族沿用 `CliExitDecision` 的 `0/2/3/4/5/6/7` | CLCFG-TODO-006、011、012、013、015 | 本地 config disposition 只做投影扩展，不制造第二套 CLI exit code |
| `apply --from-file` 必须显式 `--no-input` | CLCFG-TODO-006、013 | headless apply 和未来 CI smoke 保持可判定、不会卡在交互确认 |

## 5. Validation

1. `rg -n "config show|config plan|config validate|config apply|TTY|--from-file|--no-input|dry-run|CliExitDecision|0/2/3/4/5/6/7" docs/architecture/DASALL_cli_config交互式部署配置设计.md docs/architecture/DASALL-cli本地控制面详细设计.md docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md docs/todos/cli/deliverables/CLCFG-TODO-001-config-grammar与TTY语义冻结.md`

结果摘要：

1. config 设计、CLI 详设、专项 TODO 与本 deliverable 将同时出现统一的命令族、TTY fail-closed、`apply --from-file --no-input` 和 `CliExitDecision` 映射口径。
2. 本任务属于设计冻结轮次，当前不要求跑 parser/unit；后续进入 CLCFG-TODO-006 时，现有 `CliDaemonCommandParserTest` 将作为最近 parser 回归锚点继续扩展。

## 6. D Gate 结论

结论：PASS。

通过依据：

1. `config` 命令族、TTY/non-TTY 规则与 exit family 已形成唯一文本口径。
2. `plan` / `apply`、交互 / 非交互、开发态 / 安装态的边界已能被二值评审。
3. CLCFG-TODO-006、011、012、013、014、015 已具备明确的 Build 入口，不再需要在代码中临时发明命令行为。

## 7. 完成判定

CLCFG-TODO-001 完成的判定标准为：

1. `config`、`show`、`plan`、`validate`、`apply --from-file --no-input` 的稳定命令面与 reject 规则都可在文档中逐条核对。
2. 交互式 `config` 的 TTY fail-closed 规则与非交互命令的“绝不 prompt”规则已明确，不再残留“看实现再决定”的灰区。
3. config 本地工作流已明确复用 CLI 既有 `CliExitDecision` family，而不是另起新的本地退出码体系。
4. 后续 Build 任务只允许补 parser、coordinator、formatter 和 tests，不允许重新定义命令 grammar、TTY 契约或 exit family。