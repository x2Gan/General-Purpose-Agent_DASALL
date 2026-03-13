add_library(dasall_contracts INTERFACE)

target_include_directories(dasall_contracts
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dasall_contracts
  INTERFACE
    dasall_build_options
)
