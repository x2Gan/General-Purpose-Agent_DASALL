set(DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS)

add_executable(dasall_access_gateway_smoke_integration_test
  AccessGatewaySmokeIntegrationTest.cpp
)

target_link_libraries(dasall_access_gateway_smoke_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_gateway_smoke_integration_test)

add_test(NAME AccessGatewaySmokeIntegrationTest COMMAND dasall_access_gateway_smoke_integration_test)
set_tests_properties(AccessGatewaySmokeIntegrationTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_gateway_smoke_integration_test
)

add_executable(dasall_access_daemon_ping_integration_test
  DaemonPingIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_daemon_ping_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_access_daemon_ping_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_ping_integration_test)

add_test(NAME DaemonPingIntegrationTest COMMAND dasall_access_daemon_ping_integration_test)
set_tests_properties(DaemonPingIntegrationTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_ping_integration_test
)

add_executable(dasall_access_daemon_unary_integration_test
  DaemonUnaryIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_daemon_unary_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_access_daemon_unary_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_unary_integration_test)

add_test(NAME DaemonUnaryIntegrationTest COMMAND dasall_access_daemon_unary_integration_test)
set_tests_properties(DaemonUnaryIntegrationTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_unary_integration_test
)

add_executable(dasall_access_daemon_reject_path_integration_test
  DaemonRejectPathIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_daemon_reject_path_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_access_daemon_reject_path_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_reject_path_integration_test)

add_test(NAME DaemonRejectPathIntegrationTest COMMAND dasall_access_daemon_reject_path_integration_test)
set_tests_properties(DaemonRejectPathIntegrationTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_reject_path_integration_test
)

add_executable(dasall_access_daemon_unary_runtime_bridge_integration_test
  DaemonUnaryRuntimeBridgeTest.cpp
)

target_include_directories(dasall_access_daemon_unary_runtime_bridge_integration_test
  PRIVATE
    ../../../cognition/include
)

target_link_libraries(dasall_access_daemon_unary_runtime_bridge_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_runtime
    dasall_cognition
)

dasall_apply_common_options(dasall_access_daemon_unary_runtime_bridge_integration_test)

add_test(NAME DaemonUnaryRuntimeBridgeTest COMMAND dasall_access_daemon_unary_runtime_bridge_integration_test)
set_tests_properties(DaemonUnaryRuntimeBridgeTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_unary_runtime_bridge_integration_test
)

add_executable(dasall_access_daemon_diag_deny_integration_test
  DaemonDiagDenyIntegrationTest.cpp
)

target_link_libraries(dasall_access_daemon_diag_deny_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
)

dasall_apply_common_options(dasall_access_daemon_diag_deny_integration_test)

add_test(NAME DaemonDiagDenyIntegrationTest COMMAND dasall_access_daemon_diag_deny_integration_test)
set_tests_properties(DaemonDiagDenyIntegrationTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_diag_deny_integration_test
)

if(NOT DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "access integration registration requires at least one test target")
endif()

add_custom_target(dasall_access_integration_tests
  DEPENDS ${DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS}
)

set(DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)
