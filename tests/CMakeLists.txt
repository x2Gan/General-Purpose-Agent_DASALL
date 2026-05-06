add_subdirectory(mocks)
add_subdirectory(unit)
add_subdirectory(contract)
add_subdirectory(integration)

add_custom_target(dasall_unit_tests
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L unit
  DEPENDS ${DASALL_UNIT_TEST_EXECUTABLE_TARGETS}
)

add_custom_target(dasall_contract_tests
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L contract
  DEPENDS ${DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS}
)

add_custom_target(dasall_integration_tests
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L integration
  DEPENDS ${DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS}
)

add_custom_target(dasall_gate_int_03
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L gate-int-03
  DEPENDS
    dasall_contract_main_flow_e2e_test
    dasall_runtime_unary_integration_test
    dasall_cognition_runtime_integration_test
)

add_custom_target(dasall_gate_int_04
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L gate-int-04
  DEPENDS
    dasall_runtime_evidence_projection_integration_test
    dasall_knowledge_evidence_preservation_integration_test
)

add_custom_target(dasall_gate_int_05
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L gate-int-05
  DEPENDS
    dasall_infra_diagnostics_smoke_integration_test
    dasall_infra_diagnostics_integration_test
)

add_custom_target(dasall_gate_int_06
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L gate-int-06
  DEPENDS
    dasall_runtime_required_optional_ports_integration_test
    dasall_runtime_profile_compatibility_integration_test
    dasall_llm_smoke_integration_test
)

add_custom_target(dasall_gate_int_07
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L gate-int-07
  DEPENDS
    dasall_contract_service_result_semantics_test
    dasall_builtin_executor_lane_result_code_unit_test
)

add_custom_target(dasall_access_tests
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --output-on-failure -L access
  DEPENDS
    dasall_access_unit_tests
    dasall_access_integration_tests
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

# WP05 gate target reuses already-built contract tests and executes the
# dedicated M5/V1-ready gate script as a single CMake entrypoint.
add_custom_target(dasall_wp05_contract_gate
  COMMAND ${CMAKE_COMMAND} -E env
    BUILD_DIR=${CMAKE_BINARY_DIR}
    WP05_GATE_SKIP_CONFIGURE=1
    WP05_GATE_SKIP_BUILD=1
    bash ${CMAKE_SOURCE_DIR}/scripts/ci/wp05_contract_gate.sh
  DEPENDS dasall_contract_tests
  USES_TERMINAL
)

if(NOT DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "dasall_contract_tests requires at least one registered contract test target")
endif()

if(NOT DASALL_UNIT_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "dasall_unit_tests requires at least one registered unit test target")
endif()

if(NOT DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "dasall_integration_tests requires at least one registered integration test target")
endif()
