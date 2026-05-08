set(DASALL_APPS_CLI_INTEGRATION_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_cli_config_integration_test target_name test_name source_file)
  add_executable(${target_name}
    ${source_file}
  )

  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/tests/integration/access
  )

  target_compile_definitions(${target_name}
    PRIVATE
      DASALL_CLI_BINARY_PATH="$<TARGET_FILE:dasall-cli>"
      DASALL_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}"
  )

  target_link_libraries(${target_name}
    PRIVATE
      dasall_test_support
  )

  add_dependencies(${target_name}
    dasall-cli
  )

  dasall_apply_common_options(${target_name})

  add_test(NAME ${test_name} COMMAND ${target_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "integration;cli;config")

  list(APPEND DASALL_APPS_CLI_INTEGRATION_TEST_EXECUTABLE_TARGETS ${target_name})
endmacro()

dasall_add_cli_config_integration_test(
  dasall_cli_config_fresh_install_workflow_integration_test
  ConfigFreshInstallWorkflowTest
  ${CMAKE_SOURCE_DIR}/tests/integration/apps/cli/ConfigFreshInstallWorkflowTest.cpp
)

if(NOT DASALL_APPS_CLI_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "apps/cli integration registration requires at least one config test target")
endif()

set(DASALL_APPS_CLI_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_APPS_CLI_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)