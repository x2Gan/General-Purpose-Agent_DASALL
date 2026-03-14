add_executable(dasall_contract_smoke_test
  smoke/ContractSmokeTest.cpp
)

target_link_libraries(dasall_contract_smoke_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall_contract_smoke_test)

add_test(NAME dasall_contract_smoke_test COMMAND dasall_contract_smoke_test)
set_tests_properties(dasall_contract_smoke_test PROPERTIES LABELS "contract")

add_executable(dasall_contract_compatibility_test
  smoke/CompatibilityContractTest.cpp
)

target_link_libraries(dasall_contract_compatibility_test
  PRIVATE
    dasall_test_support
)

dasall_apply_common_options(dasall_contract_compatibility_test)

add_test(NAME dasall_contract_compatibility_test COMMAND dasall_contract_compatibility_test)
set_tests_properties(dasall_contract_compatibility_test PROPERTIES LABELS "contract")
