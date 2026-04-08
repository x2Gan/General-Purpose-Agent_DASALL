#include <exception>
#include <iostream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "plugin/IPluginCompatibilityEngine.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

std::pair<std::string_view, std::string_view> split_required_abi(std::string_view required_abi) {
  const auto separator = required_abi.find('@');
  return {required_abi.substr(0, separator), required_abi.substr(separator + 1)};
}

dasall::infra::plugin::PluginManifest make_valid_manifest(
    std::string required_abi = "x86_64-linux-gnu@1.2.0") {
  using dasall::infra::plugin::PluginManifest;
  using dasall::infra::plugin::PluginManifestExtension;

  return PluginManifest::normalize(PluginManifest{
      .schema_version = std::string("1.0.0"),
      .plugin_id = std::string("plugin.echo.vendor"),
      .version = std::string("1.2.3"),
      .entry = std::string("dasall_plugin_entry_v1"),
      .required_abi = std::move(required_abi),
      .capabilities = {std::string("plugin.echo.execute")},
      .signature_ref = std::string("sig:plugin.echo.vendor@1.2.3"),
      .extensions = {PluginManifestExtension{
          .key = std::string("x.acme.runtime_profile"),
          .serialized_value = std::string("desktop"),
      }},
  });
}

dasall::infra::plugin::PluginHostAbiSnapshot make_host_abi(
    std::string platform_tag = "x86_64-linux-gnu",
    std::string abi_version = "1.2.3",
    bool strict_mode = true,
    bool api_ready = true) {
  return dasall::infra::plugin::PluginHostAbiSnapshot{
      .platform_tag = std::move(platform_tag),
      .abi_version = std::move(abi_version),
      .strict_mode = strict_mode,
      .api_ready = api_ready,
  };
}

dasall::infra::plugin::PluginDependencyMatrixSnapshot make_dependency_matrix(
    std::vector<std::string> required_dependency_refs = {std::string("plugin.core.runtime")},
    std::vector<std::string> available_dependency_refs = {std::string("plugin.core.runtime"),
                                                          std::string("plugin.core.metrics")}) {
  return dasall::infra::plugin::PluginDependencyMatrixSnapshot{
      .required_dependency_refs = std::move(required_dependency_refs),
      .available_dependency_refs = std::move(available_dependency_refs),
  };
}

class NullPluginCompatibilityEngine final : public dasall::infra::plugin::IPluginCompatibilityEngine {
 public:
  [[nodiscard]] dasall::infra::plugin::CompatibilityReport check(
      const dasall::infra::plugin::PluginCompatibilityCheckRequest& request) const override {
    using dasall::infra::plugin::CompatibilityReport;

    if (!request.is_valid()) {
      return CompatibilityReport::failure(
          false,
          false,
          false,
          {std::string("plugin_compatibility_request_invalid")},
          request.host_abi.platform_tag,
          request.manifest.required_abi,
          std::string("audit:plugin.compatibility.invalid"));
    }

    const auto [required_platform_tag, required_abi_version] =
        split_required_abi(request.manifest.required_abi);
    const bool abi_ok = required_platform_tag == request.host_abi.platform_tag &&
                        dasall::infra::plugin::plugin_abi_version_satisfies_requirement(
                            request.host_abi.abi_version,
                            required_abi_version,
                            request.host_abi.strict_mode);
    const bool api_ok = request.host_abi.api_ready;
    const bool dependency_ok = request.dependency_matrix.satisfies_required_dependencies();

    if (abi_ok && api_ok && dependency_ok) {
      return CompatibilityReport::success(
          request.host_abi.platform_tag,
          request.manifest.required_abi,
          std::string("audit:plugin.compatibility.ok"));
    }

    std::vector<std::string> reason_codes;
    if (!abi_ok) {
      reason_codes.push_back(required_platform_tag == request.host_abi.platform_tag
                                 ? std::string("plugin_abi_version_incompatible")
                                 : std::string("plugin_platform_tag_mismatch"));
    }
    if (!api_ok) {
      reason_codes.push_back(std::string("plugin_api_handshake_unavailable"));
    }
    if (!dependency_ok) {
      reason_codes.push_back(std::string("plugin_dependency_missing"));
    }

    return CompatibilityReport::failure(
        abi_ok,
        api_ok,
        dependency_ok,
        std::move(reason_codes),
        request.host_abi.platform_tag,
        request.manifest.required_abi,
        std::string("audit:plugin.compatibility.fail"));
  }
};

