# FULLINT-TODO-005 全量业务链 discoverability verifier 清单

日期：2026-05-11
任务来源：`docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md`
任务目标：新增全量业务链 discoverability verifier 清单，确保 BC-01~BC-17 每条链路至少有一个当前代码可发现的代表测试，或被显式标记为 missing gate。

更新注记（2026-05-12）：本交付物记录的是 `FULLINT-TODO-005` 完成当日的 discoverability 基线。BC-17 当时以 `BC-17:multi_agent_coordinator_runtime_gate_missing` 作为 explicit missing gate 收口；该状态已被 `FULLINT-TODO-018` 接入的 `MultiAgentDisabledByProfileIntegrationTest`、`MultiAgentCoordinatorPipelineTest`、`MultiAgentRecoveryFoldIntegrationTest` 和 `dasall_full_business_chain_discoverability` 新结果所覆盖，不应再视为当前状态。

## 1. 研究证据

### 1.1 本地代码证据

1. `tests/VerifySystemGateDiscoverability.cmake` 已存在并被 `dasall_gate_int_09`、`dasall_gate_int_10`、`dasall_packaging_preflight_tests` 复用；本轮扩展为同时校验 expected tests 与 explicit missing gate marker。
2. `tests/CMakeLists.txt` 已注册 `dasall_gate_int_08`、`dasall_gate_int_09`、`dasall_gate_int_10` 与 `dasall_packaging_preflight_tests`；本轮新增 `dasall_full_business_chain_discoverability`，只执行 `ctest -N` 可发现性，不吞并 installed-package/qemu owner。
3. `tests/integration/access/CMakeLists.txt` 当前存在 CLI/daemon submit、HTTP gateway submit、async receipt、readiness、daemon/gateway app-binary smoke、startup diagnostics 等代表 CTest 名称。
4. `tests/integration/agent_loop`、`knowledge`、`memory`、`tools`、`services`、`llm`、`profiles`、`infra` 均有当前 CTest 注册的代表入口。
5. `multi_agent/` 当前仍只有 placeholder 骨架；未发现 Runtime 装配的 `MultiAgentCoordinator` Gate，因此 BC-17 必须显式标记为 missing gate。

### 1.2 安装态运行证据

本轮不以历史文档绿灯代替实际包运行。已在当前工作机采集：

1. `command -v dasall` -> `/usr/bin/dasall`。
2. `dpkg-query -W -f='${binary:Package} ${Version} ${Status}\n' 'dasall*'` -> `dasall`、`dasall-cli`、`dasall-common`、`dasall-daemon` 均为 `0.1.0-1 install ok installed`。
3. `systemctl is-active dasall-daemon.service` -> `active`；`systemctl is-enabled dasall-daemon.service` -> `enabled`。
4. `sudo -n dasall ping --json` -> `disposition=completed`、`task_completed=true`、daemon payload `readiness=READY`。
5. `sudo -n dasall readiness --json` -> `disposition=completed`、`state=READY`、`lifecycle_ready=true`、`listener_ready=true`、`gateway_ready=true`、`bridge_reachable=true`。
6. `sudo -n dasall status receipt:missing token local://uid/0 --json` -> exit `5`，`reason=status_missing`。
7. `sudo -n dasall cancel receipt:missing token local://uid/0 --json` -> exit `5`，`reason=cancel_missing`。
8. `sudo -n dasall diag health --json` -> exit `4`，`reason=diag_disabled`。

边界：以上安装态证据证明当前本机 rootful local control-plane 存在且可响应；本任务仍只新增 build-tree discoverability verifier，不宣称 installed-package/qemu production ready。

### 1.3 外部参考

CMake 官方 `ctest(1)` 手册说明：`ctest -N` 可列出测试而不执行，`-R` 可按测试名正则筛选，`-L` 可按标签筛选，`--show-only=json-v1` 可输出机器可读模型。本轮采用 `ctest -N -R` 作为 discoverability verifier 的低副作用入口，并保留已有 label-based 校验能力。

## 2. Design 原子清单

| 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结全量业务链代表测试清单 | `BusinessChainIntegrationMatrix.md`、当前 CMake/CTest 注册 | `DASALL_FULL_BUSINESS_CHAIN_DISCOVERABILITY_TEST_NAMES` | BC-01~BC-16 均有至少一个可发现测试 |
| D2 | 显式表达 missing gate | `multi_agent/` 当前 placeholder 状态 | `BC-17:multi_agent_coordinator_runtime_gate_missing` | missing marker 由 verifier 校验格式 |
| D3 | 保持 Gate 分层 | `SystemIntegrationGateMatrix.md` | `dasall_full_business_chain_discoverability` 仅做 `ctest -N` | 不运行 installed-package/qemu、不吞并 `Gate-INT-10` |

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收方式 |
|---|---|---|
| 全量业务链 discoverability 使用当前 CTest 注册事实 | `tests/CMakeLists.txt` 新增 target 与 test name list | `Build_CMakeTools(buildTargets=["dasall_full_business_chain_discoverability"])` |
| missing gate 必须机器可读 | `tests/VerifySystemGateDiscoverability.cmake` 新增 `EXPECTED_MISSING_GATES_CSV` 校验 | 构建 target 时输出 missing marker 校验通过 |
| 交付物必须记录 actual package 运行边界 | 本文件与 TODO/worklog 回写 | `rg -n "dasall_full_business_chain_discoverability|BC-17:multi_agent|status_missing|diag_disabled|FULLINT-TODO-005" ...` |

