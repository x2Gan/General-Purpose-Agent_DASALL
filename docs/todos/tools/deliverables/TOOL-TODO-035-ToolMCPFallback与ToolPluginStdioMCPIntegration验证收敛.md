# TOOL-TODO-035 ToolMCPFallback 与 ToolPluginStdioMCPIntegration 验证收敛

日期：2026-04-16  
任务：TOOL-TODO-035  
状态：已完成

## 1. 目标

1. 为 032~034 已落地的 `CapabilityCache`、`MCPAdapter`、`MCPLane`、`CapabilityDiscovery`、`StdioMCPServerLauncher` 补齐 integration gate，而不是停留在 unit-only 验证。
2. 验证 ToolManager 在 profile 策略约束下，能对 MCP hybrid 路径做出可解释的 route 选择，覆盖 stale snapshot、builtin fallback 与统一 ErrorInfo 映射。
3. 验证 plugin-delivered stdio MCP 路径从 `PluginExtensionBridge` 的 `launch_spec_ref` 到 loopback invoke / unload revoke 的端到端闭环，同时仍保持 “generic MCP ready 不自动对外宣称” 的边界。

## 2. 实现落点

1. 新增 `tests/mocks/include/MCPLoopbackServerFixture.h`：
   - 定义 `MCPLoopbackScenario`、`MCPLoopbackFrame`、`MCPLoopbackExitMode`；
   - 提供可注入 `StdioMCPServerLauncher` 的 deterministic stdio channel builder；
   - 记录 open/write/close 轨迹，便于在 integration test 中断言 MCP transcript 是否按预期执行。
2. 新增 `tests/integration/tools/ToolMCPFallbackIntegrationTest.cpp`：
   - 通过 `ToolManager` + `CapabilityDiscovery` + `MCPLane` + `BuiltinExecutorLane` 串起真实路由闭环；
   - 验证 trusted stale snapshot 仍可选择 MCP；
   - 验证 MCP lane 不健康时自动回退 builtin；
   - 验证 MCP protocol error 会以统一 `ErrorInfo` / `failure_reason_code` 形式透出。
3. 新增 `tests/integration/tools/ToolPluginStdioMCPIntegrationTest.cpp`：
   - 经 `PluginExtensionBridge` 发布 plugin stdio launch spec；
   - 通过 `CapabilityDiscovery` 刷新 cache 与 binding；
   - 验证 `launch_spec_ref` -> launch sample -> stdio invoke 的闭环；
   - 验证 plugin unload 后，server spec、registry binding、capability cache 均按 source-scoped 语义撤销。
4. 更新 `tests/integration/tools/CMakeLists.txt`，把 `dasall_tool_mcp_fallback_integration_test` 与 `dasall_tool_plugin_stdio_mcp_integration_test` 接入 tools integration 注册表。

## 3. 关键结论

1. `CapabilityCache` 的 stale snapshot 语义已在 ToolManager 黑盒路径上被真实消费，而不是只在 RouteSelector 单测里成立。
2. builtin fallback 发生在 route selector / lane health 层，而不是通过 MCP 失败后再做二次补救，符合 6.12.3 的 route ownership 边界。
3. `PluginExtensionBridge` 导出的 `launch_spec_ref` 已能经 `StdioMCPServerLauncher` 与 `CapabilityDiscovery` 形成可执行的 stdio MCP 闭环，且 unload revoke 能正确清理 registry 与 cache。
4. 035 完成的是 MCP hybrid integration gate，不是 generic MCP rollout 承诺；是否从 `tools_mcp=false` 进入灰度开启，仍需单独评审。

## 4. 测试覆盖

1. `ToolMCPFallbackIntegrationTest`：
   - trusted stale snapshot 仍选中 MCP 路径；
   - MCP lane unhealthy 时切换 builtin；
   - MCP protocol error 映射为统一 `ErrorInfo`。
2. `ToolPluginStdioMCPIntegrationTest`：
   - plugin `launch_spec_ref` 驱动 stdio invoke；
   - loopback transcript 覆盖 initialize / initialized / tools/list / tools/call；
   - unload 后 source-scoped revoke 清空 server spec / binding / cache。

## 5. 验证

1. 构建：
   - `Build_CMakeTools`
2. 定向测试：
   - `RunCtest_CMakeTools` tests: `ToolMCPFallbackIntegrationTest`, `ToolPluginStdioMCPIntegrationTest`
3. 结果：
   - 新增两个 tools integration targets 构建通过；
   - `ToolMCPFallbackIntegrationTest`、`ToolPluginStdioMCPIntegrationTest` 全部通过；
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，不影响通过结论。

## 6. 影响

1. `TOOL-D9` 的 unit + integration gate 现已闭环，MCP hybrid 路径具备可重复验证的自动化事实。
2. 041 可直接复用本次 integration harness，继续验证 profile compatibility 与 discoverability，不需要再重造 MCP loopback seam。
3. 后续如需要做 rollout 评审，可直接以本次 Gate-TOOL-07 自动化证据为基础，而不必重新证明 fallback / plugin stdio invoke 的基本正确性。