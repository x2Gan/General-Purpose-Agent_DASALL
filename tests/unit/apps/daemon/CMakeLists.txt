set(DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS)

include_directories(${CMAKE_SOURCE_DIR}/access/include)

add_executable(dasall-daemon_signal_handler_unit_test
  DaemonSignalHandlerTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSignalHandler.cpp
)

target_include_directories(dasall-daemon_signal_handler_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_signal_handler_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-daemon_signal_handler_unit_test)

add_test(NAME DaemonSignalHandlerTest
  COMMAND dasall-daemon_signal_handler_unit_test
)
set_tests_properties(DaemonSignalHandlerTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_signal_handler_unit_test
)

add_executable(dasall-daemon_loopback_fixture_unit_test
  DaemonLoopbackFixtureTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall-daemon_loopback_fixture_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_loopback_fixture_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

dasall_apply_common_options(dasall-daemon_loopback_fixture_unit_test)

add_test(NAME DaemonLoopbackFixtureTest
  COMMAND dasall-daemon_loopback_fixture_unit_test
)
set_tests_properties(DaemonLoopbackFixtureTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_loopback_fixture_unit_test
)

add_executable(dasall-daemon_listener_host_unit_test
  DaemonListenerHostTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
)

target_include_directories(dasall-daemon_listener_host_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_listener_host_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall-daemon_listener_host_unit_test)

add_test(NAME DaemonListenerHostTest
  COMMAND dasall-daemon_listener_host_unit_test
)
set_tests_properties(DaemonListenerHostTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_listener_host_unit_test
)

add_executable(dasall-daemon_listener_host_bind_conflict_unit_test
  DaemonListenerHostBindConflictTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
)

target_include_directories(dasall-daemon_listener_host_bind_conflict_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_listener_host_bind_conflict_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall-daemon_listener_host_bind_conflict_unit_test)

add_test(NAME DaemonListenerHostBindConflictTest
  COMMAND dasall-daemon_listener_host_bind_conflict_unit_test
)
set_tests_properties(DaemonListenerHostBindConflictTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_listener_host_bind_conflict_unit_test
)

add_executable(dasall-daemon_supervisor_adapter_unit_test
  DaemonSupervisorAdapterTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall-daemon_supervisor_adapter_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_supervisor_adapter_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall-daemon_supervisor_adapter_unit_test)

add_test(NAME DaemonSupervisorAdapterTest
  COMMAND dasall-daemon_supervisor_adapter_unit_test
)
set_tests_properties(DaemonSupervisorAdapterTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_supervisor_adapter_unit_test
)

add_executable(dasall-daemon_bootstrap_unit_test
  DaemonBootstrapTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall-daemon_bootstrap_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_bootstrap_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_platform
    dasall_infra
)

dasall_apply_common_options(dasall-daemon_bootstrap_unit_test)

add_test(NAME DaemonBootstrapTest
  COMMAND dasall-daemon_bootstrap_unit_test
)
set_tests_properties(DaemonBootstrapTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_bootstrap_unit_test
)

add_executable(dasall-daemon_graceful_shutdown_unit_test
  DaemonGracefulShutdownTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall-daemon_graceful_shutdown_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_graceful_shutdown_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_platform
    dasall_infra
)

dasall_apply_common_options(dasall-daemon_graceful_shutdown_unit_test)

add_test(NAME DaemonGracefulShutdownTest
  COMMAND dasall-daemon_graceful_shutdown_unit_test
)
set_tests_properties(DaemonGracefulShutdownTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_graceful_shutdown_unit_test
)

add_executable(dasall-daemon_lifecycle_controller_unit_test
  DaemonLifecycleControllerTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
)

target_include_directories(dasall-daemon_lifecycle_controller_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_lifecycle_controller_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-daemon_lifecycle_controller_unit_test)

add_test(NAME DaemonLifecycleControllerTest
  COMMAND dasall-daemon_lifecycle_controller_unit_test
)
set_tests_properties(DaemonLifecycleControllerTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_lifecycle_controller_unit_test
)

add_executable(dasall-daemon_bootstrap_config_unit_test
  DaemonBootstrapConfigTest.cpp
)

target_include_directories(dasall-daemon_bootstrap_config_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_bootstrap_config_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-daemon_bootstrap_config_unit_test)

add_test(NAME DaemonBootstrapConfigTest
  COMMAND dasall-daemon_bootstrap_config_unit_test
)
set_tests_properties(DaemonBootstrapConfigTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_bootstrap_config_unit_test
)

add_executable(dasall_daemon_installed_asset_root_unit_test
  DaemonInstalledAssetRootTest.cpp
)

target_link_libraries(dasall_daemon_installed_asset_root_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_daemon_installed_asset_root_unit_test)

add_test(NAME DaemonInstalledAssetRootTest
  COMMAND dasall_daemon_installed_asset_root_unit_test
)
set_tests_properties(DaemonInstalledAssetRootTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_daemon_installed_asset_root_unit_test
)

add_executable(dasall-daemon_entry_config_projection_unit_test
  DaemonEntryConfigProjectionTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonEntryConfigLoader.cpp
)

target_include_directories(dasall-daemon_entry_config_projection_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_entry_config_projection_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall-daemon_entry_config_projection_unit_test)

add_test(NAME DaemonEntryConfigProjectionTest
  COMMAND dasall-daemon_entry_config_projection_unit_test
)
set_tests_properties(DaemonEntryConfigProjectionTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_entry_config_projection_unit_test
)

add_executable(dasall-daemon_config_reload_unit_test
  DaemonConfigReloadTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonConfigReloader.cpp
)

target_include_directories(dasall-daemon_config_reload_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_config_reload_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall-daemon_config_reload_unit_test)

add_test(NAME DaemonConfigReloadTest
  COMMAND dasall-daemon_config_reload_unit_test
)
set_tests_properties(DaemonConfigReloadTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_config_reload_unit_test
)

add_executable(dasall-daemon_config_validator_unit_test
  DaemonConfigValidatorTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonConfigValidator.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
)

target_include_directories(dasall-daemon_config_validator_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_config_validator_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall-daemon_config_validator_unit_test)

add_test(NAME DaemonConfigValidatorTest
  COMMAND dasall-daemon_config_validator_unit_test
)
set_tests_properties(DaemonConfigValidatorTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_config_validator_unit_test
)

add_executable(dasall-daemon_socket_policy_unit_test
  DaemonSocketPolicyTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
)

target_include_directories(dasall-daemon_socket_policy_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_socket_policy_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall-daemon_socket_policy_unit_test)

add_test(NAME DaemonSocketPolicyTest
  COMMAND dasall-daemon_socket_policy_unit_test
)
set_tests_properties(DaemonSocketPolicyTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_socket_policy_unit_test
)

add_executable(dasall-daemon_access_pipeline_factory_unit_test
  DaemonAccessPipelineFactoryTest.cpp
)

target_link_libraries(dasall-daemon_access_pipeline_factory_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_knowledge
)

dasall_apply_common_options(dasall-daemon_access_pipeline_factory_unit_test)

add_test(NAME DaemonAccessPipelineFactoryTest
  COMMAND dasall-daemon_access_pipeline_factory_unit_test
)
set_tests_properties(DaemonAccessPipelineFactoryTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_access_pipeline_factory_unit_test
)

add_executable(dasall-daemon_memory_maintenance_proof_runner_unit_test
  MemoryMaintenanceProofRunnerTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/MemoryMaintenanceProofRunner.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonEntryConfigLoader.cpp
)

target_include_directories(dasall-daemon_memory_maintenance_proof_runner_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_memory_maintenance_proof_runner_unit_test
  PRIVATE
    dasall_test_support
    dasall_memory
    dasall_profiles
    dasall_infra
)

dasall_apply_common_options(dasall-daemon_memory_maintenance_proof_runner_unit_test)

add_test(NAME MemoryMaintenanceProofRunnerTest
  COMMAND dasall-daemon_memory_maintenance_proof_runner_unit_test
)
set_tests_properties(MemoryMaintenanceProofRunnerTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_memory_maintenance_proof_runner_unit_test
)

add_executable(dasall-daemon_memory_installed_proof_runner_unit_test
  MemoryInstalledProofRunnerTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/MemoryInstalledProofRunner.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonEntryConfigLoader.cpp
)

target_include_directories(dasall-daemon_memory_installed_proof_runner_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_memory_installed_proof_runner_unit_test
  PRIVATE
    dasall_test_support
    dasall_memory
    dasall_profiles
    dasall_infra
)

dasall_apply_common_options(dasall-daemon_memory_installed_proof_runner_unit_test)

add_test(NAME MemoryInstalledProofRunnerTest
  COMMAND dasall-daemon_memory_installed_proof_runner_unit_test
)
set_tests_properties(MemoryInstalledProofRunnerTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_memory_installed_proof_runner_unit_test
)

add_executable(dasall-daemon_tools_installed_proof_runner_unit_test
  ToolsInstalledProofRunnerTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/ToolsInstalledProofRunner.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonEntryConfigLoader.cpp
)

target_include_directories(dasall-daemon_tools_installed_proof_runner_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall-daemon_tools_installed_proof_runner_unit_test
  PRIVATE
    dasall_test_support
    dasall_apps_runtime_support
    dasall_runtime
    dasall_tools
    dasall_services
    dasall_profiles
    dasall_infra
    dasall_contracts
)

dasall_apply_common_options(dasall-daemon_tools_installed_proof_runner_unit_test)

add_test(NAME ToolsInstalledProofRunnerTest
  COMMAND dasall-daemon_tools_installed_proof_runner_unit_test
)
set_tests_properties(ToolsInstalledProofRunnerTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_tools_installed_proof_runner_unit_test
)

add_executable(dasall-daemon_runtime_installed_proof_runner_unit_test
  RuntimeInstalledProofRunnerTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/RuntimeInstalledProofRunner.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonEntryConfigLoader.cpp
)

target_include_directories(dasall-daemon_runtime_installed_proof_runner_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
    ${CMAKE_SOURCE_DIR}/cognition/include
    ${CMAKE_SOURCE_DIR}/memory/include
    ${CMAKE_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall-daemon_runtime_installed_proof_runner_unit_test
  PRIVATE
    dasall_test_support
    dasall_apps_runtime_support
    dasall_cognition
    dasall_runtime
    dasall_tools
    dasall_profiles
    dasall_infra
    dasall_contracts
)

dasall_apply_common_options(dasall-daemon_runtime_installed_proof_runner_unit_test)

add_test(NAME RuntimeInstalledProofRunnerTest
  COMMAND dasall-daemon_runtime_installed_proof_runner_unit_test
)
set_tests_properties(RuntimeInstalledProofRunnerTest PROPERTIES LABELS "unit;daemon")

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_runtime_installed_proof_runner_unit_test
)

add_executable(dasall-daemon_memory_maintenance_ticker_unit_test
  MemoryMaintenanceTickerThreadTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/MemoryMaintenanceTickerThread.cpp
)

target_include_directories(dasall-daemon_memory_maintenance_ticker_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
    ${CMAKE_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall-daemon_memory_maintenance_ticker_unit_test
  PRIVATE
    dasall_test_support
    dasall_memory
    dasall_runtime
    dasall_infra
)

dasall_apply_common_options(dasall-daemon_memory_maintenance_ticker_unit_test)

add_test(NAME MemoryMaintenanceTickerCadenceTest
  COMMAND dasall-daemon_memory_maintenance_ticker_unit_test cadence
)
add_test(NAME MemoryMaintenanceTickerFailureBackoffTest
  COMMAND dasall-daemon_memory_maintenance_ticker_unit_test failure_backoff
)
set_tests_properties(
  MemoryMaintenanceTickerCadenceTest
  MemoryMaintenanceTickerFailureBackoffTest
  PROPERTIES LABELS "unit;daemon"
)

list(APPEND DASALL_APPS_DAEMON_UNIT_TEST_EXECUTABLE_TARGETS
  dasall-daemon_memory_maintenance_ticker_unit_test
)