# DASALL 系统集成修复补充优化专项 TODO（2026-05-09）

最近更新时间：2026-05-09  
阶段：Integration Review Follow-up -> Repair / Completion / Optimization Planning  
适用范围：`runtime/`、`access/`、`apps/daemon/`、`apps/gateway/`、`tests/integration/access/`、`tests/CMakeLists.txt`、`docs/ssot/`、`docs/todos/integration/`、`docs/worklog/`、`scripts/packaging/`  
当前结论：`Gate-INT-08` 与 `Gate-INT-09` 已能证明 library/focused system integration 绿态，且 2026-05-09 连续六轮修复后 `dasall_gate_int_10` 与 `dasall_packaging_preflight_tests` 已共同恢复 build-tree `release-preflight` / app-binary preflight 绿态，`apps/gateway` 已接入 production runtime backend、补齐真实 binary unary smoke 与 missing-backend fail-closed regression，`AgentInitResult` 也已具备 `stub-ready` / `degraded-ready` / `default-ready` 结构化 helper 与入口消费修正，daemon/gateway app root 已建立最小 live runtime dependency baseline（required ports live、optional ports 缺失时 degraded）；当前剩余缺口收敛为 startup diagnostics、preflight artifact 规范与最终证据收口，尚待 `INTFIX-TODO-011` / `012` 完成。本文档作为 5 月 6 日系统集成专项的接续规划，不改写旧 Gate 历史结论，而是新增一组面向真实二进制入口、release gate 与可诊断性的修复任务。

## 文档头

### 输入依据

1. `docs/todos/integration/DASALL_系统集成专项TODO.md`
2. `docs/todos/integration/deliverables/INT-TODO-024-系统集成Gate与交付证据回写收口.md`
3. `docs/ssot/SystemIntegrationGateMatrix.md`
4. `docs/ssot/AccessUnaryProductionPathV1.md`
5. `docs/ssot/SingleAgentRuntimePortMatrix.md`
6. `docs/ssot/UnaryResponseContract.md`
7. `docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md`
8. `docs/todos/daemon/deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md`
9. `apps/daemon/src/main.cpp`
10. `apps/gateway/src/main.cpp`
11. `runtime/include/AgentTypes.h`
12. `runtime/include/RuntimeDependencySet.h`
13. `runtime/src/AgentFacade.cpp`
14. `access/src/AccessGatewayFactory.cpp`
15. `tests/integration/access/DaemonBinaryUnarySmokeTest.cpp`
16. 2026-05-09 集成复核结论：`Build_CMakeTools(target=dasall_gate_int_09)` 通过、`Build_CMakeTools(target=dasall_gate_int_08)` 通过、`Build_CMakeTools(target=dasall_packaging_preflight_tests)` 失败。

### 行业实践参照

1. Practical Test Pyramid / Consumer-Driven Contracts：用 contract、unit、narrow integration 守大部分边界；只保留少量高价值 binary / end-to-end smoke，且失败后下沉补 focused regression。
2. Twelve-Factor Config：部署差异应来自环境、profile、安装布局或外部配置；代码组合根不得依赖开发态路径或隐式 mock。
3. Google SRE Monitoring：release readiness 同时需要 black-box symptom check 与 white-box cause diagnostics；启动失败必须能回答 what broke 与 why broke。
4. OpenTelemetry Traces：跨进程请求应保留 trace context、span status、事件时间点与因果链接；async receipt / query / cancel 后续应使用 span link 或等价 cause chain。
5. Durable execution / workflow compatibility 实践：可恢复主链要区分 accepted、degraded、stub、default-ready，避免把“可启动”误写成“业务主链 ready”。

### 编制原则

1. 保持 5 月 6 日 `INT-TODO-001~030` 的历史闭环；本专项只新增修复接续项。
2. 一轮执行只选择一个最小原子任务；每项必须包含代码目标、测试目标、验收命令三件套。
3. 优先修真实 binary composition root 和 release gate，不扩 streaming、multi-agent、新 provider 或 optional backend。
4. Gate 语义分层：library/focused integration、app-binary smoke、package-installed smoke 三层必须分开记录。
5. 任何“stub 可用”不得等价为“production default unary ready”。
6. 修复应先补可诊断性和可复现性，再补功能闭环；不可用空日志或泛化失败作为长期验收结果。

