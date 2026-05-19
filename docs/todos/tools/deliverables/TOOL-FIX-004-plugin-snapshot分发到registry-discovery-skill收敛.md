# TOOL-FIX-004 plugin snapshot 分发到 registry / discovery / skill 收敛

日期：2026-05-19
来源任务：TOOL-FIX-004
范围：build-tree focused plugin delta 分发闭环；不使用 qemu / kvm 作为本轮收口证据

## 1. 任务重定义

`TOOL-FIX-003` 只闭合了 `infra/plugin lifecycle -> PluginExtensionBridge snapshot`。本轮真实缺口只有一层：同一份 plugin delta 仍未自动进入 `ToolRegistry`、`CapabilityDiscovery` 与 `SkillRegistry`，现有 `ToolPluginStdioMCPIntegrationTest` / `ToolPluginSkillBundleIntegrationTest` 仍依赖 tests 手工读取 bridge snapshot 再做子域 wiring。

因此本轮把 `TOOL-FIX-004` 重定义为：

1. 新增一个窄的 extension catalog consumer，复用 `PluginExtensionBridge::rebuild_extension_catalog()` 的 source-scoped delta 语义，把 builtin provider、stdio MCP、skill bundle 自动分发到 `ToolRegistry`、`CapabilityDiscovery`、`PluginSkillBundleImporter` / `SkillRegistry`。
2. 分发路径必须保持 fail-closed：任一子域应用失败时撤回整个 source，不允许留下 “bridge snapshot 已见、registry/discovery/skill 半可见” 的悬挂状态。
3. `CapabilityDiscovery` 在处理 plugin source refresh / revoke 时只能清理 source-owned MCP bindings，不能误删同 source 的 plugin descriptors；descriptor 与 binding 需要拆成独立的 source revoke seam。
4. authoritative 证据限定为本地 build-tree focused build/test；若 `RunCtest_CMakeTools` 继续返回仓库已知泛化“生成失败”，则按既有仓库口径回退到直接执行对应 test binary。

## 2. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 通过条件 |
|---|---|---|
| plugin delta 需要独立 consumer，而不是继续在 tests 手工读 snapshot | `tools/src/bridge/ToolPluginExtensionConsumer.h`、`tools/src/bridge/ToolPluginExtensionConsumer.cpp` | builtin/MCP/skill 三类 payload 能从同一份 delta 自动下沉到对应子域 |
| ToolRegistry 必须把 descriptor source delta 与 MCP binding source revoke 解耦 | `tools/src/registry/ToolRegistry.h`、`tools/src/registry/ToolRegistry.cpp`、`tests/unit/tools/ToolRegistryTest.cpp` | plugin descriptor 能保留，MCP binding 可单独撤销，full source revoke 仍能同时删除两者 |
| lifecycle adapter 必须把同一份 catalog 同时送入 bridge snapshot 和 downstream consumer | `tools/src/bridge/ToolPluginLifecycleBridge.h`、`tools/src/bridge/ToolPluginLifecycleBridge.cpp`、`tests/integration/tools/ToolPluginLifecycleBridgeIntegrationTest.cpp` | active set / load / unload 既更新 bridge snapshot，也更新 registry / discovery / skill |
| 现有 MCP / skill integration gate 不能再保留手工 wiring | `tests/integration/tools/ToolPluginStdioMCPIntegrationTest.cpp`、`tests/integration/tools/ToolPluginSkillBundleIntegrationTest.cpp` | 两条现有 gate 通过 consumer 自动完成 publish / revoke |
| 三类 payload 需要一条统一黑盒收口 gate | `tests/integration/tools/ToolPluginExtensionEndToEndIntegrationTest.cpp` | builtin provider、stdio MCP、skill bundle 三类 source revoke 可一次性二值断言 |

## 3. 代码落点

### 3.1 source delta consumer 与 lifecycle 接线

1. `tools/src/bridge/ToolPluginExtensionConsumer.h/.cpp`
   - 新增 `ToolPluginExtensionConsumer`，复用 `PluginExtensionBridge::rebuild_extension_catalog()` 生成的 `PluginExtensionDelta`；对 builtin provider 调 `ToolRegistry::apply_plugin_extension_delta()`，对 stdio MCP 调 `CapabilityDiscovery::on_plugin_delta()`，对 skill bundle 走 `PluginSkillBundleImporter` + `SkillRegistry`。
   - consumer 对 skill import 采取 error-level fail-closed：warning 允许 source 保持“不可见但不报错”，error 会撤回整个 source。
   - unload / apply failure 均通过 source-scoped revoke 收口，避免桥接 snapshot 与三个下游子域出现可见性漂移。
2. `tools/src/bridge/ToolPluginLifecycleBridge.h/.cpp`
   - lifecycle adapter 现在可选接入 `ToolPluginExtensionConsumer`；active set sync、load、unload 在维护 `PluginExtensionBridge` snapshot 的同时，也会同步驱动下游 consumer。
   - 若 consumer 失败，adapter 会与 bridge 一起撤回该 source，保持 fail-closed。

### 3.2 registry / discovery 细化

