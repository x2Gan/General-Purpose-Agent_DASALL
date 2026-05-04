# DASALL CLI 本地控制面专项 TODO

最近更新时间：2026-05-04
阶段：Detailed Design -> Special TODO
适用范围：`apps/cli/` 纯客户端入口壳层、CLI 到 daemon 的本地 IPC client 面、CLI 用户面契约、CLI 相关单测/契约测试/集成门禁与交付证据回写
当前结论：CLI 专项已完成两项补设计解阻任务，当前仓库已经完成 CLI 纯客户端依赖方向、CLI-daemon wire contract、shared endpoint 默认值、`CLI-TODO-001` 的命令参数 schema/usage 冻结，以及 `CLI-TODO-002` 的 CLI projection JSON envelope / `CliExitDecision` 冻结。剩余未完成面集中在 `CliCommand` 字段落位、parser/help/version、request builder、formatter/exit decision 实现、CLI 自有测试拓扑与脚本化质量门；后续 Build 任务不再需要猜测公开参数或脚本化 contract。

评估修订要点（2026-05-04）：本次复核后补充冻结 9 个执行口径：`access::map_access_error()` 只作为 access 错误事实与协议映射输入，CLI v1 退出码仍由 `CliExitDecision` 投影为 `0/2/3/4/5/6/7`；`CLI-TODO-002` 已将 `--json` 收敛为 CLI projection envelope，固定顶层 `result/error/warnings` 位置、`schema_version=cli.output.v1`、`daemon_unavailable` / `protocol_error` 本地 disposition 与 stdout/stderr 归属；`exit_code_hint` 降级为 diagnostics hint，不得替代最终 exit 决策；CLI v1 socket 覆盖的稳定用户面命名已由 `CLI-TODO-001` 冻结为 `--socket-path`，`--socket` 不再作为公开 alias；platform peer identity 不阻塞 `apps/cli` 客户端任务，但继续作为 daemon/access 端到端本地控制面的外部依赖跟踪；CLI unit topology 与 contract topology 需要提前落点；新增未决问题处置表、统一验收命令与“持续证据回写 + 最终收口”的 `CLI-TODO-014`。

## 1. 文档头

### 1.1 输入依据

本文档严格基于以下输入生成：

1. `docs/architecture/DASALL_cli本地控制面详细设计.md`
2. `docs/architecture/DASALL_Agent_architecture.md`
3. `docs/architecture/DASALL_Engineering_Blueprint.md`
4. `docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md`
5. `docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md`
6. `docs/plans/DASALL_工程落地实现步骤指引.md`
7. `docs/development/DASALL_工程协作与编码规范.md`
8. 现有交付记录：`docs/todos/access/deliverables`、`docs/todos/daemon/deliverables`
9. 当前代码与测试现状：`apps/cli/src/*`、`apps/cli/CMakeLists.txt`、`tests/unit/access/Cli*`、`tests/integration/access/Cli*`、`tests/integration/access/Daemon*`、`access/include/AccessErrors.h`、`access/src/ProtocolErrorMapper.cpp`

### 1.2 生成原则

1. 不改写已冻结 ADR 结论，不把 CLI 扩张成 Runtime 主控、恢复执行器或第二调度中心。
2. CLI 只规划 `apps/cli/` 自身职责，不重做已在 daemon/access/platform 侧完成的能力。
3. 已完成的基线工作必须显式标记为 `Done` 并回溯到真实交付记录，不能重复规划为未开始任务。
4. 对于详设仍未冻结的参数口径、JSON 包装和版本对比策略，必须先写成“补设计/契约冻结”任务，再允许后续 Build 任务启动。
5. 每个任务都保留代码目标、测试目标、验收命令三件套；若任务是补设计，其代码目标为设计文档与契约落盘，测试目标为评审可核验矩阵或既有 focused 测试扩展。
6. CLI v1 退出码以 CLI 详设 `0/2/3/4/5/6/7` 为最终用户面契约；access error mapping 只提供错误 code/domain 与既有协议映射证据，不直接替代 `CliExitDecision` 的本地退出码投影。
7. CLI v1 socket 覆盖 flag 已冻结为 `--socket-path`；详设 6.4.3 中旧的 `--socket` 已由 `CLI-TODO-001` 回链修订，不再作为公开 alias。
8. platform peer identity 是本地控制面端到端安全闭环的外部依赖，不归入 `apps/cli` 客户端代码目标，但必须在风险、未决问题与执行顺序中持续跟踪。
9. CLI 测试拓扑应尽早独立建账：unit 从 `tests/unit/access` 的历史寄存状态迁出或镜像注册到 `tests/unit/apps/cli`，contract 落到 `tests/contract/access`，避免 JSON/exit code 契约只能停留在文档中。
10. CLI v1 `--json` 已冻结为 CLI projection envelope：stdout 输出唯一 JSON document，`warnings` 为顶层数组、`error` 为顶层对象，`CliExitDecision` 负责最终 `0/2/3/4/5/6/7` 投影。

## 2. 子系统目标与范围

### 2.1 子系统目标

1. 将 CLI 固定为纯客户端入口，不直接持有 `AccessGateway`、Runtime 主状态机、预算裁定、恢复执行权和调度裁定权。
2. 在 `apps/cli/` 内收敛命令解析、请求装配、结果格式化、退出码决策和本地 UX 行为，不把 daemon 私有协议对象上抬到 `contracts/`。
3. 对齐详细设计中 v1 命令面：`run`、`status`、`cancel`、`ping`、`diag`、`help`、`version`；默认人类可读，同时提供稳定 `--json` 输出和可脚本消费的退出码。
4. 复用已完成的 daemon/access/platform 基线，补齐 CLI 自身缺失的命令参数契约、JSON/exit code 契约、help/version 行为与测试门禁。
5. 形成可评审、可回归、可回写的 CLI 专项 TODO，而不是继续把 CLI 任务散落在 access/daemon 专项文档里。

### 2.2 范围边界

纳入本专项 TODO 的对象：

1. `apps/cli/src/main.cpp`、`CliCommandParser.*`、`CliIpcClient.*`、`CliOutputFormatter.*`。
2. 详细设计建议但当前尚未落盘的 `CliRequestBuilder` 与 `CliExitDecision` 对象收敛。
3. CLI 相关测试拓扑：现有 `tests/unit/access/Cli*`、现有 `tests/integration/access/Cli*` / `Daemon*` 中由 CLI surface 驱动的用例，以及详设建议的 `tests/contract/access/CliJsonOutputContractTest.cpp`、`CliExitCodeContractTest.cpp`。
4. CLI 命令参数矩阵、JSON 输出 envelope、退出码矩阵、stdout/stderr 分离约束、默认 endpoint 与显式覆盖面。
5. `docs/todos/cli/DASALL_cli本地控制面专项TODO.md` 与后续 worklog/交付证据回写。

不纳入本专项 TODO 的对象：

1. daemon listener、Access admission pipeline、peer identity、RuntimeBridge、receipt registry、UDS socket mode/stale cleanup 的服务端实现。
2. `runtime/` 内部状态机、恢复、调度和多 Agent 编排。
3. 远程 daemon、公共 HTTP/gRPC 控制 API、完整 streaming attach、复杂 REPL。
4. `contracts/` 新增 CLI 私有对象；CLI request/response 仍应以 access daemon adapter module-local 类型承载。

外部依赖但不归入本专项实现的对象：

1. `platform/include/IIPC.h` 的 peer identity 补口与 `platform/src/linux/UnixIpcProvider.cpp` 的 `SO_PEERCRED` 等价实现；它们影响 daemon/access 对本地 trusted、diag 与 auth deny 场景的端到端验收，但不阻塞 `apps/cli` parser、formatter、request builder 与 exit decision 的客户端任务。
2. daemon/access 侧对 `LocalPeerUidFact`、AccessPolicyGate、diag policy 与 receipt ownership 的安全闭环；CLI 专项只回链这些依赖，不把实现目标搬入 `apps/cli`。

### 2.3 当前状态摘要

