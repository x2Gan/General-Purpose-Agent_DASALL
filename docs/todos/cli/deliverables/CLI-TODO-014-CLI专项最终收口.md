# CLI-TODO-014 CLI 专项最终收口

日期：2026-05-05
状态：Done
任务来源：`docs/todos/cli/DASALL-cli本地控制面专项TODO.md`

## 1. 收口目标

`CLI-TODO-014` 只负责 CLI 专项的持续证据回写与最终 close-ready 收口，不新增 parser、request builder、formatter、contract 或 integration 行为。

本轮收口回链以下冻结依据：

1. `docs/architecture/DASALL-cli本地控制面详细设计.md` 中对 CLI v1 命令面、`--json` envelope、`CliExitDecision`、stdout/stderr 归属、focused gate 与外部依赖边界的冻结定义。
2. `docs/todos/cli/DASALL-cli本地控制面专项TODO.md` 中 `CLI-TODO-001` 至 `CLI-TODO-013` 的已完成实现、Gate-CLI-01 至 Gate-CLI-05 的 focused 证据，以及 `CLI-BLK-004` 对 platform peer identity 外部依赖的历史跟踪与本轮解除回写。
3. `docs/worklog/DASALL_开发执行记录.md` 中 `#538`、`#539`、`#540`、`#541` 及更早 CLI 任务记录，作为实现与验证证据链。

## 2. 收口结论

CLI 专项当前可确认进入 close-ready 状态，结论边界如下：

1. `apps/cli` 纯客户端依赖方向、稳定命令 schema、`CliRequestBuilder`、`CliExitDecision`、human/JSON formatter、JSON/exit code contract，以及 built `dasall-cli` 的 sync/async 显式 ID 与 stdout/stderr binary gate 已全部落地。
2. CLI 用户面 contract 已由 unit、contract、integration 三层 focused gate 锁定，不再依赖手工终端探测或文档占位。
3. CLI 专项仍然只声明 `apps/cli` 客户端面完成并 close-ready，不把 platform/access 的实现职责搬入 CLI；但此前作为外部依赖跟踪的 peer identity 链路现已由 platform/access owner 侧通过 provider、adapter、fail-closed 与 real UDS allow/reject gate 完成收口，因此不再保留 `CLI-BLK-004` / `CLI-RISK-007` / `CLI-OQ-001` 残余。

## 3. 已完成任务证据索引

| 任务 | 结论 | 主要证据 |
|---|---|---|
| `CLI-TODO-001` | Done | `docs/todos/cli/deliverables/CLI-TODO-001-CLI命令schema与usage文案冻结.md` |
| `CLI-TODO-002` | Done | `docs/todos/cli/deliverables/CLI-TODO-002-CLI-JSON-envelope与CliExitDecision冻结.md` |
| `CLI-TODO-003` | Done | `docs/todos/access/deliverables/ACC-TODO-025-CliIpcClient与cli纯客户端组合根收敛.md`、`docs/todos/access/deliverables/ACC-TODO-038-CLI依赖方向纠正与UDS路径收敛.md` |
| `CLI-TODO-004` | Done | `docs/todos/daemon/deliverables/DMD-TODO-031-CLI-daemon-wire-contract收敛.md` |
| `CLI-TODO-005` | Done | `docs/todos/daemon/deliverables/DMD-TODO-036-daemon-cli-endpoint统一与覆盖收敛.md` |
| `CLI-TODO-006` | Done | `docs/worklog/DASALL_开发执行记录.md` 中 CLI parser/flag focused 记录 |
| `CLI-TODO-007` | Done | `docs/worklog/DASALL_开发执行记录.md` 中 help/version focused 记录 |
| `CLI-TODO-008` | Done | `docs/worklog/DASALL_开发执行记录.md` 中 request builder/frame shaping focused 记录 |
| `CLI-TODO-009` | Done | `docs/worklog/DASALL_开发执行记录.md` 中 `CliExitDecision` focused 记录 |
| `CLI-TODO-010` | Done | `docs/worklog/DASALL_开发执行记录.md` 中 `#537` |
| `CLI-TODO-011` | Done | `docs/todos/cli/deliverables/CLI-TODO-011-CLI测试topology与discoverability接线.md` |
| `CLI-TODO-012` | Done | `docs/worklog/DASALL_开发执行记录.md` 中 `#538` |
| `CLI-TODO-013` | Done | `docs/worklog/DASALL_开发执行记录.md` 中 `#539` |

## 4. Gate / Blocker / Risk / OQ 收口状态

### 4.1 Gate

