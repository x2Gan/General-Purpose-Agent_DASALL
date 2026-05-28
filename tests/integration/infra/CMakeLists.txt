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
set_tests_properties(InfraDiagnosticsSmokeTest PROPERTIES LABELS "integration;diagnostics;gate-int-05;diagnostics-retained-snapshot-gate")

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
set_tests_properties(InfraDiagnosticsIntegrationTest PROPERTIES LABELS "integration;diagnostics;gate-int-05;diagnostics-retained-snapshot-gate")

add_executable(dasall_infra_key_subsystem_production_logging_e2e_test
  KeySubsystemProductionLoggingE2ETest.cpp
)

target_link_libraries(dasall_infra_key_subsystem_production_logging_e2e_test
  PRIVATE
    dasall_test_support
    dasall_apps_runtime_support
    dasall_cognition
    dasall_infra
    dasall_knowledge
    dasall_memory
    dasall_profiles
    dasall_runtime
    dasall_tools
)

target_include_directories(dasall_infra_key_subsystem_production_logging_e2e_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
    ${CMAKE_SOURCE_DIR}/runtime/src
    ${CMAKE_SOURCE_DIR}/tests/mocks/include
)

dasall_apply_common_options(dasall_infra_key_subsystem_production_logging_e2e_test)

add_test(NAME KeySubsystemProductionLoggingE2ETest COMMAND dasall_infra_key_subsystem_production_logging_e2e_test)
set_tests_properties(KeySubsystemProductionLoggingE2ETest PROPERTIES LABELS "integration;infra;logging")

add_subdirectory(audit)
add_subdirectory(config)
add_subdirectory(logging)
add_subdirectory(metrics)
add_subdirectory(plugin)
add_subdirectory(policy)
add_subdirectory(secret)
add_subdirectory(health)
add_subdirectory(ota)
add_subdirectory(tracing)
add_subdirectory(watchdog)
