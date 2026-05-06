set(DASALL_LLM_INTEGRATION_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_llm_integration_test target_name test_name source_file labels)
  add_executable(${target_name}
    ${source_file}
  )

  target_link_libraries(${target_name}
    PRIVATE
      dasall_test_support
      dasall_llm
  )

  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/infra/include
  )

  dasall_apply_common_options(${target_name})

  add_test(NAME ${test_name} COMMAND ${target_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "${labels}")

  list(APPEND DASALL_LLM_INTEGRATION_TEST_EXECUTABLE_TARGETS ${target_name})
endmacro()

dasall_add_llm_integration_test(
  dasall_llm_smoke_integration_test
  LLMSubsystemSmokeIntegrationTest
  LLMSubsystemSmokeIntegrationTest.cpp
  "integration;llm"
)

set_tests_properties(LLMSubsystemSmokeIntegrationTest PROPERTIES
  LABELS "integration;llm;gate-int-06;required-optional-degraded-gate"
)

dasall_add_llm_integration_test(
  dasall_deepseek_dual_mode_selection_integration_test
  DeepSeekDualModeSelectionIntegrationTest
  DeepSeekDualModeSelectionIntegrationTest.cpp
  "integration;llm;profile"
)

dasall_add_llm_integration_test(
  dasall_llm_fallback_integration_test
  LLMFallbackIntegrationTest
  LLMFallbackIntegrationTest.cpp
  "integration;llm;fallback"
)

dasall_add_llm_integration_test(
  dasall_llm_prompt_source_switch_integration_test
  LLMPromptSourceSwitchIntegrationTest
  LLMPromptSourceSwitchIntegrationTest.cpp
  "integration;llm;profile"
)

dasall_add_llm_integration_test(
  dasall_llm_persona_selection_integration_test
  LLMPersonaSelectionIntegrationTest
  LLMPersonaSelectionIntegrationTest.cpp
  "integration;llm;profile"
)

dasall_add_llm_integration_test(
  dasall_llm_governance_failure_integration_test
  LLMGovernanceFailureIntegrationTest
  LLMGovernanceFailureIntegrationTest.cpp
  "integration;llm;failure"
)

dasall_add_llm_integration_test(
  dasall_llm_profile_integration_test
  LLMProfileIntegrationTest
  LLMProfileIntegrationTest.cpp
  "integration;llm;profile"
)

dasall_add_llm_integration_test(
  dasall_llm_provider_asset_onboarding_integration_test
  LLMProviderAssetOnboardingIntegrationTest
  LLMProviderAssetOnboardingIntegrationTest.cpp
  "integration;llm;profile"
)

if(NOT DASALL_LLM_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "llm integration registration requires at least one test target")
endif()

set(DASALL_LLM_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_LLM_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)