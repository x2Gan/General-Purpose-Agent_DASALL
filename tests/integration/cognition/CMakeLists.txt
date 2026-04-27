set(DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_cognition_integration_placeholder target_name test_name)
  add_custom_target(${target_name}
    COMMAND ${CMAKE_COMMAND} -E true
  )

  add_test(NAME ${test_name} COMMAND ${CMAKE_COMMAND} -E true)
  set_tests_properties(${test_name} PROPERTIES LABELS "integration;cognition")

  list(APPEND DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS ${target_name})
endmacro()

dasall_add_cognition_integration_placeholder(
  dasall_cognition_runtime_integration_test
  CognitionRuntimeIntegrationTest
)

dasall_add_cognition_integration_placeholder(
  dasall_cognition_runtime_interaction_contract_integration_test
  CognitionRuntimeInteractionContractTest
)

dasall_add_cognition_integration_placeholder(
  dasall_cognition_failure_injection_integration_test
  CognitionFailureInjectionIntegrationTest
)

dasall_add_cognition_integration_placeholder(
  dasall_cognition_profile_compatibility_integration_test
  CognitionProfileCompatibilityTest
)

dasall_add_cognition_integration_placeholder(
  dasall_cognition_profile_compatibility_alias_integration_test
  CognitionProfileCompatibilityIntegrationTest
)

if(NOT DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "cognition integration registration requires at least one test target")
endif()

set(DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)