## 1. 概述与目标

专项目标：

1. 修复 `DaemonBinaryUnarySmokeTest` / `dasall_packaging_preflight_tests` 当前失败，恢复 build-tree package preflight 绿态。
2. 补齐 `apps/gateway` 真实生产组合根，使 gateway binary 不再因缺 `runtime_dispatch_backend` 启动即失败。
3. 拆清 runtime init 的 accepted、degraded、stub-ready、default-ready 语义，阻止入口层误读 readiness。
4. 将 daemon/gateway app-binary smoke、package preflight、Gate-INT-08/09 的证据边界写入新的 SSOT / Gate 矩阵。
5. 将启动失败日志、trace/diagnostics 字段和 preflight failure artifact 纳入验收，提升后续调试效率。
6. 为后续把 daemon/gateway 从 skeleton unary 推进到 live dependency unary 制定可执行路径。

不纳入范围：

1. streaming attach / reconnect / replay cursor。
2. multi-agent 正式闭环。
3. 新 LLM provider、真实 KMS、OTLP exporter、`sqlite-vss` concrete backend。
4. gateway 的完整公网 HTTP release hardening；本轮只修 v1 unary production composition 与 smoke gate。
5. Debian installed-package qemu gate 重构；如需变更，只在本专项收口后进入 packaging 专项增量任务。

## 2. 当前状态

| 维度 | 当前状态 | 判断 |
|---|---|---|
| Gate-INT-09 | `Build_CMakeTools(target=dasall_gate_int_09)` 通过 | 系统 focused integration 主链当前绿 |
| Gate-INT-08 | `Build_CMakeTools(target=dasall_gate_int_08)` 通过 | Access factory / focused ingress 当前绿 |
| Packaging preflight | `Build_CMakeTools(buildTargets=["dasall_gate_int_10","dasall_packaging_preflight_tests"])` 通过 | build-tree `release-preflight` 已拆分为 app-binary Gate-INT-10 与 package-related preflight 两个正式入口，并通过 `release-preflight-gate` discoverability verifier 收口 |
| daemon binary | `Build_CMakeTools(buildTargets=["dasall-daemon","dasall_access_daemon_runtime_live_dependency_composition_test"])` 通过 | daemon app root 已显式装配 memory/cognition/response/tools required ports；knowledge/llm 缺失时走 degraded-ready，不再依赖 empty stub path |
| gateway binary | `Build_CMakeTools(buildTargets=["dasall_access_gateway_submit_composition_test","dasall_access_gateway_binary_unary_smoke_integration_test"])` 与 `Build_CMakeTools(buildTargets=["dasall_gate_int_10"])` 通过 | gateway app-binary happy path 与 missing-backend fail-closed regression 已覆盖到真实 binary/test fixture 进程边界，并已接入 Gate-INT-10；startup diagnostics / artifact 规范待 011/012 收口 |
| runtime readiness | `Build_CMakeTools(buildTargets=["dasall_runtime_agent_init_result_readiness_unit_test","dasall_gate_int_06","dasall_runtime_unary_integration_test"])` 通过 | `accepted` / `stub-ready` / `degraded-ready` / `default-ready` 已有 helper、入口消费修正与 live baseline 投影；default-ready 仍待 optional ports 后续收口 |
| 可诊断性 | daemon smoke 超时现在回写 `socket_path`/长度/exit code/log，bootstrap bind/accept 失败直出 detail | 005 已满足 daemon 侧 what/why 基线；011 继续扩展到 gateway 与统一 artifact 规范 |

## 3. 约束条件

