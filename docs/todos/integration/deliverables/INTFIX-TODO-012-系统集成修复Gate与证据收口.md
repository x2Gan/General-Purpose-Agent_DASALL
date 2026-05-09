# INTFIX-TODO-012 系统集成修复 Gate 与证据收口

状态：Done
日期：2026-05-09
来源 TODO：docs/todos/integration/DASALL_系统集成修复补充优化专项TODO-2026-05-09.md

## 1. 任务边界与前置检查

1. 本任务只处理系统集成修复专项的最终证据闭环，不新增新的 runtime owner、binary gate 或 packaging owner。
2. 前置依赖复核：`INTFIX-TODO-010` 已完成 `Gate-INT-10` / `release-preflight-gate` build-tree 入口接线；`INTFIX-TODO-011` 已完成 daemon/gateway startup diagnostics 与 preflight artifact 规范。
3. 本任务的正式输出面固定为三类长期资产：
   - docs/todos/integration/DASALL_系统集成修复补充优化专项TODO-2026-05-09.md
   - docs/todos/integration/deliverables/INTFIX-TODO-012-系统集成修复Gate与证据收口.md
   - docs/worklog/DASALL_开发执行记录.md

## 2. 命令证据

1. `Build_CMakeTools(buildTargets=["dasall_gate_int_08","dasall_gate_int_09","dasall_gate_int_10","dasall_packaging_preflight_tests"])`
2. `Build_CMakeTools(buildTargets=["dasall_access_daemon_startup_diagnostics_test","dasall_access_gateway_startup_diagnostics_test"])`
3. `ctest --test-dir build/vscode-linux-ninja -R '^(DaemonStartupDiagnosticsTest|GatewayStartupDiagnosticsTest)$' --output-on-failure`
4. `rg -n "INTFIX-TODO-012|Gate-INT-10|release-preflight|记录 #61[6-8]" docs/todos/integration/DASALL_系统集成修复补充优化专项TODO-2026-05-09.md docs/todos/integration/deliverables/INTFIX-TODO-012-系统集成修复Gate与证据收口.md docs/worklog/DASALL_开发执行记录.md docs/ssot/SystemIntegrationGateMatrix.md`

结果摘要：

1. `dasall_gate_int_08` 已通过 10 条 Access focused ingress / production-path tests，继续作为 Access v1 unary focused gate。
2. `dasall_gate_int_09` 已通过 discoverability + one-shot acceptance，维持系统 focused integration Gate 的可发现性与正式命令入口。
3. `dasall_gate_int_10` 与 `dasall_packaging_preflight_tests` 已共同通过，证明 build-tree `release-preflight` 已拆分为 app-binary smoke 与 package-related preflight 两个正式入口。
4. `DaemonStartupDiagnosticsTest` 与 `GatewayStartupDiagnosticsTest` 已 2/2 通过，证明 startup failure reporter 与 `artifact_path` 规范已被 focused tests 锁定。

## 3. Gate 回写结论

| Gate / 证据层 | 当前状态 | 正式命令 / 证据 | 长期交付物路径 | 当前状态说明 | 后继归属 | 残余风险 |
|---|---|---|---|---|---|---|
| Gate-INT-08 | Pass | `Build_CMakeTools(buildTargets=["dasall_gate_int_08"])` | docs/ssot/SystemIntegrationGateMatrix.md | Access v1 production ingress focused gate 持续绿态，未被 app-binary / release-preflight 证据吞并 | 继续由 access / integration focused tests 维护 | mock pipeline、health-only 或局部 envelope 字段仍不得外推为 release-ready |
| Gate-INT-09 | Pass | `Build_CMakeTools(buildTargets=["dasall_gate_int_09"])` | docs/ssot/SystemIntegrationGateMatrix.md | discoverability、one-shot acceptance 与文档回写责任保持收敛，focused integration Gate 入口稳定 | 仅在系统 Gate 名称、target 或 acceptance 发生变化时再开新任务 | `RunCtest_CMakeTools` 仍可能泛化失败，需继续沿用 focused CMake target / explicit ctest fallback 规则 |
| Gate-INT-10 | Pass | `Build_CMakeTools(buildTargets=["dasall_gate_int_10","dasall_packaging_preflight_tests"])` | docs/ssot/SystemIntegrationGateMatrix.md | daemon/gateway app-binary smoke 与 package-related preflight 已分层成两个正式 build-tree `release-preflight` 入口，并通过 `release-preflight-gate` 标签 discoverability 收口 | 继续由 integration / packaging 交界面维护 | build-tree `release-preflight` 不能替代 installed-package qemu / `autopkgtest` gate |
| startup diagnostics / preflight artifact | Pass | `Build_CMakeTools(buildTargets=["dasall_access_daemon_startup_diagnostics_test","dasall_access_gateway_startup_diagnostics_test"])` + focused `ctest` | docs/worklog/DASALL_开发执行记录.md | daemon/gateway 启动失败时已统一输出 `stage`、`error_code`、`trace_id`、路径字段与 `detail` / `runtime_diagnostics`，测试失败时同步回写 `artifact_path` | 继续由 app-binary smoke / diagnostics focused tests 维护 | detail 字段若未来引入 secret-bearing 内容，必须继续执行 redaction 约束 |