| 维度 | 当前状态 | 结论 |
|---|---|---|
| CLI 纯客户端依赖方向 | 已完成 | `apps/cli/CMakeLists.txt` 仅链接 `dasall_access`、`dasall_contracts`、`dasall_infra`、`dasall_platform`，未直链 runtime |
| CLI-daemon wire contract | 已完成 | `CliIpcClient` 已执行 `connect/send/receive/close` 往返并解析 `UdsResponseFrame` |
| 默认 endpoint 与 `--socket-path` | 已完成 | shared `kDefaultDaemonSocketPath` 已接入，`CliDaemonSocketPathIntegrationTest` 已覆盖默认/显式覆盖；`CLI-TODO-001` 已将详设中的 `--socket` 漂移收敛为仅保留 `--socket-path` 的公开命名 |
| 命令参数口径 | 已补设计冻结 | `CLI-TODO-001` 已冻结 `run/status/cancel/help/version` 的 usage skeleton、`--socket-path` 稳定命名、selector 规则与 version local-only 边界；Build 侧仍待 parser/request builder 对齐实现 |
| JSON 输出与退出码矩阵 | 已补设计冻结 | `CLI-TODO-002` 已冻结 `cli.output.v1` envelope、stdout/stderr 归属、`DaemonClientResponse` CLI projection 边界，以及 access error fact -> CLI `0/2/3/4/5/6/7` 投影矩阵；Build 侧仍待 formatter / exit decision / contract tests 落盘 |
| CLI 自有测试拓扑 | 未完成 | CLI 单测当前仍挂在 `tests/unit/access`；契约测试目录与用例未落盘 |
| platform peer identity 外部依赖 | 外部未闭环 | 不阻塞 CLI 客户端任务，但阻塞 daemon/access 端到端 local trusted、auth deny 与 diag 授权场景的最终闭环 |

## 3. 输入依据与约束清单

### 3.1 约束清单

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| CLI-TC001 | 工程蓝图 3.2 | Must | `apps/cli/` 只负责入口壳层、stdin/stdout 绑定和 adapter 装配 | 只能规划 parser/builder/client/formatter/main，不得把 access/runtime 实现搬进 CLI |
| CLI-TC002 | 架构文档 5.6、5.7 | Must | CLI 属于 Access Channel，协议适配、认证鉴权、归一化和结果发布 owner 在 access | CLI 只能构造客户端意图，不能在本地做 Admission/Auth/Policy 裁定 |
| CLI-TC003 | ADR-008 | Must-Not | Runtime 全局主控权只在 `AgentOrchestrator` | CLI 不得形成第二调度中心，不得自行决定任务生命周期 |
| CLI-TC004 | ADR-007 | Must-Not | 失败语义解释在 cognition，恢复准入和执行在 runtime | CLI 不能发明本地恢复策略、自动重试或补偿执行 |
| CLI-TC005 | CLI 详设 1.3、6.3.1 | Must | `UdsRequestFrame`、`UdsResponseFrame`、`LocalPeerUidFact` 为 module-local / module public；不得进入 `contracts/` | CLI 任务只能复用 access daemon codec 与 objects，不新增 contracts 字段 |
| CLI-TC006 | CLI 详设 6.4.2、6.4.3；当前实现 | Must | 默认人类可读；`--json`、`--timeout-ms`、`--async`、`--request-id`、`--session`、`--trace-id`、`--quiet`、`--no-input` 属于 v1 flags 面；socket 覆盖稳定命名冻结为 `--socket-path` | 解析器和输出层需要按 flags 表补全，且 `CLI-TODO-001` 必须回链修订详设中 `--socket` / `--socket-path` 的命名漂移 |
| CLI-TC007 | CLI 详设 6.4.4 | Must | CLI 退出码必须区分 `0/2/3/4/5/6/7` | `main.cpp` 当前仅 `EXIT_SUCCESS/EXIT_FAILURE` 不满足最终契约 |
| CLI-TC008 | CLI 详设 1.6；`CLI-TODO-001` deliverable | Cleared | `run` 的业务参数形态、`status`/`cancel` 的精确参数名、help/version usage skeleton 已由 `CLI-TODO-001` 冻结；剩余工作转为 parser/request builder 实现对齐 | `CLI-TODO-006/007/008` 可直接按冻结 contract 进入 Build，不再需要猜测用户面参数名 |
| CLI-TC009 | CLI 详设 6.4.2、6.4.4、12；`CLI-TODO-002` deliverable | Cleared | `--json` 已冻结为 CLI projection envelope；`CliExitDecision` 已绑定 local parse/transport/protocol/access facts 到 `0/2/3/4/5/6/7` 的投影顺序 | `CLI-TODO-009/010/012/013` 不再需要猜测 JSON 主键、stdout/stderr 归属或 exit family |
| CLI-TC010 | 工程规范 3.2、3.3、4.1 | Must | 公共接口放 include，测试目录与产品目录应保持镜像 | CLI 单测最终应从 `tests/unit/access` 收敛到明确 CLI 测试拓扑 |
| CLI-TC011 | 工程规范 3.7 | Must | 新增公共接口时同步增加 unit 或 contract 测试 | `CliRequestBuilder`、`CliExitDecision`、JSON contract 不能只写实现不补测试 |
| CLI-TC012 | 现有交付 `ACC-TODO-025/038` | Evidence | CLI 纯客户端组合根和 UDS endpoint 基线已完成 | 专项 TODO 不能再把纯客户端依赖方向写成未开始任务 |
| CLI-TC013 | 现有交付 `DMD-TODO-031/036` | Evidence | CLI 已具备 request/response roundtrip、default endpoint shared constant、`--socket-path` 覆盖 | 剩余工作重点转向用户面契约与测试门，而不是继续补 transport smoke |
| CLI-TC014 | `access/include/AccessErrors.h`、`AccessErrorMappingTest.cpp`、CLI 详设 6.4.4 | Evidence / Must | access 已冻结错误 code/domain 与既有协议映射；CLI v1 用户面退出码仍以详设 `0/2/3/4/5/6/7` 为准 | `CliExitDecision` 应复用 access error code/domain 作为输入事实，但不能把 `map_access_error().cli_exit_code` 直接当作 CLI v1 最终退出码 |
| CLI-TC015 | CLI 详设 6.3.3、6.9、11、12 未决问题 1 | External Dependency | platform peer identity 补口是 daemon/access 本地 trusted 与授权拒绝场景的安全前提 | 本专项需要在风险、未决问题和集成门禁中回链该依赖；`apps/cli` 客户端任务不得越权实现平台能力 |
| CLI-TC016 | 工程规范 4.1；当前 tests 现状 | Must | CLI unit/contract 测试必须拥有可发现的自有拓扑 | `CLI-TODO-011` 应提前建立 `tests/unit/apps/cli` 与 `tests/contract/access` 接线，后续实现任务只填充用例，不再临时挂靠 `tests/unit/access` |

### 3.2 当前代码与测试证据