D Gate：PASS。范围清晰、Build 三件套已锁定、未扩张到 multi_agent 实现或 installed-package qemu。

## 4. Build 清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| B1 | `tests/VerifySystemGateDiscoverability.cmake` | missing marker 格式负/正路径由 CMake target 参数覆盖 | `Build_CMakeTools(buildTargets=["dasall_full_business_chain_discoverability"])` |
| B2 | `tests/CMakeLists.txt` | `dasall_full_business_chain_discoverability` target | `Build_CMakeTools(buildTargets=["dasall_full_business_chain_discoverability"])` |
| B3 | 本交付物、专项 TODO、worklog | 文档一致性检索 | `rg -n "dasall_full_business_chain_discoverability|BC-17:multi_agent|FULLINT-TODO-005" tests docs/todos/integration docs/worklog/DASALL_开发执行记录.md` |

## 5. 业务链 discoverability 清单

| BC | 代表 CTest / 标记 | 当前结论 |
|---|---|---|
| BC-01 | `CliDaemonSubmitIntegrationTest`、`DaemonPingIntegrationTest`、`CliDaemonSocketPathIntegrationTest` | 可发现 |
| BC-02 | `HttpGatewaySubmitIntegrationTest`、`GatewayBinaryUnarySmokeTest`、`GatewayBinaryMissingBackendRegressionTest` | 可发现 |
| BC-03 | `AccessGatewayPipelineIntegrationTest`、`AccessPolicyBackendUnavailableIntegrationTest`、`AccessProfileCompatibilityTest` | 可发现 |
| BC-04 | `AccessAsyncReceiptQueryCancelIntegrationTest` | 可发现 |
| BC-05 | `RuntimeUnaryIntegrationTest`、`DaemonRuntimeLiveDependencyCompositionTest` | 可发现 |
| BC-06 | `CognitionRuntimeIntegrationTest` | 可发现 |
| BC-07 | `LLMSubsystemSmokeIntegrationTest` | 可发现 |
| BC-08 | `KnowledgeEvidencePreservationTest`、`dasall_knowledge_retrieval_smoke_integration_test` | 可发现；installed-package 正向入口仍归 FULLINT-TODO-014 |
| BC-09 | `MemoryContextAssembleIntegrationTest` | 可发现 |
| BC-10 | `MemoryWritebackIntegrationTest` | 可发现 |
| BC-11 | `ToolServicesSmokeIntegrationTest` | 可发现 |
| BC-12 | `CapabilityServicesSmokeIntegrationTest` | 可发现 |
| BC-13 | `InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest`、`DaemonStartupDiagnosticsTest`、`GatewayStartupDiagnosticsTest`、`AccessHealthReadinessIntegrationTest` | 可发现 |
| BC-14 | `ProfilesBuildRuntimeIntegrationTest`、`RuntimeProfileCompatibilityTest`、`AccessProfileCompatibilityTest` | 可发现 |
| BC-15 | `RuntimeRecoveryContextIntegrationTest`、`RuntimeCheckpointReplayRegressionTest` | 可发现 |
| BC-16 | `CliJsonOutputContractTest`、`CliExitCodeContractTest`、`GatewayBinaryMissingBackendRegressionTest`、`CliDaemonSocketPathIntegrationTest` | 可发现；qemu/lintian 仍归 release runner |
| BC-17 | `BC-17:multi_agent_coordinator_runtime_gate_missing` | missing gate 已显式标记；未实现 coordinator 前不得宣称 ready |

## 6. Build 合规复核

1. 代码注释：本轮 CMake 脚本变更为自解释变量与错误信息，无需额外注释。
2. 正/负例：正例为 35 个已注册 CTest 名称可发现；负例为 `EXPECTED_MISSING_GATES_CSV` 格式校验和 BC-17 missing marker，不用不存在测试伪装通过。
3. 发现性：新增 `dasall_full_business_chain_discoverability` 通过 `ctest -N -R` 校验，不运行旧测试结果作为成功依据。
4. TODO/worklog：本文件、专项 TODO 与 worklog 均需回写，支持提交追溯。
5. 提交隔离：本轮只包含 FULLINT-TODO-005 的 verifier、CMake 清单和文档证据。

## 7. 完成判定

`FULLINT-TODO-005` 完成条件为：

1. `dasall_full_business_chain_discoverability` target 存在并可构建。
2. BC-01~BC-16 至少有一个当前 CTest 可发现代表入口。
3. BC-17 明确以 `multi_agent_coordinator_runtime_gate_missing` 标记为 missing gate。
4. 安装态 `dasall` 控制面结果已作为边界证据记录，但未被外推为 qemu/production ready。
