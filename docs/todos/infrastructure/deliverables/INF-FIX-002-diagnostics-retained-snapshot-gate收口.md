# INF-FIX-002 diagnostics retained snapshot gate 收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `INF-GAP-002` / `INF-FIX-002`。
2. 本轮目标：把 infra diagnostics retained snapshot Gate 与 daemon / installed `diag_disabled` admin boundary 的 owner、测试拓扑、discoverability 和证据层级明确拆开，避免继续把 `diag_disabled` 误读为 retained snapshot failed 或 diagnostics ready。
3. 完成判定：`Gate-INT-05` 只由 `InfraDiagnosticsSmokeTest` / `InfraDiagnosticsIntegrationTest` 证明 retained snapshot round-trip；default-disabled / admin enablement 只由 `DaemonDiagDenyIntegrationTest`、`DaemonProfileCompatibilityTest`、`DaemonHotReloadIntegrationTest` 与 installed package 证据证明；两类结论不再混写。本轮不使用 qemu / kvm。

## 2. 本地证据

1. `docs/ssot/DiagnosticsRetainedSnapshotContract.md` 与 `docs/architecture/DASALL_infra_diagnostics模块详细设计.md` 已冻结 `execute() -> store -> get_snapshot() -> export_snapshot()` retained snapshot contract，明确 `Gate-INT-05` 的 success object 是 retained snapshot round-trip，而不是 daemon config gate。
2. `infra/src/diagnostics/DiagnosticsServiceFacade.cpp` 已实现 execute / get_snapshot / export_snapshot 三条路径，并要求 execute 在返回前完成 redaction 和 store；`get_snapshot()` / `export_snapshot()` 只消费 retained snapshot。
3. `tests/integration/infra/InfraDiagnosticsSmokeTest.cpp` 已固定最小 retained snapshot round-trip：`execute -> get_snapshot -> local export -> remote-disabled reject`。
4. `tests/integration/infra/InfraDiagnosticsIntegrationTest.cpp` 已固定 export / audit bridge 的协同行为，但不替代 smoke 的最小 round-trip。
5. `access/src/daemon/DaemonDiagnosticsHandler.cpp` 在 diagnostics gate 关闭时直接返回 `diag_disabled`，且不会调用 `IDiagnosticsService`；这说明 `diag_disabled` 属于 daemon admin boundary，而不是 retained snapshot service failure。
6. `tests/integration/access/DaemonDiagDenyIntegrationTest.cpp`、`DaemonProfileCompatibilityTest.cpp`、`DaemonHotReloadIntegrationTest.cpp` 已覆盖 default-disabled reject、baseline profile 默认关闭和 admin enablement reload 三类行为；本轮只缺统一 discoverability 标签和 SSOT/总账回写。
7. `docs/ssot/BusinessChainIntegrationMatrix.md` 与 `scripts/packaging/README.md` 已保留 installed `diag health --json -> diag_disabled` 的历史口径，因此当前缺口不是实现缺失，而是 infra 顶层没有把 build-tree Gate 与 installed admin boundary 明确分层。

## 3. 外部参考

1. OWASP Logging Cheat Sheet 指出，operational logging 与 security/audit logging 目的不同，采集内容应按用途分层，并对高风险管理功能、数据导出和配置变化保持受控、可审计的边界。这与 DASALL 的目标一致：retained snapshot gate 负责验证 diagnostics 服务的 redacted snapshot round-trip；`diag_disabled` 负责表达 admin gate 默认关闭，二者不能互相代替。

## 4. 设计结论

### 4.1 根因收口

1. `INF-FIX-002` 当前真实缺口不在 `DiagnosticsServiceFacade` 主体行为：focused tests 已证明 retained snapshot round-trip 和 daemon deny/default-disabled 行为都存在。
2. 根因在 traceability 和 discoverability：`Gate-INT-05` retained snapshot Gate 与 installed / daemon `diag_disabled` admin boundary 缺少统一分层说明，导致总账仍保留模糊口径。
3. 因此本轮最小可执行动作不是重写 diagnostics 实现，而是统一测试标签、补齐 SSOT 边界说明，并回写 infrastructure deliverable / 总账 / worklog。

### 4.2 authoritative boundary

