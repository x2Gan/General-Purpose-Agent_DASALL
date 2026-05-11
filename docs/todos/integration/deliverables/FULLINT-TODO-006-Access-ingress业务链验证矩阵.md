# FULLINT-TODO-006 Access ingress 业务链验证矩阵

日期：2026-05-11
任务来源：`docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md`
任务目标：扩展 Access ingress 业务链验证矩阵，按 CLI/daemon、HTTP/gateway、async receipt、policy fail-closed、health readiness、profile/contracts guard 拆分 Gate-INT-08 证据，避免 mock pipeline、ping liveness 或历史文档完成状态混入 release 结论。

## 1. 研究证据

### 1.1 本地代码证据

1. Access 详设 6.7 将主链固定为 `ProtocolAdapter -> SubjectResolver -> AuthenticatorChain -> AccessPolicyGate -> RateLimit/Idempotency -> RequestNormalizer -> RuntimeBridge -> ResultPublisher`，并要求 `IAccessRuntimeBridge` 是 access -> runtime 的唯一 module-local 调用面。
2. `tests/integration/access/DaemonAccessSubmitCompositionTest.cpp` 当前断言 CLI/daemon 组合工厂把 `request_id`、`session_id`、`trace_id`、`user_input` 交给 public `AgentRequest` handoff，并且 valid submit 只调用 runtime backend 一次。
3. `tests/integration/access/GatewayAccessSubmitCompositionTest.cpp` 当前断言 HTTP/gateway 组合工厂把 public `AgentRequest` 字段交给 runtime backend。
4. `tests/integration/access/DaemonReceiptFlowIntegrationTest.cpp` 当前覆盖 accepted async receipt、status active/completed、owner mismatch、cancel forwarding、expired status 与 CLI binary JSON stdout routing。
5. `tests/integration/access/AccessObservabilityMainChainIntegrationTest.cpp` 当前覆盖成功 submit 的 request/dispatch event，以及 policy backend unavailable fail-closed 且不触达 runtime。
6. `tests/integration/access/AccessHealthReadinessIntegrationTest.cpp` 当前覆盖 gateway init failed -> readiness/startup 503，runtime backend configured -> readiness/startup 200。
7. `tests/integration/access/CMakeLists.txt` 当前将 `CliDaemonSubmitIntegrationTest`、`HttpGatewaySubmitIntegrationTest`、`AccessGatewayPipelineIntegrationTest`、`AccessAsyncReceiptQueryCancelIntegrationTest`、`AccessPolicyBackendUnavailableIntegrationTest`、`AccessHealthReadinessIntegrationTest`、`AccessProfileCompatibilityTest` 标记为 `gate-int-08;access-v1-production-gate`。
8. `tests/contract/CMakeLists.txt` 当前将 `AgentRequestContractTest`、`AgentResultContractTest`、`IdentityMetadataContractTest` 标记为 `gate-int-08;access-v1-production-gate`。

### 1.2 安装态运行证据

本任务不把 build-tree Gate 外推为 installed-package ready，但仍采集当前包运行边界：

1. `/usr/bin/dasall` 存在；`dasall`、`dasall-cli`、`dasall-common`、`dasall-daemon` 均为 `0.1.0-1 install ok installed`。
2. `dasall-daemon.service` 为 `active/enabled`。
3. `sudo -n dasall ping --json` 与 `sudo -n dasall readiness --json` 均返回 `disposition=completed`，readiness payload 含 `state=READY` 与 `bridge_reachable=true`。
4. `sudo -n dasall status receipt:missing token local://uid/0 --json` 与 `sudo -n dasall cancel receipt:missing token local://uid/0 --json` 均为预期负路径，exit `5`，分别返回 `status_missing` / `cancel_missing`。
5. `sudo -n dasall diag health --json` 为预期门控，exit `4`，返回 `diag_disabled`。

边界：安装态证据只说明 rootful local control-plane 与负路径门控存在；Gate-INT-08 仍是 build-tree Access true-integration/focused ingress 证据，不代表 app-binary `Gate-INT-10`、installed-package qemu 或 production release-ready。

### 1.3 外部参考

OWASP Authorization Cheat Sheet 要求 deny by default、每个请求都验证权限、失败时安全退出、日志可审计，并为授权逻辑创建 unit/integration tests。本轮将 `AccessPolicyBackendUnavailableIntegrationTest`、observability event、contracts guard 和 readiness 分层纳入 Gate-INT-08 expected list，正是为了让 Access ingress 的 fail-closed 与审计证据成为可执行门，而不是文档声明。

## 2. Design 原子清单

| 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 明确 Gate-INT-08 expected tests | `tests/integration/access/CMakeLists.txt`、`tests/contract/CMakeLists.txt` | `DASALL_GATE_INT_08_TEST_NAMES` | CLI/HTTP/async/policy/readiness/profile/contracts 均列入清单 |
| D2 | 将 Gate-INT-08 变为 discoverability + acceptance | CTest 官方 `-N` 与已有 verifier | `dasall_gate_int_08` 调用 `VerifySystemGateDiscoverability.cmake` | expected tests 缺失时 fail，测试失败时 fail |
| D3 | 建立 Access ingress 证据矩阵 | Access 详设、当前测试源码、安装态命令 | 本交付物 + Access TODO 回链 | 每类入口/负路径都有当前代码证据与不可外推边界 |

