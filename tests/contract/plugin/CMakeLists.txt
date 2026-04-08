function(dasall_register_plugin_contract_test executable_target_name ctest_name source_file)
  dasall_register_contract_test(
    ${executable_target_name}
    ${ctest_name}
    ${source_file}
    smoke
  )

  set_tests_properties(${ctest_name} PROPERTIES LABELS "contract;smoke;plugin")

  target_include_directories(${executable_target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/infra/include
      ${CMAKE_SOURCE_DIR}/infra/src
  )

  target_link_libraries(${executable_target_name}
    PRIVATE
      dasall_infra
  )
endfunction()

dasall_register_plugin_contract_test(
  dasall_contract_plugin_descriptor_boundary_test
  PluginDescriptorBoundaryContractTest
  ../smoke/PluginDescriptorBoundaryContractTest.cpp
)

dasall_register_plugin_contract_test(
  dasall_contract_plugin_manifest_boundary_test
  PluginManifestBoundaryContractTest
  ../smoke/PluginManifestBoundaryContractTest.cpp
)

dasall_register_plugin_contract_test(
  dasall_contract_plugin_catalog_boundary_test
  PluginCatalogBoundaryContractTest
  ../smoke/PluginCatalogBoundaryContractTest.cpp
)

dasall_register_plugin_contract_test(
  dasall_contract_plugin_error_code_boundary_test
  PluginErrorCodeBoundaryContractTest
  ../smoke/PluginErrorCodeBoundaryContractTest.cpp
)

dasall_register_plugin_contract_test(
  dasall_contract_plugin_manager_boundary_test
  PluginManagerBoundaryContractTest
  ../smoke/PluginManagerBoundaryContractTest.cpp
)

dasall_register_plugin_contract_test(
  dasall_contract_plugin_policy_gate_boundary_test
  PluginPolicyGateBoundaryContractTest
  ../smoke/PluginPolicyGateBoundaryContractTest.cpp
)

dasall_register_plugin_contract_test(
  dasall_contract_plugin_signature_verifier_boundary_test
  PluginSignatureVerifierBoundaryContractTest
  ../smoke/PluginSignatureVerifierBoundaryContractTest.cpp
)

dasall_register_plugin_contract_test(
  dasall_contract_plugin_validation_pipeline_boundary_test
  PluginValidationPipelineBoundaryContractTest
  ../smoke/PluginValidationPipelineBoundaryContractTest.cpp
)

set(DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS
  "${DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS}"
  PARENT_SCOPE
)