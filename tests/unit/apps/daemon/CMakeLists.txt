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
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_daemon_loopback_fixture_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_loopback_fixture_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
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

add_executable(dasall_daemon_listener_host_unit_test
  DaemonListenerHostTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
)

target_include_directories(dasall_daemon_listener_host_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_listener_host_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_daemon_listener_host_unit_test)

add_test(NAME DaemonListenerHostTest
  COMMAND dasall_daemon_listener_host_unit_test
)
set_tests_properties(DaemonListenerHostTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_listener_host_unit_test
)

add_executable(dasall_daemon_listener_host_bind_conflict_unit_test
  DaemonListenerHostBindConflictTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
)

target_include_directories(dasall_daemon_listener_host_bind_conflict_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_listener_host_bind_conflict_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_daemon_listener_host_bind_conflict_unit_test)

add_test(NAME DaemonListenerHostBindConflictTest
  COMMAND dasall_daemon_listener_host_bind_conflict_unit_test
)
set_tests_properties(DaemonListenerHostBindConflictTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_listener_host_bind_conflict_unit_test
)

add_executable(dasall_daemon_supervisor_adapter_unit_test
  DaemonSupervisorAdapterTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_daemon_supervisor_adapter_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_supervisor_adapter_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_daemon_supervisor_adapter_unit_test)

add_test(NAME DaemonSupervisorAdapterTest
  COMMAND dasall_daemon_supervisor_adapter_unit_test
)
set_tests_properties(DaemonSupervisorAdapterTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_supervisor_adapter_unit_test
)

add_executable(dasall_daemon_bootstrap_unit_test
  DaemonBootstrapTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_daemon_bootstrap_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_bootstrap_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_platform
    dasall_infra
)

dasall_apply_common_options(dasall_daemon_bootstrap_unit_test)

add_test(NAME DaemonBootstrapTest
  COMMAND dasall_daemon_bootstrap_unit_test
)
set_tests_properties(DaemonBootstrapTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_bootstrap_unit_test
)

add_executable(dasall_daemon_graceful_shutdown_unit_test
  DaemonGracefulShutdownTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_daemon_graceful_shutdown_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_graceful_shutdown_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_platform
    dasall_infra
)

dasall_apply_common_options(dasall_daemon_graceful_shutdown_unit_test)

add_test(NAME DaemonGracefulShutdownTest
  COMMAND dasall_daemon_graceful_shutdown_unit_test
)
set_tests_properties(DaemonGracefulShutdownTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_graceful_shutdown_unit_test
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

add_executable(dasall_daemon_entry_config_projection_unit_test
  DaemonEntryConfigProjectionTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonEntryConfigLoader.cpp
)

target_include_directories(dasall_daemon_entry_config_projection_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_entry_config_projection_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall_daemon_entry_config_projection_unit_test)

add_test(NAME DaemonEntryConfigProjectionTest
  COMMAND dasall_daemon_entry_config_projection_unit_test
)
set_tests_properties(DaemonEntryConfigProjectionTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_entry_config_projection_unit_test
)

add_executable(dasall_daemon_config_reload_unit_test
  DaemonConfigReloadTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonConfigReloader.cpp
)

target_include_directories(dasall_daemon_config_reload_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_config_reload_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall_daemon_config_reload_unit_test)

add_test(NAME DaemonConfigReloadTest
  COMMAND dasall_daemon_config_reload_unit_test
)
set_tests_properties(DaemonConfigReloadTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_config_reload_unit_test
)

add_executable(dasall_daemon_config_validator_unit_test
  DaemonConfigValidatorTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonConfigValidator.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
)

target_include_directories(dasall_daemon_config_validator_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_config_validator_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_daemon_config_validator_unit_test)

add_test(NAME DaemonConfigValidatorTest
  COMMAND dasall_daemon_config_validator_unit_test
)
set_tests_properties(DaemonConfigValidatorTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_config_validator_unit_test
)

add_executable(dasall_daemon_socket_policy_unit_test
  DaemonSocketPolicyTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
)

target_include_directories(dasall_daemon_socket_policy_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_daemon_socket_policy_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_daemon_socket_policy_unit_test)

add_test(NAME DaemonSocketPolicyTest
  COMMAND dasall_daemon_socket_policy_unit_test
)
set_tests_properties(DaemonSocketPolicyTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_socket_policy_unit_test
)

add_executable(dasall_daemon_access_pipeline_factory_unit_test
  DaemonAccessPipelineFactoryTest.cpp
)

target_link_libraries(dasall_daemon_access_pipeline_factory_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_daemon_access_pipeline_factory_unit_test)

add_test(NAME DaemonAccessPipelineFactoryTest
  COMMAND dasall_daemon_access_pipeline_factory_unit_test
)
set_tests_properties(DaemonAccessPipelineFactoryTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_access_pipeline_factory_unit_test
)