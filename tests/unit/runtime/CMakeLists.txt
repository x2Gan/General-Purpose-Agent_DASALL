add_executable(dasall_runtime_smoke_test
  RuntimeSmokeTest.cpp
)

add_executable(dasall_runtime_control_plane_surface_unit_test
  RuntimeControlPlaneSurfaceTest.cpp
)

add_executable(dasall_runtime_agent_orchestrator_skeleton_unit_test
  AgentOrchestratorSkeletonTest.cpp
)

add_executable(dasall_runtime_agent_orchestrator_controller_assembly_unit_test
  AgentOrchestratorControllerAssemblyTest.cpp
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

add_executable(dasall_runtime_session_manager_unit_test
  SessionManagerTest.cpp
)

add_executable(dasall_runtime_scheduler_surface_unit_test
  SchedulerTest.cpp
)

add_executable(dasall_runtime_safe_mode_controller_unit_test
  SafeModeControllerTest.cpp
)

add_executable(dasall_runtime_event_bus_unit_test
  RuntimeEventBusTest.cpp
)

add_executable(dasall_runtime_telemetry_bridge_unit_test
  RuntimeTelemetryBridgeTest.cpp
)

add_executable(dasall_runtime_health_probe_unit_test
  RuntimeHealthProbeTest.cpp
)

add_executable(dasall_runtime_background_maintenance_hook_unit_test
  RuntimeBackgroundMaintenanceHookTest.cpp
)

add_executable(dasall_runtime_checkpoint_state_mapper_unit_test
  CheckpointStateMapperTest.cpp
)

add_executable(dasall_runtime_transition_guard_table_unit_test
  TransitionGuardTableTest.cpp
)

add_executable(dasall_runtime_cognition_loop_smoke_unit_test
  RuntimeCognitionLoopSmokeTest.cpp
)

target_link_libraries(dasall_runtime_smoke_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_link_libraries(dasall_runtime_control_plane_surface_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_runtime_control_plane_surface_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/fixtures/runtime
)

target_link_libraries(dasall_runtime_agent_orchestrator_skeleton_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_runtime_agent_orchestrator_skeleton_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall_runtime_agent_orchestrator_controller_assembly_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
    dasall_memory
)

target_include_directories(dasall_runtime_agent_orchestrator_controller_assembly_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
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

target_include_directories(dasall_runtime_session_manager_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall_runtime_session_type_surface_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_link_libraries(dasall_runtime_session_manager_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_link_libraries(dasall_runtime_scheduler_surface_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_runtime_scheduler_surface_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall_runtime_safe_mode_controller_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_runtime_safe_mode_controller_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall_runtime_event_bus_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_runtime_event_bus_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall_runtime_telemetry_bridge_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_runtime_telemetry_bridge_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall_runtime_health_probe_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_runtime_health_probe_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
)

target_link_libraries(dasall_runtime_background_maintenance_hook_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
)

target_include_directories(dasall_runtime_background_maintenance_hook_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
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

target_link_libraries(dasall_runtime_cognition_loop_smoke_unit_test
  PRIVATE
    dasall_test_support
    dasall_runtime
    dasall_memory
    dasall_sqlite3
    dasall_tools
)

target_include_directories(dasall_runtime_cognition_loop_smoke_unit_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime/src
    ${PROJECT_SOURCE_DIR}/cognition/include
    ${PROJECT_SOURCE_DIR}/memory/src
    ${PROJECT_SOURCE_DIR}/tools/src
    ${PROJECT_SOURCE_DIR}/services/include
    ${PROJECT_SOURCE_DIR}/tests/fixtures/runtime
)

target_compile_definitions(dasall_runtime_cognition_loop_smoke_unit_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

dasall_apply_common_options(dasall_runtime_smoke_test)
dasall_apply_common_options(dasall_runtime_control_plane_surface_unit_test)
dasall_apply_common_options(dasall_runtime_agent_orchestrator_skeleton_unit_test)
dasall_apply_common_options(dasall_runtime_agent_orchestrator_controller_assembly_unit_test)
dasall_apply_common_options(dasall_runtime_error_code_unit_test)
dasall_apply_common_options(dasall_runtime_cancellation_token_unit_test)
dasall_apply_common_options(dasall_agent_fsm_unit_test)
dasall_apply_common_options(dasall_budget_controller_unit_test)
dasall_apply_common_options(dasall_runtime_checkpoint_manager_unit_test)
dasall_apply_common_options(dasall_runtime_recovery_manager_unit_test)
dasall_apply_common_options(dasall_runtime_session_type_surface_unit_test)
dasall_apply_common_options(dasall_runtime_session_manager_unit_test)
dasall_apply_common_options(dasall_runtime_scheduler_surface_unit_test)
dasall_apply_common_options(dasall_runtime_safe_mode_controller_unit_test)
dasall_apply_common_options(dasall_runtime_event_bus_unit_test)
dasall_apply_common_options(dasall_runtime_telemetry_bridge_unit_test)
dasall_apply_common_options(dasall_runtime_health_probe_unit_test)
dasall_apply_common_options(dasall_runtime_background_maintenance_hook_unit_test)
dasall_apply_common_options(dasall_runtime_checkpoint_state_mapper_unit_test)
dasall_apply_common_options(dasall_runtime_transition_guard_table_unit_test)
dasall_apply_common_options(dasall_runtime_cognition_loop_smoke_unit_test)

add_test(NAME RuntimeBuildLivenessSmokeTest COMMAND dasall_runtime_smoke_test)
set_tests_properties(RuntimeBuildLivenessSmokeTest PROPERTIES LABELS "build-liveness;runtime")

add_test(NAME RuntimeControlPlaneSurfaceTest COMMAND dasall_runtime_control_plane_surface_unit_test)
set_tests_properties(RuntimeControlPlaneSurfaceTest PROPERTIES LABELS "unit;runtime")

add_test(NAME AgentOrchestratorSkeletonTest COMMAND dasall_runtime_agent_orchestrator_skeleton_unit_test)
set_tests_properties(AgentOrchestratorSkeletonTest PROPERTIES LABELS "unit")

add_test(NAME AgentOrchestratorControllerAssemblyTest COMMAND dasall_runtime_agent_orchestrator_controller_assembly_unit_test)
set_tests_properties(AgentOrchestratorControllerAssemblyTest PROPERTIES LABELS "unit")

add_test(NAME RuntimeErrorCodeTest COMMAND dasall_runtime_error_code_unit_test)
set_tests_properties(RuntimeErrorCodeTest PROPERTIES LABELS "unit")

add_test(NAME CancellationTokenTest COMMAND dasall_runtime_cancellation_token_unit_test)
set_tests_properties(CancellationTokenTest PROPERTIES LABELS "unit")

add_test(NAME RuntimeCognitionLoopSmokeTest COMMAND dasall_runtime_cognition_loop_smoke_unit_test)
set_tests_properties(RuntimeCognitionLoopSmokeTest PROPERTIES LABELS "unit;runtime")

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

add_test(NAME SessionManagerTest COMMAND dasall_runtime_session_manager_unit_test)
set_tests_properties(SessionManagerTest PROPERTIES LABELS "unit")

add_test(NAME SchedulerTest COMMAND dasall_runtime_scheduler_surface_unit_test)
set_tests_properties(SchedulerTest PROPERTIES LABELS "unit")

add_test(NAME SafeModeControllerTest COMMAND dasall_runtime_safe_mode_controller_unit_test)
set_tests_properties(SafeModeControllerTest PROPERTIES LABELS "unit")

add_test(NAME RuntimeEventBusTest COMMAND dasall_runtime_event_bus_unit_test)
set_tests_properties(RuntimeEventBusTest PROPERTIES LABELS "unit")

add_test(NAME RuntimeTelemetryBridgeTest COMMAND dasall_runtime_telemetry_bridge_unit_test)
set_tests_properties(RuntimeTelemetryBridgeTest PROPERTIES LABELS "unit")

add_test(NAME RuntimeHealthProbeTest COMMAND dasall_runtime_health_probe_unit_test)
set_tests_properties(RuntimeHealthProbeTest PROPERTIES LABELS "unit")

add_test(NAME RuntimeBackgroundMaintenanceHookTest COMMAND dasall_runtime_background_maintenance_hook_unit_test)
set_tests_properties(RuntimeBackgroundMaintenanceHookTest PROPERTIES LABELS "unit")

add_test(NAME CheckpointStateMapperTest COMMAND dasall_runtime_checkpoint_state_mapper_unit_test)
set_tests_properties(CheckpointStateMapperTest PROPERTIES LABELS "unit")

add_test(NAME TransitionGuardTableTest COMMAND dasall_runtime_transition_guard_table_unit_test)
set_tests_properties(TransitionGuardTableTest PROPERTIES LABELS "unit")
