# CLI-TODO-001 CLI 命令 schema 与 usage 文案冻结

状态：Done
日期：2026-05-04
来源 TODO：docs/todos/cli/DASALL-cli本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只冻结 `run`、`status`、`cancel`、`help`、`version` 的 v1 参数 schema、usage skeleton 与公开帮助面，不提前实现 parser 或 binary 行为变更。
2. 本任务顺带消除 `--socket` 与 `--socket-path` 的命名漂移，明确 daemon endpoint 覆盖面只有一个稳定用户面名称。
3. 本任务不提前冻结 `--json` envelope、`CliExitDecision` 或 stdout/stderr JSON 契约；这些仍由 CLI-TODO-002 收口。
4. 本任务不提前补 platform peer identity、daemon receipt 权属校验或 runtime cancel/query 实现；这里只定义 CLI 用户面应如何表达这些能力。

## 2. 证据与设计结论

### 2.1 本地证据

1. `docs/architecture/DASALL-cli本地控制面详细设计.md` 在 1.6 明确了命令族已冻结到能力级，但仍保留“`run` 业务参数形态、`status/cancel` 精确参数名属于后续 Build 冻结”的口径，导致 parser/help 任务无法直接开工。
2. `apps/cli/src/CliCommandParser.h/.cpp` 当前只支持最小 positional 形态，并暴露 `submit`、`readiness`、`actor_ref` 等兼容/过渡 surface；这些实现锚点不能直接等同于 v1 公开 contract。
3. `docs/todos/cli/DASALL-cli本地控制面专项TODO.md` 已将 `CLI-TODO-001` 明确为 `CLI-BLK-001` 的解阻任务，要求同时收敛参数矩阵、usage 文案责任边界与 `--socket-path` 稳定命名。

### 2.2 外部参考

1. CLIG 建议：帮助面应支持顶层与子命令 `-h/--help`，默认输出 concise help；复杂命令应优先用稳定 flag 名称，并避免让兼容 alias 进入公开帮助面。
2. CLIG 建议：当一个命令有多个不同语义输入时优先使用 named flags；`stdout`/`stderr` 与机器可读输出应保持可组合与可脚本消费。
3. kubectl conventions 建议：脚本场景应依赖稳定的机器可读输出与显式参数，而不是隐式上下文或会变化的默认行为。

### 2.3 本轮冻结结论

1. v1 公开命令面固定为 `run`、`status`、`cancel`、`ping`、`diag`、`help`、`version`；`submit` 与 `readiness` 只允许作为兼容/迁移 surface 存在，且不得出现在顶层 help 中。
2. v1 唯一稳定的 endpoint override flag 固定为 `--socket-path`；设计文档中旧的 `--socket` 口径回链修正为命名漂移，不保留公开 alias。
3. `status/cancel` 的公开 schema 改为“显式 selector + 显式 receipt 所有权令牌”，不再把 `actor_ref` 暴露为公开帮助面参数；本地主体由 daemon 根据 local peer identity 推导。
4. `version` 在 v1 收敛为本地 CLI 版本视图，不主动连接 daemon，也不引入 `--daemon` 一类额外网络行为；daemon 版本比对保留为后续加法扩展。
5. help 责任边界固定为：顶层 `dasall` / `dasall help` / `dasall -h` / `dasall --help` 提供 concise help；`dasall help <command>` 与 `<command> --help` 提供命令级 help；命令级 help 不承担 daemon 连通性检查。

## 3. v1 参数 schema 与 usage skeleton

### 3.1 通用规则

1. `--json`、`--timeout-ms`、`--socket-path`、`--quiet`、`--no-input` 属于 CLI UX / transport 级 flag；除特别注明外，允许出现在子命令前后，parser 应按无歧义顺序无关方式解析。
2. `--async` 只适用于 `run`；`status`、`cancel`、`help`、`version` 传入该 flag 时必须 fail-closed。
3. `-h` / `--help` 只表示帮助，不承担其他语义；附加在任意命令尾部时优先显示帮助，而不是继续执行网络请求。
4. `--socket-path` 只适用于会触达 daemon 的命令：`run`、`status`、`cancel`、`ping`、`diag`；`help`、`version` 不接受该 flag。
5. 兼容命令 `submit`、`readiness` 如在过渡期保留，必须 canonicalize 到内部稳定命令名并保持隐藏；新 help、文档、contract tests 一律不引用这些名字。

### 3.2 命令矩阵

