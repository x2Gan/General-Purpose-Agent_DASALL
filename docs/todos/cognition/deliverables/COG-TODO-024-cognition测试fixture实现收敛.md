# COG-TODO-024 cognition 测试 fixture 实现收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready test fixture implementation

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §7.1 的 COG-D09 已把 `MockLLMManager` 和 `MockCognitionFixture` 定义为 cognition bridge、telemetry、runtime smoke、failure/profile integration 的统一测试支撑面。
2. `docs/todos/cognition/deliverables/COG-TODO-004-cognition测试fixture口径收敛.md` 已冻结两类 fixture 的职责边界：`MockLLMManager` 负责 stage hint / failure projection / redaction 相关的 llm manager double，`MockCognitionFixture` 负责 runtime caller shape 与最小 request/result helper。
3. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 明确 COG-TODO-020、022、023 与 026~029 在实现前都依赖 COG-BLK-004 关闭；而 COG-BLK-004 的实现侧缺口就是 `tests/mocks/include/MockLLMManager.h` 与 `MockCognitionFixture.h` 尚未落盘。
4. 代码现状显示 `tests/mocks/include/` 之前只有 `MockLLMAdapter.h`、`MockTool.h`、`MockMemoryStore.h` 等通用脚手架，没有 cognition-facing 的 manager-level double，也没有生成 `CognitionStepRequest` / `ReflectionRequest` / `ResponseBuildRequest` 的 runtime caller fixture。

## 2. 外部参考

1. Martin Fowler 在 Mocks Aren't Stubs 中区分了 stub、spy、mock 等 test double，并强调 mock/spy 价值在于行为验证与调用记录，而不是把真实协作者全部搬进测试；这与本轮把 `MockLLMManager` 设计成 manager-level scripted double、把 `MockCognitionFixture` 设计成最小 caller-shape object mother 的方向一致：https://martinfowler.com/articles/mocksArentStubs.html

## 3. 主结论

1. 新增 `tests/mocks/include/MockLLMManager.h`，以 `ILLMManager` 为唯一 public seam 落地 cognition-facing llm manager double，支持：
   - stage 级 scripted result；
   - 默认成功 / 失败结果 helper；
   - `LLMGenerateRequest` 调用记录与 `HealthStatus` 查询计数；
   - 不暴露 provider-private 字段，也不自建 retry / breaker。
2. 新增 `tests/mocks/include/MockCognitionFixture.h`，固定 Runtime caller shape：
   - 默认 `caller_domain=runtime.agent_orchestrator`；
   - 生成最小合法的 `GoalContract`、`ContextPacket`、`BeliefState`、`Observation`；
   - 生成 `CognitionStepRequest`、`ReflectionRequest`、`ResponseBuildRequest`；
   - 提供 `make_engine()` / `make_response_builder()` / `make_response_result()` 等 helper，供后续 023 / 026 / 027 / 028 / 029 复用。
3. 新增 `tests/unit/cognition/MockCognitionFixtureSurfaceTest.cpp` 与对应 CMake 接线，证明这两个 mock seam 已经不是“文档口径”，而是当前 public surface 可以直接消费的真实构件。
4. 本轮不提前创建 `tests/integration/cognition/` 拓扑，也不伪造 `RuntimeCognitionLoopSmokeTest`、`CognitionFailureInjectionIntegrationTest`、`CognitionProfileCompatibilityTest`。这些 discoverability gate 继续由 COG-TODO-025、026、028、029 收口；024 的 owner 是把 mock seam 真正落盘并完成 narrow validation，从而关闭 COG-BLK-004。

