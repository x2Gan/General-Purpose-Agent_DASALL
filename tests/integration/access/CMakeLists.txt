set(DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS)

add_executable(dasall_access_gateway_smoke_integration_test
  AccessGatewaySmokeIntegrationTest.cpp
)

target_link_libraries(dasall_access_gateway_smoke_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_gateway_smoke_integration_test)

add_test(NAME AccessGatewaySmokeIntegrationTest COMMAND dasall_access_gateway_smoke_integration_test)
set_tests_properties(AccessGatewaySmokeIntegrationTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_gateway_smoke_integration_test
)

if(NOT DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "access integration registration requires at least one test target")
endif()

set(DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)
