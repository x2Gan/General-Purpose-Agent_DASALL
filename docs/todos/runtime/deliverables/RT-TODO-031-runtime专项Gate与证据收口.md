# RT-TODO-031 runtime 专项 Gate 与证据收口

日期：2026-04-23  
任务：RT-TODO-031  
状态：已完成

## 1. 本地证据

1. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 将 031 定义为 runtime 专项的证据收口任务：必须把 runtime-local gate、true integration gate、blocker 状态、残余风险与命令证据分开回写，不能再沿用“只看单条测试通过”的弱证据方式。
2. 在 031 开始前，RT-TODO-025 ~ 030 和 027 虽然都已有 deliverable / worklog，但专项 TODO 中的 blocker 校准表、Gate 执行证据表与 RT-TODO-031 状态仍未回写，因此专项结论仍停留在“事实已发生、文档未闭环”的中间态。
3. 当前工作区实际可用的验证面是 `build/vscode-linux-ninja`：
   - `ctest --test-dir build/vscode-linux-ninja -N` 已可发现 33 条 runtime 专项相关测试；
   - 其中覆盖 public surface、controller/unit、contract、fixture integration、true integration、resume/replay、profile、safe mode/health 与 main flow contract。
4. 第一轮用这 33 条测试做复验时，出现了 12 条 `Not Run`，但全部是 “CTest 可发现、可执行文件尚未生成” 的 build completeness 缺口，而不是断言失败：`SafeModeControllerTest`、`RuntimeEventBusTest`、`RuntimeTelemetryBridgeTest`、`RuntimeHealthProbeTest`、`RuntimeBackgroundMaintenanceHookTest`、`RuntimeUnaryFixtureIntegrationTest`、`RuntimeResumeIntegrationTest`、`RuntimeCheckpointReplayRegressionTest`、`RuntimeProfileCompatibilityTest`、`RuntimeSafeModeIntegrationTest`、`RuntimeHealthMaintenanceIntegrationTest`、`RuntimeCheckpointReplayCompatibilityTest`。
5. 使用 CMake Tools 补建这些缺失 runtime targets 后，重跑同一条 33 测试矩阵得到 `100% tests passed, 0 tests failed out of 33`。这说明 031 的真实工作不是修 gate，而是把 gate 证据从“历史单点通过”收口为“当前 build 面可重复执行”的闭环。
6. 仓库级残余 blocker 仍存在，但它们不属于 runtime 回归失败：
   - `tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp` 语法损坏，继续阻塞 `dasall_unit_tests` 聚合；
   - `InfraDiagnosticsSmokeTest` 与 `InfraDiagnosticsIntegrationTest` 继续阻塞 `dasall_integration_tests` 聚合。

## 2. 设计结论

1. Gate-RT-07 与 Gate-RT-11 必须继续分层：
   - Gate-RT-07 只证明 runtime-local fixture loop；
   - Gate-RT-11 才证明 true cross-module unary integration；
   - 两者都通过，才能说明 runtime 专项既完成 subsystem-local 控制平面，也完成最小真端口 unary 主链。
2. RT-BLK-01 已对 Gate-RT-11 的 unary gate 解阻。当前已验证的最小解法是：
   - cognition 最小 public seam；
   - runtime live unary wiring；
   - sqlite memory + builtin tool lane 的 integration 装配。
   knowledge / llm 的更宽 live 路径仍属后续扩展项，但不再阻塞本专项 true unary integration 结论。
3. Gate 证据需要分两层维护：
   - Gate-RT-01 ~ Gate-RT-06：沿用前序 RT-TODO-001 ~ 024 的 deliverable / worklog 证据，不在 031 内重复造第二份设计结论；
   - Gate-RT-07 ~ Gate-RT-11：在当前 `build/vscode-linux-ninja` 上做一次统一复验，证明 runtime 相关 executable 仍真实可运行。
4. Gate-RT-12 的通过条件不是“所有仓库聚合目标全绿”，而是“runtime 专项的 gate 证据、blocker 状态、残余外部 blocker 和后续动作被正确分账”。把 knowledge 语法损坏或 infra diagnostics 失败误记为 runtime 回归，反而会破坏专项证据质量。

