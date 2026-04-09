add_executable(dasall_adapter_router_unit_test
  AdapterRouterTest.cpp
)

target_link_libraries(dasall_adapter_router_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_adapter_router_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_adapter_router_unit_test)

add_test(NAME AdapterRouterTest COMMAND dasall_adapter_router_unit_test)
set_tests_properties(AdapterRouterTest PROPERTIES LABELS "unit")