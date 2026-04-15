add_library(dasall_tools STATIC)

target_sources(dasall_tools
  PRIVATE
    src/placeholder.cpp
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