| ID | 约束 | 对 TODO 的直接影响 |
|---|---|---|
| INTFIX-TC001 | ADR-006/007/008 不得漂移 | 不把 app main 写成新 orchestration owner；只做组合根 wiring |
| INTFIX-TC002 | production composition 不得依赖 mock/test profile | gateway/daemon binary smoke 必须走真实 factory/backend seam |
| INTFIX-TC003 | stub path 可用于开发烟测，但不得宣称 default-ready | runtime init result 必须拆语义和标签 |
| INTFIX-TC004 | package preflight 是 release 入口，不应被 Gate-INT-09 绿态覆盖 | 新增 Gate-INT-10 或 release-preflight gate |
| INTFIX-TC005 | 失败必须留诊断证据 | app main、preflight harness 和 tests 必须捕获 failure cause |
| INTFIX-TC006 | 测试金字塔优先 | 每个 high-level binary smoke 失败后必须补对应 lower-level unit/integration regression |
| INTFIX-TC007 | 继续优先使用 CMake Tools build target 作为本环境可靠入口 | 直接 `RunCtest_CMakeTools` 失败不得当作功能失败，需冻结 fallback 规则 |

## 4. Design Track 映射

| 评审发现 | 设计收口动作 | 对应任务 |
|---|---|---|
| packaging preflight 红，但 Gate-INT-08/09 绿 | 新增 release/app-binary gate 分层规则 | INTFIX-TODO-003、010、012 |
| gateway app 缺 runtime backend | 冻结 GatewayBinaryProductionPathV1 | INTFIX-TODO-002、006、009 |
| daemon runtime 仍是空 dependency stub path | 冻结 runtime app composition / stub vs live readiness 口径 | INTFIX-TODO-001、004、007、008 |
| binary smoke 失败日志为空 | 冻结 startup diagnostics / preflight artifact 要求 | INTFIX-TODO-005、011 |
| CMake Tools focused ctest 入口不稳定 | 冻结验证 fallback 与证据权威顺序 | INTFIX-TODO-003、010 |

## 5. Build Track 映射

| Build 目标 | 涉及代码面 | 对应任务 | 完成标准 |
|---|---|---|---|
| daemon preflight root-cause 修复 | `apps/daemon/`、`tests/integration/access/DaemonBinaryUnarySmokeTest.cpp` | 005 | preflight 重新转绿，失败时日志不为空 |
| gateway binary production composition | `apps/gateway/`、`access/`、`runtime/`、`tests/integration/access/` | 006、009 | gateway binary 有真实 runtime backend smoke |
| runtime readiness 语义拆分 | `runtime/include/AgentTypes.h`、`runtime/src/AgentFacade.cpp`、daemon/gateway main | 007 | accepted/degraded/stub/default-ready 可测试可投影 |
| live dependency composition | `apps/daemon/`、`apps/gateway/`、可能新增 app-local composition helper | 008 | daemon/gateway 可选择 live dependency chain，不再只有 skeleton path |
| release gate v2 | `tests/CMakeLists.txt`、`tests/integration/access/CMakeLists.txt`、`scripts/packaging/`、`docs/ssot/` | 003、010、012 | Gate-INT-10 / release-preflight 证据闭环 |
| startup diagnostics | app main、bootstrap/listener、test harness | 011 | startup failure cause 可由日志/artifact/trace 定位 |

## 6. 任务表

