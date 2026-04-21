add_library(dasall_knowledge STATIC)

set(DASALL_KNOWLEDGE_PUBLIC_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(DASALL_KNOWLEDGE_PUBLIC_HEADERS
  include/evidence/EvidenceAssembler.h
  include/IKnowledgeService.h
  include/KnowledgeErrors.h
  include/KnowledgeTypes.h
  include/health/FreshnessController.h
  include/health/KnowledgeHealthProbe.h
  include/index/CorpusCatalog.h
  include/query/CorpusRouter.h
  include/query/QueryNormalizer.h
  include/rerank/Reranker.h
  include/retrieve/RecallTypes.h
)

if(NOT EXISTS ${DASALL_KNOWLEDGE_PUBLIC_INCLUDE_DIR})
  message(FATAL_ERROR "dasall_knowledge requires ${DASALL_KNOWLEDGE_PUBLIC_INCLUDE_DIR} to exist")
endif()

foreach(public_header IN LISTS DASALL_KNOWLEDGE_PUBLIC_HEADERS)
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${public_header})
    message(FATAL_ERROR "dasall_knowledge requires ${CMAKE_CURRENT_SOURCE_DIR}/${public_header} to exist")
  endif()
endforeach()

target_sources(dasall_knowledge
  PRIVATE
    src/config/KnowledgeConfigProjector.cpp
    src/evidence/EvidenceAssembler.cpp
    src/health/FreshnessController.cpp
    src/health/KnowledgeHealthProbe.cpp
    src/KnowledgeBuildSkeleton.cpp
    src/index/CorpusCatalog.cpp
    src/observability/KnowledgeTelemetry.cpp
    src/query/CorpusRouter.cpp
    src/query/QueryNormalizer.cpp
    src/rerank/Reranker.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS
      ${DASALL_KNOWLEDGE_PUBLIC_INCLUDE_DIR}
    FILES
      ${DASALL_KNOWLEDGE_PUBLIC_HEADERS}
)

target_compile_features(dasall_knowledge PUBLIC cxx_std_20)

target_include_directories(dasall_knowledge
  PUBLIC
    $<BUILD_INTERFACE:${DASALL_KNOWLEDGE_PUBLIC_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dasall_knowledge
  PUBLIC
    dasall_contracts
  PRIVATE
    dasall_profiles
)

dasall_apply_common_options(dasall_knowledge)
