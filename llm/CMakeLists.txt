add_library(dasall_llm STATIC)

target_sources(dasall_llm
  PRIVATE
    src/placeholder.cpp
)

target_compile_features(dasall_llm PUBLIC cxx_std_20)

target_include_directories(dasall_llm
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dasall_llm
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_llm)
