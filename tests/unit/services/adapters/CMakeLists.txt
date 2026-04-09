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

add_executable(dasall_adapter_bridge_unit_test
  AdapterBridgeTest.cpp
)

target_link_libraries(dasall_adapter_bridge_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_adapter_bridge_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_adapter_bridge_unit_test)

add_test(NAME AdapterBridgeTest COMMAND dasall_adapter_bridge_unit_test)
set_tests_properties(AdapterBridgeTest PROPERTIES LABELS "unit")

add_executable(dasall_local_platform_adapter_unit_test
  LocalPlatformAdapterTest.cpp
)

target_link_libraries(dasall_local_platform_adapter_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_local_platform_adapter_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_local_platform_adapter_unit_test)

add_test(NAME LocalPlatformAdapterTest COMMAND dasall_local_platform_adapter_unit_test)
set_tests_properties(LocalPlatformAdapterTest PROPERTIES LABELS "unit")

add_executable(dasall_local_service_adapter_unit_test
  LocalServiceAdapterTest.cpp
)

target_link_libraries(dasall_local_service_adapter_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_local_service_adapter_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
)

dasall_apply_common_options(dasall_local_service_adapter_unit_test)

add_test(NAME LocalServiceAdapterTest COMMAND dasall_local_service_adapter_unit_test)
set_tests_properties(LocalServiceAdapterTest PROPERTIES LABELS "unit")