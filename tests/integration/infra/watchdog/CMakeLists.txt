function(dasall_register_watchdog_integration_test executable_target_name ctest_name source_file)
  add_executable(${executable_target_name}
    ${source_file}
  )

  target_include_directories(${executable_target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/infra/src
      ${CMAKE_SOURCE_DIR}/platform/include
  )

  target_link_libraries(${executable_target_name}
    PRIVATE
      dasall_test_support
      dasall_infra
  )

  dasall_apply_common_options(${executable_target_name})

  add_test(NAME ${ctest_name} COMMAND ${executable_target_name})
  set_tests_properties(${ctest_name} PROPERTIES LABELS "integration;watchdog;watchdog-integration")
endfunction()

dasall_register_watchdog_integration_test(
  dasall_watchdog_integration_test
  WatchdogIntegrationTest
  WatchdogIntegrationTest.cpp
)

dasall_register_watchdog_integration_test(
  dasall_watchdog_failure_injection_integration_test
  WatchdogFailureInjectionTest
  WatchdogFailureInjectionTest.cpp
)
set_tests_properties(WatchdogFailureInjectionTest PROPERTIES LABELS "integration;watchdog;watchdog-integration;failure;watchdog-failure")

dasall_register_watchdog_integration_test(
  dasall_watchdog_profile_compatibility_integration_test
  WatchdogProfileCompatibilityTest
  WatchdogProfileCompatibilityTest.cpp
)
set_tests_properties(WatchdogProfileCompatibilityTest PROPERTIES LABELS "integration;watchdog;watchdog-integration;profile;watchdog-profile")