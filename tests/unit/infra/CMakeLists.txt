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
  AuditTypesTest.cpp
)

target_link_libraries(dasall_audit_event_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_audit_event_unit_test)

add_test(NAME AuditTypesTest COMMAND dasall_audit_event_unit_test)
set_tests_properties(AuditTypesTest PROPERTIES LABELS "unit")

add_executable(dasall_health_probe_interface_unit_test
  health/HealthProbeInterfaceTest.cpp
)

target_link_libraries(dasall_health_probe_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_health_probe_interface_unit_test)

add_test(NAME HealthProbeInterfaceTest COMMAND dasall_health_probe_interface_unit_test)
set_tests_properties(HealthProbeInterfaceTest PROPERTIES LABELS "unit")

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

add_executable(dasall_metric_exporter_interface_unit_test
  MetricsExporterInterfaceTest.cpp
)

target_link_libraries(dasall_metric_exporter_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metric_exporter_interface_unit_test)

add_test(NAME MetricsExporterInterfaceTest COMMAND dasall_metric_exporter_interface_unit_test)
set_tests_properties(MetricsExporterInterfaceTest PROPERTIES LABELS "unit")

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

add_executable(dasall_metrics_provider_interface_unit_test
  MetricsProviderInterfaceTest.cpp
)

target_link_libraries(dasall_metrics_provider_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_provider_interface_unit_test)

add_test(NAME MetricsProviderInterfaceTest COMMAND dasall_metrics_provider_interface_unit_test)
set_tests_properties(MetricsProviderInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_meter_interface_unit_test
  MetricsMeterInterfaceTest.cpp
)

target_link_libraries(dasall_meter_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_meter_interface_unit_test)

add_test(NAME MetricsMeterInterfaceTest COMMAND dasall_meter_interface_unit_test)
set_tests_properties(MetricsMeterInterfaceTest PROPERTIES LABELS "unit")

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

add_executable(dasall_audit_logger_interface_unit_test
  AuditLoggerInterfaceTest.cpp
)

target_link_libraries(dasall_audit_logger_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_audit_logger_interface_unit_test)

add_test(NAME AuditInterfaceCompileTest COMMAND dasall_audit_logger_interface_unit_test)
set_tests_properties(AuditInterfaceCompileTest PROPERTIES LABELS "unit")

add_executable(dasall_audit_service_fallback_unit_test
  AuditServiceFallbackTest.cpp
)

target_link_libraries(dasall_audit_service_fallback_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_audit_service_fallback_unit_test)

add_test(NAME AuditServiceFallbackTest COMMAND dasall_audit_service_fallback_unit_test)
set_tests_properties(AuditServiceFallbackTest PROPERTIES LABELS "unit")

add_executable(dasall_audit_export_filter_unit_test
  AuditExportFilterTest.cpp
)

target_link_libraries(dasall_audit_export_filter_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_audit_export_filter_unit_test)

add_test(NAME AuditExportFilterTest COMMAND dasall_audit_export_filter_unit_test)
set_tests_properties(AuditExportFilterTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_snapshot_compatibility_unit_test
  PolicySnapshotCompatibilityTest.cpp
)

target_link_libraries(dasall_policy_snapshot_compatibility_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_snapshot_compatibility_unit_test)

add_test(NAME PolicySnapshotCompatibilityTest COMMAND dasall_policy_snapshot_compatibility_unit_test)
set_tests_properties(PolicySnapshotCompatibilityTest PROPERTIES LABELS "unit")

add_executable(dasall_diagnostics_types_unit_test
  DiagnosticsTypesTest.cpp
)

target_link_libraries(dasall_diagnostics_types_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_diagnostics_types_unit_test)

add_test(NAME DiagnosticsTypesTest COMMAND dasall_diagnostics_types_unit_test)
set_tests_properties(DiagnosticsTypesTest PROPERTIES LABELS "unit")

add_executable(dasall_diagnostics_snapshot_export_unit_test
  DiagnosticsSnapshotExportTest.cpp
)

target_link_libraries(dasall_diagnostics_snapshot_export_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_diagnostics_snapshot_export_unit_test)

add_test(NAME DiagnosticsSnapshotExportTest COMMAND dasall_diagnostics_snapshot_export_unit_test)
set_tests_properties(DiagnosticsSnapshotExportTest PROPERTIES LABELS "unit")