### 6.1 前置补设计 / Gate 语义冻结任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| INTFIX-TODO-001 | Done | 冻结 BinaryEntrypointReadinessV1 | 本评审 P0/P1；SRE black-box/white-box readiness | app-binary 层 accepted、degraded、stub-ready、default-ready、bridge-reachable、health-ready 的字段与对外语义 | L3 | `docs/ssot/BinaryEntrypointReadinessV1.md`、`docs/ssot/SystemIntegrationGateMatrix.md` | `AgentInitResult` readiness 语义表、daemon/gateway readiness projection rule | 文档一致性检查 | `rg -n "stub-ready|default-ready|bridge-reachable|health-ready|BinaryEntrypointReadiness" docs/ssot/BinaryEntrypointReadinessV1.md docs/ssot/SystemIntegrationGateMatrix.md docs/todos/integration/DASALL_系统集成修复补充优化专项TODO-2026-05-09.md` | 无 | INTFIX-BLK-01 | 已通过 `BinaryEntrypointReadinessV1` 冻结 accepted/degraded/stub-ready/default-ready/bridge-reachable/health-ready 分层与 daemon/gateway 投影规则 | `docs/ssot/BinaryEntrypointReadinessV1.md` | 仅当二进制入口层 readiness 不再只用 `accepted` 表达，且每个状态都有 owner、输出字段和 gate 归属时完成 |
| INTFIX-TODO-002 | Done | 冻结 GatewayBinaryProductionPathV1 | AccessUnaryProductionPathV1；gateway app 缺 backend | `apps/gateway` runtime backend wiring、profile/config 输入、fail-closed、mock/test profile 边界 | L3 | `docs/ssot/GatewayBinaryProductionPathV1.md`、`docs/architecture/DASALL_access子系统详细设计.md` | `GatewayAccessPipelineOptions::runtime_dispatch_backend`、gateway production composition rule | 文档一致性检查 | `rg -n "GatewayBinaryProductionPath|runtime_dispatch_backend|mock pipeline|production submit pipeline" docs/ssot/GatewayBinaryProductionPathV1.md docs/architecture/DASALL_access子系统详细设计.md apps/gateway/src/main.cpp access/src/AccessGatewayFactory.cpp` | 001 | INTFIX-BLK-02 | 已通过 `GatewayBinaryProductionPathV1` 冻结 gateway production backend owner、profile/config 输入、fail-closed 与 mock/test profile 边界 | `docs/ssot/GatewayBinaryProductionPathV1.md` | 仅当 gateway app main 的生产 backend 来源、失败语义和测试替身边界被同一份 SSOT 固定时完成 |
| INTFIX-TODO-003 | Done | 冻结 Gate-INT-10 release/app-binary preflight 分层 | SystemIntegrationGateMatrix；Practical Test Pyramid | library gate、app-binary smoke、packaging preflight、installed-package gate 的边界和命令权威 | L3 | `docs/ssot/SystemIntegrationGateMatrix.md`、`docs/todos/integration/DASALL_系统集成修复补充优化专项TODO-2026-05-09.md` | `Gate-INT-10`、`release-preflight-gate`、verification fallback rule | 文档一致性检查 | `rg -n "Gate-INT-10|release-preflight|app-binary|packaging_preflight|RunCtest_CMakeTools" docs/ssot/SystemIntegrationGateMatrix.md docs/todos/integration/DASALL_系统集成修复补充优化专项TODO-2026-05-09.md` | 001、002 | 无 | 已通过 Gate 矩阵冻结 library/app-binary/release-preflight/installed-package 四层边界与 `RunCtest_CMakeTools` fallback 规则 | 更新后的 SSOT 与本 TODO | 仅当 Gate-INT-08/09 与 package/app-binary gate 不再互相覆盖，且每层正式命令可二值判定时完成 |
| INTFIX-TODO-004 | Done | 冻结 RuntimeAppCompositionV1 与 stub/live 边界 | SingleAgentRuntimePortMatrix；daemon empty dependency set 发现 | daemon/gateway 如何构造 memory/cognition/tools/knowledge/llm dependency set；stub path 的允许范围 | L3 | `docs/ssot/RuntimeAppCompositionV1.md`、`docs/architecture/DASALL_runtime子系统详细设计.md` | `RuntimeDependencySet` production composition rule、stub admission rule | 文档一致性检查 | `rg -n "RuntimeAppComposition|stub runtime|RuntimeDependencySet|production composition|default unary" docs/ssot/RuntimeAppCompositionV1.md docs/architecture/DASALL_runtime子系统详细设计.md apps/daemon/src/main.cpp apps/gateway/src/main.cpp runtime/src/AgentFacade.cpp` | 001 | INTFIX-BLK-03 | 已通过 `RuntimeAppCompositionV1` 冻结 `RuntimeDependencySet` 的 app-level production composition owner、stub runtime path 与 live/degraded/default unary 边界 | `docs/ssot/RuntimeAppCompositionV1.md` | 仅当 live/stub dependency composition 的使用场景、profile 开关、gate 归属和禁止外推范围明确时完成 |

