set(DASALL_KNOWLEDGE_INTEGRATION_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_knowledge_integration_test target_name test_name source_file labels)
  add_executable(${target_name}
    ${CMAKE_CURRENT_SOURCE_DIR}/${source_file}
  )

  target_link_libraries(${target_name}
    PRIVATE
      dasall_test_support
      dasall_knowledge
  )

  target_compile_definitions(${target_name}
    PRIVATE
      DASALL_KNOWLEDGE_MODULE_CMAKE="${CMAKE_SOURCE_DIR}/knowledge/CMakeLists.txt"
      DASALL_KNOWLEDGE_PUBLIC_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/knowledge/include"
      DASALL_KNOWLEDGE_UNIT_TEST_DIR="${CMAKE_SOURCE_DIR}/tests/unit/knowledge"
      DASALL_KNOWLEDGE_INTEGRATION_TEST_DIR="${CMAKE_SOURCE_DIR}/tests/integration/knowledge"
      DASALL_KNOWLEDGE_INTEGRATION_TOP_LEVEL_CMAKE="${CMAKE_SOURCE_DIR}/tests/integration/CMakeLists.txt"
  )

  dasall_apply_common_options(${target_name})

  add_test(NAME ${test_name} COMMAND ${target_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "${labels}")

  list(APPEND DASALL_KNOWLEDGE_INTEGRATION_TEST_EXECUTABLE_TARGETS ${target_name})
endmacro()

dasall_add_knowledge_integration_test(
  dasall_knowledge_integration_topology_smoke_integration_test
  KnowledgeIntegrationTopologySmokeTest
  KnowledgeIntegrationTopologySmokeTest.cpp
  "integration;knowledge"
)

dasall_add_knowledge_integration_test(
  dasall_knowledge_retrieval_smoke_integration_test
  dasall_knowledge_retrieval_smoke_integration_test
  KnowledgeRetrievalSmokeTest.cpp
  "integration;knowledge"
)

dasall_add_knowledge_integration_test(
  dasall_runtime_knowledge_evidence_integration_test
  RuntimeKnowledgeEvidenceIntegrationTest
  RuntimeKnowledgeEvidenceIntegrationTest.cpp
  "integration;knowledge;runtime"
)

dasall_add_knowledge_integration_test(
  dasall_knowledge_failure_degrade_integration_test
  dasall_knowledge_failure_degrade_integration_test
  KnowledgeFailureDegradeTest.cpp
  "integration;knowledge"
)

dasall_add_knowledge_integration_test(
  dasall_knowledge_profile_compatibility_integration_test
  KnowledgeProfileCompatibilityTest
  KnowledgeProfileCompatibilityTest.cpp
  "integration;knowledge"
)

dasall_add_knowledge_integration_test(
  dasall_knowledge_retrieval_quality_regression_integration_test
  RetrievalQualityRegressionTest
  RetrievalQualityRegressionTest.cpp
  "integration;knowledge"
)

dasall_add_knowledge_integration_test(
  dasall_knowledge_refresh_loop_integration_test
  KnowledgeRefreshLoopTest
  KnowledgeRefreshLoopTest.cpp
  "integration;knowledge"
)

target_include_directories(dasall_knowledge_retrieval_smoke_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/knowledge/src
)

target_link_libraries(dasall_knowledge_retrieval_smoke_integration_test
  PRIVATE
    dasall_sqlite3
)

target_include_directories(dasall_runtime_knowledge_evidence_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/knowledge/src
    ${CMAKE_SOURCE_DIR}/memory/include
    ${CMAKE_SOURCE_DIR}/runtime/include
    ${CMAKE_SOURCE_DIR}/services/include
    ${CMAKE_SOURCE_DIR}/tests/fixtures/runtime
    ${CMAKE_SOURCE_DIR}/tools/src
)

target_compile_definitions(dasall_runtime_knowledge_evidence_integration_test
  PRIVATE
    DASALL_SQL_MEMORY_DIR="${CMAKE_SOURCE_DIR}/sql/memory"
)

target_link_libraries(dasall_runtime_knowledge_evidence_integration_test
  PRIVATE
    dasall_runtime
    dasall_memory
    dasall_cognition
    dasall_tools
    dasall_sqlite3
)

target_include_directories(dasall_knowledge_failure_degrade_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/knowledge/src
)

target_link_libraries(dasall_knowledge_failure_degrade_integration_test
  PRIVATE
    dasall_sqlite3
)

target_include_directories(dasall_knowledge_retrieval_quality_regression_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/knowledge/src
)

target_link_libraries(dasall_knowledge_retrieval_quality_regression_integration_test
  PRIVATE
    dasall_sqlite3
)

target_include_directories(dasall_knowledge_refresh_loop_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/knowledge/src
)

target_link_libraries(dasall_knowledge_refresh_loop_integration_test
  PRIVATE
    dasall_sqlite3
)

target_include_directories(dasall_knowledge_profile_compatibility_integration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/knowledge/src
)

target_link_libraries(dasall_knowledge_profile_compatibility_integration_test
  PRIVATE
    dasall_profiles
)

if(NOT DASALL_KNOWLEDGE_INTEGRATION_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "knowledge integration registration requires at least one test target")
endif()

set(DASALL_KNOWLEDGE_INTEGRATION_TEST_EXECUTABLE_TARGETS
  ${DASALL_KNOWLEDGE_INTEGRATION_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)