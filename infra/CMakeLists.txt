set(DASALL_INFRA_CORE_SOURCES
  src/placeholder.cpp
  src/InfraErrorCode.cpp
  src/InfraServiceFacade.cpp
)

set(DASALL_INFRA_TRACING_SOURCES
  src/tracing/TracingModuleAnchor.cpp
)

set(DASALL_INFRA_PUBLIC_HEADERS
  include/AuditEvent.h
  include/HealthSnapshot.h
  include/IHealthMonitor.h
  include/IInfrastructureService.h
  include/ILogger.h
  include/InfraContext.h
  include/InfraErrorCode.h
  include/LogEvent.h
  include/audit/IAuditLogger.h
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
