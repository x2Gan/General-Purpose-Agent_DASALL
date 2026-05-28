set(DASALL_RUNTIME_AGENT_LOOP_INTEGRATION_TEST_EXECUTABLE_TARGETS)
set(DASALL_RUNTIME_FIXTURE_ROOT_DIR "${CMAKE_SOURCE_DIR}/tests/fixtures/runtime")
set(DASALL_RUNTIME_FIXTURE_CHECKPOINT_DIR "${DASALL_RUNTIME_FIXTURE_ROOT_DIR}/checkpoints")

macro(dasall_add_runtime_agent_loop_integration_test target_name test_name source_file)
  add_executable(${target_name}
    ${CMAKE_CURRENT_SOURCE_DIR}/${source_file}
  )

  target_link_libraries(${target_name}
    PRIVATE
      dasall_test_support
      dasall_runtime
  )

  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/runtime/src
      ${DASALL_RUNTIME_FIXTURE_ROOT_DIR}
  )

  target_compile_definitions(${target_name}
    PRIVATE
      DASALL_RUNTIME_FIXTURE_ROOT_DIR="${DASALL_RUNTIME_FIXTURE_ROOT_DIR}"
      DASALL_RUNTIME_FIXTURE_CHECKPOINT_DIR="${DASALL_RUNTIME_FIXTURE_CHECKPOINT_DIR}"
  )

  dasall_apply_common_options(${target_name})

  add_test(NAME ${test_name} COMMAND ${target_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "integration;runtime")

  list(APPEND DASALL_RUNTIME_AGENT_LOOP_INTEGRATION_TEST_EXECUTABLE_TARGETS ${target_name})
endmacro()

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_unary_fixture_integration_test
  RuntimeUnaryFixtureIntegrationTest
  RuntimeUnaryFixtureIntegrationTest.cpp
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_unary_integration_test
  RuntimeUnaryIntegrationTest
  RuntimeUnaryIntegrationTest.cpp
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_cancellation_propagation_integration_test
  RuntimeCancellationPropagationIntegrationTest
  RuntimeCancellationPropagationIntegrationTest.cpp
)

set_tests_properties(RuntimeUnaryIntegrationTest PROPERTIES
  LABELS "integration;runtime;gate-int-03;default-unary-gate"
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_required_optional_ports_integration_test
  RuntimeRequiredOptionalPortsIntegrationTest
  RuntimeRequiredOptionalPortsIntegrationTest.cpp
)

set_tests_properties(RuntimeRequiredOptionalPortsIntegrationTest PROPERTIES
  LABELS "integration;runtime;gate-int-06;required-optional-degraded-gate"
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_evidence_projection_integration_test
  RuntimeEvidenceProjectionIntegrationTest
  RuntimeEvidenceProjectionIntegrationTest.cpp
)

set_tests_properties(RuntimeEvidenceProjectionIntegrationTest PROPERTIES
  LABELS "integration;runtime;gate-int-04;structured-evidence-gate"
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_resume_integration_test
  RuntimeResumeIntegrationTest
  RuntimeResumeIntegrationTest.cpp
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_durable_resume_integration_test
  RuntimeDurableResumeIntegrationTest
  RuntimeDurableResumeIntegrationTest.cpp
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_policy_consumer_integration_test
  RuntimePolicyConsumerIntegrationTest
  RuntimePolicyConsumerIntegrationTest.cpp
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_recovery_context_integration_test
  RuntimeRecoveryContextIntegrationTest
  RuntimeRecoveryContextIntegrationTest.cpp
)

target_link_libraries(dasall_runtime_resume_integration_test
  PRIVATE
    dasall_memory
)

target_link_libraries(dasall_runtime_durable_resume_integration_test
  PRIVATE
    dasall_memory
)

target_link_libraries(dasall_runtime_unary_integration_test
  PRIVATE
    dasall_cognition
    dasall_memory
    dasall_sqlite3
    dasall_tools
)

