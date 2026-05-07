# CLI-TODO-002 CLI JSON envelope 与 CliExitDecision 契约冻结

状态：Done
日期：2026-05-04
来源 TODO：docs/todos/cli/DASALL-cli本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只冻结 CLI v1 `--json` stable envelope、stdout/stderr 归属和 `CliExitDecision` 的最终投影规则，不提前实现 formatter、contract tests 或主程序退出码切换。
2. 本任务明确 `--json` 绑定的是 CLI projection，而不是 `contracts::AgentResult` 或 `UdsResponseFrame` 的 raw 镜像。
3. 本任务明确 access error mapping 只提供 code/domain/retryable/default reason 等输入事实；CLI v1 最终退出码仍由 `CliExitDecision` 投影为 `0/2/3/4/5/6/7`。
4. 本任务不提前补 parser/help/version、request builder、platform peer identity 或 daemon/auth/receipt 的服务端实现；这里只冻结 CLI 用户面 contract。

## 2. 证据与设计结论

### 2.1 本地证据

1. `apps/cli/src/CliOutputFormatter.h/.cpp` 当前只输出人类可读字符串，还没有 `--json` 模式，因此 JSON 主键、`error/warnings` 位置和 stdout/stderr 归属仍需先在文档中冻结。
2. `apps/cli/src/main.cpp` 当前只返回 `EXIT_SUCCESS` / `EXIT_FAILURE`，说明 `CliExitDecision` 的最终矩阵尚未在代码里落盘。
3. `apps/cli/src/CliIpcClient.h/.cpp` 已存在 `DaemonClientResponse` 这一 CLI 本地投影对象，并已承载 `request_id`、`trace_id`、`session_id`、`disposition`、`receipt_ref`、`error_ref`、`response_text`、`task_completed` 等字段，说明 CLI 用户面更接近 projection 而不是 raw `AgentResult`。
4. `DaemonClientResponse` 虽然已有 `exit_code_hint` 字段，但当前仓库只有编解码链路，没有稳定赋值链，因此它只能视作 diagnostics hint，不能拿来当最终退出码来源。
5. `tests/unit/access/AccessErrorMappingTest.cpp` 已冻结 access 侧 code/domain 与既有 `1/75/77` 协议映射；这组映射可作为 CLI 的输入事实证据，但不等于 CLI v1 最终用户面契约。

### 2.2 本轮冻结结论

1. CLI v1 `--json` 固定为 CLI projection envelope，固定 `schema_version=cli.output.v1`，并以顶层 `result`、`error`、`warnings` 承载稳定用户面字段。
2. `warnings` 固定放在顶层数组，`error` 固定放在顶层对象；不得把警告塞进 `error`，也不得把错误详情混入 `result.response_text`。
3. `daemon_unavailable` 与 `protocol_error` 是 CLI 本地 projection disposition，不要求 daemon 回传专门的 `error_ref`；脚本应依赖 `disposition + exit_code + error.kind`，而不是 stderr 文本。
4. `CliExitDecision` 采用“先本地 parse、再 transport、再 protocol、最后 daemon/access facts”的顺序；不得直接沿用 `map_access_error().cli_exit_code` 的 `1/75/77`。
5. access error 的 code/domain/retryable/default reason 是输入事实；最终 exit family 由 CLI 单独投影为 `0/2/3/4/5/6/7`。

## 3. v1 `--json` stable envelope

### 3.1 稳定输出规则

1. 进入稳定命令面且 `--json` 生效后，stdout 必须输出且只输出一个 JSON document；stderr 只允许承载附加 human diagnostics，并受 `--quiet` 抑制。
2. `help` 不属于 `--json` contract；`version --json`、`ping --json`、`run/status/cancel --json` 必须共用同一 envelope 主键。
3. `result` 与 `error` 在终态上应互斥；accepted_async 可带 `receipt_ref` 与 `result.task_completed=false` 或 `null`，但不得同时伪造 `error`。
4. `exit_code_hint` 不进入 v1 稳定 JSON 主键；如后续实现需要保留，只能进入 diagnostics/warnings，而不能覆盖最终 `exit_code`。

### 3.2 顶层字段矩阵