### 6.2 修复实现任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| INTFIX-TODO-005 | Done | 定位并修复 DaemonBinaryUnarySmokeTest readiness 失败 | 评审 P0；Gate-INT-10 | daemon 启动、profile/load、bootstrap/bind、CLI ping readiness 失败链路；失败日志采集 | L2 | `apps/daemon/src/DaemonBootstrap.cpp`、`apps/daemon/src/DaemonSocketPolicy.cpp`、`tests/integration/access/DaemonBinaryUnarySmokeTest.cpp` | `DaemonBootstrap::run()`、`validate_socket_path()`、test harness log capture | `DaemonBinaryUnarySmokeTest`、`DaemonPingIntegrationTest`、`dasall_packaging_preflight_tests` | `Build_CMakeTools(buildTargets=["dasall_access_daemon_binary_unary_smoke_integration_test","dasall_packaging_preflight_tests"])` | 001、003 | INTFIX-BLK-04 | 已通过缩短 smoke harness UDS path、在 socket policy fail-fast 拒绝超长路径、并将 bootstrap bind/accept 失败 detail 落入 daemon log 解阻 | daemon smoke 修复、socket path fail-fast、startup diagnostics baseline | 仅当 `DaemonBinaryUnarySmokeTest` 通过，且若再次失败时 daemon log/artifact 包含明确失败原因时完成 |
| INTFIX-TODO-006 | Done | 接入 gateway binary runtime backend 组合根 | GatewayBinaryProductionPathV1；Access v1 production path | `apps/gateway` 创建 runtime facade / backend 或复用 app-local runtime bridge factory，并注入 `GatewayAccessPipelineOptions` | L2 | `apps/gateway/src/main.cpp`、`apps/gateway/src/HttpProtocolAdapter.cpp`、`apps/gateway/CMakeLists.txt`、`tests/integration/access/GatewayBinaryUnarySmokeTest.cpp`、`tests/integration/access/CMakeLists.txt`、`tests/unit/access/HttpProtocolAdapterTest.cpp` | `create_gateway_access_gateway()`、`GatewayAccessPipelineOptions::runtime_dispatch_backend`、`HttpProtocolAdapter::decode()` | `GatewayAccessSubmitCompositionTest`、`GatewayBinaryUnarySmokeTest`、`HttpProtocolAdapterTest` | `Build_CMakeTools(buildTargets=["dasall_gateway","dasall_access_gateway_submit_composition_test","dasall_access_gateway_binary_unary_smoke_integration_test"])` | 002、004 | INTFIX-BLK-02 | 已通过 gateway main 装配 runtime facade + policy snapshot、补齐 HTTP packet metadata 投影，并新增真实 gateway binary unary smoke 解阻 | gateway production backend wiring、custom acceptance target、binary smoke baseline | 仅当 gateway binary 不再因缺 production submit pipeline 退出，且 submit smoke 证明请求进入 runtime backend 时完成 |
| INTFIX-TODO-007 | Done | 拆分 AgentInitResult readiness 语义并修正入口消费 | BinaryEntrypointReadinessV1；runtime stub 误读风险 | `accepted`、`degraded`、`stub_ready`、`default_ready`、`health_summary`、`diagnostics` 的结构化输出与 daemon/gateway 消费 | L3 | `runtime/include/AgentTypes.h`、`runtime/src/AgentFacade.cpp`、`apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`、`tests/unit/runtime/AgentInitResultReadinessTest.cpp`、`tests/unit/runtime/CMakeLists.txt` | `AgentInitResult::readiness_level()` / `stub_ready()` / `default_ready()`、`AgentFacade::State::init()` | `AgentInitResultReadinessTest`、`RuntimeRequiredOptionalPortsIntegrationTest`、app main build compile | `Build_CMakeTools(buildTargets=["dasall_runtime_agent_init_result_readiness_unit_test","dasall_gate_int_06"])` | 001、004 | INTFIX-BLK-01 | 已通过结构化 readiness helper、`entrypoint_ready=*` diagnostics 输出与 daemon/gateway 显式消费 accepted 解阻 | runtime readiness helper、entrypoint diagnostics、consumer projection fix | 仅当 stub/degraded/default-ready 三类 init result 可被单测区分，且既有 Gate-INT-06 不回退时完成 |
| INTFIX-TODO-008 | Done | 建立 daemon/gateway live runtime dependency composition baseline | RuntimeAppCompositionV1；SingleAgentRuntimePortMatrix | 用现有 memory/cognition/tools/knowledge/llm factories 构造最小 live dependency set；缺 optional 走 degraded，不走 empty stub | L2 | `apps/runtime_support/`、`apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`、`tests/integration/access/*RuntimeLiveDependencyCompositionTest.cpp`、相关 CMake | `compose_minimal_live_dependency_set()`、`build_daemon_agent_init_request()`、gateway runtime init helper | `DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest`、`RuntimeUnaryIntegrationTest` | `Build_CMakeTools(buildTargets=["dasall_runtime_unary_integration_test","dasall_access_daemon_runtime_live_dependency_composition_test","dasall_access_gateway_runtime_live_dependency_composition_test"])` | 004、007 | INTFIX-BLK-03 | 已通过 app-level runtime composition helper、daemon/gateway 显式依赖装配与 focused composition tests 解阻 | app binary minimal live dependency baseline、custom acceptance targets | 仅当 daemon/gateway 至少能用 required ports 形成 live dependency set，缺 knowledge/llm 时按 degraded 语义输出，而不是依赖 empty stub path 时完成 |