| 命令 | 稳定 usage skeleton | 必填参数 | 可选参数 | 明确不纳入 v1 公开面 |
|---|---|---|---|---|
| `run` | `dasall run <request_json_or_->` | 一个请求输入源：`<request_json>` 或 `-` | `--async`、`--request-id <id>`、`--session <hint>`、`--trace-id <id>`、`--timeout-ms <ms>`、`--json`、`--socket-path <path>`、`--quiet`、`--no-input` | `submit` 公开别名、`--socket`、`--actor-ref` |
| `status` | `dasall status (--receipt <receipt_ref> --ownership-token <token> | --request-id <request_id>)` | 恰好一个 selector；若走 receipt 路径则必须带 `--ownership-token` | `--timeout-ms <ms>`、`--json`、`--socket-path <path>`、`--quiet` | positional `receipt_ref ownership_token actor_ref`、`--async`、公开 `--actor-ref` |
| `cancel` | `dasall cancel (--receipt <receipt_ref> --ownership-token <token> | --request-id <request_id>)` | 恰好一个 selector；若走 receipt 路径则必须带 `--ownership-token` | `--timeout-ms <ms>`、`--json`、`--socket-path <path>`、`--quiet` | positional `receipt_ref ownership_token actor_ref`、`--async`、公开 `--actor-ref` |
| `help` | `dasall help [command] [subcommand]` | 无 | 顶层 `-h/--help`；命令级 `<command> --help` | `--json`、`--socket-path`、任何触网行为 |
| `version` | `dasall version` | 无 | `--json`、`--quiet` | `--socket-path`、daemon 自动探测、`--daemon` |

### 3.3 解释性规则

1. `run` 的 `-` 明确表示从 `stdin` 读取请求体；若未来实现支持“未显式给出 payload 且 `stdin` 非 TTY 时自动读 stdin”，该行为只能作为 `run -` 的兼容简写，不得改变 usage skeleton。
2. `status/cancel` 的 selector 规则固定为二选一：`--receipt` 路径面向 accepted receipt 回查；`--request-id` 路径面向显式 request replay / timeout 后补查。两种路径不得混用。
3. `--ownership-token` 只属于 receipt 查询/取消路径；`--request-id` 路径传入该 flag 必须被 parser 拒绝，避免把 receipt ownership 模型混入 request replay 语义。
4. `help` 保持 human-only 输出；脚本若需要机器可读元信息，应使用 `version --json` 或命令本身的 `--json` 结果，而不是消费帮助文本。
5. `version` 默认只报告 CLI 本地版本、schema 支持范围与 build metadata；daemon 兼容性检查留给后续显式扩展，而不是把 `version` 变成隐式 ping。

## 4. Design -> Build 映射

| 设计结论 | 直接解锁任务 | Build 落点 |
|---|---|---|
| `--socket-path` 是唯一稳定 endpoint override 名称 | CLI-TODO-006、CLI-TODO-007 | `CliCommand` 字段与 parser/help 只注册 `--socket-path`，并对 `--socket` fail-closed |
| `status/cancel` 使用显式 selector schema | CLI-TODO-006、CLI-TODO-007、CLI-TODO-008 | `CliCommand` 需要 selector 类型与 token 字段；request builder 将 selector 正交投影为 frame args |
| `actor_ref` 不进入公开帮助面 | CLI-TODO-007、CLI-TODO-008、CLI-TODO-013 | parser 与帮助文案移除 `actor_ref` 公开入口；集成门只验证 local peer/receipt ownership 的公开 contract |
| `version` v1 local-only | CLI-TODO-007、CLI-TODO-010 | `main()` 与 formatter 不在 `version` 路径上发起 daemon RPC |
| `submit/readiness` 仅为隐藏兼容 surface | CLI-TODO-007 | help/usage 不再展示兼容命令名；如保留 parser 兼容，需 canonicalize 到稳定命令名并补迁移注记 |

## 5. Validation

1. `rg -n "run/status/cancel|--socket-path|--socket|submit|readiness|help|version" docs/architecture/DASALL-cli本地控制面详细设计.md docs/todos/cli/DASALL-cli本地控制面专项TODO.md docs/todos/cli/deliverables/CLI-TODO-001-CLI命令schema与usage文案冻结.md`
2. `ctest --test-dir build-ci -R "CliDaemonCommandParserTest" --output-on-failure`

结果摘要：

1. 文档检索结果显示，详细设计、专项 TODO 与本 deliverable 已同时出现 `run/status/cancel/help/version`、`--socket-path`、`submit/readiness` 兼容边界与 version local-only 等关键锚点，CLI-TODO-001 的冻结口径不再只停留在单一文件。
2. `CliDaemonCommandParserTest` 已通过 focused 回归，说明本轮文档冻结没有引入对当前 parser 基线的自相矛盾约束；现有实现仍可作为 CLI-TODO-006/007 的最近实现锚点继续扩展。
3. `RunCtest_CMakeTools` stderr 中的 `DartConfiguration.tcl` 缺失仍是当前仓库的既有 CMake Tools 噪声，不影响 `CliDaemonCommandParserTest` 的通过结论。

## 6. 完成判定

CLI-TODO-001 完成的判定标准为：

1. `run/status/cancel/help/version` 的稳定参数名、usage skeleton、flag 适用域与 reject 规则均可在文档中二值核对。
2. `--socket-path` 已收敛为唯一稳定命名，`--socket` 不再作为公开 help surface 存在。
3. `version` 是否触达 daemon、`status/cancel` 是否暴露 `actor_ref`、兼容命令 `submit/readiness` 是否进入公开帮助面，这三处边界已明确，不再残留“后续冻结”描述。
4. CLI-TODO-006、CLI-TODO-007、CLI-TODO-008 可以直接引用本文件进入 parser/request builder Build 任务，而不需要再猜测用户面 contract。