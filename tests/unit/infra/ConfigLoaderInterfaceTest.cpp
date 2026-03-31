#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "config/IConfigLoader.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::config::ConfigLayerDocument make_layer_document(
    dasall::infra::config::ConfigSourceKind source_kind,
    dasall::infra::config::ConfigDocumentFormat document_format,
    std::string source_id,
    std::string version_ref,
    std::string key_path,
    std::string serialized_value) {
  return dasall::infra::config::ConfigLayerDocument{
      .layer_ref = dasall::infra::config::ConfigLayerRef{
          .source_kind = source_kind,
          .document_format = document_format,
          .source_id = source_id,
          .version_ref = std::move(version_ref),
          .schema_version = std::string("1"),
      },
      .entries = {dasall::infra::config::TypedConfig{
          .key_path = std::move(key_path),
          .value_type = dasall::infra::config::ConfigValueType::Boolean,
          .serialized_value = std::move(serialized_value),
          .schema_version = std::string("1"),
          .source_kind = source_kind,
          .source_id = std::move(source_id),
          .secret_backed = false,
      }},
  };
}

class NullConfigLoader final : public dasall::infra::config::IConfigLoader {
 public:
  explicit NullConfigLoader(std::string runtime_overlay_source_ref)
      : runtime_overlay_source_ref_(std::move(runtime_overlay_source_ref)) {}

  dasall::infra::config::ConfigLoadResult load_default() override {
    return dasall::infra::config::ConfigLoadResult::success(make_layer_document(
        dasall::infra::config::ConfigSourceKind::Defaults,
        dasall::infra::config::ConfigDocumentFormat::RuntimePolicyYamlV1,
        std::string("infra/config/defaults/runtime_policy.yaml"),
        std::string("defaults@1"),
        std::string("infra.config.validation.strict"),
        std::string("true")));
  }

  dasall::infra::config::ConfigLoadResult load_profile(std::string_view profile_id) override {
    if (!dasall::infra::config::is_supported_profile_id(profile_id)) {
      return dasall::infra::config::ConfigLoadResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "profile loader only accepts the five frozen profile identifiers",
          "config.load_profile",
          "NullConfigLoader");
    }

    std::string profile_path = std::string("profiles/") + std::string(profile_id) +
                               std::string("/runtime_policy.yaml");
    return dasall::infra::config::ConfigLoadResult::success(make_layer_document(
        dasall::infra::config::ConfigSourceKind::Profile,
        dasall::infra::config::ConfigDocumentFormat::RuntimePolicyYamlV1,
        profile_path,
        std::string(profile_id) + std::string("@1"),
        std::string("infra.config.validation.strict"),
        std::string("true")));
  }

  dasall::infra::config::ConfigLoadResult load_deploy(std::string_view source_ref) override {
    if (source_ref.empty()) {
      return dasall::infra::config::ConfigLoadResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "deployment loader requires an explicit managed source_ref",
          "config.load_deploy",
          "NullConfigLoader");
    }

    return dasall::infra::config::ConfigLoadResult::success(make_layer_document(
        dasall::infra::config::ConfigSourceKind::DeploymentOverride,
        dasall::infra::config::ConfigDocumentFormat::DeploymentOverlayYamlV1,
        std::string(source_ref),
        std::string("deploy@42"),
        std::string("infra.config.validation.strict"),
        std::string("false")));
  }

  dasall::infra::config::ConfigLoadResult load_runtime_overlay() override {
    if (runtime_overlay_source_ref_.empty()) {
      return dasall::infra::config::ConfigLoadResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "runtime overlay loader requires an explicit managed overlay source",
          "config.load_runtime_overlay",
          "NullConfigLoader");
    }

    return dasall::infra::config::ConfigLoadResult::success(make_layer_document(
        dasall::infra::config::ConfigSourceKind::RuntimeOverride,
        dasall::infra::config::ConfigDocumentFormat::RuntimeOverridePatchV1,
        runtime_overlay_source_ref_,
        std::string("runtime-overlay@1"),
        std::string("infra.config.validation.strict"),
        std::string("false")));
  }

 private:
  std::string runtime_overlay_source_ref_;
};

