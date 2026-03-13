add_library(dasall_runtime STATIC)

target_sources(dasall_runtime
  PRIVATE
    src/placeholder.cpp
)

target_compile_features(dasall_runtime PUBLIC cxx_std_20)

target_include_directories(dasall_runtime
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dasall_runtime
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_runtime)
