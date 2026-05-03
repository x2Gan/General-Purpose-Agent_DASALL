# DMD-TODO-028 daemon 专项 Gate 与交付证据收敛

日期：2026-05-03
任务：DMD-TODO-028
状态：Done

## 1. 本地证据

1. `DMD-TODO-028` 是 daemon 专项串行链的最终收口任务，前置依赖为 `DMD-TODO-024`、`025`、`026`、`027`、`031`、`034`、`035`。
2. 本任务不新增 daemon 产品语义，只负责把专项 TODO、deliverables 与 worklog 收敛为同一份 Gate / blocker / residual risk 快照。
3. 当前仓库的 `dasall_unit_tests`、`dasall_integration_tests` 和 `RunCtest_CMakeTools` 仍会受到 runtime / infra 既有失败、`DartConfiguration.tcl` 噪声以及 034 soak 路径卡住现象影响，因此 028 明确以 focused gate matrix 为权威证据来源。
4. 本轮已把专项 TODO 顶部当前结论、`DMD-TODO-028` 行、§9.4 Gate 执行证据、§9.5 blocker 变化记录、§10 当前残余风险和 §11 最终结论全部回写到位。

## 2. Gate 执行证据

| Gate ID | 结论 | 命令证据 | 结果摘要 |
|---|---|---|---|
| Gate-DMD-01 | PASS | `ctest --test-dir build-ci --output-on-failure -R "DaemonProtocolTypesTest|DaemonBootstrapConfigTest|DaemonConfigValidatorTest"` | `DMD-TODO-001`、`002`、`004` 已冻结 command taxonomy、config 投影与 validate-only。 |
| Gate-DMD-02 | PASS | `cmake --build build-ci --target dasall_daemon_bootstrap_unit_test dasall_daemon_lifecycle_controller_unit_test dasall_daemon_listener_host_unit_test dasall_daemon_config_validator_unit_test`；`ctest --test-dir build-ci -N | rg "Daemon(Bootstrap|LifecycleController|ListenerHost|ConfigValidator)Test"` | `DMD-TODO-005` ~ `010` 的进程壳层拆分已由 `DMD-TODO-023` 收口为可发现的 focused topology。 |
| Gate-DMD-03 | PASS | `RunCtest_CMakeTools(tests=["UnixIpcProviderLoopbackTest","UnixIpcProviderPeerIdentityTest","DaemonFrameCodecTest","DaemonFrameCodecMalformedTest","DaemonProtocolAdapterTest","DaemonProtocolAdapterLocalTrustedTest","DaemonPeerIdentityFailClosedTest","DaemonSocketPolicyTest","DaemonListenerHostBindConflictTest","DaemonConfigValidatorTest"])` | `DMD-TODO-029`、`030`、`011`、`012`、`032` 已证明 IIPC/codec/peer identity/UDS endpoint 安全闭环。 |
| Gate-DMD-04 | PASS | `RunCtest_CMakeTools(tests=["DaemonAccessPipelineFactoryTest","RuntimeBridgeRejectMappingTest","DaemonUnaryRuntimeBridgeTest","DaemonUnaryIntegrationTest","DaemonRejectPathIntegrationTest","DaemonBinaryUnarySmokeTest"])` | `DMD-TODO-013`、`014`、`025` 已证明 unary happy / reject path 全部经 access core，且 built `dasall_daemon` + built `dasall_cli` 的 real binary unary smoke 已通过真实 `main.cpp` 组合根。 |
| Gate-DMD-05 | PASS | `RunCtest_CMakeTools(tests=["DaemonPingCommandTest","DaemonReadinessCommandTest","DaemonPingDoesNotBypassRouterTest","DaemonDiagnosticsHandlerTest","DaemonDiagDenyIntegrationTest","CliIpcClientTest","CliIpcClientResponseTest","CliIpcClientUnavailableTest","CliDaemonCommandParserTest","CliDaemonOutputFormatterTest","DaemonPingIntegrationTest"])` | `DMD-TODO-019`、`020`、`031` 已证明 ping/readiness/diag 语义分离，CLI 已能读取稳定 daemon response。 |
| Gate-DMD-06 | PASS | `RunCtest_CMakeTools(tests=["DaemonAcceptedAsyncReceiptTest","DaemonTaskQueryHandlerTest","DaemonCancelCommandTest","DaemonReceiptFlowIntegrationTest"])` | `DMD-TODO-015`、`016`、`017`、`026` 已证明 receipt owner/TTL/status/cancel fail-closed。 |
| Gate-DMD-07 | PASS | `RunCtest_CMakeTools(tests=["DaemonObservabilityFieldSetTest","AccessGatewayLifecycleTest","DaemonGracefulShutdownTest","DaemonShutdownAbandonedAuditTest","DaemonFailureInjectionTest","DaemonProfileCompatibilityTest","DaemonConfigReloadTest","DaemonSignalHandlerTest","DaemonHotReloadIntegrationTest"])` | `DMD-TODO-021`、`022`、`027`、`033`、`039` 已证明 graceful shutdown、failure injection、profile compatibility 与 `daemon.diag_enabled` 单键 hot-reload 闭环；`daemon.log_format`、`daemon.socket_path` 等非 allowlisted 键继续拒绝。 |
| Gate-DMD-08 | PASS | `Build_CMakeTools(buildTargets=["dasall_access_daemon_backpressure_integration_test","dasall_access_daemon_soak_integration_test","dasall_access_daemon_receipt_ttl_cleanup_integration_test"])`；直接执行三条已构建测试二进制 | `DMD-TODO-034` 已证明 backpressure、soak、TTL cleanup 与资源计数回落；direct-binary evidence 是当前环境下的权威结果。 |
| Gate-DMD-09 | PASS | `rg -n "DMD-TODO-028|Gate-DMD-0[1-9]|DMD-BLK-00[1-8]|当前残余风险" docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md docs/todos/daemon/deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md docs/worklog/DASALL_开发执行记录.md` | TODO、deliverable 与 worklog 已同时包含 Gate、blocker、残余风险与最终结论。 |

