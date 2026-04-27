# COG-TODO-015 Planner 与 PlanGraphBuilder 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready planning stage implementation

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.2 已将 `Planner` 冻结为“把 GoalContract、PerceptionResult、BeliefState、ContextPacket 收敛为可执行的 PlanGraph，并在 Observation 失败或假设被推翻时输出 ReplanResult”的阶段组件，且明确非职责边界是不执行工具、不直接发起澄清、不生成 ToolRequest、不承担恢复执行控制。
2. 同一章节要求 `PlanGraph` 至少包含 `plan_id`、`revision`、`nodes`、`edges`、`open_questions`、`plan_rationale`、`estimated_complexity`，且必须满足无环、节点上限与深度上限约束；`replan` 必须保持 `plan_id` 不变并递增 `revision`。
3. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.16.2 已冻结 budget-aware planning 规则：`0.5 ≤ budget_utilization < 0.8` 时 Planner 收紧 `max_plan_nodes`；`budget_utilization ≥ 0.8` 或 `near_budget_limit=true` 时 Planner 只允许 1-2 节点浅层计划。
4. `cognition/include/IPlanner.h` 已冻结公共阶段接口：`build_plan(const PlanningRequest&) -> plan::PlanGraph` 与 `replan(const ReplanRequest&) -> plan::ReplanResult`；本轮实现必须保持 module-public surface 不变。
5. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 已把 015 的完成判定绑定到四个可验证点：DAG 构建、`revision` 递增、budget 收紧和 open question 路径。

## 2. 外部参考

1. Prompt Engineering Guide 的 LLM Agents 文档指出：planning module 的核心职责是把复杂任务拆成可单独求解的子步骤；带反馈的 planning 需要根据 past actions 和 observations 迭代修订执行计划，以支持长链路任务与复杂环境中的 trial-and-error：https://www.promptingguide.ai/research/llm-agents

本轮借鉴点：Planner 保持“先建图、后执行”的边界，`replan()` 只根据 Observation 更新图结构与 `revision`，不在规划阶段直接做动作裁定；同时在 budget 压力下优先压缩节点数量，而不是保留表面完整但不可执行的伪 DAG。

## 3. 主结论

1. 新增私有 `Planner` 与 `PlanGraphBuilder`，落在 `cognition/src/planning/Planner.h/.cpp` 与 `cognition/src/planning/PlanGraphBuilder.h/.cpp`，由 `Planner` 实现 `IPlanner`，由 `PlanGraphBuilder` 承担 build / replan 的规则式图收敛。
2. `build_plan()` 现在有三条保守路径：
   - 感知阶段已要求澄清时，直接返回仅含 `open_questions` 的计划图，不强行生成 DAG；
   - `task_type=direct_response` 时生成单节点终态计划；
   - 一般 actionable 输入时展开 staged DAG，并为每个节点补齐 `success_signal`、`action_kind_hint` 与 `evidence_refs`。
3. `PlanGraphBuilder` 在输出前统一执行图约束校验：计划必须具备非空 `plan_id` / `plan_rationale`，节点字段必须完整，边和依赖必须引用已知节点，最长路径不能超过有效 `max_plan_depth`，并且节点数不得越过 `max_plan_nodes`。
4. budget-aware planning 直接落在 planning slice 内：中等预算压力下压缩为三节点以内的浅层图；高预算压力或 near-budget-limit 下压缩到 1-2 节点，并在 `plan_rationale` 中显式标记 `compressed_for_budget`。
5. `replan()` 现保持 `plan_id` 不变、`revision` 递增，在 observation failure 下替换末端节点并补 recovery / revalidate 路径，同时显式回填 `replaced_node_ids` 与 `replan_reason`。

## 4. 边界与职责

| 组件 | 职责 | 非职责 |
|---|---|---|
| `Planner` | 作为 `IPlanner` 的私有实现，对外提供 `build_plan()` 与 `replan()` | 不执行任何工具或 workflow；不选择最终 ActionDecision；不直接写 Recovery / Runtime 状态 |
| `PlanGraphBuilder` | 负责 DAG 展开、open question 路径、budget 压缩与 replan 图修补 | 不暴露 public contract；不直接对接 façade / runtime |
| `PlanGraph` / `ReplanResult` | 承载规划与重规划的 module-public supporting type | 不扩张到 shared contracts；不写 runtime retry/backoff 等执行细节 |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| 规划阶段先判断是否应回到澄清 | `cognition/src/planning/PlanGraphBuilder.cpp` | clarification 输入时 `nodes.empty()` 且 `open_questions` 非空 |
| actionable 目标要展开为合法 DAG | `PlanGraphBuilder.cpp` + `PlannerPlanGraphTest.cpp` | 构建结果具备顺序依赖边、终态 validation node 和稳定 `plan_id` |
| replan 必须保留 `plan_id` 并递增 `revision` | `Planner.cpp` + `PlannerReplanTest.cpp` | 失败 observation 下 `plan_id` 不变、`revision++`、`replaced_node_ids` 有值 |
| budget 紧张时必须压缩节点规模 | `PlanGraphBuilder.cpp` + `PlannerNodeBudgetTest.cpp` | 中预算压到 3 节点，高预算压到 2 节点 |
| 规划组件不能伪造无效图结构 | `validate_plan_graph()` | 非空字段、无环、节点上限与深度上限均有校验 |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增私有 `Planner` / `PlanGraphBuilder` 与 planning helper | DAG / open question / replan / budget 行为可局部断言 | `Build_CMakeTools(buildTargets=["dasall_planner_plan_graph_unit_test","dasall_planner_replan_unit_test","dasall_planner_node_budget_unit_test"])` | 若 private seam 外溢，回退为 `cognition/src/planning` 内部 header |
| B2 | 把 budget-aware 压缩与 replan graph 修补固化到 planning slice | 中预算 / 高预算 / observation failure 三类路径可独立验证 | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_planner_plan_graph_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_planner_replan_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_planner_node_budget_unit_test` | 若 graph 校验不稳，优先收敛到单节点 safe fallback 而不是放任无效 DAG |
| B3 | 注册三个 planner-focused unit targets | discoverability 与直接执行成立 | 同上 | 若接线范围扩大，只保留 cognition unit slice 内变更 |

## 7. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_planner_plan_graph_unit_test","dasall_planner_replan_unit_test","dasall_planner_node_budget_unit_test"])`
   - 第一次结果：失败，局部暴露 `PlannerReplanTest.cpp` 使用了不存在的 `ResultCodeCategory::Temporary`，同时三条 planner tests 的 `CognitionConfig` 聚合初始化引出同一 slice 告警。
   - 修补同一 slice 后复跑：通过。
2. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_planner_plan_graph_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_planner_replan_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_planner_node_budget_unit_test`
   - 结果：通过；三条 planner-focused unit tests 均零输出退出。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 范围控制 | PASS：只新增 planning 私有组件、三条 focused tests 与最小 CMake 接线 |
| 正例覆盖 | PASS：覆盖 DAG 展开、终态 validation node、`plan_id` / `revision` 基线 |
| 负例与降级 | PASS：覆盖 clarification open question 路径、observation failure replan 路径和 budget 压缩路径 |
| 测试发现性 | PASS：新增三个 cognition planner unit targets，可单独 build 和直接执行 |
| 架构边界 | PASS：未把 PlanGraph / ReplanResult 推进 shared contracts；规划组件仍不执行工具、不控制恢复 |