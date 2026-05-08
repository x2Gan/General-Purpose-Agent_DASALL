set(DASALL_APPS_CLI_INTEGRATION_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_cli_config_integration_test target_name test_name source_file)
  add_executable(${target_name}
    ${source_file}
    ${ARGN}
  )

  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/tests/integration/access
      ${CMAKE_SOURCE_DIR}/apps/cli/src
      ${CMAKE_SOURCE_DIR}/infra/src
      ${CMAKE_SOURCE_DIR}/access/include
  )

  target_compile_definitions(${target_name}
    PRIVATE
      DASALL_CLI_BINARY_PATH="$<TARGET_FILE:dasall-cli>"
      DASALL_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}"
  )

  target_link_libraries(${target_name}
    PRIVATE
      dasall_test_support
      dasall_infra
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
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/CliConfigWorkflowCoordinator.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigDiffPlanner.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/DaemonConfigFileStore.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/InstallStateProbe.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPreflightChecker.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ServiceManagerAdapter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/InteractivePromptEngine.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/LlmSecretPage.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPlanFormatter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigSummaryFormatter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/PrivilegeProbe.cpp
)

dasall_add_cli_config_integration_test(
  dasall_cli_config_interactive_wizard_integration_test
  ConfigInteractiveWizardTest
  ${CMAKE_SOURCE_DIR}/tests/integration/apps/cli/ConfigInteractiveWizardTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/CliConfigWorkflowCoordinator.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigDiffPlanner.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/DaemonConfigFileStore.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/InstallStateProbe.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPreflightChecker.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ServiceManagerAdapter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/InteractivePromptEngine.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/LlmSecretPage.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPlanFormatter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigSummaryFormatter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/PrivilegeProbe.cpp
)

dasall_add_cli_config_integration_test(
  dasall_cli_config_show_validate_integration_test
  ConfigShowValidateIntegrationTest
  ${CMAKE_SOURCE_DIR}/tests/integration/apps/cli/ConfigShowValidateIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/CliConfigWorkflowCoordinator.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigDiffPlanner.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/DaemonConfigFileStore.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/InstallStateProbe.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPreflightChecker.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ServiceManagerAdapter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/InteractivePromptEngine.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/LlmSecretPage.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPlanFormatter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigSummaryFormatter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/PrivilegeProbe.cpp
)

dasall_add_cli_config_integration_test(
  dasall_cli_config_apply_integration_test
  ConfigApplyWorkflowTest
  ${CMAKE_SOURCE_DIR}/tests/integration/apps/cli/ConfigApplyWorkflowTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/CliConfigWorkflowCoordinator.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/DaemonConfigFileStore.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/InstallStateProbe.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPreflightChecker.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigDiffPlanner.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ServiceManagerAdapter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/InteractivePromptEngine.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/LlmSecretPage.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPlanFormatter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigSummaryFormatter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/PrivilegeProbe.cpp
)

dasall_add_cli_config_integration_test(
  dasall_cli_config_modify_existing_workflow_integration_test
  ConfigModifyExistingWorkflowTest
  ${CMAKE_SOURCE_DIR}/tests/integration/apps/cli/ConfigModifyExistingWorkflowTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/CliConfigWorkflowCoordinator.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/DaemonConfigFileStore.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/InstallStateProbe.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPreflightChecker.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigDiffPlanner.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ServiceManagerAdapter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/InteractivePromptEngine.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/LlmSecretPage.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPlanFormatter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigSummaryFormatter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/PrivilegeProbe.cpp
)

if(NOT DASALL_APPS_CLI_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "apps/cli integration registration requires at least one config test target")
endif()

set(DASALL_APPS_CLI_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_APPS_CLI_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)