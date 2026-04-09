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

add_executable(dasall_execution_query_lane_unit_test
  ExecutionQueryLaneTest.cpp
)

target_link_libraries(dasall_execution_query_lane_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_execution_query_lane_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_execution_query_lane_unit_test)

add_test(NAME ExecutionQueryLaneTest COMMAND dasall_execution_query_lane_unit_test)
set_tests_properties(ExecutionQueryLaneTest PROPERTIES LABELS "unit")

add_executable(dasall_compensation_catalog_unit_test
  CompensationCatalogTest.cpp
)

target_link_libraries(dasall_compensation_catalog_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_compensation_catalog_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_compensation_catalog_unit_test)

add_test(NAME CompensationCatalogTest COMMAND dasall_compensation_catalog_unit_test)
set_tests_properties(CompensationCatalogTest PROPERTIES LABELS "unit")