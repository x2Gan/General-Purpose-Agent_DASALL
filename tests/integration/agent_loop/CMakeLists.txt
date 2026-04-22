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
  dasall_runtime_resume_integration_test
  RuntimeResumeIntegrationTest
  RuntimeResumeIntegrationTest.cpp
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_checkpoint_replay_regression_test
  RuntimeCheckpointReplayRegressionTest
  RuntimeCheckpointReplayRegressionTest.cpp
)

dasall_add_runtime_agent_loop_integration_test(
  dasall_runtime_profile_compatibility_integration_test
  RuntimeProfileCompatibilityTest
  RuntimeProfileCompatibilityTest.cpp
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