| 证据对象 | 当前状态 | 对 CLI TODO 的含义 |
|---|---|---|
| `apps/cli/src/CliIpcClient.cpp` | 已能编码 request frame、接收响应并解析 `DaemonClientResponse` | transport baseline 已就绪；`CLI-TODO-002` 已冻结 `DaemonClientResponse` 需要补齐 access error code/domain/retryable 这组 CLI projection facts，供 exit decision / JSON envelope 复用 |
| `apps/cli/src/CliCommandParser.h/.cpp` | 支持 `ping/readiness/run/submit/status/cancel/diag` 和 `--socket-path`；不支持 `help/version` 与详设 flags 表 | parser 是可直接落到函数级的实现锚点；`CLI-TODO-001` 已冻结公开参数 contract，后续只需把现有最小/兼容 surface 对齐到稳定 schema |
| `apps/cli/src/CliOutputFormatter.h/.cpp` | 仅输出人类可读字符串；不区分 stdout/stderr 责任边界下的 JSON 投影 | `CLI-TODO-002` 已冻结 JSON 主键、`error/warnings` 摆放和 stdout/stderr 归属；剩余工作是 human/json 双格式实现与 contract tests |
| `apps/cli/src/main.cpp` | 已完成命令分发，但只返回成功/失败二值退出 | `CLI-TODO-002` 已冻结 `CliExitDecision` 的顺序与矩阵；剩余工作是把 `0/2/3/4/5/6/7` 决策真正落到主程序退出路径 |
| `tests/unit/access/CliDaemonCommandParserTest.cpp` | 已覆盖最小命令面和 `--socket-path` 解析 | 可复用为 parser/help/version/flags 扩展测试入口 |
| `tests/unit/access/CliIpcClientTest.cpp`、`CliIpcClientResponseTest.cpp`、`CliIpcClientUnavailableTest.cpp` | 已覆盖 request encode、response parse、daemon unavailable fail-closed | 可复用为 request shaping 与 local transport failure 断言入口 |
| `tests/unit/access/CliDaemonOutputFormatterTest.cpp` | 已覆盖最小 disposition 文本输出 | 可复用为 human mode 扩展入口，但当前未覆盖 `--json` |
| `tests/integration/access/CliDaemonSocketPathIntegrationTest.cpp` | 已覆盖默认 socket path 与显式 `--socket-path` 正向连通 | endpoint 和 deployment smoke 基线已具备 |
| `tests/integration/access/DaemonBinaryUnarySmokeTest.cpp`、`DaemonReceiptFlowIntegrationTest.cpp` | 已覆盖 built CLI/built daemon unary 与 async receipt flow | 可复用为 CLI `--async`、`request_id`、`trace_id`、stdout/stderr 集成门 |
| `tests/contract/` | 当前没有 `access/` 下的 CLI contract 用例，也没有 `tests/contract/access/CMakeLists.txt` | `CliJsonOutputContractTest`、`CliExitCodeContractTest` 与 contract topology 都是实打实缺口，需由 `CLI-TODO-011/012` 分层补齐 |

## 4. 粒度可行性评估

### 4.1 总体结论

结论：当前 CLI 专项可直接生成 `L2/L3` 混合 TODO，但不能整体按纯 `L3` 推进。

当前最细可安全落盘粒度：

1. `L3`：`CliCommandParser::parse()`、`CliCommandParser::usage_string()`、`CliIpcClient::send_request()`、`CliIpcClient::ping_daemon()` / `submit()` / `query_status()` / `cancel()` / `read_readiness()`、`CliOutputFormatter::format_*()`、`main()` 的命令分发与退出码路径。
2. `L2`：`CliCommand` 扩展选项模型、`CliRequestBuilder`、`CliExitDecision`、CLI unit/contract 测试拓扑、`--json` envelope、help/version 行为。
3. `L1`：version 与 daemon build metadata 对比策略、最终 man page 文案、`--json` 是否镜像 `AgentResult` 或投影为 CLI envelope。
4. `L0`：无。CLI 边界本身清晰，不存在“连边界都不稳定”的情况。

判断依据：

1. 详设已给出核心子组件、核心对象字段、主流程、异常流程、目录与测试出口，满足 L2/L3 拆分多数条件。
2. 当前代码已经存在 `CliCommandParser`、`CliIpcClient`、`CliOutputFormatter` 和相关 focused tests，可以直接从现有实现面继续拆分，而不是从零发明类名。
3. 当前已不存在剩余的用户面补设计缺口：参数 schema、JSON envelope、stdout/stderr 归属与 exit family 均已冻结，后续 Build 任务不再需要重新讨论公开 contract。
4. 因此本专项可以直接进入 topology + Build 阶段：`CLI-TODO-006/007/008` 可按已冻结命令/输出 contract 推进，`CLI-TODO-009/010/012` 则只剩实现与 contract topology 问题。

### 4.2 粒度可行性评估表

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| 命令族与通用交互方式 | 1.6、6.4.1 | L2 | `run/status/cancel/ping/diag/help/version` 已冻结到命令级；`CLI-TODO-001` 已补齐参数表与 usage skeleton | 剩余缺口不再是命令参数，而是 JSON/exit code 契约 | 直接进入 `CLI-TODO-006/007/008` 的 parser/request builder Build |
| `CliCommand` / `CliCommandParser` | 6.1、6.4.2、6.4.3 | L3 | 现有类与函数已存在，focused parser tests 已存在 | 详设 flags 表尚未落到字段级 | `CLI-TODO-006/007` 直接落到类型与函数级 |
| `CliIpcClient` | 6.1、6.3.2、6.5 | L3 | request/response roundtrip 已存在，response parse tests 已存在 | request shaping 仍在匿名 helper，未承载完整 flags 字段 | 在 `CLI-TODO-008` 中抽离 `CliRequestBuilder` 并复用现有 tests |
| `CliOutputFormatter` | 6.1、6.4.2、6.4.4 | L2 | human 输出与 disposition 文本已存在；`CLI-TODO-002` 已冻结 JSON 主键、`error/warnings` 位置与 stdout/stderr contract | 剩余缺口是 human/json 双格式实现与 contract tests 落盘 | 直接进入 `CLI-TODO-009/010/012` |
| `CliExitDecision` | 6.3.1、6.4.4 | L2 | `CLI-TODO-002` 已冻结 local parse/transport/protocol/access facts 到 `0/2/3/4/5/6/7` 的投影顺序；access 已有错误 code/domain 证据 | 缺失的是对象与 `main()` 落盘，不再是 contract 选择 | 直接进入 `CLI-TODO-009` |
| help/version | 1.6、6.4.1、6.4.5 | L2 | 命令名、usage skeleton、`version` local-only 策略与 help 责任边界已由 `CLI-TODO-001` 冻结 | 剩余缺口是 parser/help/version 实现落盘 | 进入 `CLI-TODO-007` 实现，不再需要额外补设计 |
| CLI contract tests | 7、9、12 未决问题 3 | L2 | 详设已明确 `CliJsonOutputContractTest`、`CliExitCodeContractTest`，`CLI-TODO-002` 已冻结场景与主键 | 目录、CMake、golden schema 未落盘 | `CLI-TODO-011/012` 直接按已冻结场景落盘 |
| CLI unit topology | 工程规范 4.1；当前 tests 现状 | L2 | 现有 CLI unit tests 可复用 | `tests/unit/apps/cli` 当前不存在 | `CLI-TODO-011` 负责 topology 收敛与 discoverability |

## 5. Design -> TODO 映射表

### 5.1 映射总表

| Design 结论 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| CLI 命令族已冻结，但参数口径不是最终 man page | 1.6、6.4.1、6.4.2、6.4.3 | 补设计 / 接口冻结 | `CLI-TODO-001` | 先冻结 `run/status/cancel/help/version` 参数矩阵，并回链修订 `--socket` / `--socket-path` 命名漂移，再动 parser/help/version 代码 |
| `--json`、退出码、stdout/stderr 分离是脚本化稳定接口 | 6.4.2、6.4.4、7、9、12 未决问题 3 | 补设计 / 契约冻结 | `CLI-TODO-002` | 已完成：JSON envelope 与 exit decision 口径已冻结，明确 access error code/domain/retryable 是输入事实、CLI `0/2/3/4/5/6/7` 是最终用户面投影 |
| CLI 纯客户端依赖方向与 UDS client 骨架已经存在 | 1.2、6.2、7 | 现状继承 | `CLI-TODO-003` | 复用 access 交付，不重复规划 runtime 直连或本地主链 |
| CLI-daemon roundtrip 已完成 request/response parse | 6.3.2、6.5、9 | 现状继承 | `CLI-TODO-004` | 复用 DMD-TODO-031 基线，把后续工作聚焦到用户面契约 |
| shared endpoint default 与 `--socket-path` 覆盖已完成 | 6.4.3、6.7.1、6.10、7 | 配置 / 入口契约 | `CLI-TODO-005` | 复用 DMD-TODO-036，不再把 socket 覆盖面写成新任务；只在 `CLI-TODO-001` 中冻结命名兼容策略 |
| flags 表需要落到字段和解析规则 | 6.4.3、6.7.3 | 数据结构 / 解析 | `CLI-TODO-006`、`CLI-TODO-007` | 先扩展 `CliCommand`，再细化 parser/help/version |
| request frame 应由 CLI request builder 统一装配 | 6.1、6.3.1、6.5 | 桥接 / 初始化 | `CLI-TODO-008` | 消除 `CliIpcClient` 内匿名 helper，对齐详设建议文件落点 |
| `CliExitDecision` 与 `CliOutputFormatter` 是 CLI 本地 contract owner | 6.3.1、6.4.4 | 错误处理 / 输出契约 | `CLI-TODO-009`、`CLI-TODO-010` | 把退出码和 human/json 输出从二值逻辑升级为稳定 contract |
| CLI 需要 own unit topology 与 contract tests | 6.10、9 | 测试与门禁 | `CLI-TODO-011`、`CLI-TODO-012` | 先建立 `tests/unit/apps/cli` 与 `tests/contract/access` 可发现拓扑，再把 JSON/exit code 契约测试落入该拓扑 |
| CLI binary 需要覆盖 async、显式 ID、stdout/stderr 脚本化行为 | 6.4.3、6.5、9 | 集成门禁 | `CLI-TODO-013` | 复用已有 integration smoke，不重新发明新的 daemon fixture |
| 专项 TODO 需要 gate 与证据回写 | 工程规范、专项 TODO 基线 | 文档 / 证据 | `CLI-TODO-014` | 每个任务完成时持续回写 focused 证据，最终由 `CLI-TODO-014` 复验 Gate 并收口 worklog / blocker / risk |

