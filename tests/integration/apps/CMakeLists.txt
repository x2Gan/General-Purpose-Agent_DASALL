add_subdirectory(cli)

set(DASALL_APPS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_APPS_CLI_INTEGRATION_TEST_EXECUTABLE_TARGETS}
)

add_executable(dasall_apps_command_routing_integration_test
  ${CMAKE_SOURCE_DIR}/tests/integration/apps/tui/DasallCommandRoutingTest.cpp
)

target_include_directories(dasall_apps_command_routing_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/integration/access
)

target_compile_definitions(dasall_apps_command_routing_integration_test
  PRIVATE
    DASALL_TUI_BINARY_PATH="$<TARGET_FILE:dasall-tui>"
    DASALL_CLI_BINARY_PATH="$<TARGET_FILE:dasall-cli>"
    DASALL_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(dasall_apps_command_routing_integration_test
  PRIVATE
    dasall_test_support
)

add_dependencies(dasall_apps_command_routing_integration_test
  dasall-tui
  dasall-cli
)

dasall_apply_common_options(dasall_apps_command_routing_integration_test)

add_test(NAME DasallCommandRoutingTest
  COMMAND dasall_apps_command_routing_integration_test)
set_tests_properties(DasallCommandRoutingTest PROPERTIES LABELS "integration;apps;tui;cli")

list(APPEND DASALL_APPS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_apps_command_routing_integration_test
)

set(DASALL_APPS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_APPS_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)