1. `tools/src/registry/ToolRegistry.h/.cpp`
   - 新增 `apply_plugin_extension_delta()`，承接 plugin-delivered descriptor batch。
   - 新增 `revoke_mcp_bindings_for_source()`，把 binding-only revoke 从 full `revoke_source()` 中拆出来。
   - `upsert_mcp_bindings()` 的空 batch 现在只撤销 source-owned MCP bindings，不再删除 plugin descriptors。
2. `tools/src/mcp/CapabilityDiscovery.cpp`
   - plugin delta refresh/revoke 现在调用 `revoke_mcp_bindings_for_source()`，不再通过 `revoke_source()` 误删同 source 的 plugin descriptors。

### 3.3 focused tests

1. `tests/unit/tools/ToolRegistryTest.cpp`
   - 新增 plugin descriptor delta 与 binding-only revoke 隔离场景，证明 descriptor / binding 不再被错误耦合清理。
2. `tests/integration/tools/ToolPluginStdioMCPIntegrationTest.cpp`
   - 由“bridge snapshot -> 手工 discovery.on_plugin_delta()”切换为 `ToolPluginExtensionConsumer::on_plugin_loaded()` / `on_plugin_unloaded()`。
3. `tests/integration/tools/ToolPluginSkillBundleIntegrationTest.cpp`
   - 由“bridge snapshot -> 手工 importer + register/revoke”切换为 `ToolPluginExtensionConsumer` 自动分发。
4. `tests/integration/tools/ToolPluginExtensionEndToEndIntegrationTest.cpp`
   - 新增 active plugin -> lifecycle bridge -> bridge snapshot + registry/discovery/skill 一次性闭环，覆盖 builtin provider、stdio MCP、skill bundle 三类 payload 的 publish / revoke。
5. `tests/integration/tools/CMakeLists.txt`
   - 新增 `dasall_tool_plugin_extension_end_to_end_integration_test` discoverability。

## 4. 验证证据

### 4.1 focused build

| 验证项 | 结果 |
|---|---|
| `Build_CMakeTools(buildTargets=["dasall_tool_registry_unit_test"])` | PASS |
| `Build_CMakeTools(buildTargets=["dasall_tools"])` | PASS |
| `Build_CMakeTools(buildTargets=["dasall_tool_plugin_stdio_mcp_integration_test","dasall_tool_plugin_skill_bundle_integration_test","dasall_tool_plugin_extension_end_to_end_integration_test","dasall_tool_plugin_lifecycle_bridge_integration_test"])` | PASS |

### 4.2 focused test

`RunCtest_CMakeTools(tests=["ToolRegistryTest","ToolPluginStdioMCPIntegrationTest","ToolPluginSkillBundleIntegrationTest","ToolPluginExtensionEndToEndIntegrationTest"])` 与定向 integration `RunCtest_CMakeTools` 当前仍返回仓库已知泛化 `生成失败`，不足以判定测试本身失败，因此按 build-tree binary fallback 复核：

1. `build/vscode-linux-ninja/tests/unit/tools/dasall_tool_registry_unit_test`：exit `0`
2. `build/vscode-linux-ninja/tests/integration/tools/dasall_tool_plugin_stdio_mcp_integration_test`：exit `0`
3. `build/vscode-linux-ninja/tests/integration/tools/dasall_tool_plugin_skill_bundle_integration_test`：exit `0`
4. `build/vscode-linux-ninja/tests/integration/tools/dasall_tool_plugin_extension_end_to_end_integration_test`：exit `0`
5. `build/vscode-linux-ninja/tests/integration/tools/dasall_tool_plugin_lifecycle_bridge_integration_test`：exit `0`

结论：本轮 focused unit / integration gate 全部通过。

## 5. 结论与边界

结论：`TOOL-FIX-004` 现可按“plugin capability 从 active set 到 tools 子域可见性已形成自动闭环”口径判定完成。

已闭合：

1. `ToolPluginLifecycleBridge` 现在不仅自动维护 `PluginExtensionBridge` snapshot，也会同步把同一份 source delta 下沉到 `ToolRegistry`、`CapabilityDiscovery` 与 `SkillRegistry`。
2. `ToolRegistry` 已明确拆分 plugin descriptor delta 与 MCP binding revoke；`CapabilityDiscovery` 不再误删 plugin descriptors。
3. `ToolPluginStdioMCPIntegrationTest`、`ToolPluginSkillBundleIntegrationTest` 不再手工 wiring，`ToolPluginExtensionEndToEndIntegrationTest` 进一步证明 builtin/MCP/skill 三类 payload 能随 lifecycle 自动 publish / revoke。

不外推：

1. 本轮不宣称 lane bulkhead / backpressure / lock-order 压力证据已闭合；那仍属于 `TOOL-FIX-005`。
2. 本轮不宣称 generic MCP transport、installed / qemu / release runner / soak 已完成；验证仍限定在本地 build-tree focused evidence。
3. 本轮没有把 plugin capability visible 直接等同为 plugin load success；visible 仍以 consumer 成功下沉 registry / discovery / skill 的结果为准。