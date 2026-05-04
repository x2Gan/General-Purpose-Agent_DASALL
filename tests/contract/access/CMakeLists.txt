# Reserve the stable CLI contract entrypoints before real assertions land.
# The placeholders are intentionally fail-closed and do not carry the
# contract label yet, so CLI-TODO-012 cannot pass until these names are
# replaced with real contract tests.
add_test(NAME CliJsonOutputContractTest COMMAND ${CMAKE_COMMAND} -E false)
set_tests_properties(CliJsonOutputContractTest PROPERTIES LABELS "topology;access;cli;reserved")

add_executable(dasall_cli_exit_code_contract_test
	CliExitCodeContractTest.cpp
	${CMAKE_SOURCE_DIR}/apps/cli/src/CliExitDecision.cpp
)

target_include_directories(dasall_cli_exit_code_contract_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/apps/cli/src
)

target_link_libraries(dasall_cli_exit_code_contract_test
	PRIVATE
		dasall_access
		dasall_contracts
		dasall_infra
		dasall_platform
		dasall_test_support
)

dasall_apply_common_options(dasall_cli_exit_code_contract_test)

add_test(NAME CliExitCodeContractTest COMMAND dasall_cli_exit_code_contract_test)
set_tests_properties(CliExitCodeContractTest PROPERTIES LABELS "contract;smoke;access;cli")

list(APPEND DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS dasall_cli_exit_code_contract_test)
set(DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS
	"${DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS}"
	PARENT_SCOPE
)
