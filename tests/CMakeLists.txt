add_subdirectory(mocks)
add_subdirectory(unit)
add_subdirectory(contract)
add_subdirectory(integration)

add_custom_target(dasall_unit_tests
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L unit
  DEPENDS ${DASALL_UNIT_TEST_EXECUTABLE_TARGETS}
)

add_custom_target(dasall_contract_tests
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L contract
  DEPENDS ${DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS}
)

add_custom_target(dasall_integration_tests
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L integration
  DEPENDS ${DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS}
)

add_custom_target(dasall_gate_int_03
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L gate-int-03
  DEPENDS
    dasall_contract_main_flow_e2e_test
    dasall_runtime_unary_integration_test
    dasall_cognition_runtime_integration_test
)

add_custom_target(dasall_gate_int_04
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L gate-int-04
  DEPENDS
    dasall_runtime_evidence_projection_integration_test
    dasall_knowledge_evidence_preservation_integration_test
)

add_custom_target(dasall_gate_int_05
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L gate-int-05
  DEPENDS
    dasall_infra_diagnostics_smoke_integration_test
    dasall_infra_diagnostics_integration_test
)

add_custom_target(dasall_gate_int_06
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L gate-int-06
  DEPENDS
    dasall_runtime_required_optional_ports_integration_test
    dasall_runtime_profile_compatibility_integration_test
    dasall_llm_smoke_integration_test
)

add_custom_target(dasall_gate_int_07
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L gate-int-07
  DEPENDS
    dasall_contract_service_result_semantics_test
    dasall_builtin_executor_lane_result_code_unit_test
)

add_custom_target(dasall_gate_int_08
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L gate-int-08
  DEPENDS
    dasall_contract_agent_request_test
    dasall_contract_agent_result_test
    dasall_contract_identity_metadata_test
    dasall_access_daemon_submit_composition_integration_test
    dasall_access_gateway_submit_composition_integration_test
    dasall_access_daemon_receipt_flow_integration_test
    dasall_access_observability_main_chain_integration_test
    dasall_access_health_readiness_integration_test
    dasall_access_daemon_profile_compatibility_integration_test
)

set(DASALL_PACKAGING_PREFLIGHT_TEST_NAMES
  CliJsonOutputContractTest
  CliExitCodeContractTest
  DaemonPingIntegrationTest
  CliDaemonSocketPathIntegrationTest
  DaemonBinaryUnarySmokeTest
)

string(JOIN "," DASALL_PACKAGING_PREFLIGHT_EXPECTED_TESTS_CSV
  ${DASALL_PACKAGING_PREFLIGHT_TEST_NAMES}
)

set(DASALL_PACKAGING_PREFLIGHT_EXECUTABLE_TARGETS
  dasall-cli_json_output_contract_test
  dasall-cli_exit_code_contract_test
  dasall_access_daemon_ping_integration_test
  dasall_access_cli_daemon_socket_path_integration_test
  dasall_access_daemon_binary_unary_smoke_integration_test
)

add_custom_target(dasall_packaging_preflight_tests
  COMMAND ${CMAKE_COMMAND}
    -DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}
    -DBUILD_DIR=${CMAKE_BINARY_DIR}
    -DEXPECTED_TESTS_CSV=${DASALL_PACKAGING_PREFLIGHT_EXPECTED_TESTS_CSV}
    -DDISCOVERABILITY_LABELS_CSV=release-preflight-gate
    -DRUN_ACCEPTANCE=ON
    -P ${CMAKE_SOURCE_DIR}/tests/VerifySystemGateDiscoverability.cmake
  DEPENDS ${DASALL_PACKAGING_PREFLIGHT_EXECUTABLE_TARGETS}
)

set(DASALL_GATE_INT_10_TEST_NAMES
  CliDaemonSocketPathIntegrationTest
  DaemonBinaryUnarySmokeTest
  GatewayBinaryUnarySmokeTest
  GatewayBinaryMissingBackendRegressionTest
)

string(JOIN "," DASALL_GATE_INT_10_EXPECTED_TESTS_CSV
  ${DASALL_GATE_INT_10_TEST_NAMES}
)

set(DASALL_GATE_INT_10_EXECUTABLE_TARGETS
  dasall_access_cli_daemon_socket_path_integration_test
  dasall_access_daemon_binary_unary_smoke_integration_test
  dasall_access_gateway_binary_unary_smoke_integration_test_bin
  dasall_access_gateway_binary_missing_backend_regression_test
)

