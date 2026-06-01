set(DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_cognition_integration_test target_name test_name source_file labels)
  add_executable(${target_name}
    ${CMAKE_CURRENT_SOURCE_DIR}/${source_file}
  )

  target_link_libraries(${target_name}
    PRIVATE
      dasall_test_support
      dasall_runtime
      dasall_memory
      dasall_sqlite3
      dasall_tools
  )

  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/runtime/src
      ${CMAKE_SOURCE_DIR}/cognition/include
      ${CMAKE_SOURCE_DIR}/tools/src
      ${CMAKE_SOURCE_DIR}/services/include
      ${CMAKE_SOURCE_DIR}/tests/fixtures/runtime
      ${CMAKE_SOURCE_DIR}/tests/mocks/include
  )

  target_compile_definitions(${target_name}
    PRIVATE
      DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
  )

  dasall_apply_common_options(${target_name})

  add_test(NAME ${test_name} COMMAND ${target_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "${labels}")

  list(APPEND DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS ${target_name})
endmacro()

dasall_add_cognition_integration_test(
  dasall_cognition_runtime_integration_test
  CognitionRuntimeIntegrationTest
  CognitionRuntimeIntegrationTest.cpp
  "integration;cognition"
)

set_tests_properties(CognitionRuntimeIntegrationTest PROPERTIES
  LABELS "integration;cognition;gate-int-03;default-unary-gate"
)

dasall_add_cognition_integration_test(
  dasall_cognition_runtime_interaction_contract_integration_test
  CognitionRuntimeInteractionContractTest
  CognitionRuntimeInteractionContractTest.cpp
  "integration;cognition"
)

dasall_add_cognition_integration_test(
  dasall_cognition_failure_injection_integration_test
  CognitionFailureInjectionIntegrationTest
  CognitionFailureInjectionIntegrationTest.cpp
  "integration;cognition;failure"
)

dasall_add_cognition_integration_test(
  dasall_cognition_profile_compatibility_integration_test
  CognitionProfileCompatibilityTest
  CognitionProfileCompatibilityTest.cpp
  "integration;cognition;profile"
)

dasall_add_cognition_integration_test(
  dasall_cognition_runtime_policy_projection_integration_test
  CognitionRuntimePolicyProjectionIntegrationTest
  CognitionRuntimePolicyProjectionIntegrationTest.cpp
  "integration;cognition;profile"
)

dasall_add_cognition_integration_test(
  dasall_cognition_review_regression_integration_test
  CognitionReviewRegressionTest
  CognitionReviewRegressionTest.cpp
  "integration;cognition;review"
)

dasall_add_cognition_integration_test(
  dasall_cognition_llm_judge_regression_integration_test
  CognitionLlmJudgeRegressionTest
  CognitionLlmJudgeRegressionTest.cpp
  "integration;cognition;judge"
)

dasall_add_cognition_integration_test(
  dasall_cognition_structured_output_integration_test
  CognitionStructuredOutputIntegrationTest
  CognitionStructuredOutputIntegrationTest.cpp
  "integration;cognition;structured-output"
)

dasall_add_cognition_integration_test(
  dasall_cognition_perception_structured_output_integration_test
  CognitionPerceptionStructuredOutputIntegrationTest
  CognitionPerceptionStructuredOutputIntegrationTest.cpp
  "integration;cognition;structured-output"
)

dasall_add_cognition_integration_test(
  dasall_cognition_reflection_structured_output_integration_test
  CognitionReflectionStructuredOutputIntegrationTest
  CognitionReflectionStructuredOutputIntegrationTest.cpp
  "integration;cognition;structured-output"
)

dasall_add_cognition_integration_test(
  dasall_cognition_production_telemetry_integration_test
  CognitionProductionTelemetryIntegrationTest
  CognitionProductionTelemetryIntegrationTest.cpp
  "integration;cognition;observability"
)

dasall_add_cognition_integration_test(
  dasall_cognition_production_logging_integration_test
  CognitionProductionLoggingIntegrationTest
  CognitionProductionLoggingIntegrationTest.cpp
  "integration;cognition;observability;logging"
)

target_include_directories(dasall_cognition_production_telemetry_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/include
    ${CMAKE_SOURCE_DIR}/infra/src
    ${CMAKE_SOURCE_DIR}/profiles/include
)

target_include_directories(dasall_cognition_production_logging_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/include
    ${CMAKE_SOURCE_DIR}/infra/src
    ${CMAKE_SOURCE_DIR}/profiles/include
)

target_link_libraries(dasall_cognition_production_telemetry_integration_test
  PRIVATE
    dasall_cognition
    dasall_infra
    dasall_profiles
)

target_link_libraries(dasall_cognition_production_logging_integration_test
  PRIVATE
    dasall_cognition
    dasall_infra
    dasall_profiles
)

target_compile_definitions(dasall_cognition_review_regression_integration_test
  PRIVATE
    DASALL_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_compile_definitions(dasall_cognition_llm_judge_regression_integration_test
  PRIVATE
    DASALL_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(dasall_cognition_llm_judge_regression_integration_test
  PRIVATE
    dasall_llm
)

if(NOT DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "cognition integration registration requires at least one test target")
endif()

set(DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)
