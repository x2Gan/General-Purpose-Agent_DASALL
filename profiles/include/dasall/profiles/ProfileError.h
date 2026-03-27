#pragma once

#include <cstdint>
#include <string_view>

namespace dasall::profiles {

enum class ProfileErrorCode : std::uint16_t {
  ProfileNotFound = 1,
  CatalogUnavailable = 2,
  SchemaInvalid = 3,
  PlatformMismatch = 4,
  ModuleIncompatible = 5,
  RequiredAdapterMissing = 6,
  OverrideInvalid = 7,
  LastKnownGoodUnavailable = 8,
};

enum class ProfileErrorCategory {
  Catalog,
  Schema,
  Compatibility,
  Override,
  Recovery,
};

[[nodiscard]] inline constexpr std::string_view profile_error_code_name(ProfileErrorCode code) {
  switch (code) {
    case ProfileErrorCode::ProfileNotFound:
      return "PRF_E_PROFILE_NOT_FOUND";
    case ProfileErrorCode::CatalogUnavailable:
      return "PRF_E_CATALOG_UNAVAILABLE";
    case ProfileErrorCode::SchemaInvalid:
      return "PRF_E_SCHEMA_INVALID";
    case ProfileErrorCode::PlatformMismatch:
      return "PRF_E_PLATFORM_MISMATCH";
    case ProfileErrorCode::ModuleIncompatible:
      return "PRF_E_MODULE_INCOMPATIBLE";
    case ProfileErrorCode::RequiredAdapterMissing:
      return "PRF_E_REQUIRED_ADAPTER_MISSING";
    case ProfileErrorCode::OverrideInvalid:
      return "PRF_E_OVERRIDE_INVALID";
    case ProfileErrorCode::LastKnownGoodUnavailable:
      return "PRF_E_LAST_KNOWN_GOOD_UNAVAILABLE";
  }

  return "PRF_E_UNKNOWN";
}

[[nodiscard]] inline constexpr ProfileErrorCategory classify_profile_error_code(
    ProfileErrorCode code) {
  switch (code) {
    case ProfileErrorCode::ProfileNotFound:
    case ProfileErrorCode::CatalogUnavailable:
      return ProfileErrorCategory::Catalog;
    case ProfileErrorCode::SchemaInvalid:
      return ProfileErrorCategory::Schema;
    case ProfileErrorCode::PlatformMismatch:
    case ProfileErrorCode::ModuleIncompatible:
    case ProfileErrorCode::RequiredAdapterMissing:
      return ProfileErrorCategory::Compatibility;
    case ProfileErrorCode::OverrideInvalid:
      return ProfileErrorCategory::Override;
    case ProfileErrorCode::LastKnownGoodUnavailable:
      return ProfileErrorCategory::Recovery;
  }

  return ProfileErrorCategory::Compatibility;
}

}  // namespace dasall::profiles