add_library(dasall_services STATIC)

target_sources(dasall_services
  PRIVATE
    src/placeholder.cpp
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
)

target_link_libraries(dasall_services
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_services)
