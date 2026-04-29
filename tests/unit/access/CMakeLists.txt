set(DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_access_unit_test target_name test_name source_file)
  add_executable(${target_name}
    ${source_file}
  )

  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/access/src
  )

  target_link_libraries(${target_name}
    PRIVATE
      dasall_test_support
      dasall_access
  )

  dasall_apply_common_options(${target_name})

  add_test(NAME ${test_name} COMMAND ${target_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "unit;access")

  list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
    ${target_name}
  )
endmacro()

dasall_add_access_unit_test(
  dasall_access_interface_surface_unit_test
  AccessInterfaceSurfaceTest
  AccessInterfaceSurfaceTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_error_code_unit_test
  AccessErrorCodeTest
  AccessErrorCodeTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_error_mapping_unit_test
  AccessErrorMappingTest
  AccessErrorMappingTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_gateway_lifecycle_unit_test
  AccessGatewayLifecycleTest
  AccessGatewayLifecycleTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_supporting_types_unit_test
  AccessSupportingTypesTest
  AccessSupportingTypesTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_publish_envelope_unit_test
  PublishEnvelopeTypesTest
  PublishEnvelopeTypesTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_async_task_receipt_unit_test
  AsyncTaskReceiptTypesTest
  AsyncTaskReceiptTypesTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_admission_controller_surface_unit_test
  AdmissionControllerSurfaceTest
  AdmissionControllerSurfaceTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_runtime_bridge_surface_unit_test
  RuntimeBridgeSurfaceTest
  RuntimeBridgeSurfaceTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_config_projection_unit_test
  AccessConfigProjectionTest
  AccessConfigProjectionTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_protocol_adapter_registry_unit_test
  ProtocolAdapterRegistryTest
  ProtocolAdapterRegistryTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_protocol_adapter_registry_conflict_unit_test
  ProtocolAdapterRegistryConflictTest
  ProtocolAdapterRegistryConflictTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_subject_resolver_unit_test
  SubjectResolverTest
  SubjectResolverTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_subject_resolver_local_trusted_unit_test
  SubjectResolverLocalTrustedTest
  SubjectResolverLocalTrustedTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_daemon_peer_identity_fail_closed_unit_test
  DaemonPeerIdentityFailClosedTest
  DaemonPeerIdentityFailClosedTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_subject_resolver_challenge_unit_test
  SubjectResolverChallengeTest
  SubjectResolverChallengeTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_authenticator_chain_unit_test
  AuthenticatorChainTest
  AuthenticatorChainTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_authenticator_chain_challenge_unit_test
  AuthenticatorChainChallengeTest
  AuthenticatorChainChallengeTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_authenticator_chain_secret_failure_unit_test
  AuthenticatorChainSecretFailureTest
  AuthenticatorChainSecretFailureTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_policy_gate_unit_test
  AccessPolicyGateTest
  AccessPolicyGateTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_policy_override_gate_unit_test
  AccessPolicyOverrideGateTest
  AccessPolicyOverrideGateTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_policy_backend_failure_unit_test
  AccessPolicyBackendFailureTest
  AccessPolicyBackendFailureTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_admission_controller_core_unit_test
  AdmissionControllerTest
  AdmissionControllerTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_rate_limit_gate_unit_test
  RateLimitGateTest
  RateLimitGateTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_idempotency_guard_unit_test
  IdempotencyGuardTest
  IdempotencyGuardTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_admission_replay_hit_unit_test
  AdmissionReplayHitTest
  AdmissionReplayHitTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_request_validator_unit_test
  RequestValidatorTest
  RequestValidatorTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_request_validator_payload_limit_unit_test
  RequestValidatorPayloadLimitTest
  RequestValidatorPayloadLimitTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_request_validator_injection_unit_test
  RequestValidatorInjectionTest
  RequestValidatorInjectionTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_request_normalizer_unit_test
  RequestNormalizerTest
  RequestNormalizerTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_request_normalizer_identity_projection_unit_test
  RequestNormalizerIdentityProjectionTest
  RequestNormalizerIdentityProjectionTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_request_normalizer_constraint_projection_unit_test
  RequestNormalizerConstraintProjectionTest
  RequestNormalizerConstraintProjectionTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_request_normalizer_contract_compatibility_unit_test
  RequestNormalizerContractCompatibilityTest
  RequestNormalizerContractCompatibilityTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_runtime_bridge_unit_test
  RuntimeBridgeTest
  RuntimeBridgeTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_runtime_bridge_async_accept_unit_test
  RuntimeBridgeAsyncAcceptTest
  RuntimeBridgeAsyncAcceptTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_runtime_bridge_reject_mapping_unit_test
  RuntimeBridgeRejectMappingTest
  RuntimeBridgeRejectMappingTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_result_publisher_unit_test
  ResultPublisherTest
  ResultPublisherTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_protocol_error_mapper_unit_test
  ProtocolErrorMapperTest
  ProtocolErrorMapperTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_result_publisher_channel_failure_unit_test
  ResultPublisherChannelFailureTest
  ResultPublisherChannelFailureTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_async_task_registry_unit_test
  AsyncTaskRegistryTest
  AsyncTaskRegistryTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_async_task_registry_ownership_unit_test
  AsyncTaskRegistryOwnershipTest
  AsyncTaskRegistryOwnershipTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_async_task_registry_expiry_unit_test
  AsyncTaskRegistryExpiryTest
  AsyncTaskRegistryExpiryTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_daemon_accepted_async_receipt_unit_test
  DaemonAcceptedAsyncReceiptTest
  DaemonAcceptedAsyncReceiptTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_result_replay_cache_unit_test
  ResultReplayCacheTest
  ResultReplayCacheTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_result_replay_cache_eviction_unit_test
  ResultReplayCacheEvictionTest
  ResultReplayCacheEvictionTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_observability_bridge_unit_test
  AccessObservabilityBridgeTest
  AccessObservabilityBridgeTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_observability_field_set_unit_test
  AccessObservabilityFieldSetTest
  AccessObservabilityFieldSetTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_gateway_facade_unit_test
  AccessGatewayFacadeTest
  AccessGatewayFacadeTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_gateway_reject_path_unit_test
  AccessGatewayRejectPathTest
  AccessGatewayRejectPathTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_gateway_async_receipt_unit_test
  AccessGatewayAsyncReceiptTest
  AccessGatewayAsyncReceiptTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_daemon_protocol_types_unit_test
  DaemonProtocolTypesTest
  DaemonProtocolTypesTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_daemon_frame_codec_unit_test
  DaemonFrameCodecTest
  DaemonFrameCodecTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_daemon_frame_codec_malformed_unit_test
  DaemonFrameCodecMalformedTest
  DaemonFrameCodecMalformedTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_daemon_health_service_unit_test
  DaemonHealthServiceTest
  DaemonHealthServiceTest.cpp
)

