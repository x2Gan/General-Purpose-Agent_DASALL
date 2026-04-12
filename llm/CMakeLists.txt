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
    src/LLMSubsystemConfig.cpp
    src/TokenEstimator.cpp
    src/prompt/PromptAssetRepository.cpp
    src/prompt/PromptComposer.cpp
    src/prompt/PromptPipeline.cpp
    src/prompt/PromptPolicy.cpp
    src/prompt/PromptRegistry.cpp
    src/prompt/TemplateRenderer.cpp
    src/provider/ProviderCatalogRepository.cpp
    src/placeholder.cpp
)

target_compile_features(dasall_llm PUBLIC cxx_std_20)

target_include_directories(dasall_llm
  PUBLIC
    $<BUILD_INTERFACE:${DASALL_LLM_PUBLIC_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${PROJECT_SOURCE_DIR}/profiles/include
)

target_link_libraries(dasall_llm
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_llm)
