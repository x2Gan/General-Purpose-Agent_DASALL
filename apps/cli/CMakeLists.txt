add_executable(dasall-cli
  src/main.cpp
  src/CliIpcClient.cpp
  src/CliRequestBuilder.cpp
  src/CliCommandParser.cpp
  src/CliExitDecision.cpp
  src/CliOutputFormatter.cpp
)

target_compile_features(dasall-cli PRIVATE cxx_std_20)

target_link_libraries(dasall-cli
  PRIVATE
    dasall_access
    dasall_contracts
    dasall_infra
    dasall_platform
)

target_include_directories(dasall-cli
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

dasall_apply_common_options(dasall-cli)
