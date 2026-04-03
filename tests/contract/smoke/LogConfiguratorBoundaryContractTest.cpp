#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "config/IConfigCenter.h"
#include "logging/ILogConfigurator.h"
#include "logging/LoggingConfigAdapter.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class BoundaryConfigCenter final : public dasall::infra::config::IConfigCenter {
 public:
  void set_entry(dasall::infra::config::TypedConfig entry) {
    entries_[entry.key_path] = std::move(entry);
  }

  dasall::infra::config::ConfigApplyResult load_layers(
      const dasall::infra::config::ConfigStartupContext&) override {
    return dasall::infra::config::ConfigApplyResult::success("rollback://logging-contract/1");
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
    return dasall::infra::config::ConfigApplyResult::success("rollback://logging-contract/2");
  }

  dasall::infra::config::ConfigApplyResult rollback(
      const dasall::infra::config::ConfigRollbackToken&) override {
    return dasall::infra::config::ConfigApplyResult::success("rollback://logging-contract/3");
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

void seed_boundary_entries(BoundaryConfigCenter& config_center, bool audit_required) {
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;

  config_center.set_entry(make_entry("infra.logging.level",
                                     ConfigValueType::String,
                                     "info",
                                     ConfigSourceKind::Profile,
                                     "profiles/desktop_full/runtime_policy.yaml"));
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
                                     "8192",
                                     ConfigSourceKind::Profile,
                                     "profiles/desktop_full/runtime_policy.yaml"));
  config_center.set_entry(make_entry("infra.logging.async.overflow_policy",
                                     ConfigValueType::String,
                                     "block",
                                     ConfigSourceKind::Profile,
                                     "profiles/desktop_full/runtime_policy.yaml"));
  config_center.set_entry(make_entry("infra.logging.file.path",
                                     ConfigValueType::String,
                                     "logs/runtime.log",
                                     ConfigSourceKind::Defaults,
                                     "config://fallback"));
  config_center.set_entry(make_entry("infra.logging.file.rotate.max_size_mb",
                                     ConfigValueType::UnsignedInteger,
                                     "50",
                                     ConfigSourceKind::Profile,
                                     "profiles/desktop_full/runtime_policy.yaml"));
  config_center.set_entry(make_entry("infra.logging.file.rotate.max_files",
                                     ConfigValueType::UnsignedInteger,
                                     "10",
                                     ConfigSourceKind::Profile,
                                     "profiles/desktop_full/runtime_policy.yaml"));
  config_center.set_entry(make_entry("infra.logging.redaction.enabled",
                                     ConfigValueType::Boolean,
                                     "true",
                                     ConfigSourceKind::Profile,
                                     "profiles/desktop_full/runtime_policy.yaml"));
  config_center.set_entry(make_entry("infra.logging.redaction.ruleset",
                                     ConfigValueType::String,
                                     "default_v1",
                                     ConfigSourceKind::Defaults,
                                     "config://fallback"));
  config_center.set_entry(make_entry("infra.logging.export.enable_diag_pull",
                                     ConfigValueType::Boolean,
                                     "true",
                                     ConfigSourceKind::Profile,
                                     "profiles/desktop_full/runtime_policy.yaml"));
  config_center.set_entry(make_entry("infra.audit.required",
                                     ConfigValueType::Boolean,
                                     audit_required ? "true" : "false",
                                     ConfigSourceKind::Profile,
                                     "profiles/desktop_full/runtime_policy.yaml"));
}

void test_log_configurator_boundary_keeps_contract_error_types_and_frozen_keys() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::ILogConfigurator;
  using dasall::infra::logging::LoggingConfig;
  using dasall::infra::logging::LoggingConfigApplyResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(LoggingConfigApplyResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(LoggingConfigApplyResult{}.error_info),
                               std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(LoggingConfig{}.source_entries),
                               std::vector<dasall::infra::config::TypedConfig>>);
  static_assert(std::is_same_v<decltype(&ILogConfigurator::apply),
                               LoggingConfigApplyResult (ILogConfigurator::*)(
                                   const LoggingConfig&)>);

  BoundaryConfigCenter config_center;
  seed_boundary_entries(config_center, true);

  dasall::infra::logging::LoggingConfigAdapter adapter(config_center);
  const auto result = adapter.load_and_apply();

  assert_true(result.applied,
              "log configurator boundary should accept a fully frozen logging config surface");
  for (const auto& entry : adapter.active_config().source_entries) {
    assert_true(dasall::infra::logging::is_logging_config_key(entry.key_path),
                "log configurator boundary should only expose the frozen logging/audit key set");
  }
}

void test_log_configurator_boundary_rejects_profile_attempt_to_disable_audit() {
  using dasall::infra::logging::LoggingConfigAdapter;
  using dasall::tests::support::assert_true;

  BoundaryConfigCenter config_center;
  seed_boundary_entries(config_center, false);

  LoggingConfigAdapter adapter(config_center);
  const auto result = adapter.load_and_apply();

  assert_true(!result.applied,
              "log configurator boundary should reject profile-owned config that disables audit_required");
  assert_true(result.references_only_contract_error_types(),
              "audit gate failures should remain inside contracts ResultCode/ErrorInfo types");
  assert_true(std::find(result.rejected_keys.begin(),
                        result.rejected_keys.end(),
                        "infra.audit.required") != result.rejected_keys.end(),
              "audit gate violations should report infra.audit.required as the rejected key");
}

}  // namespace

int main() {
  try {
    test_log_configurator_boundary_keeps_contract_error_types_and_frozen_keys();
    test_log_configurator_boundary_rejects_profile_attempt_to_disable_audit();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}