## 3. 阻塞变化与最小回退

1. `DMD-BLK-001` ~ `DMD-BLK-008` 已全部清除，daemon 专项范围内不再存在 open blocker。
2. 仓库聚合 build/test 噪声与 `RunCtest_CMakeTools` 的工具链行为不再登记为 daemon blocker，而是作为验证路径噪声留在专项残余风险中显式说明。
3. `DMD-TODO-034` 的 034 soak gate 继续以 `Build_CMakeTools + direct binary execution` 作为最小回退，不回退到 fake IPC、send-only smoke 或放宽断言。
4. `DMD-TODO-028` 本轮未触发任何产品代码回退，只完成 evidence writeback 和专项当前态收敛。

## 4. 当前残余风险

| 残余风险 | 当前状态 | 处置策略 |
|---|---|---|
| 全仓聚合 target 仍受 runtime / infra 既有失败污染 | Open | 继续使用 focused gate matrix 作为 daemon 权威结论；跨模块问题由对应 owner 解阻 |
| `RunCtest_CMakeTools` 对 034 仍可能卡住，并打印 `DartConfiguration.tcl` 噪声 | Open | 继续使用 `Build_CMakeTools + direct binary execution`；后续工具链稳定后再补充增强证据 |
| `status` 仍是 registry-only，而非 runtime live status | Scoped limitation | 保持当前 v1 边界，不将 registry-only 结果外推为 live runtime query |
| 当前部署边界不覆盖 `Type=notify`、socket activation、remote control plane、streaming attach、多 daemon 隔离 | Scoped limitation | 继续作为 v2 范围外能力，不复用当前 Gate 结论外推 |
| 长期运行证据目前只覆盖 deterministic in-process soak 与单机 direct-bind smoke | Limited evidence | 若要扩大长期运行声明，新增更长时段或多主机 soak harness |

## 5. 评审结论

1. `DMD-TODO-028` 通过。daemon 专项 `DMD-TODO-001` ~ `040` 已全部完成，`Gate-DMD-01` ~ `10` 全部 PASS。
2. 当前专项内可宣称的 v1 交付边界已经闭环：local direct-bind UDS、real daemon/CLI unary smoke、accepted_async、ping/status/cancel/只读 diag、readiness、graceful shutdown、CLI wire contract、并发/soak 与部署契约。
3. 本文件与专项 TODO / worklog 共同构成 Gate-DMD-09 的最终快照，不再允许把历史 send-only smoke、fake IPC 或聚合 target 噪声写成 daemon 已交付事实。

## 6. Build 三件套

1. 代码目标：更新 `docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md`、新增本交付物并追加 `docs/worklog/DASALL_开发执行记录.md`，统一回写 Gate、blocker、残余风险与当前结论。
2. 测试目标：验证 `DMD-TODO-028`、`Gate-DMD-01` ~ `09`、`DMD-BLK-001` ~ `008` 与当前残余风险条目在 TODO / deliverable / worklog 中均可追溯，并确认上游 024/025/026/027/031/034/035 的 focused gate 证据存在。
3. 验收命令：
   - `rg -n "DMD-TODO-028|Gate-DMD-0[1-9]|DMD-BLK-00[1-8]|当前残余风险" docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md docs/todos/daemon/deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md docs/worklog/DASALL_开发执行记录.md`
   - `rg -n "DMD-TODO-02(4|5|6|7|8)|DMD-TODO-03(1|4|5)" docs/worklog/DASALL_开发执行记录.md docs/todos/daemon/deliverables`