### 5.2 覆盖性检查

| 类型 | 是否覆盖 | 任务 ID |
|---|---|---|
| 接口定义类任务 | 是 | `CLI-TODO-001`、`CLI-TODO-002`、`CLI-TODO-007` |
| 数据结构定义类任务 | 是 | `CLI-TODO-006`、`CLI-TODO-008`、`CLI-TODO-009` |
| 生命周期与初始化类任务 | 是 | `CLI-TODO-008`、`CLI-TODO-009` |
| 适配器 / 桥接类任务 | 是 | `CLI-TODO-003`、`CLI-TODO-004`、`CLI-TODO-008` |
| 异常与错误处理类任务 | 是 | `CLI-TODO-002`、`CLI-TODO-009`、`CLI-TODO-012`、`CLI-TODO-013` |
| 配置与 Profile 裁剪类任务 | 是 | `CLI-TODO-005`、`CLI-TODO-006` |
| 测试与门禁类任务 | 是 | `CLI-TODO-011`、`CLI-TODO-012`、`CLI-TODO-013` |
| 文档 / 交付证据回写类任务 | 是 | `CLI-TODO-014` |

## 6. 原子任务清单

### 6.1 补设计 / 契约冻结任务

| Task ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CLI-TODO-001 | Done | 补齐 run/status/cancel/help/version 参数 schema 与 usage 文案 | CLI 详设 1.6、6.4.1、6.4.2、6.4.3；当前 `CliCommandParser` 仅有最小 positional 面；详设 `--socket` 与实现 `--socket-path` 命名漂移 | 1.6 参数 schema 冻结；6.4.2 参数约定；6.4.3 flags 表与 socket flag 命名 | L2 | `docs/architecture/DASALL_cli本地控制面详细设计.md`；`docs/todos/cli/DASALL_cli本地控制面专项TODO.md`；`docs/todos/cli/deliverables/CLI-TODO-001-CLI命令schema与usage文案冻结.md` | `CliCommand`；`CliCommandParser::parse()`；`CliCommandParser::usage_string()`；`--socket-path` 命名与兼容 surface policy | 复用 `CliDaemonCommandParserTest` 作为当前 parser 锚点；Build 阶段继续扩展 flags/帮助面正反例 | `rg -n "run|status|cancel|help|version|--socket-path|--socket|submit|readiness" docs/architecture/DASALL_cli本地控制面详细设计.md docs/todos/cli/DASALL_cli本地控制面专项TODO.md docs/todos/cli/deliverables/CLI-TODO-001-CLI命令schema与usage文案冻结.md && ctest --test-dir build-ci -R "CliDaemonCommandParserTest" --output-on-failure` | 无 | 已解阻 | `docs/todos/cli/deliverables/CLI-TODO-001-CLI命令schema与usage文案冻结.md`；更新后的 CLI 详设与专项 TODO | 仅当 `run/status/cancel/help/version` 的参数名、selector 规则、`--socket-path` 稳定命名、compat surface 边界和 version local-only 策略都能被二值核对时完成 |
| CLI-TODO-002 | Done | 补齐 `--json` envelope 与 `CliExitDecision` 契约 | CLI 详设 6.4.2、6.4.4、7、9、12 未决问题 3；`AccessErrorMappingTest` 已冻结 access 错误 code/domain 与既有协议映射 | 6.4.2 JSON envelope；6.4.4 退出码映射；7 Design -> Build；9 Contract | L2 | `docs/architecture/DASALL_cli本地控制面详细设计.md`；`docs/todos/cli/DASALL_cli本地控制面专项TODO.md`；`docs/todos/cli/deliverables/CLI-TODO-002-CLI-JSON-envelope与CliExitDecision冻结.md` | `CliOutputFormatter`；`DaemonClientResponse`；`CliExitDecision`；access error code/domain/retryable -> CLI local exit projection | 复用 `AccessErrorMappingTest`、`CliDaemonOutputFormatterTest` 作为当前实现锚点；Build 阶段继续新增 `CliJsonOutputContractTest`、`CliExitCodeContractTest` | `rg -n "CliJsonOutputContractTest|CliExitCodeContractTest|cli.output.v1|daemon_unavailable|protocol_error|CliExitDecision|0/2/3/4/5/6/7" docs/architecture/DASALL_cli本地控制面详细设计.md docs/todos/cli/DASALL_cli本地控制面专项TODO.md docs/todos/cli/deliverables/CLI-TODO-002-CLI-JSON-envelope与CliExitDecision冻结.md && ctest --test-dir build-ci -R "AccessErrorMappingTest|CliDaemonOutputFormatterTest" --output-on-failure` | 无 | 已解阻 | `docs/todos/cli/deliverables/CLI-TODO-002-CLI-JSON-envelope与CliExitDecision冻结.md`；更新后的 CLI 详设与专项 TODO | 仅当 `--json` 主键、`warnings/error` 放置位置、`daemon_unavailable/protocol_error` 本地 disposition、stdout/stderr 归属，以及 transport/protocol/access facts -> CLI v1 退出码的映射都可直接评审时完成 |

### 6.2 已完成基线任务

| Task ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CLI-TODO-003 | Done | 校正 CLI 纯客户端依赖方向与组合根 | 工程蓝图 3.2；CLI 详设 1.2、6.2；现有交付 `ACC-TODO-025`、`ACC-TODO-038` | 6.2 依赖方向冻结；7 `CLI 纯客户端化` | L3 | `apps/cli/CMakeLists.txt`；`apps/cli/src/main.cpp`；`apps/cli/src/CliIpcClient.*` | `dasall_cli` target；`main()`；`CliIpcClient` | `CliIpcClientTest`；`CliIpcClientUnavailableTest` | `ctest --test-dir build-ci -R "CliIpcClientTest|CliIpcClientUnavailableTest" --output-on-failure` | 无 | 无 | 已解阻 | `docs/todos/access/deliverables/ACC-TODO-025-CliIpcClient与cli纯客户端组合根收敛.md`；`docs/todos/access/deliverables/ACC-TODO-038-CLI依赖方向纠正与UDS路径收敛.md`；相关代码文件 | 仅当 `apps/cli` 不再直链 runtime，CLI 只经 `IIPC/UDS -> daemon/access` 发起请求且 focused tests 通过时完成 |
| CLI-TODO-004 | Done | 收敛 CLI-daemon wire contract 与响应解析 | CLI 详设 6.3.2、6.5、9；现有交付 `DMD-TODO-031` | CLI -> daemon 本地控制面；响应解析契约 | L3 | `apps/cli/src/CliIpcClient.*`；`apps/cli/src/CliCommandParser.*`；`apps/cli/src/CliOutputFormatter.*`；`tests/integration/access/DaemonPingIntegrationTest.cpp` | `DaemonClientResponse`；`CliIpcClient::send_request()`；`CliCommandParser::parse()`；`CliOutputFormatter::format_*()` | `CliIpcClientTest`；`CliIpcClientResponseTest`；`CliIpcClientUnavailableTest`；`CliDaemonCommandParserTest`；`CliDaemonOutputFormatterTest`；`DaemonPingIntegrationTest` | `ctest --test-dir build-ci -R "CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonCommandParserTest|CliDaemonOutputFormatterTest|DaemonPingIntegrationTest" --output-on-failure` | `CLI-TODO-003` | 无 | 已解阻 | `docs/todos/daemon/deliverables/DMD-TODO-031-CLI-daemon-wire-contract收敛.md`；相关代码与测试文件 | 仅当 CLI 能区分 `completed/accepted_async/rejected/not_ready`，并在真实 ping roundtrip 中消费响应而非只断言 `send()` 成功时完成 |
| CLI-TODO-005 | Done | 收敛 shared endpoint 默认值与 `--socket-path` 覆盖 | CLI 详设 6.7.1、6.10；现有交付 `DMD-TODO-036` | socket_path；CLI 入口契约 | L3 | `access/include/daemon/DaemonEndpointDefaults.h`；`apps/cli/src/main.cpp`；`apps/cli/src/CliCommandParser.*`；`tests/integration/access/CliDaemonSocketPathIntegrationTest.cpp` | `kDefaultDaemonSocketPath`；`CliCommand.socket_path`；`CliCommandParser::parse()`；`main()` endpoint resolution | `CliDaemonCommandParserTest`；`CliDaemonSocketPathIntegrationTest`；`DaemonPingIntegrationTest` | `ctest --test-dir build-ci -R "CliDaemonCommandParserTest|CliDaemonSocketPathIntegrationTest|DaemonPingIntegrationTest" --output-on-failure` | `CLI-TODO-004` | 无 | 已解阻 | `docs/todos/daemon/deliverables/DMD-TODO-036-daemon-cli-endpoint统一与覆盖收敛.md`；shared endpoint/default 代码与部署文档 | 仅当 CLI 与 daemon 默认 socket path 来自同一常量，且默认值与显式覆盖两条路径都可正向连通 daemon 时完成 |

