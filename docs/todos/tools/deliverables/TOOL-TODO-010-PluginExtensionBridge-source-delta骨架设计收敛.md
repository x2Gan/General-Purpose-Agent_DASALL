# TOOL-TODO-010 PluginExtensionBridge source delta 骨架设计收敛

日期：2026-04-16  
任务：TOOL-TODO-010  
状态：D Gate PASS

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-010 的验收条件明确要求：PluginExtensionBridge 必须提供 `on_plugin_loaded()`、`on_plugin_unloaded()`、`emit_builtin_delta()`、`emit_mcp_delta()`、`emit_skill_delta()`，并且 plugin unload 时 source-owned builtin / launch spec / skill asset 能被自动撤销。
2. docs/architecture/DASALL_tools子系统详细设计.md 6.12.4 把 PluginExtensionBridge 定义为 plugin 动态扩展的 source delta 收口层，要求只消费 active plugin set / export table 的公共结果，不能把 plugin load success 直接当成 capability visible。
3. 同一设计文档 1285 行之后对动态变更组件给出了明确并发约束：写路径串行、读取走 snapshot-and-swap 的一致性模型、跨组件 lock order 固定、且不持锁执行 I/O。本轮 bridge skeleton 只落内存内 delta 发布，不提前接 network / process 层。
4. TOOL-TODO-007 已冻结 `ToolPluginExtensionCatalog`、`ToolPluginProviderRef`、builtin / stdio MCP / skill bundle 三类载荷接口，因此 010 只能在 tools 内部把这些已存在对象归一化为 bridge snapshot，不扩展 shared contracts，也不回头修改 plugin public ABI。
5. TOOL-TODO-009 已落地 ToolRegistry snapshot 模型，本轮 bridge 继续沿用同样的 `shared_ptr<const Snapshot>` 发布方式，为后续把 builtin delta 接到 registry、把 launch spec 接到 discovery、把 skill asset 接到 registry/importer 留出稳定消费面。

## 2. Design 结论

1. PluginExtensionBridge 采用独立 `PluginExtensionSnapshot` 保存三类 source-owned delta：builtin provider view、MCP launch spec、skill asset ref。读路径只读取不可变 snapshot，写路径通过 `write_mutex_` 串行化。
2. `rebuild_extension_catalog()` 负责对单个 plugin catalog 做 fail-closed 校验：所有 export 必须来自同一 plugin id、payload kind 与实际 payload 集合一致、provider ref / handle / launch spec / bundle 标识必须完整，且同一 plugin 内的 server_id、bundle_id 不能重复。
3. `on_plugin_loaded()` 把单个 plugin 的三类 delta 作为一个 source-scoped 批次整体发布；reader 只能看到“全部已发布”或“全部未发布”，不会读到半更新状态。
4. `on_plugin_unloaded()` 只根据 plugin source key 撤销该 source 的 builtin / MCP / skill delta，不影响其它 source，满足 plugin safe mode / unload 时立即撤回 source-owned 扩展的设计要求。
5. 单测继续保持仓库现有的 lightweight executable 风格，不引入额外测试框架：`PluginExtensionBridgeTest.cpp` 覆盖 source reconcile、unload revoke 与 fail-closed；`PluginExtensionBridgeConcurrencyTest.cpp` 验证串行写与 snapshot 读一致性。

## 3. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| PluginExtensionBridge snapshot delta 骨架 | tools/src/bridge/PluginExtensionBridge.h；tools/src/bridge/PluginExtensionBridge.cpp |
| bridge 行为与并发单测 | tests/unit/tools/PluginExtensionBridgeTest.cpp；tests/unit/tools/PluginExtensionBridgeConcurrencyTest.cpp |
| tools unit discoverability 接线 | tools/CMakeLists.txt；tests/unit/tools/CMakeLists.txt；tests/unit/CMakeLists.txt |

## 4. Build 三件套

1. 代码目标：让 `dasall_tools` 编译真实 PluginExtensionBridge 源文件，并移除 bridge placeholder 编译入口。
2. 测试目标：通过 `PluginExtensionBridgeTest` 断言 plugin load/unload 的 source-scoped publish / revoke 与 fail-closed 行为，通过 `PluginExtensionBridgeConcurrencyTest` 验证并发读下不会观察到半发布 delta。
3. 验收命令：
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`
   - 关注 `PluginExtensionBridgeTest`、`PluginExtensionBridgeConcurrencyTest` 进入 unit 集合并保持全绿

## 5. 风险与回退

1. 当前 010 仍是 source delta skeleton，没有直接把 builtin provider 转成 ToolRegistry descriptor，也没有把 launch spec / skill asset 送入 CapabilityDiscovery / SkillRegistry；后续任务必须复用本轮 source key 与 snapshot 模型，而不是旁路新增第二套 plugin delta 容器。
2. `on_plugin_loaded()` 当前要求 payload kind 与 payload 集合显式一致，保持 fail-closed；如果未来 plugin public ABI 允许更宽松的省略表达，应先回到 007 的接口设计层评审，而不是在 bridge 内部静默推断。
3. 并发模型依赖整批 source delta 一次性发布；后续若 bridge 引入跨组件级联写入，必须继续遵守设计文档给出的固定 lock order，避免 bridge -> registry -> cache 逆序持锁。