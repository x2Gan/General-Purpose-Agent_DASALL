add_executable(dasall_infra_context_unit_test
  InfraContextTest.cpp
)

target_link_libraries(dasall_infra_context_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_infra_context_unit_test)

add_test(NAME InfraContextUnitTest COMMAND dasall_infra_context_unit_test)
set_tests_properties(InfraContextUnitTest PROPERTIES LABELS "unit")