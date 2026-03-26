add_library(dasall_infra STATIC)

target_sources(dasall_infra
  PRIVATE
    src/placeholder.cpp
    src/InfraServiceFacade.cpp
    src/tracing/TracingModuleAnchor.cpp
)

target_compile_features(dasall_infra PUBLIC cxx_std_20)

target_include_directories(dasall_infra
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dasall_infra
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_infra)
