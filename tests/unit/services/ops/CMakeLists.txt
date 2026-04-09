add_executable(dasall_service_config_adapter_unit_test
  ServiceConfigAdapterTest.cpp
)

target_link_libraries(dasall_service_config_adapter_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_service_config_adapter_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
    ${CMAKE_SOURCE_DIR}/profiles/include
)

dasall_apply_common_options(dasall_service_config_adapter_unit_test)

add_test(NAME ServiceConfigAdapterTest COMMAND dasall_service_config_adapter_unit_test)
set_tests_properties(ServiceConfigAdapterTest PROPERTIES LABELS "unit")

add_executable(dasall_service_health_probe_unit_test
  ServiceHealthProbeTest.cpp
)

target_link_libraries(dasall_service_health_probe_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_service_health_probe_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
    ${CMAKE_SOURCE_DIR}/infra/include
)

dasall_apply_common_options(dasall_service_health_probe_unit_test)

add_test(NAME ServiceHealthProbeTest COMMAND dasall_service_health_probe_unit_test)
set_tests_properties(ServiceHealthProbeTest PROPERTIES LABELS "unit")