if(NOT DEFINED CTEST_COMMAND OR CTEST_COMMAND STREQUAL "")
  message(FATAL_ERROR "CTEST_COMMAND is required")
endif()

if(NOT DEFINED BUILD_DIR OR BUILD_DIR STREQUAL "")
  message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED EXPECTED_TESTS_CSV OR EXPECTED_TESTS_CSV STREQUAL "")
  message(FATAL_ERROR "EXPECTED_TESTS_CSV is required")
endif()

if(NOT DEFINED RUN_ACCEPTANCE)
  set(RUN_ACCEPTANCE OFF)
endif()

if(NOT DEFINED DISCOVERABILITY_LABELS_CSV)
  set(DISCOVERABILITY_LABELS_CSV "")
endif()

if(NOT DEFINED EXPECTED_MISSING_GATES_CSV)
  set(EXPECTED_MISSING_GATES_CSV "")
endif()

string(REPLACE "," ";" expected_tests "${EXPECTED_TESTS_CSV}")
string(JOIN "|" test_regex_body ${expected_tests})
set(TEST_REGEX "^(${test_regex_body})$")

macro(verify_expected_tests discoverability_output discoverability_context)
  foreach(test_name IN LISTS expected_tests)
    string(FIND "${discoverability_output}" "${test_name}" found_index)
    if(found_index EQUAL -1)
      message(FATAL_ERROR "System gate discoverability missing test: ${test_name}\n${discoverability_context}\n${discoverability_output}")
    endif()
  endforeach()
endmacro()

execute_process(
  COMMAND ${CTEST_COMMAND} -N -R ${TEST_REGEX}
  WORKING_DIRECTORY ${BUILD_DIR}
  RESULT_VARIABLE ctest_result
  OUTPUT_VARIABLE ctest_output
  ERROR_VARIABLE ctest_error
)

if(NOT ctest_result EQUAL 0)
  message(FATAL_ERROR "ctest -N failed: ${ctest_error}\n${ctest_output}")
endif()

verify_expected_tests("${ctest_output}" "ctest -N -R ${TEST_REGEX}")

message(STATUS "Verified system gate discoverability for ${EXPECTED_TESTS_CSV}")

if(NOT EXPECTED_MISSING_GATES_CSV STREQUAL "")
  string(REPLACE "," ";" expected_missing_gates "${EXPECTED_MISSING_GATES_CSV}")

  foreach(missing_gate IN LISTS expected_missing_gates)
    if(NOT missing_gate MATCHES "^BC-[0-9][0-9]:[A-Za-z0-9_.-]+$")
      message(FATAL_ERROR "Invalid explicit missing gate marker: ${missing_gate}")
    endif()
  endforeach()

  message(STATUS "Verified explicit missing gate markers for ${EXPECTED_MISSING_GATES_CSV}")
endif()

if(NOT DISCOVERABILITY_LABELS_CSV STREQUAL "")
  string(REPLACE "," ";" discoverability_labels "${DISCOVERABILITY_LABELS_CSV}")

  foreach(label_name IN LISTS discoverability_labels)
    execute_process(
      COMMAND ${CTEST_COMMAND} -N -L ${label_name} -R ${TEST_REGEX}
      WORKING_DIRECTORY ${BUILD_DIR}
      RESULT_VARIABLE label_result
      OUTPUT_VARIABLE label_output
      ERROR_VARIABLE label_error
    )

    if(NOT label_result EQUAL 0)
      message(FATAL_ERROR "ctest -N -L ${label_name} failed: ${label_error}\n${label_output}")
    endif()

    verify_expected_tests("${label_output}" "ctest -N -L ${label_name} -R ${TEST_REGEX}")
  endforeach()

  message(STATUS "Verified labeled system gate discoverability for ${DISCOVERABILITY_LABELS_CSV}")
endif()

if(RUN_ACCEPTANCE)
  execute_process(
    COMMAND ${CTEST_COMMAND} --output-on-failure -R ${TEST_REGEX}
    WORKING_DIRECTORY ${BUILD_DIR}
    RESULT_VARIABLE acceptance_result
    OUTPUT_VARIABLE acceptance_output
    ERROR_VARIABLE acceptance_error
  )

  if(NOT acceptance_result EQUAL 0)
    message(FATAL_ERROR "System gate acceptance failed: ${acceptance_error}\n${acceptance_output}")
  endif()

  message(STATUS "System gate acceptance passed for ${EXPECTED_TESTS_CSV}")
endif()