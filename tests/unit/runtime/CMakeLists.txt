add_executable(dasall_runtime_smoke_test
  RuntimeSmokeTest.cpp
)

add_executable(dasall_runtime_error_code_unit_test
  RuntimeErrorCodeTest.cpp
)

add_executable(dasall_runtime_cancellation_token_unit_test
  CancellationTokenTest.cpp
)

target_link_libraries(dasall_runtime_smoke_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_link_libraries(dasall_runtime_error_code_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_link_libraries(dasall_runtime_cancellation_token_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

dasall_apply_common_options(dasall_runtime_smoke_test)
dasall_apply_common_options(dasall_runtime_error_code_unit_test)
dasall_apply_common_options(dasall_runtime_cancellation_token_unit_test)

add_test(NAME dasall_runtime_smoke_test COMMAND dasall_runtime_smoke_test)
set_tests_properties(dasall_runtime_smoke_test PROPERTIES LABELS "unit")

add_test(NAME RuntimeErrorCodeTest COMMAND dasall_runtime_error_code_unit_test)
set_tests_properties(RuntimeErrorCodeTest PROPERTIES LABELS "unit")

add_test(NAME CancellationTokenTest COMMAND dasall_runtime_cancellation_token_unit_test)
set_tests_properties(CancellationTokenTest PROPERTIES LABELS "unit")
