set(DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS "")

# Centralized registration keeps WP01 contract test entrypoints converged in a
# single CMake path and guarantees each registered test is labeled as contract.
function(dasall_register_contract_test executable_target_name ctest_name source_file)
  add_executable(${executable_target_name}
    ${source_file}
  )

  target_link_libraries(${executable_target_name}
    PRIVATE
      dasall_test_support
  )

  dasall_apply_common_options(${executable_target_name})

  add_test(NAME ${ctest_name} COMMAND ${executable_target_name})
  set_tests_properties(${ctest_name} PROPERTIES LABELS "contract")

  list(APPEND DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS ${executable_target_name})
  set(DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS "${DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS}" PARENT_SCOPE)
endfunction()

dasall_register_contract_test(
  dasall_contract_smoke_test
  dasall_contract_smoke_test
  smoke/ContractSmokeTest.cpp
)

dasall_register_contract_test(
  dasall_contract_compatibility_test
  dasall_contract_compatibility_test
  smoke/CompatibilityContractTest.cpp
)

dasall_register_contract_test(
  dasall_contract_object_boundary_catalog_test
  dasall_contract_object_boundary_catalog_test
  smoke/ObjectBoundaryCatalogContractTest.cpp
)

dasall_register_contract_test(
  dasall_contract_stable_type_presence_test
  StableTypePresenceContractTest
  smoke/StableTypePresenceContractTest.cpp
)

dasall_register_contract_test(
  dasall_contract_boundary_guards_test
  BoundaryGuardsContractTest
  smoke/BoundaryGuardsContractTest.cpp
)

dasall_register_contract_test(
  dasall_contract_context_packet_boundary_test
  ContextPacketBoundaryContractTest
  smoke/ContextPacketBoundaryContractTest.cpp
)

dasall_register_contract_test(
  dasall_contract_recovery_boundary_test
  RecoveryBoundaryContractTest
  smoke/RecoveryBoundaryContractTest.cpp
)

dasall_register_contract_test(
  dasall_contract_multi_agent_boundary_test
  MultiAgentBoundaryContractTest
  smoke/MultiAgentBoundaryContractTest.cpp
)

# Export the converged contract test target list to tests/CMakeLists.txt.
set(DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS
  "${DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS}"
  PARENT_SCOPE
)


