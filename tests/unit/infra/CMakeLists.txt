add_executable(dasall_infra_context_unit_test
  InfraContextTest.cpp
)

target_link_libraries(dasall_infra_context_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_infra_context_unit_test)

add_test(NAME InfraContextUnitTest COMMAND dasall_infra_context_unit_test)
set_tests_properties(InfraContextUnitTest PROPERTIES LABELS "unit")

add_executable(dasall_log_event_unit_test
  LogEventTest.cpp
)

target_link_libraries(dasall_log_event_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_log_event_unit_test)

add_test(NAME LogEventUnitTest COMMAND dasall_log_event_unit_test)
set_tests_properties(LogEventUnitTest PROPERTIES LABELS "unit")

add_executable(dasall_audit_event_unit_test
  AuditEventTest.cpp
)

target_link_libraries(dasall_audit_event_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_audit_event_unit_test)

add_test(NAME AuditEventUnitTest COMMAND dasall_audit_event_unit_test)
set_tests_properties(AuditEventUnitTest PROPERTIES LABELS "unit")

add_executable(dasall_health_snapshot_unit_test
  HealthSnapshotTest.cpp
)

target_link_libraries(dasall_health_snapshot_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_health_snapshot_unit_test)

add_test(NAME HealthSnapshotUnitTest COMMAND dasall_health_snapshot_unit_test)
set_tests_properties(HealthSnapshotUnitTest PROPERTIES LABELS "unit")

add_executable(dasall_infra_service_facade_unit_test
  InfraServiceFacadeTest.cpp
)

target_link_libraries(dasall_infra_service_facade_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_infra_service_facade_unit_test)

add_test(NAME InfraServiceFacadeTest COMMAND dasall_infra_service_facade_unit_test)
set_tests_properties(InfraServiceFacadeTest PROPERTIES LABELS "unit")

add_executable(dasall_logger_interface_unit_test
  LoggerInterfaceTest.cpp
)

target_link_libraries(dasall_logger_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_logger_interface_unit_test)

add_test(NAME LoggerInterfaceTest COMMAND dasall_logger_interface_unit_test)
set_tests_properties(LoggerInterfaceTest PROPERTIES LABELS "unit")