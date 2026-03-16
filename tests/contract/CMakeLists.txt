set(DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS "")

# Centralized registration keeps contract test entrypoints converged in a
# single CMake path and guarantees each registered test is labeled as contract
# plus one explicit group label: smoke/error/checkpoint/event.
function(dasall_register_contract_test executable_target_name ctest_name source_file group_label)
  add_executable(${executable_target_name}
    ${source_file}
  )

  target_link_libraries(${executable_target_name}
    PRIVATE
      dasall_test_support
  )

  dasall_apply_common_options(${executable_target_name})

  add_test(NAME ${ctest_name} COMMAND ${executable_target_name})
  set_tests_properties(${ctest_name} PROPERTIES LABELS "contract;${group_label}")

  list(APPEND DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS ${executable_target_name})
  set(DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS "${DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS}" PARENT_SCOPE)
endfunction()

# smoke group
dasall_register_contract_test(
  dasall_contract_smoke_test
  dasall_contract_smoke_test
  smoke/ContractSmokeTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_compatibility_test
  CompatibilityContractTest
  smoke/CompatibilityContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_object_boundary_catalog_test
  dasall_contract_object_boundary_catalog_test
  smoke/ObjectBoundaryCatalogContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_stable_type_presence_test
  StableTypePresenceContractTest
  smoke/StableTypePresenceContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_boundary_guards_test
  BoundaryGuardsContractTest
  smoke/BoundaryGuardsContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_context_packet_boundary_test
  ContextPacketBoundaryContractTest
  smoke/ContextPacketBoundaryContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_recovery_boundary_test
  RecoveryBoundaryContractTest
  smoke/RecoveryBoundaryContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_multi_agent_boundary_test
  MultiAgentBoundaryContractTest
  smoke/MultiAgentBoundaryContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_cross_cutting_contracts_smoke_test
  CrossCuttingContractsSmokeTest
  smoke/CrossCuttingContractsSmokeTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_field_evolution_guards_test
  FieldEvolutionGuardsContractTest
  smoke/FieldEvolutionGuardsContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_identity_metadata_test
  IdentityMetadataContractTest
  smoke/IdentityMetadataContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_time_deadline_test
  TimeDeadlineContractTest
  smoke/TimeDeadlineContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_enum_lifecycle_test
  EnumLifecycleContractTest
  smoke/EnumLifecycleContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_m2_checklist_test
  M2ChecklistContractTest
  smoke/M2ChecklistContractTest.cpp
  smoke
)

# error group
dasall_register_contract_test(
  dasall_contract_result_code_test
  ResultCodeContractTest
  error/ResultCodeContractTest.cpp
  error
)

dasall_register_contract_test(
  dasall_contract_error_info_test
  ErrorInfoContractTest
  error/ErrorInfoContractTest.cpp
  error
)

dasall_register_contract_test(
  dasall_contract_error_source_test
  ErrorSourceContractTest
  error/ErrorSourceContractTest.cpp
  error
)

# checkpoint group
dasall_register_contract_test(
  dasall_contract_runtime_budget_test
  RuntimeBudgetContractTest
  checkpoint/RuntimeBudgetContractTest.cpp
  checkpoint
)

dasall_register_contract_test(
  dasall_contract_budget_snapshot_test
  BudgetSnapshotContractTest
  checkpoint/BudgetSnapshotContractTest.cpp
  checkpoint
)

# event group
dasall_register_contract_test(
  dasall_contract_event_envelope_test
  EventEnvelopeContractTest
  event/EventEnvelopeContractTest.cpp
  event
)

# Export the converged contract test target list to tests/CMakeLists.txt.
set(DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS
  "${DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS}"
  PARENT_SCOPE
)


