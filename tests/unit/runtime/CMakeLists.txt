add_executable(dasall_runtime_smoke_test
  RuntimeSmokeTest.cpp
)

add_executable(dasall_runtime_error_code_unit_test
  RuntimeErrorCodeTest.cpp
)

add_executable(dasall_runtime_cancellation_token_unit_test
  CancellationTokenTest.cpp
)

add_executable(dasall_agent_fsm_unit_test
  AgentFsmTest.cpp
)

add_executable(dasall_budget_controller_unit_test
  BudgetControllerTest.cpp
)

add_executable(dasall_runtime_checkpoint_manager_unit_test
  CheckpointManagerTest.cpp
)

add_executable(dasall_runtime_recovery_manager_unit_test
  RecoveryManagerTest.cpp
)

add_executable(dasall_runtime_session_type_surface_unit_test
  SessionTypeSurfaceTest.cpp
)

add_executable(dasall_runtime_scheduler_surface_unit_test
  SchedulerTest.cpp
)

add_executable(dasall_runtime_checkpoint_state_mapper_unit_test
  CheckpointStateMapperTest.cpp
)

add_executable(dasall_runtime_transition_guard_table_unit_test
  TransitionGuardTableTest.cpp
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

target_link_libraries(dasall_agent_fsm_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_agent_fsm_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall_budget_controller_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_budget_controller_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall_runtime_checkpoint_manager_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_runtime_checkpoint_manager_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall_runtime_recovery_manager_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_runtime_recovery_manager_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall_runtime_session_type_surface_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_link_libraries(dasall_runtime_scheduler_surface_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_link_libraries(dasall_runtime_checkpoint_state_mapper_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_link_libraries(dasall_runtime_transition_guard_table_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_runtime_checkpoint_state_mapper_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

target_include_directories(dasall_runtime_transition_guard_table_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

dasall_apply_common_options(dasall_runtime_smoke_test)
dasall_apply_common_options(dasall_runtime_error_code_unit_test)
dasall_apply_common_options(dasall_runtime_cancellation_token_unit_test)
dasall_apply_common_options(dasall_agent_fsm_unit_test)
dasall_apply_common_options(dasall_budget_controller_unit_test)
dasall_apply_common_options(dasall_runtime_checkpoint_manager_unit_test)
dasall_apply_common_options(dasall_runtime_recovery_manager_unit_test)
dasall_apply_common_options(dasall_runtime_session_type_surface_unit_test)
dasall_apply_common_options(dasall_runtime_scheduler_surface_unit_test)
dasall_apply_common_options(dasall_runtime_checkpoint_state_mapper_unit_test)
dasall_apply_common_options(dasall_runtime_transition_guard_table_unit_test)

add_test(NAME dasall_runtime_smoke_test COMMAND dasall_runtime_smoke_test)
set_tests_properties(dasall_runtime_smoke_test PROPERTIES LABELS "unit")

add_test(NAME RuntimeErrorCodeTest COMMAND dasall_runtime_error_code_unit_test)
set_tests_properties(RuntimeErrorCodeTest PROPERTIES LABELS "unit")

add_test(NAME CancellationTokenTest COMMAND dasall_runtime_cancellation_token_unit_test)
set_tests_properties(CancellationTokenTest PROPERTIES LABELS "unit")

add_test(NAME AgentFsmTest COMMAND dasall_agent_fsm_unit_test)
set_tests_properties(AgentFsmTest PROPERTIES LABELS "unit")

add_test(NAME BudgetControllerTest COMMAND dasall_budget_controller_unit_test)
set_tests_properties(BudgetControllerTest PROPERTIES LABELS "unit")

add_test(NAME CheckpointManagerTest COMMAND dasall_runtime_checkpoint_manager_unit_test)
set_tests_properties(CheckpointManagerTest PROPERTIES LABELS "unit")

add_test(NAME RecoveryManagerTest COMMAND dasall_runtime_recovery_manager_unit_test)
set_tests_properties(RecoveryManagerTest PROPERTIES LABELS "unit")

add_test(NAME SessionTypeSurfaceTest COMMAND dasall_runtime_session_type_surface_unit_test)
set_tests_properties(SessionTypeSurfaceTest PROPERTIES LABELS "unit")

add_test(NAME SchedulerTest COMMAND dasall_runtime_scheduler_surface_unit_test)
set_tests_properties(SchedulerTest PROPERTIES LABELS "unit")

add_test(NAME CheckpointStateMapperTest COMMAND dasall_runtime_checkpoint_state_mapper_unit_test)
set_tests_properties(CheckpointStateMapperTest PROPERTIES LABELS "unit")

add_test(NAME TransitionGuardTableTest COMMAND dasall_runtime_transition_guard_table_unit_test)
set_tests_properties(TransitionGuardTableTest PROPERTIES LABELS "unit")