## 4. 修复闭环摘要

| 任务段 | 已完成任务 | 收口结果 |
|---|---|---|
| 语义冻结 | `INTFIX-TODO-001` ~ `004` | `BinaryEntrypointReadinessV1`、`GatewayBinaryProductionPathV1`、`RuntimeAppCompositionV1` 与 `SystemIntegrationGateMatrix` 已固定本轮修复边界、Gate 分层与 fallback 规则 |
| 关键修复 | `INTFIX-TODO-005` ~ `009` | daemon smoke readiness、gateway production backend、runtime readiness helper、live dependency baseline、gateway binary fail-closed regression 已逐项落地并提交 |
| Gate / diagnostics 收敛 | `INTFIX-TODO-010` ~ `011` | `Gate-INT-10` 与 `release-preflight-gate` 接线完成，startup diagnostics / artifact 规范已统一，当前修复主线不再存在未收口代码 Gate |

## 5. 文档同步结论

1. 专项 TODO 中 `INTFIX-TODO-001` ~ `012` 已全部回写为 Done，且“当前结论 / 当前状态 / 统一验收命令”与最新 gate 结果一致。
2. worklog 已按执行顺序记录 `#613` ~ `#618` 的收口链路，其中 `#616` 固化 Gate-INT-10 双入口，`#617` 固化 startup diagnostics / artifact 规范，`#618` 负责最终 Gate 与 backlog 回写。
3. `docs/ssot/SystemIntegrationGateMatrix.md` 继续作为 Gate 命名、层级和命令权威来源；本 deliverable 只回写“当前阶段已经完成的证据闭环”，不改写 SSOT 本体。

## 6. 残余风险与后续优化路线

| 优化项 | 触发条件 | 下一归属 | 不纳入本轮修复主线的原因 |
|---|---|---|---|
| gateway HTTP release hardening | gateway unary production path 长期稳定后 | access / gateway 后续专项 | 当前已完成 production submit path 与 binary smoke，公网协议 hardening 不再是本轮红灯根因 |
| installed-package qemu gate 串联 Gate-INT-10 | build-tree `release-preflight` 持续稳定后 | packaging 专项 | 已通过 `scripts/packaging/validate_gate_int_10_installed_package_qemu.sh` 固化顺序入口；正式 installed-package 结论仍取决于调用方提供 qemu image / virt-server 并完成 `autopkgtest` |
| optional ports 从 degraded-ready 走向 default-ready | knowledge / llm optional backend 达到可维护基线后 | runtime / apps runtime_support | 当前只完成 required live baseline，继续扩 optional ports 会把修复任务放大 |
| OTel-compatible trace exporter | startup diagnostics 字段与 retained snapshot 冻结后 | infra / observability 后续任务 | exporter 属于 optional backend，不影响当前 build-tree `release-preflight` 判断 |
| longer-running binary soak / chaos | app-binary smoke 与 startup diagnostics 长期稳定后 | integration / release confidence 后续任务 | 属于 release confidence 扩展，不是当前修复主线的 blocking gap |
| streaming / async trace links | unary release gate 稳定后 | access / runtime 后续任务 | streaming 不在当前 unary repair 主线 |

## 7. 结论

1. 本轮系统集成修复专项的主线任务已经全部闭环：`Gate-INT-08`、`Gate-INT-09`、`Gate-INT-10`、`release-preflight-gate` 与 startup diagnostics / artifact 规范都具备正式命令、worklog 证据和长期交付物路径。
2. 从 2026-05-09 起，build-tree `release-preflight` 的正式结论只能由 `dasall_gate_int_10`、`dasall_packaging_preflight_tests` 与 focused diagnostics evidence 共同给出；任何 focused integration 绿灯都不再允许越级覆盖这些 binary / preflight 结论。
3. 后续工作应转入 backlog / 专项增量，而不是继续在“系统集成修复补充优化专项”主线上混入新的实现任务。
