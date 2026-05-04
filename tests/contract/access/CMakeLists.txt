# Reserve the stable CLI contract entrypoints before real assertions land.
# The placeholders are intentionally fail-closed and do not carry the
# contract label yet, so CLI-TODO-012 cannot pass until these names are
# replaced with real contract tests.
add_test(NAME CliJsonOutputContractTest COMMAND ${CMAKE_COMMAND} -E false)
set_tests_properties(CliJsonOutputContractTest PROPERTIES LABELS "topology;access;cli;reserved")

add_test(NAME CliExitCodeContractTest COMMAND ${CMAKE_COMMAND} -E false)
set_tests_properties(CliExitCodeContractTest PROPERTIES LABELS "topology;access;cli;reserved")
