add_executable(dasall_runtime_smoke_test
  RuntimeSmokeTest.cpp
)

target_link_libraries(dasall_runtime_smoke_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

dasall_apply_common_options(dasall_runtime_smoke_test)

add_test(NAME dasall_runtime_smoke_test COMMAND dasall_runtime_smoke_test)
set_tests_properties(dasall_runtime_smoke_test PROPERTIES LABELS "unit")
