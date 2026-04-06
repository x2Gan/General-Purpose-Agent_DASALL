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

add_subdirectory(audit)
add_subdirectory(config)
add_subdirectory(logging)
add_subdirectory(policy)
add_subdirectory(secret)
add_subdirectory(health)
