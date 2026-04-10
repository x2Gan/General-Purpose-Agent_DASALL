#include "PolicyLoader.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dasall::infra::policy {
namespace {

constexpr std::string_view kPolicySchemaVersion = "1";
constexpr std::string_view kFallbackSourceId = "infra/policy/defaults/frozen";
constexpr std::string_view kFallbackVersionRef = "defaults@resolved";
constexpr std::string_view kUnknownProfileId = "unknown";

struct ResolvedConfigValue {
  std::string key_path;
  std::string serialized_value;
  config::ConfigValueType value_type = config::ConfigValueType::Unspecified;
  config::ConfigSourceKind source_kind = config::ConfigSourceKind::Defaults;
  std::string source_id = std::string(kFallbackSourceId);
};

struct PolicyLoaderInputs {
  std::string profile_id = std::string(kUnknownProfileId);
  bool enabled = true;
  std::string mode_name = "strict";
  bool hot_reload = true;
  std::uint32_t max_history_snapshots = 16;
  std::string default_effect_name = "deny";
  std::string priority_order = "deny-first";
  bool require_checksum = true;
  bool dry_run_required = true;
  std::uint32_t safe_mode_threshold = 3;
  bool persist_lkg = true;
  std::vector<ResolvedConfigValue> resolved_values;
};

[[nodiscard]] std::string bool_to_string(bool value) {
  return value ? "true" : "false";
}

[[nodiscard]] std::string normalize_mode_name(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  if (value == "compat" || value == "compatibility") {
    return "compat";
  }

  return "strict";
}

[[nodiscard]] std::string normalize_default_effect(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  if (value == "allow" || value == "require_confirmation" || value == "observe") {
    return value;
  }

  return "deny";
}

[[nodiscard]] std::string normalize_priority_order(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  if (value == "explicit-priority") {
    return "explicit-priority";
  }

  return "deny-first";
}

[[nodiscard]] PolicyMode parse_mode(const std::string_view& mode_name) {
  return mode_name == "compat" ? PolicyMode::Compatibility : PolicyMode::Enforced;
}

[[nodiscard]] PolicyEffect parse_effect(const std::string_view& effect_name) {
  if (effect_name == "allow") {
    return PolicyEffect::Allow;
  }

  if (effect_name == "require_confirmation") {
    return PolicyEffect::RequireConfirmation;
  }

  if (effect_name == "observe") {
    return PolicyEffect::Observe;
  }

  return PolicyEffect::Deny;
}

[[nodiscard]] std::uint32_t base_priority(const std::string_view& priority_order) {
  return priority_order == "explicit-priority" ? 100U : 1U;
}

[[nodiscard]] std::string source_kind_version_ref(config::ConfigSourceKind source_kind) {
  switch (source_kind) {
    case config::ConfigSourceKind::Defaults:
      return std::string(kFallbackVersionRef);
    case config::ConfigSourceKind::Profile:
      return "profile@resolved";
    case config::ConfigSourceKind::DeploymentOverride:
      return "deployment@resolved";
    case config::ConfigSourceKind::RuntimeOverride:
      return "runtime-override@resolved";
    case config::ConfigSourceKind::Unspecified:
      break;
  }

  return std::string(kFallbackVersionRef);
}

[[nodiscard]] std::optional<config::TypedConfig> lookup_typed_config(
    const config::IConfigCenter& config_center,
    std::initializer_list<std::string_view> key_paths,
    config::ConfigValueType expected_type) {
  for (const auto key_path : key_paths) {
    const auto value = config_center.get_typed(config::ConfigQuery{
        .key_path = std::string(key_path),
        .expected_type = expected_type,
        .default_policy = config::ConfigDefaultPolicy::FailIfMissing,
        .fallback_serialized_value = std::string(),
    });
    if (value.has_value()) {
      return value;
    }
  }

  return std::nullopt;
}

[[nodiscard]] ResolvedConfigValue make_fallback_value(std::string key_path,
                                                      config::ConfigValueType value_type,
                                                      std::string serialized_value) {
  return ResolvedConfigValue{
      .key_path = std::move(key_path),
      .serialized_value = std::move(serialized_value),
      .value_type = value_type,
      .source_kind = config::ConfigSourceKind::Defaults,
      .source_id = std::string(kFallbackSourceId),
  };
}

[[nodiscard]] ResolvedConfigValue resolve_bool(const config::IConfigCenter& config_center,
                                               std::initializer_list<std::string_view> key_paths,
                                               bool default_value) {
  const auto resolved = lookup_typed_config(config_center, key_paths, config::ConfigValueType::Boolean);
  if (!resolved.has_value()) {
    return make_fallback_value(std::string(*key_paths.begin()),
                               config::ConfigValueType::Boolean,
                               bool_to_string(default_value));
  }

  if (resolved->serialized_value == "true" || resolved->serialized_value == "false") {
    return ResolvedConfigValue{
        .key_path = resolved->key_path,
        .serialized_value = resolved->serialized_value,
        .value_type = resolved->value_type,
        .source_kind = resolved->source_kind,
        .source_id = resolved->source_id,
    };
  }

  return make_fallback_value(resolved->key_path,
                             config::ConfigValueType::Boolean,
                             bool_to_string(default_value));
}

[[nodiscard]] ResolvedConfigValue resolve_string(const config::IConfigCenter& config_center,
                                                 std::initializer_list<std::string_view> key_paths,
                                                 std::string default_value) {
  const auto resolved = lookup_typed_config(config_center, key_paths, config::ConfigValueType::String);
  if (!resolved.has_value() || resolved->serialized_value.empty()) {
    return make_fallback_value(std::string(*key_paths.begin()),
                               config::ConfigValueType::String,
                               std::move(default_value));
  }

  return ResolvedConfigValue{
      .key_path = resolved->key_path,
      .serialized_value = resolved->serialized_value,
      .value_type = resolved->value_type,
      .source_kind = resolved->source_kind,
      .source_id = resolved->source_id,
  };
}

[[nodiscard]] bool is_unsigned_integer(std::string_view value) {
  return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
           return std::isdigit(ch) != 0;
         });
}

