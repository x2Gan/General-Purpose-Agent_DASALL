# COG-TODO-008 PlanGraph 与 ReplanResult 对象收敛

状态：Done
日期：2026-04-26
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready module-public planning supporting types

## 1. 本地证据

1. COG-TODO-005 已建立 `cognition/include/` 与 `dasall_cognition` public header file set，可承载 `plan/` 头文件。
2. COG-TODO-006 已接线 `CognitionInterfaceSurfaceTest`，本轮可继续用它冻结 planning supporting types 的 public surface。
3. `docs/architecture/DASALL_cognition子系统详细设计.md` §2.2 / §2.3 明确 PlanGraph、ReplanResult、ActionDecision 字段不得提前进入 shared contracts。
4. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.5.3 指定 `PlanGraph` 字段为 `plan_id`、`revision`、`nodes`、`edges`、`open_questions`、`plan_rationale`、`estimated_complexity`，`PlanNode` 字段为 `node_id`、`objective`、`success_signal`、`action_kind_hint`、`depends_on`、`evidence_refs`，`ReplanResult` 字段为 `new_plan`、`replaced_node_ids`、`replan_reason`、`confidence`。
5. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.2 要求 PlanGraph 满足 DAG、节点上限、深度上限约束，replan 保持 `plan_id` 不变且 `revision` 递增。
6. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` COG-TODO-008 验收口径为新增 `cognition/include/plan/PlanGraph.h`、`cognition/include/plan/ReplanResult.h` 并用 `CognitionInterfaceSurfaceTest` 回归。

## 2. 外部参考

1. Apache Airflow 官方 DAG 文档把 DAG 描述为执行 workflow 的模型，并包含 tasks 与 task dependencies：https://airflow.apache.org/docs/apache-airflow/3.0.2/core-concepts/dags.html
2. LangGraph 官方 graph execution 文档强调 graph 由 named nodes 组成，节点负责具体任务并写入 graph state：https://docs.langchain.com/oss/javascript/langgraph/frontend/graph-execution
3. Protocol Buffers 官方文档强调已投入使用的字段身份不可随意变更，字段复用会导致兼容性风险：https://protobuf.dev/programming-guides/editions/

本轮借鉴点：PlanGraph 首版必须把 node identity、dependency edge、revision 与 open question 显式化；字段演进按 module-public schema 处理，先保留默认值与可选容器，不在 shared contracts 中抢先冻结。

## 3. 主结论

1. `PlanGraph` / `PlanNode` / `ReplanResult` 落在 `dasall::cognition::plan` 命名空间，属于 cognition module-public supporting types，不进入 `contracts/`。
2. `PlanGraph` 只表达语义计划：计划 ID、修订号、节点、依赖边、开放问题、规划理由与复杂度估计。
3. `PlanNode` 只表达节点目标、成功信号、动作类型提示、依赖与证据，不携带 runtime deadline、lease、worker state 或 retry/backoff。
4. `ReplanResult` 只表达新计划、被替换节点、重规划原因与置信度，不承担 recovery counters、checkpoint 或恢复调度。
5. DAG 合法性、节点上限、深度上限和 revision++ 的强校验由后续 Planner / StageOutputValidator 实现；本轮先冻结字段承载面和负例边界。
6. 首版 schema 基线用头文件注释标记为 `cognition.plan.v1`，不引入独立 schema registry。

## 4. 边界与职责

| 对象 | 职责 | 非职责 |
|---|---|---|
| `PlanNode` | 表达一个语义规划节点及其成功判定信号 | 不执行工具；不存 runtime lease / worker state / deadline |
| `PlanEdge` | 表达节点之间的有向依赖关系 | 不表达调度策略、retry/backoff 或并发控制 |
| `PlanOpenQuestion` | 表达阻塞或非阻塞的待澄清问题 | 不直接向用户发问；不提交 response |
| `PlanGraph` | 表达 DAG 形态的语义计划与 revision | 不持有执行状态；不成为 workflow engine |
| `ReplanResult` | 表达一次 replan 的结果与原因 | 不执行恢复；不持有 recovery counters |

## 5. 数据 / 接口说明

| 类型 | 字段冻结 |
|---|---|
| `PlanNode` | `node_id`、`objective`、`success_signal`、`action_kind_hint`、`depends_on`、`evidence_refs` |
| `PlanEdge` | `from_node_id`、`to_node_id`、`condition`、`evidence_refs` |
| `PlanOpenQuestion` | `question_id`、`question`、`reason`、`blocks_plan`、`evidence_refs` |
| `PlanGraph` | `plan_id`、`revision`、`nodes`、`edges`、`open_questions`、`plan_rationale`、`estimated_complexity` |
| `ReplanResult` | `new_plan`、`replaced_node_ids`、`replan_reason`、`confidence` |

## 6. 流程 / 时序

1. Planner 在后续 COG-TODO-015 中基于 Goal/Perception/Belief/Context 构造 `PlanGraph`。
2. Reasoner 在后续 COG-TODO-016 中只读取 `PlanGraph`，并投影下一步 `ActionDecision`，不修改 plan。
3. Observation 失败或假设被推翻时，Planner 后续通过 `ReplanResult` 返回新图、替换节点列表与原因。
4. Runtime 仍只通过 cognition 公共接口消费结果，不直接接管 PlanGraph 内部 schema。

## 7. D 原子项完成情况

| 原子项 | 目标 | 结果 |
|---|---|---|
| D1 | 校验 COG-TODO-005/006 状态与 blocker | PASS：依赖 Done，COG-TODO-008 无 blocker |
| D2 | 锁定 PlanGraph / PlanNode / ReplanResult 字段 | PASS：见 §5 |
| D3 | 锁定 shared contracts 禁区 | PASS：本轮不修改 `contracts/`，不推进 `IPlanner` admission |
| D4 | 锁定 Build 三件套 | PASS：见 §9 |

## 8. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| PlanGraph 字段保持 module-public | `cognition/include/plan/PlanGraph.h` | `CognitionInterfaceSurfaceTest` 字段类型断言 |
| ReplanResult 字段保持 module-public | `cognition/include/plan/ReplanResult.h` | `CognitionInterfaceSurfaceTest` 字段类型断言 |
| planning headers 进入 file set | `cognition/CMakeLists.txt` | `dasall_cognition` 构建通过 |
| 不混入 runtime/recovery/provider 字段 | surface test SFINAE 负例 | deadline / lease / worker state / recovery request / provider payload 字段不存在 |
| 不推进 shared admission | 不修改 `contracts/` / InterfaceCatalog | contract 代码无变更，`InterfaceAdmissionContractTest` 后续仍能保持 IPlanner 未准入 |

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增 `plan/PlanGraph.h` | PlanNode / PlanEdge / PlanOpenQuestion / PlanGraph 字段正例 | `CognitionInterfaceSurfaceTest` | 若字段过宽，回退 runtime 控制字段 |
| B2 | 新增 `plan/ReplanResult.h` | ReplanResult 字段正例 | `CognitionInterfaceSurfaceTest` | 若引入恢复控制字段，回退到纯语义结果 |
| B3 | 更新 `cognition/CMakeLists.txt` | public header file set 可构建 | `cmake --build ... --target dasall_cognition` | 若发现性不足，补 file set |
| B4 | 扩展 `CognitionInterfaceSurfaceTest` | 至少 1 组正例 + 1 组负例 | `ctest ... -R "CognitionInterfaceSurfaceTest"` | 若负例误伤既有对象，收敛到 plan types |

## 10. D Gate

| 检查项 | 结论 |
|---|---|
| D 文档已落盘 | PASS |
| Design->Build 映射完整 | PASS |
| Build 三件套已锁定 | PASS |
| 范围未越界 | PASS：不实现 Planner / Replan 算法，不修改 shared contracts |
| 是否允许进入 Build | PASS |

## 11. Build 结果

| 原子项 | 结果 |
|---|---|
| B1 | PASS：`PlanGraph.h` 落盘 `PlanNode`、`PlanEdge`、`PlanOpenQuestion`、`PlanGraph` |
| B2 | PASS：`ReplanResult.h` 落盘 `new_plan`、`replaced_node_ids`、`replan_reason`、`confidence` |
| B3 | PASS：`cognition/CMakeLists.txt` public header file set 已登记 `plan/*` 头 |
| B4 | PASS：`CognitionInterfaceSurfaceTest` 覆盖 plan/replan 正例字段和 runtime/recovery/provider 越界字段负例 |

## 12. 验证证据

1. `cmake -S . -B build-ci-cog008 -G "Unix Makefiles"`
   - 结果：通过。
2. `cmake --build build-ci-cog008 --target dasall_cognition dasall_unit_tests`
   - 结果：通过；464/464 unit tests passed。输出包含既有 tools / llm / infra / services 编译告警，不属于本轮 plan 头文件改动。
3. `ctest --test-dir build-ci-cog008 -R "CognitionInterfaceSurfaceTest" --output-on-failure`
   - 结果：通过；1/1 passed。
4. `cmake --build build-ci --target dasall_cognition dasall_unit_tests`
   - 结果：通过；既有 Ninja `build-ci` 完成重配置、构建与 464/464 unit tests。
5. `ctest --test-dir build-ci -R "CognitionInterfaceSurfaceTest" --output-on-failure`
   - 结果：通过；1/1 passed。

## 13. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 代码注释 | PASS：仅在 planning 头文件保留 `cognition.plan.v1` schema baseline 注释，其余字段自解释 |
| 正例覆盖 | PASS：surface test 断言 `PlanNode`、`PlanEdge`、`PlanOpenQuestion`、`PlanGraph`、`ReplanResult` 字段类型与默认值 |
| 负例覆盖 | PASS：surface test 断言 plan objects 不含 deadline、lease、worker state、tool request、recovery request、provider payload 等越界字段 |
| 测试发现性 | PASS：`CognitionInterfaceSurfaceTest` 在 `build-ci-cog008` 与既有 `build-ci` 均可运行 |
| TODO / worklog 证据 | PASS：专项 TODO、交付物、开发执行记录已回写 |
| 提交前状态隔离 | PASS：本轮只包含 COG-TODO-008 相关 plan headers、CMake、surface test 与文档证据 |
