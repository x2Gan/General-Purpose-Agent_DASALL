add_executable(dasall_daemon
  src/main.cpp
  src/DaemonBootstrap.cpp
  src/DaemonConfig.h
)

target_compile_features(dasall_daemon PRIVATE cxx_std_20)

target_link_libraries(dasall_daemon
  PRIVATE
    dasall_access
    dasall_runtime
    dasall_contracts
    dasall_infra
    dasall_platform
)

target_include_directories(dasall_daemon
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # access/src 内部头文件（AccessGateway.h 等 concrete class）
    ${CMAKE_SOURCE_DIR}/access/src
)

dasall_apply_common_options(dasall_daemon)