### 6.3 剩余实现任务

| Task ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CLI-TODO-006 | NotStarted | 定义 `CliCommand` 扩展选项模型 | CLI 详设 6.4.3、6.7.3；当前 `CliCommand` 仅承载 socket/payload/receipt/token/actor/diag | 6.4.3 通用 flags；6.7.3 CLI 本地 UX 配置 | L2 | `apps/cli/src/CliCommandParser.h` | `CliCommand`：`output_mode`、`timeout_ms`、`async_preference`、`request_id`、`session_hint`、`trace_id`、`quiet`、`no_input`、selector 类型与 ownership token | 扩展 `CliDaemonCommandParserTest`，验证字段捕获、duplicate/illegal-scope reject | `ctest --test-dir build-ci -R "CliDaemonCommandParserTest" --output-on-failure` | `CLI-TODO-001` | 无 | `CLI-TODO-001` 已冻结 flags 作用域与字段含义，可直接进入实现 | 更新后的 `CliCommandParser.h` 与专项 TODO 对应节 | 仅当详设 flags 表中的每个 v1 选项都映射到 `CliCommand` 稳定字段，且无重复/悬空字段时完成 |
| CLI-TODO-007 | NotStarted | 实现 `CliCommandParser` 的 flags、help、version 校验 | CLI 详设 1.6、6.4.1、6.4.2、6.4.3；当前 parser 无 `help/version`，也不校验 flags 作用域 | 命令面与 usage；冻结 contract -> parser 口径 | L3 | `apps/cli/src/CliCommandParser.cpp`；`apps/cli/src/main.cpp` | `CliCommandParser::parse()`；`CliCommandParser::usage_string()`；`main()` 的 `help/version` dispatch | 扩展 `CliDaemonCommandParserTest` 覆盖 `help`、`version` 与 `--async`/`--json`/`--quiet` 作用域错误 | `ctest --test-dir build-ci -R "CliDaemonCommandParserTest" --output-on-failure` | `CLI-TODO-001`、`CLI-TODO-006` | 无 | 参数矩阵与 help/version 输出责任边界已冻结；待 `CLI-TODO-006` 完成字段落点后实施 | 更新后的 parser/main；专项 TODO 对应节 | 仅当 `help`、`version`、各命令 flags 作用域和非法组合都可由 parser 二值判定时完成 |
| CLI-TODO-008 | NotStarted | 实现 `CliRequestBuilder` 与 request frame shaping | CLI 详设 6.1、6.3.1、6.4.3、6.5；当前 request shaping 仍在 `CliIpcClient.cpp` 匿名 helper | `CliRequestBuilder`；`UdsRequestFrame` 字段装配 | L2 | `apps/cli/src/CliRequestBuilder.h`；`apps/cli/src/CliRequestBuilder.cpp`；`apps/cli/CMakeLists.txt`；`apps/cli/src/CliIpcClient.cpp` | `CliRequestBuilder`；`UdsRequestFrame`；`CliIpcClient::send_request()` 调用面 | 扩展 `CliIpcClientTest`、`CliIpcClientResponseTest` 断言 `request_id`、`trace_id`、`session_hint`、`async_preference`、`deadline_ms`、`output_mode` | `ctest --test-dir build-ci -R "CliIpcClientTest|CliIpcClientResponseTest" --output-on-failure` | `CLI-TODO-001`、`CLI-TODO-006` | 无 | 参数矩阵已冻结；待 `CliCommand` 完整字段落位后实施 builder 对齐 | 新增 builder 文件；更新后的 client 测试 | 仅当 request frame 的 v1 字段都有单一 owner，且 `CliIpcClient` 不再自行拼装业务级 request 参数时完成 |
| CLI-TODO-009 | NotStarted | 实现 `CliExitDecision` 与本地退出码决策 | CLI 详设 6.3.1、6.4.4；`access::map_access_error()` 已冻结 access 错误 code/domain 与既有协议映射 | `CliExitDecision`；CLI v1 `0/2/3/4/5/6/7` | L2 | `apps/cli/src/CliExitDecision.h`；`apps/cli/src/CliExitDecision.cpp`；`apps/cli/src/main.cpp`；`apps/cli/CMakeLists.txt` | `CliExitDecision`；`main()` 退出路径；`DaemonClientResponse` + local failure path + access error code/domain/retryable 到 exit decision 的投影 | 复用 `AccessErrorMappingTest`，新增/落盘 `CliExitCodeContractTest` | `ctest --test-dir build-ci -R "AccessErrorMappingTest|CliExitCodeContractTest" --output-on-failure` | `CLI-TODO-002`、`CLI-TODO-011` | 无 | `CLI-TODO-002` 已冻结 CLI local path 到 exit code 的最终矩阵，`CLI-TODO-011` 已提供稳定 contract topology；待 exit decision 与 main 真实实现落盘 | 更新后的 exit decision/main/formatter；contract 测试或其注册面 | 仅当参数错误=2、daemon 不可达=3、认证授权拒绝=4、业务失败=5、超时/取消或暂不可完成=6、协议错误=7、成功/accepted=0 均可二值断言，且不直接沿用 access 既有 `1/75/77` 映射时完成 |
| CLI-TODO-010 | NotStarted | 实现 `CliOutputFormatter` 的 human/JSON 双格式输出 | CLI 详设 6.1、6.4.2、6.4.3、6.4.4、9；当前 formatter 仅有 human 文本 | human mode / `--json` stable envelope / stdout-stderr split | L2 | `apps/cli/src/CliOutputFormatter.h`；`apps/cli/src/CliOutputFormatter.cpp`；`apps/cli/src/main.cpp` | `CliOutputFormatter::format_*()`；`DaemonClientResponse`；`CliExitDecision` | 扩展 `CliDaemonOutputFormatterTest`，并落盘 `CliJsonOutputContractTest` | `ctest --test-dir build-ci -R "CliDaemonOutputFormatterTest|CliJsonOutputContractTest" --output-on-failure` | `CLI-TODO-002`、`CLI-TODO-009` | 无 | JSON 主键、warnings/error 摆放、human/json 切换边界已冻结；待 `CLI-TODO-009` 落 exit decision 后实施 | 更新后的 formatter/main；contract 测试或其注册面 | 仅当默认 human 输出保留、`--json` 输出稳定 envelope、成功路径 stdout 不混入错误提示且 stderr 不承载业务结果时完成 |