[[nodiscard]] ResolvedConfigValue resolve_uint(const config::IConfigCenter& config_center,
                                               std::initializer_list<std::string_view> key_paths,
                                               std::uint32_t default_value) {
  const auto resolved = lookup_typed_config(config_center,
                                            key_paths,
                                            config::ConfigValueType::UnsignedInteger);
  if (!resolved.has_value() || !is_unsigned_integer(resolved->serialized_value)) {
    return make_fallback_value(std::string(*key_paths.begin()),
                               config::ConfigValueType::UnsignedInteger,
                               std::to_string(default_value));
  }

  return ResolvedConfigValue{
      .key_path = resolved->key_path,
      .serialized_value = resolved->serialized_value,
      .value_type = resolved->value_type,
      .source_kind = resolved->source_kind,
      .source_id = resolved->source_id,
  };
}

[[nodiscard]] std::string make_generated_at() {
  const std::time_t now = std::time(nullptr);
  std::tm timestamp{};
  gmtime_r(&now, &timestamp);

  std::ostringstream stream;
  stream << std::put_time(&timestamp, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

[[nodiscard]] std::string build_source_trace(const PolicyLoaderInputs& inputs) {
  std::vector<std::string> source_entries;
  source_entries.reserve(inputs.resolved_values.size());

  for (const auto& value : inputs.resolved_values) {
    std::ostringstream entry;
    entry << "source_id=" << value.source_id
          << ",version=" << source_kind_version_ref(value.source_kind);
    const std::string candidate = entry.str();
    if (std::find(source_entries.begin(), source_entries.end(), candidate) == source_entries.end()) {
      source_entries.push_back(candidate);
    }
  }

  std::ostringstream trace;
  trace << "profile_id=" << inputs.profile_id
        << ";mode=" << inputs.mode_name
        << ";schema_version=" << kPolicySchemaVersion
        << ";sources=";

  for (std::size_t index = 0; index < source_entries.size(); ++index) {
    if (index != 0U) {
      trace << '|';
    }
    trace << source_entries[index];
  }

  return trace.str();
}

[[nodiscard]] std::string build_fingerprint(const PolicyLoaderInputs& inputs) {
  std::ostringstream stream;
  stream << inputs.profile_id << ';'
         << bool_to_string(inputs.enabled) << ';'
         << inputs.mode_name << ';'
         << bool_to_string(inputs.hot_reload) << ';'
         << inputs.max_history_snapshots << ';'
         << inputs.default_effect_name << ';'
         << inputs.priority_order << ';'
         << bool_to_string(inputs.require_checksum) << ';'
         << bool_to_string(inputs.dry_run_required) << ';'
         << inputs.safe_mode_threshold << ';'
         << bool_to_string(inputs.persist_lkg);

  for (const auto& value : inputs.resolved_values) {
    stream << ';' << value.key_path << '=' << value.serialized_value << '@' << value.source_id;
  }

  const std::size_t hash_value = std::hash<std::string>{}(stream.str());
  std::ostringstream fingerprint;
  fingerprint << std::hex << hash_value;
  return fingerprint.str();
}

[[nodiscard]] PolicyRuleDescriptor make_patch_gate_rule(const PolicyLoaderInputs& inputs) {
  const PolicyMode mode = parse_mode(inputs.mode_name);
  const std::uint32_t priority = base_priority(inputs.priority_order);
  const PolicyEffect effect = (!inputs.enabled || !inputs.hot_reload)
                                  ? PolicyEffect::Deny
                                  : PolicyEffect::RequireConfirmation;
  const std::string reason_code = !inputs.enabled
                                      ? "policy_loader_disabled_fail_closed"
                                      : (inputs.hot_reload ? "policy_patch_confirmation_required"
                                                           : "policy_hot_reload_disabled");

  return PolicyRuleDescriptor{
      .rule_id = "policy-loader-admin-patch-gate",
      .domain = PolicyDomain::PolicyAdmin,
      .subject = "infra.policy",
      .action = "apply_patch",
      .target_selector = "policy:runtime_patch",
      .effect = effect,
      .priority = priority,
      .mode = mode,
      .conditions = {
          "policy_enabled=" + bool_to_string(inputs.enabled),
          "hot_reload=" + bool_to_string(inputs.hot_reload),
          "dry_run_required=" + bool_to_string(inputs.dry_run_required),
          "require_checksum=" + bool_to_string(inputs.require_checksum),
          "safe_mode_threshold=" + std::to_string(inputs.safe_mode_threshold),
      },
      .reason_code = reason_code,
  };
}

[[nodiscard]] PolicyRuleDescriptor make_default_rule(const PolicyLoaderInputs& inputs) {
  const PolicyMode mode = parse_mode(inputs.mode_name);
  const PolicyEffect configured_effect = parse_effect(inputs.default_effect_name);
  const PolicyEffect effect = inputs.enabled ? configured_effect : PolicyEffect::Deny;

  return PolicyRuleDescriptor{
      .rule_id = "policy-loader-default-effect",
      .domain = PolicyDomain::PolicyAdmin,
      .subject = "infra.policy",
      .action = "evaluate_default",
      .target_selector = "policy:default_effect",
      .effect = effect,
      .priority = base_priority(inputs.priority_order) + 1U,
      .mode = mode,
      .conditions = {
          "default_effect=" + inputs.default_effect_name,
          "priority_order=" + inputs.priority_order,
          "max_history_snapshots=" + std::to_string(inputs.max_history_snapshots),
          "persist_lkg=" + bool_to_string(inputs.persist_lkg),
          "profile_id=" + inputs.profile_id,
      },
      .reason_code = "policy_default_" + inputs.default_effect_name,
  };
}

[[nodiscard]] PolicyLoaderInputs resolve_inputs(const config::IConfigCenter& config_center) {
  PolicyLoaderInputs inputs;

  const ResolvedConfigValue profile_id = resolve_string(
      config_center, {"profile_meta.profile_id"}, std::string(kUnknownProfileId));
  const ResolvedConfigValue enabled = resolve_bool(
      config_center, {"infra.security_policy.enabled", "infra.security.policy.enabled"}, true);
  const ResolvedConfigValue mode = resolve_string(
      config_center, {"infra.security_policy.mode", "infra.security.policy.mode"}, "strict");
  const ResolvedConfigValue hot_reload = resolve_bool(
      config_center,
      {"infra.security_policy.hot_reload", "infra.security.policy.hot_reload"},
      true);
  const ResolvedConfigValue max_history = resolve_uint(
      config_center, {"infra.security_policy.max_history_snapshots"}, 16U);
  const ResolvedConfigValue default_effect = resolve_string(
      config_center, {"infra.security_policy.default_effect"}, "deny");
  const ResolvedConfigValue priority_order = resolve_string(
      config_center, {"infra.security_policy.priority_order"}, "deny-first");
  const ResolvedConfigValue require_checksum = resolve_bool(
      config_center, {"infra.security_policy.require_checksum"}, true);
  const ResolvedConfigValue dry_run_required = resolve_bool(
      config_center, {"infra.security_policy.dry_run_required"}, true);
  const ResolvedConfigValue safe_mode_threshold = resolve_uint(
      config_center, {"infra.security_policy.safe_mode_threshold"}, 3U);
  const ResolvedConfigValue persist_lkg = resolve_bool(
      config_center, {"infra.security_policy.snapshot.persist_lkg"}, true);

  inputs.profile_id = profile_id.serialized_value;
  inputs.enabled = enabled.serialized_value == "true";
  inputs.mode_name = normalize_mode_name(mode.serialized_value);
  inputs.hot_reload = hot_reload.serialized_value == "true";
  inputs.max_history_snapshots = static_cast<std::uint32_t>(std::stoul(max_history.serialized_value));
  inputs.default_effect_name = normalize_default_effect(default_effect.serialized_value);
  inputs.priority_order = normalize_priority_order(priority_order.serialized_value);
  inputs.require_checksum = require_checksum.serialized_value == "true";
  inputs.dry_run_required = dry_run_required.serialized_value == "true";
  inputs.safe_mode_threshold =
      static_cast<std::uint32_t>(std::stoul(safe_mode_threshold.serialized_value));
  inputs.persist_lkg = persist_lkg.serialized_value == "true";
  inputs.resolved_values = {
      profile_id,
      enabled,
      mode,
      hot_reload,
      max_history,
      default_effect,
      priority_order,
      require_checksum,
      dry_run_required,
      safe_mode_threshold,
      persist_lkg,
  };
  return inputs;
}

}  // namespace

PolicyLoader::PolicyLoader(const config::IConfigCenter& config_center)
    : config_center_(config_center) {}

PolicyBundle PolicyLoader::load_from_sources() {
  const PolicyLoaderInputs inputs = resolve_inputs(config_center_);
  const std::string fingerprint = build_fingerprint(inputs);

  return PolicyBundle{
      .bundle_id = "policy-bundle-" + inputs.profile_id + '-' + fingerprint,
      .schema_version = std::string(kPolicySchemaVersion),
      .source = build_source_trace(inputs),
      .checksum = "loader-hash:" + fingerprint,
      .rules = {
          make_patch_gate_rule(inputs),
          make_default_rule(inputs),
      },
      .generated_at = make_generated_at(),
  };
}

}  // namespace dasall::infra::policy