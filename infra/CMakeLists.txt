set(DASALL_INFRA_CORE_SOURCES
  src/placeholder.cpp
  src/InfraErrorCode.cpp
  src/InfraServiceFacade.cpp
  src/audit/AuditService.cpp
)

set(DASALL_INFRA_TRACING_SOURCES
  src/tracing/TracingModuleAnchor.cpp
)

set(DASALL_INFRA_PUBLIC_HEADERS
  include/AuditEvent.h
  include/HealthSnapshot.h
  include/health/HealthStateTypes.h
  include/health/IHealthMonitor.h
  include/health/IHealthPolicy.h
  include/health/ProbeTypes.h
  include/health/RecoveryHint.h
  include/IHealthMonitor.h
  include/IDiagnosticsService.h
  include/IInfrastructureService.h
  include/ILogger.h
  include/health/IHealthProbe.h
  include/InfraContext.h
  include/InfraErrorCode.h
  include/metrics/IMetricExporter.h
  include/metrics/IMeter.h
  include/metrics/IMetricsProvider.h
  include/metrics/MetricTypes.h
  include/audit/AuditTypes.h
  include/audit/AuditExporterTypes.h
  include/audit/AuditErrors.h
  include/LogEvent.h
  include/audit/AuditService.h
  include/audit/IAuditLogger.h
  include/config/ConfigTypes.h
  include/config/ConfigErrors.h
  include/config/IConfigCenter.h
  include/config/IConfigLoader.h
  include/config/IConfigValidator.h
  include/config/IConfigSnapshotStore.h
  include/config/IConfigPublisher.h
  include/diagnostics/DiagnosticsErrors.h
  include/diagnostics/DiagnosticsTypes.h
  include/policy/ISecurityPolicyManager.h
  include/policy/PolicyBundle.h
  include/policy/PolicyDecisionRef.h
  include/policy/PolicyPatch.h
  include/policy/PolicySnapshot.h
)

add_library(dasall_infra STATIC)

target_sources(dasall_infra
  PRIVATE
    ${DASALL_INFRA_CORE_SOURCES}
    ${DASALL_INFRA_TRACING_SOURCES}
)

set_target_properties(dasall_infra PROPERTIES
  PUBLIC_HEADER "${DASALL_INFRA_PUBLIC_HEADERS}"
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