add_executable(dasall_config_types_unit_test
  ConfigTypesTest.cpp
)

target_link_libraries(dasall_config_types_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_types_unit_test)

add_test(NAME ConfigTypesTest COMMAND dasall_config_types_unit_test)
set_tests_properties(ConfigTypesTest PROPERTIES LABELS "unit")

add_executable(dasall_config_center_interface_unit_test
  ConfigCenterInterfaceTest.cpp
)

target_link_libraries(dasall_config_center_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_center_interface_unit_test)

add_test(NAME ConfigCenterInterfaceTest COMMAND dasall_config_center_interface_unit_test)
set_tests_properties(ConfigCenterInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_config_loader_interface_unit_test
  ConfigLoaderInterfaceTest.cpp
)

target_link_libraries(dasall_config_loader_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_loader_interface_unit_test)

add_test(NAME ConfigLoaderInterfaceTest COMMAND dasall_config_loader_interface_unit_test)
set_tests_properties(ConfigLoaderInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_config_validator_interface_unit_test
  ConfigValidatorInterfaceTest.cpp
)

target_link_libraries(dasall_config_validator_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_validator_interface_unit_test)

add_test(NAME ConfigValidatorInterfaceTest COMMAND dasall_config_validator_interface_unit_test)
set_tests_properties(ConfigValidatorInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_config_snapshot_store_interface_unit_test
  ConfigSnapshotStoreInterfaceTest.cpp
)

target_link_libraries(dasall_config_snapshot_store_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_snapshot_store_interface_unit_test)

add_test(NAME ConfigSnapshotStoreInterfaceTest COMMAND dasall_config_snapshot_store_interface_unit_test)
set_tests_properties(ConfigSnapshotStoreInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_config_publisher_interface_unit_test
  ConfigPublisherInterfaceTest.cpp
)

target_link_libraries(dasall_config_publisher_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_publisher_interface_unit_test)

add_test(NAME ConfigPublisherInterfaceTest COMMAND dasall_config_publisher_interface_unit_test)
set_tests_properties(ConfigPublisherInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_config_errors_unit_test
  ConfigErrorsTest.cpp
)

target_link_libraries(dasall_config_errors_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_errors_unit_test)

add_test(NAME ConfigErrorsTest COMMAND dasall_config_errors_unit_test)
set_tests_properties(ConfigErrorsTest PROPERTIES LABELS "unit")

add_executable(dasall_health_monitor_interface_unit_test
  HealthMonitorInterfaceTest.cpp
)

target_link_libraries(dasall_health_monitor_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_health_monitor_interface_unit_test)

add_test(NAME HealthMonitorInterfaceTest COMMAND dasall_health_monitor_interface_unit_test)
set_tests_properties(HealthMonitorInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_health_policy_interface_unit_test
  health/HealthPolicyInterfaceTest.cpp
)

target_link_libraries(dasall_health_policy_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_health_policy_interface_unit_test)

add_test(NAME HealthPolicyInterfaceTest COMMAND dasall_health_policy_interface_unit_test)
set_tests_properties(HealthPolicyInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_health_probe_types_unit_test
  health/ProbeTypesTest.cpp
)

target_link_libraries(dasall_health_probe_types_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_health_probe_types_unit_test)

add_test(NAME ProbeTypesUnitTest COMMAND dasall_health_probe_types_unit_test)
set_tests_properties(ProbeTypesUnitTest PROPERTIES LABELS "unit")

add_executable(dasall_health_recovery_hint_unit_test
  health/RecoveryHintTest.cpp
)

target_link_libraries(dasall_health_recovery_hint_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_health_recovery_hint_unit_test)

add_test(NAME RecoveryHintUnitTest COMMAND dasall_health_recovery_hint_unit_test)
set_tests_properties(RecoveryHintUnitTest PROPERTIES LABELS "unit")

add_executable(dasall_infra_error_code_unit_test
  InfraErrorCodeTest.cpp
)

target_link_libraries(dasall_infra_error_code_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_infra_error_code_unit_test)

add_test(NAME InfraErrorCodeUnitTest COMMAND dasall_infra_error_code_unit_test)
set_tests_properties(InfraErrorCodeUnitTest PROPERTIES LABELS "unit")