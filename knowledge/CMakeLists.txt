add_library(dasall_knowledge STATIC)

target_sources(dasall_knowledge
  PRIVATE
    src/placeholder.cpp
)

target_compile_features(dasall_knowledge PUBLIC cxx_std_20)

target_include_directories(dasall_knowledge
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dasall_knowledge
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_knowledge)
