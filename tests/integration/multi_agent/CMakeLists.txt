set(DASALL_MULTI_AGENT_INTEGRATION_TEST_EXECUTABLE_TARGETS)

add_executable(dasall_multi_agent_disabled_by_profile_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/MultiAgentDisabledByProfileIntegrationTest.cpp
)

target_link_libraries(dasall_multi_agent_disabled_by_profile_integration_test
  PRIVATE
    dasall_test_support
    dasall_apps_runtime_support
    dasall_multi_agent
    dasall_profiles
)

target_compile_definitions(dasall_multi_agent_disabled_by_profile_integration_test
  PRIVATE
    DASALL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

dasall_apply_common_options(dasall_multi_agent_disabled_by_profile_integration_test)

add_test(NAME MultiAgentDisabledByProfileIntegrationTest
  COMMAND dasall_multi_agent_disabled_by_profile_integration_test)
set_tests_properties(MultiAgentDisabledByProfileIntegrationTest PROPERTIES
  LABELS "integration;multi-agent;multi-agent-bc17-gate")

list(APPEND DASALL_MULTI_AGENT_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_multi_agent_disabled_by_profile_integration_test
)

add_executable(dasall_multi_agent_coordinator_pipeline_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/MultiAgentCoordinatorPipelineTest.cpp
)

target_link_libraries(dasall_multi_agent_coordinator_pipeline_integration_test
  PRIVATE
    dasall_test_support
    dasall_apps_runtime_support
    dasall_multi_agent
    dasall_profiles
)

target_compile_definitions(dasall_multi_agent_coordinator_pipeline_integration_test
  PRIVATE
    DASALL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

dasall_apply_common_options(dasall_multi_agent_coordinator_pipeline_integration_test)

add_test(NAME MultiAgentCoordinatorPipelineTest
  COMMAND dasall_multi_agent_coordinator_pipeline_integration_test)
set_tests_properties(MultiAgentCoordinatorPipelineTest PROPERTIES
  LABELS "integration;multi-agent;multi-agent-bc17-gate")

list(APPEND DASALL_MULTI_AGENT_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_multi_agent_coordinator_pipeline_integration_test
)

add_executable(dasall_multi_agent_recovery_fold_integration_test
  ${CMAKE_CURRENT_SOURCE_DIR}/MultiAgentRecoveryFoldIntegrationTest.cpp
)

target_link_libraries(dasall_multi_agent_recovery_fold_integration_test
  PRIVATE
    dasall_test_support
    dasall_apps_runtime_support
    dasall_multi_agent
    dasall_profiles
    dasall_runtime
)

target_include_directories(dasall_multi_agent_recovery_fold_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/runtime/src
)

target_compile_definitions(dasall_multi_agent_recovery_fold_integration_test
  PRIVATE
    DASALL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

dasall_apply_common_options(dasall_multi_agent_recovery_fold_integration_test)

add_test(NAME MultiAgentRecoveryFoldIntegrationTest
  COMMAND dasall_multi_agent_recovery_fold_integration_test)
set_tests_properties(MultiAgentRecoveryFoldIntegrationTest PROPERTIES
  LABELS "integration;multi-agent;multi-agent-bc17-gate")

list(APPEND DASALL_MULTI_AGENT_INTEGRATION_TEST_EXECUTABLE_TARGETS
  dasall_multi_agent_recovery_fold_integration_test
)

set(DASALL_MULTI_AGENT_GATE_TEST_NAMES
  MultiAgentDisabledByProfileIntegrationTest
  MultiAgentCoordinatorPipelineTest
  MultiAgentRecoveryFoldIntegrationTest
)

string(JOIN "," DASALL_MULTI_AGENT_GATE_EXPECTED_TESTS_CSV
  ${DASALL_MULTI_AGENT_GATE_TEST_NAMES}
)

add_custom_target(dasall_multi_agent_focus_integration_tests
  COMMAND ${CMAKE_COMMAND}
    -DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}
    -DBUILD_DIR=${CMAKE_BINARY_DIR}
    -DEXPECTED_TESTS_CSV=${DASALL_MULTI_AGENT_GATE_EXPECTED_TESTS_CSV}
    -DDISCOVERABILITY_LABELS_CSV=multi-agent-bc17-gate
    -DRUN_ACCEPTANCE=ON
    -P ${CMAKE_SOURCE_DIR}/tests/VerifySystemGateDiscoverability.cmake
  DEPENDS ${DASALL_MULTI_AGENT_INTEGRATION_TEST_EXECUTABLE_TARGETS}
)

if(NOT DASALL_MULTI_AGENT_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "multi_agent integration registration requires at least one test target")
endif()

set(DASALL_MULTI_AGENT_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_MULTI_AGENT_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)