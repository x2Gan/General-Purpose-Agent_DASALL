# RT-TODO-026 RuntimeUnaryFixtureIntegration 设计收敛

日期：2026-04-22  
任务：RT-TODO-026  
状态：已完成

## 1. 本地证据

1. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 将 RT-TODO-026 定义为 runtime-local unary fixture 主成功链，完成判定明确要求链路真实经过 `AgentFacade` 与 `AgentOrchestrator`，但不得外推为 true integration ready。
2. `runtime/src/AgentFacade.cpp` 当前虽然已经完成 init 最小校验，但 `handle()` 仍返回“AgentOrchestrator 未接线”的 fail-closed 文本，这意味着 026 的主成功链缺口直接落在 facade -> orchestrator handoff，而不是 Session/Budget/FSM/Checkpoint 控制器本身。
3. `tests/unit/runtime/AgentOrchestratorSkeletonTest.cpp` 与 `AgentOrchestratorControllerAssemblyTest.cpp` 已经证明默认 `AgentOrchestrator::run_once(...)` 可在 runtime-local stub/null seam 下走通 direct success、tool->abort_safe 和 waiting->resume 三类路径，因此 026 不需要重写 orchestrator，只需要把 facade 接过去并落一个 subsystem-local integration test。
4. RT-TODO-003 与 RT-TODO-004 已冻结 `RuntimeDependencySet` 的 fail-closed / null-adapter seam 语义与 `tests/fixtures/runtime/` 的统一 fixture 根，这允许 026 使用 runtime-owned fixture 组装最小 init 请求，而不引入真实相邻模块端口。
5. RT-TODO-025 已把 `RuntimeControlPlaneSurfaceTest`、fixture root 和 runtime integration discoverability 接线完成，因此 026 只需在既有 discoverability 骨架上新增 unary fixture integration target。

## 2. 外部参考

1. AWS 的 Hexagonal Architecture 指南指出，ports/adapters 的核心价值之一是让应用组件在没有数据库、UI 或外部 API 依赖的前提下独立测试；通过替换 adapter，可以保持业务逻辑可验证而不把隔离测试误报成真实基础设施联调。
2. 这与 026 的边界完全一致：`AgentFacade` 是 runtime 对外 port，`AgentOrchestrator` 是 runtime 内部控制面，`tests/fixtures/runtime/` 提供的最小依赖集合只证明 subsystem-local 闭环成立，不证明真实端口 ready。

## 3. 设计结论

1. `AgentFacade::init(...)` 负责冻结 runtime-local unary 运行所需的最小 composition：`runtime_instance_id`、`profile_id`、`policy_snapshot`、`dependency_set`，并在通过最小校验后构造一个默认 `AgentOrchestrator`。
2. `AgentFacade::handle(...)` 在 026 开始不再返回 “orchestrator 未接线” 的 fail-closed 文本；它必须把请求直接委派给 `AgentOrchestrator::run_once(...)`，并把 `agent_result` 作为 facade 出口返回。
3. 026 只打开 unary 主成功链。`resume(...)` 仍不承担 runtime-owned waiting/resume dispatcher，这条路径留给 028 结合 checkpoint replay regression 一起收口。
4. 为了避免在 unit test 与 integration test 中重复拼装 policy snapshot / init request / agent request，runtime unary fixture 应沉淀到 `tests/fixtures/runtime/RuntimeUnaryFixture.h`，作为 runtime-owned deterministic fixture 资产。

## 4. 边界 / 职责

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `AgentFacade::handle(...)` | 把 unary 请求转发给 `AgentOrchestrator::run_once(...)` | 不处理 waiting/resume dispatcher |
| `tests/fixtures/runtime/RuntimeUnaryFixture.h` | 统一组装最小 policy snapshot、init request 与 unary request | 不承载真实相邻模块实现 |
| `RuntimeUnaryFixtureIntegrationTest` | 证明 `AgentFacade -> AgentOrchestrator` 的 runtime-local 主成功链成立 | 不宣称 true cross-module integration ready |
| `RuntimeControlPlaneSurfaceTest` | 保持 public-surface gate，但更新为“成功 handoff + resume reject”语义 | 不替代 integration 级 success gate |

