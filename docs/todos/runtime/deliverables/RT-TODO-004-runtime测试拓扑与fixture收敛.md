# RT-TODO-004 runtime 测试拓扑与 caller fixture 收敛

日期：2026-04-22  
任务：RT-TODO-004  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/runtime/DASALL_runtime子系统专项TODO.md](/home/gangan/DASALL/docs/todos/runtime/DASALL_runtime子系统专项TODO.md) 将 RT-TODO-004 定义为收敛 `tests/unit/runtime/`、`tests/integration/agent_loop/` 和 caller fixture 口径，用于缓解 RT-BLK-06。
2. [docs/architecture/DASALL_runtime子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_runtime子系统详细设计.md) 当前虽然列出了部分 unit/integration 目标文件，但尚未把 runtime 测试拓扑、discoverability 责任和 fixture gate / true integration gate 的边界成表。
3. 9.4 仅用 4 条路径文字描述正常链路、恢复链路、resume 链路和 degrade 链路，仍无法回答“哪类测试可以使用 caller fixture、哪类结论只能算 subsystem-local ready”。

## 2. 外部参考

1. AWS 的 Hexagonal Architecture 指南强调，ports/adapters 结构应支持独立测试和依赖替换，这意味着 runtime caller fixture 必须作为显式测试资产存在，而不是把替换逻辑散落在 production 代码里。
2. 同一指南也指出，通过接口和适配器隔离外部依赖，可以让应用逻辑在不依赖外部系统的前提下独立验证；这正对应 runtime-local fixture gate 与 true cross-module integration gate 的分层设计。

## 3. 设计结论

1. runtime 测试拓扑固定为三层：`tests/unit/runtime/` 负责 module-local 与 control-plane surface，`tests/integration/agent_loop/` 负责 agent loop 集成路径，`tests/fixtures/runtime/` 负责 caller fixture/stub/checkpoint 资产。
2. `RuntimeControlPlaneSurfaceTest` 是后续替代旧 smoke Gate 语义的目标入口；旧 `RuntimeSmokeTest` 在 RT-TODO-025 完成 discoverability 接线前只能保留 build-liveness 含义。
3. caller fixture 只能组装 runtime-owned 控制平面 + fail-closed stub/null adapter + deterministic asset，不能冒充真实 public interface ready。
4. `RuntimeUnaryFixtureIntegrationTest` 与 `RuntimeUnaryIntegrationTest` 必须显式分层，前者只证明 subsystem-local ready，后者才可能证明 true integration ready。

## 4. 拓扑与 fixture 矩阵

| 层级 | 路径 / 文件 | 职责边界 |
|---|---|---|
| unit | `tests/unit/runtime/RuntimeControlPlaneSurfaceTest.cpp`、`AgentFsmTest.cpp`、`BudgetControllerTest.cpp` 等 | 验证 runtime-owned public surface 与组件边界，不证明跨模块联调 |
| integration | `tests/integration/agent_loop/RuntimeUnaryFixtureIntegrationTest.cpp`、`RuntimeUnaryIntegrationTest.cpp`、`RuntimeResumeIntegrationTest.cpp`、`RuntimeSafeModeIntegrationTest.cpp` | 验证 agent loop 路径，并明确区分 fixture gate 与 true integration gate |
| fixture assets | `tests/fixtures/runtime/RuntimeCallerFixture.h`、`RuntimeDependencyFixture.h`、`RuntimeCheckpointFixture.h`、`checkpoints/` | 提供 deterministic caller input、dependency seam 和 checkpoint 资产；不进入 production |

## 5. 流程 / 时序

1. discoverability：顶层 `ctest -N` 必须能发现 runtime unit 与 runtime integration 入口。
2. runtime-local gate：`RuntimeCallerFixture + fail-closed stub/null adapter -> RuntimeUnaryFixtureIntegrationTest`，只验证 runtime 自身控制平面闭环。
3. true integration gate：切换到真实 public interface 后，再运行 `RuntimeUnaryIntegrationTest` / `RuntimeResumeIntegrationTest` 等跨模块用例。
4. gate 回写：fixture gate 与 true integration gate 的结论必须分开进入 TODO / worklog / Gate 证据表，不能混写。

## 6. 文件范围

1. 设计真值源更新在 [docs/architecture/DASALL_runtime子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_runtime子系统详细设计.md) 的 8.1.1、9.1.1 和 9.4。
2. 本任务交付文档落于 [docs/todos/runtime/deliverables/RT-TODO-004-runtime测试拓扑与fixture收敛.md](/home/gangan/DASALL/docs/todos/runtime/deliverables/RT-TODO-004-runtime测试拓扑与fixture收敛.md)。
3. 后续 Build 落盘目标预留为 `tests/unit/runtime/CMakeLists.txt`、`tests/integration/agent_loop/CMakeLists.txt`、`tests/fixtures/runtime/` 和 `RuntimeControlPlaneSurfaceTest.cpp`。

## 7. Design -> Build 映射

| Design 项 | 后续 Build 落点 |
|---|---|
| runtime unit / integration discoverability 规则 | `tests/unit/runtime/CMakeLists.txt`、`tests/integration/CMakeLists.txt`、`tests/integration/agent_loop/CMakeLists.txt` |
| `RuntimeControlPlaneSurfaceTest` 替代旧 smoke 的 gate 语义 | `tests/unit/runtime/RuntimeControlPlaneSurfaceTest.cpp`、后续 TODO/工作日志证据回写 |
| caller fixture 资产边界 | `tests/fixtures/runtime/`、`tests/integration/agent_loop/RuntimeUnaryFixtureIntegrationTest.cpp` |

## 8. Build 三件套

1. 代码目标：无；本任务只收敛测试拓扑与 caller fixture 设计，不修改 runtime 或 tests 实现代码。
2. 测试目标：通过文档检索确认 `tests/unit/runtime`、`tests/integration/agent_loop`、`RuntimeUnaryIntegrationTest`、`RuntimeResumeIntegrationTest`、`RuntimeSafeModeIntegrationTest` 已在 architecture/TODO/deliverable 三处形成一致口径。
3. 验收命令：
   - `rg -n "tests/unit/runtime|tests/integration/agent_loop|RuntimeUnaryIntegrationTest|RuntimeResumeIntegrationTest|RuntimeSafeModeIntegrationTest" docs/architecture/DASALL_runtime子系统详细设计.md docs/todos/runtime/DASALL_runtime子系统专项TODO.md docs/todos/runtime/deliverables/RT-TODO-004-runtime测试拓扑与fixture收敛.md`

## 9. 风险与回退

1. 如果后续 RT-TODO-025 仍继续沿用 `RuntimeSmokeTest` 作为 Gate 证据，应回退到本轮冻结的规则，把它降级回 build-liveness-only。
2. 如果后续 fixture 资产散落到 production 目录或 integration 目录内没有统一 caller fixture 根，会导致 discoverability 和边界追溯混乱，应回退到 `tests/fixtures/runtime/` 集中管理。
3. 本任务只完成设计侧 topology/fixure 收敛，RT-BLK-06 仍需 RT-TODO-025 完成 CMake/test registration 后才能完全解除。