#include "config/ConfigValidator.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/ConfigErrors.h"

namespace dasall::infra::config {
namespace {

[[nodiscard]] ConfigValidationResult make_failure(ConfigErrorCode code,
                                                  std::string message,
                                                  std::string stage,
                                                  std::string source_ref,
                                                  ConfigValidationReport report = {}) {
  const ConfigErrorMapping mapping = map_config_error_code(code);
  return ConfigValidationResult::failure(mapping.result_code,
                                         std::string(config_error_code_name(code)) + ": " +
                                             std::move(message),
                                         std::move(stage),
                                         std::move(source_ref),
                                         std::move(report));
}

void append_issue(ConfigValidationReport& report,
                  std::string key_path,
                  std::string code,
                  ValidationSeverity severity,
                  std::string message) {
  report.issues.push_back(ValidationIssue{
      .key_path = std::move(key_path),
      .code = std::move(code),
      .severity = severity,
      .message = std::move(message),
  });
}

[[nodiscard]] std::optional<ConfigValueType> expected_type_for_key(const std::string_view& key_path) {
  if (key_path == "infra.config.watch.enabled" ||
      key_path == "infra.config.cache.stale_read_allowed" ||
      key_path == "infra.config.validation.strict" ||
      key_path == "infra.config.runtime_patch.enabled" ||
      key_path == "infra.config.rollback.enabled" ||
      key_path == "infra.config.source.external.enabled") {
    return ConfigValueType::Boolean;
  }

  if (key_path == "infra.config.watch.debounce_ms" || key_path == "infra.config.cache.ttl_ms" ||
      key_path == "infra.config.source.external.timeout_ms") {
    return ConfigValueType::UnsignedInteger;
  }

  if (key_path == "infra.config.runtime_patch.allowlist") {
    return ConfigValueType::StringList;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<bool> parse_bool(const std::string_view& serialized_value) {
  if (serialized_value == "true") {
    return true;
  }

  if (serialized_value == "false") {
    return false;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> parse_unsigned_integer(
    const std::string_view& serialized_value) {
  std::uint64_t value = 0;
  const auto* begin = serialized_value.data();
  const auto* end = serialized_value.data() + serialized_value.size();
  const auto [ptr, error] = std::from_chars(begin, end, value);
  if (error != std::errc() || ptr != end) {
    return std::nullopt;
  }

  return value;
}

[[nodiscard]] std::vector<std::string> parse_string_list(const std::string_view& serialized_value) {
  std::vector<std::string> values;
  if (serialized_value.size() < 2U || serialized_value.front() != '[' ||
      serialized_value.back() != ']') {
    return values;
  }

  std::string current;
  for (std::size_t index = 1; index + 1 < serialized_value.size(); ++index) {
    const char ch = serialized_value[index];
    if (ch == ',') {
      if (!current.empty()) {
        values.push_back(current);
        current.clear();
      }
      continue;
    }

    if (ch == ' ' || ch == '\t') {
      continue;
    }

    current.push_back(ch);
  }

  if (!current.empty()) {
    values.push_back(current);
  }

  return values;
}

[[nodiscard]] const TypedConfig* find_entry(const ConfigSnapshot& snapshot,
                                            const std::string_view& key_path) {
  const auto entry = std::find_if(snapshot.data.begin(), snapshot.data.end(), [&](const TypedConfig& candidate) {
    return candidate.key_path == key_path;
  });
  if (entry == snapshot.data.end()) {
    return nullptr;
  }

  return &(*entry);
}

[[nodiscard]] ConfigErrorCode error_code_for_issue(const std::string_view& issue_code) {
  if (issue_code.starts_with("cfg_type_")) {
    return ConfigErrorCode::TypeMismatch;
  }

  if (issue_code.starts_with("cfg_conflict_")) {
    return ConfigErrorCode::Conflict;
  }

  if (issue_code.starts_with("cfg_patch_denied_")) {
    return ConfigErrorCode::ApplyRejected;
  }

  if (issue_code.starts_with("cfg_patch_missing_")) {
    return ConfigErrorCode::NotFound;
  }

  return ConfigErrorCode::InvalidSchema;
}

void validate_entry_type(const TypedConfig& entry, ConfigValidationReport& report) {
  const std::optional<ConfigValueType> expected_type = expected_type_for_key(entry.key_path);
  if (!expected_type.has_value()) {
    return;
  }

  if (entry.value_type != *expected_type) {
    append_issue(report,
                 entry.key_path,
                 "cfg_type_mismatch",
                 ValidationSeverity::Error,
                 "typed config entry does not match the frozen config value type");
  }
}

void validate_entry_range(const TypedConfig& entry, ConfigValidationReport& report) {
  if (entry.key_path == "infra.config.watch.debounce_ms" ||
      entry.key_path == "infra.config.cache.ttl_ms" ||
      entry.key_path == "infra.config.source.external.timeout_ms") {
    const std::optional<std::uint64_t> value = parse_unsigned_integer(entry.serialized_value);
    if (!value.has_value() || *value == 0U) {
      append_issue(report,
                   entry.key_path,
                   "cfg_range_non_positive",
                   ValidationSeverity::Error,
                   "config range-constrained keys must stay above zero");
    }
  }
}

void validate_mutual_exclusion(const ConfigSnapshot& snapshot, ConfigValidationReport& report) {
  const TypedConfig* runtime_patch_enabled =
      find_entry(snapshot, "infra.config.runtime_patch.enabled");
  const TypedConfig* runtime_patch_allowlist =
      find_entry(snapshot, "infra.config.runtime_patch.allowlist");
  if (runtime_patch_enabled == nullptr || runtime_patch_allowlist == nullptr) {
    return;
  }

  const std::optional<bool> enabled = parse_bool(runtime_patch_enabled->serialized_value);
  const std::vector<std::string> allowlist = parse_string_list(runtime_patch_allowlist->serialized_value);
  if (enabled.has_value() && !*enabled && !allowlist.empty()) {
    append_issue(report,
                 runtime_patch_allowlist->key_path,
                 "cfg_conflict_runtime_patch_allowlist",
                 ValidationSeverity::Error,
                 "runtime patch allowlist must stay empty when runtime patching is disabled");
  }
}

[[nodiscard]] bool key_matches_allowlist(const std::string_view& key_path,
                                         const std::vector<std::string>& allowlist) {
  if (allowlist.empty()) {
    return false;
  }

  return std::any_of(allowlist.begin(), allowlist.end(), [&](const std::string& prefix) {
    return key_path.starts_with(prefix);
  });
}

void validate_patch_entry_against_snapshot(const ConfigSnapshot& current_snapshot,
                                           const ConfigPatchEntry& patch_entry,
                                           const std::vector<std::string>& allowlist,
                                           ConfigValidationReport& report) {
  if (!key_matches_allowlist(patch_entry.key_path, allowlist)) {
    append_issue(report,
                 patch_entry.key_path,
                 "cfg_patch_denied_allowlist",
                 ValidationSeverity::Error,
                 "runtime patch key_path is outside the frozen allowlist prefixes");
    return;
  }

  const TypedConfig* current_entry = find_entry(current_snapshot, patch_entry.key_path);
  if (current_entry == nullptr) {
    append_issue(report,
                 patch_entry.key_path,
                 "cfg_patch_missing_key",
                 ValidationSeverity::Error,
                 "runtime patch key_path must exist in the current snapshot before override");
    return;
  }

  if (patch_entry.op == ConfigPatchOperation::Remove) {
    return;
  }

  if (!patch_entry.value.has_value()) {
    append_issue(report,
                 patch_entry.key_path,
                 "cfg_patch_invalid_value",
                 ValidationSeverity::Error,
                 "replace patch entries must carry a typed replacement value");
    return;
  }

  if (current_entry->value_type != patch_entry.value->value_type) {
    append_issue(report,
                 patch_entry.key_path,
                 "cfg_type_patch_mismatch",
                 ValidationSeverity::Error,
                 "runtime patch type must match the current typed config entry");
    return;
  }

  validate_entry_type(*patch_entry.value, report);
  validate_entry_range(*patch_entry.value, report);
}

}  // namespace

ConfigValidationResult ConfigValidator::validate(const ConfigSnapshot& snapshot) const {
  if (!snapshot.is_valid()) {
    ConfigValidationReport report;
    append_issue(report,
                 "snapshot",
                 "cfg_schema_invalid_snapshot",
                 ValidationSeverity::Error,
                 "config snapshot must stay structurally valid before rule validation");
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "config snapshot must stay structurally valid before rule validation",
                        "config.validate",
                        "ConfigValidator",
                        std::move(report));
  }

  ConfigValidationReport report;
  for (const auto& entry : snapshot.data) {
    validate_entry_type(entry, report);
    validate_entry_range(entry, report);
  }
  validate_mutual_exclusion(snapshot, report);

  if (report.has_errors()) {
    const ValidationIssue& issue = report.issues.front();
    return make_failure(error_code_for_issue(issue.code),
                        issue.message,
                        "config.validate",
                        issue.key_path,
                        std::move(report));
  }

  return ConfigValidationResult::success(std::move(report));
}

ConfigValidationResult ConfigValidator::validate_patch(const ConfigSnapshot& current_snapshot,
                                                       const ConfigPatch& patch) const {
  if (!current_snapshot.is_valid()) {
    ConfigValidationReport report;
    append_issue(report,
                 "current_snapshot",
                 "cfg_schema_missing_current_snapshot",
                 ValidationSeverity::Error,
                 "current snapshot must stay valid before runtime patch validation");
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "current snapshot must stay valid before runtime patch validation",
                        "config.validate_patch",
                        "ConfigValidator",
                        std::move(report));
  }

  if (!patch.targets_only_mutable_paths()) {
    ConfigValidationReport report;
    const auto protected_path = std::find_if(patch.patches.begin(),
                                             patch.patches.end(),
                                             [](const ConfigPatchEntry& patch_entry) {
                                               return is_runtime_override_protected_path(
                                                   patch_entry.key_path);
                                             });
    append_issue(report,
                 protected_path == patch.patches.end() ? std::string("patches") : protected_path->key_path,
                 "cfg_patch_denied_protected_path",
                 ValidationSeverity::Error,
                 "runtime patch must not target frozen profile or schema paths");
    return make_failure(ConfigErrorCode::ApplyRejected,
                        "runtime patch must not target frozen profile or schema paths",
                        "config.validate_patch",
                        patch.source_id,
                        std::move(report));
  }

  if (!patch.is_valid()) {
    ConfigValidationReport report;
    append_issue(report,
                 "patches",
                 "cfg_schema_invalid_patch",
                 ValidationSeverity::Error,
                 "runtime patch metadata and entries must satisfy the frozen override contract");
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "runtime patch metadata and entries must satisfy the frozen override contract",
                        "config.validate_patch",
                        patch.source_id,
                        std::move(report));
  }

  if (current_snapshot.version != patch.base_version) {
    ConfigValidationReport report;
    append_issue(report,
                 "base_version",
                 "cfg_conflict_base_version",
                 ValidationSeverity::Error,
                 "runtime patch base_version must match the current snapshot version");
    return make_failure(ConfigErrorCode::Conflict,
                        "runtime patch base_version must match the current snapshot version",
                        "config.validate_patch",
                        patch.source_id,
                        std::move(report));
  }

  const TypedConfig* runtime_patch_enabled =
      find_entry(current_snapshot, "infra.config.runtime_patch.enabled");
  const bool patching_enabled = runtime_patch_enabled == nullptr
                                    ? false
                                    : parse_bool(runtime_patch_enabled->serialized_value).value_or(false);
  if (!patching_enabled) {
    ConfigValidationReport report;
    append_issue(report,
                 "infra.config.runtime_patch.enabled",
                 "cfg_patch_denied_disabled",
                 ValidationSeverity::Error,
                 "runtime patch validation requires infra.config.runtime_patch.enabled=true");
    return make_failure(ConfigErrorCode::ApplyRejected,
                        "runtime patch validation requires infra.config.runtime_patch.enabled=true",
                        "config.validate_patch",
                        patch.source_id,
                        std::move(report));
  }

  const TypedConfig* allowlist_entry =
      find_entry(current_snapshot, "infra.config.runtime_patch.allowlist");
  const std::vector<std::string> allowlist = allowlist_entry == nullptr
                                                 ? std::vector<std::string>{}
                                                 : parse_string_list(allowlist_entry->serialized_value);

  ConfigValidationReport report;
  for (const auto& patch_entry : patch.patches) {
    validate_patch_entry_against_snapshot(current_snapshot, patch_entry, allowlist, report);
  }

  if (report.has_errors()) {
    const ValidationIssue& issue = report.issues.front();
    return make_failure(error_code_for_issue(issue.code),
                        issue.message,
                        "config.validate_patch",
                        patch.source_id,
                        std::move(report));
  }

  return ConfigValidationResult::success(std::move(report));
}

}  // namespace dasall::infra::config