target_link_libraries(dasall_runtime_cancellation_propagation_integration_test
  PRIVATE
    dasall_memory
    dasall_sqlite3
    dasall_tools
)

target_link_libraries(dasall_runtime_required_optional_ports_integration_test
  PRIVATE
    dasall_cognition
    dasall_memory
    dasall_sqlite3
    dasall_tools
)

target_link_libraries(dasall_runtime_evidence_projection_integration_test
  PRIVATE
    dasall_cognition
    dasall_memory
    dasall_sqlite3
    dasall_tools
)

target_link_libraries(dasall_runtime_policy_consumer_integration_test
  PRIVATE
    dasall_cognition
    dasall_memory
    dasall_tools
)

target_include_directories(dasall_runtime_unary_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/include
    ${CMAKE_SOURCE_DIR}/profiles/include
    ${CMAKE_SOURCE_DIR}/services/include
    ${CMAKE_SOURCE_DIR}/tools/src
)

target_include_directories(dasall_runtime_cancellation_propagation_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/cognition/include
    ${CMAKE_SOURCE_DIR}/infra/include
    ${CMAKE_SOURCE_DIR}/profiles/include
    ${CMAKE_SOURCE_DIR}/services/include
    ${CMAKE_SOURCE_DIR}/tools/src
)

target_include_directories(dasall_runtime_required_optional_ports_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/include
    ${CMAKE_SOURCE_DIR}/profiles/include
    ${CMAKE_SOURCE_DIR}/services/include
    ${CMAKE_SOURCE_DIR}/tools/src
)

target_include_directories(dasall_runtime_evidence_projection_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/cognition/include
    ${CMAKE_SOURCE_DIR}/infra/include
    ${CMAKE_SOURCE_DIR}/knowledge/include
    ${CMAKE_SOURCE_DIR}/memory/include
    ${CMAKE_SOURCE_DIR}/profiles/include
    ${CMAKE_SOURCE_DIR}/services/include
    ${CMAKE_SOURCE_DIR}/tools/src
)

target_compile_definitions(dasall_runtime_unary_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_compile_definitions(dasall_runtime_cancellation_propagation_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_compile_definitions(dasall_runtime_required_optional_ports_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_compile_definitions(dasall_runtime_evidence_projection_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_checkpoint_replay_regression_test
  RuntimeCheckpointReplayRegressionTest
  RuntimeCheckpointReplayRegressionTest.cpp
)

target_link_libraries(dasall_runtime_checkpoint_replay_regression_test
  PRIVATE
    dasall_memory
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_profile_compatibility_integration_test
  RuntimeProfileCompatibilityTest
  RuntimeProfileCompatibilityTest.cpp
)

set_tests_properties(RuntimeProfileCompatibilityTest PROPERTIES
  LABELS "integration;runtime;gate-int-06;required-optional-degraded-gate"
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_safe_mode_integration_test
  RuntimeSafeModeIntegrationTest
  RuntimeSafeModeIntegrationTest.cpp
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_health_maintenance_integration_test
  RuntimeHealthMaintenanceIntegrationTest
  RuntimeHealthMaintenanceIntegrationTest.cpp
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_production_logging_integration_test
  RuntimeProductionLoggingIntegrationTest
  RuntimeProductionLoggingIntegrationTest.cpp
)

target_include_directories(dasall_runtime_health_maintenance_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_include_directories(dasall_runtime_production_logging_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/runtime_support/include
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_runtime_production_logging_integration_test
  PRIVATE
    dasall_apps_runtime_support
    dasall_infra
    dasall_profiles
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_checkpoint_replay_compatibility_integration_test
  RuntimeCheckpointReplayCompatibilityTest
  RuntimeCheckpointReplayCompatibilityTest.cpp
)

if(NOT DASALL_RUNTIME_AGENT_LOOP_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "runtime agent_loop integration registration requires at least one test target")
endif()

set(DASALL_RUNTIME_AGENT_LOOP_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_RUNTIME_AGENT_LOOP_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)