add_custom_target(dasall_gate_int_10
  COMMAND ${CMAKE_COMMAND}
    -DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}
    -DBUILD_DIR=${CMAKE_BINARY_DIR}
    -DEXPECTED_TESTS_CSV=${DASALL_GATE_INT_10_EXPECTED_TESTS_CSV}
    -DDISCOVERABILITY_LABELS_CSV=gate-int-10,release-preflight-gate
    -DRUN_ACCEPTANCE=ON
    -P ${CMAKE_SOURCE_DIR}/tests/VerifySystemGateDiscoverability.cmake
  DEPENDS ${DASALL_GATE_INT_10_EXECUTABLE_TARGETS}
)

set(DASALL_FULL_BUSINESS_CHAIN_DISCOVERABILITY_TEST_NAMES
  CliDaemonSubmitIntegrationTest
  HttpGatewaySubmitIntegrationTest
  AccessGatewayPipelineIntegrationTest
  AccessPolicyBackendUnavailableIntegrationTest
  AccessAsyncReceiptQueryCancelIntegrationTest
  AccessHealthReadinessIntegrationTest
  AccessProfileCompatibilityTest
  DaemonPingIntegrationTest
  CliDaemonSocketPathIntegrationTest
  DaemonBinaryUnarySmokeTest
  GatewayBinaryUnarySmokeTest
  GatewayBinaryMissingBackendRegressionTest
  DaemonStartupDiagnosticsTest
  GatewayStartupDiagnosticsTest
  DaemonRuntimeLiveDependencyCompositionTest
  GatewayRuntimeLiveDependencyCompositionTest
  RuntimeUnaryIntegrationTest
  CognitionRuntimeIntegrationTest
  RuntimeRequiredOptionalPortsIntegrationTest
  RuntimeProfileCompatibilityTest
  RuntimeRecoveryContextIntegrationTest
  RuntimeCheckpointReplayRegressionTest
  RuntimeEvidenceProjectionIntegrationTest
  KnowledgeEvidencePreservationTest
  dasall_knowledge_retrieval_smoke_integration_test
  MemoryContextAssembleIntegrationTest
  MemoryWritebackIntegrationTest
  ToolServicesSmokeIntegrationTest
  CapabilityServicesSmokeIntegrationTest
  LLMSubsystemSmokeIntegrationTest
  ProfilesBuildRuntimeIntegrationTest
  InfraDiagnosticsSmokeTest
  InfraDiagnosticsIntegrationTest
  CliJsonOutputContractTest
  CliExitCodeContractTest
)

string(JOIN "," DASALL_FULL_BUSINESS_CHAIN_DISCOVERABILITY_EXPECTED_TESTS_CSV
  ${DASALL_FULL_BUSINESS_CHAIN_DISCOVERABILITY_TEST_NAMES}
)

set(DASALL_FULL_BUSINESS_CHAIN_DISCOVERABILITY_MISSING_GATES
  "BC-17:multi_agent_coordinator_runtime_gate_missing"
)

string(JOIN "," DASALL_FULL_BUSINESS_CHAIN_DISCOVERABILITY_MISSING_GATES_CSV
  ${DASALL_FULL_BUSINESS_CHAIN_DISCOVERABILITY_MISSING_GATES}
)

set(DASALL_FULL_BUSINESS_CHAIN_DISCOVERABILITY_EXECUTABLE_TARGETS
  dasall_access_daemon_submit_composition_integration_test
  dasall_access_gateway_submit_composition_integration_test
  dasall_access_observability_main_chain_integration_test
  dasall_access_daemon_receipt_flow_integration_test
  dasall_access_health_readiness_integration_test
  dasall_access_daemon_profile_compatibility_integration_test
  dasall_access_daemon_ping_integration_test
  dasall_access_cli_daemon_socket_path_integration_test
  dasall_access_daemon_binary_unary_smoke_integration_test
  dasall_access_gateway_binary_unary_smoke_integration_test_bin
  dasall_access_gateway_binary_missing_backend_regression_test
  dasall_access_daemon_startup_diagnostics_integration_test
  dasall_access_gateway_startup_diagnostics_integration_test
  dasall_access_daemon_runtime_live_dependency_composition_integration_test
  dasall_access_gateway_runtime_live_dependency_composition_integration_test
  dasall_runtime_unary_integration_test
  dasall_cognition_runtime_integration_test
  dasall_runtime_required_optional_ports_integration_test
  dasall_runtime_profile_compatibility_integration_test
  dasall_runtime_recovery_context_integration_test
  dasall_runtime_checkpoint_replay_regression_test
  dasall_runtime_evidence_projection_integration_test
  dasall_knowledge_evidence_preservation_integration_test
  dasall_knowledge_retrieval_smoke_integration_test
  dasall_memory_context_assemble_integration_test
  dasall_memory_writeback_integration_test
  dasall_tool_services_smoke_integration_test
  dasall_services_smoke_integration_test
  dasall_llm_smoke_integration_test
  dasall_profiles_build_runtime_integration_test
  dasall_infra_diagnostics_smoke_integration_test
  dasall_infra_diagnostics_integration_test
  dasall-cli_json_output_contract_test
  dasall-cli_exit_code_contract_test
)