## 3. Gate 收口矩阵

| Gate 范围 | 收口方式 | 031 结论 |
|---|---|---|
| Gate-RT-01 ~ Gate-RT-06 | 追溯前序 deliverable / worklog 与既有 public surface / controller / observability 资产 | 保持 Pass，031 只做追溯，不重复造新结论 |
| Gate-RT-07 ~ Gate-RT-10 | 在 `build/vscode-linux-ninja` 上做统一的 discoverability + ctest matrix 复验 | 全部 Pass |
| Gate-RT-11 | 复用 027 的真端口 unary gate，并在本轮矩阵中再次复验 | Pass，且与 Gate-RT-07 明确分层 |
| Gate-RT-12 | 回写 TODO、deliverable、worklog，并把 external blockers 与 runtime 通过结论分开记账 | Pass |

## 4. 边界 / 职责

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` | 作为专项 SSOT 回写任务状态、blocker 校准和 gate evidence | 不重复解释每个任务的实现细节 |
| `docs/worklog/DASALL_开发执行记录.md` | 记录 031 的执行路径、复验命令与 residual blocker 分账 | 不取代专项 TODO 的状态真值 |
| 既有 deliverables（001 ~ 030、027） | 提供任务级设计收敛证据 | 不负责专项级横向分账 |
| `build/vscode-linux-ninja` | 提供当前工作区可重复执行的 runtime gate 证据面 | 不代表仓库所有聚合 blocker 已清零 |

## 5. 数据 / 接口说明

1. 本轮新增或回写的数据面只涉及文档证据，不新增 production interface。
2. 专项 TODO 中两类表格是 031 的核心输出：
   - blocker 校准记录：`Blocker ID / 校准时间 / 校准结果 / 剩余阻塞范围 / 备注`
   - Gate 执行证据表：`Gate ID / 执行时间 / 执行命令 / 执行结果 / subsystem-local 结论 / cross-module blocker / 后继动作`
3. Gate-RT-07 ~ 11 的当前命令证据统一锚定到两条命令：
   - discoverability：`ctest --test-dir build/vscode-linux-ninja -N | rg "..."`
   - full matrix：`ctest --test-dir build/vscode-linux-ninja -R "^(...33 tests...)$" --output-on-failure`
4. 若 discoverability 已存在但 executable 缺失，必须先补建具体 target，再重跑同一条 ctest matrix；否则只能得出“build completeness 不足”，不能直接写成 gate 失败。

## 6. 流程 / 时序

1. 汇总 RT-TODO-025 ~ 030、027 的既有 deliverable / worklog，确认哪些 gate 已有任务级证据、哪些专项表格仍为空。
2. 在当前 `build/vscode-linux-ninja` 上执行 discoverability 检查，确认 runtime 专项相关 33 条测试仍可被 CTest 发现。
3. 执行 33 测试矩阵，识别 `Not Run` 的根因是 executable 缺失，而不是断言失败。
4. 使用 CMake Tools 补建缺失的 runtime unit / integration targets。
5. 重跑同一条 33 测试矩阵，确认全部通过。
6. 回写 RT-BLK-01 校准结果、Gate-RT-07 ~ Gate-RT-12 证据、RT-TODO-031 状态与 residual external blocker 分账。
7. 在 worklog 中新增 031 记录，并将专项结论与后续动作固定下来。

## 7. 文件范围

1. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md`
2. `docs/todos/runtime/deliverables/RT-TODO-031-runtime专项Gate与证据收口.md`
3. `docs/worklog/DASALL_开发执行记录.md`

## 8. Build 三件套

1. 代码目标：回写 runtime 专项 Gate / blocker / residual risk 证据，不新增 production 代码。
2. 测试目标：
   - runtime 专项 33 条 gate 相关测试的 discoverability；
   - runtime 专项 33 条 gate 相关测试的当前 build 面复验。
