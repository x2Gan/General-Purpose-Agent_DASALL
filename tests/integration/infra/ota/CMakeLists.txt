function(dasall_register_ota_integration_test executable_target_name ctest_name source_file)
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
  set_tests_properties(${ctest_name} PROPERTIES LABELS "integration;ota")
endfunction()

dasall_register_ota_integration_test(
  dasall_ota_workflow_integration_test
  OTAWorkflowTest
  OTAWorkflowTest.cpp
)

dasall_register_ota_integration_test(
  dasall_ota_failure_injection_integration_test
  OTAFailureInjectionTest
  OTAFailureInjectionTest.cpp
)

set_tests_properties(OTAFailureInjectionTest PROPERTIES LABELS "integration;ota;failure")