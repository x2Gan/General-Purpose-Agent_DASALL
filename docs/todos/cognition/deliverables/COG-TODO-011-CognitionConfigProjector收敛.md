# COG-TODO-011 CognitionConfigProjector 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready config projection

## 1. 本地证据

1. `profiles/include/RuntimePolicySnapshot.h` 已冻结 `model_profile.stage_routes`、`token_budget_policy`、`degrade_policy`、`timeout_policy`、`ops_policy` 等配置输入面，满足“由单一 profile 快照向模块配置投影”的前提。
2. `cognition/include/CognitionConfig.h` 已冻结 cognition 首版公开配置面：`max_plan_nodes`、`max_plan_depth`、阈值、感知/响应降级开关、delegate hint 开关、观测开关。
3. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.10.1 给出首版默认配置键，§6.10.2 给出 `desktop_full` / `cloud_full` / `edge_balanced` / `edge_minimal` / `factory_test` 五档 profile 的投影意图。
4. `docs/todos/cognition/deliverables/COG-TODO-002-stage-taxonomy与StageModelHint映射收敛.md` 已冻结 `planning/execution/reflection/response` canonical stage key，并明确 projector / resolver 是允许执行 legacy alias 归一化的唯一 owner 边界。
5. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 中 COG-BLK-006 明确指出：在 `CognitionConfigProjector` 缺失前，profile→cognition 配置投影链路不存在，edge/profile 兼容性无法验证。
6. 仓库已有最近邻模式：`knowledge/src/config/KnowledgeConfigProjector.*` 与 `tests/unit/knowledge/KnowledgeConfigProjectionTest.cpp` 证明“private header + module unit test + src include seam”是本仓库已验证实现路径。

## 2. 外部参考

1. Twelve-Factor App 的 Config 原则强调 deploy 变化配置应与代码分离，避免在代码中散落常量和多套配置来源：https://12factor.net/config

本轮借鉴点：cognition 不再维护第二套平行 profile 配置系统，而是直接以 `RuntimePolicySnapshot` 作为唯一外部投影视图，在模块边界内派生 `CognitionConfig` 与 `StageModelHint`。

## 3. 主结论

1. `CognitionConfigProjector` 作为 cognition 私有可测组件落在 `cognition/src/config/`，不提升为新的 runtime-facing public API。
2. `project_config()` 只接受一致的 `RuntimePolicySnapshot`，并在五档受支持 profile 上投影出 `CognitionConfig`；缺失 canonical stage route 或 profile 不在允许集合内时 fail-closed。
3. `merge_profile_defaults()` 负责把 profile 差异收敛到现有 `CognitionConfig` 字段：
   - `edge_balanced` 收紧 `max_plan_nodes/max_plan_depth`
   - `edge_minimal` 进一步收紧 planning cap，但不关闭 cognition
   - `factory_test` 保留五段逻辑并增强观测
4. `derive_stage_model_hint()` 直接消费 canonical `stage_routes`，拒绝 `reasoning` / `planner` / `responder` 等私有 alias，保持 `StageModelHint.stage_name` 与 COG-TODO-002 一致。
5. `StageModelHint.task_type` 承载 cognition 组件语义，`capability_tier` / `requires_structured_output` / `requires_reasoning_trace` 按详设映射表派生，而不是在测试或 bridge 中私有 hardcode。

## 4. 边界与职责

| 组件 | 职责 | 非职责 |
|---|---|---|
| `CognitionConfigProjector` | 从 `RuntimePolicySnapshot` 投影 `CognitionConfig` 与 `StageModelHint` | 不维护第二套 profile schema；不解析原始 profile 文件；不执行 llm/runtime 决策 |
| `RuntimePolicySnapshot` | 作为 cognition 外部唯一配置快照输入 | 不直接表达 cognition 私有默认值 |
| `CognitionConfig` | 作为 cognition 模块公开配置载面 | 不回携 profile route key 或 provider-private 配置 |
| `StageModelHint` | 作为后续 bridge / resolver 的阶段模型建议 | 不等于 llm route ownership；llm router 仍可自主路由 |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| 以 `RuntimePolicySnapshot` 为唯一外部配置输入 | `cognition/src/config/CognitionConfigProjector.cpp` | `project_config()` 对一致快照返回值，对缺失 route 失败关闭 |
| profile 差异只在 projector 边界收敛 | 同上 | `edge_minimal` / `factory_test` 的 planning cap 与 observability 差异可二值断言 |
| canonical stage key 不能回流 alias | `derive_stage_model_hint()` + `CognitionConfigProjectionTest.cpp` | `reasoning` 等非 canonical key 被拒绝 |
| projector 维持模块内可测，不新增 public API | `cognition/src/config/CognitionConfigProjector.h` + `tests/unit/cognition/CMakeLists.txt` | cognition unit target 通过私有 src include seam 测试 projector |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增 `CognitionConfigProjector` 私有头与实现 | `project_config()` / `derive_stage_model_hint()` 可编译并返回稳定结果 | `Build_CMakeTools(buildTargets=["dasall_cognition_config_projection_unit_test"])` | 若依赖面过宽，回退到私有 src header，不扩 public include |
| B2 | 为 projector 新增专用单测 | 覆盖 profile 默认投影、stage hint 正例、alias 负例、缺失 route 负例 | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_config_projection_unit_test` | 若测试需跨模块内部头，仅补 `cognition/src` 到当前 unit target 私有 include |
| B3 | 补 CMake 源与 unit target 接线 | projector 可作为 cognition 私有组件参与后续 012 / 023 实现 | 同上 | 若接线过大，限定只动 cognition 模块与 cognition unit CMake |

## 7. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_cognition_config_projection_unit_test"])`
   - 第一次失败：`dasall_cognition` 未配置 `cognition/src` 私有 include，无法解析 `config/CognitionConfigProjector.h`。
   - 修补 `cognition/CMakeLists.txt` 后复跑通过。
2. `RunCtest_CMakeTools(tests=["CognitionConfigProjectionTest"])`
   - 结果：失败，返回仓库已知通用错误“生成失败”。
3. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_config_projection_unit_test`
   - 结果：通过；二进制零输出退出，表示 projector 的正例与负例断言全部通过。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 代码注释 | PASS：当前代码以强类型和函数名自解释，无需额外注释 |
| 正例覆盖 | PASS：覆盖五档 profile 中的 `edge_minimal`、`cloud_full`、`factory_test` 投影行为 |
| 负例覆盖 | PASS：覆盖缺失 canonical route 与非 canonical stage key 两类失败关闭路径 |
| 测试发现性 | PASS：新增 `CognitionConfigProjectionTest` 独立 unit target |
| TODO / worklog 追溯 | PASS：本交付物用于后续专项 TODO 与开发执行记录回写 |
| 范围控制 | PASS：仅修改 cognition 私有 projector、其 unit test 与相关文档，不扩到 runtime / llm 实现 |