void test_plugin_compatibility_engine_interface_accepts_strict_patch_forward_compatibility() {
  using dasall::infra::plugin::CompatibilityReport;
  using dasall::infra::plugin::IPluginCompatibilityEngine;
  using dasall::infra::plugin::PluginCompatibilityCheckRequest;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(std::declval<const IPluginCompatibilityEngine&>().check(
                                   std::declval<const PluginCompatibilityCheckRequest&>())),
                               CompatibilityReport>);

  NullPluginCompatibilityEngine engine;
  const auto report = engine.check(dasall::infra::plugin::PluginCompatibilityCheckRequest{
      .manifest = make_valid_manifest(),
      .host_abi = make_host_abi("x86_64-linux-gnu", "1.2.3", true, true),
      .dependency_matrix = make_dependency_matrix(),
  });

  assert_true(report.abi_ok && report.api_ok && report.dependency_ok && report.is_valid(),
              "IPluginCompatibilityEngine should accept exact platform plus forward patch compatibility under strict mode");
}

void test_plugin_compatibility_engine_interface_distinguishes_strict_and_non_strict_minor_rules() {
  using dasall::tests::support::assert_true;

  NullPluginCompatibilityEngine engine;
  const auto strict_failure = engine.check(dasall::infra::plugin::PluginCompatibilityCheckRequest{
      .manifest = make_valid_manifest("x86_64-linux-gnu@1.2.0"),
      .host_abi = make_host_abi("x86_64-linux-gnu", "1.3.0", true, true),
      .dependency_matrix = make_dependency_matrix(),
  });
  assert_true(!strict_failure.abi_ok && strict_failure.is_valid(),
              "strict mode should reject host ABI minor drift even when the patch moves forward");

  const auto relaxed_success = engine.check(dasall::infra::plugin::PluginCompatibilityCheckRequest{
      .manifest = make_valid_manifest("x86_64-linux-gnu@1.2.0"),
      .host_abi = make_host_abi("x86_64-linux-gnu", "1.3.0", false, true),
      .dependency_matrix = make_dependency_matrix(),
  });
  assert_true(relaxed_success.abi_ok && relaxed_success.is_valid(),
              "non-strict mode should allow forward-compatible host MINOR.PATCH coverage when MAJOR matches");
}

void test_plugin_compatibility_engine_interface_rejects_major_mismatch_and_missing_dependencies() {
  using dasall::tests::support::assert_true;

  NullPluginCompatibilityEngine engine;
  const auto report = engine.check(dasall::infra::plugin::PluginCompatibilityCheckRequest{
      .manifest = make_valid_manifest("x86_64-linux-gnu@1.2.0"),
      .host_abi = make_host_abi("x86_64-linux-gnu", "2.0.0", false, false),
      .dependency_matrix = make_dependency_matrix({std::string("plugin.core.runtime"),
                                                   std::string("plugin.core.storage")},
                                                  {std::string("plugin.core.runtime")}),
  });

  assert_true(!report.abi_ok && !report.api_ok && !report.dependency_ok && report.is_valid(),
              "major ABI mismatch, API handshake failure, and missing dependencies should all remain observable in the compatibility report");
}

}  // namespace

int main() {
  try {
    test_plugin_compatibility_engine_interface_accepts_strict_patch_forward_compatibility();
    test_plugin_compatibility_engine_interface_distinguishes_strict_and_non_strict_minor_rules();
    test_plugin_compatibility_engine_interface_rejects_major_mismatch_and_missing_dependencies();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << std::endl;
    return 1;
  }

  return 0;
}