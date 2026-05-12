add_library(dasall_access STATIC)

set(DASALL_ACCESS_PUBLIC_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(DASALL_ACCESS_PUBLIC_HEADERS
  include/AccessGatewayFactory.h
  include/AccessErrors.h
  include/AccessTypes.h
  include/daemon/DaemonFrameCodec.h
  include/daemon/DaemonHealthTypes.h
  include/daemon/DaemonProtocolTypes.h
  include/daemon/DaemonProtocolAdapter.h
  include/daemon/DaemonResponseBuilderWithReceipt.h
  include/daemon/DaemonTaskQueryHandler.h
  include/IAccessGateway.h
  include/IAccessRuntimeBridge.h
  include/IAdmissionController.h
  include/IProtocolAdapter.h
)

if(NOT EXISTS ${DASALL_ACCESS_PUBLIC_INCLUDE_DIR})
  message(FATAL_ERROR "dasall_access requires ${DASALL_ACCESS_PUBLIC_INCLUDE_DIR} to exist")
endif()

foreach(public_header IN LISTS DASALL_ACCESS_PUBLIC_HEADERS)
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${public_header})
    message(FATAL_ERROR "dasall_access requires ${CMAKE_CURRENT_SOURCE_DIR}/${public_header} to exist")
  endif()
endforeach()

target_sources(dasall_access
  PRIVATE
    src/placeholder.cpp
    src/AccessConfigAdapter.cpp
    src/AccessGateway.cpp
    src/AccessGatewayFactory.cpp
    src/AccessObservabilityBridge.cpp
    src/AsyncTaskRegistry.cpp
    src/AdmissionController.cpp
    src/AccessPolicyGate.cpp
    src/AuthenticatorChain.cpp
    src/RequestValidator.cpp
    src/RequestNormalizer.cpp
    src/ResultReplayCache.cpp
    src/RuntimeBridge.cpp
    src/ResultPublisher.cpp
    src/daemon/DaemonFrameCodec.cpp
    src/daemon/DaemonDiagnosticsHandler.cpp
    src/daemon/DaemonHealthService.cpp
    src/daemon/DaemonProtocolAdapter.cpp
    src/daemon/DaemonResponseBuilderWithReceipt.cpp
    src/daemon/DaemonTaskQueryHandler.cpp
    src/ProtocolAdapterRegistry.cpp
    src/ProtocolErrorMapper.cpp
    src/SubjectResolver.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS
      ${DASALL_ACCESS_PUBLIC_INCLUDE_DIR}
    FILES
      ${DASALL_ACCESS_PUBLIC_HEADERS}
)

target_compile_features(dasall_access PUBLIC cxx_std_20)

target_include_directories(dasall_access
  PUBLIC
    $<BUILD_INTERFACE:${DASALL_ACCESS_PUBLIC_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(dasall_access
  PUBLIC
    dasall_contracts
    dasall_platform
  PRIVATE
    dasall_infra
    dasall_knowledge
)

dasall_apply_common_options(dasall_access)
