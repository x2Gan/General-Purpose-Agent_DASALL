add_library(dasall_llm STATIC)

set(DASALL_LLM_PUBLIC_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(DASALL_LLM_PUBLIC_INCLUDE_SUBDIRECTORIES
  prompt
  provider
  route
  stream
)

if(NOT EXISTS ${DASALL_LLM_PUBLIC_INCLUDE_DIR})
  message(FATAL_ERROR "dasall_llm requires ${DASALL_LLM_PUBLIC_INCLUDE_DIR} to exist")
endif()

foreach(public_subdirectory IN LISTS DASALL_LLM_PUBLIC_INCLUDE_SUBDIRECTORIES)
  if(NOT EXISTS ${DASALL_LLM_PUBLIC_INCLUDE_DIR}/${public_subdirectory})
    message(FATAL_ERROR
      "dasall_llm requires ${DASALL_LLM_PUBLIC_INCLUDE_DIR}/${public_subdirectory} to exist"
    )
  endif()
endforeach()

target_sources(dasall_llm
  PRIVATE
    src/LLMManager.cpp
    src/LLMProductionFactory.cpp
    src/LLMSubsystemConfig.cpp
    src/TokenEstimator.cpp
    src/UsageAggregator.cpp
    src/adapters/LocalLLMAdapter.cpp
    src/adapters/OpenAICompatibleAdapter.cpp
    src/adapters/OllamaAdapter.cpp
    src/execution/ResponseNormalizer.cpp
    src/observability/LLMAuditBridge.cpp
    src/observability/LLMMetricsBridge.cpp
    src/observability/LLMTraceBridge.cpp
    src/prompt/PromptAssetRepository.cpp
    src/prompt/PromptComposer.cpp
    src/prompt/PromptPipeline.cpp
    src/prompt/PromptPolicy.cpp
    src/prompt/PromptRegistry.cpp
    src/prompt/TemplateRenderer.cpp
    src/provider/ProviderCatalogRepository.cpp
    src/route/AdapterRegistry.cpp
    src/stream/StreamSessionRegistry.cpp
    src/route/ModelRouter.cpp
    src/transport/CurlCommandLLMTransport.cpp
    src/placeholder.cpp
)

target_compile_features(dasall_llm PUBLIC cxx_std_20)

target_include_directories(dasall_llm
  PUBLIC
    $<BUILD_INTERFACE:${DASALL_LLM_PUBLIC_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/profiles/include
    ${PROJECT_SOURCE_DIR}/infra/src
)

target_link_libraries(dasall_llm
  PUBLIC
    dasall_contracts
    dasall_infra
)

install(DIRECTORY
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/prompts
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/providers
  DESTINATION ${DASALL_INSTALL_LLM_DATADIR}
)

dasall_apply_common_options(dasall_llm)
