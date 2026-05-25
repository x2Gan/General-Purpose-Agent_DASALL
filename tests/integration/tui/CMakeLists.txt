set(DASALL_APPS_TUI_INTEGRATION_TEST_EXECUTABLE_TARGETS)

add_executable(dasall_tui_integration_topology_smoke_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiIntegrationTopologySmokeTest.cpp
)

add_executable(dasall_tui_app_startup_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiAppStartupTest.cpp
)

add_executable(dasall_tui_app_startup_failure_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiAppStartupFailureTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/data/DaemonTuiDataSource.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/ipc/TuiIpcController.cpp
)

add_executable(dasall_tui_prototype_smoke_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiPrototypeSmokeTest.cpp
)

add_executable(dasall_tui_status_panel_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiStatusPanelIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/data/DaemonTuiDataSource.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/ipc/TuiIpcController.cpp
)

add_executable(dasall_tui_session_lifecycle_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiSessionLifecycleIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/command/TuiSlashCommandParser.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/data/DaemonTuiDataSource.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/ipc/TuiIpcController.cpp
)

add_executable(dasall_tui_next_preference_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiNextPreferenceIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/data/DaemonTuiDataSource.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/ipc/TuiIpcController.cpp
)

add_executable(dasall_tui_submit_turn_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiAppSubmitTurnIntegrationTest.cpp
)

add_executable(dasall_tui_entrypoint_smoke_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/DasallTuiEntrypointSmokeTest.cpp
)

add_executable(dasall_tui_socket_override_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/DasallTuiSocketOverrideTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/data/DaemonTuiDataSource.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/ipc/TuiIpcController.cpp
)

target_link_libraries(dasall_tui_integration_topology_smoke_integration_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_app_startup_integration_test
  PRIVATE
    dasall_test_support
    dasall_tui_prototype_core
)

target_link_libraries(dasall_tui_app_startup_failure_integration_test
  PRIVATE
    dasall_test_support
    dasall_tui_prototype_core
    dasall_platform
)

target_link_libraries(dasall_tui_prototype_smoke_integration_test
  PRIVATE
    dasall_test_support
    dasall_tui_prototype_core
)

target_link_libraries(dasall_tui_status_panel_integration_test
  PRIVATE
    dasall_test_support
    dasall_tui_prototype_core
    dasall_platform
)

target_link_libraries(dasall_tui_session_lifecycle_integration_test
  PRIVATE
    dasall_test_support
    dasall_tui_prototype_core
    dasall_platform
)

target_link_libraries(dasall_tui_next_preference_integration_test
  PRIVATE
    dasall_test_support
    dasall_tui_prototype_core
    dasall_platform
)

target_link_libraries(dasall_tui_submit_turn_integration_test
  PRIVATE
    dasall_test_support
    dasall_tui_prototype_core
)

