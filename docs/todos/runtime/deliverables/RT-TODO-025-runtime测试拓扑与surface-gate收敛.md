# RT-TODO-025 runtime 测试拓扑与 surface gate 收敛

日期：2026-04-22  
任务：RT-TODO-025  
状态：已完成

## 1. 本地证据

1. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 将 RT-TODO-025 定义为接线 runtime unit、integration、fixture 测试拓扑，并把旧 `RuntimeSmokeTest` 从 gate 证据中降级出去。
2. `tests/unit/CMakeLists.txt` 当前只把一小部分 runtime unit target 暴露给顶层 `dasall_unit_tests`，`AgentOrchestrator`、`SessionManager`、`SafeModeController`、`RuntimeEventBus`、`RuntimeTelemetryBridge`、`RuntimeHealthProbe`、`RuntimeBackgroundMaintenanceHook` 等 runtime unit target 仍未进入顶层 discoverability。
3. `tests/unit/runtime/RuntimeSmokeTest.cpp` 当前只串联 `MockMemoryStore`、`MockLLMAdapter` 和 `MockTool`，没有经过 runtime public surface，也不能证明 control-plane ready。
4. `tests/integration/agent_loop/CMakeLists.txt` 已有 replay compatibility integration 入口，但 fixture 根路径仍只以 checkpoint 子目录形式散落在编译定义里，缺少统一的 runtime fixture root 约束。

## 2. 外部参考

1. CMake `ctest(1)` 文档说明，`ctest -N` 只列出通过 `add_test()` 注册进 build tree 的测试，因此 discoverability 本身就是一条可以二值验证的构建事实，而不是评审时的口头约定。
2. 软件 smoke testing 的常见定义是 build verification / intake test，只用于快速发现“构建是否基本可测”，并不替代更细粒度的功能或集成证明；这与本仓库把旧 `RuntimeSmokeTest` 降级为 build-liveness-only 的要求一致。

## 3. 设计结论

1. 顶层 runtime unit discoverability 必须完整覆盖 runtime 当前已落盘的 unit target，而不是只暴露早期 smoke target；否则 `dasall_unit_tests` 不能代表 runtime test topology 已接线完成。
2. `RuntimeControlPlaneSurfaceTest` 负责承接 runtime gate 的最小 public-surface 证明：它只验证 `IAgent` / `AgentFacade` / `AgentTypes` 的真实入口、最小正向 init 和 fail-closed handle/resume 语义，不提前宣称 true integration ready。
3. 旧 `RuntimeSmokeTest` 保留编译存活价值，但必须从 `unit` gate 标签中移除，并改成显式 build-liveness 测试名，避免被 `ctest -L unit` 或 Gate-RT-02 / Gate-RT-07 误收为主控证据。
4. runtime fixture 根固定为 `tests/fixtures/runtime/`；integration 入口统一接收 fixture root 与 checkpoint root 两级路径，为 026/028/029/030 后续 gate 提供稳定资产边界。

## 4. 边界 / 职责

| 对象 | 职责 | 非职责 |
|---|---|---|
| `RuntimeControlPlaneSurfaceTest` | 验证 runtime public surface 可初始化、可 fail-closed | 不验证跨模块端口成功链 |
| `RuntimeBuildLivenessSmokeTest` | 保留旧 mock smoke 的 build-liveness 作用 | 不作为任何 runtime gate 通过证据 |
| `tests/unit/CMakeLists.txt` runtime target 列表 | 决定顶层 `dasall_unit_tests` 构建发现性 | 不决定单个测试的功能断言 |
| `tests/integration/agent_loop/CMakeLists.txt` fixture root 编译定义 | 固化 runtime integration 对 fixture 根的引用边界 | 不实现具体 fixture 行为 |
| `tests/fixtures/runtime/` | 承载 runtime-owned deterministic 资产 | 不进入 production 代码路径 |

## 5. 数据 / 接口说明

1. surface test 只消费 runtime 已冻结的 public types：`IAgent`、`AgentFacade`、`AgentInitRequest`、`ResumeHandleRequest`、`contracts::AgentRequest`。
2. surface test 的正向路径仅要求 `AgentInitRequest::has_minimum_requirements()` 成立，因此测试侧允许提供最小 `RuntimeDependencySet` 空桩，避免把 025 扩张成真实 seam 装配任务。
3. integration CMake 对外暴露两个稳定 fixture 编译定义：`DASALL_RUNTIME_FIXTURE_ROOT_DIR` 和 `DASALL_RUNTIME_FIXTURE_CHECKPOINT_DIR`。

## 6. 流程 / 时序

1. `tests/unit/runtime/CMakeLists.txt` 注册 `RuntimeControlPlaneSurfaceTest`，并把旧 smoke 以 build-liveness 名义保留。
2. `tests/unit/CMakeLists.txt` 把 runtime 全量 unit target 纳入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`，恢复顶层 unit discoverability。
3. `tests/integration/CMakeLists.txt` 明确 runtime integration target 聚合槽位。
4. `tests/integration/agent_loop/CMakeLists.txt` 统一注入 fixture root / checkpoint root 编译定义。
5. 验证时先运行 `RuntimeControlPlaneSurfaceTest`，再以 `ctest -N` 检查顶层是否能发现 runtime unit、runtime integration 与 build-liveness smoke。

## 7. 文件范围

1. `tests/unit/runtime/RuntimeControlPlaneSurfaceTest.cpp`
2. `tests/unit/runtime/CMakeLists.txt`
3. `tests/unit/CMakeLists.txt`
4. `tests/integration/CMakeLists.txt`
5. `tests/integration/agent_loop/CMakeLists.txt`
6. `tests/fixtures/runtime/README.md`

## 8. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| runtime public surface gate 替代旧 smoke 语义 | `tests/unit/runtime/RuntimeControlPlaneSurfaceTest.cpp` |
| 顶层 runtime unit discoverability 补全 | `tests/unit/CMakeLists.txt` |
| 旧 smoke 降级为 build-liveness-only | `tests/unit/runtime/CMakeLists.txt` |
| runtime integration fixture root 统一 | `tests/integration/CMakeLists.txt`、`tests/integration/agent_loop/CMakeLists.txt`、`tests/fixtures/runtime/README.md` |

## 9. Build 三件套

1. 代码目标：新增 `RuntimeControlPlaneSurfaceTest`；补齐 runtime unit target 顶层聚合；显式标记 runtime fixture root 与 build-liveness smoke。
2. 测试目标：`RuntimeControlPlaneSurfaceTest` 至少覆盖 1 条正向 init 与 1 条 fail-closed 路径；`ctest -N` 可发现 runtime unit、integration 和 build-liveness smoke。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_control_plane_surface_unit_test dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R RuntimeControlPlaneSurfaceTest --output-on-failure && ctest --test-dir build-ci -N`

## 10. 风险与回退

1. 如果旧 smoke 仍保留 `unit` 标签，后续 Gate 证据仍可能误收，应回退为 build-liveness-only 命名与标签。
2. 如果顶层 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS` 继续遗漏 runtime target，`dasall_unit_tests` 的通过不等于 runtime topology 已接线，应回退并补齐聚合列表。
3. 025 只收敛 topology 与 surface gate，不把 `AgentFacade` 当前 fail-closed skeleton 误报成 true integration ready；真正的主成功链留给 026。