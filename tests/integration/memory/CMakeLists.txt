set(DASALL_MEMORY_INTEGRATION_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_memory_integration_test target_name test_name source_file labels)
  add_executable(${target_name}
    ${source_file}
  )

  target_link_libraries(${target_name}
    PRIVATE
      dasall_test_support
      dasall_memory
  )

  target_compile_definitions(${target_name}
    PRIVATE
      DASALL_MEMORY_INTEGRATION_TEST_DIR="${CMAKE_SOURCE_DIR}/tests/integration/memory"
      DASALL_MEMORY_UNIT_TEST_DIR="${CMAKE_SOURCE_DIR}/tests/unit/memory"
      DASALL_MEMORY_INTEGRATION_TOP_LEVEL_CMAKE="${CMAKE_SOURCE_DIR}/tests/integration/CMakeLists.txt"
  )

  dasall_apply_common_options(${target_name})

  add_test(NAME ${test_name} COMMAND ${target_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "integration;memory")

  list(APPEND DASALL_MEMORY_INTEGRATION_TEST_EXECUTABLE_TARGETS ${target_name})
endmacro()

dasall_add_memory_integration_test(
  dasall_memory_integration_topology_smoke_integration_test
  MemoryIntegrationTopologySmokeTest
  MemoryIntegrationTopologySmokeTest.cpp
  "integration;memory"
)

if(NOT DASALL_MEMORY_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "memory integration registration requires at least one test target")
endif()

set(DASALL_MEMORY_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_MEMORY_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)