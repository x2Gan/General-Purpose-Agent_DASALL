#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "config/ConfigLoader.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::config::ConfigLoader;
using dasall::infra::config::ConfigLoaderOptions;
using dasall::infra::config::ConfigSourceKind;
using dasall::infra::config::ConfigValueType;
using dasall::infra::config::TypedConfig;

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__)
      .parent_path()
      .parent_path()
      .parent_path()
      .parent_path()
      .parent_path();
}

[[nodiscard]] const TypedConfig* find_entry(
    const dasall::infra::config::ConfigLayerDocument& document,
    std::string_view key_path) {
  const auto entry = std::find_if(document.entries.begin(),
                                  document.entries.end(),
                                  [&](const TypedConfig& candidate) {
                                    return candidate.key_path == key_path;
                                  });
  if (entry == document.entries.end()) {
    return nullptr;
  }

  return &(*entry);
}

void assert_entry_equals(const dasall::infra::config::ConfigLayerDocument& document,
                         std::string_view key_path,
                         ConfigValueType expected_type,
                         std::string_view expected_value,
                         std::string_view expected_source_id,
                         std::string_view message) {
  using dasall::tests::support::assert_true;

  const TypedConfig* entry = find_entry(document, key_path);
  assert_true(entry != nullptr,
              std::string(message) + " missing key_path=" + std::string(key_path));
  assert_true(entry->value_type == expected_type,
              std::string(message) + " should keep expected typed config kind");
  assert_true(entry->serialized_value == expected_value,
              std::string(message) + " should keep expected serialized value");
  assert_true(entry->source_kind == ConfigSourceKind::Profile &&
                  entry->source_id == expected_source_id,
              std::string(message) + " should keep profile-scoped provenance");
}

void assert_enabled_audit_and_overflow_baseline(
    const dasall::infra::config::ConfigLayerDocument& document,
    std::string_view message_prefix) {
  using dasall::tests::support::assert_true;

  const auto* enabled = find_entry(document, "infra.watchdog.enabled");
  const auto* overflow_policy =
      find_entry(document, "infra.watchdog.event.overflow_policy");
  const auto* audit_required =
      find_entry(document, "infra.watchdog.audit.required");
  const auto* scan_interval =
      find_entry(document, "infra.watchdog.scan.interval_ms");
  const auto* safe_mode_interval =
      find_entry(document, "infra.watchdog.safe_mode.scan_interval_ms");

  assert_true(enabled != nullptr && enabled->value_type == ConfigValueType::Boolean &&
                  enabled->serialized_value == "true",
              std::string(message_prefix) + " should keep watchdog enabled in the profile baseline");
  assert_true(overflow_policy != nullptr &&
                  overflow_policy->value_type == ConfigValueType::String &&
                  overflow_policy->serialized_value == "block",
              std::string(message_prefix) + " should keep the watchdog event overflow policy frozen at block");
  assert_true(audit_required != nullptr &&
                  audit_required->value_type == ConfigValueType::Boolean &&
                  audit_required->serialized_value == "true",
              std::string(message_prefix) + " should keep audit.required enabled across the watchdog profile matrix");
  assert_true(scan_interval != nullptr && safe_mode_interval != nullptr &&
                  std::stoul(safe_mode_interval->serialized_value) >=
                      std::stoul(scan_interval->serialized_value),
              std::string(message_prefix) +
                  " should keep safe_mode.scan_interval_ms greater than or equal to scan.interval_ms");
}

