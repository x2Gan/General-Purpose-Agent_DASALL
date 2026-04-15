# TOOL-TODO-006 IMCPAdapter 与 IMCPTransport 接口设计收敛

日期：2026-04-15  
任务：TOOL-TODO-006  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 6.6 已给出 `IMCPAdapter::ensure_session()`、`list_capabilities()`、`invoke()` 的建议签名，并把该接口定位为 tools/include/mcp 下的 module public ABI。
2. 同一设计文档 6.12.3 明确 `IMCPTransport` 的首版公共面为 `connect()`、`send()`、`receive()`、`close()`、`is_connected()`，其中 transport 只负责 raw JSON-RPC message 的收发，不解释 initialize、tools/list、invoke 等 MCP 协议语义。
3. docs/architecture/DASALL_tools子系统详细设计.md 6.12.3 与对象分层表同时要求：`MCPServerSpec` 描述 server endpoint / launch source / trust / healthcheck，`MCPToolBinding` 与 `MCPServerSession` 承接 remote capability 映射和 session 上下文；这些对象都保持在 tools/mcp 模块边界内，不进入 shared contracts。
4. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-006 的验收条件要求 adapter / transport 接口头文件可编译，且 transport 只负责 raw JSON-RPC，不解释协议语义。
5. docs/architecture/DASALL_tools子系统详细设计.md 还明确 stdio/SSE/Streamable HTTP 只是 transport 变体，后续新增 transport 实现时不应反向污染 IMCPAdapter 或上游 ToolManager 接口。

## 2. 外部参考

1. MCP Architecture 文档把 MCP 描述为 host 通过每个 server 对应的 client 建立连接的 client-server 架构，并区分 data layer 与 transport layer：transport layer 负责 connection establishment、message framing 和 message exchange，协议生命周期与能力协商属于 data layer。这支持本任务把 `IMCPTransport` 固定为原始 JSON-RPC 收发边界，而把握手、能力发现和 invoke 语义保留在 `IMCPAdapter`。

## 3. Design 结论

1. `IMCPTransport` 首版冻结为纯 transport 边界：消费 `MCPServerSpec` 建连，发送 `std::string_view` JSON-RPC 报文，按超时接收 `std::optional<std::string>` 报文，并暴露 `close()` 与 `is_connected()`；不把 protocol method、tool binding 或 capability 解析下沉到 transport。
2. `MCPServerSpec` 在 transport 头中收口为 module-public server 描述：`server_id`、`transport_kind`、`endpoint_ref`、declared capabilities、trust level、healthcheck reference；plugin-delivered command/args/env/cwd 继续留给内部 `MCPServerLaunchSpec` 处理。
3. `TransportConnectResult` 只表达连接建立结果、connection id 与可选 `ErrorInfo`，用于上层 adapter 做 retry / evidence / protocol handshake 决策。
4. `IMCPAdapter` 首版冻结为协议语义边界：确保 session、列能力、执行 invoke；它在 transport 之上实现 initialize/handshake、capability discovery、protocol error mapping，但不承担最终 route/policy 决策。
5. `MCPServerSession` 与 `MCPToolBinding` 保持为 tools/mcp module-local supporting object，用于承接 session 上下文和 remote capability 到 internal tool 的映射，不推进 shared contracts。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 transport raw JSON-RPC 接口 | tools/include/mcp/IMCPTransport.h |
| 冻结 adapter 协议语义接口 | tools/include/mcp/IMCPAdapter.h |
| 增加 MCP interface surface 证据 | tests/unit/tools/MCPInterfaceSurfaceTest.cpp |

## 5. Build 三件套

1. 代码目标：把 tools/include/mcp/IMCPAdapter.h 与 tools/include/mcp/IMCPTransport.h 从壳文件替换为真实接口签名和最小 supporting type。
2. 测试目标：新增 tests/unit/tools/MCPInterfaceSurfaceTest.cpp，以方法指针类型断言和样例初始化锁定 transport / adapter 边界，同时继续保持该测试源未接入 CMake，留待 TOOL-TODO-008 统一纳管。
3. 验收命令：
   - `c++ -std=c++20 -I/home/gangan/DASALL/tools/include -I/home/gangan/DASALL/contracts/include -x c++ -fsyntax-only /home/gangan/DASALL/tests/unit/tools/MCPInterfaceSurfaceTest.cpp`
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`
   - `dasall_unit_tests` 目标构建期间自动执行当前 unit 集合，并以构建期 gate 结果作为本轮验收依据

## 6. 风险与回退

1. 当前 `MCPServerSpec.endpoint_ref` 采用抽象引用而不是直接暴露 stdio command/args/env/cwd，以避免把 `MCPServerLaunchSpec` 的内部实现细节冻结到 module-public ABI；若后续 transport selector 需要更多字段，应先评估兼容追加而不是回退到内部 launch spec。
2. `MCPServerSession` 当前只保留 session ref、transport kind、protocol version 与 connection id；后续若需要 health/backoff 元数据，应继续由实现层或内部对象扩展，不应把 capability cache 事实混入 session 公共面。
3. 若后续实现验证表明 `TransportConnectResult` 需要更强的 retry hint 或 transport metadata，应在保持 `connect()` 主签名不变的前提下追加字段。