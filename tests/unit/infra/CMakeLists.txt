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

add_executable(dasall_metrics_config_policy_interface_unit_test
  MetricsConfigPolicyInterfaceTest.cpp
)

target_link_libraries(dasall_metrics_config_policy_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_config_policy_interface_unit_test)

add_test(NAME MetricsConfigPolicyInterfaceTest COMMAND dasall_metrics_config_policy_interface_unit_test)
set_tests_properties(MetricsConfigPolicyInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_metrics_errors_unit_test
  MetricsErrorsTest.cpp
)

target_link_libraries(dasall_metrics_errors_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_errors_unit_test)

add_test(NAME MetricsErrorsTest COMMAND dasall_metrics_errors_unit_test)
set_tests_properties(MetricsErrorsTest PROPERTIES LABELS "unit")

add_executable(dasall_metrics_snapshots_unit_test
  MetricsSnapshotsTest.cpp
)

target_link_libraries(dasall_metrics_snapshots_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_snapshots_unit_test)

add_test(NAME MetricsSnapshotsTest COMMAND dasall_metrics_snapshots_unit_test)
set_tests_properties(MetricsSnapshotsTest PROPERTIES LABELS "unit")

add_executable(dasall_metrics_health_probe_interface_unit_test
  MetricsHealthProbeInterfaceTest.cpp
)

target_link_libraries(dasall_metrics_health_probe_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_health_probe_interface_unit_test)

add_test(NAME MetricsHealthProbeInterfaceTest COMMAND dasall_metrics_health_probe_interface_unit_test)
set_tests_properties(MetricsHealthProbeInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_metric_types_unit_test
  MetricTypesTest.cpp
)

target_link_libraries(dasall_metric_types_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metric_types_unit_test)

add_test(NAME MetricTypesTest COMMAND dasall_metric_types_unit_test)
set_tests_properties(MetricTypesTest PROPERTIES LABELS "unit")

add_executable(dasall_trace_types_unit_test
  tracing/TraceTypesTest.cpp
)

target_link_libraries(dasall_trace_types_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_trace_types_unit_test)

add_test(NAME TraceTypesTest COMMAND dasall_trace_types_unit_test)
set_tests_properties(TraceTypesTest PROPERTIES LABELS "unit")

add_executable(dasall_span_interface_unit_test
  tracing/SpanInterfaceTest.cpp
)

target_link_libraries(dasall_span_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_span_interface_unit_test)

add_test(NAME SpanInterfaceTest COMMAND dasall_span_interface_unit_test)
set_tests_properties(SpanInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_trace_context_propagator_interface_unit_test
  tracing/TraceContextPropagatorInterfaceTest.cpp
)

target_link_libraries(dasall_trace_context_propagator_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_trace_context_propagator_interface_unit_test)

add_test(NAME TraceContextPropagatorInterfaceTest COMMAND dasall_trace_context_propagator_interface_unit_test)
set_tests_properties(TraceContextPropagatorInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_trace_errors_unit_test
  tracing/TraceErrorsTest.cpp
)

target_link_libraries(dasall_trace_errors_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_trace_errors_unit_test)

add_test(NAME TraceErrorsTest COMMAND dasall_trace_errors_unit_test)
set_tests_properties(TraceErrorsTest PROPERTIES LABELS "unit")

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

add_executable(dasall_secret_backend_interface_unit_test
  SecretBackendInterfaceTest.cpp
)

target_link_libraries(dasall_secret_backend_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_secret_backend_interface_unit_test)

add_test(NAME SecretBackendInterfaceTest COMMAND dasall_secret_backend_interface_unit_test)
set_tests_properties(SecretBackendInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_secret_errors_unit_test
  SecretErrorsTest.cpp
)

target_link_libraries(dasall_secret_errors_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_secret_errors_unit_test)

add_test(NAME SecretErrorsTest COMMAND dasall_secret_errors_unit_test)
set_tests_properties(SecretErrorsTest PROPERTIES LABELS "unit")

add_executable(dasall_secret_health_source_interface_unit_test
  SecretHealthSourceInterfaceTest.cpp
)

target_link_libraries(dasall_secret_health_source_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_secret_health_source_interface_unit_test)

add_test(NAME SecretHealthSourceInterfaceTest COMMAND dasall_secret_health_source_interface_unit_test)
set_tests_properties(SecretHealthSourceInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_secret_manager_interface_unit_test
  SecretManagerInterfaceTest.cpp
)

target_link_libraries(dasall_secret_manager_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_secret_manager_interface_unit_test)

add_test(NAME SecretManagerInterfaceTest COMMAND dasall_secret_manager_interface_unit_test)
set_tests_properties(SecretManagerInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_secret_secure_buffer_unit_test
  SecureBufferTest.cpp
)

