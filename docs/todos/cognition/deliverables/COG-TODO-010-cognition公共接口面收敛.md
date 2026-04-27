# COG-TODO-010 cognition 公共接口面收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready module-public cognition interface surface

## 1. 本地证据

1. COG-TODO-001 已明确 cognition 唯一 runtime-facing 可执行口径为 `ICognitionEngine::decide()`、`ICognitionEngine::reflect()` 与 `IResponseBuilder::build()`，旧 `step()` 和 `init()` 草图只能保留为历史参考，不得再回流到公共头。
2. COG-TODO-005 / 006 已建立 `cognition/include/` 公共头布局与 `CognitionInterfaceSurfaceTest`，本轮可以直接在既有头与 surface test 上冻结接口签名。
3. COG-TODO-007 / 008 / 009 已冻结 runtime-facing request/result、`PlanGraph` / `ReplanResult`、`ActionDecision` / `BeliefUpdateHint` / `BudgetContext` 等 supporting types，阶段接口已具备稳定返回值与输入承载对象。
4. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.6.1 要求 cognition 公共入口只保留 `decide` / `reflect` / `build` 三入口；§6.6.3 要求 `IPlanner::build_plan()` / `replan()`、`IReasoner::decide()`、`IReflectionEngine::analyze()` 成为模块公共阶段接口。
5. `contracts/include/boundary/InterfaceCatalog.h` 与 `tests/contract/smoke/InterfaceAdmissionContractTest.cpp` 明确 `IPlanner` 仍处于 `AwaitingSupportingContracts`，因此 010 必须冻结接口面，但不能触碰 shared admission 结论。
6. 代码现状显示 `ICognitionEngine.h` / `IResponseBuilder.h` 已具备正确 runtime-facing 入口，而 `IPlanner.h` / `IReasoner.h` / `IReflectionEngine.h` 仍是 marker-only skeleton，尚未补齐方法签名与 supporting request structs。
7. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.6.1 代码片段仍残留 `init()`，专项 TODO 行仍列出 `IReflectionEngine::reflect()` 等过时方法名，需要与已落盘公共头收敛为同一口径。

## 2. 外部参考

1. C++ Core Guidelines 的 I.1 / I.4 / C.121 / C.126 / C.41 / NR.5 强调接口应显式、强类型、保持纯抽象，并避免两阶段初始化：https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class
2. Pact 文档强调 consumer/provider 契约应以交互为单位验证，provider 验证必须持续确认实现没有偏离 consumer 期待：https://docs.pact.io/getting_started/how_pact_works

本轮借鉴点：`ICognitionEngine` 不再回引 `init()` 两阶段生命周期；阶段接口继续停留在 module-public 层，并用独立 contract smoke 证明 `IPlanner` admission 结论未被误改。

## 3. 主结论

1. `ICognitionEngine` 继续保持 `decide()` / `reflect()` 双入口，不新增 `init()`；装配与默认配置注入继续由 `create_cognition_engine()` 工厂承担。
2. `IResponseBuilder` 继续作为唯一终态结果构造接口，保持 `build()` 单入口，不把终态构造职责回卷到 `ICognitionEngine`。
3. `IPlanner`、`IReasoner`、`IReflectionEngine` 从 marker-only skeleton 收敛为具备稳定方法签名的 pure abstract interface，并保持 protected 默认构造与虚析构。
4. `PlanningRequest`、`ReplanRequest`、`ReasoningRequest`、`ReflectionAnalysisRequest` 与各自阶段接口同层落盘，作为 cognition module-public supporting request structs，不推进到 `contracts/`。
5. `CognitionInterfaceSurfaceTest` 已补充签名断言与负例约束，显式阻止 `ICognitionEngine::init()`、`IReflectionEngine::reflect()` 等旧口径回流。
6. `InterfaceAdmissionContractTest` 保持 7/7 通过，证明本轮只冻结模块公共接口面，没有把 `IPlanner` 错误推进 shared contracts。

## 4. 边界与职责