add_executable(dasall_access_daemon_protocol_adapter_local_trusted_unit_test
  DaemonProtocolAdapterLocalTrustedTest.cpp
)

target_link_libraries(dasall_access_daemon_protocol_adapter_local_trusted_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_protocol_adapter_local_trusted_unit_test)

add_test(NAME DaemonProtocolAdapterLocalTrustedTest
  COMMAND dasall_access_daemon_protocol_adapter_local_trusted_unit_test
)
set_tests_properties(DaemonProtocolAdapterLocalTrustedTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_protocol_adapter_local_trusted_unit_test
)

# ACC-TODO-029: DaemonProtocolAdapter 基础功能测试
add_executable(dasall_access_daemon_protocol_adapter_unit_test
  DaemonProtocolAdapterTest.cpp
)

target_link_libraries(dasall_access_daemon_protocol_adapter_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_protocol_adapter_unit_test)

add_test(NAME DaemonProtocolAdapterTest
  COMMAND dasall_access_daemon_protocol_adapter_unit_test
)
set_tests_properties(DaemonProtocolAdapterTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_protocol_adapter_unit_test
)

add_executable(dasall_access_cli_ipc_client_unit_test
  CliIpcClientTest.cpp
  ../../../apps/cli/src/CliIpcClient.cpp
)

target_include_directories(dasall_access_cli_ipc_client_unit_test
  PRIVATE
    ../../../apps/cli/src
)

target_link_libraries(dasall_access_cli_ipc_client_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_access_cli_ipc_client_unit_test)

