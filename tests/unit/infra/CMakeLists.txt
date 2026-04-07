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
set_tests_properties(LogEventUnitTest PROPERTIES LABELS "unit;logging")

function(dasall_register_audit_unit_test executable_target_name ctest_name source_file)
  add_executable(${executable_target_name}
    ${source_file}
  )

  target_link_libraries(${executable_target_name}
    PRIVATE
      dasall_test_support
      dasall_infra
  )

  dasall_apply_common_options(${executable_target_name})

  add_test(NAME ${ctest_name} COMMAND ${executable_target_name})
  set_tests_properties(${ctest_name} PROPERTIES LABELS "unit;audit")
endfunction()

dasall_register_audit_unit_test(
  dasall_audit_event_unit_test
  AuditTypesTest
  AuditTypesTest.cpp
)

target_include_directories(dasall_audit_event_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

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
set_tests_properties(HealthProbeInterfaceTest PROPERTIES LABELS "unit;health")

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
set_tests_properties(HealthSnapshotUnitTest PROPERTIES LABELS "unit;health")

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

add_executable(dasall_metrics_config_merge_unit_test
  metrics/MetricsConfigMergeTest.cpp
)

target_include_directories(dasall_metrics_config_merge_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_metrics_config_merge_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_config_merge_unit_test)

add_test(NAME MetricsConfigMergeTest COMMAND dasall_metrics_config_merge_unit_test)
set_tests_properties(MetricsConfigMergeTest PROPERTIES LABELS "unit;metrics")

add_executable(dasall_metrics_reader_scheduler_unit_test
  metrics/MetricsReaderSchedulerTest.cpp
)

target_include_directories(dasall_metrics_reader_scheduler_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_metrics_reader_scheduler_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_reader_scheduler_unit_test)

add_test(NAME MetricsReaderSchedulerTest COMMAND dasall_metrics_reader_scheduler_unit_test)
set_tests_properties(MetricsReaderSchedulerTest PROPERTIES LABELS "unit;metrics")

add_executable(dasall_metrics_exporter_adapter_unit_test
  metrics/MetricsExporterAdapterTest.cpp
)

target_include_directories(dasall_metrics_exporter_adapter_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_metrics_exporter_adapter_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_exporter_adapter_unit_test)

add_test(NAME MetricsExporterAdapterTest COMMAND dasall_metrics_exporter_adapter_unit_test)
set_tests_properties(MetricsExporterAdapterTest PROPERTIES LABELS "unit;metrics;failure")

add_executable(dasall_metrics_recovery_unit_test
  metrics/MetricsRecoveryTest.cpp
)

target_include_directories(dasall_metrics_recovery_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_metrics_recovery_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_recovery_unit_test)

add_test(NAME MetricsRecoveryTest COMMAND dasall_metrics_recovery_unit_test)
set_tests_properties(MetricsRecoveryTest PROPERTIES LABELS "unit;metrics;failure")

add_executable(dasall_metrics_logging_bridge_unit_test
  metrics/MetricsLoggingBridgeTest.cpp
)

target_include_directories(dasall_metrics_logging_bridge_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_metrics_logging_bridge_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_logging_bridge_unit_test)

add_test(NAME MetricsLoggingBridgeTest COMMAND dasall_metrics_logging_bridge_unit_test)
set_tests_properties(MetricsLoggingBridgeTest PROPERTIES LABELS "unit;metrics;failure")

add_executable(dasall_metrics_audit_bridge_unit_test
  metrics/MetricsAuditBridgeTest.cpp
)

target_include_directories(dasall_metrics_audit_bridge_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_metrics_audit_bridge_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_audit_bridge_unit_test)

add_test(NAME MetricsAuditBridgeTest COMMAND dasall_metrics_audit_bridge_unit_test)
set_tests_properties(MetricsAuditBridgeTest PROPERTIES LABELS "unit;metrics;failure")

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

add_executable(dasall_trace_config_unit_test
  tracing/TraceConfigTest.cpp
)

target_link_libraries(dasall_trace_config_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_trace_config_unit_test)

add_test(NAME TraceConfigTest COMMAND dasall_trace_config_unit_test)
set_tests_properties(TraceConfigTest PROPERTIES LABELS "unit;tracing")

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
set_tests_properties(TraceTypesTest PROPERTIES LABELS "unit;tracing")

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
set_tests_properties(SpanInterfaceTest PROPERTIES LABELS "unit;tracing")

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
set_tests_properties(TraceContextPropagatorInterfaceTest PROPERTIES LABELS "unit;tracing")

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
set_tests_properties(TraceErrorsTest PROPERTIES LABELS "unit;tracing")

add_executable(dasall_tracer_provider_impl_unit_test
  tracing/TracerProviderImplTest.cpp
)

target_include_directories(dasall_tracer_provider_impl_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_tracer_provider_impl_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_tracer_provider_impl_unit_test)

add_test(NAME TracerProviderImplTest COMMAND dasall_tracer_provider_impl_unit_test)
set_tests_properties(TracerProviderImplTest PROPERTIES LABELS "unit;tracing")

add_executable(dasall_sampling_policy_unit_test
  tracing/SamplingPolicyTest.cpp
)

target_include_directories(dasall_sampling_policy_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_sampling_policy_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_sampling_policy_unit_test)

add_test(NAME SamplingPolicyTest COMMAND dasall_sampling_policy_unit_test)
set_tests_properties(SamplingPolicyTest PROPERTIES LABELS "unit;tracing")

add_executable(dasall_batch_span_buffer_unit_test
  tracing/BatchSpanBufferTest.cpp
)

target_include_directories(dasall_batch_span_buffer_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_batch_span_buffer_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_batch_span_buffer_unit_test)

add_test(NAME BatchSpanBufferTest COMMAND dasall_batch_span_buffer_unit_test)
set_tests_properties(BatchSpanBufferTest PROPERTIES LABELS "unit;tracing;failure")

add_executable(dasall_batch_export_unit_test
  tracing/BatchExportTest.cpp
)

target_include_directories(dasall_batch_export_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_batch_export_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_batch_export_unit_test)

add_test(NAME BatchExportTest COMMAND dasall_batch_export_unit_test)
set_tests_properties(BatchExportTest PROPERTIES LABELS "unit;tracing;failure")

add_executable(dasall_trace_health_probe_unit_test
  tracing/TraceHealthProbeTest.cpp
)

target_include_directories(dasall_trace_health_probe_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_trace_health_probe_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_trace_health_probe_unit_test)

add_test(NAME TraceHealthProbeTest COMMAND dasall_trace_health_probe_unit_test)
set_tests_properties(TraceHealthProbeTest PROPERTIES LABELS "unit;tracing;failure")

add_executable(dasall_trace_metrics_bridge_unit_test
  tracing/TraceMetricsBridgeTest.cpp
)

target_include_directories(dasall_trace_metrics_bridge_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_trace_metrics_bridge_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_trace_metrics_bridge_unit_test)

add_test(NAME TraceMetricsBridgeTest COMMAND dasall_trace_metrics_bridge_unit_test)
set_tests_properties(TraceMetricsBridgeTest PROPERTIES LABELS "unit;tracing;failure")

add_executable(dasall_trace_audit_bridge_unit_test
  tracing/TraceAuditBridgeTest.cpp
)

target_include_directories(dasall_trace_audit_bridge_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_trace_audit_bridge_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_trace_audit_bridge_unit_test)

add_test(NAME TraceAuditBridgeTest COMMAND dasall_trace_audit_bridge_unit_test)
set_tests_properties(TraceAuditBridgeTest PROPERTIES LABELS "unit;tracing;failure")

add_executable(dasall_tracer_span_lifecycle_unit_test
  tracing/TracerSpanLifecycleTest.cpp
)

target_include_directories(dasall_tracer_span_lifecycle_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_tracer_span_lifecycle_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_tracer_span_lifecycle_unit_test)

add_test(NAME TracerSpanLifecycleTest COMMAND dasall_tracer_span_lifecycle_unit_test)
set_tests_properties(TracerSpanLifecycleTest PROPERTIES LABELS "unit;tracing")

add_executable(dasall_context_propagation_adapter_unit_test
  tracing/ContextPropagationAdapterTest.cpp
)

target_include_directories(dasall_context_propagation_adapter_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_context_propagation_adapter_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_context_propagation_adapter_unit_test)

add_test(NAME ContextPropagationAdapterTest COMMAND dasall_context_propagation_adapter_unit_test)
set_tests_properties(ContextPropagationAdapterTest PROPERTIES LABELS "unit;tracing")

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

add_executable(dasall_mock_secret_backend_unit_test
  secret/MockSecretBackendTest.cpp
)

target_include_directories(dasall_mock_secret_backend_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_mock_secret_backend_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_mock_secret_backend_unit_test)

add_test(NAME MockSecretBackendTest COMMAND dasall_mock_secret_backend_unit_test)
set_tests_properties(MockSecretBackendTest PROPERTIES LABELS "unit;secret")

add_executable(dasall_file_secret_backend_unit_test
  secret/FileSecretBackendTest.cpp
)

target_include_directories(dasall_file_secret_backend_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_file_secret_backend_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_file_secret_backend_unit_test)

add_test(NAME FileSecretBackendTest COMMAND dasall_file_secret_backend_unit_test)
set_tests_properties(FileSecretBackendTest PROPERTIES LABELS "unit;secret")

add_executable(dasall_secret_manager_facade_unit_test
  secret/SecretManagerFacadeTest.cpp
)

target_include_directories(dasall_secret_manager_facade_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_secret_manager_facade_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_secret_manager_facade_unit_test)

add_test(NAME SecretManagerFacadeTest COMMAND dasall_secret_manager_facade_unit_test)
set_tests_properties(SecretManagerFacadeTest PROPERTIES LABELS "unit;secret")

add_executable(dasall_secret_lease_registry_unit_test
  secret/SecretLeaseRegistryTest.cpp
)

target_include_directories(dasall_secret_lease_registry_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_secret_lease_registry_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_secret_lease_registry_unit_test)

add_test(NAME SecretLeaseRegistryTest COMMAND dasall_secret_lease_registry_unit_test)
set_tests_properties(SecretLeaseRegistryTest PROPERTIES LABELS "unit;secret")

add_executable(dasall_secret_rotation_coordinator_unit_test
  secret/SecretRotationCoordinatorTest.cpp
)

target_include_directories(dasall_secret_rotation_coordinator_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_secret_rotation_coordinator_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_secret_rotation_coordinator_unit_test)

add_test(NAME SecretRotationCoordinatorTest COMMAND dasall_secret_rotation_coordinator_unit_test)
set_tests_properties(SecretRotationCoordinatorTest PROPERTIES LABELS "unit;secret")

add_executable(dasall_secret_audit_bridge_unit_test
  secret/SecretAuditBridgeTest.cpp
)

target_include_directories(dasall_secret_audit_bridge_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_secret_audit_bridge_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_secret_audit_bridge_unit_test)

add_test(NAME SecretAuditBridgeTest COMMAND dasall_secret_audit_bridge_unit_test)
set_tests_properties(SecretAuditBridgeTest PROPERTIES LABELS "unit;secret")

add_executable(dasall_secret_health_probe_unit_test
  secret/SecretHealthProbeTest.cpp
)

target_include_directories(dasall_secret_health_probe_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_secret_health_probe_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_secret_health_probe_unit_test)

add_test(NAME SecretHealthProbeTest COMMAND dasall_secret_health_probe_unit_test)
set_tests_properties(SecretHealthProbeTest PROPERTIES LABELS "unit;secret")

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
set_tests_properties(SecretBackendInterfaceTest PROPERTIES LABELS "unit;secret")

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
set_tests_properties(SecretErrorsTest PROPERTIES LABELS "unit;secret")

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
set_tests_properties(SecretHealthSourceInterfaceTest PROPERTIES LABELS "unit;secret")

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
set_tests_properties(SecretManagerInterfaceTest PROPERTIES LABELS "unit;secret")

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
set_tests_properties(SecureBufferTest PROPERTIES LABELS "unit;secret")

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
set_tests_properties(SecretTypesTest PROPERTIES LABELS "unit;secret")

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

add_executable(dasall_metrics_facade_unit_test
  metrics/MetricsFacadeTest.cpp
)

target_include_directories(dasall_metrics_facade_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_metrics_facade_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_facade_unit_test)

add_test(NAME MetricsFacadeTest COMMAND dasall_metrics_facade_unit_test)
set_tests_properties(MetricsFacadeTest PROPERTIES LABELS "unit;metrics")

add_executable(dasall_instrument_registry_unit_test
  metrics/InstrumentRegistryTest.cpp
)

target_include_directories(dasall_instrument_registry_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_instrument_registry_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_instrument_registry_unit_test)

add_test(NAME InstrumentRegistryTest COMMAND dasall_instrument_registry_unit_test)
set_tests_properties(InstrumentRegistryTest PROPERTIES LABELS "unit;metrics")

add_executable(dasall_metrics_cardinality_guard_unit_test
  metrics/MetricsCardinalityGuardTest.cpp
)

target_include_directories(dasall_metrics_cardinality_guard_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_metrics_cardinality_guard_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_cardinality_guard_unit_test)

add_test(NAME MetricsCardinalityGuardTest COMMAND dasall_metrics_cardinality_guard_unit_test)
set_tests_properties(MetricsCardinalityGuardTest PROPERTIES LABELS "unit;metrics;failure")

add_executable(dasall_metrics_aggregation_unit_test
  metrics/MetricsAggregationTest.cpp
)

target_include_directories(dasall_metrics_aggregation_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_metrics_aggregation_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_metrics_aggregation_unit_test)

add_test(NAME MetricsAggregationTest COMMAND dasall_metrics_aggregation_unit_test)
set_tests_properties(MetricsAggregationTest PROPERTIES LABELS "unit;metrics")

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
set_tests_properties(LoggerInterfaceTest PROPERTIES LABELS "unit;logging")

add_executable(dasall_logging_errors_unit_test
  LoggingErrorsTest.cpp
)

target_link_libraries(dasall_logging_errors_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_logging_errors_unit_test)

add_test(NAME LoggingErrorsTest COMMAND dasall_logging_errors_unit_test)
set_tests_properties(LoggingErrorsTest PROPERTIES LABELS "unit;logging")

function(dasall_register_logging_unit_test executable_target_name ctest_name source_file)
  add_executable(${executable_target_name}
    ${source_file}
  )

  target_include_directories(${executable_target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/infra/src
  )

  target_link_libraries(${executable_target_name}
    PRIVATE
      dasall_test_support
      dasall_infra
  )

  dasall_apply_common_options(${executable_target_name})

  add_test(NAME ${ctest_name} COMMAND ${executable_target_name})
  set_tests_properties(${ctest_name} PROPERTIES LABELS "unit;logging")
endfunction()

dasall_register_logging_unit_test(
  dasall_logging_facade_unit_test
  LoggingFacadeTest
  logging/LoggingFacadeTest.cpp
)

dasall_register_logging_unit_test(
  dasall_sink_dispatcher_unit_test
  SinkDispatcherTest
  logging/SinkDispatcherTest.cpp
)

dasall_register_logging_unit_test(
  dasall_async_queue_controller_unit_test
  AsyncQueueControllerTest
  logging/AsyncQueueControllerTest.cpp
)

dasall_register_logging_unit_test(
  dasall_audit_link_adapter_unit_test
  AuditLinkAdapterTest
  logging/AuditLinkAdapterTest.cpp
)

dasall_register_logging_unit_test(
  dasall_logging_recovery_unit_test
  LoggingRecoveryTest
  logging/LoggingRecoveryTest.cpp
)

dasall_register_logging_unit_test(
  dasall_logging_config_merge_unit_test
  LoggingConfigMergeTest
  logging/LoggingConfigMergeTest.cpp
)

dasall_register_logging_unit_test(
  dasall_logging_metrics_bridge_unit_test
  LoggingMetricsBridgeTest
  logging/LoggingMetricsBridgeTest.cpp
)

dasall_register_logging_unit_test(
  dasall_logging_health_probe_unit_test
  LoggingHealthProbeTest
  logging/LoggingHealthProbeTest.cpp
)

dasall_register_logging_unit_test(
  dasall_log_query_service_unit_test
  LogQueryServiceTest
  logging/LogQueryServiceTest.cpp
)

dasall_register_audit_unit_test(
  dasall_audit_logger_interface_unit_test
  AuditInterfaceCompileTest
  AuditLoggerInterfaceTest.cpp
)

dasall_register_audit_unit_test(
  dasall_audit_service_fallback_unit_test
  AuditServiceFallbackTest
  AuditServiceFallbackTest.cpp
)

dasall_register_audit_unit_test(
  dasall_audit_export_filter_unit_test
  AuditExportFilterTest
  AuditExportFilterTest.cpp
)

target_include_directories(dasall_audit_export_filter_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

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

add_executable(dasall_policy_loader_config_read_unit_test
  PolicyLoaderConfigReadTest.cpp
)

target_include_directories(dasall_policy_loader_config_read_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_policy_loader_config_read_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_loader_config_read_unit_test)

add_test(NAME PolicyLoaderConfigReadTest COMMAND dasall_policy_loader_config_read_unit_test)
set_tests_properties(PolicyLoaderConfigReadTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_conflict_resolver_unit_test
  PolicyConflictResolverTest.cpp
)

target_include_directories(dasall_policy_conflict_resolver_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_policy_conflict_resolver_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_conflict_resolver_unit_test)

add_test(NAME PolicyConflictResolverTest COMMAND dasall_policy_conflict_resolver_unit_test)
set_tests_properties(PolicyConflictResolverTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_decision_projector_unit_test
  PolicyDecisionProjectorTest.cpp
)

target_include_directories(dasall_policy_decision_projector_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_policy_decision_projector_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_decision_projector_unit_test)

add_test(NAME PolicyDecisionProjectorTest COMMAND dasall_policy_decision_projector_unit_test)
set_tests_properties(PolicyDecisionProjectorTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_audit_bridge_unit_test
  PolicyAuditBridgeTest.cpp
)

target_include_directories(dasall_policy_audit_bridge_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_policy_audit_bridge_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_audit_bridge_unit_test)

add_test(NAME PolicyAuditBridgeTest COMMAND dasall_policy_audit_bridge_unit_test)
set_tests_properties(PolicyAuditBridgeTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_metrics_bridge_unit_test
  PolicyMetricsBridgeTest.cpp
)

target_include_directories(dasall_policy_metrics_bridge_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_policy_metrics_bridge_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_metrics_bridge_unit_test)

add_test(NAME PolicyMetricsBridgeTest COMMAND dasall_policy_metrics_bridge_unit_test)
set_tests_properties(PolicyMetricsBridgeTest PROPERTIES LABELS "unit")

add_executable(dasall_policy_health_probe_unit_test
  PolicyHealthProbeTest.cpp
)

target_include_directories(dasall_policy_health_probe_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_policy_health_probe_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_health_probe_unit_test)

add_test(NAME PolicyHealthProbeTest COMMAND dasall_policy_health_probe_unit_test)
set_tests_properties(PolicyHealthProbeTest PROPERTIES LABELS "unit")

add_executable(dasall_security_policy_manager_unit_test
  SecurityPolicyManagerTest.cpp
)

target_include_directories(dasall_security_policy_manager_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_security_policy_manager_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_security_policy_manager_unit_test)

add_test(NAME SecurityPolicyManagerTest COMMAND dasall_security_policy_manager_unit_test)
set_tests_properties(SecurityPolicyManagerTest PROPERTIES LABELS "unit")

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

add_executable(dasall_policy_schema_validator_unit_test
  PolicySchemaValidatorTest.cpp
)

target_include_directories(dasall_policy_schema_validator_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_policy_schema_validator_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_schema_validator_unit_test)

add_test(NAME PolicySchemaValidatorTest COMMAND dasall_policy_schema_validator_unit_test)
set_tests_properties(PolicySchemaValidatorTest PROPERTIES LABELS "unit")

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

add_executable(dasall_policy_snapshot_store_impl_unit_test
  PolicySnapshotStoreTest.cpp
)

target_include_directories(dasall_policy_snapshot_store_impl_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_policy_snapshot_store_impl_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_policy_snapshot_store_impl_unit_test)

add_test(NAME PolicySnapshotStoreTest COMMAND dasall_policy_snapshot_store_impl_unit_test)
set_tests_properties(PolicySnapshotStoreTest PROPERTIES LABELS "unit")

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

add_executable(dasall_diagnostics_service_interface_unit_test
  DiagnosticsServiceInterfaceTest.cpp
)

target_link_libraries(dasall_diagnostics_service_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

target_include_directories(dasall_diagnostics_service_interface_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

dasall_apply_common_options(dasall_diagnostics_service_interface_unit_test)

add_test(NAME DiagnosticsServiceInterfaceTest COMMAND dasall_diagnostics_service_interface_unit_test)
set_tests_properties(DiagnosticsServiceInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_diagnostics_command_registry_unit_test
  DiagnosticsCommandRegistryTest.cpp
)

target_include_directories(dasall_diagnostics_command_registry_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_diagnostics_command_registry_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_diagnostics_command_registry_unit_test)

add_test(NAME DiagnosticsCommandRegistryTest COMMAND dasall_diagnostics_command_registry_unit_test)
set_tests_properties(DiagnosticsCommandRegistryTest PROPERTIES LABELS "unit")

add_executable(dasall_diagnostics_command_policy_unit_test
  DiagnosticsCommandPolicyTest.cpp
)

target_include_directories(dasall_diagnostics_command_policy_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_diagnostics_command_policy_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_diagnostics_command_policy_unit_test)

add_test(NAME DiagnosticsCommandPolicyTest COMMAND dasall_diagnostics_command_policy_unit_test)
set_tests_properties(DiagnosticsCommandPolicyTest PROPERTIES LABELS "unit")

add_executable(dasall_diagnostics_snapshot_export_unit_test
  DiagnosticsSnapshotExportTest.cpp
)

target_include_directories(dasall_diagnostics_snapshot_export_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_diagnostics_snapshot_export_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_diagnostics_snapshot_export_unit_test)

add_test(NAME DiagnosticsSnapshotExportTest COMMAND dasall_diagnostics_snapshot_export_unit_test)
set_tests_properties(DiagnosticsSnapshotExportTest PROPERTIES LABELS "unit")

add_executable(dasall_diagnostics_redaction_unit_test
  DiagnosticsRedactionTest.cpp
)

target_include_directories(dasall_diagnostics_redaction_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_diagnostics_redaction_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_diagnostics_redaction_unit_test)

add_test(NAME DiagnosticsRedactionTest COMMAND dasall_diagnostics_redaction_unit_test)
set_tests_properties(DiagnosticsRedactionTest PROPERTIES LABELS "unit")

add_executable(dasall_diagnostics_redaction_failure_unit_test
  DiagnosticsRedactionFailureTest.cpp
)

target_include_directories(dasall_diagnostics_redaction_failure_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_diagnostics_redaction_failure_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_diagnostics_redaction_failure_unit_test)

add_test(NAME DiagnosticsRedactionFailureTest COMMAND dasall_diagnostics_redaction_failure_unit_test)
set_tests_properties(DiagnosticsRedactionFailureTest PROPERTIES LABELS "unit")

add_executable(dasall_diagnostics_snapshot_store_unit_test
  DiagnosticsSnapshotStoreTest.cpp
)

target_include_directories(dasall_diagnostics_snapshot_store_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_diagnostics_snapshot_store_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_diagnostics_snapshot_store_unit_test)

add_test(NAME DiagnosticsSnapshotStoreTest COMMAND dasall_diagnostics_snapshot_store_unit_test)
set_tests_properties(DiagnosticsSnapshotStoreTest PROPERTIES LABELS "unit")

add_executable(dasall_diagnostics_export_unit_test
  DiagnosticsExportTest.cpp
)

target_include_directories(dasall_diagnostics_export_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_diagnostics_export_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_diagnostics_export_unit_test)

add_test(NAME DiagnosticsExportTest COMMAND dasall_diagnostics_export_unit_test)
set_tests_properties(DiagnosticsExportTest PROPERTIES LABELS "unit")

add_executable(dasall_diagnostics_metrics_audit_bridge_unit_test
  DiagnosticsMetricsAuditBridgeTest.cpp
)

target_include_directories(dasall_diagnostics_metrics_audit_bridge_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_diagnostics_metrics_audit_bridge_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_diagnostics_metrics_audit_bridge_unit_test)

add_test(NAME DiagnosticsMetricsAuditBridgeTest COMMAND dasall_diagnostics_metrics_audit_bridge_unit_test)
set_tests_properties(DiagnosticsMetricsAuditBridgeTest PROPERTIES LABELS "unit")

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

add_executable(dasall_config_center_facade_unit_test
  ConfigCenterFacadeTest.cpp
)

target_link_libraries(dasall_config_center_facade_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_center_facade_unit_test)

add_test(NAME ConfigCenterFacadeTest COMMAND dasall_config_center_facade_unit_test)
set_tests_properties(ConfigCenterFacadeTest PROPERTIES LABELS "unit")

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

add_executable(dasall_config_loader_unit_test
  ConfigLoaderTest.cpp
)

target_link_libraries(dasall_config_loader_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_loader_unit_test)

add_test(NAME ConfigLoaderTest COMMAND dasall_config_loader_unit_test)
set_tests_properties(ConfigLoaderTest PROPERTIES LABELS "unit")

add_executable(dasall_config_merger_unit_test
  ConfigMergerTest.cpp
)

target_link_libraries(dasall_config_merger_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_merger_unit_test)

add_test(NAME ConfigMergerTest COMMAND dasall_config_merger_unit_test)
set_tests_properties(ConfigMergerTest PROPERTIES LABELS "unit")

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

add_executable(dasall_config_validator_unit_test
  ConfigValidatorTest.cpp
)

target_link_libraries(dasall_config_validator_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_validator_unit_test)

add_test(NAME ConfigValidatorTest COMMAND dasall_config_validator_unit_test)
set_tests_properties(ConfigValidatorTest PROPERTIES LABELS "unit")

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

add_executable(dasall_config_snapshot_store_unit_test
  ConfigSnapshotStoreTest.cpp
)

target_link_libraries(dasall_config_snapshot_store_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_snapshot_store_unit_test)

add_test(NAME ConfigSnapshotStoreTest COMMAND dasall_config_snapshot_store_unit_test)
set_tests_properties(ConfigSnapshotStoreTest PROPERTIES LABELS "unit")

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

add_executable(dasall_config_publisher_unit_test
  ConfigPublisherTest.cpp
)

target_link_libraries(dasall_config_publisher_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_config_publisher_unit_test)

add_test(NAME ConfigPublisherTest COMMAND dasall_config_publisher_unit_test)
set_tests_properties(ConfigPublisherTest PROPERTIES LABELS "unit")

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
set_tests_properties(HealthMonitorInterfaceTest PROPERTIES LABELS "unit;health")

add_executable(dasall_health_monitor_facade_unit_test
  health/HealthMonitorFacadeTest.cpp
)

target_include_directories(dasall_health_monitor_facade_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_health_monitor_facade_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_health_monitor_facade_unit_test)

add_test(NAME HealthMonitorFacadeTest COMMAND dasall_health_monitor_facade_unit_test)
set_tests_properties(HealthMonitorFacadeTest PROPERTIES LABELS "unit;health")

add_executable(dasall_probe_registry_unit_test
  health/ProbeRegistryTest.cpp
)

target_include_directories(dasall_probe_registry_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_probe_registry_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_probe_registry_unit_test)

add_test(NAME ProbeRegistryTest COMMAND dasall_probe_registry_unit_test)
set_tests_properties(ProbeRegistryTest PROPERTIES LABELS "unit;health")

add_executable(dasall_probe_executor_unit_test
  health/ProbeExecutorTest.cpp
)

target_include_directories(dasall_probe_executor_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_probe_executor_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_probe_executor_unit_test)

add_test(NAME ProbeExecutorTest COMMAND dasall_probe_executor_unit_test)
set_tests_properties(ProbeExecutorTest PROPERTIES LABELS "unit;health")

add_executable(dasall_health_evaluator_unit_test
  health/HealthEvaluatorTest.cpp
)

target_include_directories(dasall_health_evaluator_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_health_evaluator_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_health_evaluator_unit_test)

add_test(NAME HealthEvaluatorTest COMMAND dasall_health_evaluator_unit_test)
set_tests_properties(HealthEvaluatorTest PROPERTIES LABELS "unit;health")

add_executable(dasall_recovery_hint_emitter_unit_test
  health/RecoveryHintEmitterTest.cpp
)

target_include_directories(dasall_recovery_hint_emitter_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_recovery_hint_emitter_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_recovery_hint_emitter_unit_test)

add_test(NAME RecoveryHintEmitterTest COMMAND dasall_recovery_hint_emitter_unit_test)
set_tests_properties(RecoveryHintEmitterTest PROPERTIES LABELS "unit;health")

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

add_executable(dasall_heartbeat_sample_unit_test
  watchdog/HeartbeatSampleTest.cpp
)

target_link_libraries(dasall_heartbeat_sample_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_heartbeat_sample_unit_test)

add_test(NAME HeartbeatSampleTest COMMAND dasall_heartbeat_sample_unit_test)
set_tests_properties(HeartbeatSampleTest PROPERTIES LABELS "unit")

add_executable(dasall_timeout_decision_unit_test
  watchdog/TimeoutDecisionTest.cpp
)

target_link_libraries(dasall_timeout_decision_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_timeout_decision_unit_test)

add_test(NAME TimeoutDecisionTest COMMAND dasall_timeout_decision_unit_test)
set_tests_properties(TimeoutDecisionTest PROPERTIES LABELS "unit")

add_executable(dasall_watchdog_snapshot_unit_test
  watchdog/WatchdogSnapshotTest.cpp
)

target_link_libraries(dasall_watchdog_snapshot_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_watchdog_snapshot_unit_test)

add_test(NAME WatchdogSnapshotTest COMMAND dasall_watchdog_snapshot_unit_test)
set_tests_properties(WatchdogSnapshotTest PROPERTIES LABELS "unit")

add_executable(dasall_watchdog_recovery_hint_request_unit_test
  watchdog/RecoveryHintRequestTest.cpp
)

target_link_libraries(dasall_watchdog_recovery_hint_request_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_watchdog_recovery_hint_request_unit_test)

add_test(NAME RecoveryHintRequestTest COMMAND dasall_watchdog_recovery_hint_request_unit_test)
set_tests_properties(RecoveryHintRequestTest PROPERTIES LABELS "unit")

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
set_tests_properties(HealthPolicyInterfaceTest PROPERTIES LABELS "unit;health")

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
set_tests_properties(ProbeTypesUnitTest PROPERTIES LABELS "unit;health")

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
set_tests_properties(RecoveryHintUnitTest PROPERTIES LABELS "unit;health")

add_executable(dasall_health_errors_unit_test
  health/HealthErrorsTest.cpp
)

target_link_libraries(dasall_health_errors_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_health_errors_unit_test)

add_test(NAME HealthErrorsTest COMMAND dasall_health_errors_unit_test)
set_tests_properties(HealthErrorsTest PROPERTIES LABELS "unit;health")

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

add_executable(dasall_ota_precheck_service_unit_test
  ota/OTAPrecheckServiceTest.cpp
)

target_include_directories(dasall_ota_precheck_service_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_ota_precheck_service_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_ota_precheck_service_unit_test)

add_test(NAME OTAPrecheckServiceTest COMMAND dasall_ota_precheck_service_unit_test)
set_tests_properties(OTAPrecheckServiceTest PROPERTIES LABELS "unit;ota")

add_executable(dasall_ota_package_verifier_unit_test
  ota/PackageVerifierTest.cpp
)

target_include_directories(dasall_ota_package_verifier_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_ota_package_verifier_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_ota_package_verifier_unit_test)

add_test(NAME OTAPackageVerifierTest COMMAND dasall_ota_package_verifier_unit_test)
set_tests_properties(OTAPackageVerifierTest PROPERTIES LABELS "unit;ota")

add_executable(dasall_artifact_compatibility_evaluator_unit_test
  ota/ArtifactCompatibilityEvaluatorTest.cpp
)

target_include_directories(dasall_artifact_compatibility_evaluator_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_artifact_compatibility_evaluator_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_artifact_compatibility_evaluator_unit_test)

add_test(NAME ArtifactCompatibilityEvaluatorTest COMMAND dasall_artifact_compatibility_evaluator_unit_test)
set_tests_properties(ArtifactCompatibilityEvaluatorTest PROPERTIES LABELS "unit;ota")

add_executable(dasall_install_executor_unit_test
  ota/InstallExecutorTest.cpp
)

target_include_directories(dasall_install_executor_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_install_executor_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_install_executor_unit_test)

add_test(NAME InstallExecutorTest COMMAND dasall_install_executor_unit_test)
set_tests_properties(InstallExecutorTest PROPERTIES LABELS "unit;ota")

add_executable(dasall_slot_switch_coordinator_unit_test
  ota/SlotSwitchCoordinatorTest.cpp
)

target_include_directories(dasall_slot_switch_coordinator_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_slot_switch_coordinator_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_slot_switch_coordinator_unit_test)

add_test(NAME SlotSwitchCoordinatorTest COMMAND dasall_slot_switch_coordinator_unit_test)
set_tests_properties(SlotSwitchCoordinatorTest PROPERTIES LABELS "unit;ota")

add_executable(dasall_rollback_controller_unit_test
  ota/RollbackControllerTest.cpp
)

target_include_directories(dasall_rollback_controller_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_rollback_controller_unit_test
  PRIVATE
    dasall_test_support
    dasall_infra
)

dasall_apply_common_options(dasall_rollback_controller_unit_test)

add_test(NAME RollbackControllerTest COMMAND dasall_rollback_controller_unit_test)
set_tests_properties(RollbackControllerTest PROPERTIES LABELS "unit;ota")

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