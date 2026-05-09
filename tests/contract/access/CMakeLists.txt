add_executable(dasall-cli_json_output_contract_test
	CliJsonOutputContractTest.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/CliOutputFormatter.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/CliExitDecision.cpp
)

target_include_directories(dasall-cli_json_output_contract_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/apps/cli/src
)

target_link_libraries(dasall-cli_json_output_contract_test
	PRIVATE
		dasall_access
		dasall_contracts
		dasall_infra
		dasall_platform
		dasall_test_support
)

dasall_apply_common_options(dasall-cli_json_output_contract_test)

add_test(NAME CliJsonOutputContractTest COMMAND dasall-cli_json_output_contract_test)
set_tests_properties(CliJsonOutputContractTest PROPERTIES LABELS "contract;smoke;access;cli;release-preflight-gate")

list(APPEND DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS dasall-cli_json_output_contract_test)

add_executable(dasall-cli_exit_code_contract_test
	CliExitCodeContractTest.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/CliExitDecision.cpp
)

target_include_directories(dasall-cli_exit_code_contract_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/apps/cli/src
)

target_link_libraries(dasall-cli_exit_code_contract_test
	PRIVATE
		dasall_access
		dasall_contracts
		dasall_infra
		dasall_platform
		dasall_test_support
)

dasall_apply_common_options(dasall-cli_exit_code_contract_test)

add_test(NAME CliExitCodeContractTest COMMAND dasall-cli_exit_code_contract_test)
set_tests_properties(CliExitCodeContractTest PROPERTIES LABELS "contract;smoke;access;cli;release-preflight-gate")

list(APPEND DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS dasall-cli_exit_code_contract_test)

add_executable(dasall-config_output_contract_test
	ConfigOutputContractTest.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/CliConfigWorkflowCoordinator.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigDiffPlanner.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/DaemonConfigFileStore.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/InstallStateProbe.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPreflightChecker.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/ServiceManagerAdapter.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/InteractivePromptEngine.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/LlmSecretPage.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigPlanFormatter.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigSummaryFormatter.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/ToolSkillPage.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCapabilityResolver.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/ConfigCommandTypes.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/config/PrivilegeProbe.cpp
)

target_include_directories(dasall-config_output_contract_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/apps/cli/src
		${CMAKE_SOURCE_DIR}/infra/src
		${CMAKE_SOURCE_DIR}/access/include
)

target_link_libraries(dasall-config_output_contract_test
	PRIVATE
		dasall_access
		dasall_contracts
		dasall_infra
		dasall_platform
		dasall_test_support
)

dasall_apply_common_options(dasall-config_output_contract_test)

add_test(NAME ConfigOutputContractTest COMMAND dasall-config_output_contract_test)
set_tests_properties(ConfigOutputContractTest PROPERTIES LABELS "contract;smoke;access;cli;config")

list(APPEND DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS dasall-config_output_contract_test)
set(DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS
	"${DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS}"
	PARENT_SCOPE
)
