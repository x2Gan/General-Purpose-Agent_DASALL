add_executable(dasall-cli
  src/main.cpp
  src/CliIpcClient.cpp
  src/CliRequestBuilder.cpp
  src/CliCommandParser.cpp
  src/CliExitDecision.cpp
  src/CliOutputFormatter.cpp
  src/config/ConfigCommandTypes.cpp
  src/config/CliConfigWorkflowCoordinator.cpp
  src/config/DaemonConfigFileStore.cpp
  src/config/InstallStateProbe.cpp
  src/config/ConfigCapabilityResolver.cpp
  src/config/ConfigPreflightChecker.cpp
  src/config/ServiceManagerAdapter.cpp
  src/config/ConfigDiffPlanner.cpp
  src/config/PrivilegeProbe.cpp
  src/config/ConfigPlanFormatter.cpp
  src/config/ConfigSummaryFormatter.cpp
  src/config/ToolSkillPage.cpp
  src/config/InteractivePromptEngine.cpp
  src/config/LlmSecretPage.cpp
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
    ${CMAKE_SOURCE_DIR}/infra/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(dasall-cli PROPERTIES
  OUTPUT_NAME dasall
)

install(TARGETS dasall-cli
  RUNTIME DESTINATION ${DASALL_INSTALL_BINDIR}
)

dasall_apply_common_options(dasall-cli)