## 4. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| `MockLLMManager` 必须成为 cognition-facing llm manager double | `tests/mocks/include/MockLLMManager.h` | stage 级 scripted result、调用记录和一致性 helper 可直接被 unit tests 消费 |
| `MockCognitionFixture` 必须固定 runtime caller shape | `tests/mocks/include/MockCognitionFixture.h` | decide / reflect / response 三类 request 均可通过 `InputBoundaryValidator` |
| 024 需要可执行验证，而不是只落头文件 | `tests/unit/cognition/MockCognitionFixtureSurfaceTest.cpp`、`tests/unit/cognition/CMakeLists.txt` | 新测试 target 可被构建系统识别并显式运行通过 |
| integration discoverability 仍是后继 gate | COG-TODO-025、026、028、029 | 024 不伪造下游 integration 测试名，只提供其必需 fixture seam |

## 5. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增 `MockLLMManager.h` | 由 surface test 记录 stage request 并验证 scripted result | `Build_CMakeTools(buildTargets=["dasall_mock_cognition_fixture_surface_unit_test"])` | 若 llm manager result helper 与当前 public ABI 不一致，只回修 mock helper，不扩到 llm production owner |
| B2 | 新增 `MockCognitionFixture.h` | 由 surface test 验证三类 request 都通过 boundary validator | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_mock_cognition_fixture_surface_unit_test` | 若 request helper 漏字段，只回修 fixture 默认值，不改 production validator |
| B3 | 注册 narrow surface test | 确认 mock seam 真正可消费 | `ListBuildTargets_CMakeTools` / `ListTests_CMakeTools` 可见新 target 与 test 名 | 若 CMake Tools test 运行继续报 `生成失败`，按仓库基线回退显式二进制执行 |

## 6. 验证证据

1. `ListBuildTargets_CMakeTools`
   - 结果：成功，新增 target `dasall_mock_cognition_fixture_surface_unit_test` 已出现在可构建目标列表中。
2. `Build_CMakeTools(buildTargets=["dasall_mock_cognition_fixture_surface_unit_test"])`
   - 第一次结果：失败；`MockCognitionFixtureSurfaceTest.cpp` 使用了未纳入 include path 的 `route/ModelSelectionHint.h`。
   - 同一 slice 修补为显式相对路径后复跑：通过，目标成功完成编译与链接。
3. `ListTests_CMakeTools`
   - 结果：成功，新增 `MockCognitionFixtureSurfaceTest` 已出现在 tests 列表中。
4. `RunCtest_CMakeTools(tests=["MockCognitionFixtureSurfaceTest"])`
   - 结果：失败，工具返回通用错误 `生成失败`；与仓库既有 CTest 工具态噪声一致，不判定为代码失败。
5. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_mock_cognition_fixture_surface_unit_test`
   - 结果：通过；零输出退出，说明 `MockLLMManager` 的 scripted result / 调用记录和 `MockCognitionFixture` 的 request builder 均能被当前 cognition public surface 消费。

## 7. 完成判定与边界

1. COG-BLK-004 已关闭：真实 `MockLLMManager` / `MockCognitionFixture` header 已落盘，bridge / telemetry / facade / integration 后续不再需要借用 `MockLLMAdapter + MockTool` 旧路径来伪装 cognition gate。
2. 本轮没有宣称 integration discoverability 已完成。`tests/integration/cognition/` 拓扑与 `ctest -N` 下游 smoke/failure/profile 用例发现仍由 COG-TODO-025、026、028、029 继续收口。
3. 024 的 owner 仅限测试 seam 和 narrow validation，不把 Runtime 主链、LLM bridge、Telemetry sink 或 façade 串联逻辑提前并入本轮。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 范围控制 | PASS：只新增 mock seam 头文件、一个 narrow surface test 和最小 CMake 接线 |
| runtime caller 边界 | PASS：fixture 固定 `runtime.agent_orchestrator` caller shape，不构造第二套私有 caller 语义 |
| llm manager 边界 | PASS：mock 只模拟 `ILLMManager` 行为，不倒灌 provider adapter / prompt governance / retry breaker |
| 正负例覆盖 | PASS：surface test 覆盖 scripted success 正例；首轮构建失败提供 include 路径负例并在同一 slice 修正 |
| discoverability 约束 | PASS：新 unit target / test 已可被目标和测试列表发现；integration discoverability 继续留在后继任务，不做虚假声明 |