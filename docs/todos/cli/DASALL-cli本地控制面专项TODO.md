# DASALL CLI 本地控制面专项 TODO

最近更新时间：2026-05-05
阶段：Detailed Design -> Special TODO
适用范围：`apps/cli/` 纯客户端入口壳层、CLI 到 daemon 的本地 IPC client 面、CLI 用户面契约、CLI 相关单测/契约测试/集成门禁与交付证据回写
当前结论：CLI 专项 `CLI-TODO-001` 至 `CLI-TODO-014` 已全部完成，当前仓库已经完成 CLI 纯客户端依赖方向、CLI-daemon wire contract、shared endpoint 默认值、稳定命令参数 schema、`CliRequestBuilder`、`CliExitDecision`、human/JSON 双格式输出、JSON/exit code contract，以及 built `dasall-cli` 的 sync/async 显式 ID 与 stdout/stderr 集成门。CLI 专项现已进入 close-ready 完成态；后续不再存在 CLI 客户端面未完成 Build 任务。此前作为外部依赖跟踪的 platform peer identity 已由 platform/access owner 侧复用既有 `IIPC::describe_peer` / `UnixIpcProvider::describe_peer` 实现，并通过 `UnixIpcProviderPeerIdentityTest`、`DaemonProtocolAdapterLocalTrustedTest`、`DaemonPeerIdentityFailClosedTest`、`DaemonFailureInjectionTest` 与新增 `DaemonDiagDenyIntegrationTest` 完成 provider -> adapter -> real UDS auth/diag allow-reject 证据收口，因此 CLI 专项不再保留 `CLI-BLK-004` / `CLI-RISK-007` / `CLI-OQ-001` 残余项。

评估修订要点（2026-05-04）：本次复核后补充冻结 9 个执行口径：`access::map_access_error()` 只作为 access 错误事实与协议映射输入，CLI v1 退出码仍由 `CliExitDecision` 投影为 `0/2/3/4/5/6/7`；`CLI-TODO-002` 已将 `--json` 收敛为 CLI projection envelope，固定顶层 `result/error/warnings` 位置、`schema_version=cli.output.v1`、`daemon_unavailable` / `protocol_error` 本地 disposition 与 stdout/stderr 归属；`exit_code_hint` 降级为 diagnostics hint，不得替代最终 exit 决策；CLI v1 socket 覆盖的稳定用户面命名已由 `CLI-TODO-001` 冻结为 `--socket-path`，`--socket` 不再作为公开 alias；platform peer identity 不阻塞 `apps/cli` 客户端任务，但继续作为 daemon/access 端到端本地控制面的外部依赖跟踪；CLI unit topology 与 contract topology 需要提前落点；新增未决问题处置表、统一验收命令与“持续证据回写 + 最终收口”的 `CLI-TODO-014`。

## 1. 文档头

### 1.1 输入依据

本文档严格基于以下输入生成：

