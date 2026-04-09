# CAP-TODO-009 ServiceContextBuilder 上下文规范化骨架设计收敛

日期：2026-04-09  
任务：CAP-TODO-009  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.3 已定义 ServiceContextBuilder 的职责是汇聚 request/session/trace/tool_call/goal/budget/deadline 元数据，并输出 ServiceCallContext。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.7.1 主执行链路要求 IExecutionService 在进入 execution lane 之前调用 `normalize_context()`，因此上下文规范化必须早于 façade 委派发生。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 9.1 的测试矩阵明确要求 ServiceContextBuilder 至少覆盖一条 IDs/deadline/budget 透传正例和一条缺字段负例。
4. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 与 6.6 已冻结 ServiceCallContext 的字段面，因此本轮只能校验并透传既有字段，不能新增 services 私有上下文字段。
5. CAP-TODO-009 的完成判定要求“上下文归一化不新增语义字段、不吞 budget/deadline，且负路径返回可观测失败”，因此需要显式结果对象承载成功/失败状态。

## 2. 外部参考

1. C++ Core Guidelines F.20 建议“对输出值优先使用返回值而不是输出参数”。本轮据此让 `normalize_context()` 返回 `ContextNormalizationResult`，直接携带规范化后的 `ServiceCallContext` 或错误消息，避免通过隐式状态或输出参数传递失败信息。

## 3. Design 结论

1. ServiceContextBuilder 保持模块内部组件形态，不进入 services/include 的公共 ABI。
2. `normalize_context()` 只负责校验并透传 ServiceCallContext，成功时原样返回既有字段；失败时返回显式错误字符串，不新增 contracts 级错误对象。
3. `request_id`、`session_id`、`trace_id`、`tool_call_id`、`goal_id` 必须非空，`deadline_ms` 必须大于 0；`budget_guard` 保持可选并在存在时原样透传。
4. 本轮不引入 request id 自动生成、deadline 重算、budget 派生等新语义，继续把预算上界与 deadline owner 保持在 Runtime。
5. 为满足本轮 unit 验证，需要补一条最小 services unit target；更完整的 services unit 拓扑收口仍保留给 CAP-TODO-011。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 定义显式规范化结果对象 | services/src/ServiceContextBuilder.h 中的 ContextNormalizationResult |
| 实现上下文校验与透传 | services/src/ServiceContextBuilder.cpp 中的 normalize_context() |
| 覆盖透传正例 | tests/unit/services/ServiceContextBuilderTest.cpp |
| 覆盖缺字段负例 | tests/unit/services/ServiceContextBuilderTest.cpp |
| 提供最小 unit 验证入口 | tests/unit/CMakeLists.txt 中的 dasall_service_context_builder_unit_test |

## 5. Build 三件套

1. 代码目标：新增 services/src/ServiceContextBuilder.h，更新 services/src/ServiceContextBuilder.cpp，实现 `normalize_context()` 的校验与透传骨架。
2. 测试目标：新增 ServiceContextBuilderTest，覆盖一条完整上下文透传正例和一条缺失 request_id 的负例；通过最小 unit target 接线保证该测试进入 `dasall_unit_tests`。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_services dasall_unit_tests
   - ctest --test-dir build-ci --output-on-failure -L unit

## 6. 风险与回退

1. ServiceContextBuilder 仍只输出内部结果对象；若后续 façade 需要把规范化失败映射为公共 `ResultCode` / `ErrorInfo`，应在 CAP-TODO-010 中处理，而不是回头扩张本轮内部接口。
2. tests/unit/CMakeLists.txt 中新增的最小 target 只是为了让 009 的 unit 可执行；CAP-TODO-011 仍需把 services unit 目录整理成稳定拓扑，并补齐 ServiceHeaderLayout / ServiceFacade 等后续入口。