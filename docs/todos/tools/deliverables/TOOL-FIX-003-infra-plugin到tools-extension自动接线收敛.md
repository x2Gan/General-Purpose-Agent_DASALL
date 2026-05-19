# TOOL-FIX-003 infra/plugin 到 tools extension 自动接线收敛

日期：2026-05-19
来源任务：TOOL-FIX-003
范围：build-tree focused lifecycle adapter 闭环；不使用 qemu / kvm 作为本轮收口证据

## 1. 任务重定义

本轮不把 `TOOL-FIX-003` 扩写成 plugin capability 直接进入 `ToolRegistry`、`CapabilityDiscovery` 或 `SkillRegistry` 的大一统自动分发，也不把 build-tree focused evidence 外推为 installed / release / qemu ready。真实缺口只有一件事：当前 tools 侧虽然已有 `PluginExtensionBridge`，但 bridge 仍只能被测试手工驱动，尚未消费 infra/plugin 的 active set 与 load/unload 边界。

因此本轮把 `TOOL-FIX-003` 重定义为：

1. 新增一个窄的 lifecycle adapter，只依赖 `IPluginManager::list_active()`、`PluginLoadResult.handle_ref` 与 `PluginUnloadResult`，把 plugin 生命周期事件自动送入 `PluginExtensionBridge`。
2. adapter 必须保持 fail-closed：active set 里的 disabled plugin 不可见，invalid catalog 只隔离对应 plugin，safe mode 触发时立即撤回 source-owned delta。
3. 本轮只闭合 “infra/plugin lifecycle -> bridge snapshot” 这一层；snapshot 到 `ToolRegistry` / `CapabilityDiscovery` / `SkillRegistry` 的二次分发继续留给 `TOOL-FIX-004`。
4. authoritative 证据限定为本地 build-tree focused build/test；若 `RunCtest_CMakeTools` 继续返回仓库已知泛化“生成失败”，则按既有仓库口径回退到直接执行对应 test binary。

## 2. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 通过条件 |
|---|---|---|
| infra/plugin 生命周期必须先汇总为 tools 内部 adapter，再驱动 bridge | `tools/src/bridge/ToolPluginLifecycleBridge.h`、`tools/src/bridge/ToolPluginLifecycleBridge.cpp` | adapter 能消费 active set / load / unload，自动调用 `PluginExtensionBridge`，而不是要求调用方手工喂 bridge |
| bridge 必须继续保持 source-scoped snapshot-and-swap 语义 | `tests/unit/tools/PluginExtensionBridgeTest.cpp` | 多 source 同时存在时，单 source revoke 只移除对应 plugin 的 builtin / MCP / skill batch |
| lifecycle adapter 必须保持 fail-closed 与 safe mode revoke | `tests/integration/tools/ToolPluginLifecycleBridgeIntegrationTest.cpp` | active plugin 可见、disabled plugin 不可见、load/unload 生效、safe mode 撤销、invalid catalog 不发生部分发布 |
| focused evidence 不依赖 qemu / kvm | `Build_CMakeTools` + build-tree direct binary fallback | 相关 unit/integration target 构建成功，3 条 focused binaries 退出码均为 `0` |

## 3. 代码落点

### 3.1 lifecycle adapter

1. `tools/src/bridge/ToolPluginLifecycleBridge.h/.cpp`
   - 新增 `ToolPluginLifecycleBridge`，构造输入固定为 `IPluginManager`、`PluginExtensionBridge` 与 `ToolPluginExtensionCatalogResolver`。
   - 提供 `synchronize_active_plugins()`、`on_plugin_loaded()`、`on_plugin_unloaded()` 三个最小入口，分别承接 active set、load boundary 和 unload boundary。
   - active set 只接受 `Loaded` / `Active` plugin 为可见源；`Disabled` source 会被撤回，不会透传为 capability visible。
   - `safe_mode_active=true` 时，adapter 会立即对已追踪 source 与当前 active set 中的 plugin 执行 revoke，保持 fail-closed。
2. `tools/CMakeLists.txt`
   - 将 `ToolPluginLifecycleBridge.cpp` 接入 `dasall_tools`，使 adapter 成为 tools 生产源码的一部分，而不是仅停留在测试夹具。

### 3.2 测试与 discoverability

1. `tests/unit/tools/PluginExtensionBridgeTest.cpp`
   - 扩展多 plugin source 场景，断言撤销 `plugin.alpha` 不会误删 `plugin.beta` 的 builtin / MCP / skill 视图。
2. `tests/integration/tools/ToolPluginLifecycleBridgeIntegrationTest.cpp`
   - 新增 fake plugin manager 与 fake catalog resolver，覆盖 active set 同步、disabled ignore、load/unload publish/revoke、safe mode revoke 与 invalid catalog isolation。
3. `tests/integration/tools/CMakeLists.txt`
   - 新增 `dasall_tool_plugin_lifecycle_bridge_integration_test` target，保证 focused integration test 可被 build/test discoverability 发现。

## 4. 验证证据

### 4.1 focused build

| 验证项 | 结果 |
|---|---|
| `Build_CMakeTools(buildTargets=["dasall_tools"])` | PASS |
| `Build_CMakeTools(buildTargets=["dasall_plugin_extension_bridge_unit_test","dasall_plugin_extension_bridge_concurrency_unit_test","dasall_tool_plugin_lifecycle_bridge_integration_test"])` | PASS |

### 4.2 focused test

`RunCtest_CMakeTools(tests=["PluginExtensionBridgeTest","PluginExtensionBridgeConcurrencyTest","ToolPluginLifecycleBridgeIntegrationTest"])` 当前仍返回仓库已知的泛化 `生成失败`，不足以判定测试本身失败，因此按 build-tree binary fallback 复核：

1. `build/vscode-linux-ninja/tests/unit/tools/dasall_plugin_extension_bridge_unit_test`：exit `0`
2. `build/vscode-linux-ninja/tests/unit/tools/dasall_plugin_extension_bridge_concurrency_unit_test`：exit `0`
3. `build/vscode-linux-ninja/tests/integration/tools/dasall_tool_plugin_lifecycle_bridge_integration_test`：exit `0`

结论：本轮 3 条 focused tests 全部通过。

## 5. 结论与边界

结论：`TOOL-FIX-003` 现可按“infra/plugin lifecycle 已自动驱动 tools bridge snapshot”口径判定完成。

已闭合：

1. tools 生产源码现在真实使用 `IPluginManager` / `ActivePluginSet` / load-unload boundary，不再只有手工调用 `PluginExtensionBridge` 的测试路径。
2. active set、load/unload、safe mode、invalid catalog isolation 已具备 focused build-tree evidence。
3. `PluginExtensionBridge` 的 source-scoped revoke 语义已补强到多 source 共存场景。

不外推：

1. 本轮不宣称 plugin snapshot 已自动进入 `ToolRegistry`、`CapabilityDiscovery` 或 `SkillRegistry`；该二次治理自动分发仍属于 `TOOL-FIX-004`。
2. 本轮不宣称 installed / qemu / release runner / soak 已完成；验证仍限定在本地 build-tree focused evidence。
3. 本轮没有引入新的 shared contracts，也没有把 tools 反向耦合到 infra/plugin 内部状态机或 runtime bridge 私有实现。