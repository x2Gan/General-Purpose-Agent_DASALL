#include <exception>
#include <iostream>
#include <string_view>
#include <type_traits>

#include "IProfileCatalog.h"
#include "support/TestAssertions.h"

namespace {

class FakeProfileCatalog final : public dasall::profiles::IProfileCatalog {
 public:
  [[nodiscard]] dasall::profiles::ProfileCatalogListResult list_profiles() const override {
    return dasall::profiles::ProfileCatalogListResult{
        .profiles = {make_profile_descriptor("desktop_full")},
        .error_code = std::nullopt,
    };
  }

  [[nodiscard]] dasall::profiles::ProfileCatalogLookupResult get_profile(
      std::string_view profile_id) const override {
    if (profile_id == "desktop_full") {
      return dasall::profiles::ProfileCatalogLookupResult{
          .profile = make_profile_descriptor("desktop_full"),
          .error_code = std::nullopt,
      };
    }

    return dasall::profiles::ProfileCatalogLookupResult{
        .profile = std::nullopt,
        .error_code = std::nullopt,
    };
  }

 private:
  static dasall::profiles::ProfileDescriptor make_profile_descriptor(std::string_view profile_id) {
    return dasall::profiles::ProfileDescriptor{
        .profile_id = std::string(profile_id),
        .schema_version = "v1",
        .target_platform = "linux",
        .asset_paths = dasall::profiles::ProfileAssetPaths{
            .profile_root = "profiles/desktop_full",
            .profile_cmake_path = "profiles/desktop_full/profile.cmake",
            .runtime_policy_path = "profiles/desktop_full/runtime_policy.yaml",
        },
        .support_level = "ga",
    };
  }
};

void test_profile_descriptor_requires_complete_identity_and_asset_paths() {
  using dasall::profiles::ProfileDescriptor;
  using dasall::tests::support::assert_true;

  const ProfileDescriptor valid_descriptor{
      .profile_id = "desktop_full",
      .schema_version = "v1",
      .target_platform = "linux",
      .asset_paths = {
          .profile_root = "profiles/desktop_full",
          .profile_cmake_path = "profiles/desktop_full/profile.cmake",
          .runtime_policy_path = "profiles/desktop_full/runtime_policy.yaml",
      },
      .support_level = "ga",
  };

  ProfileDescriptor missing_schema = valid_descriptor;
  missing_schema.schema_version.clear();

  ProfileDescriptor missing_assets = valid_descriptor;
  missing_assets.asset_paths.runtime_policy_path.clear();

  assert_true(valid_descriptor.has_consistent_values(),
              "profile descriptor should accept complete identity and asset paths");
  assert_true(!missing_schema.has_consistent_values(),
              "profile descriptor should reject empty schema version");
  assert_true(!missing_assets.has_consistent_values(),
              "profile descriptor should reject incomplete asset paths");
}

void test_profile_catalog_surface_supports_mock_listing_and_lookup() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const FakeProfileCatalog catalog;
  const auto listed_profiles = catalog.list_profiles();
  const auto looked_up_profile = catalog.get_profile("desktop_full");

  assert_true(listed_profiles.ok(), "profile catalog list should support success-only mock path");
  assert_true(listed_profiles.has_consistent_values(),
              "listed profiles should satisfy unique id and descriptor consistency checks");
  assert_equal(static_cast<std::size_t>(1), listed_profiles.profiles.size(),
               "profile catalog should return one stable mock profile");
  assert_true(looked_up_profile.ok(), "profile lookup should return the known mock descriptor");
  assert_equal(std::string("desktop_full"), looked_up_profile.profile->profile_id,
               "profile lookup should preserve the requested profile id");
}

void test_iprofile_catalog_interface_surface_stays_stable() {
  using dasall::profiles::IProfileCatalog;
  using dasall::profiles::ProfileCatalogListResult;
  using dasall::profiles::ProfileCatalogLookupResult;
  using dasall::tests::support::assert_true;

  using ListProfilesSignature = ProfileCatalogListResult (IProfileCatalog::*)() const;
  using GetProfileSignature = ProfileCatalogLookupResult (IProfileCatalog::*)(std::string_view) const;

  static_assert(std::is_same_v<decltype(&IProfileCatalog::list_profiles), ListProfilesSignature>,
                "IProfileCatalog::list_profiles signature should remain stable");
  static_assert(std::is_same_v<decltype(&IProfileCatalog::get_profile), GetProfileSignature>,
                "IProfileCatalog::get_profile signature should remain stable");

  assert_true(std::is_abstract_v<IProfileCatalog>,
              "IProfileCatalog should remain an abstract interface");
}

}  // namespace

int main() {
  try {
    test_profile_descriptor_requires_complete_identity_and_asset_paths();
    test_profile_catalog_surface_supports_mock_listing_and_lookup();
    test_iprofile_catalog_interface_surface_stays_stable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}