add_test(NAME CliIpcClientTest COMMAND dasall_access_cli_ipc_client_unit_test)
set_tests_properties(CliIpcClientTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_cli_ipc_client_unit_test
)

# ACC-TODO-025: CliIpcClientUnavailableTest
add_executable(dasall_access_cli_ipc_client_unavailable_unit_test
  CliIpcClientUnavailableTest.cpp
  ../../../apps/cli/src/CliIpcClient.cpp
)

target_include_directories(dasall_access_cli_ipc_client_unavailable_unit_test
  PRIVATE
    ../../../apps/cli/src
)

target_link_libraries(dasall_access_cli_ipc_client_unavailable_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_access_cli_ipc_client_unavailable_unit_test)

add_test(NAME CliIpcClientUnavailableTest
  COMMAND dasall_access_cli_ipc_client_unavailable_unit_test)
set_tests_properties(CliIpcClientUnavailableTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_cli_ipc_client_unavailable_unit_test
)

# ACC-TODO-025: CliOutputFormatterTest
add_executable(dasall_access_cli_output_formatter_unit_test
  CliOutputFormatterTest.cpp
  ../../../apps/cli/src/CliOutputFormatter.cpp
)

target_include_directories(dasall_access_cli_output_formatter_unit_test
  PRIVATE
    ../../../apps/cli/src
)

target_link_libraries(dasall_access_cli_output_formatter_unit_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall_access_cli_output_formatter_unit_test)

add_test(NAME CliOutputFormatterTest
  COMMAND dasall_access_cli_output_formatter_unit_test)
set_tests_properties(CliOutputFormatterTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_cli_output_formatter_unit_test
)


# ACC-TODO-026: HttpProtocolAdapterTest
add_executable(dasall_access_http_protocol_adapter_unit_test
  HttpProtocolAdapterTest.cpp
  ../../../apps/gateway/src/HttpProtocolAdapter.cpp
)

target_include_directories(dasall_access_http_protocol_adapter_unit_test
  PRIVATE
    ../../../apps/gateway/src
)

target_link_libraries(dasall_access_http_protocol_adapter_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_http_protocol_adapter_unit_test)

add_test(NAME HttpProtocolAdapterTest
  COMMAND dasall_access_http_protocol_adapter_unit_test)
set_tests_properties(HttpProtocolAdapterTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_http_protocol_adapter_unit_test
)

# ACC-TODO-026: HttpProtocolAdapterErrorMappingTest
add_executable(dasall_access_http_protocol_adapter_error_mapping_unit_test
  HttpProtocolAdapterErrorMappingTest.cpp
  ../../../apps/gateway/src/HttpProtocolAdapter.cpp
)

target_include_directories(dasall_access_http_protocol_adapter_error_mapping_unit_test
  PRIVATE
    ../../../apps/gateway/src
)

target_link_libraries(dasall_access_http_protocol_adapter_error_mapping_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_http_protocol_adapter_error_mapping_unit_test)

add_test(NAME HttpProtocolAdapterErrorMappingTest
  COMMAND dasall_access_http_protocol_adapter_error_mapping_unit_test)
set_tests_properties(HttpProtocolAdapterErrorMappingTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_http_protocol_adapter_error_mapping_unit_test
)


# ACC-TODO-028: AccessHealthProbeHandlerTest
add_executable(dasall_access_health_probe_handler_unit_test
  AccessHealthProbeHandlerTest.cpp
  ../../../apps/gateway/src/HealthProbeHandler.cpp
)

target_include_directories(dasall_access_health_probe_handler_unit_test
  PRIVATE
    ../../../apps/gateway/src
)

target_link_libraries(dasall_access_health_probe_handler_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_health_probe_handler_unit_test)

add_test(NAME AccessHealthProbeHandlerTest
  COMMAND dasall_access_health_probe_handler_unit_test)
set_tests_properties(AccessHealthProbeHandlerTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_health_probe_handler_unit_test
)

# ACC-TODO-028: AccessHttpSecurityHeadersTest
add_executable(dasall_access_http_security_headers_unit_test
  AccessHttpSecurityHeadersTest.cpp
  ../../../apps/gateway/src/HealthProbeHandler.cpp
)