target_link_libraries(dasall_tui_entrypoint_smoke_integration_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_socket_override_integration_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

target_include_directories(dasall_tui_socket_override_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_compile_definitions(dasall_tui_integration_topology_smoke_integration_test
  PRIVATE
    DASALL_TUI_INTEGRATION_TEST_DIR="${CMAKE_SOURCE_DIR}/tests/integration/tui"
    DASALL_TUI_INTEGRATION_TOP_LEVEL_CMAKE="${CMAKE_SOURCE_DIR}/tests/integration/CMakeLists.txt"
    DASALL_TUI_INTEGRATION_LOCAL_CMAKE="${CMAKE_SOURCE_DIR}/tests/integration/tui/CMakeLists.txt"
    DASALL_TUI_GOLDEN_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/tui/golden"
    DASALL_APPS_TOP_LEVEL_CMAKE="${CMAKE_SOURCE_DIR}/apps/CMakeLists.txt"
    DASALL_APPS_TUI_CMAKE="${CMAKE_SOURCE_DIR}/apps/tui/CMakeLists.txt"
    DASALL_APPS_TUI_MAIN="${CMAKE_SOURCE_DIR}/apps/tui/src/main.cpp"
)

target_compile_definitions(dasall_tui_entrypoint_smoke_integration_test
  PRIVATE
    DASALL_TUI_BINARY_PATH="$<TARGET_FILE:dasall-tui>"
    DASALL_TUI_LOGICAL_BINARY_PATH="$<TARGET_FILE_DIR:dasall-tui>/dasall-tui"
    DASALL_APPS_TUI_CMAKE="${CMAKE_SOURCE_DIR}/apps/tui/CMakeLists.txt"
    DASALL_APPS_TUI_MAIN="${CMAKE_SOURCE_DIR}/apps/tui/src/main.cpp"
)

target_compile_definitions(dasall_tui_socket_override_integration_test
  PRIVATE
    DASALL_TUI_INTEGRATION_LOCAL_CMAKE="${CMAKE_SOURCE_DIR}/tests/integration/tui/CMakeLists.txt"
    DASALL_APPS_TUI_MAIN="${CMAKE_SOURCE_DIR}/apps/tui/src/main.cpp"
)

add_dependencies(dasall_tui_entrypoint_smoke_integration_test dasall-tui)

dasall_apply_common_options(dasall_tui_integration_topology_smoke_integration_test)
dasall_apply_common_options(dasall_tui_app_startup_integration_test)
dasall_apply_common_options(dasall_tui_app_startup_failure_integration_test)
dasall_apply_common_options(dasall_tui_prototype_smoke_integration_test)
dasall_apply_common_options(dasall_tui_status_panel_integration_test)
dasall_apply_common_options(dasall_tui_session_lifecycle_integration_test)
dasall_apply_common_options(dasall_tui_next_preference_integration_test)
dasall_apply_common_options(dasall_tui_submit_turn_integration_test)
dasall_apply_common_options(dasall_tui_entrypoint_smoke_integration_test)
dasall_apply_common_options(dasall_tui_socket_override_integration_test)

add_test(NAME TuiTestTopologyDiscoverability COMMAND dasall_tui_integration_topology_smoke_integration_test)
set_tests_properties(TuiTestTopologyDiscoverability PROPERTIES LABELS "integration;tui")

add_test(NAME TuiAppStartupTest COMMAND dasall_tui_app_startup_integration_test)
set_tests_properties(TuiAppStartupTest PROPERTIES LABELS "integration;tui")

add_test(NAME TuiAppStartupFailureTest COMMAND dasall_tui_app_startup_failure_integration_test)
set_tests_properties(TuiAppStartupFailureTest PROPERTIES LABELS "integration;tui")

add_test(NAME TuiPrototypeSmokeTest COMMAND dasall_tui_prototype_smoke_integration_test)
set_tests_properties(TuiPrototypeSmokeTest PROPERTIES LABELS "integration;tui")

add_test(NAME TuiStatusPanelIntegrationTest COMMAND dasall_tui_status_panel_integration_test)
set_tests_properties(TuiStatusPanelIntegrationTest PROPERTIES LABELS "integration;tui")

add_test(NAME TuiSessionLifecycleIntegrationTest COMMAND dasall_tui_session_lifecycle_integration_test)
set_tests_properties(TuiSessionLifecycleIntegrationTest PROPERTIES LABELS "integration;tui")

add_test(NAME TuiNextPreferenceIntegrationTest COMMAND dasall_tui_next_preference_integration_test)
set_tests_properties(TuiNextPreferenceIntegrationTest PROPERTIES LABELS "integration;tui")

add_test(NAME TuiAppSubmitTurnIntegrationTest COMMAND dasall_tui_submit_turn_integration_test)
set_tests_properties(TuiAppSubmitTurnIntegrationTest PROPERTIES LABELS "integration;tui")

add_test(NAME DasallTuiEntrypointSmokeTest COMMAND dasall_tui_entrypoint_smoke_integration_test)
set_tests_properties(DasallTuiEntrypointSmokeTest PROPERTIES LABELS "integration;tui")

add_test(NAME DasallTuiSocketOverrideTest COMMAND dasall_tui_socket_override_integration_test)
set_tests_properties(DasallTuiSocketOverrideTest PROPERTIES LABELS "integration;tui")

add_test(NAME TuiPrototypeBuildSmokeTest
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target dasall_tui_prototype
)
set_tests_properties(TuiPrototypeBuildSmokeTest PROPERTIES LABELS "integration;tui")

list(APPEND DASALL_APPS_TUI_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_tui_integration_topology_smoke_integration_test
  dasall_tui_app_startup_integration_test
  dasall_tui_app_startup_failure_integration_test
  dasall_tui_prototype_smoke_integration_test
  dasall_tui_status_panel_integration_test
  dasall_tui_session_lifecycle_integration_test
  dasall_tui_next_preference_integration_test
  dasall_tui_submit_turn_integration_test
  dasall_tui_entrypoint_smoke_integration_test
  dasall_tui_socket_override_integration_test
)

set(DASALL_APPS_TUI_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_APPS_TUI_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)