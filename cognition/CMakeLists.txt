add_library(dasall_cognition STATIC)

target_sources(dasall_cognition
  PRIVATE
    src/CognitionFacade.cpp
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