### 6.4 测试、门禁与证据任务

| Task ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CLI-TODO-011 | Done | 提前注册 CLI unit 与 contract 拓扑 discoverability | 工程规范 4.1；当前 CLI 单测仍位于 `tests/unit/access`；当前 `tests/contract/access` 不存在 | CLI own unit topology；CLI contract topology | L2 | `tests/unit/apps/cli/CMakeLists.txt`；`tests/unit/CMakeLists.txt`；`tests/contract/access/CMakeLists.txt`；`tests/contract/CMakeLists.txt` | CLI 单测聚合列表；contract 测试接入点；过渡期 mirror registration 策略 | `ctest -N` 发现现有 CLI unit tests，并预留 `CliJsonOutputContractTest`、`CliExitCodeContractTest` 的 contract 接入点 | `ctest --test-dir build/vscode-linux-ninja -N | rg "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|CliJsonOutputContractTest|CliExitCodeContractTest"` | `CLI-TODO-001`、`CLI-TODO-002` | 已解阻 | 命令/JSON/exit contract 命名已冻结，可稳定命名测试目标；CLI unit 已切入自有 topology，contract/access 已提供 fail-closed reserved entrypoint | `docs/todos/cli/deliverables/CLI-TODO-011-CLI测试topology与discoverability接线.md`；新增/更新的 unit 与 contract CMake 拓扑 | 仅当 CLI unit tests 可由 CLI 自有拓扑发现，contract/access 目录和 CMake 接线存在，且后续契约测试不再需要临时挂靠 `tests/unit/access` 时完成 |
| CLI-TODO-012 | NotStarted | 新增 CLI JSON 与 exit code contract tests | CLI 详设 7、9、12 未决问题 3；`CLI-TODO-011` 提供 contract topology | `CliJsonOutputContractTest`；`CliExitCodeContractTest` | L2 | `tests/contract/access/CliJsonOutputContractTest.cpp`；`tests/contract/access/CliExitCodeContractTest.cpp` | `CliJsonOutputContractTest`；`CliExitCodeContractTest` | `ctest --test-dir build-ci -R "CliJsonOutputContractTest|CliExitCodeContractTest" --output-on-failure` | `CLI-TODO-002`、`CLI-TODO-009`、`CLI-TODO-010`、`CLI-TODO-011` | 无 | JSON envelope 与 exit code contract 已冻结，`CLI-TODO-011` 已提供 reserved topology；待 `CLI-TODO-009/010` 落实现并以真实断言替换 fail-closed 占位入口 | 新增 contract 测试用例 | 仅当 `ping`、`run completed`、`accepted_async`、`auth deny`、`daemon unavailable` 等场景的 JSON 主键与 CLI v1 退出码均可自动化锁定时完成 |
| CLI-TODO-013 | NotStarted | 验证 CLI binary 的 async、显式 ID 与 stdout/stderr 集成门 | CLI 详设 6.4.3、6.5、9；现有 `DaemonBinaryUnarySmokeTest`、`DaemonReceiptFlowIntegrationTest`、`CliDaemonSocketPathIntegrationTest` | `run --async`；`--request-id`；`--trace-id`；脚本化 I/O | L2 | `apps/cli/src/main.cpp`；`tests/integration/access/DaemonBinaryUnarySmokeTest.cpp`；`tests/integration/access/DaemonReceiptFlowIntegrationTest.cpp`；`tests/integration/access/CliDaemonSocketPathIntegrationTest.cpp` | 复用现有 integration tests，扩展 CLI binary 场景覆盖 async、显式 IDs、stdout/stderr 责任边界 | `ctest --test-dir build-ci -R "CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest|DaemonReceiptFlowIntegrationTest" --output-on-failure` | `CLI-TODO-008`、`CLI-TODO-009`、`CLI-TODO-010`、`CLI-TODO-012` | 无 | parser、request builder、formatter、contract tests 均已落盘；若 peer identity 外部依赖仍未闭环，只能在 auth deny / diag 覆盖范围上标注降级 | 更新后的 integration tests；专项 TODO 对应节 | 仅当 built `dasall_cli` 能在 sync/async 两条路径下正确携带显式 `request_id` / `trace_id`，且成功结果只落 stdout、错误提示只落 stderr 时完成 |
| CLI-TODO-014 | NotStarted | 持续证据回写与 CLI 专项最终收口 | 工程规范 6/7；专项 TODO 基线；当前 CLI 交付散落在 access/daemon 文档 | CLI 专项证据闭环；Gate-CLI-06 | L2 | `docs/todos/cli/DASALL_cli本地控制面专项TODO.md`；`docs/worklog/DASALL_开发执行记录.md`；必要时 `docs/todos/cli/deliverables/` | 每任务 focused evidence 回写；Gate/Blocker/Risk/OQ 状态最终复验 | 文档证据检查 + focused gate 复跑摘要 | `rg -n "CLI-TODO-00[1-9]|CLI-TODO-01[0-4]|Gate-CLI-0[1-6]|CLI-BLK-00[1-4]|CLI-OQ-00[1-7]|CLI-RISK-00[1-8]" docs/todos/cli/DASALL_cli本地控制面专项TODO.md docs/worklog/DASALL_开发执行记录.md` | `CLI-TODO-001` ~ `CLI-TODO-013` | 无 | 每个前序任务完成时已即时回写 focused evidence；收口时只做一致性复验、残余风险与外部依赖状态更新 | 更新后的专项 TODO、worklog 与可选 deliverables | 仅当所有已完成任务都有 focused 证据、Gate-CLI-05/06 状态明确、Blocker/Risk/OQ 均完成更新，并注明 platform peer identity 等外部依赖是否仍残留时完成 |

## 7. 执行顺序建议

### 7.1 顺序与并行段

| 波次 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| Wave 0 | `CLI-TODO-001 -> CLI-TODO-002` | 已完成 | 两项补设计任务已完成，命令参数 contract 与脚本化 contract 均已冻结；后续进入 topology 与 Build 任务 |
| Wave 1 | `CLI-TODO-003 ~ CLI-TODO-005`；`CLI-TODO-011` | 基线继承 + topology 先行 | 003~005 已按 focused tests + 历史交付物复核并确认代码完整实现；011 已建立 unit/contract 可发现拓扑，后续测试不再需要继续挂靠 access |
| Wave 2 | `CLI-TODO-006` 与 `CLI-TODO-009` | 可并行 | 006 依赖参数 schema；009 依赖 JSON/exit design freeze 与 topology；两者解不同 blocker |
| Wave 3 | `CLI-TODO-007 -> CLI-TODO-008`；`CLI-TODO-010` | `007/008` 串行；`010` 可与 `008` 并行收尾 | parser/help/version 先于 request builder；formatter 依赖 009，但不依赖 builder 完成 |
| Wave 4 | `CLI-TODO-012 -> CLI-TODO-013` | contract -> integration | 先锁 JSON/exit code 契约，再跑 binary/integration gate，避免脚本化行为只被集成测试隐式覆盖 |
| Wave 5 | `CLI-TODO-014` | 持续回写 + 最终收口 | 001~013 每完成一项即时回写 focused evidence；014 只做最终一致性复验、worklog 链接、残余风险与 OQ 状态收口 |

### 7.2 必过门禁表

