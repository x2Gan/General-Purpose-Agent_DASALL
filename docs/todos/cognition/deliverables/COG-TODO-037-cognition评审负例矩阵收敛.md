# COG-TODO-037 cognition 评审负例矩阵收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md

## 1. 任务边界

1. 本任务只补评审缺口对应的回归矩阵与 gate 入口，不修改 cognition 生产语义。
2. 能复用既有 focused tests 的负例，不重复造第二套断言；本轮新增的 review regression 只负责补最薄弱、此前没有专用 gate 的缺口。
3. 目标是让评审指出的缺口在 CI/gate 中具备稳定失败出口，而不是继续依赖人工阅读日志判断。

## 2. 评审缺口到测试矩阵映射

| 评审缺口 | 测试入口 | 结论 |
|---|---|---|
| bridge not invoked | `CognitionReviewRegressionTest` + `CognitionFacadeFlowTest` | 新增 review regression 锁定 live integration 的 planning/execution/reflection bridge 调用；既有 façade flow 继续覆盖 response stage bridge |
| missing canonical route fail-closed | `CognitionReviewRegressionTest` | 新增 review regression 直接锁定 runtime init fail-closed 行为 |
| belief/context signal observed | `CognitionRuntimeInteractionContractTest` | 继续复用既有 writeback 与单次 context refresh 断言 |
| reflection abort observed | `CognitionRuntimeInteractionContractTest` | 继续复用 `AbortSafe` 停止主链断言 |
| schema pseudo-field rejected | `StageOutputValidatorSchemaTest` | 继续复用 escaped pseudo-field 负例 |
| placeholder alias absent | `CognitionReviewRegressionTest` | 新增文件级回归断言，阻止 integration CMake 回退到空跑别名 |

## 3. 设计收敛

### 3.1 边界与职责

1. review regression test 不拥有生产逻辑，只消费已存在的 runtime/cognition fixture、mock seam 和测试注册面。
2. placeholder alias absence 属于测试拓扑治理，不应通过 shell grep 作为唯一长期证据，因此新增代码级断言读取 `tests/integration/cognition/CMakeLists.txt`。
3. response stage bridge 是否可被回退由既有 `CognitionFacadeFlowTest` 与 `ResponseBuilderTemplateFallbackTest` 分工守护；review regression 不强行把允许的模板回退误报成 bridge 回归。

### 3.2 文件范围

1. `tests/integration/cognition/CognitionReviewRegressionTest.cpp`
2. `tests/integration/cognition/CMakeLists.txt`
3. 本交付物
4. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md`

### 3.3 流程与时序

1. 在 integration CMake 中注册 `CognitionReviewRegressionTest`。
2. 用 live integration fixture 跑一条最小 desktop_full path，验证 planning/execution/reflection canonical stage 真实调用 bridge。
3. 直接读取 integration CMake 源文件，验证 placeholder helper、历史 profile compatibility 空跑别名和空命令未回归。
4. 在任务文档中把新增 review regression 与既有 focused tests 组合成完整负例矩阵。

## 4. Validation

1. `Build_CMakeTools(buildTargets=["dasall_cognition_review_regression_integration_test","dasall_cognition_runtime_interaction_contract_integration_test","dasall_stage_output_validator_schema_unit_test","dasall_cognition_facade_flow_unit_test","dasall_response_builder_template_fallback_unit_test"])`
2. `RunCtest_CMakeTools(tests=["CognitionReviewRegressionTest","CognitionFacadeFlowTest","CognitionRuntimeInteractionContractTest","StageOutputValidatorSchemaTest","ResponseBuilderTemplateFallbackTest"])`

结果摘要：

1. 新增 `CognitionReviewRegressionTest` 已被 CMake 识别并通过。
2. review regression 首轮暴露了一个测试假设过宽的问题：response stage 允许模板回退，不能被硬编码为 live integration 必经 bridge。该断言已收紧到 planning/execution/reflection 三个 canonical stage。
3. 负例矩阵继续复用既有 runtime/schema/response focused tests，不重复复制断言逻辑。

## 5. 完成判定

COG-TODO-037 已完成。判定依据：

1. 评审缺口已映射到明确的测试入口，不再依赖人工比对日志。
2. 新增 review regression 对 bridge 调用和 placeholder alias 回归具备自动化失败出口。
3. 既有 focused tests 与新增 review regression 组合后，可以覆盖 037 要求的负例矩阵。