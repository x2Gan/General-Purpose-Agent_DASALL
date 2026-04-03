#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "config/IConfigCenter.h"
#include "logging/ILogConfigurator.h"
#include "logging/LoggingConfigAdapter.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class StubConfigCenter final : public dasall::infra::config::IConfigCenter {
 public:
  void set_entry(dasall::infra::config::TypedConfig entry) {
    entries_[entry.key_path] = std::move(entry);
  }

  dasall::infra::config::ConfigApplyResult load_layers(
      const dasall::infra::config::ConfigStartupContext&) override {
    return dasall::infra::config::ConfigApplyResult::success("rollback://logging-config/1");
  }

  std::optional<dasall::infra::config::TypedConfig> get_typed(
      const dasall::infra::config::ConfigQuery& query) const override {
    if (!query.is_valid()) {
      return std::nullopt;
    }

    const auto entry = entries_.find(query.key_path);
    if (entry != entries_.end()) {
      return entry->second;
    }

    if (query.default_policy != dasall::infra::config::ConfigDefaultPolicy::ReturnFallback) {
      return std::nullopt;
    }

    return dasall::infra::config::TypedConfig{
        .key_path = query.key_path,
        .value_type = query.expected_type,
        .serialized_value = query.fallback_serialized_value,
        .schema_version = std::string(dasall::infra::config::kConfigSchemaVersionV1),
        .source_kind = dasall::infra::config::ConfigSourceKind::Defaults,
        .source_id = std::string("config://fallback"),
        .secret_backed = false,
    };
  }

  dasall::infra::config::ConfigApplyResult apply_override(
      const dasall::infra::config::ConfigPatch&) override {
    return dasall::infra::config::ConfigApplyResult::success("rollback://logging-config/2");
  }

  dasall::infra::config::ConfigApplyResult rollback(
      const dasall::infra::config::ConfigRollbackToken&) override {
    return dasall::infra::config::ConfigApplyResult::success("rollback://logging-config/3");
  }

  std::optional<dasall::infra::config::ConfigSubscriptionHandle> subscribe(
      const dasall::infra::config::ConfigSubscriptionRequest&) override {
    return std::nullopt;
  }

 private:
  std::map<std::string, dasall::infra::config::TypedConfig> entries_;
};

[[nodiscard]] dasall::infra::config::TypedConfig make_entry(
    std::string key_path,
    dasall::infra::config::ConfigValueType value_type,
    std::string serialized_value,
    dasall::infra::config::ConfigSourceKind source_kind,
    std::string source_id) {
  return dasall::infra::config::TypedConfig{
      .key_path = std::move(key_path),
      .value_type = value_type,
      .serialized_value = std::move(serialized_value),
      .schema_version = std::string(dasall::infra::config::kConfigSchemaVersionV1),
      .source_kind = source_kind,
      .source_id = std::move(source_id),
      .secret_backed = false,
  };
}

void seed_valid_logging_entries(StubConfigCenter& config_center) {
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;

  config_center.set_entry(make_entry("infra.logging.level",
                                     ConfigValueType::String,
                                     "debug",
                                     ConfigSourceKind::RuntimeOverride,
                                     "ops://incident/log-level"));
  config_center.set_entry(make_entry("infra.logging.format",
                                     ConfigValueType::String,
                                     "json_line",
                                     ConfigSourceKind::Profile,
                                     "profiles/desktop_full/runtime_policy.yaml"));
  config_center.set_entry(make_entry("infra.logging.async.enabled",
                                     ConfigValueType::Boolean,
                                     "true",
                                     ConfigSourceKind::Profile,
                                     "profiles/desktop_full/runtime_policy.yaml"));
  config_center.set_entry(make_entry("infra.logging.async.queue_size",
                                     ConfigValueType::UnsignedInteger,
                                     "16384",
                                     ConfigSourceKind::DeploymentOverride,
                                     "deploy://site/logging.yaml"));
  config_center.set_entry(make_entry("infra.logging.async.overflow_policy",
                                     ConfigValueType::String,
                                     "overrun_oldest",
                                     ConfigSourceKind::DeploymentOverride,
                                     "deploy://site/logging.yaml"));
  config_center.set_entry(make_entry("infra.logging.file.path",
                                     ConfigValueType::String,
                                     "logs/incident-runtime.log",
                                     ConfigSourceKind::RuntimeOverride,
                                     "ops://incident/log-path"));
  config_center.set_entry(make_entry("infra.logging.file.rotate.max_size_mb",
                                     ConfigValueType::UnsignedInteger,
                                     "128",
                                     ConfigSourceKind::DeploymentOverride,
                                     "deploy://site/logging.yaml"));
  config_center.set_entry(make_entry("infra.logging.file.rotate.max_files",
                                     ConfigValueType::UnsignedInteger,
                                     "20",
                                     ConfigSourceKind::DeploymentOverride,
                                     "deploy://site/logging.yaml"));
  config_center.set_entry(make_entry("infra.logging.redaction.enabled",
                                     ConfigValueType::Boolean,
                                     "true",
                                     ConfigSourceKind::Profile,
                                     "profiles/desktop_full/runtime_policy.yaml"));
  config_center.set_entry(make_entry("infra.logging.redaction.ruleset",
                                     ConfigValueType::String,
                                     "incident_v2",
                                     ConfigSourceKind::RuntimeOverride,
                                     "ops://incident/redaction"));
  config_center.set_entry(make_entry("infra.logging.export.enable_diag_pull",
                                     ConfigValueType::Boolean,
                                     "false",
                                     ConfigSourceKind::DeploymentOverride,
                                     "deploy://site/logging.yaml"));
  config_center.set_entry(make_entry("infra.audit.required",
                                     ConfigValueType::Boolean,
                                     "true",
                                     ConfigSourceKind::Profile,
                                     "profiles/desktop_full/runtime_policy.yaml"));
}