add_custom_target(dasall_full_business_chain_discoverability
  COMMAND ${CMAKE_COMMAND}
    -DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}
    -DBUILD_DIR=${CMAKE_BINARY_DIR}
    -DEXPECTED_TESTS_CSV=${DASALL_FULL_BUSINESS_CHAIN_DISCOVERABILITY_EXPECTED_TESTS_CSV}
    -DEXPECTED_MISSING_GATES_CSV=${DASALL_FULL_BUSINESS_CHAIN_DISCOVERABILITY_MISSING_GATES_CSV}
    -P ${CMAKE_SOURCE_DIR}/tests/VerifySystemGateDiscoverability.cmake
  DEPENDS ${DASALL_FULL_BUSINESS_CHAIN_DISCOVERABILITY_EXECUTABLE_TARGETS}
)

set(DASALL_SYSTEM_GATE_ACCEPTANCE_TEST_NAMES
  ${DASALL_SYSTEM_GATE_ACCEPTANCE_CONTRACT_TEST_NAMES}
  BuiltinExecutorLaneResultCodeTest
  ${DASALL_SYSTEM_GATE_ACCEPTANCE_INTEGRATION_TEST_NAMES}
)

string(JOIN "|" DASALL_SYSTEM_GATE_ACCEPTANCE_TEST_REGEX_BODY
  ${DASALL_SYSTEM_GATE_ACCEPTANCE_TEST_NAMES}
)

string(JOIN "," DASALL_SYSTEM_GATE_ACCEPTANCE_EXPECTED_TESTS_CSV
  ${DASALL_SYSTEM_GATE_ACCEPTANCE_TEST_NAMES}
)

set(DASALL_SYSTEM_GATE_ACCEPTANCE_EXECUTABLE_TARGETS
  ${DASALL_SYSTEM_GATE_ACCEPTANCE_CONTRACT_TEST_EXECUTABLE_TARGETS}
  dasall_builtin_executor_lane_result_code_unit_test
  ${DASALL_SYSTEM_GATE_ACCEPTANCE_INTEGRATION_TEST_EXECUTABLE_TARGETS}
)

add_custom_target(dasall_gate_int_09
  COMMAND ${CMAKE_COMMAND}
    -DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}
    -DBUILD_DIR=${CMAKE_BINARY_DIR}
    -DEXPECTED_TESTS_CSV=${DASALL_SYSTEM_GATE_ACCEPTANCE_EXPECTED_TESTS_CSV}
    -DRUN_ACCEPTANCE=ON
    -P ${CMAKE_SOURCE_DIR}/tests/VerifySystemGateDiscoverability.cmake
  DEPENDS ${DASALL_SYSTEM_GATE_ACCEPTANCE_EXECUTABLE_TARGETS}
)

add_custom_target(dasall_access_tests
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L access
  DEPENDS
    dasall_access_unit_tests
    dasall_access_integration_tests
)

add_custom_target(dasall_wp04_contract_gate
  COMMAND ${CMAKE_COMMAND} -E env
    BUILD_DIR=${CMAKE_BINARY_DIR}
    WP04_GATE_SKIP_CONFIGURE=1
    WP04_GATE_SKIP_BUILD=1
    bash ${CMAKE_SOURCE_DIR}/scripts/ci/wp04_contract_gate.sh
  DEPENDS dasall_contract_tests
  USES_TERMINAL
)

# WP05 gate target reuses already-built contract tests and executes the
# dedicated M5/V1-ready gate script as a single CMake entrypoint.
add_custom_target(dasall_wp05_contract_gate
  COMMAND ${CMAKE_COMMAND} -E env
    BUILD_DIR=${CMAKE_BINARY_DIR}
    WP05_GATE_SKIP_CONFIGURE=1
    WP05_GATE_SKIP_BUILD=1
    bash ${CMAKE_SOURCE_DIR}/scripts/ci/wp05_contract_gate.sh
  DEPENDS dasall_contract_tests
  USES_TERMINAL
)

if(NOT DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "dasall_contract_tests requires at least one registered contract test target")
endif()

if(NOT DASALL_UNIT_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "dasall_unit_tests requires at least one registered unit test target")
endif()

if(NOT DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "dasall_integration_tests requires at least one registered integration test target")
endif()
