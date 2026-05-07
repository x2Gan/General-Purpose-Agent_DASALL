add_executable(dasall_profile_catalog_interface_unit_test
  ProfileCatalogInterfaceTest.cpp
)

target_link_libraries(dasall_profile_catalog_interface_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall_profile_catalog_interface_unit_test)

add_test(NAME ProfileCatalogInterfaceTest COMMAND dasall_profile_catalog_interface_unit_test)
set_tests_properties(ProfileCatalogInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_profile_catalog_unit_test
  ProfileCatalogTest.cpp
)

target_link_libraries(dasall_profile_catalog_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall_profile_catalog_unit_test)

add_test(NAME ProfileCatalogTest COMMAND dasall_profile_catalog_unit_test)
set_tests_properties(ProfileCatalogTest PROPERTIES LABELS "unit")

add_executable(dasall_build_profile_manifest_unit_test
  BuildProfileManifestTest.cpp
)

target_link_libraries(dasall_build_profile_manifest_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall_build_profile_manifest_unit_test)

add_test(NAME BuildProfileManifestTest COMMAND dasall_build_profile_manifest_unit_test)
set_tests_properties(BuildProfileManifestTest PROPERTIES LABELS "unit")

add_executable(dasall_build_profile_resolver_unit_test
  BuildProfileResolverTest.cpp
)

target_link_libraries(dasall_build_profile_resolver_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall_build_profile_resolver_unit_test)

add_test(NAME BuildProfileResolverTest COMMAND dasall_build_profile_resolver_unit_test)
set_tests_properties(BuildProfileResolverTest PROPERTIES LABELS "unit")

add_executable(dasall_runtime_policy_snapshot_unit_test
  RuntimePolicySnapshotTest.cpp
)

target_link_libraries(dasall_runtime_policy_snapshot_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall_runtime_policy_snapshot_unit_test)

add_test(NAME RuntimePolicySnapshotTest COMMAND dasall_runtime_policy_snapshot_unit_test)
set_tests_properties(RuntimePolicySnapshotTest PROPERTIES LABELS "unit")

add_executable(dasall_runtime_policy_provider_unit_test
  RuntimePolicyProviderTest.cpp
)

target_link_libraries(dasall_runtime_policy_provider_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall_runtime_policy_provider_unit_test)

add_test(NAME RuntimePolicyProviderTest COMMAND dasall_runtime_policy_provider_unit_test)
set_tests_properties(RuntimePolicyProviderTest PROPERTIES LABELS "unit")

add_executable(dasall-daemon_profile_projection_unit_test
  DaemonProfileProjectionTest.cpp
)

target_link_libraries(dasall-daemon_profile_projection_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall-daemon_profile_projection_unit_test)

add_test(NAME DaemonProfileProjectionTest COMMAND dasall-daemon_profile_projection_unit_test)
set_tests_properties(DaemonProfileProjectionTest PROPERTIES LABELS "unit")

add_executable(dasall_profile_compatibility_validator_unit_test
  ProfileCompatibilityValidatorTest.cpp
)

target_link_libraries(dasall_profile_compatibility_validator_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall_profile_compatibility_validator_unit_test)

add_test(NAME ProfileCompatibilityValidatorTest
         COMMAND dasall_profile_compatibility_validator_unit_test)
set_tests_properties(ProfileCompatibilityValidatorTest PROPERTIES LABELS "unit")

add_executable(dasall_profile_matrix_consistency_unit_test
  ProfileMatrixConsistencyTest.cpp
)

target_link_libraries(dasall_profile_matrix_consistency_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

target_include_directories(dasall_profile_matrix_consistency_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/profiles/src
)

dasall_apply_common_options(dasall_profile_matrix_consistency_unit_test)

add_test(NAME ProfileMatrixConsistencyTest COMMAND dasall_profile_matrix_consistency_unit_test)
set_tests_properties(ProfileMatrixConsistencyTest PROPERTIES LABELS "unit")

add_executable(dasall_profile_overlay_composer_unit_test
  ProfileOverlayComposerTest.cpp
)

target_link_libraries(dasall_profile_overlay_composer_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall_profile_overlay_composer_unit_test)

add_test(NAME ProfileOverlayComposerTest COMMAND dasall_profile_overlay_composer_unit_test)
set_tests_properties(ProfileOverlayComposerTest PROPERTIES LABELS "unit")

add_executable(dasall_validation_report_unit_test
  ValidationReportTest.cpp
)

target_link_libraries(dasall_validation_report_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall_validation_report_unit_test)

add_test(NAME ValidationReportTest COMMAND dasall_validation_report_unit_test)
set_tests_properties(ValidationReportTest PROPERTIES LABELS "unit")

add_executable(dasall_last_known_good_store_unit_test
  LastKnownGoodStoreInterfaceTest.cpp
)

target_link_libraries(dasall_last_known_good_store_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall_last_known_good_store_unit_test)

add_test(NAME LastKnownGoodStoreInterfaceTest COMMAND dasall_last_known_good_store_unit_test)
set_tests_properties(LastKnownGoodStoreInterfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_last_known_good_store_impl_unit_test
  LastKnownGoodStoreTest.cpp
)

target_link_libraries(dasall_last_known_good_store_impl_unit_test
  PRIVATE
    dasall_test_support
    dasall_profiles
)

dasall_apply_common_options(dasall_last_known_good_store_impl_unit_test)

add_test(NAME LastKnownGoodStoreTest COMMAND dasall_last_known_good_store_impl_unit_test)
set_tests_properties(LastKnownGoodStoreTest PROPERTIES LABELS "unit")