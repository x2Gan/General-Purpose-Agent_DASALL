add_library(dasall_apps_runtime_support STATIC
  src/RuntimeLiveDependencyComposition.cpp
)

target_compile_features(dasall_apps_runtime_support PUBLIC cxx_std_20)

target_include_directories(dasall_apps_runtime_support
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_SOURCE_DIR}/runtime/src
    ${CMAKE_SOURCE_DIR}/services/include
    ${CMAKE_SOURCE_DIR}/tools/src
)

target_link_libraries(dasall_apps_runtime_support
  PUBLIC
    dasall_runtime
  PRIVATE
    dasall_cognition
    dasall_infra
    dasall_knowledge
    dasall_llm
    dasall_memory
    dasall_multi_agent
    dasall_services
    dasall_tools
)

dasall_apply_common_options(dasall_apps_runtime_support)