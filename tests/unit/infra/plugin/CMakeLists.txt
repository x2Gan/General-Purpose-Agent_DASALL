set(DASALL_PLUGIN_UNIT_TEST_EXECUTABLE_TARGETS "")

function(dasall_register_plugin_unit_test executable_target_name ctest_name source_file)
  add_executable(${executable_target_name}
    ${source_file}
  )

  target_link_libraries(${executable_target_name}
    PRIVATE
      dasall_test_support
      dasall_infra
  )

  target_include_directories(${executable_target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/infra/src
  )

  dasall_apply_common_options(${executable_target_name})

  add_test(NAME ${ctest_name} COMMAND ${executable_target_name})
  set_tests_properties(${ctest_name} PROPERTIES LABELS "unit;plugin")

  list(APPEND DASALL_PLUGIN_UNIT_TEST_EXECUTABLE_TARGETS ${executable_target_name})
  set(DASALL_PLUGIN_UNIT_TEST_EXECUTABLE_TARGETS "${DASALL_PLUGIN_UNIT_TEST_EXECUTABLE_TARGETS}" PARENT_SCOPE)
endfunction()

dasall_register_plugin_unit_test(
  dasall_plugin_descriptor_unit_test
  PluginDescriptorFieldTest
  PluginDescriptorTest.cpp
)

dasall_register_plugin_unit_test(
  dasall_plugin_catalog_unit_test
  PluginCatalogTest
  PluginCatalogTest.cpp
)

dasall_register_plugin_unit_test(
  dasall_plugin_error_code_unit_test
  PluginErrorCodeTest
  PluginErrorCodeTest.cpp
)

dasall_register_plugin_unit_test(
  dasall_plugin_manager_interface_unit_test
  PluginManagerInterfaceCompileTest
  PluginManagerInterfaceTest.cpp
)

dasall_register_plugin_unit_test(
  dasall_plugin_policy_gate_interface_unit_test
  PluginPolicyGateInterfaceCompileTest
  PluginPolicyGateInterfaceTest.cpp
)

dasall_register_plugin_unit_test(
  dasall_plugin_validation_pipeline_unit_test
  PluginValidationPipelineTest
  PluginValidationPipelineTest.cpp
)

dasall_register_plugin_unit_test(
  dasall_plugin_audit_adapter_unit_test
  PluginAuditAdapterTest
  PluginAuditAdapterTest.cpp
)

set(DASALL_PLUGIN_UNIT_TEST_EXECUTABLE_TARGETS
  "${DASALL_PLUGIN_UNIT_TEST_EXECUTABLE_TARGETS}"
  PARENT_SCOPE
)