| JSON 主键 | 类型 | 语义 | 出现规则 |
|---|---|---|---|
| `schema_version` | string | 固定为 `cli.output.v1` | 总是出现 |
| `command` | string | 稳定公开命令名；兼容 alias 先 canonicalize | 总是出现 |
| `request_id` | string or null | CLI/daemon 对账 ID | daemon 响应或客户端已生成时出现；否则为 `null` |
| `trace_id` | string or null | 跨 CLI/daemon/runtime 的追踪 ID | 与 `request_id` 同步出现或为 `null` |
| `session_id` | string or null | daemon 返回的 session 事实 | 无则 `null` |
| `disposition` | string | `completed`、`accepted_async`、`rejected`、`not_ready`、`daemon_unavailable`、`protocol_error` 之一 | 总是出现 |
| `receipt_ref` | string or null | accepted_async 或 receipt 查询引用 | 无则 `null` |
| `result` | object or null | CLI 稳定结果投影 | 成功或 accepted_async 时出现；其余为 `null` |
| `error` | object or null | CLI 稳定错误投影 | 失败时出现；成功时为 `null` |
| `warnings` | array | 非致命告警列表；元素字段固定为 `code`、`message` | 总是出现，默认空数组 |
| `exit_code` | integer | `CliExitDecision` 的最终 CLI v1 退出码 | 总是出现 |

### 3.3 子对象约束

`result` 子对象只允许以下主键：

| 子键 | 类型 | 语义 |
|---|---|---|
| `response_text` | string or null | 对用户稳定暴露的结果文本或摘要 |
| `task_completed` | bool or null | runtime 是否给出 final completion 事实 |

`error` 子对象只允许以下主键：

| 子键 | 类型 | 语义 |
|---|---|---|
| `kind` | string | `argument`、`transport`、`access_denied`、`business`、`timeout_or_cancel`、`protocol` 之一 |
| `reason` | string or null | CLI 本地失败原因或 access default reason |
| `error_ref` | string or null | daemon/access 返回的稳定错误引用 |
| `access_error_code` | integer or null | access error facts 的数值 code；本地 transport/protocol 失败为 `null` |
| `access_error_domain` | string or null | `validation`、`authentication`、`authorization`、`admission`、`runtime_dispatch`、`publish`、`receipt`、`internal` 之一；本地失败为 `null` |
| `retryable` | bool or null | access/local 失败是否可重试；未知则 `null` |

### 3.4 场景冻结矩阵

| 场景 | `disposition` | `result` | `error` | `exit_code` |
|---|---|---|---|---|
| `ping` 成功 | `completed` | `response_text` 携带 readiness 摘要 | `null` | `0` |
| `run --async` 被接受 | `accepted_async` | `task_completed=false` 或 `null`；并返回 `receipt_ref` | `null` | `0` |
| 认证/授权拒绝 | `rejected` | `null` | `kind=access_denied`，并带 `access_error_domain`/`error_ref` | `4` |
| daemon 不可达 | `daemon_unavailable` | `null` | `kind=transport`，`reason` 来自本地 failure reason | `3` |
| 响应损坏或协议不兼容 | `protocol_error` | `null` | `kind=protocol` | `7` |

## 4. `CliExitDecision` 冻结规则

### 4.1 裁定顺序

1. 先判 CLI parser/flag 错误，再判本地 transport，再判 protocol，再判 daemon/access facts。
2. `Completed` / `AcceptedAsync` 且无 daemon failure facts 时，统一视为成功族并返回 `0`。
3. daemon 侧 validation 失败仍视为参数/请求输入错误，统一投影到 `2`。
4. access error 的 authentication / authorization，以及 `ReceiptOwnerMismatch`，统一投影到 `4`。
5. `not_ready`、timeout、cancel、queue full、bridge unavailable、shutting down 等 retryable / 暂不可完成情形，统一投影到 `6`。
6. 其余 non-retryable daemon-side failure 统一投影到 `5`。

### 4.2 投影矩阵

| 输入事实 | 输出 exit code | 说明 |
|---|---|---|
| CLI parser/flag 校验失败 | `2` | 包括缺参、非法 selector 组合、flag 作用域错误 |
| 本地 IPC 配置非法，或 connect/send/receive 失败，或 peer 提前关闭 | `3` | 属于 daemon unavailable / local transport failure |
| 响应 decode 失败、schema/disposition 不兼容、`accepted_async` 缺少 `receipt_ref` | `7` | 属于 protocol/version contract 破坏 |
| `disposition=completed` 或 `accepted_async`，且无 daemon failure facts | `0` | 包括 replay-hit 这类 success-like 结果 |
| `access_error_domain=validation` | `2` | daemon 侧验证拒绝仍视为用户输入错误 |
| `access_error_domain=authentication`、`authorization`，或 `access_error_code=ReceiptOwnerMismatch` | `4` | access deny / ownership deny 统一归到 deny 族 |
| `disposition=not_ready`，或 `access_error_retryable=true` 且不属于 access deny | `6` | 包括 timeout、cancel、queue full、shutting down、bridge unavailable |
| 其余 daemon-side reject / failure | `5` | 包括 non-retryable runtime/publish/internal/receipt failure |

