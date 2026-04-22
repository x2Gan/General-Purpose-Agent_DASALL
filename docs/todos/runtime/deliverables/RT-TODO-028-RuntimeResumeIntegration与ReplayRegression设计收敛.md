# RT-TODO-028 RuntimeResumeIntegration 与 Replay Regression 设计收敛

日期：2026-04-22  
任务：RT-TODO-028  
状态：已完成

## 1. 本地证据

1. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 将 028 固定为 waiting-state resume、incompatible schema reject 与 golden checkpoint replay regression 三件事的合并 gate，且明确说明本轮只能宣称 runtime-owned resume 语义已验证，不能替代 027 的 true integration。
2. `runtime/src/AgentOrchestrator.cpp` 中 `handle_waiting_state(...)` 已经能在 runtime-local in-memory session/checkpoint store 上完成 `load_session -> prepare_turn -> build_resume_seed -> load checkpoint -> make_resume_plan -> continue_from_checkpoint(...)`，说明 028 的主缺口不在 controller 自身，而在 facade 侧的 waiting session dispatcher 和 replay fixture 注入面。
3. 026 已把 `AgentFacade::handle(...)` 接到 `AgentOrchestrator::run_once(...)`，但 `resume(...)` 仍停在 fail-closed 文本层，因此 028 的最短控制路径就是 facade 对 waiting session / checkpoint 的保存与转发。
4. 024 已提供 `tests/fixtures/runtime/checkpoints/` 下的 replay fixtures 与 `RuntimeCheckpointReplayCompatibilityTest.cpp`，说明 028 不需要重造 checkpoint fixture，只需要把这些固定输入接到 facade / orchestrator 的真实 resume 路径上。

## 2. 设计结论

1. `RuntimeDependencySet` 在 028 中升级为 runtime-local stub/seed seam：
   - `local_stub_ports` 控制 facade 初始化时的 orchestrator 主循环出口；
   - `seeded_waiting_session` 与 `seeded_checkpoints` 用于把 024 的 replay fixture 注入 runtime-local resume 路径。
2. `AgentFacade` 必须保存最近一个可恢复 waiting session，并在 `resume(...)` 收到完整锚点后把请求分发给 `AgentOrchestrator::handle_waiting_state(...)`；若当前没有活跃 waiting session，必须 fail-closed。
3. `AgentOrchestrator::continue_from_checkpoint(...)` 不能只支持 `WaitingClarify`，还必须支持 024 fixture 对应的 `WaitingExternal -> Reflecting -> Reasoning -> Responding` replay 路径，否则 replay regression 只能停留在 `CheckpointManager` 层，无法证明 runtime-owned resume 真正闭环。
4. 028 的测试面拆成两层：
   - `RuntimeResumeIntegrationTest` 验证 facade 从 waiting clarify 主链恢复；
   - `RuntimeCheckpointReplayRegressionTest` 复用 024 的 waiting-tool / schema-v2 fixture，验证 replay success 与 incompatible schema reject。

## 3. 边界 / 职责

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `RuntimeDependencySet` | 提供 runtime-local stub/seed seam | 不承载真实相邻模块端口 |
| `AgentFacade::resume(...)` | 维护 active waiting session，并转发到 `handle_waiting_state(...)` | 不处理 true persistence / cross-module resume |
| `AgentOrchestrator::continue_from_checkpoint(...)` | 覆盖 `WaitingClarify` 与 `WaitingExternal` 两类 runtime-owned replay 路径 | 不代替 027 的真端口 resume |
| `RuntimeResumeIntegrationTest` | 证明 facade waiting resume 主成功链 | 不验证 golden fixture schema 兼容 |
| `RuntimeCheckpointReplayRegressionTest` | 复用 024 fixture 证明 replay success / schema reject | 不宣称真实存储 round-trip ready |

## 4. 数据 / 接口说明

1. `RuntimeDependencySet.h` 新增三类 runtime-local 配置：
   - `RuntimeLocalStubPorts`：映射到 `OrchestratorStubPorts`；
   - `seeded_waiting_session`：提供 facade 初始可恢复 session；
   - `seeded_checkpoints`：提供 replay fixture 注入点。
2. facade 保存的 waiting session 需要满足两个条件：
   - `active_checkpoint_ref` 非空；
   - `pending_interaction` 激活，且 FSM 状态处于 waiting 系列。
