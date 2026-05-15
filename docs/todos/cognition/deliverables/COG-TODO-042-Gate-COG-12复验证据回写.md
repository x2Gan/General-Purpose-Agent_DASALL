# COG-TODO-042 Gate-COG-12复验证据回写

状态：Done
日期：2026-05-15
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Gate 复验与统一验收证据回写

## 1. 任务边界

1. 本任务只复验 Gate-COG-12 的统一验收命令，并把结论分层回写到 deliverable / TODO / worklog。
2. 本任务不继续修改 cognition / runtime 生产代码；若统一命令受 repo-wide 既有 blocker 阻断，只记录 owner、最小解阻条件与 cognition scope 结论。
3. 本任务不提前关闭 COG-TODO-043 / 044；文档一致性和 warning hygiene 仍按后续原子任务单独收口。

## 2. 复验命令与结果

1. `cmake -S . -B build-ci-cog042 -G "Unix Makefiles"`
   - 结果：通过，干净目录成功配置。
2. `cmake --build build-ci-cog042 --target dasall_cognition dasall_unit_tests dasall_contract_tests dasall_integration_tests`
   - 结果：`dasall_cognition` 构建通过，并进入 `dasall_unit_tests` 的 586 条 unit 集合执行；`RuntimeCognitionLoopSmokeTest` 已在该聚合中真实通过，说明 COG-TODO-039 的 runtime smoke 聚合缺口已关闭。
   - 阻断：`dasall_unit_tests` 在 repo-wide 非 cognition 残余上失败，后续 `dasall_contract_tests` / `dasall_integration_tests` 未继续执行。
   - 首批 non-cognition blockers：
     - runtime：`RuntimeDependencySetReadinessTest` missing executable
     - llm：`LLMBaselineAssetPathTest` missing executable
     - tools：`BuiltinExecutorLaneResultCodeTest` missing executable
     - infra：`DiagnosticsFixtureSurfaceTest`、`DiagnosticsSnapshotStoreContractTest`、`HealthConfigPolicyTest`、`ProbeSchedulerTest` missing executable
     - platform / profiles：`UnixIpcProviderLoopbackTest`、`DaemonProfileProjectionTest` missing executable
     - access / daemon：`DaemonReadinessCommandTest` failed (`gateway should initialize for readiness command test`)，`DaemonCancelCommandTest` failed (`async submit should be accepted: expected=2 actual=0`)，`AccessCancelForwardingTest` failed with the same cancel path failure
3. `ctest --test-dir build-ci-cog042 -N`
   - 结果：注册发现 `Total Tests: 885`，满足“发现 733+ tests”的数量门槛。
   - 残余：由于上一条统一聚合在 `dasall_unit_tests` 提前失败，目录内仍存在大量 contract / integration executable 缺失，因此 `ctest -N` 会继续报告 missing executable；这说明当前 gate 的主要阻断不在 cognition owner，而在 repo-wide 聚合依赖未闭合。
4. `ctest --test-dir build-ci-cog042 --output-on-failure -R "Cognition|RuntimeCognitionLoopSmoke|GoalContractFieldContractTest|BeliefStateContractTest|ContextPacketFieldContractTest|ObservationContractTest|ReflectionDecisionContractTest|AgentResultContractTest|MainFlowContractE2ETest"`
   - 结果：30 条匹配测试中，前 15 条 cognition / runtime unit slice 通过，包括 `RuntimeCognitionLoopSmokeTest`、`CognitionInterfaceSurfaceTest`、`CognitionFacadeFlowTest`、`MockCognitionFixtureSurfaceTest` 等。
   - 残余：后 15 条 contract / cognition integration slice 全部 `Not Run`，根因不是 cognition 行为断言失败，而是 contract / integration executables 尚未由统一聚合产出，包括 `GoalContractFieldContractTest`、`ObservationContractTest`、`BeliefStateContractTest`、`ContextPacketFieldContractTest`、`AgentResultContractTest`、`MainFlowContractE2ETest`、`ReflectionDecisionContractTest` 以及 `CognitionRuntimeIntegrationTest`、`CognitionRuntimeInteractionContractTest`、`CognitionFailureInjectionIntegrationTest`、`CognitionProfileCompatibilityTest`、`CognitionRuntimePolicyProjectionIntegrationTest`、`CognitionReviewRegressionTest`、`CognitionStructuredOutputIntegrationTest`。

## 3. Owner 归因与最小解阻条件

1. runtime / llm / tools / infra / platform / profiles 的 `Not Run` 缺口都表现为“已注册 test 名称，但 clean gate 目录中没有对应 executable”。最小解阻条件是：把这些 test executable 纳入各自聚合 target 的真实 build 依赖，使 `dasall_unit_tests`、`dasall_contract_tests`、`dasall_integration_tests` 在运行 `ctest` 前先产出对应 binary。
2. access / daemon 当前存在真实功能失败，而不是单纯 discoverability 缺口。最小解阻条件是：
   - `DaemonReadinessCommandTest`：修复 readiness command 测试场景下 gateway 初始化失败。
   - `DaemonCancelCommandTest` 与 `AccessCancelForwardingTest`：修复 cancel path 的 async submit 未被接受问题，使预期 `expected=2 actual=0` 收敛。
3. 由于 `dasall_unit_tests` 已在第 532 / 548 / 549 条失败并退出，`dasall_contract_tests` 与 `dasall_integration_tests` 没有获得执行机会；因此 focused regex 中 contract / cognition integration 的 `Not Run` 应视为上游 gate 聚合阻断的连带结果，而不是本次 039 ~ 041 cognition 修复的回归。

## 4. Cognition Scope 结论

1. COG-TODO-039 的核心修复已在 clean gate 目录内得到复验：`RuntimeCognitionLoopSmokeTest` 已被统一 unit 聚合真实执行且通过，说明 runtime smoke executable 不再停留在“仅注册未聚合”状态。
2. COG-TODO-040 / 041 的 unit slice 也没有在 Gate-COG-12 复验中出现新的 cognition owner 失败：`CognitionInterfaceSurfaceTest`、`CognitionFacadeFlowTest`、`MockCognitionFixtureSurfaceTest` 等均通过。
3. 当前 Gate-COG-12 仍不能标为 Pass，但根因已不再是 2026-04-28 复验时发现的 cognition 主链缺口；它现阶段被 repo-wide 非 cognition 聚合依赖缺口与 access/daemon 失败阻断。

## 5. 回写结论

COG-TODO-042 已完成。

1. Gate-COG-12 已完成一次新的 clean Unix Makefiles 复验，不再只依赖历史 Gate-COG-11 的 Pass 记录。
2. Gate-COG-12 当前状态应保持 `Changes Requested / Pending`：一方面 043 / 044 仍待完成，另一方面 unified acceptance 仍被 non-cognition owner blockers 阻断。
3. 风险状态可以收敛为：
   - `COG-R19` / `COG-R20`：已由 039 的 runtime smoke 聚合与 canonical route fixture 修复关闭。
   - `COG-R21` / `COG-R22`：已分别由 040 / 041 的 focused validation 关闭，且本轮 gate 复验未见 cognition unit 反证。
   - `COG-R23`：仍保持 Open，等待 043 / 044 完成文档一致性与 warning hygiene 收口。