void test_config_loader_interface_exposes_four_layer_entrypoints() {
  using dasall::infra::config::ConfigDocumentFormat;
  using dasall::infra::config::ConfigLoadResult;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::IConfigLoader;
  using dasall::tests::support::assert_true;

  static_assert(
      std::is_same_v<decltype(std::declval<IConfigLoader&>().load_default()), ConfigLoadResult>);
  static_assert(std::is_same_v<decltype(std::declval<IConfigLoader&>().load_profile(
                                   std::declval<std::string_view>())),
                               ConfigLoadResult>);
  static_assert(std::is_same_v<decltype(std::declval<IConfigLoader&>().load_deploy(
                                   std::declval<std::string_view>())),
                               ConfigLoadResult>);
  static_assert(std::is_same_v<decltype(std::declval<IConfigLoader&>().load_runtime_overlay()),
                               ConfigLoadResult>);

  NullConfigLoader loader("ops://window/bootstrap");

  const auto defaults = loader.load_default();
  assert_true(defaults.loaded && defaults.document.is_valid(),
              "IConfigLoader should expose a valid defaults layer document placeholder");
  assert_true(defaults.document.layer_ref.source_kind == ConfigSourceKind::Defaults,
              "defaults loader should freeze defaults as the first source kind");

  const auto profile = loader.load_profile("desktop_full");
  assert_true(profile.loaded && profile.document.is_valid(),
              "IConfigLoader should accept frozen profile identifiers for the profile layer");
  assert_true(profile.document.layer_ref.document_format == ConfigDocumentFormat::RuntimePolicyYamlV1,
              "profile loader should keep runtime_policy.yaml as the frozen profile document format");

  const auto deploy = loader.load_deploy("deploy://site-001/config.yaml");
  assert_true(deploy.loaded && deploy.document.is_valid(),
              "IConfigLoader should accept a managed deployment source reference");
  assert_true(deploy.document.layer_ref.source_kind == ConfigSourceKind::DeploymentOverride,
              "deployment loader should keep deployment_override as the managed source kind");

  const auto runtime_overlay = loader.load_runtime_overlay();
  assert_true(runtime_overlay.loaded && runtime_overlay.document.is_valid(),
              "IConfigLoader should expose a valid runtime overlay entrypoint for the fourth layer");
  assert_true(runtime_overlay.document.layer_ref.document_format ==
                  ConfigDocumentFormat::RuntimeOverridePatchV1,
              "runtime overlay loader should freeze structured patch v1 as its document format");
}

void test_config_loader_interface_rejects_unfrozen_profile_aliases_and_missing_source_refs() {
  using dasall::tests::support::assert_true;

  NullConfigLoader loader_without_overlay("");

  const auto invalid_profile = loader_without_overlay.load_profile("staging");
  assert_true(!invalid_profile.loaded,
              "IConfigLoader should reject profile aliases outside the five frozen profile identifiers");
  assert_true(invalid_profile.references_only_contract_error_types(),
              "profile loader failures should remain inside contracts ResultCode/ErrorInfo types");

  const auto invalid_deploy = loader_without_overlay.load_deploy("");
  assert_true(!invalid_deploy.loaded,
              "IConfigLoader should reject deployment loads without an explicit managed source reference");
  assert_true(invalid_deploy.references_only_contract_error_types(),
              "deployment loader failures should remain inside contracts ResultCode/ErrorInfo types");

  const auto missing_runtime_overlay = loader_without_overlay.load_runtime_overlay();
  assert_true(!missing_runtime_overlay.loaded,
              "IConfigLoader should reject runtime overlay loads without an explicit managed overlay source");
  assert_true(missing_runtime_overlay.references_only_contract_error_types(),
              "runtime overlay failures should remain inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_config_loader_interface_exposes_four_layer_entrypoints();
    test_config_loader_interface_rejects_unfrozen_profile_aliases_and_missing_source_refs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}