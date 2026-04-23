add_library(dasall_access STATIC)

set(DASALL_ACCESS_PUBLIC_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(DASALL_ACCESS_PUBLIC_HEADERS
  include/AccessErrors.h
  include/AccessTypes.h
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
    src/ProtocolErrorMapper.cpp
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
  PRIVATE
    dasall_infra
)

dasall_apply_common_options(dasall_access)
