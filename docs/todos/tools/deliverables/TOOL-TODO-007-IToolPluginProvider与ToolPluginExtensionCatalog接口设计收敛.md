# TOOL-TODO-007 IToolPluginProvider 与 ToolPluginExtensionCatalog 接口设计收敛

日期：2026-04-15  
任务：TOOL-TODO-007  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 6.5.4 明确 deployment-time extension 统一经 infra/plugin discover/validate/load/unload，tools 只消费 active plugin set 和 export table，不复制 plugin manager 的治理职责。
2. 同一设计文档 6.5.4、6.12.4 明确 plugin 向 tools 暴露的工具域载荷只允许 `builtin_tool_provider`、`mcp_server.stdio`、`skill_bundle` 三类或其组合。
3. docs/architecture/DASALL_tools子系统详细设计.md 6.6 已给出 `IToolPluginProvider::describe_extensions() -> ToolPluginExtensionCatalog` 的建议签名，并把 `ToolPluginExtensionCatalog / ToolPluginProviderRef` 定位为 tools/include/plugin 下的 module public 对象。
4. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-007 的验收条件要求：公共接口只能表达这三类载荷，且 plugin 扩展面必须保持 consumer-local，不能复制 infra/plugin 的 discover/load/unload/sign/ABI 治理职责。
5. docs/architecture/DASALL_tools子系统详细设计.md 12.6.4 进一步要求 consumer-specific 扩展语义在 tools bridge/importer 内解析，因此 007 只冻结目录视图和 source traceability ref，不在公共 ABI 中暴露 ToolDescriptor、MCPServerLaunchSpec 或 SkillSpecAsset 的完整实现细节。

## 2. 外部参考

1. VS Code Extension Manifest 文档要求每个扩展通过 `package.json` 显式声明入口与 `contributes`，并允许一个扩展组合多类贡献。这支持本任务把 plugin -> tools 边界设计为“显式扩展目录 + 有限贡献类别”的形式，而不是让 tools 任意扫描插件内部实现细节。

## 3. Design 结论

1. `IToolPluginProvider` 首版冻结为单一目录接口：`describe_extensions() const -> ToolPluginExtensionCatalog`；它只暴露插件向 tools 提供的 consumer-local 目录视图，不承担 discover/load/unload 或签名、ABI 校验职责。
2. `ToolPluginPayloadKind` 只保留三个枚举值：`builtin_tool_provider`、`mcp_server_stdio`、`skill_bundle`，从类型层面限制 007 的表达能力。
3. `ToolPluginProviderRef` 作为最小 source traceability 锚点，保留 `plugin_id`、`export_key`、`source_revision`，供后续 PluginExtensionBridge 做 source-owned delta、撤销和并发一致性管理。
4. `ToolPluginExtensionCatalog` 只暴露三类导出数组：builtin provider refs、stdio MCP server refs、skill bundle refs；具体 descriptor、launch spec、skill asset 的 consumer-specific 归一化仍留给 bridge/importer 内部处理。
5. `MCPServerLaunchSpec`、SkillSpec 归一化结果和 builtin tool factory 仍不进入 shared contracts；007 只冻结 module-public 目录视图，为 017、018、031 等后续实现任务提供 ABI 基线。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 plugin extension 目录接口 | tools/include/plugin/IToolPluginProvider.h |
| 增加 plugin catalog surface 证据 | tests/unit/tools/ToolPluginProviderSurfaceTest.cpp |

## 5. Build 三件套

1. 代码目标：把 tools/include/plugin/IToolPluginProvider.h 从壳文件替换为真实接口签名与最小目录 supporting type。
2. 测试目标：新增 tests/unit/tools/ToolPluginProviderSurfaceTest.cpp，以方法指针类型断言和样例 catalog 初始化锁定 plugin extension 公共面，同时保持该测试源未接入 CMake，留待 TOOL-TODO-008 统一纳管。
3. 验收命令：
   - `c++ -std=c++20 -I/home/gangan/DASALL/tools/include -x c++ -fsyntax-only /home/gangan/DASALL/tests/unit/tools/ToolPluginProviderSurfaceTest.cpp`
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`
   - `dasall_unit_tests` 目标构建期间自动执行当前 unit 集合，并以构建期 gate 结果作为本轮验收依据

## 6. 风险与回退

1. 当前 catalog 只暴露 ref 级目录信息；如果后续 bridge 需要更多 traceability 字段，应优先追加兼容字段，而不是把 descriptor、launch spec 或 skill asset 的完整实现细节直接暴露到公共 ABI。
2. 若未来出现新的 plugin payload 类别，不应在 007 的 ABI 上直接塞入泛化 `custom` 类型；应先更新设计文档和专项 TODO，再显式扩展 `ToolPluginPayloadKind`。
3. 若后续 infra/plugin 评审要求 source revision 使用更稳定的 handle/ref 语义，可在保持 `ToolPluginProviderRef` 兼容的前提下替换字段含义或追加新字段。