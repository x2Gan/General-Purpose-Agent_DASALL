# TOOL-TODO-034 CapabilityDiscovery 与 StdioMCPServerLauncher 实现

日期：2026-04-16  
任务：TOOL-TODO-034  
状态：已完成

## 1. 目标

1. 将 031 冻结的 plugin `launch_spec_ref` 样本真正接入 tools MCP runtime，而不是继续让 `MCPServerLaunchSpec` 停留在 opaque ref 状态。
2. 把 032 的 `CapabilityCache` 与 033 的 `MCPAdapter` / `StdioMCPTransport` / `MCPLane` 接成一条可验证的 discovery 链，覆盖 plugin delta、capability refresh、failure backoff 和 stale snapshot 保留。
3. 保持 034 的职责边界清晰：launcher 只做 sample 解析与 transport factory 接线，discovery 只做 source-scoped server 管理和 refresh，不越权替代 RouteSelector 或 ToolManager。

## 2. 实现落点

1. 新增 `tools/src/mcp/StdioMCPServerLauncher.h` 与 `tools/src/mcp/StdioMCPServerLauncher.cpp`：
   - 定义 `StdioMCPLaunchSample` 与 `StdioLaunchBindingTemplate`，收口 031 规定的 launch sample 解析结果；
   - `build_server_spec()` 把 plugin bridge 的 `launch_spec_ref` 解析为 `MCPServerSpec`；
   - `build_bindings()` 把 sample 内部 `tool_bindings` 解析为 `MCPToolBinding`；
   - `build_transport_factory()` 把 sample resolver + channel builder 包装为可交给 `MCPAdapter` 的 stdio transport factory。
2. 新增 `tools/src/mcp/CapabilityDiscovery.h` 与 `tools/src/mcp/CapabilityDiscovery.cpp`：
   - `on_plugin_delta()` 以 source-scoped 方式接收 plugin 提供的 launch spec 批次，并发布可解析的 server spec snapshot；
   - `schedule_refresh()` 只枚举当前到期 server，不持锁执行任何 adapter I/O；
   - `refresh_once()` 对每个到期 server 独立执行 `ensure_session()` + `list_capabilities()`；
   - 成功路径写入 `CapabilityCache::update()`、设置下次 refresh 时间，并向 `ToolRegistry` 发布 source-scoped MCP bindings；
   - 失败路径写入 `CapabilityCache::mark_failed()`，并按 `failure_backoff_ms` 推迟该 server 的下一次 refresh。
3. 更新 `tools/CMakeLists.txt`，把 `CapabilityDiscovery.cpp` 与 `StdioMCPServerLauncher.cpp` 纳入 `dasall_tools`。

## 3. 关键设计结论

1. `launch_spec_ref` 仍保持 plugin bridge 输出的 opaque ref 形态；034 通过 sample resolver 在 launcher 内部解析，不修改 010/031 已冻结的 bridge ABI。
2. discovery 不维护第二套 freshness 状态机；TTL、stale/expired 分类继续由 032 的 `CapabilityCache` 统一负责，034 只补 refresh 调度与失败退避。
3. discovery 采用 source-scoped replace/revoke 模型：plugin delta 到来时先替换 source 自有 server 视图，再在成功 refresh 后发布该 source 的 MCP bindings，符合 010 的 source ownership 约束。
4. launcher 只把 sample 转成 server spec / bindings / transport factory；它不做 session 管理、route 决策或 cache policy，从而保持与 033 的 adapter/lane 边界一致。

## 4. 测试覆盖

1. 新增 `tests/unit/tools/StdioMCPServerLauncherTest.cpp`：
   - 验证 `launch_spec_ref` 能被解析为 `MCPServerSpec` 与 `MCPToolBinding`；
   - 验证 launcher 暴露的 stdio transport factory 能通过注入 channel builder 建立连接。
2. 新增 `tests/unit/tools/CapabilityDiscoveryTest.cpp`：
   - 验证 plugin delta 会让新 server 立即进入 refresh schedule；
   - 验证成功 refresh 后 capability cache 与 registry bindings 都会更新；
   - 验证失败 refresh 会应用 `failure_backoff_ms`，并保留已有 trusted snapshot 为 stale。
3. 更新 `tests/unit/tools/CMakeLists.txt` 与 `tests/unit/CMakeLists.txt`，把 `dasall_stdio_mcp_server_launcher_unit_test`、`dasall_capability_discovery_unit_test` 注册并接入聚合 unit target。

## 5. 验证

1. 构建：
   - `Build_CMakeTools` targets: `dasall_tools`, `dasall_unit_tests`
2. 定向测试：
   - `RunCtest_CMakeTools` tests: `CapabilityDiscoveryTest`, `StdioMCPServerLauncherTest`
3. 结果：
   - `dasall_tools` 与 `dasall_unit_tests` 构建通过；
   - `CapabilityDiscoveryTest`、`StdioMCPServerLauncherTest` 全部通过；
   - `Build_CMakeTools` 的 unit 聚合构建也已包含新增测试目标；
   - `DartConfiguration.tcl` 历史噪声仍存在，但不影响通过结论。

## 6. 对后续任务的影响

1. 035 可以直接复用 `CapabilityDiscovery::resolve_server_spec()` 作为 `MCPLane` 的 server spec 来源，不需要再临时拼装一套 plugin->server resolver。
2. 035 的 fallback integration gate 可以直接利用 discovery 的 stale snapshot 保留语义，而不是在集成测试里重新伪造 cache 行为。
3. plugin stdio integration gate 现在只剩 loopback fixture + ToolManager/RouteSelector 闭环接线，不再缺失 launcher/discovery runtime 底座。

## 7. 风险与后续

1. 当前 launch sample 仍依赖受控 resolver 注入，没有引入真实插件资产装载；这符合 034 只做 runtime 接线、不做资产系统扩张的边界。
2. `CapabilityDiscovery` 目前只发布 bindings 与 cache，不替代 RouteSelector 做最终 fallback 决策；route fallback 仍需由 035 的集成门验证。
3. generic MCP 仍不能对外宣称 ready；在 `ToolMCPFallbackIntegrationTest` 与 `ToolPluginStdioMCPIntegrationTest` 落地前，`tools_mcp=false` 的 rollout 结论不变。