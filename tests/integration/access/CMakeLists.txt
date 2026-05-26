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
set_tests_properties(DaemonPingIntegrationTest PROPERTIES LABELS "integration;access;release-preflight-gate")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_ping_integration_test
)

add_executable(dasall_access_daemon_socket_mode_integration_test
  DaemonSocketModeIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_daemon_socket_mode_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/src
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_access_daemon_socket_mode_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_socket_mode_integration_test)

add_test(NAME DaemonSocketModeIntegrationTest
  COMMAND dasall_access_daemon_socket_mode_integration_test)
set_tests_properties(DaemonSocketModeIntegrationTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_socket_mode_integration_test
)

add_executable(dasall_access_daemon_stale_socket_recovery_integration_test
  DaemonStaleSocketRecoveryIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_daemon_stale_socket_recovery_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/src
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_access_daemon_stale_socket_recovery_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_stale_socket_recovery_integration_test)

add_test(NAME DaemonStaleSocketRecoveryIntegrationTest
  COMMAND dasall_access_daemon_stale_socket_recovery_integration_test)
set_tests_properties(DaemonStaleSocketRecoveryIntegrationTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_stale_socket_recovery_integration_test
)

add_executable(dasall_access_cli_daemon_socket_path_integration_test
  CliDaemonSocketPathIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliCommandParser.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_cli_daemon_socket_path_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_compile_definitions(dasall_access_cli_daemon_socket_path_integration_test
  PRIVATE
    DASALL_CLI_BINARY_PATH="$<TARGET_FILE:dasall-cli>"
)

target_link_libraries(dasall_access_cli_daemon_socket_path_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

add_dependencies(dasall_access_cli_daemon_socket_path_integration_test
  dasall-cli
)

dasall_apply_common_options(dasall_access_cli_daemon_socket_path_integration_test)

add_test(NAME CliDaemonSocketPathIntegrationTest
  COMMAND dasall_access_cli_daemon_socket_path_integration_test)
set_tests_properties(CliDaemonSocketPathIntegrationTest PROPERTIES LABELS "integration;access;gate-int-10;release-preflight-gate")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_cli_daemon_socket_path_integration_test
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

add_executable(dasall_knowledge_runtime_refresh_trigger_integration_test
  KnowledgeRuntimeRefreshTriggerIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliCommandParser.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_knowledge_runtime_refresh_trigger_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_knowledge_runtime_refresh_trigger_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_knowledge
    dasall_platform
)

dasall_apply_common_options(dasall_knowledge_runtime_refresh_trigger_integration_test)

add_test(NAME KnowledgeRuntimeRefreshTriggerIntegrationTest
  COMMAND dasall_knowledge_runtime_refresh_trigger_integration_test)
set_tests_properties(KnowledgeRuntimeRefreshTriggerIntegrationTest PROPERTIES LABELS "integration;access;knowledge")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_knowledge_runtime_refresh_trigger_integration_test
)

add_executable(dasall_knowledge_runtime_query_surface_integration_test
  KnowledgeRuntimeQuerySurfaceIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliCommandParser.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_knowledge_runtime_query_surface_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_knowledge_runtime_query_surface_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_knowledge
    dasall_platform
)

dasall_apply_common_options(dasall_knowledge_runtime_query_surface_integration_test)

add_test(NAME KnowledgeRuntimeQuerySurfaceIntegrationTest
  COMMAND dasall_knowledge_runtime_query_surface_integration_test)
set_tests_properties(KnowledgeRuntimeQuerySurfaceIntegrationTest PROPERTIES LABELS "integration;access;knowledge")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_knowledge_runtime_query_surface_integration_test
)

add_executable(dasall_access_daemon_binary_unary_smoke_integration_test
  DaemonBinaryUnarySmokeTest.cpp
)