## 5. 数据 / 接口说明

1. fixture 资产仅使用 runtime 已冻结 public surface：`IAgent`、`AgentFacade`、`AgentInitRequest`、`ResumeHandleRequest`、`contracts::AgentRequest`。
2. `RuntimeDependencySet` 在 026 仍只需要一个最小测试侧定义，因为 `AgentOrchestrator` 默认 direct-success composition 不读取真实外部 adapter；本轮不扩写出新的 shared/public dependency graph。
3. unary fixture integration 的成功判据至少包含：`AgentResultStatus::Completed`、`task_completed=true`、`checkpoint_ref` 已返回、请求与 trace 关联字段保留。

## 6. 流程 / 时序

1. `AgentFacade::init(...)` 校验最小请求后构建 runtime-local `AgentOrchestrator`。
2. `AgentFacade::handle(...)` 将 `AgentRequest` 直接委派到 `run_once(...)`。
3. `AgentOrchestrator::run_once(...)` 复用既有 Session/Budget/FSM/Checkpoint 控制器默认 direct-success 路径。
4. `RuntimeUnaryFixtureIntegrationTest` 通过 fixture 头初始化 facade，再发送 unary request，断言 completed result 与 checkpoint anchor。
5. `RuntimeControlPlaneSurfaceTest` 同步更新为 public-surface handoff 验证，确保 025 的 surface gate 与 026 的成功链不互相矛盾。

## 7. 文件范围

1. `runtime/src/AgentFacade.cpp`
2. `tests/fixtures/runtime/RuntimeUnaryFixture.h`
3. `tests/unit/runtime/RuntimeControlPlaneSurfaceTest.cpp`
4. `tests/unit/runtime/CMakeLists.txt`
5. `tests/integration/agent_loop/CMakeLists.txt`
6. `tests/integration/agent_loop/RuntimeUnaryFixtureIntegrationTest.cpp`

## 8. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| facade -> orchestrator handoff | `runtime/src/AgentFacade.cpp` |
| runtime unary deterministic fixture | `tests/fixtures/runtime/RuntimeUnaryFixture.h` |
| unary fixture integration gate | `tests/integration/agent_loop/RuntimeUnaryFixtureIntegrationTest.cpp` |
| public-surface gate 与 integration fixture 共享输入 | `tests/unit/runtime/RuntimeControlPlaneSurfaceTest.cpp`、对应 CMake |

## 9. Build 三件套

1. 代码目标：把 `AgentFacade::handle(...)` 接到 `AgentOrchestrator::run_once(...)`；新增 runtime unary fixture 头和 unary fixture integration target。
2. 测试目标：`RuntimeUnaryFixtureIntegrationTest` 至少验证 completed unary success；`RuntimeControlPlaneSurfaceTest` 同步验证 facade handoff；复用 `AgentOrchestratorSkeletonTest` 作为内部 stage 证明。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_control_plane_surface_unit_test dasall_runtime_unary_fixture_integration_test dasall_runtime_agent_orchestrator_skeleton_unit_test && ctest --test-dir build-ci -R "^(RuntimeControlPlaneSurfaceTest|RuntimeUnaryFixtureIntegrationTest|AgentOrchestratorSkeletonTest)$" --output-on-failure`

## 10. 风险与回退

1. 如果 026 继续让 facade 停在 fail-closed 文本层，`RuntimeUnaryFixtureIntegrationTest` 就只能绕过 facade 直接打 orchestrator，会违背任务的真实控制面要求，应回退到 facade handoff 接线。
2. 如果 026 顺手把 resume dispatcher 也接入 facade，会和 028 的 checkpoint/replay gate 混层；本轮必须只收口 unary 成功链。
3. 如果 fixture 资产散落到 test 文件内部而不进入 `tests/fixtures/runtime/`，后续 028/029/030 将重复组装 policy/request 输入，需回退到统一 fixture 头。