### 6.3 测试支撑 / Gate 收敛任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| INTFIX-TODO-009 | Done | 新增 gateway app-binary unary smoke 与 fail-closed 回归 | Gate-INT-10；Practical Test Pyramid | 真实 `dasall_gateway` binary 启动、submit、runtime backend handoff、缺 backend fail-closed regression | L2 | `tests/integration/access/GatewayBinaryUnarySmokeTest.cpp`、`tests/integration/access/GatewayBinaryMissingBackendFixture.cpp`、`tests/integration/access/GatewayBinaryMissingBackendRegressionTest.cpp`、`tests/integration/access/CMakeLists.txt` | `GatewayBinaryUnarySmokeTest`、`GatewayBinaryMissingBackendRegressionTest`、gateway binary custom target wiring | `GatewayBinaryUnarySmokeTest`、`GatewayBinaryMissingBackendRegressionTest`、`GatewayAccessSubmitCompositionTest` | `Build_CMakeTools(buildTargets=["dasall_access_gateway_submit_composition_test","dasall_access_gateway_binary_unary_smoke_integration_test"])` | 006 | 无 | 已通过 gateway binary happy-path smoke 证据与 missing-backend fail-closed regression 解锁 app-binary gate 前置面 | gateway binary smoke/regression tests 与 CMake 注册 | 仅当 gateway 真实 binary path 被测试覆盖，且测试不会与 factory fake backend 证据混淆时完成 |
| INTFIX-TODO-010 | Done | 接线 Gate-INT-10 release/app-binary preflight target | Gate-INT-10；packaging preflight red灯 | 聚合 daemon/gateway app-binary smoke、packaging preflight、discoverability verifier | L2 | `tests/CMakeLists.txt`、`tests/contract/access/CMakeLists.txt`、`tests/integration/access/CMakeLists.txt`、`tests/VerifySystemGateDiscoverability.cmake`、`tests/integration/access/DaemonBinaryUnarySmokeTest.cpp`、`scripts/packaging/README.md` | `dasall_gate_int_10`、`release-preflight-gate` label、`dasall_packaging_preflight_tests` relation | `DaemonBinaryUnarySmokeTest`、`GatewayBinaryUnarySmokeTest`、`GatewayBinaryMissingBackendRegressionTest`、`CliDaemonSocketPathIntegrationTest` | `Build_CMakeTools(buildTargets=["dasall_gate_int_10","dasall_packaging_preflight_tests"])` | 003、005、009 | 无 | 已通过 gate-int-10 / release-preflight-gate discoverability wiring 与 daemon legacy skeleton 断言修正解锁 build-tree release-preflight | Gate-INT-10 target / labels / verifier / docs | 仅当 app-binary release preflight 有独立 target，且不会被 Gate-INT-08/09 绿态误覆盖时完成 |
| INTFIX-TODO-011 | NotStarted | 补齐 startup diagnostics 与 preflight artifact 规范 | SRE monitoring；OpenTelemetry trace/events | daemon/gateway 启动失败日志、config/profile/runtime init failure cause、trace id、test artifact path | L2 | `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`、`tests/integration/access/DaemonBinaryUnarySmokeTest.cpp`、`tests/integration/access/GatewayBinaryUnarySmokeTest.cpp` | startup failure reporter、test log capture helper | `DaemonStartupDiagnosticsTest`、`GatewayStartupDiagnosticsTest` | `Build_CMakeTools(buildTargets=["dasall_access_daemon_startup_diagnostics_test","dasall_access_gateway_startup_diagnostics_test"])` | 005、006 | 无 | failure cause 输出路径已在 005/006 中暴露 | startup diagnostics helper/tests | 仅当任一 app binary 启动失败时，测试输出能稳定包含阶段、错误码、配置/asset root、socket path 或 runtime init diagnostics 时完成 |
| INTFIX-TODO-012 | NotStarted | 回写系统集成修复 Gate 与后续优化路线 | Gate-INT-10；TODO 格式标准 | TODO、deliverable、worklog、review addendum、残余风险、后续优化 backlog | L2 | `docs/todos/integration/DASALL_系统集成修复补充优化专项TODO-2026-05-09.md`、`docs/todos/integration/deliverables/INTFIX-TODO-012-系统集成修复Gate与证据收口.md`、`docs/worklog/DASALL_开发执行记录.md` | Gate evidence、blocker status、residual optimization backlog | 文档一致性检查 | `rg -n "INTFIX-TODO-012|Gate-INT-10|release-preflight|记录 #" docs/todos/integration/DASALL_系统集成修复补充优化专项TODO-2026-05-09.md docs/todos/integration/deliverables/INTFIX-TODO-012-系统集成修复Gate与证据收口.md docs/worklog/DASALL_开发执行记录.md docs/ssot/SystemIntegrationGateMatrix.md` | 010、011 | 无 | Gate-INT-10 与 diagnostics 已稳定 | deliverable + worklog 回写 | 仅当代码 Gate、preflight 证据、文档状态和残余风险一致，且后续优化不再混入修复主线时完成 |

