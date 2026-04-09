add_executable(dasall_data_projection_cache_unit_test
  DataProjectionCacheTest.cpp
)

target_link_libraries(dasall_data_projection_cache_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_data_projection_cache_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_data_projection_cache_unit_test)

add_test(NAME DataProjectionCacheTest COMMAND dasall_data_projection_cache_unit_test)
set_tests_properties(DataProjectionCacheTest PROPERTIES LABELS "unit")

add_executable(dasall_data_query_lane_unit_test
  DataQueryLaneTest.cpp
)

target_link_libraries(dasall_data_query_lane_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_data_query_lane_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_data_query_lane_unit_test)

add_test(NAME DataQueryLaneTest COMMAND dasall_data_query_lane_unit_test)
set_tests_properties(DataQueryLaneTest PROPERTIES LABELS "unit")