1. `docs/architecture/DASALL-cli本地控制面详细设计.md`
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
5. `docs/todos/cli/DASALL-cli本地控制面专项TODO.md` 与后续 worklog/交付证据回写。

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
| 命令参数口径 | 已补设计冻结，parser/help/version 已落位 | `CLI-TODO-001` 已冻结 `run/status/cancel/help/version` 的 usage skeleton、`--socket-path` 稳定命名、selector 规则与 version local-only 边界；`CLI-TODO-006/007` 已将稳定 flags/selector 字段、help/version 解析和本地 dispatch 落到 parser/main，Build 侧剩余 request builder 对齐实现 |
| JSON 输出与退出码矩阵 | 已完成并收口 | `CLI-TODO-002` 已冻结 `cli.output.v1` envelope、stdout/stderr 归属、`DaemonClientResponse` CLI projection 边界，以及 access error fact -> CLI `0/2/3/4/5/6/7` 投影矩阵；`CLI-TODO-009/010/012/013` 已将 `CliExitDecision`、`CliOutputFormatter`、`main()` 路由、access error JSON projection，以及 built binary 的 sync/async 显式 ID 与 stdout/stderr 集成门一并落盘，`CLI-TODO-014` 已完成证据收口 |
| CLI 自有测试拓扑 | 已完成并收口 | `CLI-TODO-011` 已建立 CLI 自有 unit/contract discoverability；`CliExitCodeContractTest` 已在 `CLI-TODO-009` 中替换 reserved entrypoint 为真实断言，`CliJsonOutputContractTest` 也已在 `CLI-TODO-010` 中进入真实断言，014 已将 discoverability / contract / integration gate 统一纳入 close-ready 验收 |
| CLI 专项收口状态 | 已完成 | `CLI-TODO-014` 已完成 focused evidence 汇总、Gate-CLI-06 复验、统一验收命令对齐与 peer identity 外部依赖解除状态回写；CLI 专项当前不再保留 blocker/risk/OQ 残余项 |
| platform peer identity 外部依赖 | 已完成外部回收 | platform/access owner 已以 provider、adapter、fail-closed 与 real UDS auth/diag allow-reject gate 收敛 peer identity 闭环；CLI 文档仅回链证据，不越权承担平台实现 |

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
| `apps/cli/src/CliIpcClient.cpp` | 已能编码 request frame、接收响应并解析 `DaemonClientResponse` | transport baseline 已就绪；`CLI-TODO-012` 已通过 `CliAccessErrorProjection` + `CliOutputFormatter` 将稳定 `error_ref` alias 投影为 JSON `access_error_code/domain/retryable` 事实，供 exit decision / JSON envelope 共用 |
| `apps/cli/src/CliCommandParser.h/.cpp` | 已对齐稳定命令 schema，支持 `help`、`version`、`run/status/cancel/ping/diag` 与 v1 flags/selector 作用域校验 | parser 用户面 contract 已落地，不再停留在最小兼容 surface |
| `apps/cli/src/CliOutputFormatter.h/.cpp` | 已同时支持 human 与 `cli.output.v1` JSON envelope，并按 `CliExitDecision` 区分 stdout/stderr 路由 | formatter 已成为稳定 CLI projection owner，`error/warnings/result` 主键与本地 disposition 已由 contract tests 锁定 |
| `apps/cli/src/main.cpp` | 已统一通过 `CliExitDecision` 投影 `0/2/3/4/5/6/7`，并对 daemon-facing 与 local-only 命令分别完成输出/退出路径收敛 | 主程序出口不再是二值成功/失败，而是稳定 CLI v1 contract |
| `tests/unit/access/CliDaemonCommandParserTest.cpp` | 已覆盖稳定命令面、`help/version`、flags 作用域、selector 组合与 `--socket-path` 解析 | parser focused gate 已完成，不再只是预留扩展入口 |
| `tests/unit/access/CliIpcClientTest.cpp`、`CliIpcClientResponseTest.cpp`、`CliIpcClientUnavailableTest.cpp` | 已覆盖 request encode、response parse、daemon unavailable fail-closed | 可复用为 request shaping 与 local transport failure 断言入口 |
| `tests/unit/access/CliDaemonOutputFormatterTest.cpp` | 已覆盖 human 输出与 JSON envelope 关键主键/本地 failure 投影 | formatter focused gate 已完成，并与 contract gate 对齐 |
| `tests/integration/access/CliDaemonSocketPathIntegrationTest.cpp` | 已覆盖默认 socket path 与显式 `--socket-path` 正向连通 | endpoint 和 deployment smoke 基线已具备 |
| `tests/integration/access/DaemonBinaryUnarySmokeTest.cpp`、`DaemonReceiptFlowIntegrationTest.cpp` | 已覆盖 built CLI/built daemon unary 与 async receipt flow | 可复用为 CLI `--async`、`request_id`、`trace_id`、stdout/stderr 集成门 |
| `tests/unit/apps/cli/CMakeLists.txt`、`tests/contract/access/CMakeLists.txt` | CLI unit/contract discoverability 已接通，`CliJsonOutputContractTest` 与 `CliExitCodeContractTest` 已成为真实 contract 入口 | CLI close-ready 验收已具备独立 topology，不再依赖临时挂靠或 reserved placeholder |

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
| CLI-TODO-001 | Done | 补齐 run/status/cancel/help/version 参数 schema 与 usage 文案 | CLI 详设 1.6、6.4.1、6.4.2、6.4.3；当前 `CliCommandParser` 仅有最小 positional 面；详设 `--socket` 与实现 `--socket-path` 命名漂移 | 1.6 参数 schema 冻结；6.4.2 参数约定；6.4.3 flags 表与 socket flag 命名 | L2 | `docs/architecture/DASALL-cli本地控制面详细设计.md`；`docs/todos/cli/DASALL-cli本地控制面专项TODO.md`；`docs/todos/cli/deliverables/CLI-TODO-001-CLI命令schema与usage文案冻结.md` | `CliCommand`；`CliCommandParser::parse()`；`CliCommandParser::usage_string()`；`--socket-path` 命名与兼容 surface policy | 复用 `CliDaemonCommandParserTest` 作为当前 parser 锚点；Build 阶段继续扩展 flags/帮助面正反例 | `rg -n "run|status|cancel|help|version|--socket-path|--socket|submit|readiness" docs/architecture/DASALL-cli本地控制面详细设计.md docs/todos/cli/DASALL-cli本地控制面专项TODO.md docs/todos/cli/deliverables/CLI-TODO-001-CLI命令schema与usage文案冻结.md && ctest --test-dir build-ci -R "CliDaemonCommandParserTest" --output-on-failure` | 无 | 已解阻 | `docs/todos/cli/deliverables/CLI-TODO-001-CLI命令schema与usage文案冻结.md`；更新后的 CLI 详设与专项 TODO | 仅当 `run/status/cancel/help/version` 的参数名、selector 规则、`--socket-path` 稳定命名、compat surface 边界和 version local-only 策略都能被二值核对时完成 |
| CLI-TODO-002 | Done | 补齐 `--json` envelope 与 `CliExitDecision` 契约 | CLI 详设 6.4.2、6.4.4、7、9、12 未决问题 3；`AccessErrorMappingTest` 已冻结 access 错误 code/domain 与既有协议映射 | 6.4.2 JSON envelope；6.4.4 退出码映射；7 Design -> Build；9 Contract | L2 | `docs/architecture/DASALL-cli本地控制面详细设计.md`；`docs/todos/cli/DASALL-cli本地控制面专项TODO.md`；`docs/todos/cli/deliverables/CLI-TODO-002-CLI-JSON-envelope与CliExitDecision冻结.md` | `CliOutputFormatter`；`DaemonClientResponse`；`CliExitDecision`；access error code/domain/retryable -> CLI local exit projection | 复用 `AccessErrorMappingTest`、`CliDaemonOutputFormatterTest` 作为当前实现锚点；Build 阶段继续新增 `CliJsonOutputContractTest`、`CliExitCodeContractTest` | `rg -n "CliJsonOutputContractTest|CliExitCodeContractTest|cli.output.v1|daemon_unavailable|protocol_error|CliExitDecision|0/2/3/4/5/6/7" docs/architecture/DASALL-cli本地控制面详细设计.md docs/todos/cli/DASALL-cli本地控制面专项TODO.md docs/todos/cli/deliverables/CLI-TODO-002-CLI-JSON-envelope与CliExitDecision冻结.md && ctest --test-dir build-ci -R "AccessErrorMappingTest|CliDaemonOutputFormatterTest" --output-on-failure` | 无 | 已解阻 | `docs/todos/cli/deliverables/CLI-TODO-002-CLI-JSON-envelope与CliExitDecision冻结.md`；更新后的 CLI 详设与专项 TODO | 仅当 `--json` 主键、`warnings/error` 放置位置、`daemon_unavailable/protocol_error` 本地 disposition、stdout/stderr 归属，以及 transport/protocol/access facts -> CLI v1 退出码的映射都可直接评审时完成 |

### 6.2 已完成基线与实现任务

