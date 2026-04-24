add_executable(dasall_cli
  src/main.cpp
  src/CliIpcClient.cpp
  src/CliCommandParser.cpp
  src/CliOutputFormatter.cpp
)

target_compile_features(dasall_cli PRIVATE cxx_std_20)

target_link_libraries(dasall_cli
  PRIVATE
    dasall_access
    dasall_contracts
    dasall_infra
    dasall_platform
)

target_include_directories(dasall_cli
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

dasall_apply_common_options(dasall_cli)
