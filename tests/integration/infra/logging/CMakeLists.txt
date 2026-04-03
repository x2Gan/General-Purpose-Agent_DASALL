function(dasall_register_logging_integration_test executable_target_name ctest_name source_file)
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
  set_tests_properties(${ctest_name} PROPERTIES LABELS "integration;logging")
endfunction()

dasall_register_logging_integration_test(
  dasall_logging_pipeline_integration_test
  LoggingPipelineIntegrationTest
  LoggingPipelineIntegrationTest.cpp
)

dasall_register_logging_integration_test(
  dasall_logging_audit_link_integration_test
  LoggingAuditLinkIntegrationTest
  LoggingAuditLinkIntegrationTest.cpp
)

dasall_register_logging_integration_test(
  dasall_log_query_integration_test
  LogQueryIntegrationTest
  LogQueryIntegrationTest.cpp
)