3. replay regression 使用 024 的 `replay_waiting_tool_v1.fixture` 与 `replay_waiting_tool_schema_v2.fixture`，不复制 fixture 文件，只在测试侧补 runtime-local session snapshot。

## 5. 流程 / 时序

1. `AgentFacade::init(...)` 根据 `RuntimeDependencySet` 构建 orchestrator stub 配置，并在需要时 seed checkpoint / waiting session。
2. waiting 主链场景：`handle(...)` 进入 waiting clarify，facade 缓存可恢复 session；随后 `resume(...)` 通过 `handle_waiting_state(...)` 继续执行并返回 completed result。
3. replay regression 场景：测试在 init 阶段预装 024 fixture 与 waiting session，随后直接调用 `resume(...)`；`handle_waiting_state(...)` 读取 seeded checkpoint，生成 resume plan，再进入 `continue_from_checkpoint(...)`。
4. 对 `WaitingExternal` replay，`continue_from_checkpoint(...)` 必须走 `Reflecting -> Reasoning -> Responding`，而不是跳过 tool replay 直接 terminalize。

## 6. 文件范围

1. `runtime/include/RuntimeDependencySet.h`
2. `runtime/src/AgentFacade.cpp`
3. `runtime/src/AgentOrchestrator.h`
4. `runtime/src/AgentOrchestrator.cpp`
5. `runtime/src/session/SessionManager.h`
6. `runtime/src/session/SessionManager.cpp`
7. `tests/fixtures/runtime/RuntimeUnaryFixture.h`
8. `tests/integration/agent_loop/CMakeLists.txt`
9. `tests/integration/agent_loop/RuntimeResumeIntegrationTest.cpp`
10. `tests/integration/agent_loop/RuntimeCheckpointReplayRegressionTest.cpp`

## 7. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| facade waiting resume dispatcher | `runtime/src/AgentFacade.cpp` |
| runtime-local stub / seed seam | `runtime/include/RuntimeDependencySet.h`、`tests/fixtures/runtime/RuntimeUnaryFixture.h` |
| waiting clarify resume gate | `tests/integration/agent_loop/RuntimeResumeIntegrationTest.cpp` |
| waiting external replay regression | `runtime/src/AgentOrchestrator.cpp`、`tests/integration/agent_loop/RuntimeCheckpointReplayRegressionTest.cpp` |

## 8. Build 三件套

1. 代码目标：为 facade 增加 waiting session dispatcher；为 orchestrator 增加 `WaitingExternal` replay；为 dependency seam 增加 stub/seed 能力。
2. 测试目标：`RuntimeResumeIntegrationTest`、`RuntimeCheckpointReplayRegressionTest`，并回归 `RuntimeControlPlaneSurfaceTest`、`AgentOrchestratorControllerAssemblyTest`、`RuntimeUnaryFixtureIntegrationTest`。
3. 验收命令：
   - `cmake --build build-ci --target dasall_runtime_control_plane_surface_unit_test dasall_runtime_agent_orchestrator_controller_assembly_unit_test dasall_runtime_unary_fixture_integration_test dasall_runtime_resume_integration_test dasall_runtime_checkpoint_replay_regression_test && ctest --test-dir build-ci -R "^(RuntimeControlPlaneSurfaceTest|AgentOrchestratorControllerAssemblyTest|RuntimeUnaryFixtureIntegrationTest|RuntimeResumeIntegrationTest|RuntimeCheckpointReplayRegressionTest)$" --output-on-failure`
   - `ctest --test-dir build-ci -N | rg "RuntimeResumeIntegrationTest|RuntimeCheckpointReplayRegressionTest"`

## 9. 风险与回退

1. 如果 facade 不保存 waiting session，只能在 unit 层直接调用 `handle_waiting_state(...)`，会违背 028 对 public control-plane resume 的要求。
2. 如果 replay regression 仍只验证 `CheckpointManager` 的 `make_resume_plan(...)`，则无法证明 `WaitingExternal` 恢复后能真正回到 response / checkpoint terminalize。
3. 如果把 024 fixture 修改成 028 私有版本，会破坏 replay-safe regression 的基线一致性；本轮必须复用原 fixture 文件。