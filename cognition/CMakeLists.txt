add_library(dasall_cognition STATIC)

target_sources(dasall_cognition
  PRIVATE
    src/CognitionFacade.cpp
    src/StagePolicyResolver.cpp
    src/llm/CognitionLlmBridge.cpp
    src/observability/CognitionTelemetry.cpp
    src/response/ResponseBuilder.cpp
    src/planning/Planner.cpp
    src/planning/PlanGraphBuilder.cpp
    src/reasoning/Reasoner.cpp
    src/reasoning/DecisionProjector.cpp
    src/reflection/ReflectionEngine.cpp
    src/belief/BeliefUpdateSynthesizer.cpp
    src/perception/PerceptionEngine.cpp
    src/validation/InputBoundaryValidator.cpp
    src/validation/StageOutputValidator.cpp
    src/config/CognitionConfigProjector.cpp
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
      include/CognitionConfig.h
      include/CognitionTypes.h
      include/ICognitionEngine.h
      include/IPlanner.h
      include/IReasoner.h
      include/IReflectionEngine.h
      include/IResponseBuilder.h
      include/belief/BeliefUpdateHint.h
      include/decision/ActionDecision.h
      include/perception/PerceptionResult.h
      include/plan/PlanGraph.h
      include/plan/ReplanResult.h
      include/response/ResponseBuildRequest.h
      include/response/ResponseBuildResult.h
)

target_compile_features(dasall_cognition PUBLIC cxx_std_20)

target_include_directories(dasall_cognition
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dasall_cognition
  PUBLIC
    dasall_contracts
  PRIVATE
    dasall_llm
    dasall_profiles
)

dasall_apply_common_options(dasall_cognition)
