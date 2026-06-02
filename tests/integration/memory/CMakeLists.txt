set(DASALL_MEMORY_INTEGRATION_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_memory_integration_test target_name test_name source_file labels)
  add_executable(${target_name}
    ${CMAKE_CURRENT_SOURCE_DIR}/${source_file}
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

dasall_add_memory_integration_test(
  dasall_memory_context_assemble_integration_test
  MemoryContextAssembleIntegrationTest
  MemoryContextAssembleIntegrationTest.cpp
  "integration;memory"
)

dasall_add_memory_integration_test(
  dasall_memory_context_integration_test
  MemoryContextIntegrationTest
  MemoryContextIntegrationTest.cpp
  "integration;memory"
)

dasall_add_memory_integration_test(
  dasall_memory_failure_injection_integration_test
  MemoryFailureInjectionTest
  MemoryFailureInjectionTest.cpp
  "integration;memory"
)

dasall_add_memory_integration_test(
  dasall_memory_profile_compatibility_integration_test
  MemoryProfileCompatibilityTest
  MemoryProfileCompatibilityTest.cpp
  "integration;memory"
)

dasall_add_memory_integration_test(
  dasall_memory_writeback_integration_test
  MemoryWritebackIntegrationTest
  MemoryWritebackIntegrationTest.cpp
  "integration;memory"
)

dasall_add_memory_integration_test(
  dasall_memory_maintenance_integration_test
  MemoryMaintenanceIntegrationTest
  MemoryMaintenanceIntegrationTest.cpp
  "integration;memory"
)

dasall_add_memory_integration_test(
  dasall_memory_observability_bridge_integration_test
  MemoryObservabilityBridgeTest
  MemoryObservabilityBridgeTest.cpp
  "integration;memory"
)

dasall_add_memory_integration_test(
  dasall_memory_production_logging_integration_test
  MemoryProductionLoggingIntegrationTest
  MemoryProductionLoggingIntegrationTest.cpp
  "integration;memory;logging"
)

dasall_add_memory_integration_test(
  dasall_memory_compression_llm_summarizer_integration_test
  MemoryCompressionLLMSummarizerIntegrationTest
  MemoryCompressionLLMSummarizerIntegrationTest.cpp
  "integration;memory"
)

dasall_add_memory_integration_test(
  dasall_memory_checkpoint_busy_integration_test
  MemoryCheckpointBusyTest
  MemoryCheckpointBusyTest.cpp
  "integration;memory"
)

dasall_add_memory_integration_test(
  dasall_memory_long_running_soak_integration_test
  MemoryLongRunningSoakTest
  MemoryLongRunningSoakTest.cpp
  "integration;memory;stress"
)

target_link_libraries(dasall_memory_context_assemble_integration_test
  PRIVATE
    dasall_sqlite3
)

target_link_libraries(dasall_memory_context_integration_test
  PRIVATE
    dasall_sqlite3
)

target_link_libraries(dasall_memory_writeback_integration_test
  PRIVATE
    dasall_sqlite3
)

target_link_libraries(dasall_memory_failure_injection_integration_test
  PRIVATE
    dasall_sqlite3
)

target_link_libraries(dasall_memory_profile_compatibility_integration_test
  PRIVATE
    dasall_profiles
)

target_link_libraries(dasall_memory_maintenance_integration_test
  PRIVATE
    dasall_sqlite3
)

target_link_libraries(dasall_memory_observability_bridge_integration_test
  PRIVATE
    dasall_infra
    dasall_sqlite3
)

target_link_libraries(dasall_memory_production_logging_integration_test
  PRIVATE
    dasall_apps_runtime_support
    dasall_llm
    dasall_infra
    dasall_sqlite3
)

target_link_libraries(dasall_memory_compression_llm_summarizer_integration_test
  PRIVATE
    dasall_apps_runtime_support
    dasall_llm
    dasall_sqlite3
)

target_link_libraries(dasall_memory_checkpoint_busy_integration_test
  PRIVATE
    dasall_sqlite3
)

target_link_libraries(dasall_memory_long_running_soak_integration_test
  PRIVATE
    dasall_sqlite3
)

target_include_directories(dasall_memory_writeback_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/memory/src
)

target_include_directories(dasall_memory_failure_injection_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/memory/src
)

target_include_directories(dasall_memory_profile_compatibility_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/memory/src
)

target_include_directories(dasall_memory_maintenance_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/memory/src
)

target_include_directories(dasall_memory_observability_bridge_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/infra/include
    ${CMAKE_SOURCE_DIR}/infra/src
    ${CMAKE_SOURCE_DIR}/memory/src
)

target_include_directories(dasall_memory_production_logging_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/runtime_support/src
    ${CMAKE_SOURCE_DIR}/infra/include
    ${CMAKE_SOURCE_DIR}/infra/src
    ${CMAKE_SOURCE_DIR}/memory/src
    ${CMAKE_SOURCE_DIR}/tests/mocks/include
)

target_include_directories(dasall_memory_compression_llm_summarizer_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/runtime_support/src
    ${CMAKE_SOURCE_DIR}/tests/mocks/include
)

target_include_directories(dasall_memory_checkpoint_busy_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/memory/src
)

target_include_directories(dasall_memory_long_running_soak_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/memory/src
)

target_compile_definitions(dasall_memory_context_assemble_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_compile_definitions(dasall_memory_context_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_compile_definitions(dasall_memory_writeback_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_compile_definitions(dasall_memory_failure_injection_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_compile_definitions(dasall_memory_maintenance_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_compile_definitions(dasall_memory_observability_bridge_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_compile_definitions(dasall_memory_production_logging_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_compile_definitions(dasall_memory_compression_llm_summarizer_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_compile_definitions(dasall_memory_checkpoint_busy_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_compile_definitions(dasall_memory_long_running_soak_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

set_tests_properties(MemoryLongRunningSoakTest PROPERTIES TIMEOUT 180)

if(NOT DASALL_MEMORY_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "memory integration registration requires at least one test target")
endif()

set(DASALL_MEMORY_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_MEMORY_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)