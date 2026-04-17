# TOOL-TODO-041 ToolProfileIntegration 与 tools discoverability Gate 收敛

日期：2026-04-17
任务：TOOL-TODO-041
状态：已完成

## 1. 目标

1. 为 tools 子系统补齐 desktop_full 与 edge_minimal 的 profile compatibility integration gate，避免 timeout、allowed domains、visibility 与 stale-read 策略漂移只停留在 unit 层断言。
2. 用 build-ci 下的 `ctest -N` 显式回收 tools unit / integration discoverability 证据，完成 Gate-TOOL-09 与 Gate-TOOL-10 的实现侧闭环。
3. 保持 041 的边界清晰：只补 `ToolProfileIntegrationTest` 与最小 CMake 注册，不扩张 runtime policy schema，不改写既有 ToolConfigAdapter / ToolPolicyGate 生产语义。

## 2. 实现落点

1. 新增 `tests/integration/tools/ToolProfileIntegrationTest.cpp`：
   - 构造 `desktop_full` 与 `edge_minimal` 两组 `RuntimePolicySnapshot` + `BuildProfileManifest`；
   - 断言 builtin / mcp / workflow timeout、`max_tool_calls`、`stale_read_allowed`、`allowed_tool_domains`、`tool_visibility_rules` 的投影差异；
   - 通过 `ToolPolicyGate` 进一步断言 profile 差异会改变 mcp domain gate 与 builtin visibility gate 的准入结果。
2. 更新 `tests/integration/tools/CMakeLists.txt`，新增 `dasall_tool_profile_integration_test`，将 `ToolProfileIntegrationTest` 接入 tools integration 聚合目标。
3. 更新 tools 详设当前基线段落，明确 Gate-TOOL-09 与 Gate-TOOL-10 已由自动化测试和 discoverability 命令覆盖。

## 3. 关键设计结论

1. `ToolProfileIntegrationTest` 证明 profile 差异不是静态字段比较，而是会真实影响 `ToolPolicyGate` 的 admission decision：desktop_full 允许 trusted mcp route，edge_minimal 对同一请求返回 `policy.domain_denied`。
2. edge_minimal 的 builtin visibility 被收敛到 `builtin:essential` 后，非 essential builtin 请求会稳定命中 `policy.visibility_denied`，从而避免 visibility 规则只停留在配置值存在性校验。
3. `ctest -N` 的 discoverability 证据继续采用 build-ci 生成器；根据官方 CTest 手册，`-N` / `--show-only` 只列出会执行的测试而不实际运行测试，适合作为 Gate-TOOL-10 的稳定门禁依据。

## 4. 测试覆盖

1. 新增 `tests/integration/tools/ToolProfileIntegrationTest.cpp`：
   - 正例：desktop_full 保留更宽的 timeout budget、mcp lane、allowed domains 和 visibility；
   - 负例：edge_minimal 对 trusted mcp request 返回 `policy.domain_denied`，对 non-essential builtin request 返回 `policy.visibility_denied`。
2. discoverability 验证：
   - `ctest --test-dir build-ci -N | rg "ToolInvocationContextSurfaceTest|ToolProfileIntegrationTest|ToolPluginSkillBundleIntegrationTest|ToolServicesSmokeIntegrationTest"`
3. CMake Tools 辅助验证：
   - `Build_CMakeTools`
   - `RunCtest_CMakeTools` tests: `ToolProfileIntegrationTest`

## 5. 验证

1. build-ci acceptance：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_tool_profile_integration_test`
   - `ctest --test-dir build-ci --output-on-failure -L integration -R ToolProfileIntegrationTest`
   - `ctest --test-dir build-ci -N | rg "ToolInvocationContextSurfaceTest|ToolProfileIntegrationTest|ToolPluginSkillBundleIntegrationTest|ToolServicesSmokeIntegrationTest"`
2. CMake Tools 辅助：
   - `Build_CMakeTools`
   - `RunCtest_CMakeTools` tests: `ToolProfileIntegrationTest`
3. 结果：
   - `dasall_tool_profile_integration_test` 构建通过；
   - `ToolProfileIntegrationTest` 在 build-ci 与 CMake Tools 两条路径下均通过；
   - build-ci `ctest -N` 可同时发现 tools unit 与 integration 入口；
   - 历史 `DartConfiguration.tcl` 噪声仅出现在 CMake Tools 测试输出，不影响 041 通过结论。

## 6. 对后续任务的影响

1. Gate-TOOL-09 与 Gate-TOOL-10 的实现侧证据已经齐备，042 可以专注于 tools 专项 Gate 与交付证据的总回写，而不需要再补测试基座。
2. 后续如果 profile 策略继续演进，应优先扩 `ToolProfileIntegrationTest` 的黑盒断言，而不是把 profile 差异回退到单纯的 unit 字段比较。

## 7. 风险与后续

1. 当前 discoverability gate 仍以 build-ci 为正式依据，VS Code CMake Tools 的 Ninja 构建目录只作为开发期辅助，不应替代正式 Gate 结论。
2. build-ci 单目标构建过程中暴露的 `ToolMetricsBridge.cpp` 未用函数告警是既有问题，不属于 041 的改动范围；若后续要做 warning-clean 收口，应单独立任务处理。
