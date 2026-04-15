# TOOL-TODO-009 ToolRegistry 描述符目录骨架设计收敛

日期：2026-04-15  
任务：TOOL-TODO-009  
状态：D Gate PASS

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-009 的验收条件明确要求：`ToolRegistry.cpp`、`BuiltinCatalog.cpp`、`MCPBindingRegistry.cpp` 落盘，并且 descriptor / binding 增删改查、source-scoped revoke、并发读一致性要能通过自动化断言。
2. docs/architecture/DASALL_tools子系统详细设计.md 6.12.1 把 ToolRegistry 收敛为“统一 descriptor / binding 目录”，内部方法包含 `resolve_descriptor()`、`list_descriptors()`、`register_builtin()`、`upsert_mcp_bindings()`、`revoke_source()`，并要求写路径串行、读路径基于不可变 snapshot。
3. 同一设计文档 856 行把本轮推荐测试固定为 `ToolRegistryTest.cpp`、`ToolRegistryConcurrentReadTest.cpp`，验收重点是 descriptor / binding CRUD 与 source-scoped revoke 行为可自动断言。
4. contracts/include/tool/ToolDescriptor.h 与 tests/contract/tool/ToolDescriptorIRContractTest.cpp 已冻结共享 descriptor 语义，因此本轮 registry 只能消费 `ToolDescriptor`，不能擅自扩展 shared contract 或放宽字段校验。
5. TOOL-TODO-008 已把 `tools/src/registry` 目录与 `tests/unit/tools` discoverability 接通，所以 009 的最小收口方案应直接替换 registry placeholder，为后续 PluginExtensionBridge / RouteSelector 提供可复用的内部目录面。

## 2. 外部参考

1. cppreference 对 `std::atomic<std::shared_ptr>` 的说明指出：`load()` 会原子地返回底层 `shared_ptr` 的副本，`store()` / `exchange()` 会原子替换当前指针值。这为 ToolRegistry 采用 “writer 在副本上计算，随后发布新的 `shared_ptr<const Snapshot>`；reader 只读取当前快照副本” 的 snapshot-and-swap 并发模型提供了直接依据。

## 3. Design 结论

1. ToolRegistry 采用内部 `ToolRegistrySnapshot` 作为不可变目录快照，读路径只复制当前 `shared_ptr<const ToolRegistrySnapshot>`，随后在快照上完成 `resolve_descriptor()`、`list_descriptors()`、`list_mcp_bindings()`，避免把可变 store 暴露给上游。
2. 写路径使用单一 `write_mutex_` 串行化；每次注册 builtin、reconcile MCP 绑定或 source revoke 时，先复制当前 snapshot，再在副本上修改并一次性发布，保证 reader 只看到前一版或后一版完整目录，而不会看到半发布状态。
3. `BuiltinCatalog.cpp` 只负责提供静态 builtin descriptor seed；ToolRegistry 通过保留 `builtin.static` source key 来保护 builtin source 不被 `revoke_source()` 误删，从而满足“plugin unload 不得破坏静态 builtin”这一设计约束。
4. `MCPBindingRegistry.cpp` 收口 source-scoped binding reconcile：同一 source 的新批次会整体替换旧批次，不同 source 可以共存；`revoke_source()` 只删除目标 source 的 binding，不波及其它 source。
5. 单测保持当前仓库的 lightweight executable 风格，不引入 gtest：`ToolRegistryTest.cpp` 负责 descriptor / binding CRUD 与 fail-closed 行为，`ToolRegistryConcurrentReadTest.cpp` 负责验证并发 publish 下 snapshot 读一致性。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| ToolRegistry snapshot-and-swap 目录骨架 | tools/src/registry/ToolRegistry.h；tools/src/registry/ToolRegistry.cpp |
| builtin descriptor seed 目录 | tools/src/registry/BuiltinCatalog.h；tools/src/registry/BuiltinCatalog.cpp |
| MCP binding source reconcile / revoke | tools/src/registry/MCPBindingRegistry.h；tools/src/registry/MCPBindingRegistry.cpp |
| registry 行为与并发读单测 | tests/unit/tools/ToolRegistryTest.cpp；tests/unit/tools/ToolRegistryConcurrentReadTest.cpp；tests/unit/tools/CMakeLists.txt；tests/unit/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：让 `dasall_tools` 编译真实 ToolRegistry / BuiltinCatalog / MCPBindingRegistry 源文件，并移除 registry placeholder 依赖。
2. 测试目标：通过 `ToolRegistryTest` 断言 descriptor / binding CRUD、source-scoped revoke 与 fail-closed 行为，通过 `ToolRegistryConcurrentReadTest` 验证 snapshot-and-swap 并发读一致性，并保持 contract baseline 无回退。
3. 验收命令：
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`、`dasall_contract_tests`
   - 关注 `ToolRegistryTest`、`ToolRegistryConcurrentReadTest` 与 `ToolDescriptorIRContractTest` 全绿

## 6. 风险与回退

1. 当前 ToolRegistry 只落 descriptor catalog 与 MCP binding registry 的最小骨架，还没有引入 workflow descriptor、plugin extension delta 或 route selector 消费面；后续任务应复用当前 snapshot 结构，而不是另起第二套目录容器。
2. 009 的并发模型依赖 immutable snapshot copy 语义；后续如果把 `resolve_descriptor()` 改为返回内部引用，必须同时携带 snapshot 生命周期句柄，否则会重新引入悬垂引用风险。
3. `revoke_source()` 当前显式保护 builtin source；若后续新增其它保留 source（例如 profile-seeded workflow catalog），必须沿同一保留源策略扩展，而不能让通用 unload 逻辑直接清空静态目录。 