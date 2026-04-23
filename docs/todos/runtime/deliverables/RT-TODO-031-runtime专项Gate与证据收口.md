# RT-TODO-031 runtime 专项 Gate 与证据收口

日期：2026-04-23  
任务：RT-TODO-031  
状态：已完成

## 1. 本地证据

1. 031 的证据口径已从历史 build 目录复验升级为一次 clean-build 可复现 gate：证据唯一锚定到 `build-ci`，并要求从零开始完成 configure、target build 与 anchored `ctest` 矩阵。
2. 本轮 clean-build 使用以下 one-shot 命令完成：先删除 `build-ci`，再执行 `cmake -S . -B build-ci -G Ninja`、显式构建 33 个 runtime gate target，最后运行 33 条 runtime gate 测试的 anchored `ctest` 矩阵。
3. 2026-04-23 最新 clean-build 复验结果：`100% tests passed, 0 tests failed out of 33`。这次复验是在 `continue_from_checkpoint()` stale context 修复与 replay regression 装配对齐之后重新从零删除 `build-ci` 执行得到，说明 031 的 gate 仍然可以被任何人从空 build 目录一次性复现为 runtime gate 全绿。
4. 本轮 gate 同时覆盖了最新 runtime 修正后的关键路径：
   - `continue_from_checkpoint()` 现在会重新通过 memory manager 刷新 resume context，而不是合成 `ContextAssembled`；
   - `RuntimeResumeIntegrationTest` 新增了 mismatched `resume_token` 拒绝路径；
   - `RuntimeCheckpointReplayRegressionTest` 重新证明 valid waiting-tool fixture 能在新的 context refresh 语义下 replay 到 completed；
   - `AgentOrchestratorControllerAssemblyTest` 新增了 profile-driven safe terminal state 收敛验证，证明相同 recovery fact 会被 SafeModeController 按 policy 收敛到 `Degraded` 或 `FailedSafe`。
5. `RunCtest_CMakeTools` 在当前仓库仍可能只返回泛化的“生成失败”。按照仓库既有验证口径，这属于工具状态问题，不作为 031 gate 的判定来源；权威证据以显式 `build-ci` configure/build/ctest 为准。
6. 仓库级外部 blocker 仍需单独记账，但已经不再影响本专项 clean-build gate：
   - `tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp` 的既有损坏继续阻塞部分聚合 target；
   - diagnostics 相关聚合失败仍属于 infra 侧独立问题。  
   这些问题不能回流记成 runtime 专项 gate 失败。

## 2. 设计结论

1. Gate-RT-07 与 Gate-RT-11 继续保持分层语义，但它们的证据已经统一收口到同一条 clean-build gate：
   - Gate-RT-07 证明 runtime-local fixture / controller / FSM 组装面；
   - Gate-RT-11 证明 true cross-module unary / resume / replay / profile / safe-mode integration。
2. 031 的通过条件不再是“测试可发现”或“在某个已有 build 目录里补建过 target”，而是“零 build 目录下的一次完整 configure + build + anchored ctest 能复现 33/33 通过”。
3. 对 runtime gate 来说，`Unable to find executable` 一类 build completeness 缺口已经不再是可接受证据，因为 clean-build gate 会先显式构建全部目标，再运行统一矩阵。
4. 本轮 031 不新增 runtime public interface；它的职责是把最新 runtime 主链修正后的验证面收敛成可复制、可回归、可审计的一条 gate 命令。

## 3. Gate 收口矩阵

| Gate 范围 | 收口方式 | 031 结论 |
|---|---|---|
| Gate-RT-01 ~ Gate-RT-06 | 追溯前序 deliverable / worklog 证据，不重复造第二份实现结论 | 保持 Pass |
| Gate-RT-07 ~ Gate-RT-10 | 在 clean `build-ci` 上显式构建 unit / controller / observability / contract target，并执行 anchored `ctest` | Pass |
| Gate-RT-11 | 在同一 clean-build gate 中覆盖 unary / resume / replay / profile / safe-mode integration | Pass |
| Gate-RT-12 | 将 gate 命令、执行结果、工具状态问题与外部 blocker 明确分账 | Pass |

## 4. 边界 / 职责

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `docs/todos/runtime/deliverables/RT-TODO-031-runtime专项Gate与证据收口.md` | 固化 clean-build gate 命令、33/33 结果、工具状态说明与 blocker 分账 | 不重复展开所有 runtime 子任务实现细节 |
| `build-ci` | 提供一次从零开始可复现的 runtime gate 证据面 | 不代表仓库所有聚合 target 已全部清零 |
| `RunCtest_CMakeTools` | 作为 IDE 便捷入口 | 不作为 031 的权威证据来源 |
| 外部聚合 blocker | 独立记录和后续解阻 | 不回流为 runtime gate fail |

## 5. 数据 / 接口说明

1. 031 本轮新增的核心证据是“一条统一验收命令”与其执行结果，不新增 production interface。
2. gate 的测试面保持 33 条不变，但覆盖内容已随代码更新而增强：resume token 绑定拒绝、resume context refresh 与 replay fixture 回归、safe-mode policy 收敛都已经纳入现有测试二进制。
3. clean-build gate 的判定规则只有三步：
   - configure 成功；
   - 33 个 target 全部成功链接；
   - 33 条 anchored `ctest` 全部通过。
4. 若 IDE 测试入口继续报“生成失败”，但显式 `build-ci` 命令通过，则应记录为工具状态问题，而不是 runtime gate 回归。

## 6. 流程 / 时序