## 7. 执行顺序建议

| 阶段 | 任务 | 编排建议 | 说明 |
|---|---|---|---|
| A 语义冻结 | 001、002、003、004 | 001 先行；002/003/004 可并行 | 先拆 readiness、gateway production path、Gate-INT-10、runtime composition |
| B 复现与关键修复 | 005、006、007 | 005 与 006 可并行；007 在 001 后启动 | 先让 daemon/gateway 二进制入口可诊断、可启动、语义不混淆 |
| C live dependency baseline | 008 | 依赖 004、007 | 把 skeleton path 从 production default 中剥离出去 |
| D app-binary Gate | 009、010、011 | 009 依赖 006；010 依赖 005/009；011 在 005/006 后 | 固化真实 binary smoke 与 failure artifact |
| E 证据收口 | 012 | 串行 | 统一回写 TODO / deliverable / worklog / SSOT |

推荐第一批执行顺序：`INTFIX-TODO-001` -> `INTFIX-TODO-003` -> `INTFIX-TODO-005`。理由是当前最痛的红灯是 packaging preflight，先冻结 readiness/gate 语义，再修 daemon binary smoke，收益最高且不会被 gateway/live dependency 扩面拖慢。

## 8. 阻塞项与解阻条件

| Blocker ID | 阻塞项 | 当前影响 | 解阻条件 | 回退策略 |
|---|---|---|---|---|
| INTFIX-BLK-01 | binary readiness 语义尚未冻结 | 阻断 007，入口层可能继续误用 `is_ready()` | 完成 INTFIX-TODO-001 | 暂时保留 `accepted` 旧语义，但不得宣称 default-ready |
| INTFIX-BLK-02 | gateway production backend owner 未冻结 | 阻断 006/009 | 完成 INTFIX-TODO-002 | gateway binary 继续 fail-closed，不写入 release-ready |
| INTFIX-BLK-03 | runtime live dependency composition owner 未冻结 | 阻断 008 | 完成 INTFIX-TODO-004 | daemon/gateway 只可宣称 skeleton smoke，不宣称 live unary |
| INTFIX-BLK-04 | daemon binary smoke failure cause 尚不可诊断 | 阻断 005 根因修复效率 | 在 005 首步补 log/artifact 捕获 | 不以空日志失败作为长期已知限制，必须补诊断 |