| Task ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CLI-TODO-003 | Done | 校正 CLI 纯客户端依赖方向与组合根 | 工程蓝图 3.2；CLI 详设 1.2、6.2；现有交付 `ACC-TODO-025`、`ACC-TODO-038` | 6.2 依赖方向冻结；7 `CLI 纯客户端化` | L3 | `apps/cli/CMakeLists.txt`；`apps/cli/src/main.cpp`；`apps/cli/src/CliIpcClient.*` | `dasall-cli` target；`main()`；`CliIpcClient` | `CliIpcClientTest`；`CliIpcClientUnavailableTest` | `ctest --test-dir build-ci -R "CliIpcClientTest|CliIpcClientUnavailableTest" --output-on-failure` | 无 | 无 | 已解阻 | `docs/todos/access/deliverables/ACC-TODO-025-CliIpcClient与cli纯客户端组合根收敛.md`；`docs/todos/access/deliverables/ACC-TODO-038-CLI依赖方向纠正与UDS路径收敛.md`；相关代码文件 | 仅当 `apps/cli` 不再直链 runtime，CLI 只经 `IIPC/UDS -> daemon/access` 发起请求且 focused tests 通过时完成 |
| CLI-TODO-004 | Done | 收敛 CLI-daemon wire contract 与响应解析 | CLI 详设 6.3.2、6.5、9；现有交付 `DMD-TODO-031` | CLI -> daemon 本地控制面；响应解析契约 | L3 | `apps/cli/src/CliIpcClient.*`；`apps/cli/src/CliCommandParser.*`；`apps/cli/src/CliOutputFormatter.*`；`tests/integration/access/DaemonPingIntegrationTest.cpp` | `DaemonClientResponse`；`CliIpcClient::send_request()`；`CliCommandParser::parse()`；`CliOutputFormatter::format_*()` | `CliIpcClientTest`；`CliIpcClientResponseTest`；`CliIpcClientUnavailableTest`；`CliDaemonCommandParserTest`；`CliDaemonOutputFormatterTest`；`DaemonPingIntegrationTest` | `ctest --test-dir build-ci -R "CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonCommandParserTest|CliDaemonOutputFormatterTest|DaemonPingIntegrationTest" --output-on-failure` | `CLI-TODO-003` | 无 | 已解阻 | `docs/todos/daemon/deliverables/DMD-TODO-031-CLI-daemon-wire-contract收敛.md`；相关代码与测试文件 | 仅当 CLI 能区分 `completed/accepted_async/rejected/not_ready`，并在真实 ping roundtrip 中消费响应而非只断言 `send()` 成功时完成 |
| CLI-TODO-005 | Done | 收敛 shared endpoint 默认值与 `--socket-path` 覆盖 | CLI 详设 6.7.1、6.10；现有交付 `DMD-TODO-036` | socket_path；CLI 入口契约 | L3 | `access/include/daemon/DaemonEndpointDefaults.h`；`apps/cli/src/main.cpp`；`apps/cli/src/CliCommandParser.*`；`tests/integration/access/CliDaemonSocketPathIntegrationTest.cpp` | `kDefaultDaemonSocketPath`；`CliCommand.socket_path`；`CliCommandParser::parse()`；`main()` endpoint resolution | `CliDaemonCommandParserTest`；`CliDaemonSocketPathIntegrationTest`；`DaemonPingIntegrationTest` | `ctest --test-dir build-ci -R "CliDaemonCommandParserTest|CliDaemonSocketPathIntegrationTest|DaemonPingIntegrationTest" --output-on-failure` | `CLI-TODO-004` | 无 | 已解阻 | `docs/todos/daemon/deliverables/DMD-TODO-036-daemon-cli-endpoint统一与覆盖收敛.md`；shared endpoint/default 代码与部署文档 | 仅当 CLI 与 daemon 默认 socket path 来自同一常量，且默认值与显式覆盖两条路径都可正向连通 daemon 时完成 |
| CLI-TODO-006 | Done | 定义 `CliCommand` 扩展选项模型 | CLI 详设 6.4.3、6.7.3；当前 `CliCommand` 仅承载 socket/payload/receipt/token/actor/diag | 6.4.3 通用 flags；6.7.3 CLI 本地 UX 配置 | L2 | `apps/cli/src/CliCommandParser.h`；`apps/cli/src/CliCommandParser.cpp`；`tests/unit/access/CliDaemonCommandParserTest.cpp` | `CliCommand`：`output_mode`、`timeout_ms`、`async_preference`、`request_id`、`session_hint`、`trace_id`、`quiet`、`no_input`、selector 类型与 ownership token | 扩展 `CliDaemonCommandParserTest`，验证字段捕获、duplicate/illegal-scope reject | `ctest --test-dir build/vscode-linux-ninja -R "CliDaemonCommandParserTest" --output-on-failure` | `CLI-TODO-001` | 无 | `CLI-TODO-001` 已冻结 flags 作用域与字段含义；当前实现已把稳定字段和最小作用域校验落到 `CliCommand` / parser | 更新后的 `CliCommandParser.h/.cpp`、`CliDaemonCommandParserTest.cpp` 与专项 TODO 对应节 | 仅当详设 flags 表中的每个 v1 选项都映射到 `CliCommand` 稳定字段，且无重复/悬空字段时完成 |
| CLI-TODO-007 | Done | 实现 `CliCommandParser` 的 flags、help、version 校验 | CLI 详设 1.6、6.4.1、6.4.2、6.4.3；当前 parser 无 `help/version`，也不校验 flags 作用域 | 命令面与 usage；冻结 contract -> parser 口径 | L3 | `apps/cli/src/CliCommandParser.cpp`；`apps/cli/src/CliCommandParser.h`；`apps/cli/src/main.cpp`；`tests/unit/access/CliDaemonCommandParserTest.cpp` | `CliCommandParser::parse()`；`CliCommandParser::usage_string()`；`main()` 的 `help/version` dispatch | 扩展 `CliDaemonCommandParserTest` 覆盖 `help`、`version` 与 local-only 命令 flags 作用域错误；built CLI 验证 `--help` 与 `version --json` | `ctest --test-dir build/vscode-linux-ninja -R "CliDaemonCommandParserTest" --output-on-failure && ./build/vscode-linux-ninja/apps/cli/dasall-cli --help && ./build/vscode-linux-ninja/apps/cli/dasall-cli version --json` | `CLI-TODO-001`、`CLI-TODO-006` | 无 | 参数矩阵与 help/version 输出责任边界已冻结；当前实现已将 help/version canonicalization、usage skeleton 和 local-only dispatch 落盘 | 更新后的 parser/main/test 与专项 TODO 对应节 | 仅当 `help`、`version`、各命令 flags 作用域和非法组合都可由 parser/main 二值判定时完成 |
| CLI-TODO-008 | Done | 实现 `CliRequestBuilder` 与 request frame shaping | CLI 详设 6.1、6.3.1、6.4.3、6.5；当前 request shaping 仍在 `CliIpcClient.cpp` 匿名 helper | `CliRequestBuilder`；`UdsRequestFrame` 字段装配 | L2 | `apps/cli/src/CliRequestBuilder.h`；`apps/cli/src/CliRequestBuilder.cpp`；`apps/cli/src/CliIpcClient.h`；`apps/cli/src/CliIpcClient.cpp`；`apps/cli/src/main.cpp`；`access/include/daemon/DaemonProtocolTypes.h`；`access/src/daemon/DaemonFrameCodec.cpp` | `CliRequestBuilder`；`UdsRequestFrame`；`CliIpcClient::invoke()`；`main()` 的 daemon-facing dispatch | 扩展 `CliIpcClientTest`、`CliIpcClientResponseTest` 与 `DaemonFrameCodecTest/MalformedTest`，断言 `request_id`、`trace_id`、`session_hint`、`async_preference`、`deadline_ms`、`output_mode` | `Build_CMakeTools() && ctest --test-dir build/vscode-linux-ninja -R "CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|DaemonFrameCodecTest|DaemonFrameCodecMalformedTest" --output-on-failure` | `CLI-TODO-001`、`CLI-TODO-006` | 无 | 参数矩阵已冻结；当前实现已把 request frame 稳定字段、builder 单一 owner 与 main/client 调用面一并落盘 | 新增 builder 文件；更新后的 client/codec/test 与专项 TODO 对应节 | 仅当 request frame 的 v1 字段都有单一 owner，且 `CliIpcClient` 不再自行拼装业务级 request 参数时完成 |
| CLI-TODO-010 | Done | 实现 `CliOutputFormatter` 的 human/JSON 双格式输出 | CLI 详设 6.1、6.4.2、6.4.3、6.4.4、9；当前 formatter 仅有 human 文本 | human mode / `--json` stable envelope / stdout-stderr split | L2 | `apps/cli/src/CliOutputFormatter.h`；`apps/cli/src/CliOutputFormatter.cpp`；`apps/cli/src/main.cpp`；`tests/contract/access/CMakeLists.txt`；`tests/contract/access/CliJsonOutputContractTest.cpp` | `CliOutputFormatter::format_*()`；`CliOutputFormatter::format_json_output()`；`DaemonClientResponse`；`CliExitDecision`；`main()` 输出流路由 | 扩展 `CliDaemonOutputFormatterTest`，并将 `CliJsonOutputContractTest` 从 reserved entrypoint 替换为真实断言；built CLI 验证 `ping --json`/human 在 daemon 不可达时的 stdout/stderr 分离，以及 `version --json` 共享同一 envelope 主键 | `Build_CMakeTools() && ctest --test-dir build/vscode-linux-ninja -R "CliDaemonOutputFormatterTest|CliJsonOutputContractTest" --output-on-failure && ./build/vscode-linux-ninja/apps/cli/dasall-cli ping --json --socket-path /tmp/dasall-cli-010-missing.sock && ./build/vscode-linux-ninja/apps/cli/dasall-cli version --json` | `CLI-TODO-002`、`CLI-TODO-009` | 无 | JSON 主键、warnings/error 摆放、human/json 切换边界已冻结；当前实现已将 formatter、真实 contract 与 stdout/stderr 路由一并落盘 | 更新后的 formatter/main/contract test 与专项 TODO 对应节 | 仅当默认 human 输出保留、`--json` 输出稳定 envelope、成功路径 stdout 不混入错误提示且 stderr 不承载业务结果时完成 |
| CLI-TODO-009 | Done | 实现 `CliExitDecision` 与本地退出码决策 | CLI 详设 6.3.1、6.4.4；`access::map_access_error()` 已冻结 access 错误 code/domain 与既有协议映射 | `CliExitDecision`；CLI v1 `0/2/3/4/5/6/7` | L2 | `apps/cli/src/CliExitDecision.h`；`apps/cli/src/CliExitDecision.cpp`；`apps/cli/src/main.cpp`；`apps/cli/CMakeLists.txt`；`tests/contract/access/CMakeLists.txt`；`tests/contract/access/CliExitCodeContractTest.cpp` | `CliExitDecision`；`main()` 退出路径；`DaemonClientResponse` + local failure path + stable `error_ref` 事实到 exit decision 的投影 | 复用 `AccessErrorMappingTest`，新增并激活 `CliExitCodeContractTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessErrorMappingTest|CliExitCodeContractTest" --output-on-failure` | `CLI-TODO-002`、`CLI-TODO-011` | 无 | `CLI-TODO-002` 已冻结 CLI local path 到 exit code 的最终矩阵，`CLI-TODO-011` 已提供稳定 contract topology；当前实现已将 exit decision 与 contract entrypoint 落盘 | 更新后的 exit decision/main/CMake；真实 `CliExitCodeContractTest` 与专项 TODO 对应节 | 仅当参数错误=2、daemon 不可达=3、认证授权拒绝=4、业务失败=5、超时/取消或暂不可完成=6、协议错误=7、成功/accepted=0 均可二值断言，且不直接沿用 access 既有 `1/75/77` 映射时完成 |

