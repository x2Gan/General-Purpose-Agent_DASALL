# RT-FIX-003 durable checkpoint / resume gate closeout

来源任务：RT-FIX-003
完成日期：2026-05-21
关联缺口：RT-GAP-003
关联设计：`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/todos/runtime/deliverables/RT-TODO-024-CheckpointReplayCompatibility设计收敛.md`、`docs/todos/runtime/deliverables/RT-TODO-028-RuntimeResumeIntegration与ReplayRegression设计收敛.md`

## 1. 任务边界

1. 本轮只收口 runtime owner 内部的 durable checkpoint / session / resume gate，不扩张到 app-binary、installed package、release runner 或 qemu 证据。
2. authoritative 问题定义固定为：waiting checkpoint 必须能在新的 `AgentFacade` 实例中通过 durable root 重载，并继续走 clarify / waiting external replay；schema/version mismatch 必须继续 fail-closed。
3. 用户已明确禁止使用 qemu / kvm；本轮只使用 build-tree focused unit / integration tests 作为权威证据。

## 2. 本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| checkpoint durable owner | `runtime/src/checkpoint/CheckpointManager.h/.cpp` 新增 `durable_state_root`、module-local key=value 持久化、budget sidecar 写回与 `load()` durable fallback | waiting checkpoint 不再只存在进程内 map；新的 manager 实例可以从同一 durable root 重新加载 checkpoint |
| session durable owner | `runtime/src/session/SessionManager.h/.cpp` 新增 `durable_state_root`、按 `session_id` 建模的 snapshot map、durable session writeback/readback 与 `load_session()` fallback | waiting session/pending interaction 不再只依赖当前进程内 `stored_snapshot_`；新的 manager 实例可恢复同一 session |
| composition / resume seam | `runtime/include/RuntimeDependencySet.h`、`runtime/src/AgentOrchestrator.cpp`、`runtime/src/AgentFacade.cpp` 现已透传 durable root，并允许 `resume()` 在无进程内 waiting session 时先从 durable store 重载 canonical snapshot | runtime resume 不再要求旧进程还握着 `waiting_session`；重启后的 facade 也能进入 waiting-state dispatch |
| manager focused regression | `tests/unit/runtime/CheckpointManagerTest.cpp`、`tests/unit/runtime/SessionManagerTest.cpp` 已补跨实例 durable reload case | durable owner 的最小行为在 unit 层可独立二值判定 |
| cross-instance clarify resume | 新增 `tests/integration/agent_loop/RuntimeDurableResumeIntegrationTest.cpp` | 第一实例生成 waiting checkpoint 后，第二实例共享 durable root 即可恢复并完成 clarify 路径 |
| waiting-tool replay + guard retention | `tests/integration/agent_loop/RuntimeCheckpointReplayRegressionTest.cpp` 现已覆盖 durable restart replay；既有 schema-v2 fixture reject 继续保留 | `continue_from_checkpoint()` 的 waiting external/tool observation replay 现具跨实例 durable 证据，同时未放松 schema/version guard |

## 3. 设计结论

1. durable checkpoint / session 的 owner 固定在 runtime 内部的 `CheckpointManager` 与 `SessionManager`，而不是把格式、生命周期或恢复准入外推给 shared contracts、memory 或 app layer。这样可以继续守住 ADR-006/007/008：memory 只提供 resume 时的上下文装配，recovery admission 与全局控制仍归 runtime。
2. durable 持久化格式采用 runtime module-local 的 key=value 文件，并对字符串字段做十六进制编码；这是为了最小化依赖面，避免为本轮引入新的 shared serialization surface。该格式只作为 runtime owner 的内部实现细节，不构成对外契约。
3. `RuntimeDependencySet.durable_state_root` 现在是 runtime composition 的唯一 durable seam。`AgentOrchestrator` 在构造时把它下发给 checkpoint/session owner，因此 seeded checkpoint/session、waiting turn writeback 与后续 resume reload 都落在同一 durable root 下。
4. `AgentFacade::resume()` 不再把“当前进程内必须已有 active waiting session”当作硬前置，而是只在本地快照存在时执行 session/checkpoint 绑定校验；若本地快照缺失，则交由 `SessionManager::load_session()` 从 durable store 重载 canonical waiting session。`AgentOrchestrator::handle_waiting_state()` 也改为先 reload，再验证 pending interaction / active checkpoint invariant。
5. `continue_from_checkpoint()` 的 replay-safe 语义没有被新的 durable gate 稀释。waiting external action / tool observation 仍走 checkpoint replay 兼容路径；schema/version mismatch 仍在 checkpoint load 阶段 fail-closed，并由 regression fixture 固定。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 落点 |
|---|---|
| checkpoint owner 持久化 waiting / budget sidecar | `runtime/src/checkpoint/CheckpointManager.h`、`runtime/src/checkpoint/CheckpointManager.cpp`、`tests/unit/runtime/CheckpointManagerTest.cpp` |
| session owner 持久化 waiting session / pending interaction | `runtime/src/session/SessionManager.h`、`runtime/src/session/SessionManager.cpp`、`tests/unit/runtime/SessionManagerTest.cpp` |
| runtime composition 下发 durable root，resume 可在无进程内 session 时重载 | `runtime/include/RuntimeDependencySet.h`、`runtime/src/AgentOrchestrator.cpp`、`runtime/src/AgentFacade.cpp` |
| cross-instance clarify waiting checkpoint 可恢复 | `tests/integration/agent_loop/RuntimeDurableResumeIntegrationTest.cpp` |
| waiting-tool replay after restart + schema guard | `tests/integration/agent_loop/RuntimeCheckpointReplayRegressionTest.cpp` |
| integration target 注册 | `tests/integration/agent_loop/CMakeLists.txt` |

## 5. D Gate

1. 范围单一：只处理 `RT-FIX-003` / `RT-GAP-003`。
2. 本轮不引入 installed package / app-binary / release-runner 级别的 durable evidence，也不把当前结果外推为 `RT-GAP-008` 已完成。
3. 本轮不使用 qemu / kvm；更高层环境证据仍留给后续 runtime / packaging 任务。

## 6. 验证结果

1. `cmake --build build/vscode-linux-ninja --target dasall_runtime_resume_integration_test dasall_runtime_checkpoint_replay_regression_test dasall_runtime_durable_resume_integration_test`：通过。
2. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(CheckpointManagerTest|SessionManagerTest|RuntimeResumeIntegrationTest|RuntimeCheckpointReplayRegressionTest|RuntimeDurableResumeIntegrationTest)$'`：通过。

## 7. 完成判定

1. `RT-GAP-003` 已在当前树关闭：runtime 现已拥有 durable checkpoint/session store seam，waiting checkpoint 不再只依赖 in-memory synthetic resume。
2. `RuntimeDurableResumeIntegrationTest` 证明了 clarify waiting checkpoint 可在新 `AgentFacade` 实例中从 durable root 恢复并完成。
3. `RuntimeCheckpointReplayRegressionTest` 现同时证明 waiting external/tool observation 的 durable restart replay 仍可完成，而 schema/version mismatch fixture 继续显式拒绝。
4. 本结论不外推为 installed package、release-runner、qemu 或 app-binary durable evidence 已具备；这些仍属于后续任务范围。