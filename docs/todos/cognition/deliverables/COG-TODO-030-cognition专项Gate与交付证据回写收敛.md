# COG-TODO-030 cognition 专项 Gate 与交付证据回写收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md

## 1. 任务边界与前置检查

1. 本任务前置依赖 `COG-TODO-026 ~ 029` 均已完成并具备交付物。
2. 阻塞项状态复核：
   - 已解阻：`COG-BLK-001`、`COG-BLK-002`、`COG-BLK-003`、`COG-BLK-004`、`COG-BLK-006`
   - 非阻塞保留：`COG-BLK-005`（supporting contracts 尚未 admission，按专项约束保持 module-local/module-public，不阻断 Gate 收口）

## 2. 命令证据

> 说明：`build-ci` 当前为 Ninja 生成器缓存，按仓库等效口径执行 `cmake -S . -B build-ci`，避免与 `-G "Unix Makefiles"` 发生 generator mismatch。

1. `cmake -S . -B build-ci`
2. `cmake --build build-ci --target dasall_cognition dasall_unit_tests dasall_contract_tests dasall_integration_tests`
3. `ctest --test-dir build-ci -N`
4. `ctest --test-dir build-ci --output-on-failure -R "Cognition|RuntimeCognitionLoopSmoke|GoalContractFieldContractTest|BeliefStateContractTest|ContextPacketFieldContractTest|ObservationContractTest|ReflectionDecisionContractTest|AgentResultContractTest|MainFlowContractE2ETest"`

结果摘要：

1. discoverability：`Total Tests: 733`。
2. 原始 `25/25` 统计包含后续评审确认的 profile compatibility 历史空跑别名，因此该计数不能继续作为真实通过率口径。
3. 经 COG-TODO-036 收敛后，cognition integration profile 证据应只统计真实测试名 `CognitionProfileCompatibilityTest`，不得再把历史空跑别名计入通过率。
4. 全量 integration 聚合目标存在仓库既有残余：
   - `InfraDiagnosticsSmokeTest` failed
   - `InfraDiagnosticsIntegrationTest` not run（可执行文件缺失）
   - `PluginAuditTraceIntegrationTest` not run（可执行文件缺失）
   - `PluginFailureObservabilityIntegrationTest` not run（可执行文件缺失）
   - `ProfilePluginMatrixIntegrationTest` not run（可执行文件缺失）

## 3. Gate 回写结论

| Gate | 状态 | 证据 | 说明 |
|---|---|---|---|
| Gate-COG-01 | Pass | COG-TODO-001 ~ 004 交付物 | 接缝统一门已闭环 |
| Gate-COG-02 | Pass | COG-TODO-005 ~ 006 交付物 | cognition 已退出 placeholder-only |
| Gate-COG-03 | Pass | COG-TODO-007 ~ 010 交付物 | 对象/接口冻结并保持 contracts 边界 |
| Gate-COG-04 | Pass | COG-TODO-011 ~ 013 交付物 | projector/resolver/input boundary 全绿 |
| Gate-COG-05 | Pass | COG-TODO-014 ~ 019 交付物 | 五段主链单测闭环 |
| Gate-COG-06 | Pass | COG-TODO-020 ~ 023 交付物 | bridge/validator/telemetry/facade 闭环 |
| Gate-COG-07 | Pass | COG-TODO-024 ~ 025 交付物；`ctest -N` | cognition integration 可发现 |
| Gate-COG-08 | Pass | `RuntimeCognitionLoopSmokeTest`、`CognitionRuntimeIntegrationTest` | runtime happy path 已打通 |
| Gate-COG-09 | Pass | `CognitionRuntimeInteractionContractTest`、`CognitionFailureInjectionIntegrationTest` | 交互契约与失败链路通过 |
| Gate-COG-10 | Pass（聚焦，口径待 036 更正）；Residual（全量） | 本任务命令证据 + COG-TODO-029 + COG-TODO-036 | profile compatibility 与证据回写已完成；原始聚焦计数含历史空跑别名，036 后应以真实 `CognitionProfileCompatibilityTest` 重建统计；全量 integration 仍有 infra/plugin 既有残余 |

## 4. 风险残留与后续动作

1. 残留风险：`dasall_integration_tests` 聚合目标包含 infra/plugin 既有异常，导致“全量 integration 全绿”不成立。
2. 影响判断：不阻断 cognition 专项 Gate 收口；cognition 相关聚焦矩阵仍全部通过。
3. 后续动作：
   - 在 infra/plugin 责任域补齐缺失可执行目标并修复 `InfraDiagnosticsSmokeTest`。
   - 修复后复跑全量聚合命令，更新专项 TODO 的风险残留状态。
