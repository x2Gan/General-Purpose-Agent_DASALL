add_library(dasall_test_support INTERFACE)

target_include_directories(dasall_test_support
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dasall_test_support
  INTERFACE
    dasall_build_options
    dasall_contracts
    dasall_llm
)
