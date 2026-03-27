add_library(dasall_profiles STATIC)

target_sources(dasall_profiles
  PRIVATE
    src/ProfileYamlParser.cpp
    src/ProfileCatalog.cpp
    src/BuildProfileResolver.cpp
    src/RuntimePolicyProvider.cpp
    src/ProfileCompatibilityValidator.cpp
    src/placeholder.cpp
)

target_include_directories(dasall_profiles
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(dasall_profiles PUBLIC cxx_std_20)

target_link_libraries(dasall_profiles
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_profiles)