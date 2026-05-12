add_library(dasall_multi_agent STATIC)

target_sources(dasall_multi_agent
  PRIVATE
    src/MultiAgentCoordinator.cpp
    src/MultiAgentRuntimeFold.cpp
)

target_compile_features(dasall_multi_agent PUBLIC cxx_std_20)

target_include_directories(dasall_multi_agent
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tools/src
)

target_link_libraries(dasall_multi_agent
  PUBLIC
    dasall_contracts
    dasall_tools
)

dasall_apply_common_options(dasall_multi_agent)