### 6.3 剩余实现任务

| Task ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|

### 6.4 测试、门禁与证据任务

| Task ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CLI-TODO-011 | Done | 提前注册 CLI unit 与 contract 拓扑 discoverability | 工程规范 4.1；当前 CLI 单测仍位于 `tests/unit/access`；当前 `tests/contract/access` 不存在 | CLI own unit topology；CLI contract topology | L2 | `tests/unit/apps/cli/CMakeLists.txt`；`tests/unit/CMakeLists.txt`；`tests/contract/access/CMakeLists.txt`；`tests/contract/CMakeLists.txt` | CLI 单测聚合列表；contract 测试接入点；过渡期 mirror registration 策略 | `ctest -N` 发现现有 CLI unit tests，并预留 `CliJsonOutputContractTest`、`CliExitCodeContractTest` 的 contract 接入点 | `ctest --test-dir build/vscode-linux-ninja -N | rg "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|CliJsonOutputContractTest|CliExitCodeContractTest"` | `CLI-TODO-001`、`CLI-TODO-002` | 已解阻 | 命令/JSON/exit contract 命名已冻结，可稳定命名测试目标；CLI unit 已切入自有 topology，contract/access 已提供 fail-closed reserved entrypoint | `docs/todos/cli/deliverables/CLI-TODO-011-CLI测试topology与discoverability接线.md`；新增/更新的 unit 与 contract CMake 拓扑 | 仅当 CLI unit tests 可由 CLI 自有拓扑发现，contract/access 目录和 CMake 接线存在，且后续契约测试不再需要临时挂靠 `tests/unit/access` 时完成 |
| CLI-TODO-012 | Done | 补齐 CLI JSON contract tests 与 exit code 扩展场景 | CLI 详设 7、9、12 未决问题 3；`CLI-TODO-011` 提供 contract topology | `CliJsonOutputContractTest`；`CliExitCodeContractTest` | L2 | `apps/cli/src/CliAccessErrorProjection.h`；`apps/cli/src/CliExitDecision.cpp`；`apps/cli/src/CliOutputFormatter.cpp`；`tests/contract/access/CliJsonOutputContractTest.cpp`；`tests/contract/access/CliExitCodeContractTest.cpp` | `CliAccessErrorProjection`；扩展后的 `CliJsonOutputContractTest`；扩展后的 `CliExitCodeContractTest`；`CliOutputFormatter::format_json_output()` | `RunCtest_CMakeTools(tests=["AccessErrorMappingTest","CliJsonOutputContractTest","CliExitCodeContractTest"])`；`Build_CMakeTools()` | `CLI-TODO-002`、`CLI-TODO-009`、`CLI-TODO-010`、`CLI-TODO-011` | 无 | `CLI-TODO-009/010` 已落基础 exit code contract 与真实 JSON contract；012 已补齐 `run completed`、`auth deny`、`daemon unavailable` 以及 replay-hit / authentication alias / receipt failure 等扩展场景矩阵，并将 access error code/domain/retryable 投影接入稳定 JSON error object | 更新后的 formatter/helper 与扩展 contract 测试用例 | 仅当 `ping`、`run completed`、`accepted_async`、`auth deny`、`daemon unavailable` 等场景的 JSON 主键与 CLI v1 退出码均可自动化锁定，且 access error code/domain/retryable 不再退化为全 `null` 时完成 |
| CLI-TODO-013 | Done | 验证 CLI binary 的 async、显式 ID 与 stdout/stderr 集成门 | CLI 详设 6.4.3、6.5、9；现有 `DaemonBinaryUnarySmokeTest`、`DaemonReceiptFlowIntegrationTest`、`CliDaemonSocketPathIntegrationTest` | `run --async`；`--request-id`；`--trace-id`；脚本化 I/O | L2 | `access/include/AccessTypes.h`；`access/src/daemon/DaemonProtocolAdapter.cpp`；`access/src/AccessGatewayFactory.cpp`；`apps/daemon/src/main.cpp`；`tests/integration/access/CliBinaryTestSupport.h`；`tests/integration/access/DaemonIntegrationHarness.h`；`tests/integration/access/DaemonBinaryUnarySmokeTest.cpp`；`tests/integration/access/DaemonReceiptFlowIntegrationTest.cpp`；`tests/integration/access/CliDaemonSocketPathIntegrationTest.cpp`；`tests/integration/access/CMakeLists.txt` | `InboundPacket` trace/session 透传；daemon completed/accepted_async publish envelope；built CLI binary probe helpers；Gate-CLI-05 集成门 | `RunCtest_CMakeTools(tests=["CliDaemonSocketPathIntegrationTest","DaemonBinaryUnarySmokeTest","DaemonReceiptFlowIntegrationTest"])`；`Build_CMakeTools()` | `CLI-TODO-008`、`CLI-TODO-009`、`CLI-TODO-010`、`CLI-TODO-012` | 无 | parser、request builder、formatter、contract tests 均已落盘；013 已将 built `dasall-cli` 的 sync completed、async accepted_async、显式 `request_id/trace_id` 与 human/json stdout-stderr 责任边界全部落到真实 integration gate；后续 platform/access owner 已补齐 peer identity allow/reject 端到端 gate，因此不再需要针对 auth deny / diag 场景保留降级说明 | 更新后的 access/daemon 透传实现、binary test helper、integration tests 与专项 TODO 对应节 | 仅当 built `dasall-cli` 能在 sync/async 两条路径下正确携带显式 `request_id` / `trace_id`，且成功结果只落 stdout、错误提示只落 stderr 时完成 |
| CLI-TODO-014 | Done | 持续证据回写与 CLI 专项最终收口 | 工程规范 6/7；专项 TODO 基线；当前 CLI 交付散落在 access/daemon 文档 | CLI 专项证据闭环；Gate-CLI-06 | L2 | `docs/todos/cli/DASALL-cli本地控制面专项TODO.md`；`docs/worklog/DASALL_开发执行记录.md`；`docs/todos/cli/deliverables/CLI-TODO-014-CLI专项最终收口.md` | 每任务 focused evidence 回写；Gate/Blocker/Risk/OQ 状态最终复验 | 文档证据检查 + focused gate 复跑摘要 + one-shot close-ready gate 结果回写 | `rg -n "CLI-TODO-00[1-9]|CLI-TODO-01[0-4]|Gate-CLI-0[1-6]|CLI-BLK-00[1-4]|CLI-OQ-00[1-7]|CLI-RISK-00[1-8]" docs/todos/cli/DASALL-cli本地控制面专项TODO.md docs/worklog/DASALL_开发执行记录.md && cmake --build build-ci --target dasall-cli dasall-daemon dasall_access_daemon_diag_deny_integration_test dasall_access_daemon_failure_injection_integration_test dasall_access_daemon_protocol_adapter_local_trusted_unit_test dasall_access_daemon_peer_identity_fail_closed_unit_test dasall_unix_ipc_provider_peer_identity_unit_test && ctest --test-dir build-ci -N | rg "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|CliJsonOutputContractTest|CliExitCodeContractTest|CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest|DaemonReceiptFlowIntegrationTest|UnixIpcProviderPeerIdentityTest|DaemonProtocolAdapterLocalTrustedTest|DaemonPeerIdentityFailClosedTest|DaemonFailureInjectionTest|DaemonDiagDenyIntegrationTest" && ctest --test-dir build-ci -R "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|AccessErrorMappingTest|CliJsonOutputContractTest|CliExitCodeContractTest|CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest|DaemonReceiptFlowIntegrationTest|UnixIpcProviderPeerIdentityTest|DaemonProtocolAdapterLocalTrustedTest|DaemonPeerIdentityFailClosedTest|DaemonFailureInjectionTest|DaemonDiagDenyIntegrationTest" --output-on-failure` | `CLI-TODO-001` ~ `CLI-TODO-013` | 无 | 已由 001~013 的 focused evidence、014 的最终复验，以及 platform/access owner 侧 peer identity allow/reject 实证一并解阻；当前不再保留外部依赖残余跟踪 | 更新后的专项 TODO、worklog 与 `CLI-TODO-014-CLI专项最终收口.md` | 仅当所有已完成任务都有 focused 证据、Gate-CLI-05/06 明确为 PASS、Blocker/Risk/OQ 已更新，并把 peer identity 外部依赖解除状态回写清楚时完成 |

