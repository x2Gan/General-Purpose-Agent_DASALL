set(DASALL_SERVICES_INTEGRATION_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_services_integration_test target_name test_name source_file labels)
  add_executable(${target_name}
    ${source_file}
  )

  target_link_libraries(${target_name}
    PRIVATE
      dasall_test_support
      dasall_services
  )

  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/services/src
      ${CMAKE_SOURCE_DIR}/infra/include
  )

  dasall_apply_common_options(${target_name})

  add_test(NAME ${test_name} COMMAND ${target_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "${labels}")

  list(APPEND DASALL_SERVICES_INTEGRATION_TEST_EXECUTABLE_TARGETS ${target_name})
endmacro()

dasall_add_services_integration_test(
  dasall_services_smoke_integration_test
  CapabilityServicesSmokeIntegrationTest
  CapabilityServicesSmokeIntegrationTest.cpp
  "integration"
)

dasall_add_services_integration_test(
  dasall_services_failure_integration_test
  CapabilityServicesFailureIntegrationTest
  CapabilityServicesFailureIntegrationTest.cpp
  "integration;failure"
)

dasall_add_services_integration_test(
  dasall_services_audit_integration_test
  CapabilityServicesAuditIntegrationTest
  CapabilityServicesAuditIntegrationTest.cpp
  "integration"
)

dasall_add_services_integration_test(
  dasall_services_metrics_integration_test
  CapabilityServicesMetricsIntegrationTest
  CapabilityServicesMetricsIntegrationTest.cpp
  "integration"
)

dasall_add_services_integration_test(
  dasall_services_trace_integration_test
  CapabilityServicesTraceIntegrationTest
  CapabilityServicesTraceIntegrationTest.cpp
  "integration"
)

dasall_add_services_integration_test(
  dasall_services_health_integration_test
  CapabilityServicesHealthIntegrationTest
  CapabilityServicesHealthIntegrationTest.cpp
  "integration"
)

if(NOT DASALL_SERVICES_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "services integration registration requires at least one test target")
endif()

set(DASALL_SERVICES_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_SERVICES_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)