3. 验收命令：
   - `ctest --test-dir build/vscode-linux-ninja -N | rg "RuntimeControlPlaneSurfaceTest|RuntimeErrorCodeTest|CancellationTokenTest|AgentFsmTest|BudgetControllerTest|CheckpointManagerTest|RecoveryManagerTest|SchedulerTest|TransitionGuardTableTest|CheckpointStateMapperTest|SessionTypeSurfaceTest|SessionManagerTest|SafeModeControllerTest|AgentOrchestratorSkeletonTest|AgentOrchestratorControllerAssemblyTest|RuntimeTelemetryBridgeTest|RuntimeEventBusTest|RuntimeHealthProbeTest|RuntimeBackgroundMaintenanceHookTest|RuntimeBudgetContractTest|CheckpointFieldContractTest|MainFlowContractE2ETest|ReflectionDecisionContractTest|RecoveryRequestContractTest|RecoveryOutcomeContractTest|RuntimeUnaryFixtureIntegrationTest|RuntimeUnaryIntegrationTest|RuntimeResumeIntegrationTest|RuntimeCheckpointReplayRegressionTest|RuntimeProfileCompatibilityTest|RuntimeSafeModeIntegrationTest|RuntimeHealthMaintenanceIntegrationTest|RuntimeCheckpointReplayCompatibilityTest"`
   - `cmake --build build/vscode-linux-ninja --target dasall_runtime_safe_mode_controller_unit_test dasall_runtime_event_bus_unit_test dasall_runtime_telemetry_bridge_unit_test dasall_runtime_health_probe_unit_test dasall_runtime_background_maintenance_hook_unit_test dasall_runtime_unary_fixture_integration_test dasall_runtime_resume_integration_test dasall_runtime_checkpoint_replay_regression_test dasall_runtime_profile_compatibility_integration_test dasall_runtime_safe_mode_integration_test dasall_runtime_health_maintenance_integration_test dasall_runtime_checkpoint_replay_compatibility_integration_test`
   - `ctest --test-dir build/vscode-linux-ninja -R "^(RuntimeControlPlaneSurfaceTest|RuntimeErrorCodeTest|CancellationTokenTest|AgentFsmTest|BudgetControllerTest|CheckpointManagerTest|RecoveryManagerTest|SchedulerTest|TransitionGuardTableTest|CheckpointStateMapperTest|SessionTypeSurfaceTest|SessionManagerTest|SafeModeControllerTest|AgentOrchestratorSkeletonTest|AgentOrchestratorControllerAssemblyTest|RuntimeTelemetryBridgeTest|RuntimeEventBusTest|RuntimeHealthProbeTest|RuntimeBackgroundMaintenanceHookTest|RuntimeBudgetContractTest|CheckpointFieldContractTest|ReflectionDecisionContractTest|RecoveryRequestContractTest|RecoveryOutcomeContractTest|RuntimeCheckpointReplayCompatibilityTest|RuntimeUnaryFixtureIntegrationTest|RuntimeUnaryIntegrationTest|RuntimeResumeIntegrationTest|RuntimeCheckpointReplayRegressionTest|RuntimeProfileCompatibilityTest|RuntimeSafeModeIntegrationTest|RuntimeHealthMaintenanceIntegrationTest|MainFlowContractE2ETest)$" --output-on-failure`

## 9. 风险与回退

1. 如果未来 `ctest -N` 仍能列出测试，但实际 executable 缺失，不应直接把结果写成 gate fail；必须先按 target 补建，再判断是真失败还是 build completeness 问题。
2. `dasall_unit_tests` 与 `dasall_integration_tests` 的仓库级全量绿灯仍受外部 blocker 影响：
   - `tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp` 语法损坏；
   - `InfraDiagnosticsSmokeTest` / `InfraDiagnosticsIntegrationTest` 失败。
   这些问题必须继续独立记账，不能回流成 runtime 专项缺陷。
3. 若后续继续扩 true integration 范围，应新增独立 gate 去覆盖 true-port session persist、dependency unavailable live route 等更宽路径，而不是把这些需求回填到 Gate-RT-11，导致 gate 语义再次混层。