## 7. 执行顺序建议

### 7.1 顺序与并行段

| 波次 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| Wave 0 | `CLI-TODO-001 -> CLI-TODO-002` | 已完成 | 两项补设计任务已完成，命令参数 contract 与脚本化 contract 均已冻结；后续进入 topology 与 Build 任务 |
| Wave 1 | `CLI-TODO-003 ~ CLI-TODO-005`；`CLI-TODO-011` | 基线继承 + topology 先行 | 003~005 已按 focused tests + 历史交付物复核并确认代码完整实现；011 已建立 unit/contract 可发现拓扑，后续测试不再需要继续挂靠 access |
| Wave 2 | `CLI-TODO-006` 与 `CLI-TODO-009` | 已完成 | 006 已将稳定 flags/selector 字段落到 `CliCommand`，009 已将 `CliExitDecision`、`main()` 退出路径与 `CliExitCodeContractTest` 落盘；Wave 3/4 继续围绕 parser/request builder/formatter 与 JSON contract 收口 |
| Wave 3 | `CLI-TODO-007 -> CLI-TODO-008`；`CLI-TODO-010` | 007/008/010 已完成 | parser/help/version、request builder 与 human/JSON formatter 均已完成；Wave 4 转入扩展 contract 与 binary 集成门 |
| Wave 4 | `CLI-TODO-012 -> CLI-TODO-013` | 012/013 已完成 | JSON/exit code 契约与 binary/integration gate 均已锁定；Wave 5 只剩证据收口与外部依赖状态复验 |
| Wave 5 | `CLI-TODO-014` | 已完成 | 001~013 的 focused evidence 已汇总到专项 TODO、worklog 与 014 deliverable；Gate-CLI-06 已完成最终一致性复验，platform peer identity 残余也已由 platform/access owner 侧完成回收 |

