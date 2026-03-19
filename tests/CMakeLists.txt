add_subdirectory(mocks)
add_subdirectory(unit)
add_subdirectory(contract)

add_custom_target(dasall_unit_tests
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L unit
  DEPENDS dasall_runtime_smoke_test
)

add_custom_target(dasall_contract_tests
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L contract
  DEPENDS ${DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS}
)

add_custom_target(dasall_wp04_contract_gate
  COMMAND ${CMAKE_COMMAND} -E env
    BUILD_DIR=${CMAKE_BINARY_DIR}
    WP04_GATE_SKIP_CONFIGURE=1
    WP04_GATE_SKIP_BUILD=1
    bash ${CMAKE_SOURCE_DIR}/scripts/ci/wp04_contract_gate.sh
  DEPENDS dasall_contract_tests
  USES_TERMINAL
)

if(NOT DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "dasall_contract_tests requires at least one registered contract test target")
endif()
