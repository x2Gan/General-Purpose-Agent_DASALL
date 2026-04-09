add_executable(dasall_service_header_layout_unit_test
  ServiceHeaderLayoutTest.cpp
)

target_link_libraries(dasall_service_header_layout_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

dasall_apply_common_options(dasall_service_header_layout_unit_test)

add_test(NAME ServiceHeaderLayoutTest COMMAND dasall_service_header_layout_unit_test)
set_tests_properties(ServiceHeaderLayoutTest PROPERTIES LABELS "unit")

add_executable(dasall_service_context_builder_unit_test
  ServiceContextBuilderTest.cpp
)

target_link_libraries(dasall_service_context_builder_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_service_context_builder_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_service_context_builder_unit_test)

add_test(NAME ServiceContextBuilderTest COMMAND dasall_service_context_builder_unit_test)
set_tests_properties(ServiceContextBuilderTest PROPERTIES LABELS "unit")

add_executable(dasall_service_facade_unit_test
  ServiceFacadeTest.cpp
)

target_link_libraries(dasall_service_facade_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_service_facade_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_service_facade_unit_test)

add_test(NAME ServiceFacadeTest COMMAND dasall_service_facade_unit_test)
set_tests_properties(ServiceFacadeTest PROPERTIES LABELS "unit")

add_subdirectory(adapters)
add_subdirectory(execution)
add_subdirectory(data)
add_subdirectory(mapping)