target_link_libraries(dasall_secret_secure_buffer_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_secret_secure_buffer_unit_test)

add_test(NAME SecureBufferTest COMMAND dasall_secret_secure_buffer_unit_test)
set_tests_properties(SecureBufferTest PROPERTIES LABELS "unit")

add_executable(dasall_secret_types_unit_test
  SecretTypesTest.cpp
)

target_link_libraries(dasall_secret_types_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_secret_types_unit_test)

add_test(NAME SecretTypesTest COMMAND dasall_secret_types_unit_test)
set_tests_properties(SecretTypesTest PROPERTIES LABELS "unit")

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

add_executable(dasall_policy_manager_interface_unit_test
  PolicyManagerInterfaceTest.cpp
)

target_link_libraries(dasall_policy_manager_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_manager_interface_unit_test)

add_test(NAME PolicyManagerInterfaceTest COMMAND dasall_policy_manager_interface_unit_test)
set_tests_properties(PolicyManagerInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_loader_interface_unit_test
  PolicyLoaderInterfaceTest.cpp
)

target_link_libraries(dasall_policy_loader_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_loader_interface_unit_test)

add_test(NAME PolicyLoaderInterfaceTest COMMAND dasall_policy_loader_interface_unit_test)
set_tests_properties(PolicyLoaderInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_schema_validator_interface_unit_test
  PolicySchemaValidatorInterfaceTest.cpp
)

target_link_libraries(dasall_policy_schema_validator_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_schema_validator_interface_unit_test)

add_test(NAME PolicySchemaValidatorInterfaceTest COMMAND dasall_policy_schema_validator_interface_unit_test)
set_tests_properties(PolicySchemaValidatorInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_snapshot_store_interface_unit_test
  PolicySnapshotStoreInterfaceTest.cpp
)

target_link_libraries(dasall_policy_snapshot_store_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_snapshot_store_interface_unit_test)

add_test(NAME PolicySnapshotStoreInterfaceTest COMMAND dasall_policy_snapshot_store_interface_unit_test)
set_tests_properties(PolicySnapshotStoreInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_types_rule_bundle_unit_test
  PolicyTypesRuleBundleTest.cpp
)

target_link_libraries(dasall_policy_types_rule_bundle_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_types_rule_bundle_unit_test)

add_test(NAME PolicyTypesRuleBundleTest COMMAND dasall_policy_types_rule_bundle_unit_test)
set_tests_properties(PolicyTypesRuleBundleTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_patch_validation_types_unit_test
  PolicyPatchValidationTypesTest.cpp
)

target_link_libraries(dasall_policy_patch_validation_types_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_patch_validation_types_unit_test)

add_test(NAME PolicyPatchValidationTypesTest COMMAND dasall_policy_patch_validation_types_unit_test)
set_tests_properties(PolicyPatchValidationTypesTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_snapshot_op_result_types_unit_test
  PolicySnapshotOpResultTypesTest.cpp
)

target_link_libraries(dasall_policy_snapshot_op_result_types_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_snapshot_op_result_types_unit_test)

add_test(NAME PolicySnapshotOpResultTypesTest COMMAND dasall_policy_snapshot_op_result_types_unit_test)
set_tests_properties(PolicySnapshotOpResultTypesTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_query_decision_ref_types_unit_test
  PolicyQueryDecisionRefTypesTest.cpp
)

target_link_libraries(dasall_policy_query_decision_ref_types_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_query_decision_ref_types_unit_test)

add_test(NAME PolicyQueryDecisionRefTypesTest COMMAND dasall_policy_query_decision_ref_types_unit_test)
set_tests_properties(PolicyQueryDecisionRefTypesTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_errors_unit_test
  PolicyErrorsTest.cpp
)

target_link_libraries(dasall_policy_errors_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_errors_unit_test)

add_test(NAME PolicyErrorsTest COMMAND dasall_policy_errors_unit_test)
set_tests_properties(PolicyErrorsTest PROPERTIES LABELS "unit")

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

add_executable(dasall_watchdog_service_interface_unit_test
  watchdog/WatchdogServiceInterfaceTest.cpp
)

target_link_libraries(dasall_watchdog_service_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_watchdog_service_interface_unit_test)

add_test(NAME WatchdogServiceInterfaceTest COMMAND dasall_watchdog_service_interface_unit_test)
set_tests_properties(WatchdogServiceInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_heartbeat_source_interface_unit_test
  watchdog/HeartbeatSourceInterfaceTest.cpp
)

target_link_libraries(dasall_heartbeat_source_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_heartbeat_source_interface_unit_test)

add_test(NAME HeartbeatSourceInterfaceTest COMMAND dasall_heartbeat_source_interface_unit_test)
set_tests_properties(HeartbeatSourceInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_timeout_policy_interface_unit_test
  watchdog/TimeoutPolicyInterfaceTest.cpp
)

