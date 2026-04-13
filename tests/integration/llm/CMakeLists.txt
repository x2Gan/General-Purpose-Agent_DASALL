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

if(NOT DASALL_LLM_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "llm integration registration requires at least one test target")
endif()

set(DASALL_LLM_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_LLM_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)