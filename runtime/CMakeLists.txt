add_library(dasall_runtime STATIC)

target_sources(dasall_runtime
  PRIVATE
    src/AgentFacade.cpp
    src/AgentOrchestrator.cpp
    src/budget/BudgetController.cpp
    src/checkpoint/CheckpointManager.cpp
    src/checkpoint/CheckpointStateMapper.cpp
    src/fsm/AgentFsm.cpp
    src/fsm/TransitionGuardTable.cpp
    src/health/RuntimeHealthProbe.cpp
    src/maintenance/BackgroundMaintenanceHooks.cpp
    src/recovery/RecoveryManager.cpp
    src/safety/SafeModeController.cpp
    src/scheduling/Scheduler.cpp
    src/session/SessionManager.cpp
    src/telemetry/RuntimeEventBus.cpp
    src/telemetry/RuntimeTelemetryBridge.cpp
)

target_compile_features(dasall_runtime PUBLIC cxx_std_20)

target_include_directories(dasall_runtime
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_SOURCE_DIR}/knowledge/include
)

target_link_libraries(dasall_runtime
  PUBLIC
    dasall_contracts
    dasall_profiles
  PRIVATE
    dasall_cognition
    dasall_memory
    dasall_tools
)

dasall_apply_common_options(dasall_runtime)
