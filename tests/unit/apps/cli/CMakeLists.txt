set(DASALL_APPS_CLI_UNIT_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_cli_unit_test target_name test_name source_file)
  add_executable(${target_name}
    ${source_file}
    ${ARGN}
  )

  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/apps/cli/src
      ${CMAKE_SOURCE_DIR}/access/include
  )

  add_test(NAME ${test_name} COMMAND ${target_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "unit;access;cli")

  list(APPEND DASALL_APPS_CLI_UNIT_TEST_EXECUTABLE_TARGETS
    ${target_name}
  )
endmacro()

dasall_add_cli_unit_test(
  dasall-cli_ipc_client_unit_test
  CliIpcClientTest
  ${CMAKE_SOURCE_DIR}/tests/unit/access/CliIpcClientTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliRequestBuilder.cpp
)

target_link_libraries(dasall-cli_ipc_client_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_platform
)

dasall_apply_common_options(dasall-cli_ipc_client_unit_test)

dasall_add_cli_unit_test(
  dasall-cli_ipc_client_response_unit_test
  CliIpcClientResponseTest
  ${CMAKE_SOURCE_DIR}/tests/unit/access/CliIpcClientResponseTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliRequestBuilder.cpp
)

target_link_libraries(dasall-cli_ipc_client_response_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_platform
)

dasall_apply_common_options(dasall-cli_ipc_client_response_unit_test)

dasall_add_cli_unit_test(
  dasall-cli_ipc_client_unavailable_unit_test
  CliIpcClientUnavailableTest
  ${CMAKE_SOURCE_DIR}/tests/unit/access/CliIpcClientUnavailableTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliRequestBuilder.cpp
)

target_link_libraries(dasall-cli_ipc_client_unavailable_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_platform
)

dasall_apply_common_options(dasall-cli_ipc_client_unavailable_unit_test)

dasall_add_cli_unit_test(
  dasall-cli_command_parser_unit_test
  CliDaemonCommandParserTest
  ${CMAKE_SOURCE_DIR}/tests/unit/access/CliDaemonCommandParserTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliCommandParser.cpp
)

target_link_libraries(dasall-cli_command_parser_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-cli_command_parser_unit_test)

dasall_add_cli_unit_test(
  dasall-config_command_parser_unit_test
  ConfigCommandParserTest
  ${CMAKE_SOURCE_DIR}/tests/unit/apps/cli/ConfigCommandParserTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliCommandParser.cpp
)

target_link_libraries(dasall-config_command_parser_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-config_command_parser_unit_test)

dasall_add_cli_unit_test(
  dasall-config_command_types_unit_test
  ConfigCommandTypesTest
  ${CMAKE_SOURCE_DIR}/tests/unit/apps/cli/ConfigCommandTypesTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
)

target_link_libraries(dasall-config_command_types_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-config_command_types_unit_test)

dasall_add_cli_unit_test(
  dasall-install_state_probe_unit_test
  InstallStateProbeTest
  ${CMAKE_SOURCE_DIR}/tests/unit/apps/cli/InstallStateProbeTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/InstallStateProbe.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
)

target_link_libraries(dasall-install_state_probe_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-install_state_probe_unit_test)

dasall_add_cli_unit_test(
  dasall-config_capability_resolver_unit_test
  ConfigCapabilityResolverTest
  ${CMAKE_SOURCE_DIR}/tests/unit/apps/cli/ConfigCapabilityResolverTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCapabilityResolver.cpp
)

target_link_libraries(dasall-config_capability_resolver_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-config_capability_resolver_unit_test)

dasall_add_cli_unit_test(
  dasall-daemon_config_file_store_unit_test
  DaemonConfigFileStoreTest
  ${CMAKE_SOURCE_DIR}/tests/unit/apps/cli/DaemonConfigFileStoreTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/DaemonConfigFileStore.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
)

target_link_libraries(dasall-daemon_config_file_store_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-daemon_config_file_store_unit_test)

dasall_add_cli_unit_test(
  dasall-service_manager_adapter_unit_test
  ServiceManagerAdapterTest
  ${CMAKE_SOURCE_DIR}/tests/unit/apps/cli/ServiceManagerAdapterTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ServiceManagerAdapter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
)

target_link_libraries(dasall-service_manager_adapter_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-service_manager_adapter_unit_test)

dasall_add_cli_unit_test(
  dasall-config_preflight_checker_unit_test
  ConfigPreflightCheckerTest
  ${CMAKE_SOURCE_DIR}/tests/unit/apps/cli/ConfigPreflightCheckerTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPreflightChecker.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/PrivilegeProbe.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
)

target_link_libraries(dasall-config_preflight_checker_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-config_preflight_checker_unit_test)

dasall_add_cli_unit_test(
  dasall-interactive_prompt_engine_unit_test
  InteractivePromptEngineTest
  ${CMAKE_SOURCE_DIR}/tests/unit/apps/cli/InteractivePromptEngineTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/InteractivePromptEngine.cpp
)

target_link_libraries(dasall-interactive_prompt_engine_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-interactive_prompt_engine_unit_test)

dasall_add_cli_unit_test(
  dasall-config_plan_formatter_unit_test
  ConfigPlanFormatterTest
  ${CMAKE_SOURCE_DIR}/tests/unit/apps/cli/ConfigPlanFormatterTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPlanFormatter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
)

target_link_libraries(dasall-config_plan_formatter_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-config_plan_formatter_unit_test)

dasall_add_cli_unit_test(
  dasall-config_summary_formatter_unit_test
  ConfigSummaryFormatterTest
  ${CMAKE_SOURCE_DIR}/tests/unit/apps/cli/ConfigSummaryFormatterTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigSummaryFormatter.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
)

target_link_libraries(dasall-config_summary_formatter_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-config_summary_formatter_unit_test)

dasall_add_cli_unit_test(
  dasall-cli_output_formatter_unit_test
  CliDaemonOutputFormatterTest
  ${CMAKE_SOURCE_DIR}/tests/unit/access/CliDaemonOutputFormatterTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliOutputFormatter.cpp
)

target_link_libraries(dasall-cli_output_formatter_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall-cli_output_formatter_unit_test)
