# COG-TODO-039 统一验收构建聚合与 runtime fixture 收敛

状态：Done
日期：2026-05-15
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build 测试聚合与 runtime init fixture 修复

## 1. 任务边界

1. 本任务只修复统一 unit 聚合中的 runtime cognition smoke 可执行发现性，以及 runtime unary fixture 的 canonical stage route 合法性。
2. 本任务不改写 cognition / runtime 生产语义，只修复测试聚合入口和最小 init fixture。
3. 若 `dasall_unit_tests` 后续仍存在其它子系统失败，只要失败原因不再是 `RuntimeCognitionLoopSmokeTest` missing executable 或 `RuntimeControlPlaneSurfaceTest` 的 canonical route 缺失，即不阻断 039 完成判定。

## 2. 本地证据

1. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 中 COG-TODO-039 已明确记录：`RuntimeCognitionLoopSmokeTest` 已注册但未纳入 `dasall_unit_tests` 聚合构建，且 `RuntimeUnaryFixture.h` 的最小 policy snapshot 只提供 `main` route。
2. `tests/unit/CMakeLists.txt` 原有 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS` 未包含 `dasall_runtime_cognition_loop_smoke_unit_test`，因此统一 unit 聚合无法保证先产出该 executable。
3. `tests/fixtures/runtime/RuntimeUnaryFixture.h` 原有 `make_policy_snapshot()` 只填充 `main` route；而 `runtime/src/AgentFacade.cpp` 在需要按 `policy_snapshot` 组合 cognition ports 时，会通过 cognition factories 消费 canonical `planning`、`execution`、`reflection`、`response` stage routes，缺失时直接 fail-closed。
4. `tests/unit/runtime/RuntimeControlPlaneSurfaceTest.cpp` 直接使用 `make_init_request("desktop_full", ...)`，因此它确实走到了最小 init fixture 与 runtime policy projector 的真实组合路径，而不是纯 stub 绕过。

## 3. 修复结果

1. `tests/unit/CMakeLists.txt`
   - 将 `dasall_runtime_cognition_loop_smoke_unit_test` 纳入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`。
   - 结果：`dasall_unit_tests` 运行时会真实包含并执行 `RuntimeCognitionLoopSmokeTest`，不再出现仅注册不聚合的缺口。
2. `tests/fixtures/runtime/RuntimeUnaryFixture.h`
   - 将最小 `policy_snapshot` 从仅有 `main` route 扩展为 `main` + canonical `planning`、`execution`、`reflection`、`response` routes。
   - 结果：`RuntimeControlPlaneSurfaceTest` 的最小 init fixture 可以通过 cognition canonical route 投影，不再因 stage route 缺失被 runtime init fail-closed。

## 4. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_runtime_control_plane_surface_unit_test","dasall_runtime_cognition_loop_smoke_unit_test"])`
   - 结果：通过，两个受影响 runtime unit targets 均成功编译并链接。
2. `RunCtest_CMakeTools(tests=["RuntimeControlPlaneSurfaceTest","RuntimeCognitionLoopSmokeTest"])`
   - 结果：通过，两个聚焦 tests 全绿。
3. `Build_CMakeTools(buildTargets=["dasall_unit_tests"])`
   - 结果：聚合 target 已真实执行 `RuntimeCognitionLoopSmokeTest`，日志中其作为 unit 聚合序列内测试通过；剩余失败点出现在 access/daemon 责任域（`DaemonReadinessCommandTest`、`DaemonCancelCommandTest`、`AccessCancelForwardingTest`），不再属于 039 的 missing executable 或 runtime fixture route 缺口。
4. `ctest --test-dir build-ci-cog039 -N | rg "RuntimeCognitionLoopSmokeTest|RuntimeControlPlaneSurfaceTest"`
   - 结果：干净 Unix Makefiles 目录中可检索到两条 runtime 测试 discoverability 记录，说明 039 关注的测试入口已被发现。

## 5. 完成判定

COG-TODO-039 已完成。

1. `RuntimeCognitionLoopSmokeTest` 现在不仅已注册，而且已被统一 unit 聚合真实纳入并执行。
2. `RuntimeControlPlaneSurfaceTest` 使用最小 unary fixture 时，不再因 canonical stage route 缺失而导致 runtime init 失败。
3. 039 修复未改变 cognition / runtime 的生产职责边界，只补齐了测试聚合与 fixture 合法性。
4. `dasall_unit_tests` 的剩余失败已转移到 access/daemon 既有测试，不再是 039 的控制范围。