### 7.2 必过门禁表

| Gate ID | 触发时机 | 通过条件 | 关联任务 | 失败回退 |
|---|---|---|---|---|
| Gate-CLI-01 | `CLI-TODO-001/002` 完成时；任何 Build PR 合入前 | 参数矩阵、socket flag 命名、JSON/exit contract 文档中无 `TBD`、无未决字段；`--socket-path` 与 access error -> CLI exit projection 口径已回链详设 | `CLI-TODO-001`、`CLI-TODO-002` | 停止后续 Build 任务，回到详设补完 |
| Gate-CLI-02 | topology 任务完成时；contract PR 合入前 | `ctest -N` 可发现现有 CLI unit tests 与 CLI contract tests，且 labels/目录不再只能依附 `tests/unit/access`；contract/access 入口已可承载真实断言 | `CLI-TODO-011` | 保留 access label 镜像，但不得把 discoverability 当作 contract 场景已补齐 |
| Gate-CLI-03 | parser/builder/formatter 完成时 | `CliDaemonCommandParserTest`、`CliIpcClientTest`、`CliIpcClientResponseTest`、`CliIpcClientUnavailableTest`、`CliDaemonOutputFormatterTest` 全通过 | `CLI-TODO-006`、`CLI-TODO-007`、`CLI-TODO-008`、`CLI-TODO-010` | 保留当前 parser/wire baseline，回滚未冻结的新 flags 行为 |
| Gate-CLI-04 | exit code 与 contract 完成时 | `AccessErrorMappingTest`、`CliJsonOutputContractTest`、`CliExitCodeContractTest` 全通过；CLI 退出码确认使用 `0/2/3/4/5/6/7` 而非 access 既有 `1/75/77` | `CLI-TODO-009`、`CLI-TODO-012` | 保持 human-only 输出，不开放稳定 `--json` 和新退出码 |
| Gate-CLI-05 | binary/integration 完成时 | `CliDaemonSocketPathIntegrationTest`、`DaemonBinaryUnarySmokeTest`、`DaemonReceiptFlowIntegrationTest`、`DaemonFailureInjectionTest`、`DaemonDiagDenyIntegrationTest` 全通过；peer identity allow/reject 端到端证据已闭环 | `CLI-TODO-013` | 保持现有最小 unary surface，不宣称超出 v1 的新 CLI 能力 |
| Gate-CLI-06 | 收口时 | 本专项 TODO、worklog、blocker、risk、OQ、统一验收命令与 peer identity 回收状态齐全 | `CLI-TODO-014` | 不进入 close-ready，仅保持 draft-ready |

### 7.3 当前 Gate 状态摘要

| Gate ID | 当前状态 | 收口说明 |
|---|---|---|
| Gate-CLI-01 | PASS | `CLI-TODO-001/002` 的参数 schema、`--socket-path` 命名与 JSON/exit contract 已冻结并回链详设。 |
| Gate-CLI-02 | PASS | `CLI-TODO-011` 已建立 CLI unit/contract discoverability，`ctest -N` 可稳定发现真实入口。 |
| Gate-CLI-03 | PASS | parser / client / formatter focused gate 已随 `CLI-TODO-006/007/008/010` 完成。 |
| Gate-CLI-04 | PASS | `AccessErrorMappingTest`、`CliJsonOutputContractTest`、`CliExitCodeContractTest` 已锁定 CLI v1 contract。 |
| Gate-CLI-05 | PASS | `CLI-TODO-013` 已通过 built CLI binary 的 sync/async、显式 ID 与 stdout/stderr 集成门；`DaemonFailureInjectionTest` 与 `DaemonDiagDenyIntegrationTest` 也已补齐 peer identity 的真实 UDS deny/allow 证据。 |
| Gate-CLI-06 | PASS | `CLI-TODO-014` 已完成专项 TODO、worklog、deliverable、统一验收命令与 peer identity 回收状态的最终对齐。 |

## 8. 阻塞项与解阻条件

| Blocker ID | 对应设计 Blocker | 阻塞内容 | 影响任务 | 解阻条件 | 最小解阻动作 |
|---|---|---|---|---|---|
| CLI-BLK-001 | CLI 详设 1.6 | 已解阻：`CLI-TODO-001` 已冻结 `run/status/cancel/help/version` 参数 schema、selector 规则、`--socket-path` 稳定命名与 version local-only 边界 | 已对 `CLI-TODO-006/007/008` 解阻；`CLI-TODO-013` 不再受命令 schema 未冻结影响 | 详设、专项 TODO 与 deliverable 三处口径一致；parser tests 可直接按冻结 contract 扩展 | 后续 Build 任务按已冻结 contract 落实现，不再回到“猜参数名”阶段 |
| CLI-BLK-002 | CLI 详设 12 未决问题 3；6.4.4 | 已解阻：`CLI-TODO-002` 已冻结 `cli.output.v1` envelope、`warnings/error` 顶层位置、`daemon_unavailable/protocol_error` 本地 disposition、stdout/stderr 归属，以及 access error facts -> CLI `0/2/3/4/5/6/7` 的投影矩阵 | 已对 `CLI-TODO-009/010/012/013` 清除脚本化 contract 不确定性；剩余阻塞转向 topology 与实现落盘 | 详设、专项 TODO 与 deliverable 三处口径一致；focused tests 仍以 access error mapping 与 formatter 现状为最近锚点 | 后续实现任务按已冻结 contract 落盘，不再回到“镜像 AgentResult 还是 CLI projection”的讨论 |
| CLI-BLK-003 | 工程规范 4.1；当前 tests 现状 | 已解阻：`CLI-TODO-011` 已建立 `tests/unit/apps/cli` discoverability，`CLI-TODO-009/010` 已将 `CliExitCodeContractTest`、`CliJsonOutputContractTest` 接成真实 contract 入口 | 已对 `CLI-TODO-009/010/012/014` 清除 topology 缺口；后续只剩扩展 JSON / exit code 场景矩阵与 binary 门禁补齐 | `ctest -N` 可稳定发现现有 CLI unit tests 与真实 contract 名称；CLI unit 不再只能依附 `tests/unit/access` | 后续 contract 扩展直接在既有真实 entrypoint 上增量补场景，不再额外补 topology |
| CLI-BLK-004 | CLI 详设 6.3.3、6.9、11、12 未决问题 1 | 已解阻：platform/access owner 已复用既有 `IIPC::describe_peer` / `UnixIpcProvider::describe_peer`，并以 `UnixIpcProviderPeerIdentityTest`、`DaemonProtocolAdapterLocalTrustedTest`、`DaemonPeerIdentityFailClosedTest`、`DaemonFailureInjectionTest`、`DaemonDiagDenyIntegrationTest` 锁定 provider -> adapter -> real UDS auth/diag allow-reject 闭环 | 已对 `CLI-TODO-013`、`CLI-TODO-014` 解阻 | 真实 UDS 正/负向授权 gate 与既有 fail-closed/provider gate 同时通过，并已回写 CLI 文档 | platform/access owner 继续维护上述 gate，CLI 侧只回链证据，不越权承担平台实现 |

