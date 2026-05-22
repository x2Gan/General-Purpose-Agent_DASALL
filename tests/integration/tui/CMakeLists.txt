set(DASALL_APPS_TUI_INTEGRATION_TEST_EXECUTABLE_TARGETS)

add_executable(dasall_tui_integration_topology_smoke_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiIntegrationTopologySmokeTest.cpp
)

target_link_libraries(dasall_tui_integration_topology_smoke_integration_test
  PRIVATE
    dasall_test_support
)

target_compile_definitions(dasall_tui_integration_topology_smoke_integration_test
  PRIVATE
    DASALL_TUI_INTEGRATION_TEST_DIR="${CMAKE_SOURCE_DIR}/tests/integration/tui"
    DASALL_TUI_INTEGRATION_TOP_LEVEL_CMAKE="${CMAKE_SOURCE_DIR}/tests/integration/CMakeLists.txt"
    DASALL_TUI_INTEGRATION_LOCAL_CMAKE="${CMAKE_SOURCE_DIR}/tests/integration/tui/CMakeLists.txt"
    DASALL_TUI_GOLDEN_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/tui/golden"
)

dasall_apply_common_options(dasall_tui_integration_topology_smoke_integration_test)

add_test(NAME TuiTestTopologyDiscoverability COMMAND dasall_tui_integration_topology_smoke_integration_test)
set_tests_properties(TuiTestTopologyDiscoverability PROPERTIES LABELS "integration;tui")

add_test(NAME TuiAppStartupTest COMMAND dasall_tui_integration_topology_smoke_integration_test)
set_tests_properties(TuiAppStartupTest PROPERTIES LABELS "integration;tui")

add_test(NAME TuiPrototypeSmokeTest COMMAND dasall_tui_integration_topology_smoke_integration_test)
set_tests_properties(TuiPrototypeSmokeTest PROPERTIES LABELS "integration;tui")

list(APPEND DASALL_APPS_TUI_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_tui_integration_topology_smoke_integration_test
)

set(DASALL_APPS_TUI_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_APPS_TUI_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)