1. 删除 `build-ci`，确保证据来自零 build 目录，而不是增量构建残留。
2. 执行 `cmake -S . -B build-ci -G Ninja`，确认 clean configure 成功。
3. 显式构建 33 个 runtime gate 对应 target，避免 discoverability 与 executable completeness 脱节。
4. 运行 anchored `ctest` 矩阵，收集唯一权威结果。
5. 记录 CMake Tools 测试入口仍可能泛化失败，但不影响 clean-build gate 结论。
6. 回写本 deliverable，完成 031 的 gate 证据收口。

## 7. 文件范围

1. `runtime/src/AgentOrchestrator.h`
2. `runtime/src/AgentOrchestrator.cpp`
3. `runtime/src/fsm/TransitionGuardTable.cpp`
4. `tests/unit/runtime/TransitionGuardTableTest.cpp`
5. `tests/unit/runtime/AgentOrchestratorControllerAssemblyTest.cpp`
6. `tests/integration/agent_loop/RuntimeResumeIntegrationTest.cpp`
7. `tests/integration/agent_loop/RuntimeCheckpointReplayRegressionTest.cpp`
8. `tests/integration/agent_loop/CMakeLists.txt`
9. `docs/todos/runtime/deliverables/RT-TODO-031-runtime专项Gate与证据收口.md`

## 8. Build 三件套

1. 代码目标：
   - 将 SafeModeController 接入 AgentOrchestrator 的真实 recovery 主链；
   - 补齐 `Reflecting -> Degraded` 与 `Degraded/SafeMode -> Responding` 的 FSM 桥接；
   - 修复 `continue_from_checkpoint()` 的 stale context，让 resume 重新走真实 context 准备；
   - 补 `resume_token` 负面集成验证与 replay fixture 回归装配，并把 031 证据升级到 clean-build gate。
2. 测试目标：
   - 窄验证：`AgentOrchestratorControllerAssemblyTest`、`TransitionGuardTableTest`、`RuntimeResumeIntegrationTest`；
   - gate 验证：33 条 runtime gate 测试的一次 clean-build anchored 矩阵。
3. 验收命令：

```bash
cd /home/gangan/DASALL && rm -rf build-ci && cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_runtime_control_plane_surface_unit_test dasall_runtime_error_code_unit_test dasall_runtime_cancellation_token_unit_test dasall_agent_fsm_unit_test dasall_budget_controller_unit_test dasall_runtime_checkpoint_manager_unit_test dasall_runtime_recovery_manager_unit_test dasall_runtime_scheduler_surface_unit_test dasall_runtime_transition_guard_table_unit_test dasall_runtime_checkpoint_state_mapper_unit_test dasall_runtime_session_type_surface_unit_test dasall_runtime_session_manager_unit_test dasall_runtime_safe_mode_controller_unit_test dasall_runtime_agent_orchestrator_skeleton_unit_test dasall_runtime_agent_orchestrator_controller_assembly_unit_test dasall_runtime_telemetry_bridge_unit_test dasall_runtime_event_bus_unit_test dasall_runtime_health_probe_unit_test dasall_runtime_background_maintenance_hook_unit_test dasall_contract_runtime_budget_test dasall_contract_checkpoint_field_test dasall_contract_main_flow_e2e_test dasall_contract_reflection_decision_test dasall_contract_recovery_request_test dasall_contract_recovery_outcome_test dasall_runtime_unary_fixture_integration_test dasall_runtime_unary_integration_test dasall_runtime_resume_integration_test dasall_runtime_checkpoint_replay_regression_test dasall_runtime_profile_compatibility_integration_test dasall_runtime_safe_mode_integration_test dasall_runtime_health_maintenance_integration_test dasall_runtime_checkpoint_replay_compatibility_integration_test && ctest --test-dir build-ci -R "^(RuntimeControlPlaneSurfaceTest|RuntimeErrorCodeTest|CancellationTokenTest|AgentFsmTest|BudgetControllerTest|CheckpointManagerTest|RecoveryManagerTest|SchedulerTest|TransitionGuardTableTest|CheckpointStateMapperTest|SessionTypeSurfaceTest|SessionManagerTest|SafeModeControllerTest|AgentOrchestratorSkeletonTest|AgentOrchestratorControllerAssemblyTest|RuntimeTelemetryBridgeTest|RuntimeEventBusTest|RuntimeHealthProbeTest|RuntimeBackgroundMaintenanceHookTest|RuntimeBudgetContractTest|CheckpointFieldContractTest|MainFlowContractE2ETest|ReflectionDecisionContractTest|RecoveryRequestContractTest|RecoveryOutcomeContractTest|RuntimeUnaryFixtureIntegrationTest|RuntimeUnaryIntegrationTest|RuntimeResumeIntegrationTest|RuntimeCheckpointReplayRegressionTest|RuntimeProfileCompatibilityTest|RuntimeSafeModeIntegrationTest|RuntimeHealthMaintenanceIntegrationTest|RuntimeCheckpointReplayCompatibilityTest)$" --output-on-failure
```

4. 2026-04-23 最新复验结果：`100% tests passed, 0 tests failed out of 33`。

## 9. 风险与回退

1. 若 clean configure 失败，应先记录为环境 / 依赖问题，不能直接判 runtime gate fail。
2. 若 target 构建失败，应按具体 target 归因到代码回归，而不是退回 discoverability 口径。
3. 若 `RunCtest_CMakeTools` 继续报泛化“生成失败”，应沿用显式 `build-ci` 命令收集证据，并在结论中注明 IDE 工具状态异常。
4. 仓库其它子系统的聚合 blocker 仍需独立处理；只要本命令链保持 33/33 通过，就不能把外部问题误记为 runtime 专项回归。