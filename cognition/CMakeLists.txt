add_library(dasall_cognition STATIC)

target_sources(dasall_cognition
  PRIVATE
    src/CognitionFacade.cpp
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
)

target_link_libraries(dasall_cognition
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_cognition)
