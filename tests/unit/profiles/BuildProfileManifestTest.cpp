#include <exception>
#include <iostream>

#include "BuildProfileManifest.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_build_profile_manifest_exposes_complete_field_set() {
  using dasall::profiles::BuildProfileManifest;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const BuildProfileManifest manifest{
      .enabled_modules = {"runtime", "llm", "tools"},
      .enabled_adapters = {"mcp", "sqlite"},
      .observability_level = "full",
      .build_tags = {"unit", "desktop_full"},
      .toolchain_hint = std::string("host-gcc"),
  };

  assert_true(manifest.has_consistent_values(),
              "build profile manifest should accept complete matrix metadata");
  assert_true(manifest.enables_module("runtime"),
              "build profile manifest should report enabled modules");
  assert_true(manifest.enables_adapter("mcp"),
              "build profile manifest should report enabled adapters");
  assert_true(manifest.has_build_tag("unit"),
              "build profile manifest should report registered build tags");
  assert_equal(std::string("host-gcc"), *manifest.toolchain_hint,
               "build profile manifest should preserve toolchain hint");
}

void test_build_profile_manifest_rejects_incomplete_or_duplicate_fields() {
  using dasall::profiles::BuildProfileManifest;
  using dasall::tests::support::assert_true;

  BuildProfileManifest missing_modules{
      .enabled_modules = {},
      .enabled_adapters = {},
      .observability_level = "minimal",
      .build_tags = {},
      .toolchain_hint = std::nullopt,
  };

  BuildProfileManifest duplicate_modules{
      .enabled_modules = {"runtime", "runtime"},
      .enabled_adapters = {},
      .observability_level = "minimal",
      .build_tags = {},
      .toolchain_hint = std::nullopt,
  };

  BuildProfileManifest duplicate_tags{
      .enabled_modules = {"runtime"},
      .enabled_adapters = {},
      .observability_level = "minimal",
      .build_tags = {"unit", "unit"},
      .toolchain_hint = std::nullopt,
  };

  BuildProfileManifest empty_toolchain_hint{
      .enabled_modules = {"runtime"},
      .enabled_adapters = {},
      .observability_level = "minimal",
      .build_tags = {},
      .toolchain_hint = std::string(),
  };

  assert_true(!missing_modules.has_consistent_values(),
              "build profile manifest should reject empty enabled_modules");
  assert_true(!duplicate_modules.has_consistent_values(),
              "build profile manifest should reject duplicate module names");
  assert_true(!duplicate_tags.has_consistent_values(),
              "build profile manifest should reject duplicate build tags");
  assert_true(!empty_toolchain_hint.has_consistent_values(),
              "build profile manifest should reject empty toolchain hints");
}

}  // namespace

int main() {
  try {
    test_build_profile_manifest_exposes_complete_field_set();
    test_build_profile_manifest_rejects_incomplete_or_duplicate_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}