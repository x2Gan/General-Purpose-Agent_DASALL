function(dasall_register_health_integration_test executable_target_name ctest_name source_file)
  add_executable(${executable_target_name}
    ${source_file}
  )

  target_include_directories(${executable_target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/infra/src
  )

  target_link_libraries(${executable_target_name}
    PRIVATE
      dasall_test_support
      dasall_infra
  )

  dasall_apply_common_options(${executable_target_name})

  add_test(NAME ${ctest_name} COMMAND ${executable_target_name})
  set_tests_properties(${ctest_name} PROPERTIES LABELS "integration;health")
endfunction()

dasall_register_health_integration_test(
  dasall_health_wiring_integration_test
  HealthWiringIntegrationTest
  HealthWiringIntegrationTest.cpp
)

add_executable(dasall_infra_health_cadence_integration_test
  InfraHealthCadenceIntegrationTest.cpp
)

target_include_directories(dasall_infra_health_cadence_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
    ${CMAKE_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall_infra_health_cadence_integration_test
  PRIVATE
    dasall_test_support
    dasall_infra
    dasall_runtime
    dasall_platform
)

dasall_apply_common_options(dasall_infra_health_cadence_integration_test)

add_test(NAME InfraHealthCadenceIntegrationTest COMMAND dasall_infra_health_cadence_integration_test)
set_tests_properties(InfraHealthCadenceIntegrationTest PROPERTIES LABELS "integration;health")