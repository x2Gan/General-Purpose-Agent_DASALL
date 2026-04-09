add_executable(dasall_service_audit_bridge_unit_test
  ServiceAuditBridgeTest.cpp
)

target_link_libraries(dasall_service_audit_bridge_unit_test
  PRIVATE
    dasall_test_support
    dasall_services
)

target_include_directories(dasall_service_audit_bridge_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/services/src
    ${CMAKE_SOURCE_DIR}/infra/include
)

dasall_apply_common_options(dasall_service_audit_bridge_unit_test)

add_test(NAME ServiceAuditBridgeTest COMMAND dasall_service_audit_bridge_unit_test)
set_tests_properties(ServiceAuditBridgeTest PROPERTIES LABELS "unit")