target_compile_definitions(dasall_access_daemon_binary_unary_smoke_integration_test
  PRIVATE
    DASALL_DAEMON_BINARY_PATH="$<TARGET_FILE:dasall-daemon>"
    DASALL_CLI_BINARY_PATH="$<TARGET_FILE:dasall-cli>"
    DASALL_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(dasall_access_daemon_binary_unary_smoke_integration_test
  PRIVATE
    dasall_test_support
)

add_dependencies(dasall_access_daemon_binary_unary_smoke_integration_test
  dasall-daemon
  dasall-cli
)

dasall_apply_common_options(dasall_access_daemon_binary_unary_smoke_integration_test)

add_test(NAME DaemonBinaryUnarySmokeTest
  COMMAND dasall_access_daemon_binary_unary_smoke_integration_test)
set_tests_properties(DaemonBinaryUnarySmokeTest PROPERTIES LABELS "integration;access;gate-int-10;release-preflight-gate")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_binary_unary_smoke_integration_test
)

add_executable(dasall_access_daemon_startup_diagnostics_integration_test
  DaemonStartupDiagnosticsTest.cpp
)

target_compile_definitions(dasall_access_daemon_startup_diagnostics_integration_test
  PRIVATE
    DASALL_DAEMON_BINARY_PATH="$<TARGET_FILE:dasall-daemon>"
    DASALL_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(dasall_access_daemon_startup_diagnostics_integration_test
  PRIVATE
    dasall_test_support
)

add_dependencies(dasall_access_daemon_startup_diagnostics_integration_test
  dasall-daemon
)

dasall_apply_common_options(dasall_access_daemon_startup_diagnostics_integration_test)

add_test(NAME DaemonStartupDiagnosticsTest
  COMMAND dasall_access_daemon_startup_diagnostics_integration_test)
set_tests_properties(DaemonStartupDiagnosticsTest PROPERTIES LABELS "integration;access;gate-int-05;diagnostics-retained-snapshot-gate;release-preflight-gate")

add_custom_target(dasall_access_daemon_startup_diagnostics_test
  COMMAND $<TARGET_FILE:dasall_access_daemon_startup_diagnostics_integration_test>
  DEPENDS dasall_access_daemon_startup_diagnostics_integration_test
  USES_TERMINAL
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_startup_diagnostics_integration_test
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

add_executable(dasall_access_daemon_submit_composition_integration_test
  DaemonAccessSubmitCompositionTest.cpp
)

target_link_libraries(dasall_access_daemon_submit_composition_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_daemon_submit_composition_integration_test)

add_test(NAME DaemonAccessSubmitCompositionTest
  COMMAND dasall_access_daemon_submit_composition_integration_test)
set_tests_properties(DaemonAccessSubmitCompositionTest PROPERTIES LABELS "integration;access")

add_test(NAME CliDaemonSubmitIntegrationTest
  COMMAND dasall_access_daemon_submit_composition_integration_test)
set_tests_properties(CliDaemonSubmitIntegrationTest PROPERTIES
  LABELS "integration;access;gate-int-08;access-v1-production-gate"
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_submit_composition_integration_test
)

add_executable(dasall_access_gateway_submit_composition_integration_test
  GatewayAccessSubmitCompositionTest.cpp
)

target_link_libraries(dasall_access_gateway_submit_composition_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_gateway_submit_composition_integration_test)

add_test(NAME GatewayAccessSubmitCompositionTest
  COMMAND dasall_access_gateway_submit_composition_integration_test)
set_tests_properties(GatewayAccessSubmitCompositionTest PROPERTIES LABELS "integration;access")

add_test(NAME HttpGatewaySubmitIntegrationTest
  COMMAND dasall_access_gateway_submit_composition_integration_test)
set_tests_properties(HttpGatewaySubmitIntegrationTest PROPERTIES
  LABELS "integration;access;gate-int-08;access-v1-production-gate"
)

add_test(NAME AccessGatewayPipelineIntegrationTest
  COMMAND dasall_access_gateway_submit_composition_integration_test)
set_tests_properties(AccessGatewayPipelineIntegrationTest PROPERTIES
  LABELS "integration;access;gate-int-08;access-v1-production-gate"
)

add_custom_target(dasall_access_gateway_submit_composition_test
  COMMAND $<TARGET_FILE:dasall_access_gateway_submit_composition_integration_test>
  DEPENDS dasall_access_gateway_submit_composition_integration_test
  USES_TERMINAL
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_gateway_submit_composition_integration_test
)

add_executable(dasall_access_gateway_binary_unary_smoke_integration_test_bin
  GatewayBinaryUnarySmokeTest.cpp
)

target_compile_definitions(dasall_access_gateway_binary_unary_smoke_integration_test_bin
  PRIVATE
    DASALL_GATEWAY_BINARY_PATH="$<TARGET_FILE:dasall_gateway>"
    DASALL_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(dasall_access_gateway_binary_unary_smoke_integration_test_bin
  PRIVATE
    dasall_test_support
    cpp-httplib
)

add_dependencies(dasall_access_gateway_binary_unary_smoke_integration_test_bin
  dasall_gateway
)

dasall_apply_common_options(dasall_access_gateway_binary_unary_smoke_integration_test_bin)

add_test(NAME GatewayBinaryUnarySmokeTest
  COMMAND dasall_access_gateway_binary_unary_smoke_integration_test_bin)
set_tests_properties(GatewayBinaryUnarySmokeTest PROPERTIES LABELS "integration;access;gate-int-10;release-preflight-gate")

add_executable(dasall_access_gateway_startup_diagnostics_integration_test
  GatewayStartupDiagnosticsTest.cpp
)

target_compile_definitions(dasall_access_gateway_startup_diagnostics_integration_test
  PRIVATE
    DASALL_GATEWAY_BINARY_PATH="$<TARGET_FILE:dasall_gateway>"
    DASALL_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(dasall_access_gateway_startup_diagnostics_integration_test
  PRIVATE
    dasall_test_support
)

add_dependencies(dasall_access_gateway_startup_diagnostics_integration_test
  dasall_gateway
)

dasall_apply_common_options(dasall_access_gateway_startup_diagnostics_integration_test)

add_test(NAME GatewayStartupDiagnosticsTest
  COMMAND dasall_access_gateway_startup_diagnostics_integration_test)
set_tests_properties(GatewayStartupDiagnosticsTest PROPERTIES LABELS "integration;access;gate-int-05;diagnostics-retained-snapshot-gate;release-preflight-gate")

add_custom_target(dasall_access_gateway_startup_diagnostics_test
  COMMAND $<TARGET_FILE:dasall_access_gateway_startup_diagnostics_integration_test>
  DEPENDS dasall_access_gateway_startup_diagnostics_integration_test
  USES_TERMINAL
)

add_executable(dasall_access_gateway_missing_backend_binary_fixture
  GatewayBinaryMissingBackendFixture.cpp
)

target_link_libraries(dasall_access_gateway_missing_backend_binary_fixture
  PRIVATE
    dasall_access
)

dasall_apply_common_options(dasall_access_gateway_missing_backend_binary_fixture)

add_executable(dasall_access_gateway_binary_missing_backend_regression_test
  GatewayBinaryMissingBackendRegressionTest.cpp
)

target_compile_definitions(dasall_access_gateway_binary_missing_backend_regression_test
  PRIVATE
    DASALL_GATEWAY_MISSING_BACKEND_BINARY_PATH="$<TARGET_FILE:dasall_access_gateway_missing_backend_binary_fixture>"
)

target_link_libraries(dasall_access_gateway_binary_missing_backend_regression_test
  PRIVATE
    dasall_test_support
)

add_dependencies(dasall_access_gateway_binary_missing_backend_regression_test
  dasall_access_gateway_missing_backend_binary_fixture
)

dasall_apply_common_options(dasall_access_gateway_binary_missing_backend_regression_test)

add_test(NAME GatewayBinaryMissingBackendRegressionTest
  COMMAND dasall_access_gateway_binary_missing_backend_regression_test)
set_tests_properties(GatewayBinaryMissingBackendRegressionTest PROPERTIES LABELS "integration;access;gate-int-10;release-preflight-gate")

add_custom_target(dasall_access_gateway_binary_unary_smoke_integration_test
  COMMAND $<TARGET_FILE:dasall_access_gateway_binary_unary_smoke_integration_test_bin>
  COMMAND $<TARGET_FILE:dasall_access_gateway_binary_missing_backend_regression_test>
  DEPENDS dasall_access_gateway_binary_unary_smoke_integration_test_bin
          dasall_access_gateway_binary_missing_backend_regression_test
  USES_TERMINAL
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_gateway_binary_unary_smoke_integration_test_bin
  dasall_access_gateway_binary_missing_backend_regression_test
)

add_executable(dasall_access_daemon_runtime_live_dependency_composition_integration_test
  DaemonRuntimeLiveDependencyCompositionTest.cpp
)

target_link_libraries(dasall_access_daemon_runtime_live_dependency_composition_integration_test
  PRIVATE
    dasall_test_support
    dasall_apps_runtime_support
    dasall_infra
    dasall_profiles
    dasall_runtime
    dasall_tools
)

target_compile_definitions(dasall_access_daemon_runtime_live_dependency_composition_integration_test
  PRIVATE
    DASALL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

dasall_apply_common_options(dasall_access_daemon_runtime_live_dependency_composition_integration_test)

add_test(NAME DaemonRuntimeLiveDependencyCompositionTest
  COMMAND dasall_access_daemon_runtime_live_dependency_composition_integration_test)
set_tests_properties(DaemonRuntimeLiveDependencyCompositionTest PROPERTIES LABELS "integration;access")

add_custom_target(dasall_access_daemon_runtime_live_dependency_composition_test
  COMMAND $<TARGET_FILE:dasall_access_daemon_runtime_live_dependency_composition_integration_test>
  DEPENDS dasall_access_daemon_runtime_live_dependency_composition_integration_test
  USES_TERMINAL
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_runtime_live_dependency_composition_integration_test
)

add_executable(dasall_access_gateway_runtime_live_dependency_composition_integration_test
  GatewayRuntimeLiveDependencyCompositionTest.cpp
)

target_link_libraries(dasall_access_gateway_runtime_live_dependency_composition_integration_test
  PRIVATE
    dasall_test_support
    dasall_apps_runtime_support
    dasall_infra
    dasall_profiles
    dasall_runtime
    dasall_tools
)

target_compile_definitions(dasall_access_gateway_runtime_live_dependency_composition_integration_test
  PRIVATE
    DASALL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

dasall_apply_common_options(dasall_access_gateway_runtime_live_dependency_composition_integration_test)

add_test(NAME GatewayRuntimeLiveDependencyCompositionTest
  COMMAND dasall_access_gateway_runtime_live_dependency_composition_integration_test)
set_tests_properties(GatewayRuntimeLiveDependencyCompositionTest PROPERTIES LABELS "integration;access")

add_custom_target(dasall_access_gateway_runtime_live_dependency_composition_test
  COMMAND $<TARGET_FILE:dasall_access_gateway_runtime_live_dependency_composition_integration_test>
  DEPENDS dasall_access_gateway_runtime_live_dependency_composition_integration_test
  USES_TERMINAL
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_gateway_runtime_live_dependency_composition_integration_test
)

add_executable(dasall_access_runtime_live_composition_failure_matrix_integration_test
  RuntimeLiveCompositionFailureMatrixTest.cpp
)

target_link_libraries(dasall_access_runtime_live_composition_failure_matrix_integration_test
  PRIVATE
    dasall_test_support
    dasall_apps_runtime_support
    dasall_infra
    dasall_knowledge
    dasall_platform
    dasall_profiles
    dasall_runtime
)

target_compile_definitions(dasall_access_runtime_live_composition_failure_matrix_integration_test
  PRIVATE
    DASALL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

dasall_apply_common_options(dasall_access_runtime_live_composition_failure_matrix_integration_test)

add_test(NAME RuntimeLiveCompositionFailureMatrixTest
  COMMAND dasall_access_runtime_live_composition_failure_matrix_integration_test)
set_tests_properties(RuntimeLiveCompositionFailureMatrixTest PROPERTIES LABELS "integration;access")

add_custom_target(dasall_access_runtime_live_composition_failure_matrix_test
  COMMAND $<TARGET_FILE:dasall_access_runtime_live_composition_failure_matrix_integration_test>
  DEPENDS dasall_access_runtime_live_composition_failure_matrix_integration_test
  USES_TERMINAL
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_runtime_live_composition_failure_matrix_integration_test
)

add_executable(dasall_runtime_knowledge_hybrid_canary_integration_test
  RuntimeKnowledgeHybridCanaryIntegrationTest.cpp
)

target_link_libraries(dasall_runtime_knowledge_hybrid_canary_integration_test
  PRIVATE
    dasall_test_support
    dasall_apps_runtime_support
    dasall_infra
    dasall_knowledge
    dasall_profiles
    dasall_runtime
)

target_compile_definitions(dasall_runtime_knowledge_hybrid_canary_integration_test
  PRIVATE
    DASALL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

dasall_apply_common_options(dasall_runtime_knowledge_hybrid_canary_integration_test)

add_test(NAME RuntimeKnowledgeHybridCanaryIntegrationTest
  COMMAND dasall_runtime_knowledge_hybrid_canary_integration_test)
set_tests_properties(RuntimeKnowledgeHybridCanaryIntegrationTest PROPERTIES LABELS "integration;access;knowledge")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_runtime_knowledge_hybrid_canary_integration_test
)

add_executable(dasall_runtime_knowledge_query_encoder_integration_test
  RuntimeKnowledgeQueryEncoderIntegrationTest.cpp
)

target_link_libraries(dasall_runtime_knowledge_query_encoder_integration_test
  PRIVATE
    dasall_test_support
    dasall_apps_runtime_support
    dasall_infra
    dasall_knowledge
    dasall_llm
    dasall_profiles
    dasall_runtime
)

target_compile_definitions(dasall_runtime_knowledge_query_encoder_integration_test
  PRIVATE
    DASALL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

dasall_apply_common_options(dasall_runtime_knowledge_query_encoder_integration_test)

add_test(NAME RuntimeKnowledgeQueryEncoderIntegrationTest
  COMMAND dasall_runtime_knowledge_query_encoder_integration_test)
set_tests_properties(RuntimeKnowledgeQueryEncoderIntegrationTest PROPERTIES LABELS "integration;access;knowledge")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_runtime_knowledge_query_encoder_integration_test
)

add_executable(dasall_access_runtime_production_health_composition_integration_test
  RuntimeProductionHealthCompositionTest.cpp
)

target_compile_definitions(dasall_access_runtime_production_health_composition_integration_test
  PRIVATE
    DASALL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(dasall_access_runtime_production_health_composition_integration_test
  PRIVATE
    dasall_test_support
    dasall_apps_runtime_support
    dasall_infra
    dasall_profiles
    dasall_runtime
    dasall_tools
)

dasall_apply_common_options(dasall_access_runtime_production_health_composition_integration_test)

add_test(NAME RuntimeProductionHealthCompositionTest
  COMMAND dasall_access_runtime_production_health_composition_integration_test)
set_tests_properties(RuntimeProductionHealthCompositionTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_runtime_production_health_composition_integration_test
)

add_executable(dasall_access_health_readiness_integration_test
  AccessHealthReadinessIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/gateway/src/HealthProbeHandler.cpp
)

target_include_directories(dasall_access_health_readiness_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/gateway/src
)

target_link_libraries(dasall_access_health_readiness_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_health_readiness_integration_test)

add_test(NAME AccessHealthReadinessIntegrationTest
  COMMAND dasall_access_health_readiness_integration_test)
set_tests_properties(AccessHealthReadinessIntegrationTest PROPERTIES
  LABELS "integration;access;gate-int-08;access-v1-production-gate"
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_health_readiness_integration_test
)

add_executable(dasall_access_policy_evaluator_integration_test
  AccessPolicyEvaluatorIntegrationTest.cpp
)

target_link_libraries(dasall_access_policy_evaluator_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_policy_evaluator_integration_test)

add_test(NAME AccessPolicyEvaluatorIntegrationTest
  COMMAND dasall_access_policy_evaluator_integration_test)
set_tests_properties(AccessPolicyEvaluatorIntegrationTest PROPERTIES
  LABELS "integration;access;gate-int-08;access-v1-production-gate"
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_policy_evaluator_integration_test
)

add_executable(dasall_access_observability_main_chain_integration_test
  AccessObservabilityMainChainIntegrationTest.cpp
)

target_link_libraries(dasall_access_observability_main_chain_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_observability_main_chain_integration_test)

add_test(NAME AccessObservabilityMainChainIntegrationTest
  COMMAND dasall_access_observability_main_chain_integration_test)
set_tests_properties(AccessObservabilityMainChainIntegrationTest PROPERTIES
  LABELS "integration;access;gate-int-08;access-v1-production-gate"
)

add_test(NAME AccessPolicyBackendUnavailableIntegrationTest
  COMMAND dasall_access_observability_main_chain_integration_test)
set_tests_properties(AccessPolicyBackendUnavailableIntegrationTest PROPERTIES
  LABELS "integration;access;gate-int-08;access-v1-production-gate"
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_observability_main_chain_integration_test
)

add_executable(dasall_access_reject_trace_anchor_integration_test
  AccessRejectTraceAnchorTest.cpp
)

target_link_libraries(dasall_access_reject_trace_anchor_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_reject_trace_anchor_integration_test)

add_test(NAME AccessRejectTraceAnchorTest
  COMMAND dasall_access_reject_trace_anchor_integration_test)
set_tests_properties(AccessRejectTraceAnchorTest PROPERTIES
  LABELS "integration;access;gate-int-08;access-v1-production-gate"
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_reject_trace_anchor_integration_test
)

add_executable(dasall_access_publish_failure_audit_integration_test
  AccessPublishFailureAuditTest.cpp
)

target_link_libraries(dasall_access_publish_failure_audit_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_publish_failure_audit_integration_test)

add_test(NAME AccessPublishFailureAuditTest
  COMMAND dasall_access_publish_failure_audit_integration_test)
set_tests_properties(AccessPublishFailureAuditTest PROPERTIES
  LABELS "integration;access;gate-int-08;access-v1-production-gate"
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_publish_failure_audit_integration_test
)

add_executable(dasall_access_daemon_receipt_flow_integration_test
  DaemonReceiptFlowIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_daemon_receipt_flow_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/src
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_compile_definitions(dasall_access_daemon_receipt_flow_integration_test
  PRIVATE
    DASALL_CLI_BINARY_PATH="$<TARGET_FILE:dasall-cli>"
)

target_link_libraries(dasall_access_daemon_receipt_flow_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

add_dependencies(dasall_access_daemon_receipt_flow_integration_test
  dasall-cli
)

dasall_apply_common_options(dasall_access_daemon_receipt_flow_integration_test)

add_test(NAME DaemonReceiptFlowIntegrationTest COMMAND dasall_access_daemon_receipt_flow_integration_test)
set_tests_properties(DaemonReceiptFlowIntegrationTest PROPERTIES LABELS "integration;access")

add_test(NAME AccessAsyncReceiptQueryCancelIntegrationTest
  COMMAND dasall_access_daemon_receipt_flow_integration_test)
set_tests_properties(AccessAsyncReceiptQueryCancelIntegrationTest PROPERTIES
  LABELS "integration;access;gate-int-08;access-v1-production-gate"
)

add_test(NAME DaemonSubmitQueryCancelIntegrationTest
  COMMAND dasall_access_daemon_receipt_flow_integration_test)
set_tests_properties(DaemonSubmitQueryCancelIntegrationTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_receipt_flow_integration_test
)

add_executable(dasall_access_daemon_failure_injection_integration_test
  DaemonFailureInjectionIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_daemon_failure_injection_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_access_daemon_failure_injection_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_failure_injection_integration_test)

add_test(NAME DaemonFailureInjectionTest COMMAND dasall_access_daemon_failure_injection_integration_test)
set_tests_properties(DaemonFailureInjectionTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_failure_injection_integration_test
)

add_executable(dasall_access_daemon_profile_compatibility_integration_test
  DaemonProfileCompatibilityTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonEntryConfigLoader.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_daemon_profile_compatibility_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_access_daemon_profile_compatibility_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
    dasall_profiles
)

dasall_apply_common_options(dasall_access_daemon_profile_compatibility_integration_test)

add_test(NAME DaemonProfileCompatibilityTest COMMAND dasall_access_daemon_profile_compatibility_integration_test)
set_tests_properties(DaemonProfileCompatibilityTest PROPERTIES LABELS "integration;access;diagnostics-admin-boundary")

add_test(NAME AccessProfileCompatibilityTest
  COMMAND dasall_access_daemon_profile_compatibility_integration_test)
set_tests_properties(AccessProfileCompatibilityTest PROPERTIES
  LABELS "integration;access;gate-int-08;access-v1-production-gate"
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_profile_compatibility_integration_test
)

add_executable(dasall_access_daemon_backpressure_integration_test
  DaemonBackpressureIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_daemon_backpressure_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/src
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_access_daemon_backpressure_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_backpressure_integration_test)

add_test(NAME DaemonBackpressureIntegrationTest COMMAND dasall_access_daemon_backpressure_integration_test)
set_tests_properties(DaemonBackpressureIntegrationTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_backpressure_integration_test
)

add_executable(dasall_access_daemon_soak_integration_test
  DaemonSoakIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_daemon_soak_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/src
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_access_daemon_soak_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_soak_integration_test)

add_test(NAME DaemonSoakIntegrationTest COMMAND dasall_access_daemon_soak_integration_test)
set_tests_properties(DaemonSoakIntegrationTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_soak_integration_test
)

add_executable(dasall_access_daemon_receipt_ttl_cleanup_integration_test
  DaemonReceiptTtlCleanupIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_daemon_receipt_ttl_cleanup_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/src
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_access_daemon_receipt_ttl_cleanup_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_receipt_ttl_cleanup_integration_test)

add_test(NAME DaemonReceiptTtlCleanupIntegrationTest COMMAND dasall_access_daemon_receipt_ttl_cleanup_integration_test)
set_tests_properties(DaemonReceiptTtlCleanupIntegrationTest PROPERTIES LABELS "integration;access")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_receipt_ttl_cleanup_integration_test
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
  ${CMAKE_SOURCE_DIR}/apps/cli/src/CliIpcClient.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonLifecycleController.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonListenerHost.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSocketPolicy.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonSupervisorAdapter.cpp
)

target_include_directories(dasall_access_daemon_diag_deny_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/cli/src
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_access_daemon_diag_deny_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_diag_deny_integration_test)

add_test(NAME DaemonDiagDenyIntegrationTest COMMAND dasall_access_daemon_diag_deny_integration_test)
set_tests_properties(DaemonDiagDenyIntegrationTest PROPERTIES
  LABELS "integration;access;gate-int-08;access-v1-production-gate;diagnostics-admin-boundary"
)

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_diag_deny_integration_test
)

add_executable(dasall_access_daemon_hot_reload_integration_test
  DaemonHotReloadIntegrationTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonConfigReloader.cpp
  ${CMAKE_SOURCE_DIR}/apps/daemon/src/DaemonEntryConfigLoader.cpp
)

target_include_directories(dasall_access_daemon_hot_reload_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/apps/daemon/src
)

target_link_libraries(dasall_access_daemon_hot_reload_integration_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_infra
    dasall_profiles
)

dasall_apply_common_options(dasall_access_daemon_hot_reload_integration_test)

add_test(NAME DaemonHotReloadIntegrationTest COMMAND dasall_access_daemon_hot_reload_integration_test)
set_tests_properties(DaemonHotReloadIntegrationTest PROPERTIES LABELS "integration;access;diagnostics-admin-boundary")

list(APPEND DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_hot_reload_integration_test
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
