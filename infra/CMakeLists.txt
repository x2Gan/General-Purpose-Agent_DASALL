set(DASALL_INFRA_CORE_SOURCES
  src/InfraErrorCode.cpp
  src/InfraServiceFacade.cpp
  src/config/ConfigCenterFacade.cpp
  src/config/ConfigPublisher.cpp
  src/config/ConfigLoader.cpp
  src/config/ConfigMerger.cpp
  src/config/ConfigValidator.cpp
  src/config/ConfigSnapshotStore.cpp
  src/plugin/PluginManager.cpp
)

set(DASALL_INFRA_AUDIT_SOURCES
  src/audit/AuditFallbackPipeline.cpp
  src/audit/AuditPipeline.cpp
  src/audit/AuditValidator.cpp
  src/audit/AuditService.cpp
)

set(DASALL_INFRA_TRACING_SOURCES
  src/tracing/TracingModuleAnchor.cpp
)

set(DASALL_INFRA_LOGGING_SOURCES
  src/logging/AsyncQueueController.cpp
  src/logging/AuditLinkAdapter.cpp
  src/logging/LoggingConfigAdapter.cpp
  src/logging/LoggingFacade.cpp
  src/logging/LoggingHealthProbe.cpp
  src/logging/LogQueryService.cpp
  src/logging/LoggingMetricsBridge.cpp
  src/logging/LoggingRecovery.cpp
  src/logging/SinkDispatcher.cpp
)

set(DASALL_INFRA_PUBLIC_HEADERS
  include/health/HealthStateTypes.h
  include/health/IHealthMonitor.h
  include/health/IHealthPolicy.h
  include/health/ProbeTypes.h
  include/health/RecoveryHint.h
  include/watchdog/IHeartbeatSource.h
  include/watchdog/ITimeoutPolicy.h
  include/watchdog/IWatchdogService.h
  include/watchdog/HeartbeatSample.h
  include/watchdog/RecoveryHintRequest.h
  include/watchdog/TimeoutDecision.h
  include/watchdog/WatchedEntityDescriptor.h
  include/watchdog/WatchdogSnapshot.h
  include/IInfrastructureService.h
  include/health/IHealthProbe.h
  include/diagnostics/IDiagnosticsService.h
  include/logging/IAuditLinkAdapter.h
  include/logging/ILogConfigurator.h
  include/logging/ILogger.h
  include/logging/LoggingErrors.h
  include/logging/LogTypes.h
  include/ota/IOTAManager.h
  include/secret/ISecretBackend.h
  include/secret/ISecretManager.h
  include/secret/ISecretHealthSource.h
  include/secret/SecureBuffer.h
  include/secret/SecretErrors.h
  include/secret/SecretTypes.h
  include/InfraContext.h
  include/InfraErrorCode.h
  include/tracing/ITracerProvider.h
  include/tracing/ITracer.h
  include/tracing/ISpan.h
  include/tracing/ITraceContextPropagator.h
  include/tracing/TraceTypes.h
  include/tracing/TraceErrors.h
  include/metrics/IMetricConfigPolicy.h
  include/metrics/IMetricExporter.h
  include/metrics/IMeter.h
  include/metrics/MetricsErrors.h
  include/metrics/IMetricsHealthProbe.h
  include/metrics/IMetricsProvider.h
  include/metrics/MetricsSnapshots.h
  include/metrics/MetricTypes.h
  include/LogEvent.h
  include/config/ConfigTypes.h
  include/config/ConfigErrors.h
  include/config/IConfigCenter.h
  include/config/ConfigCenterFacade.h
  include/config/ConfigPublisher.h
  include/config/IConfigLoader.h
  include/config/ConfigLoader.h
  include/config/ConfigMerger.h
  include/config/IConfigValidator.h
  include/config/ConfigValidator.h
  include/config/IConfigSnapshotStore.h
  include/config/ConfigSnapshotStore.h
  include/config/IConfigPublisher.h
  include/diagnostics/DiagnosticsErrors.h
  include/diagnostics/DiagnosticsTypes.h
  include/ota/IBootControlAdapter.h
  include/ota/IInstallExecutor.h
  include/ota/IOTAPackageVerifier.h
  include/ota/OTATypes.h
  include/policy/IPolicyLoader.h
  include/policy/IPolicySchemaValidator.h
  include/policy/IPolicySnapshotStore.h
  include/policy/ISecurityPolicyManager.h
  include/policy/PolicyErrors.h
  include/policy/PolicyTypes.h
  include/policy/PolicyBundle.h
  include/policy/PolicyDecisionRef.h
  include/policy/PolicyPatch.h
  include/policy/PolicySnapshot.h
  include/plugin/PluginCatalog.h
  include/plugin/PluginDescriptor.h
  include/plugin/PluginErrorCode.h
  include/plugin/IPluginManager.h
  include/plugin/IPluginPolicyGate.h
)

set(DASALL_INFRA_AUDIT_PUBLIC_HEADERS
  include/audit/AuditTypes.h
  include/audit/AuditExporterTypes.h
  include/audit/AuditErrors.h
  include/audit/AuditService.h
  include/audit/IAuditLogger.h
)

add_library(dasall_infra STATIC)

target_sources(dasall_infra
  PRIVATE
    ${DASALL_INFRA_CORE_SOURCES}
    ${DASALL_INFRA_AUDIT_SOURCES}
    ${DASALL_INFRA_LOGGING_SOURCES}
    ${DASALL_INFRA_TRACING_SOURCES}
)

set_target_properties(dasall_infra PROPERTIES
  PUBLIC_HEADER "${DASALL_INFRA_PUBLIC_HEADERS};${DASALL_INFRA_AUDIT_PUBLIC_HEADERS}"
)

target_compile_features(dasall_infra PUBLIC cxx_std_20)

target_include_directories(dasall_infra
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dasall_infra
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_infra)
