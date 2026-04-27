# COG-TODO-035 StageOutputValidator 结构化校验收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Design + Build validator 结构化 JSON/schema 补强

## 1. 本地证据

1. `cognition/src/validation/StageOutputValidator.cpp` 当前通过 `payload.find("\"field\":")`、`find(']')`、`find('"')` 这类 substring/单字符扫描做 required / enum / numeric / list 校验。
2. 这条实现路径没有先把 payload 解析成结构化 object，因此顶层 schema 约束会误命中字符串字面量、嵌套 object / array 中的同名字段，也会被转义引号和嵌套数组中的逗号扰动。
3. 现有 `StageOutputValidatorSchemaTest` 只覆盖“简单合法 JSON”与“显式缺失/越界”，没有覆盖 035 TODO 指向的空白顺序变化、字符串转义伪字段、嵌套数组、字段类型错误和 malformed JSON fail-closed。
4. 仓库当前没有正式 JSON 第三方依赖；`apps/simulator` 的简化提取不满足生产校验要求，但 `tools/src/execution/WorkflowEngine.cpp` 已有一套更稳的字符串/值 token 解析思路，可作为 cognition private helper 的参考。

## 2. 边界与职责收敛

1. 035 只补 `StageOutputValidator::validate_stage_output()` 的 payload 结构化解析，不扩 shared utility，也不改 `llm_bridge::StageSchemaSpec` public surface。
2. JSON 解析 helper 保持在 `cognition/src/validation/StageOutputValidator.cpp` 的 private namespace 内，避免把仓库尚未统一的 parser 能力扩成跨模块依赖。
3. `StageSchemaSpec.field_path` 继续保持当前 validator private 语义：本轮至少保证顶层 object field 的结构化解析正确，不再允许通过全文字符串扫描“撞中”字段。
4. malformed JSON 继续 fail-closed，并以 validator 自己的 `ValidationIssueSet` / `ErrorInfo` 输出，不把 parser 错误穿透成 llm 或 runtime 错误类型。

## 3. 数据与接口说明

| 接口 / 数据 | 方向 | 本轮约束 |
|---|---|---|
| `StageLlmCallResult.response.content_payload` | llm bridge -> validator | 必须先解析成结构化 top-level object；解析失败即 fail-closed |
| `StageSchemaSpec.required_fields / enum_constraints / numeric_bounds / list_constraints` | validator private schema | 继续作为 validator private spec，不改 public signature |
| `ValidationIssueSet` / `ValidationResult` | validator -> facade / tests | malformed JSON、字段类型错误、伪字段命中都要有 deterministic validation result |

## 4. 流程与时序

1. validator 先把 `content_payload` 解析为 top-level object field -> raw JSON token map。
2. required / enum / numeric / list 约束全部基于该 map 做字段级定位，不再扫描全文。
3. string value 解析必须处理转义；numeric value 必须基于完整 raw token 解析；list size 必须基于顶层 array item 计数而不是直接数逗号。
4. 解析对象失败、字段 token 类型与约束不匹配、或数组/字符串未正确闭合时，统一 fail-closed。

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| 摆脱全文字符串扫描 | `StageOutputValidator.cpp` private parser helper | nested/escaped pseudo field 不再误判为真实字段 |
| array size 基于结构化 token 计数 | `extract_list_size()` 替换为 array token 解析 | 嵌套数组不会因内部逗号被误计数 |
| malformed JSON fail-closed | `validate_stage_output()` 解析前置 | 非法 JSON 返回 deterministic validation failure |
| 只在 cognition private 范围补 parser | 不新增 shared/third-party JSON 依赖 | 035 不改跨模块接口与依赖图 |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 在 `StageOutputValidator.cpp` 增加 private top-level object/token parser，并替换 required/enum/numeric/list 提取逻辑 | schema test 覆盖空白顺序变化、嵌套数组、malformed JSON | `Build_CMakeTools(buildTargets=["dasall_stage_output_validator_schema_unit_test","dasall_stage_output_validator_plan_graph_invariant_unit_test","dasall_stage_output_validator_response_envelope_unit_test"])` | 若 parser 复杂度膨胀，限制在 top-level object + raw value token，不扩 full AST |
| B2 | 扩展 `StageOutputValidatorSchemaTest.cpp` 验证伪字段与类型错误 fail-closed | focused unit 证明旧 substring bypass 已关闭 | `RunCtest_CMakeTools(tests=["StageOutputValidatorSchemaTest","StageOutputValidatorPlanGraphInvariantTest","StageOutputValidatorResponseEnvelopeTest"])` | 若现有 tests 表达力不足，只补 validator-focused cases，不新开 integration target |

## 7. D Gate

Gate = PASS。

进入 Build 的依据已经充分：owner 已锁定在 `StageOutputValidator`；仓库又没有可直接复用的正式 JSON 依赖，因此 035 的最小正确做法是在 cognition private validator 内补窄 parser，而不是继续加 substring 规则。

## 8. Build 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_stage_output_validator_schema_unit_test","dasall_stage_output_validator_plan_graph_invariant_unit_test","dasall_stage_output_validator_response_envelope_unit_test"])`
	- 结果：PASS。`StageOutputValidator.cpp` 与三条 validator-focused unit targets 均成功编译并链接。
2. `RunCtest_CMakeTools(tests=["StageOutputValidatorSchemaTest","StageOutputValidatorPlanGraphInvariantTest","StageOutputValidatorResponseEnvelopeTest"])`
	- 结果：PASS。首轮仅暴露 035 本地 parser 缺口：array item tokenizer 把外层 `]` 误判为非法字符；同一 slice 修正后复跑 `3/3` tests passed。
	- 备注：工具 stderr 仍打印已知 `DartConfiguration.tcl` 缺失提示，但 result code=0 且 stdout 明确显示三条测试全部通过，按仓库基线计为有效证据。
3. `get_errors(filePaths=[cognition/src/validation/StageOutputValidator.cpp, cognition/src/validation/StageOutputValidator.h, tests/unit/cognition/StageOutputValidatorSchemaTest.cpp])`
	- 结果：无新增编辑器错误。

## 9. 完成判定

已完成。完成信号如下：

1. `validate_stage_output()` 不再通过全文 substring 扫描命中字段，而是先解析 top-level JSON object，再基于字段 token 做 required / enum / numeric / list 校验。
2. escaped pseudo-field 不再能绕过 required field 校验；nested array list size 改为按 top-level item 计数，不再被内部逗号扰动。
3. malformed JSON 现在 deterministic fail-closed，并通过 `ValidationIssueCode::MalformedJson` 统一输出。
4. `StageOutputValidatorSchemaTest` 已覆盖空白顺序变化、escaped pseudo-field、nested array、字段类型错误与 malformed JSON；plan graph / response envelope 既有 focused tests 保持通过。
5. 本轮未新增 shared / third-party JSON 依赖，parser 能力仍严格限定在 cognition private validator helper 内。