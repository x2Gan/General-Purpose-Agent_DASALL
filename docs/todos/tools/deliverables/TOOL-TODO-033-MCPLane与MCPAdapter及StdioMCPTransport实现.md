# TOOL-TODO-033 MCPLane 与 MCPAdapter 及 StdioMCPTransport 实现

日期：2026-04-16  
任务：TOOL-TODO-033  
状态：已完成

## 1. 目标

1. 将 `IMCPTransport`、`IMCPAdapter`、`MCPLane` 从 interface-only / design-only 状态推进到可执行的内部实现。
2. 在 034 之前先把“transport 只管 raw JSON-RPC、adapter 只管协议语义、lane 只管 binding/session 组装”三层职责固定下来，避免 discovery / launcher 进来后继续缠在一起。
3. 为 035 的 MCP fallback / plugin stdio integration gate 先打好本地单测底座，特别是握手失败、transport switch 与协议错误映射。

## 2. 实现落点

1. 新增 `tools/src/mcp/StdioMCPTransport.h` 与 `tools/src/mcp/StdioMCPTransport.cpp`：
   - `StdioMCPTransport` 实现 `IMCPTransport`；
   - 通过 `IStdioTransportChannel` + `StdioTransportChannelFactory` 注入底层 channel；
   - `connect()` / `send()` / `receive()` / `close()` / `is_connected()` 全部只处理 raw JSON-RPC 通道，不解释 initialize / tools/list / tools/call 协议语义。
2. 新增 `tools/src/mcp/MCPAdapter.h` 与 `tools/src/mcp/MCPAdapter.cpp`：
   - 按 transport kind 选择具体 transport；
   - 对同一 server 做 session 缓存；
   - `ensure_session()` 执行 initialize -> initialized 最小握手；
   - `list_capabilities()` 把 tools/list 响应投影为 `CapabilitySnapshot`；
   - `invoke()` 把 tools/call 响应映射为 `ToolResult`；
   - 协议错误统一映射为稳定 `ToolResult.error`，timeout 统一映射到 `ProviderTimeout`。
3. 新增 `tools/src/mcp/MCPLane.h` 与 `tools/src/mcp/MCPLane.cpp`：
   - 从 `ToolRegistry` 解析 `MCPToolBinding`；
   - 通过 `server_spec_resolver` 查找 `MCPServerSpec`；
   - 调用 `IMCPAdapter` 准备 session 并执行 invoke；
   - fail-closed 处理缺失 binding / 缺失 session / 缺失 spec。
4. 更新 `tools/CMakeLists.txt`，将 `MCPAdapter.cpp`、`MCPLane.cpp`、`StdioMCPTransport.cpp` 纳入 `dasall_tools`。

## 3. 关键设计结论

1. `StdioMCPTransport` 本轮不直接实现真实 subprocess launcher；033 只把 stdio message channel 和 lifecycle 承载层落地，034 再把 `launch_spec_ref` 解析与真实 launch 逻辑接进来。这保证 033 不越权侵入 launcher 任务边界。
2. `MCPAdapter` 的默认 stdio transport 仍然可以 fail-closed；真正的成功路径通过单测里的 scripted channel 验证，不依赖 034 之前不存在的 launcher。
3. transport switch 在 adapter 层完成，而不是在 lane 或 route 层绕写：同一 server 若切换 transport kind / endpoint，adapter 会关闭旧连接并建立新 session。
4. `MCPLane` 不消费 cache policy、也不修改路由决策；它只做 binding 解析、server spec 查找和 invoke 转发，符合 033 的分层目标。

## 4. 测试覆盖

1. 新增 `tests/unit/tools/MCPAdapterTest.cpp`：
   - 使用真实 `StdioMCPTransport` + scripted stdio channel 验证 initialize / initialized / tools/list / tools/call 最小闭环。
2. 新增 `tests/unit/tools/MCPAdapterTransportSwitchTest.cpp`：
   - 验证 handshake failure fail-closed；
   - 验证同一 server 的 transport switch 会关闭旧连接；
   - 验证协议错误会被映射为稳定 `ToolResult.error`。
3. 新增 `tests/unit/tools/MCPLaneInvokeTest.cpp`：
   - 验证 lane 能从 registry 解析 binding、从 resolver 解析 server spec，并把请求转发给 adapter；
   - 验证缺失 binding 时 fail-closed。
4. 更新 `tests/unit/tools/CMakeLists.txt` 与 `tests/unit/CMakeLists.txt`，注册并聚合上述 3 个新测试目标。

## 5. 验证

1. 构建：
   - `Build_CMakeTools` targets: `dasall_tools`, `dasall_unit_tests`
2. 定向测试：
   - `RunCtest_CMakeTools` tests: `MCPAdapterTest`, `MCPAdapterTransportSwitchTest`, `MCPLaneInvokeTest`
3. 结果：
   - 构建通过；
   - 3 个定向测试全部通过；
   - `RunCtest_CMakeTools` 仍打印历史 `DartConfiguration.tcl` 噪声，但不影响通过结论。

## 6. 对后续任务的影响

1. 034 可以把 `StdioMCPServerLauncher` 直接接到 `StdioTransportChannelFactory` / transport 选择路径，而不必重写 adapter 会话状态机。
2. 034 的 `CapabilityDiscovery` 可以直接消费 `MCPAdapter::ensure_session()` 与 `list_capabilities()`，并把失败态交给 032 的 `CapabilityCache::mark_failed()`。
3. 035 的 MCP integration gate 已有单测级 transport switch / protocol error mapping 先行证据，集成层只需验证 loopback fixture 与 plugin stdio launch 的闭环，不需要再重新设计错误语义。

## 7. 风险与后续

1. 当前 JSON 解析保持最小实现，只覆盖 031 冻结的 initialize / tools/list / tools/call 测试协议面；若 034 引入更复杂 payload，应优先补内部解析 supporting object，而不是扩大 public ABI。
2. `StdioMCPTransport` 目前依赖注入 channel，不直接拉起进程；真实 subprocess lifecycle 仍由 034 的 launcher 负责接入。
3. 当前 `MCPLane` 尚未把 server spec 解析接入真实 discovery / plugin delta 来源；这不是遗漏，而是有意留给 034 完成 source-of-truth 接线。