1. `Gate-INT-05` 的 authoritative owner 仍是 infra diagnostics retained snapshot round-trip，只接受 `InfraDiagnosticsSmokeTest` 与 `InfraDiagnosticsIntegrationTest` 作为 focused Gate 输入。
2. `diag_disabled` 的 authoritative owner 是 daemon / installed admin boundary：它表示 diagnostics command surface 仍默认关闭，不能被解释为 retained snapshot failed，也不能被解释为 diagnostics ready。
3. admin enablement 的 authoritative path 是 daemon config gate / reload 行为，而不是修改 retained snapshot contract；因此 `DaemonDiagDenyIntegrationTest`、`DaemonProfileCompatibilityTest`、`DaemonHotReloadIntegrationTest` 应被统一标记为 diagnostics admin boundary discoverability surface。
4. 本轮只收口 build-tree focused evidence 与既有 installed evidence 的口径分层，不新增 qemu / kvm / release-runner 结论。

## 5. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | retained snapshot round-trip 与 daemon admin boundary 必须各自有 owner，不能共享同一结论 | `docs/ssot/DiagnosticsRetainedSnapshotContract.md`、`docs/ssot/SystemIntegrationGateMatrix.md`、`docs/ssot/BusinessChainIntegrationMatrix.md` |
| D2 | diagnostics admin boundary tests 必须有统一 discoverability label | `tests/integration/access/CMakeLists.txt` |
| D3 | infra 顶层 deliverable、总账与 worklog 必须回链 Gate-INT-05 与 installed/admin boundary 的分层说明 | 本文档、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 6. Build 三件套

1. 代码目标：不改写 `DiagnosticsServiceFacade` 产品逻辑；通过 `tests/integration/access/CMakeLists.txt` 增加 `diagnostics-admin-boundary` discoverability label，并在 SSOT / 总账中固定 retained snapshot gate 与 admin boundary 的 owner 分层。
2. 测试目标：
   - retained snapshot gate：`InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest`
   - admin boundary：`DaemonDiagDenyIntegrationTest`
   - discoverability：`ctest -N -L diagnostics-admin-boundary`
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_infra_diagnostics_smoke_integration_test","dasall_infra_diagnostics_integration_test","dasall_access_daemon_diag_deny_integration_test"])`
   - `RunCtest_CMakeTools(tests=["InfraDiagnosticsSmokeTest","InfraDiagnosticsIntegrationTest","DaemonDiagDenyIntegrationTest"])` 当前仍返回仓库已知泛化 `生成失败`，因此按 fallback 直接执行：`./build/vscode-linux-ninja/tests/integration/infra/dasall_infra_diagnostics_smoke_integration_test && ./build/vscode-linux-ninja/tests/integration/infra/dasall_infra_diagnostics_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_diag_deny_integration_test`
   - `ctest --test-dir build/vscode-linux-ninja -N -L diagnostics-admin-boundary`

## 7. Rollout Checklist

1. `Gate-INT-05` 继续只代表 retained snapshot round-trip，不接管 installed default gate。
2. `diag_disabled` 继续只代表 diagnostics admin boundary 默认关闭，不被写成 diagnostics failed / ready。
3. `DaemonDiagDenyIntegrationTest`、`DaemonProfileCompatibilityTest`、`DaemonHotReloadIntegrationTest` 现在可以通过统一 label 被一次性发现。
4. infrastructure deliverable、总账、SSOT、worklog 四处口径一致。
5. 本轮不使用 qemu / kvm，也不把 build-tree focused 结果外推为 release runner 结论。

## 8. 风险与回退

1. 如果后续继续把 installed `diag_disabled` 当成 `Gate-INT-05` 通过信号，仍会把 admin gate 与 retained snapshot service seam 混写；这正是本轮禁止的外推。
2. 如果删除 diagnostics admin boundary label，Gate discoverability 会再次退回人工 grep；届时应优先恢复统一 label，而不是增加更多重复测试。
3. 如果后续 `DiagnosticsServiceFacade` success path 回退，应由 `InfraDiagnosticsSmokeTest` / `InfraDiagnosticsIntegrationTest` 重新拉红，而不是借 `diag_disabled` 默认关闭掩盖问题。

## 9. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 SSOT、discoverability、总账和 worklog。
3. Build 三件套已锁定，且不依赖 qemu / kvm。
4. 范围保持在 `INF-FIX-002`，不扩张到 health/watchdog、optional backend 或 release-runner 任务。

结论：D Gate = PASS；`INF-FIX-002` 可按既有 diagnostics 实现 + focused tests + boundary traceability 收口。