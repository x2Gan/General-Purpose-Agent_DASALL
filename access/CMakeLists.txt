add_library(dasall_access STATIC)

target_sources(dasall_access
  PRIVATE
    src/placeholder.cpp
)

target_compile_features(dasall_access PUBLIC cxx_std_20)

target_include_directories(dasall_access
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dasall_access
  PUBLIC
    dasall_contracts
  PRIVATE
    dasall_infra
)

dasall_apply_common_options(dasall_access)