target_include_directories(dasall_access_http_security_headers_unit_test
  PRIVATE
    ../../../apps/gateway/src
)

target_link_libraries(dasall_access_http_security_headers_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_http_security_headers_unit_test)

add_test(NAME AccessHttpSecurityHeadersTest
  COMMAND dasall_access_http_security_headers_unit_test)
set_tests_properties(AccessHttpSecurityHeadersTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_http_security_headers_unit_test
)

# ACC-TODO-027: AccessTaskQueryHandlerTest
add_executable(dasall_access_task_query_handler_unit_test
  AccessTaskQueryHandlerTest.cpp
  ../../../apps/gateway/src/TaskQueryHandler.cpp
)

target_include_directories(dasall_access_task_query_handler_unit_test
  PRIVATE
    ../../../apps/gateway/src
    ../../../access/src
)

target_link_libraries(dasall_access_task_query_handler_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_task_query_handler_unit_test)

add_test(NAME AccessTaskQueryHandlerTest
  COMMAND dasall_access_task_query_handler_unit_test)
set_tests_properties(AccessTaskQueryHandlerTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_task_query_handler_unit_test
)

add_executable(dasall_access_daemon_task_query_handler_unit_test
  DaemonTaskQueryHandlerTest.cpp
)

target_include_directories(dasall_access_daemon_task_query_handler_unit_test
  PRIVATE
    ../../../access/src
)

target_link_libraries(dasall_access_daemon_task_query_handler_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_daemon_task_query_handler_unit_test)

add_test(NAME DaemonTaskQueryHandlerTest
  COMMAND dasall_access_daemon_task_query_handler_unit_test)
set_tests_properties(DaemonTaskQueryHandlerTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_task_query_handler_unit_test
)

add_executable(dasall_access_daemon_cancel_command_unit_test
  DaemonCancelCommandTest.cpp
)

target_include_directories(dasall_access_daemon_cancel_command_unit_test
  PRIVATE
    ../../../access/src
)

target_link_libraries(dasall_access_daemon_cancel_command_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_daemon_cancel_command_unit_test)

add_test(NAME DaemonCancelCommandTest
  COMMAND dasall_access_daemon_cancel_command_unit_test)
set_tests_properties(DaemonCancelCommandTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_cancel_command_unit_test
)

# ACC-TODO-030: SimulatorProtocolAdapterTest
add_executable(dasall_access_simulator_protocol_adapter_unit_test
  SimulatorProtocolAdapterTest.cpp
  ../../../apps/simulator/src/SimulatorProtocolAdapter.cpp
)

target_include_directories(dasall_access_simulator_protocol_adapter_unit_test
  PRIVATE
    ../../../apps/simulator/src
    ../../../access/src
)

target_link_libraries(dasall_access_simulator_protocol_adapter_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_simulator_protocol_adapter_unit_test)

add_test(NAME SimulatorProtocolAdapterTest
  COMMAND dasall_access_simulator_protocol_adapter_unit_test)
set_tests_properties(SimulatorProtocolAdapterTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_simulator_protocol_adapter_unit_test
)

# ACC-TODO-030: SimulatorProtocolAdapterDeterministicTest
add_executable(dasall_access_simulator_protocol_adapter_deterministic_unit_test
  SimulatorProtocolAdapterDeterministicTest.cpp
  ../../../apps/simulator/src/SimulatorProtocolAdapter.cpp
)

target_include_directories(dasall_access_simulator_protocol_adapter_deterministic_unit_test
  PRIVATE
    ../../../apps/simulator/src
    ../../../access/src
)

target_link_libraries(dasall_access_simulator_protocol_adapter_deterministic_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_simulator_protocol_adapter_deterministic_unit_test)

add_test(NAME SimulatorProtocolAdapterDeterministicTest
  COMMAND dasall_access_simulator_protocol_adapter_deterministic_unit_test)
set_tests_properties(SimulatorProtocolAdapterDeterministicTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_simulator_protocol_adapter_deterministic_unit_test
)

if(NOT DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "access unit registration requires at least one test target")
endif()

add_custom_target(dasall_access_unit_tests
  DEPENDS ${DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS}
)

set(DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  ${DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)