void test_watchdog_profile_compatibility_keeps_timeout_matrix_frozen_across_profiles() {
  using dasall::tests::support::assert_true;

  ConfigLoader loader(ConfigLoaderOptions{
      .repository_root = repository_root(),
      .runtime_overlay_source_ref = std::string(),
  });

  const auto desktop = loader.load_profile("desktop_full");
  const auto cloud = loader.load_profile("cloud_full");
  const auto balanced = loader.load_profile("edge_balanced");
  const auto minimal = loader.load_profile("edge_minimal");
  const auto factory = loader.load_profile("factory_test");

  assert_true(desktop.loaded && cloud.loaded && balanced.loaded && minimal.loaded &&
                  factory.loaded,
              "WatchdogProfileCompatibilityTest should load all five frozen profile runtime policies before comparing watchdog settings");
  assert_true(desktop.document.is_valid() && cloud.document.is_valid() &&
                  balanced.document.is_valid() && minimal.document.is_valid() &&
                  factory.document.is_valid(),
              "WatchdogProfileCompatibilityTest should keep all loaded watchdog profile documents structurally valid");

  assert_entry_equals(desktop.document,
                      "infra.watchdog.scan.interval_ms",
                      ConfigValueType::UnsignedInteger,
                      "500",
                      "profiles/desktop_full/runtime_policy.yaml",
                      "desktop_full watchdog scan interval");
  assert_entry_equals(desktop.document,
                      "infra.watchdog.timeout_ms",
                      ConfigValueType::UnsignedInteger,
                      "15000",
                      "profiles/desktop_full/runtime_policy.yaml",
                      "desktop_full watchdog timeout");
  assert_entry_equals(desktop.document,
                      "infra.watchdog.timeout.level.policy",
                      ConfigValueType::String,
                      "warn_then_critical",
                      "profiles/desktop_full/runtime_policy.yaml",
                      "desktop_full watchdog timeout policy");

  assert_entry_equals(cloud.document,
                      "infra.watchdog.scan.interval_ms",
                      ConfigValueType::UnsignedInteger,
                      "400",
                      "profiles/cloud_full/runtime_policy.yaml",
                      "cloud_full watchdog scan interval");
  assert_entry_equals(cloud.document,
                      "infra.watchdog.timeout_ms",
                      ConfigValueType::UnsignedInteger,
                      "12000",
                      "profiles/cloud_full/runtime_policy.yaml",
                      "cloud_full watchdog timeout");
  assert_entry_equals(cloud.document,
                      "infra.watchdog.timeout.level.policy",
                      ConfigValueType::String,
                      "warn_then_critical",
                      "profiles/cloud_full/runtime_policy.yaml",
                      "cloud_full watchdog timeout policy");

  assert_entry_equals(balanced.document,
                      "infra.watchdog.scan.interval_ms",
                      ConfigValueType::UnsignedInteger,
                      "800",
                      "profiles/edge_balanced/runtime_policy.yaml",
                      "edge_balanced watchdog scan interval");
  assert_entry_equals(balanced.document,
                      "infra.watchdog.timeout_ms",
                      ConfigValueType::UnsignedInteger,
                      "18000",
                      "profiles/edge_balanced/runtime_policy.yaml",
                      "edge_balanced watchdog timeout");
  assert_entry_equals(balanced.document,
                      "infra.watchdog.timeout.level.policy",
                      ConfigValueType::String,
                      "warn_then_critical",
                      "profiles/edge_balanced/runtime_policy.yaml",
                      "edge_balanced watchdog timeout policy");

  assert_entry_equals(minimal.document,
                      "infra.watchdog.scan.interval_ms",
                      ConfigValueType::UnsignedInteger,
                      "1500",
                      "profiles/edge_minimal/runtime_policy.yaml",
                      "edge_minimal watchdog scan interval");
  assert_entry_equals(minimal.document,
                      "infra.watchdog.timeout_ms",
                      ConfigValueType::UnsignedInteger,
                      "20000",
                      "profiles/edge_minimal/runtime_policy.yaml",
                      "edge_minimal watchdog timeout");
  assert_entry_equals(minimal.document,
                      "infra.watchdog.timeout.level.policy",
                      ConfigValueType::String,
                      "critical_only",
                      "profiles/edge_minimal/runtime_policy.yaml",
                      "edge_minimal watchdog timeout policy");
  assert_entry_equals(minimal.document,
                      "infra.watchdog.recovery_hint.enabled",
                      ConfigValueType::Boolean,
                      "false",
                      "profiles/edge_minimal/runtime_policy.yaml",
                      "edge_minimal watchdog recovery hint switch");

  assert_entry_equals(factory.document,
                      "infra.watchdog.scan.interval_ms",
                      ConfigValueType::UnsignedInteger,
                      "700",
                      "profiles/factory_test/runtime_policy.yaml",
                      "factory_test watchdog scan interval");
  assert_entry_equals(factory.document,
                      "infra.watchdog.timeout_ms",
                      ConfigValueType::UnsignedInteger,
                      "10000",
                      "profiles/factory_test/runtime_policy.yaml",
                      "factory_test watchdog timeout");
  assert_entry_equals(factory.document,
                      "infra.watchdog.consecutive_miss_threshold",
                      ConfigValueType::UnsignedInteger,
                      "2",
                      "profiles/factory_test/runtime_policy.yaml",
                      "factory_test watchdog miss threshold");
  assert_entry_equals(factory.document,
                      "infra.watchdog.timeout.level.policy",
                      ConfigValueType::String,
                      "critical_only",
                      "profiles/factory_test/runtime_policy.yaml",
                      "factory_test watchdog timeout policy");
}

