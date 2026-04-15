# TOOL-TODO-001 tools 公共 include 布局与 CMake 骨架设计收敛

日期：2026-04-15  
任务：TOOL-TODO-001  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 6.5.1 明确 ToolInvocationContext、ToolInvocationEnvelope、IToolPluginProvider、IMCPTransport 等对象当前保持 tools 模块公共或内部层级，不进入 shared contracts。
2. docs/architecture/DASALL_tools子系统详细设计.md 6.6 已明确 ITool、IToolManager、IPolicyGate、ICapabilityCache、IToolPluginProvider、IMCPAdapter 的公共接口落点都应位于 tools/include。
3. docs/architecture/DASALL_tools子系统详细设计.md 8.1 已给出 tools/include、tools/include/plugin、tools/include/mcp 的目录布局建议，并将其列为 Phase 0 的首批工程落点。
4. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-001 的 owner 只覆盖公共 include 布局与 CMake 骨架；对象字段和接口签名分别由 TOOL-TODO-002 到 007 继续细化，源码树与 unit 入口由 TOOL-TODO-008 收口。
5. tools/CMakeLists.txt 当前只编译 src/placeholder.cpp，虽然暴露了 include 根，但没有显式 public header file set，导致模块公共头文件面尚未成为构建图中的稳定交付物。

## 2. 外部参考

1. Kitware CMake `target_sources` 文档说明 `FILE_SET` 的 `HEADERS` 类型用于通过 `#include` 消费的头文件；`PUBLIC` / `INTERFACE` file set 会把 `BASE_DIRS` 传播到依赖方的 include properties，并为 IDE 与导出场景提供稳定 header surface。本任务据此采用 `public_headers` file set，而不是只保留裸 `target_include_directories`。

## 3. Design 结论

1. tools/include 直接作为模块公共 include 根，首轮先把未来稳定入口文件全部落盘为可包含的壳文件，建立路径与命名约束，不提前在 001 中冻结字段与方法语义。
2. ToolInvocationContext、ToolInvocationEnvelope、plugin、mcp 子目录应与核心接口头一并建立目录槽位，因为 6.5.1 和 8.1 已经固定了它们的模块公共归属；具体对象内容仍分别留给 TOOL-TODO-002、003、006、007。
3. dasall_tools 在本轮继续保持 placeholder-only 源文件状态，避免与 TOOL-TODO-008 的真实源码骨架接线重叠；但要通过 `FILE_SET public_headers` 把公共头文件面显式接入构建图。
4. 本轮不改写 shared contracts、InterfaceCatalog 或任何 admission 结论；contract 侧只验证现有 ToolRequest、ToolResult、ToolDescriptor/IR 契约不回退。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 建立 tools 公共 include 根 | tools/include/ |
| 预留治理链公共接口头槽位 | tools/include/ITool.h、tools/include/IToolManager.h、tools/include/IPolicyGate.h、tools/include/ICapabilityCache.h |
| 预留调用上下文与统一返回面头槽位 | tools/include/ToolInvocationContext.h、tools/include/ToolInvocationEnvelope.h |
| 预留 plugin 扩展 ABI 头槽位 | tools/include/plugin/IToolPluginProvider.h |
| 预留 MCP 公共接口头槽位 | tools/include/mcp/IMCPAdapter.h、tools/include/mcp/IMCPTransport.h |
| 建立 CMake 公共头文件交付面 | tools/CMakeLists.txt 的 `FILE_SET public_headers` |

## 5. Build 三件套

1. 代码目标：新增 tools/include 公共头文件壳与 plugin/mcp 子目录，并在 tools/CMakeLists.txt 上登记 `public_headers` file set。
2. 测试目标：验证 dasall_tools、dasall_unit_tests、dasall_contract_tests 均可继续构建，且既有 ToolRequestContractTest、ToolResultContractTest、ToolDescriptorIRContractTest 不回退；真正的 tools unit discoverability 入口留给 TOOL-TODO-008。
3. 验收命令：
   - ListBuildTargets_CMakeTools
   - Build_CMakeTools 构建 dasall_tools、dasall_unit_tests、dasall_contract_tests
   - RunCtest_CMakeTools 运行 ToolRequestContractTest、ToolResultContractTest、ToolDescriptorIRContractTest

## 6. 风险与回退

1. TOOL-TODO-001 不能提前把 ITool、IToolManager、IPolicyGate、ICapabilityCache、IMCPAdapter、IToolPluginProvider 的方法签名写满，否则会与 TOOL-TODO-002 到 007 的对象/接口冻结任务重叠。
2. TOOL-TODO-001 不能把 src 骨架、ToolManager.cpp 或 tools unit 注册一并做完，否则会侵入 TOOL-TODO-008 的 owner 范围。
3. 若后续评审认为某个头文件壳过薄，应在对应的 002 到 007 任务中补齐定义，而不是回头扩大 001 的任务边界。