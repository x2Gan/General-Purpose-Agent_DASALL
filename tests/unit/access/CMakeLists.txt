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

if(NOT DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "access unit registration requires at least one test target")
endif()

set(DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  ${DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)