### 4.3 access fact 复用规则

1. `access::map_access_error()` 与 `describe_access_error()` 只提供 access error 的 code/domain/retryable/default reason 事实与既有协议映射证据；CLI v1 最终退出码必须由 `CliExitDecision` 单独投影。
2. `map_access_error().cli_exit_code` 的 `1/75/77` 只能作为既有 access protocol regression 证据，不得透传为 CLI v1 最终用户面。
3. `exit_code_hint` 只作 diagnostics hint；即使后续 daemon 开始填充，也不得改变 `CliExitDecision` 的最终矩阵。

## 5. Design -> Build 映射

| 设计结论 | 直接解锁任务 | Build 落点 |
|---|---|---|
| `DaemonClientResponse` 是 CLI projection，而不是 raw `AgentResult` 镜像 | CLI-TODO-009、CLI-TODO-010 | `CliIpcClient` 需要补齐 access error code/domain/retryable facts，供 formatter / exit decision 共用 |
| `cli.output.v1` envelope 固定顶层 `result/error/warnings` | CLI-TODO-010、CLI-TODO-012 | `CliOutputFormatter` 输出统一 envelope；contract tests 锁顶层主键和场景 |
| `CliExitDecision` 独立投影 `0/2/3/4/5/6/7` | CLI-TODO-009、CLI-TODO-012 | 新增 `CliExitDecision` 对象；`main()` 退出路径不再使用旧 `0/1` |
| `daemon_unavailable` / `protocol_error` 是 CLI 本地 disposition | CLI-TODO-009、CLI-TODO-010、CLI-TODO-013 | formatter 与 binary 集成门需要覆盖本地 transport/protocol failure 场景 |
| access error facts 是输入证据，不是最终 CLI exit code | CLI-TODO-009、CLI-TODO-012 | `CliExitCodeContractTest` 锁 CLI v1 matrix；`AccessErrorMappingTest` 只继续做 access regression |

## 6. Validation

1. `rg -n "CliJsonOutputContractTest|CliExitCodeContractTest|cli.output.v1|daemon_unavailable|protocol_error|CliExitDecision|0/2/3/4/5/6/7" docs/architecture/DASALL-cli本地控制面详细设计.md docs/todos/cli/DASALL-cli本地控制面专项TODO.md docs/todos/cli/deliverables/CLI-TODO-002-CLI-JSON-envelope与CliExitDecision冻结.md`
2. `ctest --test-dir build-ci -R "AccessErrorMappingTest|CliDaemonOutputFormatterTest" --output-on-failure`

结果摘要：

1. 文档检索应在详设、专项 TODO 与本 deliverable 三处同时看到 `cli.output.v1`、`daemon_unavailable`、`protocol_error`、`CliExitDecision` 与 `0/2/3/4/5/6/7`，证明 CLI-TODO-002 的 contract 已完成多点回链。
2. `AccessErrorMappingTest` 的作用是继续固定 access error code/domain 与既有协议映射，作为 CLI exit projection 的输入证据，而不是最终用户面。
3. `CliDaemonOutputFormatterTest` 的作用是继续固定当前 human formatter 锚点，证明本轮补设计没有脱离现有实现命名与结果语义。

## 7. 完成判定

CLI-TODO-002 完成的判定标准为：

1. `--json` 主键、`result/error/warnings` 放置位置、`daemon_unavailable/protocol_error` 本地 disposition 与 stdout/stderr 归属均可在文档中二值核对。
2. `CliExitDecision` 的顺序与矩阵已明确绑定 local parse/transport/protocol 与 daemon/access facts，不再残留“沿用 access `1/75/77` 还是另起 CLI 投影”的未决项。
3. `CLI-TODO-009/010/012/013` 可以直接引用本文件进入实现或 contract 测试任务，而不需要再猜测 JSON 主键、错误摆放或 exit family。