D Gate：PASS。任务范围是 Gate-INT-08 矩阵和验证入口，不扩张到 readiness 投影修复、gateway exit-code 或 startup diagnostics 覆盖。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收方式 |
|---|---|---|
| Gate-INT-08 必须显式列出 Access ingress 代表测试 | `tests/CMakeLists.txt` 新增 `DASALL_GATE_INT_08_TEST_NAMES` | `Build_CMakeTools(buildTargets=["dasall_gate_int_08"])` |
| 不能用标签空跑冒充 Gate | `dasall_gate_int_08` 改为 verifier script + `RUN_ACCEPTANCE=ON` | target 输出 expected tests discoverability 与 acceptance passed |
| async status active 查询不能被 CLI 误判为 run 业务失败 | `apps/cli/src/CliExitDecision.*`、`apps/cli/src/main.cpp`、`tests/contract/access/CliExitCodeContractTest.cpp` | `AccessAsyncReceiptQueryCancelIntegrationTest` 通过，且 contract 锁定 `status` active exit 0 / `run` incomplete exit 5 |
| Access TODO 需要回链当前集成矩阵 | `docs/todos/access/DASALL_access子系统专项TODO.md` | `rg -n "FULLINT-TODO-006|DASALL_GATE_INT_08_TEST_NAMES|Access ingress" ...` |

## 4. Build 清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| B1 | `tests/CMakeLists.txt` | `dasall_gate_int_08` expected-test discoverability + acceptance | `Build_CMakeTools(buildTargets=["dasall_gate_int_08"])` |
| B2 | `apps/cli/src/CliExitDecision.*`、`apps/cli/src/main.cpp`、`tests/contract/access/CliExitCodeContractTest.cpp` | `status` active query exit-code blocker fix；`run` incomplete 负例 | `Build_CMakeTools(buildTargets=["dasall_gate_int_08"])` |
| B3 | 本交付物与 Access TODO 回链 | 文档一致性 | `rg -n "FULLINT-TODO-006|DASALL_GATE_INT_08_TEST_NAMES|Access ingress|status_missing|diag_disabled" tests/CMakeLists.txt docs/todos/integration docs/todos/access docs/worklog/DASALL_开发执行记录.md` |

## 5. Access Ingress 验证矩阵

| 范围 | 代表 CTest | 代码行为证据 | 当前结论 | 不外推边界 |
|---|---|---|---|---|
| Contracts guard | `AgentRequestContractTest`、`AgentResultContractTest`、`IdentityMetadataContractTest` | contracts CMake 标记 `gate-int-08;access-v1-production-gate` | Access handoff 仍受 frozen request/result/identity 元数据约束 | 不代表 runtime/LLM 主功能 |
| CLI / daemon submit | `CliDaemonSubmitIntegrationTest` | `DaemonAccessSubmitCompositionTest.cpp` 断言 public `AgentRequest` 字段进入 runtime backend，runtime call count 为 1 | CLI/daemon submit focused path 可执行 | 不代表 installed package 普通用户 socket 权限已治理 |
| HTTP / gateway submit | `HttpGatewaySubmitIntegrationTest`、`AccessGatewayPipelineIntegrationTest` | `GatewayAccessSubmitCompositionTest.cpp` 断言 HTTP packet 经 gateway factory 进入 runtime backend | HTTP/gateway unary focused path 可执行 | 不代表 gateway app-binary exit-code 或 installed HTTP endpoint |
| Async receipt / query / cancel | `AccessAsyncReceiptQueryCancelIntegrationTest` | `DaemonReceiptFlowIntegrationTest.cpp` 覆盖 receipt、status active/completed、owner mismatch、cancel forwarding、expired | async receipt focused path 可执行 | 不代表全链 recovery/replay/writeback continuity |
| Policy fail-closed / observability | `AccessPolicyBackendUnavailableIntegrationTest` | `AccessObservabilityMainChainIntegrationTest.cpp` 断言 policy backend unavailable 被拒绝且 runtime call count 为 0，并产生 policy denied event | Access policy backend unavailable fail-closed 可执行 | 不代表更广安全治理或所有 diagnostics pull |
| Health readiness | `AccessHealthReadinessIntegrationTest` | `AccessHealthReadinessIntegrationTest.cpp` 断言 gateway init failed -> 503，runtime backend configured -> 200 | readiness 不是单纯 ping/liveness | `default-ready/degraded-ready/stub-ready` 对外投影仍归 FULLINT-TODO-007 |
| Profile guard | `AccessProfileCompatibilityTest` | `DaemonProfileCompatibilityTest` 通过同一 daemon profile fixture 暴露 Access profile compatibility CTest | profile/contracts guard 可发现 | 不代表 multi_agent runtime-ready |

## 6. Build 合规复核

1. 代码注释：CMake 变量命名和 verifier 调用自解释，无需新增注释。
2. 正/负例：正例为 10 个 expected CTest discoverability；负例为 expected CTest 缺失或 label 缺失时 verifier fail、policy backend unavailable fail-closed 测试，以及 `run` completed/non-final 仍 exit 5 的 CLI contract 负例。
3. 发现性：`dasall_gate_int_08` 现在先做 `ctest -N -R` 和 `ctest -N -L gate-int-08/access-v1-production-gate`，再执行 acceptance。
4. TODO/worklog：本文件、专项 TODO、Access TODO 与 worklog 均回写。
5. 提交隔离：本轮只包含 FULLINT-TODO-006 的 Gate-INT-08 CMake 矩阵、async status blocker fix 和文档回链。

## 7. 完成判定

`FULLINT-TODO-006` 完成条件为：

1. `dasall_gate_int_08` target 已从纯标签执行升级为 expected-test discoverability + acceptance。
2. Access ingress 证据矩阵明确拆分 CLI/HTTP/async/security/readiness/profile/contracts。
3. 当前 installed package 控制面结果作为边界证据记录，但未外推为 release/app-binary/installed-package ready。
4. Access TODO 已回链本次矩阵，后续 Access 任务可按该矩阵定位 Gate-INT-08 回退点。
