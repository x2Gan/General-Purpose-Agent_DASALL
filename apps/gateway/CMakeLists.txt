# ACC-TODO-004:
# - gateway v1 transport is intentionally frozen to HTTP/1.1 unary + accepted async receipt only.
# - health probes use a separate listener under the same HTTP transport family.
# - WS/MQTT dependencies remain absent until ACC-TODO-005 / ACC-TODO-026 unlock them.
# - planned transport seam: cpp-httplib (header-only), wired in implementation phase rather than this design-only round.
add_executable(dasall_gateway
  src/main.cpp
  src/HttpProtocolAdapter.cpp
    src/HealthProbeHandler.cpp
    src/TaskQueryHandler.cpp
)

target_compile_features(dasall_gateway PRIVATE cxx_std_20)

target_link_libraries(dasall_gateway
  PRIVATE
    dasall_access
    dasall_runtime
    dasall_contracts
    dasall_infra
    cpp-httplib
)

target_include_directories(dasall_gateway
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # access/src 内部头文件（AccessGateway.h 等 concrete class）
    ${CMAKE_SOURCE_DIR}/access/src
)

dasall_apply_common_options(dasall_gateway)
