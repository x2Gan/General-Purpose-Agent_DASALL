add_library(dasall_memory STATIC)

target_sources(dasall_memory
  PRIVATE
    src/placeholder.cpp
)

target_compile_features(dasall_memory PUBLIC cxx_std_20)

target_include_directories(dasall_memory
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dasall_memory
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_memory)
