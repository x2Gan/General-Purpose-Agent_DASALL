add_library(dasall_profiles STATIC)

target_sources(dasall_profiles
  PRIVATE
    src/ProfileYamlParser.cpp
    src/ProfileCatalog.cpp
    src/BuildProfileResolver.cpp
    src/DaemonProfileProjection.cpp
    src/RuntimePolicyProvider.cpp
    src/ProfileOverlayComposer.cpp
    src/ProfileCompatibilityValidator.cpp
    src/LastKnownGoodStore.cpp
    src/ProfileTelemetryAdapter.cpp
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
    dasall_infra
)

install(DIRECTORY
  ${CMAKE_CURRENT_SOURCE_DIR}/desktop_full
  ${CMAKE_CURRENT_SOURCE_DIR}/cloud_full
  ${CMAKE_CURRENT_SOURCE_DIR}/edge_balanced
  ${CMAKE_CURRENT_SOURCE_DIR}/edge_minimal
  ${CMAKE_CURRENT_SOURCE_DIR}/factory_test
  DESTINATION ${DASALL_INSTALL_PROFILESDIR}
)

dasall_apply_common_options(dasall_profiles)