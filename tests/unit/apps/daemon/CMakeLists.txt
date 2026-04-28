set(DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS)

add_executable(dasall_daemon_signal_handler_unit_test
  DaemonSignalHandlerTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSignalHandler.cpp
)

target_include_directories(dasall_daemon_signal_handler_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_signal_handler_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall_daemon_signal_handler_unit_test)

add_test(NAME DaemonSignalHandlerTest
  COMMAND dasall_daemon_signal_handler_unit_test
)
set_tests_properties(DaemonSignalHandlerTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_signal_handler_unit_test
)

add_executable(dasall_daemon_loopback_fixture_unit_test
  DaemonLoopbackFixtureTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
)

target_include_directories(dasall_daemon_loopback_fixture_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_loopback_fixture_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_platform
)

dasall_apply_common_options(dasall_daemon_loopback_fixture_unit_test)

add_test(NAME DaemonLoopbackFixtureTest
  COMMAND dasall_daemon_loopback_fixture_unit_test
)
set_tests_properties(DaemonLoopbackFixtureTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_loopback_fixture_unit_test
)

add_executable(dasall_daemon_lifecycle_controller_unit_test
  DaemonLifecycleControllerTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
)

target_include_directories(dasall_daemon_lifecycle_controller_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_lifecycle_controller_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall_daemon_lifecycle_controller_unit_test)

add_test(NAME DaemonLifecycleControllerTest
  COMMAND dasall_daemon_lifecycle_controller_unit_test
)
set_tests_properties(DaemonLifecycleControllerTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_lifecycle_controller_unit_test
)

add_executable(dasall_daemon_bootstrap_config_unit_test
  DaemonBootstrapConfigTest.cpp
)

target_include_directories(dasall_daemon_bootstrap_config_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_bootstrap_config_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall_daemon_bootstrap_config_unit_test)

add_test(NAME DaemonBootstrapConfigTest
  COMMAND dasall_daemon_bootstrap_config_unit_test
)
set_tests_properties(DaemonBootstrapConfigTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_bootstrap_config_unit_test
)

add_executable(dasall_daemon_config_validator_unit_test
  DaemonConfigValidatorTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonConfigValidator.cpp
)

target_include_directories(dasall_daemon_config_validator_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_config_validator_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall_daemon_config_validator_unit_test)

add_test(NAME DaemonConfigValidatorTest
  COMMAND dasall_daemon_config_validator_unit_test
)
set_tests_properties(DaemonConfigValidatorTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_config_validator_unit_test
)