## 9. 测试矩阵与统一验收命令

### 9.1 测试矩阵

| 层级 | 测试 / Gate | 目的 |
|---|---|---|
| 文档 / SSOT | `BinaryEntrypointReadinessV1`、`GatewayBinaryProductionPathV1`、`RuntimeAppCompositionV1`、`SystemIntegrationGateMatrix` | 固定语义和 gate 分层 |
| unit | `AgentInitResultReadinessTest`、startup diagnostics tests | 守住 readiness helper 与 failure cause 输出 |
| focused integration | `RuntimeRequiredOptionalPortsIntegrationTest`、`GatewayAccessSubmitCompositionTest` | 守住旧 Gate 不回退 |
| app-binary smoke | `DaemonBinaryUnarySmokeTest`、`GatewayBinaryUnarySmokeTest` | 验证真实二进制入口和组合根 |
| release preflight | `dasall_gate_int_10`、`dasall_packaging_preflight_tests` | 验证 app-binary/release readiness |
| installed-package | `validate_ubuntu_dpkg_v1.sh`、qemu `autopkgtest` | 后续 packaging 专项复验，不作为本专项首批必须重构对象 |

### 9.2 统一验收命令

本专项完成态的 build-tree 统一验收命令：

```text
Build_CMakeTools(buildTargets=["dasall_gate_int_08","dasall_gate_int_09","dasall_gate_int_10","dasall_packaging_preflight_tests"])
```

说明：若 `RunCtest_CMakeTools` 继续返回泛化 `生成失败`，不作为功能失败；以 focused CMake custom target 的结果、测试二进制输出和 preflight artifact 为正式证据。

## 10. 风险与回退策略

| Risk ID | 风险 | 影响 | 缓解动作 |
|---|---|---|---|
| INTFIX-R01 | 急于把 daemon/gateway 同时升级到完整 live dependency，导致任务过大 | 修复周期拉长，preflight 红灯拖延 | 先修 smoke + readiness + diagnostics，再做 live baseline |
| INTFIX-R02 | 把 Gate-INT-08/09 绿态继续外推到 release-ready | 决策误判 | Gate-INT-10 单列 app-binary/release preflight |
| INTFIX-R03 | 为了通过测试而放宽断言或回退到 fake IPC/mock backend | 集成证据失真 | binary smoke 必须真实启动 app target，只允许 narrow fake 位于明确 test seam |
| INTFIX-R04 | runtime readiness 字段修改引入下游编译震荡 | 多模块回归 | 先 additive helper，再逐步迁移入口消费；保留兼容字段 |
| INTFIX-R05 | 诊断输出泄露 secret/profile 敏感字段 | 安全风险 | 只输出 path、stage、error code、redacted diagnostics，不输出 secret value |

## 11. 可行性结论

当前不存在总阻塞，建议立即进入 `INTFIX-TODO-001`。最小高收益路径是：先冻结 binary readiness 和 Gate-INT-10，再修复 `DaemonBinaryUnarySmokeTest`，随后补 gateway binary composition 与 runtime readiness 拆分。这样能最快把“系统 focused gate 绿但 release preflight 红”的状态收敛为可解释、可修复、可长期回归的工程闭环。

## 12. 后续优化 Backlog

| 优化项 | 触发条件 | 不纳入当前 P0 的原因 |
|---|---|---|
| gateway HTTP release hardening | gateway binary unary smoke 绿后 | 当前先修组合根，不扩公网协议治理 |
| installed-package qemu gate 回归串联 Gate-INT-10 | build-tree release preflight 绿后 | 需重新构包与 root/qemu 环境，不应阻断本地修复 |
| OTel-compatible trace exporter | startup diagnostics 与 trace context 字段稳定后 | exporter 是 optional backend，不影响 P0 readiness |
| longer-running binary soak | app-binary smoke 稳定后 | 属于 release confidence 扩展，不是当前红灯根因 |
| streaming / async trace links | unary release gate 稳定后 | streaming 不在当前 unary 修复主线 |
