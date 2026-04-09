add_executable(dasall_execution_command_lane_unit_test
  ExecutionCommandLaneTest.cpp
)

target_link_libraries(dasall_execution_command_lane_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_execution_command_lane_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_execution_command_lane_unit_test)

add_test(NAME ExecutionCommandLaneTest COMMAND dasall_execution_command_lane_unit_test)
set_tests_properties(ExecutionCommandLaneTest PROPERTIES LABELS "unit")