说明：`DMD-TODO-031`、`DMD-TODO-036` 已清除 CLI wire contract 与 endpoint drift 风险；peer identity 仍然不是 `apps/cli` 的实现目标，但其外部闭环已由 platform/access owner 完成并回写到 Gate-CLI-05/06，不再构成 CLI 专项残余阻塞。

## 9. 验收与质量门

### 9.1 测试矩阵

| 测试层级 | 覆盖范围 | 当前状态 | 代表测试 |
|---|---|---|---|
| Unit | parser 稳定命令面、help/version、flags 作用域、socket-path 覆盖 | 已完成并纳入 close-ready gate | `CliDaemonCommandParserTest` |
| Unit | request encode / response parse / fail-closed | 已完成并纳入 close-ready gate | `CliIpcClientTest`、`CliIpcClientResponseTest`、`CliIpcClientUnavailableTest` |
| Unit | human/json formatter | 已完成并纳入 close-ready gate | `CliDaemonOutputFormatterTest` |
| Contract | `--json` 主键与 envelope 稳定性 | 已完成并纳入 close-ready gate | `CliJsonOutputContractTest` |
| Contract | exit code 稳定性 | 已完成并纳入 close-ready gate | `CliExitCodeContractTest` |
| Integration | default endpoint / explicit endpoint override | 已有基线 | `CliDaemonSocketPathIntegrationTest` |
| Integration | built CLI -> built daemon unary | 已完成，已锁定 stdout/stderr 与显式 IDs | `DaemonBinaryUnarySmokeTest` |
| Integration | accepted_async / receipt / cancel | 已完成，built CLI async gate 已锁定 receipt/status binary surface | `DaemonReceiptFlowIntegrationTest` |
| Integration | peer identity auth deny / diag authorization 端到端闭环 | 已完成并纳入 close-ready gate | `DaemonFailureInjectionTest`、`DaemonDiagDenyIntegrationTest` |
| Regression | access 错误 code/domain 与既有协议映射回归证据 | 已有基线 | `AccessErrorMappingTest` |

### 9.2 Focused 验收命令

1. parser / client / formatter focused gate：`ctest --test-dir build-ci -R "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest" --output-on-failure`
2. contract gate：`ctest --test-dir build-ci -R "AccessErrorMappingTest|CliJsonOutputContractTest|CliExitCodeContractTest" --output-on-failure`
3. integration gate：`ctest --test-dir build-ci -R "CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest|DaemonReceiptFlowIntegrationTest" --output-on-failure`
4. discoverability gate：`ctest --test-dir build-ci -N | rg "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|CliJsonOutputContractTest|CliExitCodeContractTest"`
5. peer identity closure gate：`ctest --test-dir build-ci -R "UnixIpcProviderPeerIdentityTest|DaemonProtocolAdapterLocalTrustedTest|DaemonPeerIdentityFailClosedTest|DaemonFailureInjectionTest|DaemonDiagDenyIntegrationTest" --output-on-failure`

### 9.3 统一验收命令建议

CLI 专项完成态必须提供一条 one-shot 命令覆盖 build、discoverability、unit、contract、integration focused gate 与 peer identity closure gate。当前推荐命令如下；`CLI-TODO-014` 已以该命令作为 close-ready gate 的统一入口，并把结果摘要回写到 deliverable / worklog。

```bash
cmake --build build-ci --target dasall-cli dasall-daemon dasall_access_daemon_diag_deny_integration_test dasall_access_daemon_failure_injection_integration_test dasall_access_daemon_protocol_adapter_local_trusted_unit_test dasall_access_daemon_peer_identity_fail_closed_unit_test dasall_unix_ipc_provider_peer_identity_unit_test \
   && ctest --test-dir build-ci -N \
      | rg "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|CliJsonOutputContractTest|CliExitCodeContractTest|CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest|DaemonReceiptFlowIntegrationTest|UnixIpcProviderPeerIdentityTest|DaemonProtocolAdapterLocalTrustedTest|DaemonPeerIdentityFailClosedTest|DaemonFailureInjectionTest|DaemonDiagDenyIntegrationTest" \
   && ctest --test-dir build-ci -R "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|AccessErrorMappingTest|CliJsonOutputContractTest|CliExitCodeContractTest|CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest|DaemonReceiptFlowIntegrationTest|UnixIpcProviderPeerIdentityTest|DaemonProtocolAdapterLocalTrustedTest|DaemonPeerIdentityFailClosedTest|DaemonFailureInjectionTest|DaemonDiagDenyIntegrationTest" --output-on-failure
```

统一验收通过定义：所有上述测试均被发现并通过；peer identity 相关 provider、adapter、fail-closed 与 real UDS allow/reject 证据必须同时存在，方可视为 CLI 专项不再保留 `CLI-BLK-004` / `CLI-RISK-007` / `CLI-OQ-001` 残余项。

## 10. 风险与回退策略