target_link_libraries(dasall_timeout_policy_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_timeout_policy_interface_unit_test)

add_test(NAME TimeoutPolicyInterfaceTest COMMAND dasall_timeout_policy_interface_unit_test)
set_tests_properties(TimeoutPolicyInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_watched_entity_descriptor_unit_test
  watchdog/WatchedEntityDescriptorTest.cpp
)

target_link_libraries(dasall_watched_entity_descriptor_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_watched_entity_descriptor_unit_test)

add_test(NAME WatchedEntityDescriptorTest COMMAND dasall_watched_entity_descriptor_unit_test)
set_tests_properties(WatchedEntityDescriptorTest PROPERTIES LABELS "unit")

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

add_executable(dasall_ota_types_unit_test
  OTATypesTest.cpp
)

target_link_libraries(dasall_ota_types_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_ota_types_unit_test)

add_test(NAME OTATypesCompileTest COMMAND dasall_ota_types_unit_test)
set_tests_properties(OTATypesCompileTest PROPERTIES LABELS "unit")

add_executable(dasall_ota_manager_interface_unit_test
  IOTAManagerInterfaceTest.cpp
)

target_link_libraries(dasall_ota_manager_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_ota_manager_interface_unit_test)

add_test(NAME OTAInterfaceCompileTest COMMAND dasall_ota_manager_interface_unit_test)
set_tests_properties(OTAInterfaceCompileTest PROPERTIES LABELS "unit")

add_executable(dasall_ota_package_verifier_interface_unit_test
  OTAPackageVerifierInterfaceTest.cpp
)

target_link_libraries(dasall_ota_package_verifier_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_ota_package_verifier_interface_unit_test)

add_test(NAME OTAPackageVerifierInterfaceTest COMMAND dasall_ota_package_verifier_interface_unit_test)
set_tests_properties(OTAPackageVerifierInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_install_executor_interface_unit_test
  InstallExecutorInterfaceTest.cpp
)

target_link_libraries(dasall_install_executor_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_install_executor_interface_unit_test)

add_test(NAME InstallExecutorInterfaceTest COMMAND dasall_install_executor_interface_unit_test)
set_tests_properties(InstallExecutorInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_boot_control_adapter_interface_unit_test
  BootControlAdapterInterfaceTest.cpp
)

target_link_libraries(dasall_boot_control_adapter_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_boot_control_adapter_interface_unit_test)

add_test(NAME BootControlAdapterInterfaceTest COMMAND dasall_boot_control_adapter_interface_unit_test)
set_tests_properties(BootControlAdapterInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_plugin_descriptor_unit_test
  plugin/PluginDescriptorTest.cpp
)

target_link_libraries(dasall_plugin_descriptor_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_plugin_descriptor_unit_test)

add_test(NAME PluginDescriptorFieldTest COMMAND dasall_plugin_descriptor_unit_test)
set_tests_properties(PluginDescriptorFieldTest PROPERTIES LABELS "unit")

add_executable(dasall_plugin_catalog_unit_test
  plugin/PluginCatalogTest.cpp
)

target_link_libraries(dasall_plugin_catalog_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_plugin_catalog_unit_test)

add_test(NAME PluginCatalogTest COMMAND dasall_plugin_catalog_unit_test)
set_tests_properties(PluginCatalogTest PROPERTIES LABELS "unit")

add_executable(dasall_plugin_error_code_unit_test
  plugin/PluginErrorCodeTest.cpp
)

target_link_libraries(dasall_plugin_error_code_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_plugin_error_code_unit_test)

add_test(NAME PluginErrorCodeTest COMMAND dasall_plugin_error_code_unit_test)
set_tests_properties(PluginErrorCodeTest PROPERTIES LABELS "unit")

add_executable(dasall_plugin_manager_interface_unit_test
  plugin/PluginManagerInterfaceTest.cpp
)

target_link_libraries(dasall_plugin_manager_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_plugin_manager_interface_unit_test)

add_test(NAME PluginManagerInterfaceCompileTest COMMAND dasall_plugin_manager_interface_unit_test)
set_tests_properties(PluginManagerInterfaceCompileTest PROPERTIES LABELS "unit")

add_executable(dasall_plugin_policy_gate_interface_unit_test
  plugin/PluginPolicyGateInterfaceTest.cpp
)

target_link_libraries(dasall_plugin_policy_gate_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_plugin_policy_gate_interface_unit_test)

add_test(NAME PluginPolicyGateInterfaceCompileTest COMMAND dasall_plugin_policy_gate_interface_unit_test)
set_tests_properties(PluginPolicyGateInterfaceCompileTest PROPERTIES LABELS "unit")