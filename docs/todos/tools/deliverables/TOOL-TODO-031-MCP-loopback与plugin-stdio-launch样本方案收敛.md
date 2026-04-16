# TOOL-TODO-031 MCP loopback 与 plugin stdio launch 样本方案收敛

日期：2026-04-16  
任务：TOOL-TODO-031  
状态：已完成

## 1. 本地证据

1. `tools/src/mcp/` 当前只有 `placeholder.cpp`，说明 MCP 子域尚无任何生产实现，032~035 仍缺统一的 test seam 与 launch sample 来源。
2. `tests/integration/tools/` 当前已具备 services / workflow / observability 集成拓扑，但尚无 `ToolMCPFallbackIntegrationTest` 与 `ToolPluginStdioMCPIntegrationTest`，证明 MCP gate 仍处于“设计已分层、实现未起步”状态。
3. `tests/mocks/include/CapabilityServicesLoopbackFixture.h` 已提供 header-only loopback fixture 的仓内先例，因此 031 的正确收敛方向是复用 `tests/mocks/include/` 作为 MCP loopback fixture 落点，而不是在 `tools/src/mcp/` 先写测试专用生产类。
4. `tools/src/bridge/PluginExtensionBridge.h` 中 `bridge::MCPServerLaunchSpec` 当前只有 `provider_ref`、`source_key`、`server_id`、`launch_spec_ref`、`trust_level`，说明 launch sample 必须围绕 `launch_spec_ref` 定义解析 shape，而不应在 031 回滚 010 已冻结的 bridge ABI。
5. `docs/architecture/DASALL_tools子系统详细设计.md` 的 11.1 blocker 表此前把 MCP fixture 记为 `TOOL-BLK-003`，而 `docs/todos/tools/DASALL_tools子系统专项TODO.md` 已将 `TOOL-BLK-003` 用于 integration discoverability，这会直接导致 031~036 的依赖锚点混乱，必须在本轮统一。

## 2. 外部参考

1. MCP 官方 transports 规范（2025-03-26）要求 stdio transport 由 client 拉起子进程，所有 JSON-RPC message 使用 UTF-8、newline-delimited framing，server 不得向 stdout 写非 MCP message；这决定了 loopback fixture 必须模拟 newline-delimited stdio transcript，而不是任意内存对象互调。
2. MCP 官方 lifecycle 规范（2025-03-26）要求 `initialize` 必须是首个且非 batch 请求，server 回 `initialize` 后 client 必须发送 `notifications/initialized`，并在 stdio shutdown 时按 close stdin -> wait -> SIGTERM / SIGKILL 的顺序收尾；这决定了 031 必须把 initialize/list/call/shutdown 都纳入最小闭环，而不是只模拟单次 tool call。

## 3. 主结论

1. loopback fixture 的 canonical 落点固定为 `tests/mocks/include/MCPLoopbackServerFixture.h`，采用 header-only 形式，复用仓内 `CapabilityServicesLoopbackFixture` 的夹具组织方式。
2. fixture 的最小脚本化闭环固定为五段：
   - `initialize` request -> response
   - `notifications/initialized`
   - `tools/list`
   - `tools/call`
   - shutdown evidence
3. fixture 不硬编码单一路径，而是暴露 scenario supporting object，用于编排成功、handshake failure、transport close、stale snapshot 等集成前置事实。
4. plugin stdio launch 样本不在 031 里变更 bridge ABI，而是约定由 `launch_spec_ref` 指向一个 plugin-local、可重放、不可联网的 loopback sample payload；首版样本至少覆盖 `command`、`args`、`env`、`working_dir`、`protocol_version`、`server_capabilities`、`tool_bindings`、`healthcheck_mode`。
5. blocker 编号在本轮统一为：
   - `TOOL-BLK-003`：integration discoverability（已由 024 解阻）
   - `TOOL-BLK-004`：MCP loopback / plugin stdio launch 方案（已由 031 解阻）
   - `TOOL-BLK-005`：skill 样本
   - `TOOL-BLK-006`：CMake Tools 预设可见性（仅保留在详设）
6. 031 只解开 design blocker，不把 generic MCP ready 写成已完成事实；在 032~035 真正落地前，仍保持 `tools_mcp=false` 的 builtin/workflow rollout 结论。

## 4. Design -> Build 映射

| 设计项 | Build 落点 | 验收出口 |
|---|---|---|
| `MCPLoopbackServerFixture` 放置约定与 transcript 规则 | `tests/mocks/include/MCPLoopbackServerFixture.h`（032~035 期间落盘） | `ToolMCPFallbackIntegrationTest.cpp`、`ToolPluginStdioMCPIntegrationTest.cpp` |
| `launch_spec_ref` 规范样本 shape | `tools/src/mcp/StdioMCPServerLauncher.cpp` 解析路径 | `ToolPluginStdioMCPIntegrationTest.cpp` |
| initialize/list/call/shutdown 最小闭环 | `IMCPAdapter`、`StdioMCPTransport`、`MCPLane` | `MCPAdapterTest.cpp`、`ToolMCPFallbackIntegrationTest.cpp` |
| stale snapshot / fallback 前置事实 | `CapabilityCache.cpp`、`CapabilityDiscovery.cpp` | `CapabilityCacheTest.cpp`、`ToolMCPFallbackIntegrationTest.cpp` |

## 5. D Gate 结果

1. Design Gate：PASS。
2. 进入 Build 的条件已满足：
   - fixture 放置路径明确；
   - launch sample 来源明确；
   - blocker 编号已统一；
   - 032~035 的代码目标、测试目标和集成出口已能从单一方案追溯。

## 6. 验证

1. 采用命令：
   - `rg -n "loopback|stdio|MCPServerLaunchSpec|CapabilityDiscovery|ToolMCPFallbackIntegrationTest" docs/architecture/DASALL_tools子系统详细设计.md docs/todos/tools/DASALL_tools子系统专项TODO.md`
2. 结果摘要：
   - 命中 `MCPServerLaunchSpec`、`ToolMCPFallbackIntegrationTest`、`ToolPluginStdioMCPIntegrationTest`、8.3 blocker 表与 11.1 blocker 表。
   - 架构详设与专项 TODO 中的 blocker 编号、fixture 放置约定和 launch sample 方案保持一致。

## 7. 风险与回退

1. 031 只冻结样本方案，不创建真实 loopback server 二进制；若 033/034 需要超出当前 shape 的 transport 行为，应先回到设计层补 supporting object，而不是在实现轮次中偷偷扩张协议语义。
2. `launch_spec_ref` 目前仍是 opaque ref；如果后续 `StdioMCPServerLauncher` 在实现中需要直接消费 command/args/env，必须通过解析 sample payload 解决，而不是修改 010 已收敛的 `PluginExtensionBridge` 输出结构。
3. 官方 MCP 规范仍在演进；当前方案锚定 2025-03-26 版 stdio / lifecycle 行为，若后续仓库决定切到更新协议版本，应在 033 之前重新检查 initialize / shutdown 约束。