| Gate ID | 触发时机 | 通过条件 | 关联任务 | 失败回退 |
|---|---|---|---|---|
| Gate-CLI-01 | `CLI-TODO-001/002` 完成时；任何 Build PR 合入前 | 参数矩阵、socket flag 命名、JSON/exit contract 文档中无 `TBD`、无未决字段；`--socket-path` 与 access error -> CLI exit projection 口径已回链详设 | `CLI-TODO-001`、`CLI-TODO-002` | 停止后续 Build 任务，回到详设补完 |
| Gate-CLI-02 | topology 任务完成时；contract PR 合入前 | `ctest -N` 可发现现有 CLI unit tests 与计划中的 CLI contract tests，且 labels/目录不再只能依附 `tests/unit/access`；reserved contract 入口在真实断言落地前保持 fail-closed | `CLI-TODO-011` | 保留 access label 镜像，但不得把 reserved contract entrypoint 当作真实 contract 完成 |
| Gate-CLI-03 | parser/builder/formatter 完成时 | `CliDaemonCommandParserTest`、`CliIpcClientTest`、`CliIpcClientResponseTest`、`CliIpcClientUnavailableTest`、`CliDaemonOutputFormatterTest` 全通过 | `CLI-TODO-006`、`CLI-TODO-007`、`CLI-TODO-008`、`CLI-TODO-010` | 保留当前 parser/wire baseline，回滚未冻结的新 flags 行为 |
| Gate-CLI-04 | exit code 与 contract 完成时 | `AccessErrorMappingTest`、`CliJsonOutputContractTest`、`CliExitCodeContractTest` 全通过；CLI 退出码确认使用 `0/2/3/4/5/6/7` 而非 access 既有 `1/75/77` | `CLI-TODO-009`、`CLI-TODO-012` | 保持 human-only 输出，不开放稳定 `--json` 和新退出码 |
| Gate-CLI-05 | binary/integration 完成时 | `CliDaemonSocketPathIntegrationTest`、`DaemonBinaryUnarySmokeTest`、`DaemonReceiptFlowIntegrationTest` 全通过；peer identity 相关 auth deny 若仍由外部依赖阻塞，必须写明降级覆盖范围 | `CLI-TODO-013` | 保持现有最小 unary surface，不宣称 async/script-ready |
| Gate-CLI-06 | 收口时 | 本专项 TODO、worklog、blocker、risk、OQ、统一验收命令与外部依赖状态齐全 | `CLI-TODO-014` | 不进入 close-ready，仅保持 draft-ready |

## 8. 阻塞项与解阻条件

| Blocker ID | 对应设计 Blocker | 阻塞内容 | 影响任务 | 解阻条件 | 最小解阻动作 |
|---|---|---|---|---|---|
| CLI-BLK-001 | CLI 详设 1.6 | 已解阻：`CLI-TODO-001` 已冻结 `run/status/cancel/help/version` 参数 schema、selector 规则、`--socket-path` 稳定命名与 version local-only 边界 | 已对 `CLI-TODO-006/007/008` 解阻；`CLI-TODO-013` 不再受命令 schema 未冻结影响 | 详设、专项 TODO 与 deliverable 三处口径一致；parser tests 可直接按冻结 contract 扩展 | 后续 Build 任务按已冻结 contract 落实现，不再回到“猜参数名”阶段 |
| CLI-BLK-002 | CLI 详设 12 未决问题 3；6.4.4 | 已解阻：`CLI-TODO-002` 已冻结 `cli.output.v1` envelope、`warnings/error` 顶层位置、`daemon_unavailable/protocol_error` 本地 disposition、stdout/stderr 归属，以及 access error facts -> CLI `0/2/3/4/5/6/7` 的投影矩阵 | 已对 `CLI-TODO-009/010/012/013` 清除脚本化 contract 不确定性；剩余阻塞转向 topology 与实现落盘 | 详设、专项 TODO 与 deliverable 三处口径一致；focused tests 仍以 access error mapping 与 formatter 现状为最近锚点 | 后续实现任务按已冻结 contract 落盘，不再回到“镜像 AgentResult 还是 CLI projection”的讨论 |
| CLI-BLK-003 | 工程规范 4.1；当前 tests 现状 | 已解阻：`CLI-TODO-011` 已建立 `tests/unit/apps/cli` discoverability，并在 `tests/contract/access` 提供 `CliJsonOutputContractTest`、`CliExitCodeContractTest` 的 fail-closed reserved entrypoint | 已对 `CLI-TODO-009/010/012/014` 清除 topology 缺口；后续只剩真实 JSON / exit code 实现与 contract 断言落盘 | `ctest -N` 可稳定发现现有 CLI unit tests 与预留 contract 名称；CLI unit 不再只能依附 `tests/unit/access` | `CLI-TODO-012` 直接以既有 reserved entrypoint 替换为真实 contract tests，不再额外补 topology |
| CLI-BLK-004 | CLI 详设 6.3.3、6.9、11、12 未决问题 1 | platform peer identity 补口未完全闭环，影响 daemon/access 的 local trusted、auth deny、diag 授权端到端证据 | `CLI-TODO-013`、`CLI-TODO-014` | platform 侧补口设计或 TODO 明确 owner、接口形态与验收命令；在未完成前集成门禁必须标注降级覆盖范围 | 回链 platform/access TODO，不把 peer identity 实现塞入 CLI 专项；未解阻前仅宣称 CLI 客户端面完成 |

说明：`DMD-TODO-031`、`DMD-TODO-036` 已清除 CLI wire contract 与 endpoint drift 风险；daemon/platform/access 不阻塞 `apps/cli` 客户端任务，但 platform peer identity 仍阻塞本地控制面端到端安全闭环，需作为外部依赖在 Gate-CLI-05/06 中持续回写。

## 9. 验收与质量门

### 9.1 测试矩阵

| 测试层级 | 覆盖范围 | 当前状态 | 代表测试 |
|---|---|---|---|
| Unit | parser 最小命令面、socket-path 覆盖 | 已有基线，需扩展 | `CliDaemonCommandParserTest` |
| Unit | request encode / response parse / fail-closed | 已有基线，需扩展 | `CliIpcClientTest`、`CliIpcClientResponseTest`、`CliIpcClientUnavailableTest` |
| Unit | human formatter | 已有基线，需扩展到 warnings/json mode 切换 | `CliDaemonOutputFormatterTest` |
| Contract | `--json` 主键与 envelope 稳定性 | 未落盘 | `CliJsonOutputContractTest` |
| Contract | exit code 稳定性 | 未落盘 | `CliExitCodeContractTest` |
| Integration | default endpoint / explicit endpoint override | 已有基线 | `CliDaemonSocketPathIntegrationTest` |
| Integration | built CLI -> built daemon unary | 已有基线，需扩展 stdout/stderr 和显式 IDs | `DaemonBinaryUnarySmokeTest` |
| Integration | accepted_async / receipt / cancel | 已有基线，需扩展 `--async` 与 CLI binary surface | `DaemonReceiptFlowIntegrationTest` |
| Regression | access 错误 code/domain 与既有协议映射回归证据 | 已有基线 | `AccessErrorMappingTest` |

### 9.2 Focused 验收命令

1. parser / client / formatter focused gate：`ctest --test-dir build-ci -R "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest" --output-on-failure`
2. contract gate：`ctest --test-dir build-ci -R "AccessErrorMappingTest|CliJsonOutputContractTest|CliExitCodeContractTest" --output-on-failure`
3. integration gate：`ctest --test-dir build-ci -R "CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest|DaemonReceiptFlowIntegrationTest" --output-on-failure`
4. discoverability gate：`ctest --test-dir build-ci -N | rg "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|CliJsonOutputContractTest|CliExitCodeContractTest"`

### 9.3 统一验收命令建议

CLI 专项完成态必须提供一条 one-shot 命令覆盖 build、discoverability、unit、contract 与 integration focused gate。当前推荐命令如下；`CLI-TODO-011` 完成后，contract test 名称已可被 `ctest -N` 发现，但在 `CLI-TODO-012` 完成前这些 reserved entrypoint 会故意 fail-closed，不能拿来冒充真实 contract 断言通过。

```bash
cmake --build build-ci --target dasall_cli dasall_daemon \
   && ctest --test-dir build-ci -N \
      | rg "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|CliJsonOutputContractTest|CliExitCodeContractTest|CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest|DaemonReceiptFlowIntegrationTest" \
   && ctest --test-dir build-ci -R "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|AccessErrorMappingTest|CliJsonOutputContractTest|CliExitCodeContractTest|CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest|DaemonReceiptFlowIntegrationTest" --output-on-failure
```

统一验收通过定义：所有上述测试均被发现并通过；若 platform peer identity 外部依赖未完成，`auth deny` / `diag` 端到端场景必须在 `CLI-TODO-014` 中标注为外部残余，不得把 CLI 客户端完成态误写为本地控制面全链路完成态。

## 10. 风险与回退策略

