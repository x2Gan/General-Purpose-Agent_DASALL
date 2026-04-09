add_executable(dasall_services_audit_integration_test
  CapabilityServicesAuditIntegrationTest.cpp
)

target_link_libraries(dasall_services_audit_integration_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_services_audit_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
    ${CMAKE_SOURCE_DIR}/infra/include
)

dasall_apply_common_options(dasall_services_audit_integration_test)

add_test(NAME CapabilityServicesAuditIntegrationTest COMMAND dasall_services_audit_integration_test)
set_tests_properties(CapabilityServicesAuditIntegrationTest PROPERTIES LABELS "integration")

add_executable(dasall_services_metrics_integration_test
  CapabilityServicesMetricsIntegrationTest.cpp
)

target_link_libraries(dasall_services_metrics_integration_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_services_metrics_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
    ${CMAKE_SOURCE_DIR}/infra/include
)

dasall_apply_common_options(dasall_services_metrics_integration_test)

add_test(NAME CapabilityServicesMetricsIntegrationTest COMMAND dasall_services_metrics_integration_test)
set_tests_properties(CapabilityServicesMetricsIntegrationTest PROPERTIES LABELS "integration")