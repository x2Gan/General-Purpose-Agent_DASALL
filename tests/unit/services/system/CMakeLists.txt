add_executable(dasall_system_snapshot_lane_unit_test
  SystemSnapshotLaneTest.cpp
)

target_link_libraries(dasall_system_snapshot_lane_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_system_snapshot_lane_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_system_snapshot_lane_unit_test)

add_test(NAME SystemSnapshotLaneTest COMMAND dasall_system_snapshot_lane_unit_test)
set_tests_properties(SystemSnapshotLaneTest PROPERTIES LABELS "unit")