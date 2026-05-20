add_library(dasall_tools STATIC)

target_sources(dasall_tools
  PRIVATE
    src/ToolManager.cpp
    src/builtin/dataset/AgentDatasetTool.cpp
    src/builtin/terminal/AgentTerminalTool.cpp
    src/bridge/PluginExtensionBridge.cpp
    src/bridge/ToolPluginExtensionConsumer.cpp
    src/bridge/ToolPluginLifecycleBridge.cpp
    src/bridge/ToolServiceBridge.cpp
    src/config/ToolConfigAdapter.cpp
    src/execution/BuiltinExecutorLane.cpp
    src/execution/CompensationLedger.cpp
    src/execution/ExecutorLanePool.cpp
    src/execution/WorkflowEngine.cpp
    src/mcp/CapabilityCache.cpp
    src/mcp/CapabilityDiscovery.cpp
    src/mcp/MCPAdapter.cpp
    src/mcp/MCPLane.cpp
    src/mcp/StdioMCPServerLauncher.cpp
    src/mcp/StdioMCPTransport.cpp
    src/ops/ToolAuditBridge.cpp
    src/ops/ToolHealthProbe.cpp
    src/ops/ToolMetricsBridge.cpp
    src/ops/ToolTraceBridge.cpp
    src/policy/ToolPolicyGate.cpp
    src/projection/ResultProjector.cpp
    src/registry/ToolRegistry.cpp
    src/registry/BuiltinCatalog.cpp
    src/registry/MCPBindingRegistry.cpp
    src/route/ToolRouteSelector.cpp
    src/skills/ExternalSkillImporter.cpp
    src/skills/PluginSkillBundleImporter.cpp
    src/skills/SkillImportSupport.cpp
    src/skills/SkillRegistry.cpp
    src/skills/SkillRuntime.cpp
    src/validation/ToolValidator.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
      include/ITool.h
      include/IToolManager.h
      include/IPolicyGate.h
      include/ICapabilityCache.h
      include/ToolInvocationContext.h
      include/ToolInvocationEnvelope.h
      include/plugin/IToolPluginProvider.h
      include/mcp/IMCPAdapter.h
      include/mcp/IMCPTransport.h
)

target_compile_features(dasall_tools PUBLIC cxx_std_20)

target_include_directories(dasall_tools
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/infra/include
    ${CMAKE_SOURCE_DIR}/profiles/include
    ${CMAKE_SOURCE_DIR}/services/include
)

target_link_libraries(dasall_tools
  PUBLIC
    dasall_contracts
)

install(DIRECTORY
  ${CMAKE_SOURCE_DIR}/skills
  DESTINATION ${DASALL_INSTALL_DASALL_DATADIR}
)

dasall_apply_common_options(dasall_tools)
