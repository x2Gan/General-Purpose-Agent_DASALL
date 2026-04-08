add_executable(dasall_infra_diagnostics_smoke_integration_test
  InfraDiagnosticsSmokeTest.cpp
)

target_link_libraries(dasall_infra_diagnostics_smoke_integration_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

target_include_directories(dasall_infra_diagnostics_smoke_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

dasall_apply_common_options(dasall_infra_diagnostics_smoke_integration_test)

add_test(NAME InfraDiagnosticsSmokeTest COMMAND dasall_infra_diagnostics_smoke_integration_test)
set_tests_properties(InfraDiagnosticsSmokeTest PROPERTIES LABELS "integration")

add_executable(dasall_infra_diagnostics_integration_test
  InfraDiagnosticsIntegrationTest.cpp
)

target_link_libraries(dasall_infra_diagnostics_integration_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

target_include_directories(dasall_infra_diagnostics_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

dasall_apply_common_options(dasall_infra_diagnostics_integration_test)

add_test(NAME InfraDiagnosticsIntegrationTest COMMAND dasall_infra_diagnostics_integration_test)
set_tests_properties(InfraDiagnosticsIntegrationTest PROPERTIES LABELS "integration")

add_subdirectory(audit)
add_subdirectory(config)
add_subdirectory(logging)
add_subdirectory(plugin)
add_subdirectory(policy)
add_subdirectory(secret)
add_subdirectory(health)
add_subdirectory(ota)
add_subdirectory(tracing)
add_subdirectory(watchdog)
