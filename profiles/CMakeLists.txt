add_library(dasall_profiles STATIC)

target_sources(dasall_profiles
  PRIVATE
    src/placeholder.cpp
)

target_compile_features(dasall_profiles PUBLIC cxx_std_20)

target_link_libraries(dasall_profiles
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_profiles)