void test_log_configurator_surface_and_successful_merge_path() {
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::logging::ILogConfigurator;
  using dasall::infra::logging::LoggingConfig;
  using dasall::infra::logging::LoggingConfigAdapter;
  using dasall::infra::logging::LoggingConfigApplyResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&ILogConfigurator::apply),
                               LoggingConfigApplyResult (ILogConfigurator::*)(
                                   const LoggingConfig&)>);

  StubConfigCenter config_center;
  seed_valid_logging_entries(config_center);

  LoggingConfigAdapter adapter(config_center);
  const auto result = adapter.load_and_apply();

  assert_true(result.applied,
              "logging config adapter should accept a frozen active config assembled from default/profile/deployment/runtime layers");
  assert_true(result.runtime_override_active,
              "runtime override should be reported when any active logging key comes from the runtime layer");
  assert_true(adapter.has_active_config(),
              "logging config adapter should retain the active config after a successful apply");

  const auto& active_config = adapter.active_config();
  assert_true(active_config.has_consistent_values(),
              "active logging config should remain internally consistent after load_and_apply");
  assert_equal(static_cast<int>(dasall::infra::LogLevel::Debug),
               static_cast<int>(active_config.level),
               "runtime override should win for infra.logging.level");
  assert_equal(std::string("logs/incident-runtime.log"),
               active_config.file_path,
               "runtime override should win for infra.logging.file.path");
  assert_equal(16384,
               static_cast<int>(active_config.queue_size),
               "deployment override should remain effective for queue size when runtime override does not replace it");
  assert_equal(20,
               static_cast<int>(active_config.rotate_max_files),
               "deployment override should supply file rotation counts");
  assert_equal(std::string("incident_v2"),
               active_config.redaction_ruleset,
               "runtime override should update the redaction ruleset on a frozen tunable key");
  assert_true(!active_config.enable_diag_pull,
              "deployment override should be able to lower diagnostic pull exposure when runtime does not replace it");
  assert_true(active_config.find_source_entry("infra.logging.level") != nullptr &&
                  active_config.find_source_entry("infra.logging.level")->source_kind ==
                      ConfigSourceKind::RuntimeOverride,
              "active config should preserve the source kind for infra.logging.level");
  assert_true(active_config.find_source_entry("infra.logging.async.queue_size") != nullptr &&
                  active_config.find_source_entry("infra.logging.async.queue_size")->source_kind ==
                      ConfigSourceKind::DeploymentOverride,
              "active config should preserve the source kind for deployment-owned queue size");
}

void test_logging_config_adapter_rejects_runtime_override_on_non_tunable_key() {
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::logging::LoggingConfigAdapter;
  using dasall::tests::support::assert_true;

  StubConfigCenter config_center;
  seed_valid_logging_entries(config_center);
  config_center.set_entry(make_entry("infra.logging.async.queue_size",
                                     ConfigValueType::UnsignedInteger,
                                     "4096",
                                     ConfigSourceKind::RuntimeOverride,
                                     "ops://incident/queue-size"));

  LoggingConfigAdapter adapter(config_center);
  const auto result = adapter.load_and_apply();

  assert_true(!result.applied,
              "logging config adapter should reject runtime overrides on non-runtime-tunable queue_size");
  assert_true(result.references_only_contract_error_types(),
              "logging config apply failures should remain inside contracts ResultCode/ErrorInfo types");
  assert_true(std::find(result.rejected_keys.begin(),
                        result.rejected_keys.end(),
                        "infra.logging.async.queue_size") != result.rejected_keys.end(),
              "rejected runtime queue_size override should be reported for auditability");
}

}  // namespace

int main() {
  try {
    test_log_configurator_surface_and_successful_merge_path();
    test_logging_config_adapter_rejects_runtime_override_on_non_tunable_key();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}