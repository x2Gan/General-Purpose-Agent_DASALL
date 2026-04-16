add_library(dasall_tools STATIC)

target_sources(dasall_tools
  PRIVATE
    src/ToolManager.cpp
    src/bridge/PluginExtensionBridge.cpp
    src/bridge/ToolServiceBridge.cpp
    src/config/ToolConfigAdapter.cpp
    src/execution/BuiltinExecutorLane.cpp
    src/execution/CompensationLedger.cpp
    src/execution/ExecutorLanePool.cpp
    src/execution/WorkflowEngine.cpp
    src/mcp/CapabilityCache.cpp
    src/mcp/placeholder.cpp
    src/ops/ToolAuditBridge.cpp
    src/ops/ToolHealthProbe.cpp
    src/ops/ToolMetricsBridge.cpp
    src/ops/ToolTraceBridge.cpp
    src/ops/placeholder.cpp
    src/policy/ToolPolicyGate.cpp
    src/projection/ResultProjector.cpp
    src/projection/placeholder.cpp
    src/registry/ToolRegistry.cpp
    src/registry/BuiltinCatalog.cpp
    src/registry/MCPBindingRegistry.cpp
    src/route/ToolRouteSelector.cpp
    src/skills/placeholder.cpp
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

dasall_apply_common_options(dasall_tools)