| Risk ID | 对应设计 Risk | 风险内容 | 影响面 | 回退策略 |
|---|---|---|---|---|
| CLI-RISK-001 | CLI 详设 1.6 | 参数 schema 冻结若直接照当前最小 positional 代码推进，后续 CLI man page 与脚本兼容将返工 | parser、help、version、binary smoke | 先完成 `CLI-TODO-001`；若评审未通过，保留当前最小 positional surface，不合并新 flags |
| CLI-RISK-002 | CLI 详设 6.4.2、12 未决问题 3 | 若后续实现直接暴露 raw `AgentResult` / `UdsResponseFrame`，会破坏已冻结的 CLI projection envelope | formatter、contract tests、自动化脚本 | 已由 `CLI-TODO-002` 固定 `cli.output.v1` 主键与 `result/error/warnings` 位置；实现阶段必须以 `CliJsonOutputContractTest` 锁定 |
| CLI-RISK-003 | CLI 详设 6.4.4 | 若后续实现直接复用 access 既有 `1/75/77` 或继续返回旧 `0/1`，会违背 CLI v1 exit family | CLI 自动化集成、binary smoke | 已由 `CLI-TODO-002` 固定 `0/2/3/4/5/6/7` 与投影顺序；实现阶段必须以 `CliExitCodeContractTest` 锁定 |
| CLI-RISK-004 | 工程规范 4.1；当前 tests 现状 | 测试拓扑从 `tests/unit/access` 收敛到 `tests/unit/apps/cli` 时，容易导致 discoverability 断裂 | CI / `ctest -N` / 维护入口 | 过渡期允许镜像注册，待 `ctest -N` 稳定后再移除旧接线 |
| CLI-RISK-005 | CLI 详设 6.4.1、6.4.5 | 若后续实现回退到 daemon 自动探测或 version 触网，会把 CLI version 重新做成伪运维入口 | `help/version` 文案、用户预期 | 已由 `CLI-TODO-001` 冻结 `version` local-only；后续扩展只能走显式加法 surface |
| CLI-RISK-006 | CLI 详设 6.4.3；当前实现 | 若后续实现重新接受公开 `--socket`，会破坏脚本兼容与 help 文案一致性 | parser、usage、integration smoke、文档 | 已由 `CLI-TODO-001` 冻结 `--socket-path` 为唯一稳定命名；`--socket` 不得回归为公开 alias |
| CLI-RISK-007 | CLI 详设 6.3.3、6.9、11 | 已回收：peer identity allow/reject 端到端证据已由 platform/access owner 补齐，当前仅需防止后续回归时再次把该链路从 close-ready gate 中移除 | binary integration、security evidence、Gate-CLI-05/06 | 将 `UnixIpcProviderPeerIdentityTest`、`DaemonProtocolAdapterLocalTrustedTest`、`DaemonPeerIdentityFailClosedTest`、`DaemonFailureInjectionTest`、`DaemonDiagDenyIntegrationTest` 持续纳入 close-ready gate |
| CLI-RISK-008 | 工程规范 6/7；专项 TODO 基线 | 证据若集中到最后一次性回写，容易丢失 focused 命令与 blocker 演进证据 | worklog、deliverables、Gate 复验 | 每个 TODO 完成时即时回写 focused evidence；`CLI-TODO-014` 只做最终复验与残余风险收口 |

## 11. 可行性结论

1. 本专项 TODO 已完成从补设计冻结、Build 落地、contract gate、binary/integration gate 到最终证据收口的整轮闭环；`CLI-TODO-001` 至 `CLI-TODO-014` 当前均为 Done。
2. CLI 客户端用户面当前已达到 close-ready：稳定命令 schema、`CliRequestBuilder`、`CliExitDecision`、human/JSON formatter、JSON/exit code contract 与 built binary 的 sync/async 表面行为均已有二值证据。
3. 当前不再存在 CLI 专项内部或外部对其 close-ready 的阻塞；此前 `CLI-BLK-004` / `CLI-RISK-007` / `CLI-OQ-001` 所指向的 peer identity 外部依赖已由 platform/access owner 完成闭环并回写。
4. 因此本专项后续动作不再是继续解 peer identity 阻塞，而是把相关 gate 作为回归基线维护；若新增 CLI 用户面能力，应以新的专项 TODO 行进入。
5. CLI 专项最终结论仍然是“CLI 客户端面完成并 close-ready”，同时 peer identity 相关 local trusted、auth deny 与 diag 授权端到端证据也已完成收口。

## 12. 未决问题处置表

| OQ ID | 来源 | 未决问题 | 处置策略 | 对应任务 / 外部依赖 | 完成判定 |
|---|---|---|---|---|---|
| CLI-OQ-001 | CLI 详设 12.1 | platform peer identity 能力最终扩展 IIPC 还是 side-interface | 已收口为复用既有 `IIPC::describe_peer` -> `UnixIpcProvider::describe_peer` -> `DaemonProtocolAdapter::describe_local_peer_uid_fact()` 路径；CLI 专项只回链实证，不新增 side-interface | `DMD-TODO-012`、`DMD-TODO-027`、`UnixIpcProviderPeerIdentityTest`、`DaemonDiagDenyIntegrationTest` | 已完成：provider/unit、adapter/unit、fail-closed integration 与 real UDS allow/reject integration 全部通过，并已回写 CLI 文档 |
| CLI-OQ-002 | CLI 详设 12.2 | ping 是否直接暴露 `profile_id` 与 build hash | 已在 `CLI-TODO-001` 冻结为保守口径：ping 继续优先暴露 schema/readiness 摘要，`version` 只报告 CLI 本地 build metadata；daemon 对比保持后续加法扩展 | `CLI-TODO-001`、`CLI-TODO-010` | ping/version 输出字段在详设与 TODO 中可二值核对，formatter tests 覆盖 |
| CLI-OQ-003 | CLI 详设 12.3 | `--json` 是镜像 `AgentResult` 还是 CLI projection | 已在 `CLI-TODO-002` 冻结为 CLI projection envelope；固定 `cli.output.v1`、顶层 `result/error/warnings`、本地 `daemon_unavailable/protocol_error` disposition，并禁止 raw `AgentResult` 直出 | `CLI-TODO-002`、`CLI-TODO-010`、`CLI-TODO-012` | JSON 主键、error/warnings、disposition、request_id/trace_id、receipt/result 投影均有 contract tests |
| CLI-OQ-004 | CLI 详设 12.4 | v1 是否允许 CLI 在开发场景尝试拉起 daemon | 已收敛为否；CLI v1 不自动拉起 daemon，`version` 也不隐式触达 daemon，避免客户端承担 daemon lifecycle | `CLI-TODO-001`、`CLI-TODO-002`、`CLI-TODO-009`、`CLI-TODO-013` | daemon unavailable contract / integration 覆盖 exit 3，文档不宣称 auto-start |
| CLI-OQ-005 | CLI 详设 12.5 | diag 是否开放 log query artifact_ref | 延后；v1 仅保留 `diag health`、`diag queue`、`diag threads` 三个受控只读方向，是否开放取决于 infra/diagnostics 实现节奏 | `CLI-TODO-001`、`DMD-TODO-020`、infra/diagnostics TODO | parser/help 明确 diag 子命令范围；未授权与未启用路径 fail-closed |
| CLI-OQ-006 | 本次评估新增 | `--socket` 与 `--socket-path` 命名漂移 | 已在 `CLI-TODO-001` 回链详设；v1 稳定用户面只保留 `--socket-path`，`--socket` 不再作为公开 alias | `CLI-TODO-001`、`CLI-RISK-006` | 详设、TODO、usage、parser tests 与 integration smoke 使用同一主命名 |
| CLI-OQ-007 | 本次评估新增 | access 既有 `cli_exit_code` 与 CLI v1 `0/2/3/4/5/6/7` 的关系 | 已在 `CLI-TODO-002` 冻结为“access error code/domain/retryable 输入 + CLI local projection 输出”；`map_access_error().cli_exit_code` 不得进入最终用户面契约 | `CLI-TODO-002`、`CLI-TODO-009`、`CLI-TODO-012` | `CliExitCodeContractTest` 锁定 CLI v1 矩阵，`AccessErrorMappingTest` 仅作为 access error mapping regression |