| 接口 / 对象 | 职责 | 非职责 |
|---|---|---|
| `ICognitionEngine` | 对 Runtime 暴露决策与反思两条执行入口 | 不承担终态结果构造；不再承担两阶段 `init()` 生命周期 |
| `IResponseBuilder` | 把终态决策与上下文构造成 `AgentResult` | 不参与下一步动作选择 |
| `IPlanner` | 基于 goal / context / belief / perception 构造与重规划 `PlanGraph` | 不执行工具；不进入 shared contracts |
| `IReasoner` | 基于 perception / plan / belief / observation 选择 `ActionDecision` | 不生成 `ToolRequest`；不直接 replan |
| `IReflectionEngine` | 基于 observation / error / active plan 产出 suggestion-only `ReflectionDecision` | 不执行恢复动作；不持有 Runtime retry / checkpoint 权限 |
| 阶段 request structs | 为阶段接口封装强类型输入、保持参数收束 | 不混入 runtime/recovery/provider-private 越界字段 |

## 5. 数据 / 接口说明

| 接口 / 对象 | 冻结结果 |
|---|---|
| `ICognitionEngine` | `decide(const CognitionStepRequest&) -> CognitionDecisionResult`；`reflect(const ReflectionRequest&) -> CognitionReflectionResult`；无 `init()` |
| `IResponseBuilder` | `build(const ResponseBuildRequest&) -> ResponseBuildResult` |
| `IPlanner` | `build_plan(const PlanningRequest&) -> plan::PlanGraph`；`replan(const ReplanRequest&) -> plan::ReplanResult` |
| `IReasoner` | `decide(const ReasoningRequest&) -> decision::ActionDecision` |
| `IReflectionEngine` | `analyze(const ReflectionAnalysisRequest&) -> contracts::ReflectionDecision` |
| `PlanningRequest` | `caller_domain`、`request_id`、`trace_id`、`profile_id`、`goal_contract`、`context_packet`、`belief_state`、`perception_result`、`budget_context`、`execution_hints` |
| `ReplanRequest` | `caller_domain`、`request_id`、`trace_id`、`profile_id`、`goal_contract`、`context_packet`、`belief_state`、`active_plan`、`latest_observation`、`budget_context`、`execution_hints` |
| `ReasoningRequest` | `caller_domain`、`request_id`、`trace_id`、`profile_id`、`goal_contract`、`context_packet`、`belief_state`、`perception_result`、`active_plan`、`latest_observation`、`budget_context`、`execution_hints` |
| `ReflectionAnalysisRequest` | `caller_domain`、`request_id`、`trace_id`、`profile_id`、`goal_contract`、`belief_state`、`latest_observation`、`error_info`、`active_plan`、`execution_hints` |

## 6. 流程 / 时序

1. Runtime 仍只通过 `ICognitionEngine::decide()` / `reflect()` 与 `IResponseBuilder::build()` 进入 cognition 公共接口面。
2. Cognition 内部阶段协作时，可通过 `PlanningRequest`、`ReplanRequest`、`ReasoningRequest`、`ReflectionAnalysisRequest` 把多参数约束收束为稳定 supporting request object。
3. Planner / Reasoner / ReflectionEngine 的 supporting request structs 保持 module-public，只服务 cognition 内部装配、测试 seam 与后续实现，不进入 shared contracts。
4. `InterfaceAdmissionContractTest` 继续验证 `IPlanner` 为 postponed，确保接口冻结与 shared admission 解耦。

## 7. D 原子项完成情况

| 原子项 | 目标 | 结果 |
|---|---|---|
| D1 | 确认 runtime-facing 公开口径仍以 `decide/reflect/build` 为准 | PASS：`ICognitionEngine` 无 `init()`，`IResponseBuilder` 保持 `build()` |
| D2 | 为阶段接口补齐 supporting request structs 与方法签名 | PASS：`IPlanner` / `IReasoner` / `IReflectionEngine` 已具备稳定签名 |
| D3 | 通过 surface test 锁定新签名并拦截旧口径 | PASS：新增签名断言，显式阻止 `init()` / `IReflectionEngine::reflect()` |
| D4 | 确认 shared admission 未被误改 | PASS：`InterfaceAdmissionContractTest` 维持 7/7 passed |