| Risk ID | 对应设计 Risk | 风险内容 | 影响面 | 回退策略 |
|---|---|---|---|---|
| CLI-RISK-001 | CLI 详设 1.6 | 参数 schema 冻结若直接照当前最小 positional 代码推进，后续 CLI man page 与脚本兼容将返工 | parser、help、version、binary smoke | 先完成 `CLI-TODO-001`；若评审未通过，保留当前最小 positional surface，不合并新 flags |
| CLI-RISK-002 | CLI 详设 6.4.2、12 未决问题 3 | 若后续实现直接暴露 raw `AgentResult` / `UdsResponseFrame`，会破坏已冻结的 CLI projection envelope | formatter、contract tests、自动化脚本 | 已由 `CLI-TODO-002` 固定 `cli.output.v1` 主键与 `result/error/warnings` 位置；实现阶段必须以 `CliJsonOutputContractTest` 锁定 |
| CLI-RISK-003 | CLI 详设 6.4.4 | 若后续实现直接复用 access 既有 `1/75/77` 或继续返回旧 `0/1`，会违背 CLI v1 exit family | CLI 自动化集成、binary smoke | 已由 `CLI-TODO-002` 固定 `0/2/3/4/5/6/7` 与投影顺序；实现阶段必须以 `CliExitCodeContractTest` 锁定 |
| CLI-RISK-004 | 工程规范 4.1；当前 tests 现状 | 测试拓扑从 `tests/unit/access` 收敛到 `tests/unit/apps/cli` 时，容易导致 discoverability 断裂 | CI / `ctest -N` / 维护入口 | 过渡期允许镜像注册，待 `ctest -N` 稳定后再移除旧接线 |
| CLI-RISK-005 | CLI 详设 6.4.1、6.4.5 | 若后续实现回退到 daemon 自动探测或 version 触网，会把 CLI version 重新做成伪运维入口 | `help/version` 文案、用户预期 | 已由 `CLI-TODO-001` 冻结 `version` local-only；后续扩展只能走显式加法 surface |
| CLI-RISK-006 | CLI 详设 6.4.3；当前实现 | 若后续实现重新接受公开 `--socket`，会破坏脚本兼容与 help 文案一致性 | parser、usage、integration smoke、文档 | 已由 `CLI-TODO-001` 冻结 `--socket-path` 为唯一稳定命名；`--socket` 不得回归为公开 alias |
| CLI-RISK-007 | CLI 详设 6.3.3、6.9、11 | platform peer identity 若从 CLI 专项视野中消失，可能导致 auth deny / diag 授权场景被误判为 CLI 完成 | binary integration、security evidence、Gate-CLI-05/06 | 把 peer identity 保持为外部依赖和 `CLI-BLK-004`；未解阻前只宣称 CLI 客户端面完成 |
| CLI-RISK-008 | 工程规范 6/7；专项 TODO 基线 | 证据若集中到最后一次性回写，容易丢失 focused 命令与 blocker 演进证据 | worklog、deliverables、Gate 复验 | 每个 TODO 完成时即时回写 focused evidence；`CLI-TODO-014` 只做最终复验与残余风险收口 |

## 11. 可行性结论

1. 本专项 TODO 可以直接进入执行，且两项补设计解阻任务 `CLI-TODO-001/002` 已完成；当前剩余前置从“用户面 contract 未冻结”收敛为“测试 topology 与实现落盘”。
2. 当前可安全落到的最细粒度为：`CliCommandParser::parse()`、`CliIpcClient::send_request()`、`CliOutputFormatter::format_*()` 和 `main()` 退出路径的 `L3` 任务；`CliRequestBuilder`、`CliExitDecision` 与 contract topology 仍是 `L2` 实现对象，但不再是设计未决。
3. 当前已不存在会阻止纯函数级推进的用户面证据缺口；命令参数、JSON 主键、stdout/stderr 归属与 exit family 均可直接引用文档进行二值核对。
4. 因此当前结论不是 “Blocked”，而是 “CLI 用户面已冻结，下一步应并行推进 `CLI-TODO-011` 与 `CLI-TODO-006/007/008/009/010`”。若绕过这些冻结结论直接按当前最小实现习惯落盘，最可能返工的是 JSON projection 与 exit family，而不是 transport 层。
5. 建议执行策略：先完成 `CLI-TODO-011` 建立 topology，同时按已冻结 contract 推进 parser/request builder/exit decision/formatter，再把 JSON/exit contract tests 与 binary/integration 门串起来，最终通过 `CLI-TODO-014` 收口证据。
6. 当前专项完成态的边界必须写清：`apps/cli` 客户端面可以独立完成；若 platform peer identity 仍未闭环，则本地控制面端到端安全场景只能标为外部依赖残余，不能宣称全链路 close-ready。

## 12. 未决问题处置表

| OQ ID | 来源 | 未决问题 | 处置策略 | 对应任务 / 外部依赖 | 完成判定 |
|---|---|---|---|---|---|
| CLI-OQ-001 | CLI 详设 12.1 | platform peer identity 能力最终扩展 IIPC 还是 side-interface | 转外部 platform/access 决策；CLI 专项只回链，不实现 | `CLI-BLK-004`、`CLI-RISK-007`、platform peer identity TODO | platform 侧 owner、接口形态、验收命令明确；CLI Gate-CLI-05/06 标注残余或解除 |
| CLI-OQ-002 | CLI 详设 12.2 | ping 是否直接暴露 `profile_id` 与 build hash | 已在 `CLI-TODO-001` 冻结为保守口径：ping 继续优先暴露 schema/readiness 摘要，`version` 只报告 CLI 本地 build metadata；daemon 对比保持后续加法扩展 | `CLI-TODO-001`、`CLI-TODO-010` | ping/version 输出字段在详设与 TODO 中可二值核对，formatter tests 覆盖 |
| CLI-OQ-003 | CLI 详设 12.3 | `--json` 是镜像 `AgentResult` 还是 CLI projection | 已在 `CLI-TODO-002` 冻结为 CLI projection envelope；固定 `cli.output.v1`、顶层 `result/error/warnings`、本地 `daemon_unavailable/protocol_error` disposition，并禁止 raw `AgentResult` 直出 | `CLI-TODO-002`、`CLI-TODO-010`、`CLI-TODO-012` | JSON 主键、error/warnings、disposition、request_id/trace_id、receipt/result 投影均有 contract tests |
| CLI-OQ-004 | CLI 详设 12.4 | v1 是否允许 CLI 在开发场景尝试拉起 daemon | 已收敛为否；CLI v1 不自动拉起 daemon，`version` 也不隐式触达 daemon，避免客户端承担 daemon lifecycle | `CLI-TODO-001`、`CLI-TODO-002`、`CLI-TODO-009`、`CLI-TODO-013` | daemon unavailable contract / integration 覆盖 exit 3，文档不宣称 auto-start |
| CLI-OQ-005 | CLI 详设 12.5 | diag 是否开放 log query artifact_ref | 延后；v1 仅保留 `diag health`、`diag queue`、`diag threads` 三个受控只读方向，是否开放取决于 infra/diagnostics 实现节奏 | `CLI-TODO-001`、`CLI-BLK-004`、infra/diagnostics TODO | parser/help 明确 diag 子命令范围；未授权与未启用路径 fail-closed |
| CLI-OQ-006 | 本次评估新增 | `--socket` 与 `--socket-path` 命名漂移 | 已在 `CLI-TODO-001` 回链详设；v1 稳定用户面只保留 `--socket-path`，`--socket` 不再作为公开 alias | `CLI-TODO-001`、`CLI-RISK-006` | 详设、TODO、usage、parser tests 与 integration smoke 使用同一主命名 |
| CLI-OQ-007 | 本次评估新增 | access 既有 `cli_exit_code` 与 CLI v1 `0/2/3/4/5/6/7` 的关系 | 已在 `CLI-TODO-002` 冻结为“access error code/domain/retryable 输入 + CLI local projection 输出”；`map_access_error().cli_exit_code` 不得进入最终用户面契约 | `CLI-TODO-002`、`CLI-TODO-009`、`CLI-TODO-012` | `CliExitCodeContractTest` 锁定 CLI v1 矩阵，`AccessErrorMappingTest` 仅作为 access error mapping regression |