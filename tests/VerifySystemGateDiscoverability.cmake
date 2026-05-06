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

string(REPLACE "," ";" expected_tests "${EXPECTED_TESTS_CSV}")
string(JOIN "|" test_regex_body ${expected_tests})
set(TEST_REGEX "^(${test_regex_body})$")

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

string(REPLACE "," ";" expected_tests "${EXPECTED_TESTS_CSV}")

foreach(test_name IN LISTS expected_tests)
  string(FIND "${ctest_output}" "${test_name}" found_index)
  if(found_index EQUAL -1)
    message(FATAL_ERROR "System gate discoverability missing test: ${test_name}\n${ctest_output}")
  endif()
endforeach()

message(STATUS "Verified system gate discoverability for ${EXPECTED_TESTS_CSV}")

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