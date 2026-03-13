add_library(dasall_tools STATIC)

target_sources(dasall_tools
  PRIVATE
    src/placeholder.cpp
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