| Gate | 状态 | 收口说明 |
|---|---|---|
| `Gate-CLI-01` | PASS | 参数 schema、`--socket-path` 命名、JSON/exit contract 已冻结并回链详设。 |
| `Gate-CLI-02` | PASS | CLI unit/contract discoverability 已接通。 |
| `Gate-CLI-03` | PASS | parser/client/formatter focused tests 已通过。 |
| `Gate-CLI-04` | PASS | JSON/exit code contract 已通过并锁定 CLI v1 `0/2/3/4/5/6/7`。 |
| `Gate-CLI-05` | PASS | built CLI binary 的 sync/async、显式 ID 与 stdout/stderr 集成门已通过。 |
| `Gate-CLI-06` | PASS | 本文档、专项 TODO、worklog、统一验收命令与外部依赖状态已完成最终对齐。 |

### 4.2 Blocker / Risk / OQ

1. `CLI-BLK-001`、`CLI-BLK-002`、`CLI-BLK-003` 已解阻，不再阻塞 CLI 客户端面 close-ready。
2. `CLI-BLK-004` 已解阻：platform/access owner 已复用既有 `IIPC::describe_peer` / `UnixIpcProvider::describe_peer`，并以 `UnixIpcProviderPeerIdentityTest`、`DaemonProtocolAdapterLocalTrustedTest`、`DaemonPeerIdentityFailClosedTest`、`DaemonFailureInjectionTest`、`DaemonDiagDenyIntegrationTest` 锁定 provider -> adapter -> real UDS auth/diag allow-reject 闭环。
3. `CLI-RISK-001` 至 `CLI-RISK-006`、`CLI-RISK-008` 已随 `CLI-TODO-001` 至 `CLI-TODO-013` 的落地与证据回写收敛到受控状态。
4. `CLI-RISK-007` 已回收为回归性风险：当前不再是 closeout 残余，只需持续把 peer identity gate 保持在 close-ready 验收矩阵里。
5. `CLI-OQ-001` 已收口为沿用既有 `IIPC::describe_peer` 路径，不新增 side-interface；`CLI-OQ-002` 至 `CLI-OQ-007` 已由详设回链与自动化门禁锁定，不再构成本专项内未决项。

## 5. 统一验收与结果摘要

本专项 close-ready 统一验收命令沿用专项 TODO 中的一条 one-shot focused gate，并补入 peer identity closure gate：

```bash
cmake --build build-ci --target dasall-cli dasall-daemon dasall_access_daemon_diag_deny_integration_test dasall_access_daemon_failure_injection_integration_test dasall_access_daemon_protocol_adapter_local_trusted_unit_test dasall_access_daemon_peer_identity_fail_closed_unit_test dasall_unix_ipc_provider_peer_identity_unit_test \
  && ctest --test-dir build-ci -N \
    | rg "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|CliJsonOutputContractTest|CliExitCodeContractTest|CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest|DaemonReceiptFlowIntegrationTest|UnixIpcProviderPeerIdentityTest|DaemonProtocolAdapterLocalTrustedTest|DaemonPeerIdentityFailClosedTest|DaemonFailureInjectionTest|DaemonDiagDenyIntegrationTest" \
  && ctest --test-dir build-ci -R "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonOutputFormatterTest|AccessErrorMappingTest|CliJsonOutputContractTest|CliExitCodeContractTest|CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest|DaemonReceiptFlowIntegrationTest|UnixIpcProviderPeerIdentityTest|DaemonProtocolAdapterLocalTrustedTest|DaemonPeerIdentityFailClosedTest|DaemonFailureInjectionTest|DaemonDiagDenyIntegrationTest" --output-on-failure
```

收口判定规则：

1. discoverability、unit、contract、integration focused gate 全部通过。
2. TODO 与 worklog 能回溯每个 CLI 任务的 focused evidence。
3. peer identity 相关 provider、adapter、fail-closed 与 real UDS allow/reject 证据同时通过，并已将 `CLI-BLK-004` / `CLI-RISK-007` / `CLI-OQ-001` 从残余项中回收。

## 6. 最终边界声明

`CLI-TODO-014` 完成后，CLI 专项的后续动作不再是继续补 CLI 客户端代码，而是：

1. platform/access owner 已完成 peer identity 闭环并更新 `CLI-BLK-004`、`CLI-RISK-007`、`CLI-OQ-001` 状态；后续只需维护相应 gate，避免回归到手写 `peer_ref` 的伪闭环。
2. 若 CLI 用户面再有新增能力，必须以新的专项 TODO 行进入，不得复用 014 的 closeout 行作为实现入口。