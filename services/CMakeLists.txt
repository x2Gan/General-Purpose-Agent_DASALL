add_library(dasall_services STATIC)

target_sources(dasall_services
  PRIVATE
    src/ServiceFacade.cpp
    src/ServiceContextBuilder.cpp
    src/adapters/AdapterRouter.cpp
    src/adapters/AdapterBridge.cpp
    src/adapters/LocalPlatformAdapter.cpp
    src/adapters/LocalServiceAdapter.cpp
    src/adapters/RemoteServiceAdapter.cpp
    src/execution/CompensationCatalog.cpp
    src/execution/ExecutionCommandLane.cpp
    src/execution/ExecutionDiagnoseService.cpp
    src/execution/ExecutionQueryLane.cpp
    src/execution/ExecutionSubscriptionHub.cpp
    src/mapping/ResultMapper.cpp
    src/execution/placeholder.cpp
    src/data/DataProjectionCache.cpp
    src/data/DataQueryLane.cpp
    src/data/placeholder.cpp
    src/system/placeholder.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
      include/ServiceTypes.h
      include/IExecutionService.h
      include/IDataService.h
)

target_compile_features(dasall_services PUBLIC cxx_std_20)

target_include_directories(dasall_services
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dasall_services
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_services)
