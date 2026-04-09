add_executable(dasall_result_mapper_unit_test
  ResultMapperTest.cpp
)

target_link_libraries(dasall_result_mapper_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_result_mapper_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_result_mapper_unit_test)

add_test(NAME ResultMapperTest COMMAND dasall_result_mapper_unit_test)
set_tests_properties(ResultMapperTest PROPERTIES LABELS "unit")