set(DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS)

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