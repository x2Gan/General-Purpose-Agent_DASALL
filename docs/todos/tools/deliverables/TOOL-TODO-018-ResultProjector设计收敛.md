# TOOL-TODO-018 ResultProjector 设计收敛

日期：2026-04-16  
任务：TOOL-TODO-018  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 5.2.6、6.10、6.12.2 已冻结 ObservationDigest 五字段边界，以及 ResultProjector 作为 `ToolResult -> Observation / ObservationDigest / audit facts` 唯一投影出口的职责。
2. contracts/include/observation/ObservationDigest.h 与 ObservationDigestGuards.h 已固定 summary / key_facts / citations / omitted_details / confidence 的必填与边界语义，因此 018 的实现重点是把规则化投影从 ToolManager 内部 fallback 抽出来，而不是扩 contracts。
3. TOOL-TODO-017 已把 builtin route 执行收口到 BuiltinExecutorLane，018 的唯一主目标是替换默认 projector，并确保投影发生在 `normalize_result()` 之后，避免 digest 基于未归一化 result 生成。

## 2. Design 结论

1. 新增 module-local `ResultProjector`，集中实现 `project()`、`project_success()`、`project_failure()`、`build_observation()`、`build_digest()`，成为 tools 内部唯一默认投影实现。
2. ToolManager 现改为先 `normalize_result()`，再把归一化后的 `ToolResult` 交给 projector；即便调用方自定义 projector 返回部分 envelope，也会由标准 `ResultProjector` 作为 fallback 补齐 observation / digest / evidence / failure_reason，从根上修复“投影早于归一化”的不一致。
3. success path 的规则化摘要采用 v1 纯规则实现：
   - 结构化 JSON 优先提取顶层 `summary` / `message` / `description` / `result`；
   - 无约定摘要键时退回到 route-aware fallback summary；
   - key facts 只展开顶层字段，数组只投影前 5 项并标注 `[N of M total]`，嵌套对象记为 `{...}`。
4. failure path 明确不泄漏 raw payload 到 digest：summary 只来自 `ErrorInfo.message + failure_type`，key facts 只暴露 `failure_type`、`error_stage`、route/decision，若原始 payload 存在则通过 `omitted_details` 记录 `failure payload suppressed`。
5. confidence 采用规则化扣减：summary 截断 -0.1，fact 截断 -0.05/项，payload 超过 4096 bytes -0.15，下限 clamp 到 0.1；其语义只衡量投影忠实度，不表示执行成功概率。
6. 默认 citations / evidence refs 保持执行身份最小集：`tool_call`、`route_kind`、`route_reason` 与可选 `server_id`，为后续 019~021 的 observability bridge 提供稳定输入面。

## 3. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| ResultProjector 本体 | tools/src/projection/ResultProjector.h、tools/src/projection/ResultProjector.cpp |
| ToolManager 默认 projector 替换与 normalize 后投影 | tools/src/ToolManager.cpp |
| 结构化 success 投影验证 | tests/unit/tools/ResultProjectorTest.cpp |
| plain-text truncation / omitted details / confidence 验证 | tests/unit/tools/ResultProjectorTruncationTest.cpp |
| failure path payload suppression + digest contract 验证 | tests/unit/tools/ResultProjectorConfidenceTest.cpp |
| unit / contract 注册与追踪回写 | tools/CMakeLists.txt、tests/unit/tools/CMakeLists.txt、tests/unit/CMakeLists.txt、docs/todos/tools/DASALL_tools子系统专项TODO.md、docs/worklog/DASALL_开发执行记录.md |

## 4. Build 三件套

1. 代码目标：
   - tools/src/projection/ResultProjector.h
   - tools/src/projection/ResultProjector.cpp
   - tools/src/ToolManager.cpp
2. 测试目标：
   - `ResultProjectorTest`
   - `ResultProjectorTruncationTest`
   - `ResultProjectorConfidenceTest`
   - `ToolManagerPipelineTest`
   - `ToolManagerFailurePathTest`
   - `ObservationDigestBoundaryContractTest`
   - full unit regression
   - full contract regression
3. 验收基线：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_tools dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract`

## 5. 本地验证

1. 定向构建：
   - Build_CMakeTools: `dasall_result_projector_unit_test`、`dasall_result_projector_truncation_unit_test`、`dasall_result_projector_confidence_unit_test`
   - Build_CMakeTools: `dasall_tool_manager_pipeline_unit_test`、`dasall_tool_manager_failure_path_unit_test`
   - Build_CMakeTools: `dasall_contract_observation_digest_boundary_test`
2. 定向验证：
   - RunCtest_CMakeTools: `ResultProjectorTest`、`ResultProjectorTruncationTest`、`ResultProjectorConfidenceTest`
   - RunCtest_CMakeTools: `ToolManagerPipelineTest`、`ToolManagerFailurePathTest`
   - RunCtest_CMakeTools: `ObservationDigestBoundaryContractTest`
3. 聚合回归：
   - Build_CMakeTools: `dasall_tools`、`dasall_unit_tests`、`dasall_contract_tests`
   - `dasall_unit_tests` 构建期间自动执行 unit 集合，结果 `278/278 passed`
   - `dasall_contract_tests` 构建期间自动执行 contract 集合，结果 `152/152 passed`
4. 结果摘要：
   - ResultProjector 的 structured success、plain-text truncation、failure payload suppression 和 digest contract 行为全部通过。
   - ToolManager pipeline / failure tests 在默认 projector 切换为独立实现后保持通过。
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，但未影响 unit / contract 通过结论。

## 6. 风险与回退

1. v1 的结构化提取仍是浅层 top-level 规则，不支持深层 JSON 展开或 schema-aware projection；若后续 tool payload 更复杂，应在 projector 内扩规则，而不是回退到 raw payload 透传。
2. 当前 projector 已提供稳定 citations / omitted_details / confidence，但 audit / metrics / trace 仍未拥有独立 bridge；019~021 需要继续把这些投影事实接入 observability 主链，而不是在 ToolManager 中追加临时逻辑。