void test_watchdog_profile_compatibility_keeps_capacity_and_safe_mode_baselines_consistent() {
  ConfigLoader loader(ConfigLoaderOptions{
      .repository_root = repository_root(),
      .runtime_overlay_source_ref = std::string(),
  });

  const auto desktop = loader.load_profile("desktop_full");
  const auto cloud = loader.load_profile("cloud_full");
  const auto balanced = loader.load_profile("edge_balanced");
  const auto minimal = loader.load_profile("edge_minimal");
  const auto factory = loader.load_profile("factory_test");

  assert_entry_equals(desktop.document,
                      "infra.watchdog.event.queue_size",
                      ConfigValueType::UnsignedInteger,
                      "2048",
                      "profiles/desktop_full/runtime_policy.yaml",
                      "desktop_full watchdog queue size");
  assert_entry_equals(desktop.document,
                      "infra.watchdog.max_entities",
                      ConfigValueType::UnsignedInteger,
                      "1024",
                      "profiles/desktop_full/runtime_policy.yaml",
                      "desktop_full watchdog max_entities");
  assert_entry_equals(desktop.document,
                      "infra.watchdog.safe_mode.scan_interval_ms",
                      ConfigValueType::UnsignedInteger,
                      "2000",
                      "profiles/desktop_full/runtime_policy.yaml",
                      "desktop_full watchdog safe mode interval");

  assert_entry_equals(cloud.document,
                      "infra.watchdog.event.queue_size",
                      ConfigValueType::UnsignedInteger,
                      "4096",
                      "profiles/cloud_full/runtime_policy.yaml",
                      "cloud_full watchdog queue size");
  assert_entry_equals(cloud.document,
                      "infra.watchdog.max_entities",
                      ConfigValueType::UnsignedInteger,
                      "2048",
                      "profiles/cloud_full/runtime_policy.yaml",
                      "cloud_full watchdog max_entities");
  assert_entry_equals(cloud.document,
                      "infra.watchdog.safe_mode.scan_interval_ms",
                      ConfigValueType::UnsignedInteger,
                      "2000",
                      "profiles/cloud_full/runtime_policy.yaml",
                      "cloud_full watchdog safe mode interval");

  assert_entry_equals(balanced.document,
                      "infra.watchdog.event.queue_size",
                      ConfigValueType::UnsignedInteger,
                      "1024",
                      "profiles/edge_balanced/runtime_policy.yaml",
                      "edge_balanced watchdog queue size");
  assert_entry_equals(balanced.document,
                      "infra.watchdog.max_entities",
                      ConfigValueType::UnsignedInteger,
                      "512",
                      "profiles/edge_balanced/runtime_policy.yaml",
                      "edge_balanced watchdog max_entities");
  assert_entry_equals(balanced.document,
                      "infra.watchdog.safe_mode.scan_interval_ms",
                      ConfigValueType::UnsignedInteger,
                      "3000",
                      "profiles/edge_balanced/runtime_policy.yaml",
                      "edge_balanced watchdog safe mode interval");

  assert_entry_equals(minimal.document,
                      "infra.watchdog.event.queue_size",
                      ConfigValueType::UnsignedInteger,
                      "256",
                      "profiles/edge_minimal/runtime_policy.yaml",
                      "edge_minimal watchdog queue size");
  assert_entry_equals(minimal.document,
                      "infra.watchdog.max_entities",
                      ConfigValueType::UnsignedInteger,
                      "128",
                      "profiles/edge_minimal/runtime_policy.yaml",
                      "edge_minimal watchdog max_entities");
  assert_entry_equals(minimal.document,
                      "infra.watchdog.safe_mode.scan_interval_ms",
                      ConfigValueType::UnsignedInteger,
                      "4000",
                      "profiles/edge_minimal/runtime_policy.yaml",
                      "edge_minimal watchdog safe mode interval");

  assert_entry_equals(factory.document,
                      "infra.watchdog.event.queue_size",
                      ConfigValueType::UnsignedInteger,
                      "512",
                      "profiles/factory_test/runtime_policy.yaml",
                      "factory_test watchdog queue size");
  assert_entry_equals(factory.document,
                      "infra.watchdog.max_entities",
                      ConfigValueType::UnsignedInteger,
                      "256",
                      "profiles/factory_test/runtime_policy.yaml",
                      "factory_test watchdog max_entities");
  assert_entry_equals(factory.document,
                      "infra.watchdog.safe_mode.scan_interval_ms",
                      ConfigValueType::UnsignedInteger,
                      "2500",
                      "profiles/factory_test/runtime_policy.yaml",
                      "factory_test watchdog safe mode interval");

  assert_enabled_audit_and_overflow_baseline(desktop.document, "desktop_full watchdog profile baseline");
  assert_enabled_audit_and_overflow_baseline(cloud.document, "cloud_full watchdog profile baseline");
  assert_enabled_audit_and_overflow_baseline(balanced.document, "edge_balanced watchdog profile baseline");
  assert_enabled_audit_and_overflow_baseline(minimal.document, "edge_minimal watchdog profile baseline");
  assert_enabled_audit_and_overflow_baseline(factory.document, "factory_test watchdog profile baseline");
}

}  // namespace

int main() {
  try {
    test_watchdog_profile_compatibility_keeps_timeout_matrix_frozen_across_profiles();
    test_watchdog_profile_compatibility_keeps_capacity_and_safe_mode_baselines_consistent();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}