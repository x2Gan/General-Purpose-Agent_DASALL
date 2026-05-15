# COG-TODO-044 cognition 测试初始化 warning 收敛

状态：Done
日期：2026-05-15
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：测试初始化 warning hygiene 收口

## 1. 任务边界

1. 本任务只处理 cognition 责任域测试中的 `CognitionRuntimeDependencies` 聚合初始化 warning，不扩张到 tools / infra / services 等其他子系统。
2. 本任务不改变 cognition 生产行为，不修改 ADR-006 / 007 / 008 结论，只把测试初始化显式补全到当前依赖结构体字段集合。
3. 本任务的验收以 044 行锁定的三个 cognition unit targets 为准；额外补齐的 snapshot-backed integration helper 只作为同类 warning hygiene 的顺手收口，不改变本轮验收出口。

## 2. 根因与设计结论

1. `CognitionRuntimeDependencies` 在 `cognition/include/CognitionDependencies.h` 中已经新增 `policy_snapshot` 字段。
2. `tests/mocks/include/MockCognitionFixture.h`、`tests/unit/cognition/ResponseBuilderTemplateFallbackTest.cpp` 以及两个 snapshot-backed integration helper 仍使用旧的显式聚合初始化，只填写了 `llm_manager`，因此在编译时触发 missing-field initializer warning。
3. 最小修复策略：
   - 对没有 runtime policy snapshot 的测试夹具和 fallback 单测，显式写入 `.policy_snapshot = nullptr`。
   - 对已经持有 snapshot 的 integration helper，显式透传 `.policy_snapshot = snapshot`，避免依赖 factory 内部的兜底填充逻辑来“碰巧消除” warning。

## 3. 实施结果

1. 更新 `tests/mocks/include/MockCognitionFixture.h`
   - `make_engine()` 与 `make_response_builder()` 的 `CognitionRuntimeDependencies` 现都显式设置 `.policy_snapshot = nullptr`。
2. 更新 `tests/unit/cognition/ResponseBuilderTemplateFallbackTest.cpp`
   - bridge failure fallback 场景下的 `create_response_builder(..., CognitionRuntimeDependencies{...})` 现显式设置 `.policy_snapshot = nullptr`。
3. 更新 snapshot-backed helper
   - `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp` 中的 helper 现显式透传 `.policy_snapshot = snapshot`。
   - `tests/integration/cognition/CognitionStructuredOutputIntegrationTest.cpp` 中的 helper 现显式透传 `.policy_snapshot = snapshot`。

## 4. 验证证据

1. `cmake -S . -B build-ci-cog044 -G "Unix Makefiles"`
   - 结果：配置成功。
2. `cmake --build build-ci-cog044 --target dasall_cognition_facade_flow_unit_test dasall_response_builder_template_fallback_unit_test dasall_mock_cognition_fixture_surface_unit_test`
   - 结果：三个目标均构建成功，构建输出中未再出现 `CognitionRuntimeDependencies::policy_snapshot` 或 `missing initializer` 相关 warning。
3. `ctest --test-dir build-ci-cog044 --output-on-failure -R "CognitionFacadeFlowTest|ResponseBuilderTemplateFallbackTest|MockCognitionFixtureSurfaceTest"`
   - 结果：3/3 通过。

## 5. 完成判定

COG-TODO-044 已完成。

1. cognition 责任域已不再因 `policy_snapshot` 聚合初始化遗漏产生本任务关注的 warning。
2. `CognitionFacadeFlowTest`、`ResponseBuilderTemplateFallbackTest`、`MockCognitionFixtureSurfaceTest` 行为未变，说明本轮仅完成 warning hygiene 收口。
3. `COG-R23` 的剩余面已闭合；Gate-COG-12 继续保持 Pending 的原因只剩 repo-wide non-cognition blocker，而非 cognition 自身 docs / warning hygiene 未收口。