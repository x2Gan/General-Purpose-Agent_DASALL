add_executable(dasall_infra_diagnostics_smoke_integration_test
  InfraDiagnosticsSmokeTest.cpp
)

target_link_libraries(dasall_infra_diagnostics_smoke_integration_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_infra_diagnostics_smoke_integration_test)

add_test(NAME InfraDiagnosticsSmokeTest COMMAND dasall_infra_diagnostics_smoke_integration_test)
set_tests_properties(InfraDiagnosticsSmokeTest PROPERTIES LABELS "integration")

add_executable(dasall_infra_audit_health_integration_test
  InfraAuditHealthIntegrationTest.cpp
)

target_include_directories(dasall_infra_audit_health_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_infra_audit_health_integration_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_infra_audit_health_integration_test)

add_test(NAME InfraAuditHealthIntegrationTest COMMAND dasall_infra_audit_health_integration_test)
set_tests_properties(InfraAuditHealthIntegrationTest PROPERTIES LABELS "integration")

add_subdirectory(config)
add_subdirectory(logging)
