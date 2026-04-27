# COG-TODO-013 InputBoundaryValidator 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready input boundary validation

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.9 明确要求：当 request 缺 `GoalContract` / `ContextPacket` / `BeliefState` 关键字段时，cognition 输出必须是 `ErrorInfo + failed ResultCode`，Runtime 后续动作是立即失败而不是重试。
2. 同一文档 §6.13.2 指出，歧义检测和输入边界校验应尽早发生，避免把不确定输入直接放大为错误工具意图或错误回复。
3. 请求边界表要求 `caller_domain`、`request_id`、`trace_id`、`profile_id`、`goal_contract`、`context_packet`、`belief_state` 为 decide / reflect 主链的必填输入；其中 `belief_state` 缺失时必须由 `InputBoundaryValidator` fail-fast，不允许退化为 recent-history only。
4. §9.3 的失败注入表再次冻结了负例语义：`缺失 BeliefState` 的预期行为是输入边界校验失败，不得静默退化。
5. §7.1 的 COG-D03 已把 `validation/InputBoundaryValidator.cpp` 和 `PerceptionBoundaryValidationTest.cpp` 作为感知阶段最小可用链路的一部分，因此 013 只需要落私有 validator seam，不扩 public/shared contract。

## 2. 外部参考

1. OWASP Input Validation Cheat Sheet 指出，输入校验应尽可能早地发生在数据流入口，防止格式错误的数据继续流入下游并触发组件异常：https://cheatsheetseries.owasp.org/cheatsheets/Input_Validation_Cheat_Sheet.html

本轮借鉴点：把 request 的 allowlist 式 required-field 校验集中到 cognition 边界统一处理，让 façade 和 response builder 在入口 fail-fast，而不是各自兜底出一套静默降级逻辑。

## 3. 主结论

1. 新增私有 `InputBoundaryValidator`，统一提供 `validate_decide_request()`、`validate_reflection_request()`、`validate_response_request()` 三条入口校验路径。
2. validator 对顶层元数据以及 `GoalContract`、`ContextPacket`、`BeliefState`、`Observation` 的 required 字段做 presence 校验；`decide()` 允许首轮没有 `latest_observation`，但如果传入 Observation，则仍要求其内部字段完整。
3. `CognitionFacade::decide()`、`CognitionFacade::reflect()` 与 `ResponseBuilder::build()` 现在都先走 validator，invalid input 会统一映射为 `ResultCode::ValidationFieldMissing` + stage-specific `ErrorInfo`，不再静默降级为 recent-history only 或默认部分结果。
4. `ContextSufficiencySignal` 现在会携带精确缺失字段列表；当缺失项位于 `context_packet` / `belief_state` / `latest_observation` 时，显式要求 Runtime 触发 `recommend_context_reload`。
5. validator 保持在 `cognition/src/validation/` 私有目录下，不改变 public include，也不把 input boundary 语义推进 shared contracts。

## 4. 边界与职责

| 组件 | 职责 | 非职责 |
|---|---|---|
| `InputBoundaryValidator` | 校验 cognition 三入口 request 的 required field presence，并生成统一 `ErrorInfo` | 不做阶段执行；不做 schema/output 校验；不持有恢复策略 |
| `CognitionFacade` | 在 decide / reflect 入口 fail-fast，返回显式错误与上下文缺口提示 | 不在各分支手写第二套输入校验规则 |
| `ResponseBuilder` | 在 build 入口拒绝不完整 request，避免错误输入被包装成表面成功结果 | 不替 Runtime 决定 retry / recovery |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| `cognition.invalid_input` 必须是统一入口错误 | `cognition/src/validation/InputBoundaryValidator.cpp` | 三类 request 都能输出统一 `ValidationFieldMissing` + `ErrorInfo` |
| 缺失 `BeliefState` 不得退化为 recent-history only | `InputBoundaryValidator.cpp` + `CognitionFacade.cpp` | invalid decide request 不再合成默认 action decision |
| observation 缺口要在 reflect 入口 fail-fast | `InputBoundaryValidator.cpp` + `CognitionFacade.cpp` | `CognitionFacadeInvalidInputTest` 断言缺失 payload 时无 reflection decision |
| 输入边界校验必须可 discover | `tests/unit/cognition/CMakeLists.txt` + 两个新 test 文件 | 新 target 可被 CMake Tools 发现并构建 |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增 `InputBoundaryValidator` 私有 seam 和 supporting result struct | validator 正例 / 负例可独立断言 | `Build_CMakeTools(buildTargets=["dasall_perception_boundary_validation_unit_test","dasall_cognition_facade_invalid_input_unit_test"])` | 若边界语义外溢，回退为 `cognition/src` 私有 header |
| B2 | 把 façade / response builder 接入统一入口校验 | invalid input 不再落入静默降级分支 | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_boundary_validation_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_invalid_input_unit_test` | 若 fail-fast 影响公共对象，限制在 result_code / error_info / diagnostics slice |
| B3 | 注册 focused unit tests 与 CMake 接线 | discoverability 与直接执行同时成立 | 同上 | 若测试接线过大，仅保留 cognition unit 范围 |

## 7. 验证证据

1. `ListBuildTargets_CMakeTools()`
   - 结果：可发现 `dasall_perception_boundary_validation_unit_test` 与 `dasall_cognition_facade_invalid_input_unit_test`。
2. `Build_CMakeTools(buildTargets=["dasall_perception_boundary_validation_unit_test","dasall_cognition_facade_invalid_input_unit_test"])`
   - 结果：通过。
3. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_boundary_validation_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_invalid_input_unit_test`
   - 结果：通过；两项 focused unit tests 均零输出退出。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 范围控制 | PASS：只新增私有 validator、focused tests、CMake 接线和最小 façade fail-fast 改动 |
| 错误语义 | PASS：invalid input 统一映射到 `ValidationFieldMissing` + stage-specific `ErrorInfo` |
| 负例覆盖 | PASS：覆盖缺失 goal/context/belief 字段和缺失 observation payload 两类主要负例 |
| discoverability | PASS：两项新 cognition unit target 已可被 CMake Tools 发现 |
| 架构边界 | PASS：未侵入 ADR-006/007/008；cognition 只做输入边界校验，不获取 recovery 主控权 |