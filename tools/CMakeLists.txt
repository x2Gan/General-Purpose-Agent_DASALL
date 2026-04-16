add_library(dasall_tools STATIC)

target_sources(dasall_tools
  PRIVATE
    src/ToolManager.cpp
    src/bridge/PluginExtensionBridge.cpp
    src/config/placeholder.cpp
    src/execution/placeholder.cpp
    src/mcp/placeholder.cpp
    src/ops/placeholder.cpp
    src/policy/placeholder.cpp
    src/projection/placeholder.cpp
    src/registry/ToolRegistry.cpp
    src/registry/BuiltinCatalog.cpp
    src/registry/MCPBindingRegistry.cpp
    src/route/placeholder.cpp
    src/skills/placeholder.cpp
    src/validation/placeholder.cpp
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
)

target_link_libraries(dasall_tools
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_tools)
