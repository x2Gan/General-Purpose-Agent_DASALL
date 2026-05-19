set(DASALL_TOOLS_INTEGRATION_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_tools_integration_test target_name test_name source_file labels)
  add_executable(${target_name}
    ${source_file}
  )

  target_link_libraries(${target_name}
    PRIVATE
      dasall_test_support
      dasall_tools
  )

  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/tools/src
      ${CMAKE_SOURCE_DIR}/infra/include
      ${CMAKE_SOURCE_DIR}/profiles/include
      ${CMAKE_SOURCE_DIR}/services/include
  )

  dasall_apply_common_options(${target_name})

  add_test(NAME ${test_name} COMMAND ${target_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "${labels}")

  list(APPEND DASALL_TOOLS_INTEGRATION_TEST_EXECUTABLE_TARGETS ${target_name})
endmacro()

dasall_add_tools_integration_test(
  dasall_tool_mcp_fallback_integration_test
  ToolMCPFallbackIntegrationTest
  ToolMCPFallbackIntegrationTest.cpp
  "integration;tools"
)

dasall_add_tools_integration_test(
  dasall_tool_observability_integration_test
  ToolObservabilityIntegrationTest
  ToolObservabilityIntegrationTest.cpp
  "integration;tools"
)

dasall_add_tools_integration_test(
  dasall_tool_production_observability_integration_test
  ToolProductionObservabilityIntegrationTest
  ToolProductionObservabilityIntegrationTest.cpp
  "integration;tools"
)

target_include_directories(dasall_tool_production_observability_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_tool_production_observability_integration_test
  PRIVATE
    dasall_infra
    dasall_profiles
    dasall_services
)

dasall_add_tools_integration_test(
  dasall_tool_profile_integration_test
  ToolProfileIntegrationTest
  ToolProfileIntegrationTest.cpp
  "integration;tools"
)

dasall_add_tools_integration_test(
  dasall_tool_plugin_skill_bundle_integration_test
  ToolPluginSkillBundleIntegrationTest
  ToolPluginSkillBundleIntegrationTest.cpp
  "integration;tools"
)

dasall_add_tools_integration_test(
  dasall_tool_plugin_stdio_mcp_integration_test
  ToolPluginStdioMCPIntegrationTest
  ToolPluginStdioMCPIntegrationTest.cpp
  "integration;tools"
)

dasall_add_tools_integration_test(
  dasall_tool_plugin_extension_end_to_end_integration_test
  ToolPluginExtensionEndToEndIntegrationTest
  ToolPluginExtensionEndToEndIntegrationTest.cpp
  "integration;tools"
)

dasall_add_tools_integration_test(
  dasall_tool_plugin_lifecycle_bridge_integration_test
  ToolPluginLifecycleBridgeIntegrationTest
  ToolPluginLifecycleBridgeIntegrationTest.cpp
  "integration;tools"
)

dasall_add_tools_integration_test(
  dasall_tool_skill_runtime_integration_test
  ToolSkillRuntimeIntegrationTest
  ToolSkillRuntimeIntegrationTest.cpp
  "integration;tools"
)

dasall_add_tools_integration_test(
  dasall_tool_services_smoke_integration_test
  ToolServicesSmokeIntegrationTest
  ToolServicesSmokeIntegrationTest.cpp
  "integration;tools"
)

dasall_add_tools_integration_test(
  dasall_tool_services_production_bridge_integration_test
  ToolServicesProductionBridgeIntegrationTest
  ToolServicesProductionBridgeIntegrationTest.cpp
  "integration;tools"
)

dasall_add_tools_integration_test(
  dasall_tool_compensation_services_integration_test
  ToolCompensationServicesIntegrationTest
  ToolCompensationServicesIntegrationTest.cpp
  "integration;tools"
)

target_link_libraries(dasall_tool_services_production_bridge_integration_test
  PRIVATE
    dasall_infra
    dasall_profiles
    dasall_services
)

target_link_libraries(dasall_tool_compensation_services_integration_test
  PRIVATE
    dasall_infra
    dasall_profiles
    dasall_services
)

dasall_add_tools_integration_test(
  dasall_tool_workflow_failure_integration_test
  ToolWorkflowFailureIntegrationTest
  ToolWorkflowFailureIntegrationTest.cpp
  "integration;tools"
)

dasall_add_tools_integration_test(
  dasall_tool_batch_invoke_independence_integration_test
  ToolBatchInvokeIndependenceIntegrationTest
  ToolBatchInvokeIndependenceIntegrationTest.cpp
  "integration;tools"
)

if(NOT DASALL_TOOLS_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "tools integration registration requires at least one test target")
endif()

set(DASALL_TOOLS_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_TOOLS_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)