## 8. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| runtime-facing 只保留 `decide/reflect/build` | `cognition/include/ICognitionEngine.h`、`cognition/include/IResponseBuilder.h` | surface test 断言无 `init()`，工厂仍返回可用接口 |
| 阶段接口需要稳定方法签名 | `cognition/include/IPlanner.h`、`cognition/include/IReasoner.h`、`cognition/include/IReflectionEngine.h` | surface test 断言方法指针签名 |
| 阶段 supporting requests 维持 module-public | 同上三个头文件 | surface test 断言 request 字段类型，并阻止 recovery/tool 越界字段 |
| `IPlanner` 不推进 shared admission | `tests/contract/smoke/InterfaceAdmissionContractTest.cpp` 仅作回归验证 | 直接执行 contract binary 保持 postponed 语义 |
| 详细设计与 TODO 口径收敛 | `docs/architecture/DASALL_cognition子系统详细设计.md`、专项 TODO | 文档不再残留 `init()` / `IReflectionEngine::reflect()` |

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 更新阶段接口头并落盘 supporting request structs | 接口头自洽且可被 unit target 编译 | `cmake --build build-ci --target dasall_cognition_interface_surface_unit_test` | 若 public surface 越界，回退到 module-public supporting request object |
| B2 | 扩展 `CognitionInterfaceSurfaceTest` | 新签名正例与旧口径负例 | `./build-ci/tests/unit/cognition/dasall_cognition_interface_surface_unit_test` | 若断言过严误锁未来空间，只保留当前设计已确认的边界 |
| B3 | 复核 `IPlanner` admission 状态不变 | `InterfaceAdmissionContractTest` 继续通过 | `cmake --build build-ci --target dasall_contract_interface_admission_test && ./build-ci/tests/contract/dasall_contract_interface_admission_test` | 若误触 shared admission，立即回退 `contracts/` 以外的越界修改 |

## 10. D Gate

| 检查项 | 结论 |
|---|---|
| D 文档已落盘 | PASS |
| Design->Build 映射完整 | PASS |
| Build 三件套已锁定 | PASS |
| 范围未越界 | PASS：不修改 `contracts/` admission catalog，不扩张 façade 实现 |
| 是否允许进入 Build | PASS |

## 11. Build 结果

| 原子项 | 结果 |
|---|---|
| B1 | PASS：阶段接口头已补齐 `build_plan` / `replan` / `decide` / `analyze` 与 supporting request structs |
| B2 | PASS：surface test 已新增签名断言与旧口径负例 |
| B3 | PASS：interface admission contract 继续保持 `IPlanner` postponed |

## 12. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_cognition_interface_surface_unit_test"])`
   - 结果：通过；接口面 unit target 成功完成重编译与链接。
2. `RunCtest_CMakeTools(tests=["CognitionInterfaceSurfaceTest"])`
   - 结果：失败，返回仓库已知泛化错误“生成失败”；本轮不把它作为代码回归依据。
3. `./build-ci/tests/unit/cognition/dasall_cognition_interface_surface_unit_test`
   - 结果：通过；二进制零输出退出，表示所有静态断言与运行时断言通过。
4. `Build_CMakeTools(buildTargets=["dasall_contract_interface_admission_test"])`
   - 结果：通过；contract smoke target 无需额外重编译。
5. `./build-ci/tests/contract/dasall_contract_interface_admission_test`
   - 结果：通过；7/7 passed，其中 `test_planner_is_postponed_until_supporting_contracts_freeze` 明确保持绿灯。

## 13. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 代码注释 | PASS：未引入多余注释，仅靠类型与字段名表达语义 |
| 正例覆盖 | PASS：五个接口的签名与四个阶段 request structs 字段均有断言 |
| 负例覆盖 | PASS：显式阻止 `ICognitionEngine::init()` 与 `IReflectionEngine::reflect()` 回流 |
| 测试发现性 | PASS：`dasall_cognition_interface_surface_unit_test` 与 `dasall_contract_interface_admission_test` 均可独立执行 |
| TODO / worklog 证据 | PASS：专项 TODO、详细设计、开发记录与本交付物已同步回写 |
| 提交前状态隔离 | PASS：本轮仅包含 cognition 公共接口头、surface test 与文档闭环 |