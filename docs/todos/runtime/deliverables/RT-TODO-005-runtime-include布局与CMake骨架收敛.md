# RT-TODO-005 runtime include 布局与 CMake 骨架收敛

日期：2026-04-22  
任务：RT-TODO-005  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/runtime/DASALL_runtime子系统专项TODO.md](/home/gangan/DASALL/docs/todos/runtime/DASALL_runtime子系统专项TODO.md) 将 RT-TODO-005 定义为新增 runtime include 布局与 CMake 骨架，直接目标是让 `dasall_runtime` 摆脱 placeholder-only 状态，并为后续 006~011 的 public surface 提供稳定挂载点。
2. [docs/architecture/DASALL_runtime子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_runtime子系统详细设计.md) 的 6.24.3 与 6.24.3.1 已冻结 `AgentInitRequest`、`AgentInitResult`、`HandleOptions`、`ResumeHandleRequest` 的字段和边界；本轮前这些对象尚未进入 runtime public ABI，本轮 Build 已将其落到 `runtime/include/AgentTypes.h`。
3. 当前 [runtime/CMakeLists.txt](/home/gangan/DASALL/runtime/CMakeLists.txt) 仅编译 `src/placeholder.cpp`，而 [tests/unit/runtime/RuntimeSmokeTest.cpp](/home/gangan/DASALL/tests/unit/runtime/RuntimeSmokeTest.cpp) 只是 mock build-liveness；在 RT-TODO-025 之前不能把它当作 control-plane gate。

## 2. 外部参考

1. CMake 官方文档对 `target_link_libraries()` 的说明明确指出，`PUBLIC` 依赖既参与当前目标链接，也进入 link interface，供依赖该目标的下游继续继承；这适用于 runtime public 头直接暴露 `RuntimePolicySnapshot` 的场景。
2. CMake 官方文档对 `target_include_directories()` 的说明明确指出，依赖头路径应由依赖目标自身通过 usage requirements 传播，而不是由当前目标手工转抄第三方 include 路径；这支持 runtime 通过 `dasall_profiles` 的 public interface 继承 profile 头，而不是硬编码额外 include 目录。

## 3. 设计结论

1. runtime 的根层 public surface 在本轮只建立最小入口集：`IAgent.h`、`AgentTypes.h`、`AgentFacade.h`；组件级接口目录如 `fsm/`、`budget/`、`checkpoint/`、`session/`、`scheduling/` 留给 006~011 分任务进入。
2. `IAgent` 继续保持 apps-facing 最小门面，只暴露 `init`、`handle`、`resume`、`stop` 四个生命周期动作，不提前把内部控制器或相邻模块 seam 暴露到 public ABI。
3. `AgentFacade` 在本轮只提供 fail-closed skeleton：负责持有 boot-time 组合根状态、完成参数归一化和生命周期兜底，但不提前伪造真实 orchestrator 行为。
4. 由于 `AgentTypes.h` 需要公开引用 `RuntimePolicySnapshot`，`dasall_runtime` 必须把 `dasall_profiles` 作为 `PUBLIC` 依赖加入 link interface；否则下游仅包含 runtime public 头时会失去 profile usage requirements。
5. 旧 `RuntimeSmokeTest` 在本轮不删除也不升格；从本轮开始它只被视为历史 build-liveness 脚手架，真正的 runtime control-plane surface gate 继续留给 RT-TODO-025 收口。

## 4. 文件范围

1. 本轮 Build 目标为 `runtime/include/IAgent.h`、`runtime/include/AgentTypes.h`、`runtime/include/AgentFacade.h`、`runtime/src/AgentFacade.cpp`、更新后的 `runtime/CMakeLists.txt`。
2. 本轮不改 `tests/unit/runtime/CMakeLists.txt` 与 `RuntimeSmokeTest.cpp` 的 discoverability 语义；测试拓扑调整继续由 RT-TODO-025 承担。
3. TODO / worklog / architecture 的状态回写已在 Build 验证通过后完成，避免把未验证的骨架误写成完成态。

## 5. 流程 / 时序

1. 上游 apps 仅通过 `IAgent` 或 `AgentFacade` 进入 runtime。
2. `AgentFacade::init()` 接收 `AgentInitRequest`，持有 runtime instance、profile snapshot 和 dependency-set anchor，形成 boot-time 组合根状态。
3. `AgentFacade::handle()` 与 `resume()` 在本轮只完成 fail-closed 生命周期兜底，并为后续 `AgentOrchestrator` 接线预留稳定入口。
4. `stop()` 负责释放 boot-time 状态并返回统一停机结果；不会在本轮创建第二主循环或抢占 RecoveryManager / ContextOrchestrator 的职责。

## 6. Design -> Build 映射

| Design 项 | 本轮 Build 落点 |
|---|---|
| apps-facing 最小 runtime public ABI | `runtime/include/IAgent.h` |
| AgentFacade supporting types 实体化 | `runtime/include/AgentTypes.h` |
| fail-closed public facade skeleton | `runtime/include/AgentFacade.h`、`runtime/src/AgentFacade.cpp` |
| profile 头的 usage requirements 传播 | `runtime/CMakeLists.txt` 中 `dasall_profiles` 的 `PUBLIC` 链接 |
| 摆脱 placeholder-only 构建入口 | `runtime/CMakeLists.txt` 改由 `AgentFacade.cpp` 承载真实编译单元 |

## 7. Build 三件套

1. 代码目标：新增 runtime public include 根的最小入口文件和 `AgentFacade` fail-closed skeleton，并更新 `dasall_runtime` 的 CMake 依赖图。
2. 测试目标：验证 `dasall_runtime` 能在引入 runtime public ABI 后独立构建通过，且不依赖旧 smoke 语义作为交付证据。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime`

## 8. 风险与回退

1. 如果本轮把 `HandleOptions` 扩成第二份业务请求体，或让 `AgentFacade` 直接显式依赖 cognition / tools / memory 的实现细节，会违反 6.24.3.1 和 ADR-006/007/008 的边界，应回退到最小门面语义。
2. 如果 `AgentTypes.h` 引用 `RuntimePolicySnapshot` 却没有把 `dasall_profiles` 放进 runtime 的 public link interface，下游包含 runtime 头时会出现 usage requirements 缺失，应回退并恢复 `PUBLIC` 依赖传播。
3. 如果本轮试图顺手把 `RuntimeSmokeTest` 升格为 Gate，会和 RT-TODO-025 的 discoverability 任务混层；应保持 smoke 仅作为历史 build-liveness 资产。