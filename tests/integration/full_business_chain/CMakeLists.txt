set(DASALL_FULL_BUSINESS_CHAIN_INTEGRATION_TEST_EXECUTABLE_TARGETS)

add_executable(dasall_fullint_011_async_recovery_causality
  ${CMAKE_CURRENT_SOURCE_DIR}/FullIntAsyncRecoveryCausalityTest.cpp
)

target_link_libraries(dasall_fullint_011_async_recovery_causality
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_runtime
    dasall_memory
    dasall_sqlite3
)

target_include_directories(dasall_fullint_011_async_recovery_causality
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/src
    ${CMAKE_SOURCE_DIR}/memory/src
    ${CMAKE_SOURCE_DIR}/runtime/src
)

target_compile_definitions(dasall_fullint_011_async_recovery_causality
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

dasall_apply_common_options(dasall_fullint_011_async_recovery_causality)

add_test(NAME FullIntAsyncRecoveryCausalityTest
  COMMAND dasall_fullint_011_async_recovery_causality
)

set_tests_properties(FullIntAsyncRecoveryCausalityTest PROPERTIES
  LABELS "integration;full-business-chain;fullint-011"
)

list(APPEND DASALL_FULL_BUSINESS_CHAIN_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_fullint_011_async_recovery_causality
)

if(NOT DASALL_FULL_BUSINESS_CHAIN_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "full_business_chain integration registration requires at least one test target")
endif()

set(DASALL_FULL_BUSINESS_CHAIN_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_FULL_BUSINESS_CHAIN_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)